#pragma once
#include <string>
#include <vector>
#include <limits>

#include "CityBuyer.h"

using namespace std;

// Tracks copy actions, including repeated copies of the same item.
class ShopEstimate {
public:
	bool Copy(long long profit, size_t index = 0) {
		if ((profit > 0 && total > (std::numeric_limits<long long>::max)() - profit) ||
			(profit < 0 && total < (std::numeric_limits<long long>::min)() - profit)) return false;
		history.emplace_back(index, profit); total += profit; return true;
	}
	bool Undo(size_t& index) {
		if (history.empty()) return false;
		index = history.back().first; total -= history.back().second; history.pop_back(); return true;
	}
	void Reset() { total = 0; history.clear(); }
	long long Total() const { return total; }
	size_t Count() const { return history.size(); }
	const vector<pair<size_t, long long>>& History() const { return history; }
private:
	long long total = 0;
	vector<pair<size_t, long long>> history;
};

// One line of a hand-shopping list: everything needed to buy an item manually
// in the city, condensed into a figure that can be acted on without thinking.
//
// The plan table is a per-lot view - one row per quality band - which is the
// right shape for a machine working through it and the wrong shape for a
// person. A person searches once per item and then judges the rows the game
// shows them, so the list they want is one entry per SEARCH, not per lot.
struct ShopBlock {
	// What gets typed into the market's search box, and what a click copies
	// to the clipboard: "Boltcasters T4.2". The game parses the tier and
	// enchantment out of it by itself.
	string searchText;
	string displayName;

	int tier = 0;
	int enchant = 0;

	// Units the black market can still absorb across every band merged here.
	// A hard cap, not a target: anything bought past it has no order waiting
	// for it and sits in the bag.
	int quantity = 0;

	// The LOWEST quality any of the merged orders will accept. The black
	// market takes anything at or above what it asked for, so this reads as
	// "this quality or better" - buying an Outstanding against an order for
	// Good is fine, and often cheaper.
	int minQuality = 0;

	// The most that may be paid per unit and still clear the margin.
	//
	// The lowest ceiling among the merged lots, and it has to be the lowest
	// one. A block that merges an order wanting Good (ceiling 12,450) with one
	// wanting Excellent (ceiling 20,100) advertises the Good quality as its
	// floor, so a Good item at 18,000 is a purchase this list would have
	// invited - and a Good item cannot fill the Excellent order, so it would
	// have to sell into the Good one at a loss. Taking the minimum is what
	// makes "cheaper than this is worth buying" true of every row the search
	// can return, with no exception to remember.
	long long ceiling = 0;

	long long profit = 0;
	long long spend = 0;

	// How many planned lots were merged. More than one means the block spans
	// several quality bands and `ceiling` is the conservative one.
	int bands = 0;
};


// Groups planned lots by what would be typed to find them - item, tier and
// enchantment - and ranks the groups by total profit, biggest first.
//
// Ranked by BLOCK, not by lot: the unit of work being ordered here is one
// search and the buying that follows it, so two lots of the same item are one
// job and their profits belong together.
vector<ShopBlock> BuildShopList(const vector<FlipLot>& lots);


// "Good", "Outstanding", ... for the quality numbers in ItemQuality.
const char* QualityName(int quality);
