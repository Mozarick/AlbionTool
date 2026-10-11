// Proves the C++ side of the pipeline: starts MarketFeed, waits for AlbionFeed
// to POST real market data, and prints what was parsed together with the search
// string the bot would type into the game.
//
// Run this, then start the feed against it:
//     D:\AlbionTool\bin\AlbionFeed\AlbionFeed.exe --port 5000
//
// It also prints the purchase events AlbionFeed forwards when something is
// bought in game, which is what CityBuyer waits on instead of guessing from the
// offer list.
//
// Usage: FeedTest.exe [seconds]

#include "../SFML/Headers/MarketFeed.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

using namespace std;


int main(int argc, char** argv) {
	int seconds = argc > 1 ? atoi(argv[1]) : 90;

	ItemTable items;
	if (!items.Load("D:/AlbionTool/OpenCV_and_tesseract/SFML/items.tsv")) {
		cout << "could not load items.tsv, names will be blank\n";
	}

	MarketFeed feed;
	if (!feed.Start(5000)) {
		cout << "could not start the feed\n";
		return 1;
	}

	cout << "waiting " << seconds << "s for data. Browse the market in game.\n\n";

	auto deadline = chrono::steady_clock::now() + chrono::seconds(seconds);
	vector<FeedOrder> batch;
	vector<FeedPurchase> bought;
	unsigned long long printed = 0;

	while (chrono::steady_clock::now() < deadline) {
		this_thread::sleep_for(chrono::milliseconds(200));

		feed.DrainPurchases(bought);
		for (const FeedPurchase& purchase : bought) {
			printf("PURCHASE order=%lld amount=%d %s\n", purchase.orderId,
				purchase.amount,
				purchase.confirmed ? "confirmed" : "sent");
		}

		feed.Drain(batch);
		if (batch.empty()) {
			continue;
		}

		printf("\n--- %zu orders (messages=%llu, total orders=%llu) ---\n",
			batch.size(), feed.MessagesSeen(), feed.OrdersSeen());
		printf("%-34s %2s %2s %12s %5s %-7s %-6s %s\n",
			"ItemTypeId", "T", "en", "silver", "amt", "type", "loc",
			"search string");

		size_t show = batch.size() < 12 ? batch.size() : 12;
		for (size_t i = 0; i < show; ++i) {
			const FeedOrder& o = batch[i];
			string search = items.SearchString(o.itemTypeId, o.tier, o.enchant);

			printf("%-34s %2d %2d %12lld %5d %-7s %-6s %s\n",
				o.itemTypeId.c_str(), o.tier, o.enchant, o.price, o.amount,
				o.isBuyOrder ? "BUY" : "sell",
				o.locationId.c_str(), search.c_str());
		}
		if (batch.size() > show) {
			printf("... and %zu more\n", batch.size() - show);
		}
		printed += batch.size();
	}

	feed.Stop();
	printf("\ndone. %llu orders in %llu messages, %llu purchase events.\n",
		feed.OrdersSeen(), feed.MessagesSeen(), feed.PurchasesSeen());
	return 0;
}
