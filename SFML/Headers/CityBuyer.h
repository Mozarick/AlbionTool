#pragma once
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "Holdings.h"
#include "MarketFeed.h"
#include "MarketUi.h"
#include "MoveToPosition.h"

using namespace std;


// Albion quality levels, in the order the filter menu lists them.
enum ItemQuality {
	QUALITY_NORMAL = 1,
	QUALITY_GOOD = 2,
	QUALITY_OUTSTANDING = 3,
	QUALITY_EXCELLENT = 4,
	QUALITY_MASTERPIECE = 5,
};

// The black market takes anything at or above the quality it asked for, so a
// Masterpiece fills a Good order. That is what lets the planner shop across
// quality bands instead of demanding an exact match.
bool QualityFills(int offered, int wanted);


// Screen areas of the city market, in 1920x1080 coordinates.
struct CityBuyLayout {
	// the game parses "Boltcasters T4.2" out of the search box by itself
	ClickRect search = { 586, 264, 697, 277 };

	// Buying always works on the top row. The list is sorted cheapest first,
	// and once the top row is bought out the next one moves up into it, so a
	// whole sweep is just this button clicked repeatedly. No per-row
	// coordinates and no scrolling.
	//
	// Bottom edge raised 442 -> 440; the top edge is unchanged. The old box
	// reached two pixels past the button, so a click that happened to land in
	// that band hit nothing and the game never answered it - which is what
	// "no answer from the game, clicking again" is. ClickInside picks a random
	// point, so it was only some of the clicks, some of the time: 17 dropped
	// clicks across 137 units in one run, two of which used up their retries
	// and abandoned the rest of a lot.
	ClickRect buyFirstRow = { 1232, 424, 1314, 440 };

	ClickRect amountField = { 567, 570, 583, 578 };
	ClickRect confirmBuy = { 844, 722, 922, 739 };

	// The final "are you sure? you may not be able to use this" confirm,
	// shown only when the item needs a higher tier/spec than currently worn.
	// Clicked unconditionally: when the notice is absent this is just empty
	// space, and when it is present, this is the click that actually
	// completes the purchase - remeasured against the real game.
	//
	// Nudged up 3px from 548-558 to 545-555: the clicks were landing a
	// little low against the real dialog.
	//
	// DO NOT extend this box downward past y=558. The 8px band right below it
	// (830,566 to 866,585) was flagged by hand as unsafe to click - close
	// enough to this confirm that a careless widening of levelWarning would
	// swallow it. levelWarning as measured does not reach it (558 vs 566),
	// so nothing needs to change today; this note exists so a future
	// "increase the target area a bit" edit does not walk into it blind, and
	// so ClickInside's random-point-in-rect behaviour, which is deliberate
	// everywhere else in this codebase, does not become the wrong choice
	// here.
	ClickRect levelWarning = { 819, 545, 899, 555 };

	// The quality filter, shared with the selling side (MarketUi.h). This used
	// to be a second, separately measured copy of the same dropdown, a few
	// pixels off the one in BlackMarketLayout - two sets of magic numbers for
	// one piece of UI, each only ever corrected when its own side misbehaved.
	QualityLayout quality;
};


// One planned purchase: sweep this many units of a single item at a single
// quality, then flip them into one standing black market order.
struct FlipLot {
	string itemTypeId;
	string displayName;
	string searchText;      // what gets typed: "Boltcasters T4.2"
	int tier = 0;
	int enchant = 0;
	int quality = 0;        // quality actually being bought
	int bmQuality = 0;      // quality the black market order asked for

	int quantity = 0;
	long long avgBuyPrice = 0;
	long long maxBuyPrice = 0;   // dearest offer included in the sweep
	long long spend = 0;         // avgBuyPrice * quantity
	long long bmPrice = 0;       // averaged over the orders actually filled
	long long profit = 0;        // total, after tax

	// The offers the sweep actually took, cheapest first: (unit price, units).
	// Kept so a lot trimmed to fit the budget can be re-costed exactly. Reusing
	// the full lot's average and price ceiling for a trimmed lot overstates
	// both - the trimmed version only keeps the cheapest end of the ladder.
	vector<pair<long long, int>> ladder;

	// Quality bands that can fill this lot's orders, cheapest planned supply
	// first. The band that was actually planned is always element 0.
	//
	// The black market takes anything at or above the quality it asked for, so
	// an order for Good is equally well filled by an Outstanding - and if the
	// Outstanding is the cheaper of the two, that is the one worth buying.
	// Plan already picks the cheapest band; this list is what lets the BUYING
	// run keep doing it after the market moves.
	//
	// Without it, a lot planned against a cheap Outstanding that somebody else
	// buys out is simply abandoned - even with Good sitting on the same screen,
	// at a price the margin still clears. The whole lot was lost to a
	// substitution the market itself considers valid.
	vector<int> qualityOrder;

	// Most that may be paid per unit in ANY of those bands and still clear the
	// required margin against the cheapest order this lot fills.
	//
	// `maxBuyPrice` cannot do this job: it is the dearest offer of the band
	// that happened to be planned, so falling back to a DEARER band - which is
	// the normal case, since the cheap band is the one that got planned - would
	// be refused by it every time. This is derived from the revenue side
	// instead, so it holds whichever band a unit comes from.
	long long marginCeiling = 0;

	// The black market orders the sweep sold into, DEAREST first: (unit price
	// before tax, units). The mirror of `ladder` on the revenue side.
	//
	// A lot is no longer priced against a single order. One item can have
	// several standing orders at different prices, and a lot big enough to
	// fill more than the best of them earns less on the units that spill into
	// the next one down - so a single bmPrice cannot re-cost a trimmed lot.
	// Trimming keeps the cheapest units bought, which are the ones paired with
	// the dearest orders, so it keeps this ladder from the front.
	vector<pair<long long, int>> revenueLadder;
};

struct BuyPlan {
	// silver available; 0 means unlimited
	long long budget = 0;

	// skip anything thinner than this, as a fraction (0.2 = 20%)
	double minProfitRate = 0.20;

	// Floor on the TOTAL net profit one lot must earn, in silver - the figure
	// the plan table prints in its last column, not a per-unit one.
	//
	// Zero means it is not in play and the percentage decides alone, which is
	// what every run did before this existed. It answers a complaint the
	// percentage cannot: 300% on a 2k flip is a wonderful rate and forty
	// silver of profit, and a screen full of those is not worth the clicking.
	long long minProfitSilver = 0;

	// How the two thresholds combine when both are set. true = AND, both must
	// clear. false = OR, either one is enough.
	//
	// AND is the strict reading ("worth my time AND worth the silver"); OR is
	// the wide one, letting a fat-but-thin flip through next to a
	// thin-but-huge one. Ignored while minProfitSilver is zero.
	bool requireBoth = true;

	// hard ceiling on what a single unit may cost, 0 for none
	long long maxUnitPrice = 0;

	// Tiers the plan is allowed to buy. Empty means no restriction.
	//
	// This is a hard gate, not a preference: an item outside it is not bought
	// however good the margin looks. Scanning tier 8 and coming home with tier
	// 6 is not a bargain the planner found, it is the store still holding
	// offers from a scan run hours ago at a different setting - the store is
	// keyed by order id and keeps everything until Clear, so yesterday's tiers
	// are just as present as today's.
	set<int> allowedTiers;
};

struct BuyReport {
	int lotsPlanned = 0;
	int lotsBought = 0;
	int unitsBought = 0;
	long long spent = 0;
	int vanished = 0;    // offer was gone once we searched for it
	int skipped = 0;

	// Units of each planned lot the run did NOT get, in the same order as the
	// lots handed to Run - one entry per lot, whatever happened to it.
	//
	// A run that comes home short is the normal case, not a fault: offers get
	// taken by other players while the bot is working down the list, and a
	// laggy moment costs a click. Counting it per lot is what lets the
	// shortfall be handed back as a hand-shopping list instead of the user
	// re-running Plan and comparing two tables by eye.
	//
	// Aborted, skipped and never-reached lots all appear here at their full
	// quantity, which is what they are: nothing bought.
	vector<int> unbought;

	// True when the run stopped on the emergency stop rather than finishing.
	bool aborted = false;
};


// Works out what to buy in a royal city to flip into black market orders, and
// then buys it.
//
// Prices, quantities, enchantments and qualities all come from
// AlbionFeed, so none of the arithmetic depends on reading pixels. The
// mouse is only used to drive the UI.
class CityBuyer {
public:
	CityBuyer(MarketFeed& feed, const ItemTable& items);

	// Pure calculation, no clicking: pairs standing black market orders with
	// what the city is offering and returns the worthwhile lots, best value
	// for silver first.
	vector<FlipLot> Plan(const vector<FeedOrder>& blackMarketOrders,
		const vector<FeedOrder>& cityOffers, const BuyPlan& settings) const;

	// Prints a plan the way a shopping list reads, so it can be checked before
	// any silver is spent.
	static void PrintPlan(const vector<FlipLot>& lots);

	// The part of a plan a buying run did not manage to buy, re-costed as
	// lots in its own right.
	//
	// `unbought` is BuyReport::unbought, one entry per lot in `lots`. Lots that
	// were bought in full drop out; the rest come back holding only the units
	// that are still out there.
	//
	// Re-costed from the TAIL of both ladders, and that is the whole subtlety.
	// A run buys cheapest-first, so what it leaves behind is the DEAREST end
	// of the offer ladder, paired with the CHEAPEST orders - the exact mirror
	// of a budget trim, which keeps the cheap end and earns more per unit.
	// Reusing the whole lot's averages here would overstate what the leftovers
	// are worth and rank them above lots that really are worth more.
	//
	// marginCeiling is the one figure that carries over untouched: it is
	// derived from the cheapest order the lot fills, and the cheapest order is
	// in the tail, which is precisely the part that survives.
	static vector<FlipLot> Shortfall(const vector<FlipLot>& lots,
		const vector<int>& unbought);

	// Executes a plan. Re-reads the feed before every purchase, because the
	// prices seen while planning may be minutes old by now.
	//
	// Everything bought is written into `carried`, which is what the black
	// market run later sells: without that ledger it has no idea which items
	// the character is actually holding.
	BuyReport Run(const vector<FlipLot>& lots, const BuyPlan& settings,
		Holdings& carried);

	void Reset();

	// What the game said about a Buy click, as reported by AlbionFeed.
	enum class PurchaseOutcome {
		None,       // the game never acted on it - the click did not land
		Sent,       // the client asked to buy; no server answer yet
		Confirmed,  // the server answered
		Elsewhere,  // a purchase happened, but on a different order id
	};

private:
	bool SelectQuality(int quality);

	// Types a search and reports, through `mark`, the feed position the result
	// will arrive after. The caller cannot take that mark itself: clearing the
	// box is part of searching and it provokes a listing of its own, which the
	// caller would otherwise mistake for the search result.
	bool SearchFor(const string& text, unsigned long long& mark);

	bool BuyTopRow(int amount);

	// Waits for a feed message newer than `since`, so the data being judged
	// belongs to the search just performed.
	bool WaitForRefresh(unsigned long long since, int timeoutMs);

	// Waits for the game to report what the last Buy click did.
	//
	// This is the whole reason the bot runs its own capture client. The old
	// test was "did the offer list get shorter?", which needs the game to
	// re-request the list after every purchase - and when it does not, a
	// perfectly good purchase reads as a failure and the rest of the lot is
	// abandoned. AuctionBuyOffer carries the order id and the unit count, so
	// there is nothing left to infer.
	PurchaseOutcome WaitForPurchase(long long expectedOrderId, int timeoutMs,
		int& unitsBought, long long& boughtOrderId);

	MarketFeed& feed;
	const ItemTable& items;
	CityBuyLayout layout;

	int currentQuality = 0;   // avoids re-picking the same filter
	unordered_set<string> doneKeys;
};
