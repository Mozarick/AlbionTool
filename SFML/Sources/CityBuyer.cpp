#include "../Headers/CityBuyer.h"
#include "../Headers/AbortSwitch.h"
#include "../Headers/BlackMarket.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <iostream>
#include <map>
#include <thread>

using namespace std;


// Selling into a standing black market order costs 4%. There is no setup fee
// on this path: the bot takes existing offers rather than posting its own
// order, which would cost 2.5% of the order value whether or not it filled.
static const double BLACK_MARKET_TAX = 0.04;

// How long to wait for the market to re-send its list after a purchase, when
// it bothers to. Short on purpose: the purchase itself is confirmed by the
// game (see WaitForPurchase), so a fresher list is a nicety, not a
// requirement, and it must not cost seconds on every single unit bought.
static const int LIST_REFRESH_MS = 400;

// How many times the quality bands of one lot may be swept.
//
// One pass is not enough. A band left behind because its cheap rows ran out can
// have something new listed in it while a later band is being worked, and the
// rule this whole path exists to serve is "always take the cheapest unit that
// fills the order" - which is not a one-way walk. A pass that buys nothing
// anywhere ends the lot, so this is only an upper bound on luck, not a budget
// that gets spent.
static const int MAX_BAND_PASSES = 3;


bool QualityFills(int offered, int wanted) {
	return offered >= wanted;
}


CityBuyer::CityBuyer(MarketFeed& feedRef, const ItemTable& itemTable)
	: feed(feedRef)
	, items(itemTable)
{
}

void CityBuyer::Reset() {
	doneKeys.clear();
	currentQuality = 0;
}


// ------------------------------------------------------------------ planning

namespace {

	// Everything the city is selling for one item id + enchant + quality,
	// cheapest first.
	struct OfferGroup {
		string itemTypeId;
		int tier = 0;
		int enchant = 0;
		int quality = 0;
		vector<const FeedOrder*> offers;
	};

	struct GroupKey {
		string id;
		int enchant;
		int quality;

		bool operator< (const GroupKey& other) const {
			if (id != other.id) {
				return id < other.id;
			}
			if (enchant != other.enchant) {
				return enchant < other.enchant;
			}
			return quality < other.quality;
		}
	};

	bool CheaperFirst(const FeedOrder* a, const FeedOrder* b) {
		return a->price < b->price;
	}

	long long NetAfterTax(long long price) {
		return (long long)(price * (1.0 - BLACK_MARKET_TAX));
	}

	// One standing black market order, with the units still unclaimed by this
	// plan.
	//
	// `left` is the whole point. An order for 10 swords is demand for ten
	// swords in total, not ten of every quality band that happens to fill it -
	// and the city usually has the same item listed at three or four
	// qualities. Planning each band against the order's full amount is
	// planning to sell thirty into an order that takes ten: two thirds of the
	// units have no buyer, and their imaginary revenue is what made the Deals
	// screen promise several times what the run could actually earn.
	struct Demand {
		long long price = 0;
		int quality = 0;
		int left = 0;
	};

	bool DearestFirst(const Demand& a, const Demand& b) {
		return a.price > b.price;
	}

	// Does a lot earning `profit` silver at `rate` clear what the user asked
	// for?
	//
	// The silver floor is a total for the whole lot, matching the last column
	// of the plan table. Left at zero it is not in play at all and the
	// percentage decides alone - the behaviour of every run before the second
	// threshold existed, and the reason PlanTest's older cases still read the
	// same way.
	bool Accepts(const BuyPlan& settings, double rate, long long profit) {
		const bool byRate = rate >= settings.minProfitRate;
		if (settings.minProfitSilver <= 0) {
			return byRate;
		}

		const bool bySilver = profit >= settings.minProfitSilver;
		return settings.requireBoth ? (byRate && bySilver) : (byRate || bySilver);
	}

	// One meeting between a city offer and a black market order, worth `units`.
	struct PairStep {
		long long offerPrice = 0;
		long long bidPrice = 0;
		size_t pick = 0;          // index into the demand pool
		int units = 0;
	};

}  // namespace


vector<FlipLot> CityBuyer::Plan(const vector<FeedOrder>& blackMarketOrders,
	const vector<FeedOrder>& cityOffers, const BuyPlan& settings) const {

	// Black market demand, indexed by item + enchant and dearest first.
	//
	// EVERY order is kept, not just the best-paying one. The old code priced a
	// lot against the single richest order and discarded the rest, which threw
	// away real demand: five standing orders of five units each is a buyer for
	// twenty-five, and capping the lot at the first five leaves silver unspent
	// that had somewhere to go.
	map<pair<string, int>, vector<Demand>> requests;
	for (const FeedOrder& order : blackMarketOrders) {
		if (!order.isBuyOrder || order.price <= 0 || order.amount <= 0) {
			continue;
		}
		Demand demand;
		demand.price = order.price;
		demand.quality = order.quality;
		demand.left = order.amount;
		requests[{ order.groupTypeId, order.enchant }].push_back(demand);
	}
	for (auto& entry : requests) {
		sort(entry.second.begin(), entry.second.end(), DearestFirst);
	}

	// city sell offers, grouped by item + enchant + quality
	map<GroupKey, OfferGroup> groups;
	for (const FeedOrder& order : cityOffers) {
		if (order.isBuyOrder || order.price <= 0 || order.amount <= 0) {
			continue;
		}
		if (settings.maxUnitPrice > 0 && order.price > settings.maxUnitPrice) {
			continue;
		}

		// Only the tiers that were actually scanned for. Rejected here, at the
		// point offers enter the plan, rather than filtered out of the results
		// afterwards: a lot that was never built cannot be bought by mistake,
		// and it also keeps the "nothing worth buying" count honest.
		if (!settings.allowedTiers.empty() &&
			settings.allowedTiers.count(order.tier) == 0) {
			continue;
		}

		GroupKey key{ order.groupTypeId, order.enchant, order.quality };
		OfferGroup& group = groups[key];
		if (group.offers.empty()) {
			// The *base* id, not order.itemTypeId. The feed sends both:
			// itemTypeId carries the enchant suffix (T6_MAIN_SWORD@2) while
			// groupTypeId does not (T6_MAIN_SWORD). The enchant is already
			// kept separately in group.enchant, and every later comparison -
			// CityBuyer::Run matching live offers, BlackMarketSeller matching
			// bids against the ledger - tests against groupTypeId. Storing the
			// suffixed form here meant those tests could never match for
			// enchanted goods, so every enchanted lot reported "gone from the
			// market" and every enchanted stack was unsellable. Plain items
			// have no suffix, so the two forms are identical and they worked,
			// which is what kept this hidden.
			group.itemTypeId = order.groupTypeId;
			group.tier = order.tier;
			group.enchant = order.enchant;
			group.quality = order.quality;
		}
		group.offers.push_back(&order);
	}

	// The order the groups are planned in decides who gets the demand.
	//
	// Within one item + enchant the quality bands compete for the same standing
	// orders, and whichever is planned first claims them. Cheapest supply
	// first, so the band that can fill an order for the least silver is the one
	// that gets to - which is also the band that earns the most on it. Across
	// different items the order does not matter; sorting by id and enchant only
	// keeps the competing bands adjacent and the result reproducible.
	//
	// This is a greedy, not an optimum. A cheap high-quality band takes the
	// dearest order it can fill, which is occasionally one a lower band could
	// also have filled, leaving that band's own order unmatched - a min-cost
	// flow would pair them better. The error is always in the safe direction:
	// it plans slightly fewer units than the market could absorb, never more.
	// Not worth the machinery until a run is visibly leaving demand on the
	// table.
	vector<OfferGroup*> ordered;
	ordered.reserve(groups.size());
	for (auto& entry : groups) {
		sort(entry.second.offers.begin(), entry.second.offers.end(),
			CheaperFirst);
		ordered.push_back(&entry.second);
	}
	sort(ordered.begin(), ordered.end(),
		[](const OfferGroup* a, const OfferGroup* b) {
			if (a->itemTypeId != b->itemTypeId) {
				return a->itemTypeId < b->itemTypeId;
			}
			if (a->enchant != b->enchant) {
				return a->enchant < b->enchant;
			}
			const long long cheapestA = a->offers.front()->price;
			const long long cheapestB = b->offers.front()->price;
			if (cheapestA != cheapestB) {
				return cheapestA < cheapestB;
			}
			return a->quality < b->quality;
		});

	// Only bands of the same item/enchantment can be alternatives. Index
	// these once instead of searching all market groups for every lot.
	map<pair<string, int>, vector<const OfferGroup*>> relatedBands;
	for (const OfferGroup* group : ordered) {
		relatedBands[{group->itemTypeId, group->enchant}].push_back(group);
	}

	vector<FlipLot> lots;
	lots.reserve(groups.size());

	for (OfferGroup* groupPtr : ordered) {
		OfferGroup& group = *groupPtr;

		auto found = requests.find({ group.itemTypeId, group.enchant });
		if (found == requests.end()) {
			continue;
		}
		vector<Demand>& pool = found->second;

		// Walk the offers cheapest first and the orders dearest first, pairing
		// units off against each other, and go as deep as the units stay
		// individually worth buying.
		//
		// Both sides move now. The old sweep held the revenue side still at one
		// order's price and only ever asked how much supply it could absorb.
		//
		// Nothing is claimed in this pass. How deep to actually go is decided
		// afterwards, because the two thresholds pull against each other: every
		// extra unit adds silver to the total and takes something off the
		// margin, so the depth that best satisfies "at least this much profit"
		// is not the depth that best satisfies "at least this good a rate".
		vector<PairStep> steps;
		vector<int> claimed(pool.size(), 0);
		bool exhausted = false;

		for (const FeedOrder* offer : group.offers) {
			if (exhausted) {
				break;
			}

			int supply = offer->amount;
			while (supply > 0) {
				// Dearest order this quality can fill that still has units
				// unclaimed. The pool is sorted dearest first, so the first
				// match is the best one.
				size_t pick = pool.size();
				for (size_t i = 0; i < pool.size(); ++i) {
					if (pool[i].left - claimed[i] <= 0) {
						continue;
					}
					if (!QualityFills(group.quality, pool[i].quality)) {
						continue;
					}
					pick = i;
					break;
				}
				if (pick == pool.size()) {
					// Demand exhausted: earlier quality bands took it all, or
					// none of it accepts this quality.
					exhausted = true;
					break;
				}

				// Offers only get dearer from here and orders only cheaper, so
				// a unit that loses money is the end of the sweep, not a step
				// to skip over.
				if (NetAfterTax(pool[pick].price) <= offer->price) {
					exhausted = true;
					break;
				}

				const int room = pool[pick].left - claimed[pick];
				const int take = supply < room ? supply : room;

				PairStep step;
				step.offerPrice = offer->price;
				step.bidPrice = pool[pick].price;
				step.pick = pick;
				step.units = take;
				steps.push_back(step);

				claimed[pick] += take;
				supply -= take;
			}
		}

		// Now choose the depth. Total profit climbs while units are still
		// profitable and the margin only ever falls, so the best answer is not
		// simply "stop where the rate crosses" - under OR it can be much
		// deeper, and under AND it can be shallower than the rate alone would
		// allow. Walk the whole sweep and keep the best prefix that satisfies
		// what the user asked for.
		size_t depth = 0;
		long long bestProfit = 0;
		{
			long long runSpend = 0;
			long long runNet = 0;
			int runQty = 0;

			for (size_t k = 0; k < steps.size(); ++k) {
				runSpend += steps[k].offerPrice * steps[k].units;
				runNet += NetAfterTax(steps[k].bidPrice) * steps[k].units;
				runQty += steps[k].units;

				const long long avgBuy = runSpend / runQty;
				if (avgBuy <= 0) {
					continue;
				}
				const long long avgNet = runNet / runQty;
				if (avgNet <= avgBuy) {
					break;
				}

				const double rate = double(avgNet - avgBuy) / double(avgBuy);
				const long long profit = runNet - runSpend;

				if (!Accepts(settings, rate, profit)) {
					continue;
				}

				// Strictly better only, so a tie keeps the shallower prefix:
				// same profit for less silver committed and less clicking.
				if (profit > bestProfit) {
					bestProfit = profit;
					depth = k + 1;
				}
			}
		}

		if (depth == 0) {
			continue;
		}

		long long spend = 0;
		long long grossRevenue = 0;
		long long netRevenue = 0;
		int quantity = 0;
		long long maxPrice = 0;
		int filledQuality = 0;
		vector<pair<long long, int>> ladder;
		vector<pair<long long, int>> revenueLadder;

		for (size_t k = 0; k < depth; ++k) {
			const PairStep& step = steps[k];

			spend += step.offerPrice * step.units;
			grossRevenue += step.bidPrice * step.units;
			netRevenue += NetAfterTax(step.bidPrice) * step.units;
			quantity += step.units;
			maxPrice = step.offerPrice;

			// Reported quality is the LOWEST order filled: that is the one the
			// "buying above what was asked for" marker is about.
			if (filledQuality == 0 ||
				pool[step.pick].quality < filledQuality) {
				filledQuality = pool[step.pick].quality;
			}

			// Claimed here, once the depth is settled, so the next quality
			// band planning the same item sees demand that has really been
			// spoken for - not demand a deeper sweep merely considered.
			pool[step.pick].left -= step.units;

			// One offer can span two orders and two offers can fall to the
			// same order, so merge equal prices rather than emitting a step per
			// pairing.
			if (!ladder.empty() && ladder.back().first == step.offerPrice) {
				ladder.back().second += step.units;
			}
			else {
				ladder.push_back(make_pair(step.offerPrice, step.units));
			}

			if (!revenueLadder.empty() &&
				revenueLadder.back().first == step.bidPrice) {
				revenueLadder.back().second += step.units;
			}
			else {
				revenueLadder.push_back(make_pair(step.bidPrice, step.units));
			}
		}

		long long avg = spend / quantity;

		// No ceiling on the margin. A wide gap between what the city sells for
		// and what the black market bids is not automatically a mispairing -
		// it is what an actual bargain looks like, and the arithmetic above
		// (item + enchant matched, quality filtered, ladder swept
		// cheapest-first) is what makes it trustworthy, not the size of the
		// number it produces.
		FlipLot lot;
		lot.itemTypeId = group.itemTypeId;
		const string* name = items.BaseName(group.itemTypeId);
		lot.displayName = name != NULL ? *name : group.itemTypeId;
		lot.searchText = items.SearchString(group.itemTypeId, group.tier,
			group.enchant);
		lot.tier = group.tier;
		lot.enchant = group.enchant;
		lot.quality = group.quality;
		lot.bmQuality = filledQuality;
		lot.quantity = quantity;
		lot.avgBuyPrice = avg;
		lot.maxBuyPrice = maxPrice;
		lot.spend = spend;
		lot.bmPrice = grossRevenue / quantity;
		lot.profit = netRevenue - spend;
		lot.ladder = std::move(ladder);
		lot.revenueLadder = std::move(revenueLadder);

		// What may be paid per unit and still clear the margin, against the
		// CHEAPEST order this lot fills. Taking the cheapest rather than the
		// average makes it hold for every unit individually, whichever order
		// that unit ends up against.
		//
		// Only used when falling back to another quality band. The planned
		// band keeps maxBuyPrice, so a run that never needs to substitute
		// behaves exactly as it did before this existed.
		long long cheapestBid = 0;
		for (const auto& step : lot.revenueLadder) {
			if (cheapestBid == 0 || step.first < cheapestBid) {
				cheapestBid = step.first;
			}
		}
		const double markup = 1.0 + settings.minProfitRate;
		lot.marginCeiling = markup > 0.0
			? (long long)(NetAfterTax(cheapestBid) / markup)
			: 0;

		// Every band that could fill the same orders, cheapest supply first.
		//
		// "Could fill" is decided by the ORDER's quality, not this band's: an
		// order for Good accepts Good and everything above it. The planned band
		// leads because it was the cheapest when the plan was made, and the
		// rest follow in the order the buying run should try them.
		lot.qualityOrder.push_back(group.quality);
		{
			vector<pair<long long, int>> alternates;
			for (const OfferGroup* other : relatedBands.at({group.itemTypeId, group.enchant})) {
				if (other == groupPtr) {
					continue;
				}
				if (other->itemTypeId != group.itemTypeId ||
					other->enchant != group.enchant) {
					continue;
				}
				if (!QualityFills(other->quality, filledQuality)) {
					continue;
				}
				alternates.push_back(
					make_pair(other->offers.front()->price, other->quality));
			}
			sort(alternates.begin(), alternates.end());
			for (const auto& alt : alternates) {
				lot.qualityOrder.push_back(alt.second);
			}
		}

		lots.push_back(std::move(lot));
	}

	// Rank by total silver earned, biggest first. Buying then works down that
	// list, so the fattest deal is taken while it is still there and the small
	// change is left for last.
	//
	// This used to rank by profit per silver committed, on the reasoning that
	// silver is the binding constraint - a 300% flip tying up 2k earns less
	// than a 25% flip that absorbs 200k. That is true when the budget is the
	// only thing that runs out, but it is not what actually happens: the
	// offers themselves get taken by other players while the bot is working
	// through a list of tiny high-percentage flips, and the deal worth 200k
	// profit is exactly the one that disappears. Ties break on the thinner
	// spend, so of two lots earning the same the cheaper one goes first.
	sort(lots.begin(), lots.end(), [](const FlipLot& a, const FlipLot& b) {
		if (a.profit != b.profit) {
			return a.profit > b.profit;
		}
		return a.spend < b.spend;
	});

	// Fill the basket until the silver runs out, skipping lots that no longer
	// fit rather than stopping at the first one that does not.
	if (settings.budget > 0) {
		vector<FlipLot> affordable;
		affordable.reserve(lots.size());

		long long left = settings.budget;
		for (FlipLot& lot : lots) {
			if (lot.spend <= left) {
				left -= lot.spend;
				affordable.push_back(std::move(lot));
				continue;
			}

			// Take a partial lot if even one unit fits. Walk the real price
			// ladder cheapest-first rather than assuming every unit costs the
			// full lot's average: a trimmed lot keeps only the cheap end, so
			// its true average, ceiling and profit are all better than the
			// untrimmed figures. Costing it at the old average understated how
			// many units fit, and leaving maxBuyPrice untouched let the buying
			// run pay a price the trimmed lot never budgeted for.
			long long trimSpend = 0;
			long long trimBudget = left;
			int trimQty = 0;
			long long trimMax = 0;
			vector<pair<long long, int>> trimLadder;

			for (const auto& step : lot.ladder) {
				if (step.first <= 0) {
					continue;
				}
				long long affordableUnits = trimBudget / step.first;
				int take = step.second;
				if (affordableUnits < (long long)take) {
					take = int(affordableUnits);
				}
				if (take <= 0) {
					break;
				}

				trimSpend += step.first * take;
				trimBudget -= step.first * take;
				trimQty += take;
				trimMax = step.first;
				trimLadder.push_back(make_pair(step.first, take));
			}

			if (trimQty > 0 && trimQty < lot.quantity) {
				// Re-cost the revenue side from the real ladder as well. The
				// units that survive a trim are the cheapest ones bought, and
				// those are paired with the DEAREST orders, so a trimmed lot
				// earns more per unit than the whole one did. Flattening that
				// back to one average bmPrice understated the trimmed profit
				// and, once a lot could span several orders, was simply not a
				// price anything had agreed to pay.
				long long trimGross = 0;
				long long trimNet = 0;
				int need = trimQty;
				vector<pair<long long, int>> trimRevenue;

				for (const auto& step : lot.revenueLadder) {
					if (need <= 0) {
						break;
					}
					const int units = step.second < need ? step.second : need;
					trimGross += step.first * units;
					trimNet += NetAfterTax(step.first) * units;
					trimRevenue.push_back(make_pair(step.first, units));
					need -= units;
				}

				const long long trimAvg = trimSpend / trimQty;
				const long long trimNetAvg = trimNet / trimQty;

				// A trimmed lot has to clear the thresholds in its own right.
				// Cutting a lot down changes both numbers the user set a floor
				// under, and a lot that only qualified at full size is not one
				// they asked to buy a slice of. Skipped rather than stopping
				// the fill, so the silver goes to the next lot that does fit.
				if (trimNetAvg <= trimAvg) {
					continue;
				}
				const double trimRate =
					double(trimNetAvg - trimAvg) / double(trimAvg);
				if (!Accepts(settings, trimRate, trimNet - trimSpend)) {
					continue;
				}

				lot.quantity = trimQty;
				lot.spend = trimSpend;
				lot.avgBuyPrice = trimAvg;
				lot.maxBuyPrice = trimMax;
				lot.bmPrice = trimGross / trimQty;
				lot.profit = trimNet - trimSpend;
				lot.revenueLadder.swap(trimRevenue);
				lot.ladder.swap(trimLadder);

				// A trim drops the cheapest orders off the tail, so the
				// fallback ceiling has to be re-derived from what is left or it
				// stays lower than the trimmed lot can actually justify.
				long long trimCheapest = 0;
				for (const auto& step : lot.revenueLadder) {
					if (trimCheapest == 0 || step.first < trimCheapest) {
						trimCheapest = step.first;
					}
				}
				const double trimMarkup = 1.0 + settings.minProfitRate;
				lot.marginCeiling = trimMarkup > 0.0
					? (long long)(NetAfterTax(trimCheapest) / trimMarkup)
					: 0;

				left -= trimSpend;
				affordable.push_back(std::move(lot));
			}
		}
		lots.swap(affordable);
	}

	return lots;
}

vector<FlipLot> CityBuyer::Shortfall(const vector<FlipLot>& lots,
	const vector<int>& unbought) {

	vector<FlipLot> left;

	for (size_t i = 0; i < lots.size(); ++i) {
		const int missing = i < unbought.size() ? unbought[i] : 0;
		if (missing <= 0 || missing > lots[i].quantity) {
			continue;
		}

		FlipLot lot = lots[i];

		// Nothing was bought at all: the lot is its own shortfall and none of
		// the arithmetic below would change a thing.
		if (missing == lots[i].quantity) {
			left.push_back(std::move(lot));
			continue;
		}

		// Walk past the units the run did buy. Both ladders are indexed by the
		// same unit - offers ascending, orders descending, paired off in that
		// order by Plan - so dropping the same count from the front of each
		// keeps every remaining unit matched with the order it was planned
		// against.
		const int taken = lots[i].quantity - missing;

		long long spend = 0;
		long long maxPrice = 0;
		vector<pair<long long, int>> ladder;
		{
			int skip = taken;
			for (const auto& step : lots[i].ladder) {
				int units = step.second;
				if (skip > 0) {
					const int drop = skip < units ? skip : units;
					skip -= drop;
					units -= drop;
				}
				if (units <= 0) {
					continue;
				}
				spend += step.first * units;
				maxPrice = step.first;
				ladder.push_back(make_pair(step.first, units));
			}
		}

		long long gross = 0;
		long long net = 0;
		vector<pair<long long, int>> revenue;
		{
			int skip = taken;
			for (const auto& step : lots[i].revenueLadder) {
				int units = step.second;
				if (skip > 0) {
					const int drop = skip < units ? skip : units;
					skip -= drop;
					units -= drop;
				}
				if (units <= 0) {
					continue;
				}
				gross += step.first * units;
				net += NetAfterTax(step.first) * units;
				revenue.push_back(make_pair(step.first, units));
			}
		}

		// A lot whose ladders do not add up to its quantity cannot be re-costed
		// honestly, and a made-up figure here would rank it against real ones.
		// Left whole instead: too many units on the list is a wasted search,
		// while a wrong price is a purchase made at the wrong price.
		if (ladder.empty() || revenue.empty()) {
			left.push_back(std::move(lot));
			continue;
		}

		lot.quantity = missing;
		lot.spend = spend;
		lot.avgBuyPrice = spend / missing;
		lot.maxBuyPrice = maxPrice;
		lot.bmPrice = gross / missing;
		lot.profit = net - spend;
		lot.ladder.swap(ladder);
		lot.revenueLadder.swap(revenue);

		// marginCeiling is deliberately left alone. It comes from the cheapest
		// order the lot fills, the tail of revenueLadder is where that order
		// is, and the tail is exactly what a shortfall keeps.

		left.push_back(std::move(lot));
	}

	// Same rule the plan itself is ranked by, applied again because re-costing
	// changes the profits it was sorted on.
	sort(left.begin(), left.end(),
		[](const FlipLot& a, const FlipLot& b) {
			if (a.profit != b.profit) {
				return a.profit > b.profit;
			}
			return a.spend < b.spend;
		});

	return left;
}

void CityBuyer::PrintPlan(const vector<FlipLot>& lots) {
	if (lots.empty()) {
		cout << "Nothing worth buying.\n";
		return;
	}

	// snprintf into a buffer and hand the result to cout, rather than printf
	// straight out. The column widths are worth keeping - the plan is meant to
	// be read like a table - but printf writes to the C runtime's stdout,
	// which goes nowhere now that the app is a GUI binary with no console.
	// Only cout is redirected into the in-window log panel, so a printf here
	// silently threw the entire plan away.
	char row[256];

	snprintf(row, sizeof(row), "%-28s %2s %2s %3s  %4s %11s %11s %11s",
		"item", "T", "en", "qty", "qual", "avg buy", "BM pays", "profit");
	cout << row << '\n';

	long long spend = 0;
	long long profit = 0;
	bool anySpread = false;

	for (const FlipLot& lot : lots) {
		snprintf(row, sizeof(row), "%-28s %2d %2d %3d   q%d%s %11lld %11lld %11lld",
			lot.displayName.c_str(), lot.tier, lot.enchant, lot.quantity,
			lot.quality, lot.quality > lot.bmQuality ? "*" : " ",
			lot.avgBuyPrice, lot.bmPrice, lot.profit);
		cout << row << '\n';
		spend += lot.spend;
		profit += lot.profit;

		if (lot.revenueLadder.size() > 1) {
			anySpread = true;
		}
	}

	snprintf(row, sizeof(row), "%-28s %28s %11lld %11s %11lld",
		"TOTAL", "", spend, "", profit);
	cout << row << '\n';
	cout << "* = buying above the quality the black market asked for, "
		"which it still accepts\n";

	if (anySpread) {
		// Worth saying out loud, because the column looks like a quoted price
		// and for these lots it is not one.
		cout << "Some lots fill more than one standing order, so \"BM pays\" "
			"is averaged\n  over them - the later units earn less than the "
			"first ones.\n";
	}
}


// ----------------------------------------------------------------- executing

bool CityBuyer::WaitForRefresh(unsigned long long since, int timeoutMs) {
	const int step = Config.PollIntervalMs > 0 ? Config.PollIntervalMs : 15;
	const auto deadline = chrono::steady_clock::now() +
		chrono::milliseconds(timeoutMs);

	while (chrono::steady_clock::now() < deadline) {
		// Latch only, same as WaitForPurchase. Returning early here would be
		// safe as far as the game goes, but it would hand the caller a "no
		// data arrived" that never happened, and the caller turns that into
		// "gone from the market" in the log. An invented observation is worse
		// than a second of waiting.
		(void)Abort::Requested();

		if (feed.MessagesSeen() <= since) {
			this_thread::sleep_for(chrono::milliseconds(step));
			continue;
		}

		// The first message is not the whole listing - a search result arrives
		// as several packets - so reading immediately can price a lot off half
		// the offers. This used to be handled by sleeping a flat two poll
		// intervals, which is simultaneously too long when the batch was a
		// single packet and too short when it was not.
		//
		// Wait for the stream to go quiet instead: keep extending while
		// messages are still arriving, and stop the moment one interval passes
		// with none. Bounded, because "quiet" never comes while the game is
		// re-sending something unrelated in the background.
		const auto settleBy = chrono::steady_clock::now() +
			chrono::milliseconds(200);

		unsigned long long seen = feed.MessagesSeen();
		while (chrono::steady_clock::now() < settleBy) {
			this_thread::sleep_for(chrono::milliseconds(step));

			const unsigned long long now = feed.MessagesSeen();
			if (now == seen) {
				break;
			}
			seen = now;
		}
		return true;
	}
	return false;
}

CityBuyer::PurchaseOutcome CityBuyer::WaitForPurchase(long long expectedOrderId,
	int timeoutMs, int& unitsBought, long long& boughtOrderId) {

	const int step = Config.PollIntervalMs > 0 ? Config.PollIntervalMs : 15;

	vector<FeedPurchase> batch;
	PurchaseOutcome outcome = PurchaseOutcome::None;

	unitsBought = 0;
	boughtOrderId = 0;

	// The full budget covers the worst case for the *request* packet: click to
	// the game noticing. Two packets arrive per purchase though, and once the
	// first one is in hand the second is only a server round trip behind it -
	// so continuing to wait the whole remaining budget for it is time spent on
	// a deadline that no longer applies. Seeing the request shortens the wait
	// to `afterSent`, which matters because this is the one path that runs
	// once per unit: at four seconds a lost confirmation used to stall a
	// twenty-unit lot for over a minute.
	auto deadline = chrono::steady_clock::now() +
		chrono::milliseconds(timeoutMs);

	const auto afterSent = chrono::milliseconds(
		timeoutMs / 3 > 400 ? timeoutMs / 3 : 400);

	while (chrono::steady_clock::now() < deadline) {
		// Noticed here, acted on nowhere near here.
		//
		// This wait is the longest thing a buying run does between checkpoints
		// and so it is exactly where a person presses the stop combination -
		// but it is also the one place stopping would be unsafe, because a Buy
		// click is already out there and the server's answer to it is what
		// this loop exists to hear. So the poll only sets the latch, and the
		// run leaves at the end of the lot, once the purchase is resolved and
		// written into the ledger.
		(void)Abort::Requested();

		feed.DrainPurchases(batch);

		for (const FeedPurchase& bought : batch) {
			if (bought.orderId != expectedOrderId) {
				// Somebody else took the row we were aiming at between the
				// reading and the click, so the one below it slid up and got
				// bought instead. Its price was never checked against the
				// margin, which is why this is reported rather than counted.
				outcome = PurchaseOutcome::Elsewhere;
				boughtOrderId = bought.orderId;
				unitsBought = bought.amount;
				continue;
			}

			boughtOrderId = bought.orderId;
			unitsBought = bought.amount;

			// The server has answered; nothing better is coming.
			if (bought.confirmed) {
				return PurchaseOutcome::Confirmed;
			}

			// Keep waiting for the confirmation, but remember that the click
			// itself reached the game, and stop holding the original deadline
			// open for it.
			if (outcome == PurchaseOutcome::None) {
				outcome = PurchaseOutcome::Sent;

				const auto shortened = chrono::steady_clock::now() + afterSent;
				if (shortened < deadline) {
					deadline = shortened;
				}
			}
		}

		this_thread::sleep_for(chrono::milliseconds(step));
	}

	return outcome;
}

bool CityBuyer::SelectQuality(int quality) {
	const ClickRect* row = layout.quality.Row(quality);
	if (row == NULL) {
		return false;
	}

	// Deliberately NOT skipped when the wanted quality matches what was
	// picked last time. That shortcut assumed the game keeps the quality
	// filter across a search and a clear-search click, which has never been
	// verified - and if the filter does reset, the bot buys whatever quality
	// happens to be on the top row while believing it is filtered. Two
	// clicks per lot is cheap next to buying the wrong goods with real
	// silver. currentQuality is kept only for reporting.

	MoveAndClickRect(layout.quality.button);
	this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

	MoveAndClickRect(*row);
	this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

	currentQuality = quality;
	return true;
}

bool CityBuyer::SearchFor(const string& text, unsigned long long& mark) {
	if (text.empty()) {
		return false;
	}

	// the market has its own clear-search button, more reliable than trying to
	// wipe the field by hand
	ClearItemName();
	this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

	// Throw away everything the clear-search click provoked, and only then
	// start counting.
	//
	// Clearing the box makes the game re-send the whole unfiltered listing,
	// and so does changing the quality filter just before this. Those packets
	// used to sit in the buffer with the caller's mark taken before the click:
	// the wait for "fresh data after the search" was satisfied by them
	// instantly, and the drain that followed handed back the unfiltered
	// listing instead of the search result. Nothing in it matched the item
	// being bought, so a lot sitting right there on screen was written off as
	// gone from the market.
	//
	// Two things fix it, and both are needed: bin the stale orders, and take
	// the mark AFTER they are binned so the wait cannot be satisfied by
	// something older than the search.
	vector<FeedOrder> stale;
	feed.Drain(stale);
	mark = feed.MessagesSeen();

	MoveAndClickRect(layout.search);
	TypeString(text);
	this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));
	return true;
}

bool CityBuyer::BuyTopRow(int amount) {
	MoveAndClickRect(layout.buyFirstRow);
	this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

	// Only touch the quantity box when buying more than one.
	//
	// Confirmed against the real game (previously guessed otherwise, and the
	// guess was wrong): a single-unit buy has no quantity step to interact
	// with, and clicking/typing into that area anyway derails the click
	// sequence that follows - every qty-1 purchase failed outright
	// ("purchase did not register") once this was made unconditional. Only
	// a multi-unit buy has an actual field here that needs clearing and
	// retyping, since it may still hold a number from an earlier purchase in
	// the same run.
	//
	// With Config::BuyUnitsPerClick at its default of 1 this branch never runs
	// at all, which is the point: the dialog's shape stops mattering.
	if (amount > 1) {
		MoveAndClickRect(layout.amountField);
		SelectAllAndDelete();
		TypeString(to_string(amount));
		this_thread::sleep_for(chrono::milliseconds(Config.ClickDelayMs));
	}

	MoveAndClickRect(layout.confirmBuy);
	this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

	// harmless when the notice is absent, essential when it is not
	MoveAndClickRect(layout.levelWarning);
	this_thread::sleep_for(chrono::milliseconds(Config.ClickDelayMs));

	return true;
}

BuyReport CityBuyer::Run(const vector<FlipLot>& lots, const BuyPlan& settings,
	Holdings& carried) {
	BuyReport report;
	report.lotsPlanned = int(lots.size());

	// Every lot starts out recorded as entirely unbought, and units are
	// subtracted as they actually arrive. Doing it this way round means the
	// paths that leave a lot alone - no name, already done, too dear for the
	// remaining budget, never reached because the run was stopped - are
	// already right without each of them having to remember to say so.
	report.unbought.resize(lots.size());
	for (size_t i = 0; i < lots.size(); ++i) {
		report.unbought[i] = lots[i].quantity;
	}

	long long left = settings.budget > 0 ? settings.budget : LLONG_MAX;
	vector<FeedOrder> batch;

	for (size_t lotIndex = 0; lotIndex < lots.size(); ++lotIndex) {
		const FlipLot& lot = lots[lotIndex];

		// The outermost safe place to stop: between lots nothing is
		// half-bought, and everything already paid for is in the ledger.
		if (Abort::Requested()) {
			report.aborted = true;
			cout << "\n" << Abort::HotkeyName()
				<< " - stopping after " << lotIndex << " of "
				<< lots.size() << " lots.\n";
			break;
		}

		if (lot.searchText.empty()) {
			cout << "  no name for " << lot.itemTypeId << ", skipping\n";
			++report.skipped;
			continue;
		}

		const string key = MakeOrderKey(lot.itemTypeId, lot.enchant)
			+ "#q" + to_string(lot.quality);
		if (doneKeys.count(key)) {
			continue;
		}
		if (lot.spend > left) {
			++report.skipped;
			continue;
		}

		cout << "Buying " << lot.quantity << "x " << lot.searchText
			<< " q" << lot.quality << " up to " << lot.maxBuyPrice
			<< " each\n";

		// These live across the quality bands below, because they describe the
		// LOT, not the band it is currently being filled from.
		int remaining = lot.quantity;

		// Purchases that landed on a row the bot could not find a price for.
		// Capped by Config.StrayBuyLimit: those are the ones whose margin was
		// never checked.
		int blindBuys = 0;

		// Purchases that landed on a different row than the one aimed at, but
		// priced fine and inside the ceiling.
		//
		// Counted rather than announced. In a busy market this is what almost
		// every purchase after the first one looks like, and a warning-shaped
		// line for each of them buries the two that matter - an unpriced buy,
		// and one over the ceiling. One summary line at the end of the lot says
		// the same thing without training the reader to skim.
		int shiftedRows = 0;

		bool anyListing = false;
		bool sawAnyData = false;

		// Which bands units were actually bought from, for the summary line.
		// Indexed by quality 1..5; index 0 unused.
		bool boughtFrom[6] = { false, false, false, false, false, false };

		// Judge the live list, not the plan: minutes may have passed and the
		// cheap offers are exactly the ones other people take first.
		vector<FeedOrder> live;

		// Which quality band is on screen right now. The list has to be
		// narrowed to ONE of them before anything in it is clicked: the search
		// box takes name, tier and enchant but not quality, so without the
		// filter the cheapest row could easily be a quality the order will not
		// take.
		int activeQuality = 0;

		// Replaces the working list, but only when the feed actually brought
		// offers for this item. The old version cleared first and refilled
		// from whatever the drain returned, so a batch that happened to hold
		// something else - the game re-sending a different listing, a stray
		// black market update - emptied the list and ended the lot as "gone
		// from the market" while the goods were still sitting on screen.
		auto pullLive = [&]() -> bool {
			feed.Drain(batch);

			vector<FeedOrder> fresh;
			for (FeedOrder& order : batch) {
				if (order.isBuyOrder || order.groupTypeId != lot.itemTypeId) {
					continue;
				}
				if (order.enchant != lot.enchant ||
					order.quality != activeQuality) {
					continue;
				}
				if (order.price <= 0 || order.amount <= 0) {
					continue;
				}
				fresh.push_back(std::move(order));
			}

			if (fresh.empty()) {
				return false;
			}

			sort(fresh.begin(), fresh.end(),
				[](const FeedOrder& a, const FeedOrder& b) {
					return a.price < b.price;
				});
			live.swap(fresh);
			return true;
		};

		// Work down the quality bands that can fill this lot's orders.
		//
		// The black market takes anything at or above the quality it asked for,
		// so a lot planned against a cheap Outstanding is not lost when someone
		// else buys that Outstanding out - the Good it was undercutting is
		// still on the same screen, and so is every band between them. The
		// planned band is first because it was the cheapest when the plan was
		// made; the rest are tried in the order the plan ranked them.
		//
		// One search per band, and only when the previous one ran out. A lot
		// that fills from its first band costs exactly what it always did.
		// Sweep the bands, and keep sweeping while a pass is still finding
		// things worth buying.
		//
		// Re-sweeping is the point, not a safety net. Prices move under the run:
		// a band abandoned at its ceiling two seconds ago can have a cheap row
		// listed in it while the next band is being worked, and that row is
		// exactly the one that should be taken next. A pass that buys nothing
		// anywhere means there is nothing left under the ceiling in any band,
		// and the lot is done.
		for (int pass = 0; pass < MAX_BAND_PASSES && remaining > 0; ++pass) {
		const int beforePass = remaining;

		for (size_t bandIndex = 0;
			bandIndex < lot.qualityOrder.size() && remaining > 0;
			++bandIndex) {

			// Moving to the next quality band means a fresh search and filter
			// change, so this is a boundary too - and a cheap one to leave at,
			// since nothing has been clicked for the new band yet.
			if (Abort::Requested()) {
				break;
			}

			activeQuality = lot.qualityOrder[bandIndex];
			live.clear();

			// One ceiling for every band, and it has to be one.
			//
			// This started out as maxBuyPrice for the planned band and the
			// margin limit for the rest, to leave the planned band's verified
			// behaviour untouched. A live run showed why that is wrong: the
			// planned band was held to 142988 while its fallbacks could go to
			// 146509, so the bot would refuse a Good at 145000 and accept an
			// Outstanding at the same price - when Good is the quality the
			// order actually asked for. The stricter limit was on the wrong
			// band.
			//
			// maxBuyPrice is a price the plan already agreed to pay;
			// marginCeiling is the most any unit can cost and still clear the
			// margin. Both are justified, so the larger of the two is the
			// honest limit, and it is the same limit everywhere.
			const long long ceiling = lot.marginCeiling > lot.maxBuyPrice
				? lot.marginCeiling : lot.maxBuyPrice;

			const int beforeBand = remaining;

			if (bandIndex > 0 || pass > 0) {
				cout << "  " << remaining << " left - "
					<< (pass > 0 ? "back round to q" : "trying q")
					<< activeQuality << " up to " << ceiling << " each\n";
			}

			SelectQuality(activeQuality);

			unsigned long long mark = 0;
			SearchFor(lot.searchText, mark);

			// Read the listing back, and be willing to wait more than once for
			// it.
			//
			// A search result is several packets, and the panel sends unrelated
			// ones in between, so one wait-and-drain can easily come back
			// holding none of this item. That is indistinguishable from the
			// goods really being gone if the bot only looks once - which is
			// what it used to do, and why lots that were plainly on screen were
			// written off.
			bool listed = false;
			for (int attempt = 0; attempt < Config.SearchRetries; ++attempt) {
				if (!WaitForRefresh(mark, Config.WaitTimeoutMs)) {
					// nothing at all arrived; waiting again buys nothing
					break;
				}
				mark = feed.MessagesSeen();
				sawAnyData = true;

				if (pullLive()) {
					listed = true;
					break;
				}
			}

			if (!listed) {
				// Not fatal any more: this band is empty, but another one may
				// still be able to fill the order.
				//
				// Said out loud all the same. When a band failing stopped being
				// fatal it also stopped being logged, and a lot that tried four
				// bands then printed "0/29 units" gave no way to tell whether
				// the market had none of it or whether everything on offer was
				// simply too dear. That is the I10 mistake with the message
				// removed entirely rather than merely wrong.
				cout << "    q" << activeQuality << ": nothing listed\n";
				continue;
			}

			anyListing = true;

			// Sweep the top row, one purchase at a time.
			//
			// Only the top row is ever clicked: the list is sorted cheapest first
			// and buying it out makes the next row slide up into the same place, so
			// a whole sweep is one button pressed repeatedly - no per-row
			// coordinates, no scrolling.
			//
			// Each click takes one unit by default (Config::BuyUnitsPerClick).
			// Five of an item almost always means five separate one-unit listings
			// from five different sellers, and the buy dialog only grows a quantity
			// box when the row itself holds more than one. Asking for five there
			// typed into a dialog that had no such box, which broke the rest of the
			// click sequence - every one of those purchases failed and the lot was
			// abandoned. One click per unit needs no quantity box at all.
			//
			// Whether a click worked is no longer inferred. AlbionFeed forwards the
			// game's own buy packets, so the bot is told the order id and the unit
			// count.
			// Two per unit plus slack: with one unit per click the old bound of
			// quantity+8 was tight enough that a couple of retries could exhaust it
			// mid-lot. Sized on what is LEFT, so a band entered late gets a bound
			// that matches the work it actually has.
			int rounds = 0;
			const int maxRounds = remaining * 2 + 8;
			int retries = 0;

			while (remaining > 0 && rounds++ < maxRounds) {
				// The innermost safe boundary, and the only one that matters
				// for correctness: BEFORE the next Buy click is sent, never
				// after. Once a click is out there the game may act on it, and
				// walking away from a purchase the server is still answering
				// is how the ledger loses track of goods that are really in
				// the bag - the same class of mistake as inferring a failed
				// purchase from silence.
				if (Abort::Requested()) {
					break;
				}

				if (live.empty()) {
					break;
				}

				const FeedOrder top = live.front();
				if (top.price > ceiling) {
					// Dearer than this band may go: stop rather than erode the
					// margin. The lot is not over - another band may still be
					// cheaper, and the loop above moves on to it.
					break;
				}
				if (top.price > left) {
					break;
				}

				int take = top.amount < remaining ? top.amount : remaining;

				// Clamp to what the remaining silver actually covers. The check
				// above only proves ONE unit is affordable; without this, a row
				// of 20 at 100 silver each was bought whole against 1000 silver
				// left, overspending the budget the user typed in by 1000. left
				// is LLONG_MAX when no budget was set, so this is a no-op then.
				if (top.price > 0) {
					long long affordable = left / top.price;
					if (affordable < (long long)take) {
						take = int(affordable);
					}
				}

				if (take > Config.BuyUnitsPerClick) {
					take = Config.BuyUnitsPerClick;
				}

				if (take <= 0) {
					break;
				}

				// Drop anything reported before this click, so a confirmation left
				// over from the previous unit cannot be read as this one's.
				vector<FeedPurchase> stale;
				feed.DrainPurchases(stale);

				unsigned long long mark = feed.MessagesSeen();
				BuyTopRow(take);

				int unitsBought = 0;
				long long boughtOrderId = 0;
				const PurchaseOutcome outcome = WaitForPurchase(top.id,
					Config.PurchaseConfirmMs, unitsBought, boughtOrderId);

				if (outcome == PurchaseOutcome::None) {
					// The game never saw the click. Usually the window lost focus
					// for a moment, and clicking again fixes it.
					if (retries++ < Config.BuyRetries) {
						cout << "  no answer from the game, clicking again ("
							<< retries << "/" << Config.BuyRetries << ")\n";
						continue;
					}
					cout << "  the game never acted on the buy click, "
						<< "leaving this item\n";
					break;
				}

				if (outcome == PurchaseOutcome::Elsewhere) {
					// Another player took the row we had priced, the one below slid
					// up, and the click bought that instead.
					//
					// This is the ordinary weather of a busy market, not a fault,
					// and it used to abandon the whole lot. That is what turned a
					// planned sweep of ten or eleven units into one unit and a move
					// on to the next item - ten lots in one run, every one of them
					// stopped on its first purchase. The reason given was that the
					// price of what got bought was never checked against the
					// margin, which is true only when the bot cannot price it at
					// all. Usually it can: the row is in the listing it already
					// holds.
					//
					// So: price it. Within the ceiling, carry on with a re-read
					// list. Over the ceiling, stop - that is a real margin breach.
					// Not priceable at all, count it and allow a few before
					// stopping, since those are the ones flying blind.
					long long paid = 0;
					for (const FeedOrder& order : live) {
						if (order.id == boughtOrderId) {
							paid = order.price;
							break;
						}
					}

					// The goods are in the bag either way, so the ledger has to
					// know about them or SellBM will never offer them. If the id is
					// not one we have seen, the stack is recorded at unknown cost,
					// which BlackMarketSeller already handles (it sells those to any
					// positive bid).
					const int strayUnits = unitsBought > 0 ? unitsBought : take;
					carried.Add(lot.itemTypeId, lot.searchText, lot.tier,
						lot.enchant, activeQuality, strayUnits, paid);

					report.unitsBought += strayUnits;
					report.spent += paid * strayUnits;
					left -= paid * strayUnits;
					remaining -= strayUnits;
					boughtFrom[activeQuality] = true;

					// Take them off the local copy too, or the next round aims at
					// a row that is already gone and shifts again for the same
					// reason.
					for (size_t i = 0; i < live.size(); ++i) {
						if (live[i].id != boughtOrderId) {
							continue;
						}
						if (live[i].amount > strayUnits) {
							live[i].amount -= strayUnits;
						}
						else {
							live.erase(live.begin() + i);
						}
						break;
					}

					if (paid == 0) {
						cout << "  bought order " << boughtOrderId
							<< " at a price not in the listing - check the ledger "
							<< "before selling\n";
						if (++blindBuys > Config.StrayBuyLimit) {
							cout << "    that is " << blindBuys
								<< " unpriced purchases, leaving this item\n";
							break;
						}
					}
					else if (paid > ceiling) {
						cout << "  the list shifted and " << paid
							<< " is above the " << ceiling
							<< " this band may pay - leaving it\n";
						break;
					}
					else {
						// Normal. Counted for the end-of-lot line, not announced.
						++shiftedRows;
					}

					retries = 0;

					// Re-read before aiming again: the list demonstrably moved.
					if (WaitForRefresh(mark, LIST_REFRESH_MS)) {
						pullLive();
					}
					continue;
				}

				retries = 0;

				// The game echoes back what was asked for. A figure outside that
				// means the packet was read wrong, and the click is the better
				// witness.
				if (unitsBought <= 0 || unitsBought > take) {
					unitsBought = take;
				}

				remaining -= unitsBought;
				boughtFrom[activeQuality] = true;
				long long cost = top.price * unitsBought;
				left -= cost;
				report.spent += cost;
				report.unitsBought += unitsBought;

				// Record it straight away rather than at the end of the lot: if
				// the run is interrupted halfway the goods are still in the bag,
				// and the ledger has to agree with that.
				carried.Add(lot.itemTypeId, lot.searchText, lot.tier, lot.enchant,
					activeQuality, unitsBought, top.price);

				// Take the units off the local copy of the row. The game re-sends
				// the list when it feels like it, and until it does this is what
				// keeps the next iteration honest.
				if (!live.empty() && live.front().id == top.id) {
					if (live.front().amount > unitsBought) {
						live.front().amount -= unitsBought;
					}
					else {
						live.erase(live.begin());
					}
				}

				// If the game did re-send the listing, prefer its version.
				if (WaitForRefresh(mark, LIST_REFRESH_MS)) {
					pullLive();
				}
			}

			// A band that had goods but sold none of them has to say which it
			// was: too dear, or bought out while it was being worked. Both look
			// identical in the totals, and only one of them means "raise the
			// margin headroom".
			if (remaining == beforeBand) {
				if (live.empty()) {
					cout << "    q" << activeQuality
						<< ": bought out while working it\n";
				}
				else {
					cout << "    q" << activeQuality << ": cheapest "
						<< live.front().price << " is over the " << ceiling
						<< " ceiling\n";
				}
			}
		}

		// A whole pass over every band that bought nothing means nothing is
		// left under the ceiling anywhere. Going round again would just be the
		// same searches for the same answer.
		if (remaining == beforePass) {
			break;
		}
		}

		// Nothing on offer in ANY band that could fill this lot's orders.
		//
		// Kept apart in the log: silence means the search never reached the
		// game (focus, or the capture client died), while data that never
		// mentions the item means the market really has none of it - and now
		// that every acceptable quality was tried, "none of it" is a much
		// stronger statement than it used to be.
		if (!anyListing) {
			if (!sawAnyData) {
				cout << "  no data after the search, skipping\n";
				++report.skipped;
			}
			else {
				cout << "  gone from the market in every quality that fits\n";
				++report.vanished;
			}
			continue;
		}

		// One line per lot, and only when there is something to say. A lot that
		// swept exactly what it planned from its first band, with nothing odd
		// on the way, speaks for itself - the "Buying 12x ..." line above
		// already said what it was going to do.
		const int got = lot.quantity - remaining;
		int bandCount = 0;
		for (int q = 1; q <= 5; ++q) {
			if (boughtFrom[q]) {
				++bandCount;
			}
		}

		if (shiftedRows > 0 || got < lot.quantity || bandCount > 1) {
			cout << "  " << got << "/" << lot.quantity << " units";
			if (bandCount > 1) {
				cout << " across " << bandCount << " quality bands";
			}
			if (shiftedRows > 0) {
				cout << ", " << shiftedRows << " row"
					<< (shiftedRows == 1 ? "" : "s")
					<< " shifted under the click";
			}
			cout << "\n";
		}

		report.unbought[lotIndex] = remaining;

		if (remaining < lot.quantity) {
			++report.lotsBought;
			doneKeys.insert(key);
		}
		else {
			++report.skipped;
		}

		// A stop noticed while this lot was being filled - inside a wait for
		// the server, say - is acted on here rather than in the middle of it.
		// The lot is finished and written down first; only then does the run
		// end. This is the whole reason Abort latches instead of being read
		// live at the point of use.
		if (Abort::Latched()) {
			report.aborted = true;
			cout << "\n" << Abort::HotkeyName()
				<< " - stopping after this lot.\n";
			break;
		}
	}

	cout << "Buying finished: " << report.unitsBought << " units in "
		<< report.lotsBought << "/" << report.lotsPlanned << " lots, spent "
		<< report.spent << " silver\n";
	if (report.vanished > 0) {
		cout << "  " << report.vanished << " lots had gone from the market\n";
	}

	return report;
}
