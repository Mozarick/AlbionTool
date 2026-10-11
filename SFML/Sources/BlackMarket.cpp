#include "../Headers/BlackMarket.h"
#include "../Headers/AbortSwitch.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <unordered_map>

using namespace std;


const char* const BLACK_MARKET_LOCATION = "3003";

// How long to wait for the game to re-request the list after a search before
// giving up on that item. The round trip is normally well under a second.
static const int REFRESH_TIMEOUT_MS = 4000;

// Matches CityBuyer.cpp's BLACK_MARKET_TAX. Kept as its own local constant
// rather than shared, the same way REFRESH_TIMEOUT_MS-style knobs are
// per-file here already - this file stands alone from the buy-side pairing.
static const double BLACK_MARKET_TAX = 0.04;

// The lowest bid that still clears minProfitRate over what Holdings says was
// paid, after tax. A zero or unknown cost can't have a rate applied to it -
// any positive bid is taken as clearing it rather than blocking the sale.
static long long MinProfitableBid(long long unitCost, double minProfitRate) {
	if (unitCost <= 0) {
		return 1;
	}
	double required = double(unitCost) * (1.0 + minProfitRate) / (1.0 - BLACK_MARKET_TAX);
	return (long long)ceil(required);
}


// The sell side's search box wants the tier marker in lower case: "Bag t6.0",
// not the "Bag T6.0" that ItemTable::SearchString produces for buying.
//
// Only that one letter is touched. A blanket tolower() over the whole string
// would also flatten the item name, and the names are what the game matches on.
static string SellSearchText(const string& searchText) {
	string out = searchText;

	// SearchString appends " T<tier>.<enchant>", so look for a space, a T and a
	// digit. Matching on the digit as well keeps an item whose own name happens
	// to contain " T..." from being rewritten.
	for (size_t i = 0; i + 2 < out.size(); ++i) {
		if (out[i] == ' ' && out[i + 1] == 'T' &&
			isdigit((unsigned char)out[i + 2])) {
			out[i + 1] = 't';
		}
	}

	return out;
}


string MakeOrderKey(const string& itemTypeId, int enchant) {
	string key;
	key.reserve(itemTypeId.size() + 3);
	key += itemTypeId;
	key += '#';
	key += to_string(enchant);
	return key;
}


BlackMarketSeller::BlackMarketSeller(MarketFeed& feedRef,
	const ItemTable& itemsRef)
	: feed(feedRef)
	, items(itemsRef)
{
}

void BlackMarketSeller::Reset() {
	soldKeys.clear();
}

// Empties whatever the feed has buffered, and discards it.
//
// Discarding is the point. Every stack is priced from a Drain() taken right
// after searching for that stack, so anything sitting in the buffer when the
// run starts is older data about other items - and mixing it into the first
// search's results is how the run would end up judging a bid that belongs to
// something else. This used to file the batch into a `known` list that only a
// never-called accessor read, which made it look like state the run depended
// on when the only thing that mattered was the side effect.
void BlackMarketSeller::Absorb() {
	vector<FeedOrder> batch;
	feed.Drain(batch);
}

// Narrows the list to a single quality, by coordinate.
//
// Deliberately NOT skipped when the wanted quality already matches the last
// one picked, for the same reason CityBuyer::SelectQuality is not: that
// shortcut assumes the filter survives a search and a clear-search click,
// which has never been verified. If it does reset, the bot sells whatever
// quality happens to be on the top row while believing it is filtered. Two
// clicks per stack is cheap next to selling the wrong goods.
bool BlackMarketSeller::SelectQuality(int quality) {
	const ClickRect* row = layout.quality.Row(quality);
	if (row == NULL) {
		return false;
	}

	MoveAndClickRect(layout.quality.button);
	this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

	MoveAndClickRect(*row);
	this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

	return true;
}

bool BlackMarketSeller::WaitForRefresh(unsigned long long since,
	int timeoutMs) {

	auto deadline = chrono::steady_clock::now() +
		chrono::milliseconds(timeoutMs);

	while (chrono::steady_clock::now() < deadline) {
		if (feed.MessagesSeen() > since) {
			// let the rest of the burst land before reading it
			this_thread::sleep_for(chrono::milliseconds(120));
			return true;
		}
		this_thread::sleep_for(chrono::milliseconds(15));
	}
	return false;
}

SellReport BlackMarketSeller::Run(const SellPlan& plan, Holdings& carried) {
	SellReport report;

	Absorb();

	if (carried.Empty()) {
		cout << "Nothing to sell: the bot has not bought anything.\n";
		return report;
	}

	cout << "Selling " << carried.TotalUnits() << " units from "
		<< carried.All().size() << " stacks, if it clears "
		<< (plan.minProfitRate * 100.0) << "% profit over cost\n";

	// Copy the list first: selling mutates the ledger underneath us.
	const vector<Holding> stacks = carried.All();

	for (const Holding& stack : stacks) {
		// Between stacks: the previous sale is finished and the ledger has
		// already been updated for it, so there is nothing half-done to leave
		// behind. Never inside the loop that clicks Sell.
		if (Abort::Requested()) {
			cout << "\n" << Abort::HotkeyName() << " - stopping. "
				<< report.sold << " stacks sold, the rest are still held.\n";
			break;
		}

		if (plan.maxSales > 0 && report.sold >= plan.maxSales) {
			break;
		}
		if (stack.searchText.empty()) {
			cout << "  skip " << stack.itemTypeId << " (no name)\n";
			continue;
		}

		long long minBid = MinProfitableBid(stack.unitCost, plan.minProfitRate);

		// What actually gets typed. Logged in this form rather than as the
		// ledger's own spelling, so the log shows the keystrokes that were sent.
		const string typed = SellSearchText(stack.searchText);

		++report.attempted;
		cout << "  [" << report.attempted << "] " << typed
			<< " q" << stack.quality << " x" << stack.quantity
			<< "  cost " << stack.unitCost << "  need bid >= " << minBid << "\n";

		int remaining = stack.quantity;
		int rounds = 0;

		// One click sells the whole stack, so a handful of passes is plenty -
		// more than one is only needed when a single buy order wants less
		// than the stack holds and the rest has to go to the next order.
		// This used to be stack.quantity + 8 because the code wrongly
		// believed each click sold a single unit.
		const int maxRounds = 12;

		// Last order clicked, to notice a sell that never registered.
		long long lastOrderId = 0;
		int lastOrderAmount = 0;

		while (remaining > 0 && rounds++ < maxRounds) {
			// Searching makes the game re-request the list, so the feed always
			// describes the state right now rather than the state at scan time.
			unsigned long long before = feed.MessagesSeen();

			// Clear first, exactly like the buying side does. Without this the
			// box still holds the previous stack's name and the new text is
			// appended to it - the first stack searched fine and every one
			// after it searched for nonsense, which reads as "no bid left for
			// it" rather than as a typing bug.
			ClearItemName();
			MoveAndClickRect(layout.search);
			TypeString(typed);

			// Let the list actually redraw before anything is clicked in it.
			// WaitForRefresh below only proves the *feed* has the new prices,
			// which arrives from the game server and can land before the client
			// has finished repainting the rows. Clicking into a list that is
			// still the previous item's is how a sell ends up on the wrong row.
			this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

			if (!WaitForRefresh(before, REFRESH_TIMEOUT_MS)) {
				cout << "      no refresh from the feed, moving on\n";
				break;
			}

			// Then narrow it to this stack's quality. The search box takes a
			// name, a tier and an enchant but not a quality, so up to this point
			// the list still holds every quality of the item - and the sell click
			// only ever lands on the top row.
			unsigned long long beforeQuality = feed.MessagesSeen();

			if (!SelectQuality(stack.quality)) {
				cout << "      quality " << stack.quality
					<< " is not one the filter offers, leaving this stack\n";
				break;
			}

			// Best effort. The game does not always re-request after a filter
			// change - if the filter did not alter which orders match, there is
			// nothing new to send - and the sleeps inside SelectQuality have
			// already given the list time to redraw either way.
			WaitForRefresh(beforeQuality, REFRESH_TIMEOUT_MS);

			vector<FeedOrder> fresh;
			feed.Drain(fresh);

			// Best bid this stack can actually fill. The black market takes
			// anything at or above the quality it asked for, so an order for
			// Good is fair game for a Masterpiece.
			const FeedOrder* live = NULL;
			for (const auto& order : fresh) {
				if (!order.isBuyOrder ||
					order.locationId != BLACK_MARKET_LOCATION) {
					continue;
				}
				if (order.groupTypeId != stack.itemTypeId ||
					order.enchant != stack.enchant) {
					continue;
				}
				if (stack.quality < order.quality) {
					continue;
				}
				if (live == NULL || order.price > live->price) {
					live = &order;
				}
			}

			if (live == NULL) {
				++report.vanished;
				cout << "      no bid left for it, moving on\n";
				break;
			}
			if (live->price < minBid) {
				++report.tooCheap;
				cout << "      bid is only " << live->price << ", need >= "
					<< minBid << " for " << (plan.minProfitRate * 100.0)
					<< "% profit, moving on\n";
				break;
			}

			// A sell click that did not register leaves the same order sitting
			// there with the same stock. Clicking again would fail the same
			// way, so the stack is left alone rather than hammered.
			if (live->id == lastOrderId && live->amount >= lastOrderAmount) {
				cout << "      sale did not register, leaving this stack\n";
				break;
			}
			lastOrderId = live->id;
			lastOrderAmount = live->amount;

			// The sell button moves the WHOLE stack in one click, limited by
			// how many units the buy order still wants. Anything left over
			// goes round again for the next-best order.
			int qty = remaining;
			if (live->amount > 0 && live->amount < qty) {
				qty = live->amount;
			}

			// Two clicks, mechanically, with no OCR between them: the row, then
			// the confirm the row opens. Nothing here reads the screen - the
			// prices already came from the feed, and the only thing the dialog
			// adds is a button at a fixed place.
			MoveAndClickRect(layout.sell);
			this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

			MoveAndClickRect(layout.confirmSell);
			this_thread::sleep_for(chrono::milliseconds(Config.MenuOpenDelayMs));

			remaining -= qty;
			report.sold += qty;

			// Net of the black market's 4% sale tax, so the figure is what
			// actually lands rather than the headline bid.
			report.silver +=
				(long long)(double(live->price) * qty * (1.0 - BLACK_MARKET_TAX));
			carried.Remove(stack.itemTypeId, stack.enchant, stack.quality, qty);

			cout << "      sold " << qty << " at " << live->price
				<< " each (" << remaining << " left)\n";
		}
	}

	cout << "Done. attempted=" << report.attempted
		<< " sold=" << report.sold
		<< " vanished=" << report.vanished
		<< " tooCheap=" << report.tooCheap
		<< " silver=" << report.silver << "\n";

	return report;
}
