// Proves the rules the hand-shopping list is expected to obey, with made-up
// lots rather than a live market:
//
//   1. Lots that would be typed into the same search - same item, tier and
//      enchantment - become ONE block, and their quantities and profits add
//      up.
//   2. When a block merges several quality bands it advertises the LOWEST
//      quality and the LOWEST price ceiling. Anything else invites a purchase
//      that cannot be sold at a profit.
//   3. Blocks are ranked by block profit, not by the profit of any one lot
//      inside them.
//   4. CityBuyer::Shortfall re-costs what a run did not buy from the DEAREST
//      end of the offer ladder and the CHEAPEST end of the order ladder,
//      because that is the part a cheapest-first run leaves behind.
//
// Nothing here clicks anything.

#include "../SFML/Headers/ShopList.h"

#include <iostream>
using namespace std;

static int failures = 0;

static void Check(bool ok, const string& what) {
    cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) {
        ++failures;
    }
}

// A planned lot. Only the fields BuildShopList reads are filled in.
static FlipLot Lot(const string& id, int tier, int enchant, int quality,
    int bmQuality, int quantity, long long ceiling, long long profit,
    long long spend) {

    FlipLot lot;
    lot.itemTypeId = id;
    lot.displayName = id;
    lot.searchText = id + " T" + to_string(tier) + "." + to_string(enchant);
    lot.tier = tier;
    lot.enchant = enchant;
    lot.quality = quality;
    lot.bmQuality = bmQuality;
    lot.quantity = quantity;
    lot.marginCeiling = ceiling;
    lot.profit = profit;
    lot.spend = spend;
    return lot;
}


static void MergesSameSearch() {
    cout << "one block per search\n";

    vector<FlipLot> lots;
    lots.push_back(Lot("SWORD", 6, 0, 2, 2, 10, 12450, 400000, 100000));
    lots.push_back(Lot("SWORD", 6, 0, 4, 4, 9, 20100, 500000, 150000));
    lots.push_back(Lot("SWORD", 6, 1, 1, 1, 5, 9000, 90000, 40000));

    vector<ShopBlock> blocks = BuildShopList(lots);

    Check(blocks.size() == 3 - 1, "two lots of SWORD T6.0 merge, T6.1 stays apart");
    if (blocks.size() != 2) {
        return;
    }

    // Ranked by block profit: the merged block is worth 900k, the other 90k.
    const ShopBlock& merged = blocks[0];
    Check(merged.enchant == 0, "the merged block is the enchant 0 one");
    Check(merged.bands == 2, "it records that two bands were merged");
    Check(merged.quantity == 19, "quantities add up");
    Check(merged.profit == 900000, "profits add up");
    Check(merged.spend == 250000, "spend adds up");
    Check(merged.searchText == "SWORD T6.0", "search text is what gets typed");
}

static void MergedBlockIsConservative() {
    cout << "a merged block advertises the safe figures\n";

    vector<FlipLot> lots;
    // An order wanting Excellent pays more, so its ceiling is higher - but a
    // Good item cannot fill it. Advertising Good quality next to the
    // Excellent ceiling would invite buying a Good at 18,000 that can only be
    // sold into the 12,450 order.
    lots.push_back(Lot("BOW", 4, 2, 4, 4, 9, 20100, 500000, 150000));
    lots.push_back(Lot("BOW", 4, 2, 2, 2, 10, 12450, 400000, 100000));

    vector<ShopBlock> blocks = BuildShopList(lots);
    Check(blocks.size() == 1, "one block");
    if (blocks.empty()) {
        return;
    }

    Check(blocks[0].minQuality == 2, "quality shown is the lowest accepted");
    Check(blocks[0].ceiling == 12450, "ceiling shown is the lowest, not the best");
}

static void RankedByBlock() {
    cout << "ranking is by block, not by lot\n";

    vector<FlipLot> lots;
    // One fat lot, against two middling lots of the same item that together
    // beat it. Ranking per lot would put SOLO on top; ranking per block must
    // not.
    lots.push_back(Lot("SOLO", 5, 0, 1, 1, 4, 5000, 600000, 90000));
    lots.push_back(Lot("PAIR", 5, 0, 2, 2, 4, 5000, 400000, 50000));
    lots.push_back(Lot("PAIR", 5, 0, 4, 4, 4, 6000, 350000, 50000));

    vector<ShopBlock> blocks = BuildShopList(lots);
    Check(blocks.size() == 2, "two blocks");
    if (blocks.size() != 2) {
        return;
    }
    Check(blocks[0].displayName == "PAIR",
        "the 750k pair outranks the 600k single lot");
    Check(blocks[1].displayName == "SOLO", "and the single lot is second");
}

static void SkipsEmptyLots() {
    cout << "a lot with nothing in it is not a shopping entry\n";

    vector<FlipLot> lots;
    lots.push_back(Lot("GHOST", 3, 0, 1, 1, 0, 1000, 0, 0));
    lots.push_back(Lot("REAL", 3, 0, 1, 1, 2, 1000, 5000, 1000));

    vector<ShopBlock> blocks = BuildShopList(lots);
    Check(blocks.size() == 1, "the zero-quantity lot is dropped");
    if (!blocks.empty()) {
        Check(blocks[0].displayName == "REAL", "the real one survives");
    }
}

static void ShortfallKeepsTheDearEnd() {
    cout << "shortfall re-costs from the tail of both ladders\n";

    // 4 units planned. Offers 100, 200, 300, 400 - bought cheapest first.
    // Orders 1000, 900, 800, 700 - filled dearest first.
    FlipLot lot = Lot("AXE", 5, 0, 1, 1, 4, 700, 0, 1000);
    lot.ladder.push_back(make_pair(100, 1));
    lot.ladder.push_back(make_pair(200, 1));
    lot.ladder.push_back(make_pair(300, 1));
    lot.ladder.push_back(make_pair(400, 1));
    lot.revenueLadder.push_back(make_pair(1000, 1));
    lot.revenueLadder.push_back(make_pair(900, 1));
    lot.revenueLadder.push_back(make_pair(800, 1));
    lot.revenueLadder.push_back(make_pair(700, 1));

    vector<FlipLot> lots;
    lots.push_back(lot);

    // The run got 2 of the 4, so the two it missed are the 300 and 400 offers
    // paired with the 800 and 700 orders.
    vector<int> unbought;
    unbought.push_back(2);

    vector<FlipLot> left = CityBuyer::Shortfall(lots, unbought);
    Check(left.size() == 1, "one lot comes back");
    if (left.empty()) {
        return;
    }

    Check(left[0].quantity == 2, "two units left");
    Check(left[0].spend == 700, "spend is 300+400, not the cheap end");
    Check(left[0].avgBuyPrice == 350, "average is of the dear end");
    Check(left[0].maxBuyPrice == 400, "dearest offer left is 400");

    // 800 and 700 gross 1500; after the 4% tax that is 768 + 672 = 1440.
    Check(left[0].bmPrice == 750, "bm price averages the two cheapest orders");
    Check(left[0].profit == 1440 - 700, "profit is net of tax on what is left");

    // The cheapest order the lot fills is in the tail, and the tail is what
    // survives, so the per-unit ceiling is unchanged by the trim.
    Check(left[0].marginCeiling == 700, "margin ceiling carries over");
}

static void ShortfallDropsWhatWasBought() {
    cout << "a lot bought in full is not on the list\n";

    FlipLot lot = Lot("PIKE", 5, 0, 1, 1, 3, 500, 900, 300);
    lot.ladder.push_back(make_pair(100, 3));
    lot.revenueLadder.push_back(make_pair(500, 3));

    vector<FlipLot> lots;
    lots.push_back(lot);

    vector<int> none;
    none.push_back(0);
    Check(CityBuyer::Shortfall(lots, none).empty(), "nothing missing, nothing listed");

    // Nothing bought at all: the lot is its own shortfall, untouched.
    vector<int> all;
    all.push_back(3);
    vector<FlipLot> left = CityBuyer::Shortfall(lots, all);
    Check(left.size() == 1, "a lot the run never touched stays on the list");
    if (!left.empty()) {
        Check(left[0].quantity == 3 && left[0].profit == 900,
            "and its figures are untouched");
    }
}


// The list is read to decide what to buy by hand, so nothing may be quietly
// left off it - least of all the thin entries, which are the ones a person
// would otherwise never think to look for.
static void ShowsEverything() {
    cout << "nothing is left off the list\n";

    // A single unit is still a thing to go and buy.
    vector<FlipLot> one;
    one.push_back(Lot("LONE", 7, 0, 1, 1, 1, 900, 4200, 1100));
    vector<ShopBlock> blocks = BuildShopList(one);
    Check(blocks.size() == 1, "a one-unit lot is a block");
    if (!blocks.empty()) {
        Check(blocks[0].quantity == 1, "and it says one unit");
        Check(blocks[0].ceiling == 900, "and it keeps its price ceiling");
    }

    // No cap, no top-N: 60 distinct items in, 60 blocks out.
    vector<FlipLot> many;
    for (int i = 0; i < 60; ++i) {
        many.push_back(Lot("ITEM" + to_string(i), 4, 0, 1, 1,
            1 + (i % 3), 1000 + i, 100 + i, 50));
    }
    vector<ShopBlock> all = BuildShopList(many);
    Check(all.size() == 60, "sixty items produce sixty blocks");

    // The thinnest entry survives all the way to the bottom of the ranking.
    bool foundThinnest = false;
    for (const ShopBlock& b : all) {
        if (b.displayName == "ITEM0") { foundThinnest = true; break; }
    }
    Check(foundThinnest, "the least profitable one is still on the list");

    // And a shortfall of a single unit is still worth going back for.
    FlipLot lot = Lot("REMNANT", 5, 0, 1, 1, 3, 700, 900, 300);
    lot.ladder.push_back(make_pair(100, 3));
    lot.revenueLadder.push_back(make_pair(500, 3));
    vector<FlipLot> lots;
    lots.push_back(lot);
    vector<int> missing;
    missing.push_back(1);
    vector<FlipLot> left = CityBuyer::Shortfall(lots, missing);
    Check(left.size() == 1, "one unit missing is still a shortfall");
    if (!left.empty()) {
        Check(left[0].quantity == 1, "and it is one unit");
        Check(BuildShopList(left).size() == 1, "and it reaches the shopping list");
    }
}


int main() {
    cout << "ShopListTest\n\n";

    MergesSameSearch();
    MergedBlockIsConservative();
    RankedByBlock();
    SkipsEmptyLots();
    ShortfallKeepsTheDearEnd();
    ShortfallDropsWhatWasBought();
    ShowsEverything();

    cout << "\n" << (failures == 0 ? "ALL PASS" : "FAILURES") << ": "
        << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
