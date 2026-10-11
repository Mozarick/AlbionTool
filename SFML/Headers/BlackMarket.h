#pragma once
#include <string>
#include <unordered_set>
#include <vector>

#include "Holdings.h"
#include "MarketFeed.h"
#include "MarketUi.h"
#include "MoveToPosition.h"

using namespace std;


// The black market reports itself as location 3003.
extern const char* const BLACK_MARKET_LOCATION;

// Screen areas used by the direct-sell flow, in 1920x1080 coordinates.
struct BlackMarketLayout {
	// where the item name is typed; the game parses "Boltcasters T4.2" itself
	ClickRect search = { 586, 264, 697, 277 };

	// Clicking the top row does not sell on its own - it opens a confirmation,
	// exactly like buying does. Same coordinates as CityBuyer's buyFirstRow:
	// it is the same row in the same list, the market just reads it as a sell
	// because that is which side of the order book is open.
	ClickRect sell = { 1238, 427, 1323, 442 };

	// Completes the sale. Same coordinates as CityBuyer's confirmBuy - one
	// confirm dialog shared by both directions. Unlike buying's levelWarning,
	// this one is not conditional: it appears every time and has to be
	// clicked every time.
	ClickRect confirmSell = { 848, 725, 919, 735 };

	// The quality filter, shared with the buying side (MarketUi.h).
	// The search box takes a name, a tier and an enchant but not a
	// quality, so without this the list still holds every quality of the
	// item and the top row - the one the sell click lands on - is
	// whichever one happens to be cheapest, not the one being sold.
	QualityLayout quality;
};


// What the caller wants out of a run, and what it got.
struct SellPlan {
	// Only sell a stack when the standing bid clears this much profit over
	// what Holdings says was paid for it, as a fraction (0.2 = 20%), after
	// the black market's 4% sale tax. The bot decides per stack from its own
	// ledger instead of the user guessing a silver price by hand.
	double minProfitRate = 0.0;
	// stop after this many successful sales (0 = no limit)
	int maxSales = 0;
};

struct SellReport {
	int attempted = 0;  // stacks tried
	int sold = 0;       // units sold
	int vanished = 0;   // order was gone by the time we searched for it
	int tooCheap = 0;
	long long silver = 0;  // received after the 4% sale tax
};


// Sells into black market buy orders one item at a time.
//
// The prices come from AlbionFeed rather than the screen, so tier,
// enchantment, quantity and price are exact. The mouse is still needed to
// drive the UI, but nothing is read off pixels.
//
// Typing a name into the search box makes the game re-request the list, which
// means a fresh feed message arrives for every item examined. That is what
// makes a stale order detectable: if the item is no longer in the refreshed
// data, somebody else sold into that order first, and it is skipped.
class BlackMarketSeller {
public:
	BlackMarketSeller(MarketFeed& feed, const ItemTable& items);

	// Sells what the character is carrying, taking each stack out of `carried`
	// as it goes.
	//
	// It walks the ledger rather than the black market's order book. Walking
	// the order book meant searching for items the character does not own and
	// then clicking sell on whatever row happened to be there.
	SellReport Run(const SellPlan& plan, Holdings& carried);

	void Reset();

private:
	// Narrows the list to one quality before anything in it is clicked.
	//
	// No OCR: the dropdown rows never move, so they are clicked by measured
	// coordinate (MarketUi.h). Returns false only when the quality is outside
	// 1..5, which would mean a corrupt ledger row.
	bool SelectQuality(int quality);

	// Waits for the feed to deliver a message newer than `since`, so the
	// data examined always belongs to the current search.
	bool WaitForRefresh(unsigned long long since, int timeoutMs);

	// Empties the feed buffer so the first search of the run is judged only
	// on data that search produced.
	void Absorb();

	MarketFeed& feed;
	const ItemTable& items;
	BlackMarketLayout layout;

	// items already sold this run, so the loop moves on instead of retrying
	unordered_set<string> soldKeys;
};


// item id + enchant, the granularity the black market prices at
string MakeOrderKey(const string& itemTypeId, int enchant);
