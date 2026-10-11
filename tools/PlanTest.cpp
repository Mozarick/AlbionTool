// Proves the two rules CityBuyer::Plan is now expected to obey, with made-up
// orders rather than a live market:
//
//   1. allowedTiers is a hard gate. A tier outside it is never planned, no
//      matter how fat the margin is.
//   2. Lots come back ranked by total profit, biggest first - which is also
//      the order CityBuyer::Run buys them in.
//
// Nothing here clicks anything; Plan() is pure arithmetic.

#include "../SFML/Headers/CityBuyer.h"

#include <iostream>
using namespace std;

static int failures = 0;

static void Check(bool ok, const string& what) {
    cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) {
        ++failures;
    }
}

// A city sell offer.
static FeedOrder Offer(long long id, const string& base, int tier,
    long long price, int amount) {

    FeedOrder o;
    o.id = id;
    o.itemTypeId = base;
    o.groupTypeId = base;
    o.locationId = "3005";
    o.price = price;
    o.amount = amount;
    o.tier = tier;
    o.enchant = 0;
    o.quality = 1;
    o.isBuyOrder = false;
    return o;
}

// A black market buy order for the same thing.
static FeedOrder Bid(long long id, const string& base, int tier,
    long long price, int amount) {

    FeedOrder o = Offer(id, base, tier, price, amount);
    o.locationId = "3003";
    o.isBuyOrder = true;
    return o;
}

// The same, at a chosen quality.
static FeedOrder OfferQ(long long id, const string& base, int tier,
    long long price, int amount, int quality) {

    FeedOrder o = Offer(id, base, tier, price, amount);
    o.quality = quality;
    return o;
}

static FeedOrder BidQ(long long id, const string& base, int tier,
    long long price, int amount, int quality) {

    FeedOrder o = Bid(id, base, tier, price, amount);
    o.quality = quality;
    return o;
}

int main() {
    ItemTable items;
    items.Load("../SFML/items.tsv");

    MarketFeed feed;           // never started: Plan() does not touch it
    CityBuyer buyer(feed, items);

    // T6 is the better deal per silver by a mile; T8 earns more in total.
    // Buy 1000 -> sells 5000 net 4800, profit 3800 each, 10 of them.
    // Buy 90000 -> sells 130000 net 124800, profit 34800 each, 2 of them.
    vector<FeedOrder> offers;
    offers.push_back(Offer(1, "T6_2H_DUALSWORD", 6, 1000, 10));
    offers.push_back(Offer(2, "T8_HEAD_PLATE_SET1", 8, 90000, 2));

    vector<FeedOrder> bids;
    bids.push_back(Bid(101, "T6_2H_DUALSWORD", 6, 5000, 10));
    bids.push_back(Bid(102, "T8_HEAD_PLATE_SET1", 8, 130000, 2));

    // ---- 1. no restriction: both plan, ranked by total profit ----
    {
        BuyPlan settings;
        settings.minProfitRate = 0.20;

        vector<FlipLot> lots = buyer.Plan(bids, offers, settings);

        cout << "unrestricted:\n";
        for (const FlipLot& lot : lots) {
            cout << "    T" << lot.tier << " x" << lot.quantity
                << " profit " << lot.profit << " spend " << lot.spend << "\n";
        }

        Check(lots.size() == 2, "both tiers planned when nothing is restricted");
        if (lots.size() == 2) {
            Check(lots[0].tier == 8,
                "biggest total profit first (T8 69600 before T6 38000)");
            Check(lots[0].profit >= lots[1].profit, "ranked descending");

            // The old ranking was profit per silver, which would have put T6
            // first: 380% against 38%.
            double t6Rate = double(lots[1].profit) / double(lots[1].spend);
            double t8Rate = double(lots[0].profit) / double(lots[0].spend);
            Check(t6Rate > t8Rate,
                "and it is NOT ranked per silver (T6 has the better rate)");
        }
    }

    // ---- 2. T8 only: T6 must not appear, however good it looks ----
    {
        BuyPlan settings;
        settings.minProfitRate = 0.20;
        settings.allowedTiers.insert(8);

        vector<FlipLot> lots = buyer.Plan(bids, offers, settings);

        cout << "allowedTiers = {8}:\n";
        for (const FlipLot& lot : lots) {
            cout << "    T" << lot.tier << " x" << lot.quantity
                << " profit " << lot.profit << "\n";
        }

        Check(lots.size() == 1, "only one lot survives the tier gate");
        bool anyT6 = false;
        for (const FlipLot& lot : lots) {
            if (lot.tier != 8) {
                anyT6 = true;
            }
        }
        Check(!anyT6, "no tier other than 8 is planned");
    }

    // ---- 3. a tier nothing was scanned for plans nothing at all ----
    {
        BuyPlan settings;
        settings.minProfitRate = 0.20;
        settings.allowedTiers.insert(7);

        vector<FlipLot> lots = buyer.Plan(bids, offers, settings);
        Check(lots.empty(), "allowedTiers = {7} plans nothing");
    }

    // ---- 4. one bid is one bid, however many qualities can fill it ----
    //
    // The black market wants 10. The city has 10 of the item in each of three
    // quality bands, and all three can fill that one order - anything at or
    // above the quality asked for is accepted. Planning 10 from each band is
    // planning to sell 30 into an order that takes 10: the other 20 have no
    // buyer at all, and the profit column adds up their imaginary revenue.
    {
        vector<FeedOrder> qOffers;
        qOffers.push_back(OfferQ(201, "T7_2H_DUALSWORD", 7, 50000, 10, 1));
        qOffers.push_back(OfferQ(202, "T7_2H_DUALSWORD", 7, 51000, 10, 2));
        qOffers.push_back(OfferQ(203, "T7_2H_DUALSWORD", 7, 52000, 10, 3));

        vector<FeedOrder> qBids;
        qBids.push_back(BidQ(301, "T7_2H_DUALSWORD", 7, 100000, 10, 1));

        BuyPlan settings;
        settings.minProfitRate = 0.20;

        vector<FlipLot> lots = buyer.Plan(qBids, qOffers, settings);

        int planned = 0;
        long long profit = 0;
        cout << "one 10-unit bid against three quality bands of supply:\n";
        for (const FlipLot& lot : lots) {
            cout << "    q" << lot.quality << " x" << lot.quantity
                << " profit " << lot.profit << "\n";
            planned += lot.quantity;
            profit += lot.profit;
        }

        Check(planned == 10,
            "plans the 10 units the bid asked for, not 10 per quality band");

        // The cheapest band is the one that should get to fill it.
        Check(profit <= (96000 - 50000) * 10,
            "and claims only the profit those 10 units can really earn");
    }

    // ---- 5. every bid counts, not just the dearest one ----
    //
    // Two standing orders for the same item. Pricing against the richest and
    // throwing the rest away caps the lot at 5 units, which leaves silver
    // unspent that had a real buyer waiting for it.
    {
        vector<FeedOrder> mOffers;
        mOffers.push_back(Offer(401, "T7_HEAD_PLATE_SET1", 7, 50000, 20));

        vector<FeedOrder> mBids;
        mBids.push_back(Bid(501, "T7_HEAD_PLATE_SET1", 7, 100000, 5));
        mBids.push_back(Bid(502, "T7_HEAD_PLATE_SET1", 7, 90000, 5));

        BuyPlan settings;
        settings.minProfitRate = 0.20;

        vector<FlipLot> lots = buyer.Plan(mBids, mOffers, settings);

        int planned = 0;
        cout << "two bids of 5 units each:\n";
        for (const FlipLot& lot : lots) {
            cout << "    x" << lot.quantity << " BM pays " << lot.bmPrice
                << " profit " << lot.profit << "\n";
            planned += lot.quantity;
        }

        Check(planned == 10, "both bids are filled, not just the dearest");
        if (lots.size() == 1) {
            // 5 at net 96000 and 5 at net 86400, against 50000 each.
            Check(lots[0].profit == (96000 - 50000) * 5 + (86400 - 50000) * 5,
                "profit is priced against the bid each unit actually fills");
        }
    }

    // ---- 6. a lot trimmed to the budget is re-costed on both sides ----
    //
    // Supply: 5 at 50000 then 5 at 60000. Demand: 5 at 100000 then 5 at 90000.
    // The whole lot is 10 units for 550000, and the cheap units are the ones
    // paired with the dear orders. A budget that only reaches the first five
    // therefore keeps the BEST five, not five average ones - so the trimmed
    // lot earns 480000 - 250000, not the whole lot's average margin applied
    // five times.
    {
        vector<FeedOrder> tOffers;
        tOffers.push_back(Offer(601, "T7_HEAD_PLATE_SET1", 7, 50000, 5));
        tOffers.push_back(Offer(602, "T7_HEAD_PLATE_SET1", 7, 60000, 5));

        vector<FeedOrder> tBids;
        tBids.push_back(Bid(701, "T7_HEAD_PLATE_SET1", 7, 100000, 5));
        tBids.push_back(Bid(702, "T7_HEAD_PLATE_SET1", 7, 90000, 5));

        BuyPlan whole;
        whole.minProfitRate = 0.20;
        vector<FlipLot> full = buyer.Plan(tBids, tOffers, whole);

        Check(full.size() == 1 && full[0].quantity == 10,
            "untrimmed, the lot spans both offers and both orders");
        if (full.size() == 1) {
            Check(full[0].spend == 550000, "untrimmed spend is 550000");
            Check(full[0].profit == 912000 - 550000,
                "untrimmed profit sums each pairing");
        }

        BuyPlan tight;
        tight.minProfitRate = 0.20;
        tight.budget = 300000;
        vector<FlipLot> cut = buyer.Plan(tBids, tOffers, tight);

        cout << "budget 300000:\n";
        for (const FlipLot& lot : cut) {
            cout << "    x" << lot.quantity << " spend " << lot.spend
                << " BM pays " << lot.bmPrice
                << " profit " << lot.profit << "\n";
        }

        Check(cut.size() == 1, "one trimmed lot survives the budget");
        if (cut.size() == 1) {
            Check(cut[0].quantity == 5, "trimmed to what the budget reaches");
            Check(cut[0].spend == 250000, "and costed at the cheap end only");
            Check(cut[0].maxBuyPrice == 50000,
                "the ceiling comes down with it");
            Check(cut[0].bmPrice == 100000,
                "the units kept are the ones the dearest order takes");
            Check(cut[0].profit == 480000 - 250000,
                "so the trimmed profit is priced off that order, not an average");
        }
    }

    // ---- 7. the two thresholds, and how AND and OR differ ----
    //
    // Two deals that fail in opposite directions against a 30% / 100000 gate:
    //
    //   tiny  10 at 1000 -> 5000   rate 380%, profit  38000  (rate yes, silver no)
    //   fat   20 at 100000 -> 130000  rate 24.8%, profit 496000  (rate no, silver yes)
    //
    // AND takes neither. OR takes both. Nothing else distinguishes them, which
    // is the point: the percentage on its own cannot say "and make it worth
    // the clicking", and the silver floor on its own cannot say "and do not
    // tie up my whole budget for 3%".
    {
        vector<FeedOrder> gOffers;
        gOffers.push_back(Offer(801, "T6_2H_DUALSWORD", 6, 1000, 10));
        gOffers.push_back(Offer(802, "T8_HEAD_PLATE_SET1", 8, 100000, 20));

        vector<FeedOrder> gBids;
        gBids.push_back(Bid(901, "T6_2H_DUALSWORD", 6, 5000, 10));
        gBids.push_back(Bid(902, "T8_HEAD_PLATE_SET1", 8, 130000, 20));

        // Sanity: with no silver floor the percentage decides alone and only
        // the tiny one clears 30%.
        {
            BuyPlan rateOnly;
            rateOnly.minProfitRate = 0.30;

            vector<FlipLot> lots = buyer.Plan(gBids, gOffers, rateOnly);
            Check(lots.size() == 1 && lots[0].tier == 6,
                "no silver floor: the percentage decides alone, as before");
        }

        {
            BuyPlan both;
            both.minProfitRate = 0.30;
            both.minProfitSilver = 100000;
            both.requireBoth = true;

            vector<FlipLot> lots = buyer.Plan(gBids, gOffers, both);
            cout << "AND (30% and 100000 silver): " << lots.size()
                << " lots\n";
            Check(lots.empty(),
                "AND takes neither: each one fails the other threshold");
        }

        {
            BuyPlan either;
            either.minProfitRate = 0.30;
            either.minProfitSilver = 100000;
            either.requireBoth = false;

            vector<FlipLot> lots = buyer.Plan(gBids, gOffers, either);
            cout << "OR (30% or 100000 silver):\n";
            for (const FlipLot& lot : lots) {
                cout << "    T" << lot.tier << " x" << lot.quantity
                    << " profit " << lot.profit << "\n";
            }

            Check(lots.size() == 2, "OR takes both");
            if (lots.size() == 2) {
                Check(lots[0].profit == 496000 && lots[1].profit == 38000,
                    "and still ranks them biggest profit first, as bought");
            }
        }
    }

    // ---- 8. a silver floor holds after the budget trims a lot ----
    //
    // The lot below is worth 230000 whole and 30000 once the budget cuts it to
    // one unit. A floor of 100000 asked for lots worth 100000, not slices of
    // them, so the trimmed version must not be bought.
    {
        vector<FeedOrder> sOffers;
        sOffers.push_back(Offer(1001, "T8_HEAD_PLATE_SET1", 8, 50000, 5));

        vector<FeedOrder> sBids;
        sBids.push_back(Bid(1101, "T8_HEAD_PLATE_SET1", 8, 100000, 5));

        BuyPlan gate;
        gate.minProfitRate = 0.20;
        gate.minProfitSilver = 100000;
        gate.requireBoth = true;

        vector<FlipLot> whole = buyer.Plan(sBids, sOffers, gate);
        Check(whole.size() == 1 && whole[0].profit == (96000 - 50000) * 5,
            "untrimmed, the lot clears the 100000 floor");

        gate.budget = 50000;      // reaches exactly one unit
        vector<FlipLot> cut = buyer.Plan(sBids, sOffers, gate);
        cout << "budget 50000 against a 100000 silver floor: " << cut.size()
            << " lots\n";
        Check(cut.empty(),
            "trimmed to one unit it is worth 46000, so it is not bought");
    }

    // ---- 9. a cheaper high quality is preferred, and the rest stay reachable ----
    //
    // The black market wants Good (q2). The city has:
    //     q2 Good        at 60000
    //     q3 Outstanding at 50000   <- cheaper than the Good it can substitute for
    //     q4 Excellent   at 55000   <- between the two
    //
    // The plan must buy the Outstanding, because a q3 fills a q2 order and costs
    // less. But the buying run has to be able to fall back: if that Outstanding
    // is taken by someone else, the Excellent is still cheaper than the Good, and
    // if that goes too the Good itself is still worth buying. So the lot has to
    // carry the whole ranked list, not just the band that won.
    {
        vector<FeedOrder> subOffers;
        subOffers.push_back(OfferQ(1201, "T7_2H_DUALSWORD", 7, 60000, 4, 2));
        subOffers.push_back(OfferQ(1202, "T7_2H_DUALSWORD", 7, 50000, 4, 3));
        subOffers.push_back(OfferQ(1203, "T7_2H_DUALSWORD", 7, 55000, 4, 4));

        vector<FeedOrder> subBids;
        subBids.push_back(BidQ(1301, "T7_2H_DUALSWORD", 7, 100000, 4, 2));

        BuyPlan settings;
        settings.minProfitRate = 0.20;

        vector<FlipLot> lots = buyer.Plan(subBids, subOffers, settings);

        cout << "BM wants q2; city has q2@60000, q3@50000, q4@55000:\n";
        for (const FlipLot& lot : lots) {
            cout << "    planned q" << lot.quality << " x" << lot.quantity
                 << "  fallback order:";
            for (int q : lot.qualityOrder) {
                cout << " q" << q;
            }
            cout << "  ceiling " << lot.marginCeiling << "\n";
        }

        Check(lots.size() == 1, "one lot: the order is only worth filling once");
        if (lots.size() == 1) {
            const FlipLot& lot = lots[0];

            Check(lot.quality == 3,
                "buys the Outstanding, not the Good - it fills the same order for less");

            // q2 is the order's own quality, so q2, q3 and q4 all fill it.
            Check(lot.qualityOrder.size() == 3,
                "all three bands that can fill a q2 order are kept");
            if (lot.qualityOrder.size() == 3) {
                Check(lot.qualityOrder[0] == 3, "planned band first (q3, 50000)");
                Check(lot.qualityOrder[1] == 4, "then the next cheapest (q4, 55000)");
                Check(lot.qualityOrder[2] == 2, "then the Good itself (q2, 60000)");
            }

            // net 96000 at 20% -> 80000. Every band above is under that, so the
            // run can reach all of them; maxBuyPrice (50000) alone could not.
            Check(lot.marginCeiling == 80000,
                "fallback ceiling is the margin limit, not the planned band's top");
            Check(lot.marginCeiling > lot.maxBuyPrice,
                "and it is above maxBuyPrice, or no fallback would ever be tried");
        }
    }

    // ---- 10. a band that cannot fill the order is never offered as fallback ----
    //
    // The order asks for Excellent (q4). Normal and Good cannot fill it at any
    // price, so however cheap they are they must not appear in the list - buying
    // one would leave a stack with no buyer.
    {
        vector<FeedOrder> hiOffers;
        hiOffers.push_back(OfferQ(1401, "T7_HEAD_PLATE_SET1", 7,  10000, 4, 1));
        hiOffers.push_back(OfferQ(1402, "T7_HEAD_PLATE_SET1", 7,  12000, 4, 2));
        hiOffers.push_back(OfferQ(1403, "T7_HEAD_PLATE_SET1", 7,  60000, 4, 4));
        hiOffers.push_back(OfferQ(1404, "T7_HEAD_PLATE_SET1", 7,  58000, 4, 5));

        vector<FeedOrder> hiBids;
        hiBids.push_back(BidQ(1501, "T7_HEAD_PLATE_SET1", 7, 100000, 4, 4));

        BuyPlan settings;
        settings.minProfitRate = 0.20;

        vector<FlipLot> lots = buyer.Plan(hiBids, hiOffers, settings);

        cout << "BM wants q4; cheap q1/q2 exist but cannot fill it:\n";
        for (const FlipLot& lot : lots) {
            cout << "    planned q" << lot.quality << "  fallback order:";
            for (int q : lot.qualityOrder) {
                cout << " q" << q;
            }
            cout << "\n";
        }

        Check(lots.size() == 1, "only the bands that fit produce a lot");
        if (lots.size() == 1) {
            const FlipLot& lot = lots[0];
            Check(lot.quality == 5,
                "the Masterpiece at 58000 undercuts the Excellent at 60000");

            bool tooLow = false;
            for (int q : lot.qualityOrder) {
                if (q < 4) {
                    tooLow = true;
                }
            }
            Check(!tooLow, "no band below the order's own quality is offered");
            Check(lot.qualityOrder.size() == 2, "just q5 and q4");
        }
    }

    cout << (failures == 0 ? "\nPlanTest PASSED\n" : "\nPlanTest FAILED\n");
    return failures == 0 ? 0 : 1;
}
