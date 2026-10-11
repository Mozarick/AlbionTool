#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std;


// One order exactly as AlbionFeed saw it on the wire.
struct FeedOrder {
	// The game's own order id. Scanning the same tier twice resends the same
	// orders, so this is what stops a second pass from double counting the
	// stock available.
	long long id = 0;
	string itemTypeId;    // T4_2H_DUALCROSSBOW_HELL@2
	string groupTypeId;   // T4_2H_DUALCROSSBOW_HELL  (no enchant suffix)
	string locationId;    // "3003" is the black market
	long long price = 0;  // real silver: the feed multiplies by 10000
	int amount = 0;
	int tier = 0;         // parsed out of the id once, here
	int enchant = 0;
	int quality = 0;
	bool isBuyOrder = false;  // AuctionType == "request"
	long long seenAt = 0;    // capture UTC seconds; never refreshed when loading saved data
	long long expiresAt = 0; // game expiry UTC seconds, 0 if not supplied
};


// A purchase the game itself reported.
//
// The bot used to infer this: click Buy, wait for the market list to be re-sent
// and see whether the offer had shrunk. That guess is wrong whenever the game
// does not re-request the list, and it is what produced "purchase did not
// register" on purchases that had in fact gone through. AlbionFeed forwards the
// AuctionBuyOffer packets instead, so the order id and the number of units come
// from the game.
//
// Two arrive per purchase: `confirmed == false` when the client sent the
// request, then `confirmed == true` when the server answered it.
struct FeedPurchase {
	long long orderId = 0;
	int amount = 0;
	bool confirmed = false;
};


// UniqueName -> display name, loaded from items.tsv (generated from
// ao-bin-dumps). Needed because the feed speaks in ids like
// T4_2H_DUALCROSSBOW_HELL while the game's search box wants "Boltcasters".
class ItemTable {
public:
	bool Load(const string& path);

	size_t Size() const;

	// Base display name for an id with or without an @enchant suffix,
	// or nullptr when the id is not in the table.
	const string* BaseName(const string& itemTypeId) const;

	// What to type into the market search box: "Boltcasters T4.2".
	// Empty when the id is unknown.
	string SearchString(const string& itemTypeId, int tier, int enchant) const;

private:
	unordered_map<string, string> byId;
};


// Receives live market data from AlbionFeed.
//
// AlbionFeed (D:\AlbionTool\AlbionFeed, built on AlbionDataAvalonia's packet
// parser) POSTs a JSON body here for every market packet it decodes:
//
//   POST /market    {"Orders":[ ... ]}       one entry per order
//   POST /purchase  {"Purchase":{ ... }}     the game confirming a buy
//
// The path is ignored - the body's shape is what selects the handler - so a
// future topic can be added without touching the routing. The field names are
// the game's own, which is why they are capitalised.
//
// The socket work runs on its own thread so the bot's own loop never blocks on
// it. Counters are atomic so the "keep clicking until nothing new arrives"
// loops can poll without taking a lock.
class MarketFeed {
	friend struct ToolRegressionAccess;
public:
	MarketFeed();
	~MarketFeed();

	MarketFeed(const MarketFeed&) = delete;
	MarketFeed& operator= (const MarketFeed&) = delete;

	bool Start(unsigned short port = 5000);
	void Stop();
	bool IsRunning() const;
	unsigned short Port() const { return boundPort.load(); }

	// Hands over everything buffered so far. Swaps the buffer rather than
	// copying it, so the lock is held for a few instructions.
	void Drain(vector<FeedOrder>& out);

	// Same, for purchases. Kept in its own queue rather than mixed into the
	// order stream: buying drains orders repeatedly while it works through a
	// lot, and a confirmation that got thrown away by one of those drains is a
	// confirmation the buyer never sees.
	void DrainPurchases(vector<FeedPurchase>& out);

	// Lock-free progress counters.
	unsigned long long MessagesSeen() const;
	unsigned long long OrdersSeen() const;
	unsigned long long PurchasesSeen() const;

	// Enchantment level of the most recent order to arrive; -1 before any has.
	//
	// The scanner picks an enchantment by clicking a measured coordinate,
	// which nothing on screen can confirm. A missed tier click only loses
	// data, but a missed enchantment click files real orders under the wrong
	// level - so this is the cheap check for it: whatever the filter was just
	// set to, the orders coming back should carry that level.
	int LastOrderEnchant() const;
	unsigned long long LastMarketTick() const { return lastMarketTick.load(); }
	unsigned long long RejectedMessages() const { return rejectedMessages.load(); }

private:
	void ListenLoop();
	void Serve(uintptr_t client);
	void Ingest(const char* body, size_t length);

	thread worker;
	atomic<bool> running{ false };
	atomic<unsigned short> boundPort{ 0 };
	atomic<unsigned long long> messagesSeen{ 0 };
	atomic<unsigned long long> ordersSeen{ 0 };
	atomic<unsigned long long> purchasesSeen{ 0 };
	atomic<int> lastOrderEnchant{ -1 };
	atomic<unsigned long long> lastMarketTick{ 0 };
	atomic<unsigned long long> rejectedMessages{ 0 };

	// really a SOCKET; kept opaque so this header stays free of winsock
	atomic<uintptr_t> listenSocket{ 0 };
	mutex clientLock;
	uintptr_t activeClient = 0;

	mutex bufferLock;
	vector<FeedOrder> buffer;

	mutex purchaseLock;
	vector<FeedPurchase> purchases;
};
