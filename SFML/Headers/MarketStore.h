#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "MarketFeed.h"

using namespace std;


// Keeps the two halves of a flip apart.
//
// A scan does not have to say where it happened: every order carries its own
// LocationId and AuctionType, so the split is decided by the data rather than
// by which button was pressed. That also makes it possible to notice when a
// scan was run in the wrong place.
//
// Orders are held by their game id, so re-scanning a tier refreshes the stock
// figures instead of adding a second copy of every row.
class MarketStore {
public:
	// Takes everything currently buffered in the feed and files it.
	// Returns how many orders were absorbed.
	size_t Absorb(MarketFeed& feed);
	// Same filing rules, for a batch the caller has already filtered. Name
	// scanning uses this to keep partial-autocomplete results out of the store.
	size_t Absorb(vector<FeedOrder> batch);

	// Standing black market buy orders: what can be sold, and for how much.
	vector<FeedOrder> BlackMarketOrders() const;

	// Sell offers in the royal cities: what can be bought.
	// Pass a location to restrict it to one city.
	vector<FeedOrder> CityOffers(const string& locationId = "") const;

	// Cities seen so far, for reporting.
	vector<string> KnownCities() const;

	size_t BlackMarketCount() const;
	size_t CityCount() const;

	void Clear();
	void ClearBlackMarket();
	void ClearCities();

private:
	unordered_map<long long, FeedOrder> blackMarket;
	unordered_map<long long, FeedOrder> cities;
};
