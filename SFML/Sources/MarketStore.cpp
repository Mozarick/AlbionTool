#include "../Headers/MarketStore.h"
extern const char* const BLACK_MARKET_LOCATION;

#include <algorithm>
using namespace std;


size_t MarketStore::Absorb(MarketFeed& feed) {
	vector<FeedOrder> batch;
	feed.Drain(batch);
	return Absorb(std::move(batch));
}

size_t MarketStore::Absorb(vector<FeedOrder> batch) {
	const size_t received = batch.size();

	for (FeedOrder& order : batch) {
		if (order.id == 0 || order.price <= 0) {
			continue;
		}

		// The black market only ever posts buy orders and the cities only ever
		// post sell offers, so either field would do; requiring both means a
		// surprise gets dropped rather than filed in the wrong half.
		const bool isBlackMarket = order.locationId == BLACK_MARKET_LOCATION;

		if (isBlackMarket && order.isBuyOrder) {
			blackMarket[order.id] = std::move(order);
		}
		else if (!isBlackMarket && !order.isBuyOrder) {
			cities[order.id] = std::move(order);
		}
	}

	return received;
}

vector<FeedOrder> MarketStore::BlackMarketOrders() const {
	vector<FeedOrder> out;
	out.reserve(blackMarket.size());
	for (const auto& entry : blackMarket) {
		out.push_back(entry.second);
	}
	return out;
}

vector<FeedOrder> MarketStore::CityOffers(const string& locationId) const {
	vector<FeedOrder> out;
	out.reserve(cities.size());
	for (const auto& entry : cities) {
		if (!locationId.empty() && entry.second.locationId != locationId) {
			continue;
		}
		out.push_back(entry.second);
	}
	return out;
}

vector<string> MarketStore::KnownCities() const {
	vector<string> out;
	for (const auto& entry : cities) {
		if (find(out.begin(), out.end(), entry.second.locationId) == out.end()) {
			out.push_back(entry.second.locationId);
		}
	}
	sort(out.begin(), out.end());
	return out;
}

size_t MarketStore::BlackMarketCount() const {
	return blackMarket.size();
}

size_t MarketStore::CityCount() const {
	return cities.size();
}

void MarketStore::Clear() {
	blackMarket.clear();
	cities.clear();
}

void MarketStore::ClearBlackMarket() {
	blackMarket.clear();
}

void MarketStore::ClearCities() {
	cities.clear();
}
