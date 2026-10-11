#include "../Headers/MarketScanner.h"
#include "../Headers/AbortSwitch.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace std;


// ------------------------------------------------------------ CategoryTable

bool CategoryTable::Load(const string& path) {
	ifstream fin(path);
	if (!fin) {
		cout << "CategoryTable: cannot open " << path << '\n';
		return false;
	}

	rows.clear();
	rows.reserve(64);

	string line;
	while (getline(fin, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}

		// key \t menu_top \t menu_sub \t slug_top \t slug_sub
		string parts[5];
		size_t at = 0;
		int found = 0;
		for (; found < 5; ++found) {
			size_t tab = line.find('\t', at);
			if (tab == string::npos) {
				parts[found] = line.substr(at);
				++found;
				break;
			}
			parts[found] = line.substr(at, tab - at);
			at = tab + 1;
		}
		if (found < 3 || parts[0].empty()) {
			continue;
		}

		MarketCategory row;
		row.key = parts[0];
		row.menuTop = parts[1];
		row.menuSub = parts[2];
		row.slugTop = parts[3];
		row.slugSub = parts[4];
		rows.push_back(std::move(row));
	}

	cout << "CategoryTable: loaded " << rows.size() << " categories\n";
	return !rows.empty();
}

size_t CategoryTable::Size() const {
	return rows.size();
}

const vector<MarketCategory>& CategoryTable::All() const {
	return rows;
}

const MarketCategory* CategoryTable::Find(const string& key) const {
	for (const auto& row : rows) {
		if (row.key == key) {
			return &row;
		}
	}
	return NULL;
}


// ------------------------------------------------------------ MarketScanner

void MarketScanner::Wait(int milliseconds) {
	const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(milliseconds);
	do {
		if (service) service();
		const auto left = chrono::duration_cast<chrono::milliseconds>(
			deadline - chrono::steady_clock::now()).count();
		if (left <= 0) break;
		this_thread::sleep_for(chrono::milliseconds(min<long long>(left, 25)));
	} while (chrono::steady_clock::now() < deadline);
}

MarketScanner::MarketScanner(MarketFeed& feedRef)
	: feed(feedRef)
	, pageTimeoutMs(Config.PageTimeoutMs)
	, quietClicksToStop(Config.PageQuietClicks)
	, pageSize(Config.PageSize)
{
}

// ------------------------------------------------------- waiting for a redraw

unsigned long long MarketScanner::RegionHash(const ClickRect& area) {
	ScreenShot region(area.x1, area.y1,
		area.x2 - area.x1, area.y2 - area.y1);

	const cv::Mat& frame = capturer.Grab(region);
	if (frame.empty()) {
		return 0;
	}

	// FNV-1a over the pixels. Alpha is skipped: BitBlt from the screen leaves
	// it undefined, so hashing it would make identical frames hash differently
	// and the "has it stopped changing" test would never come true.
	unsigned long long hash = 1469598103934665603ULL;
	for (int row = 0; row < frame.rows; ++row) {
		const unsigned char* pixel = frame.ptr<unsigned char>(row);
		const int bytes = frame.cols * 4;
		for (int i = 0; i < bytes; i += 4) {
			hash = (hash ^ pixel[i]) * 1099511628211ULL;
			hash = (hash ^ pixel[i + 1]) * 1099511628211ULL;
			hash = (hash ^ pixel[i + 2]) * 1099511628211ULL;
		}
	}

	// 0 is the "grab failed" answer, so never return it for a real frame
	return hash == 0 ? 1 : hash;
}

void MarketScanner::SettleRegion(const ClickRect& area,
	unsigned long long before, int timeoutMs) {

	const int step = Config.PollIntervalMs > 0 ? Config.PollIntervalMs : 15;
	const auto started = chrono::steady_clock::now();
	const auto deadline = started + chrono::milliseconds(timeoutMs);

	// Never return before this much has passed, however quiet the region
	// looks. ChooseCategory is called exactly once per category with nothing
	// left to retry, so returning early costs the whole category, while
	// waiting a few extra milliseconds costs nothing. Cheap insurance against
	// the one way this can
	// be fooled: the cursor travelling over the panel highlights a row, which
	// is a real repaint, and if the dropdown itself has not begun to draw yet
	// the frames either side of that highlight are legitimately identical.
	const auto floorAt = started + chrono::milliseconds(
		timeoutMs < 80 ? timeoutMs : 80);

	// phase 1: wait for the menu to start painting
	unsigned long long current = before;
	while (chrono::steady_clock::now() < deadline) {
		current = RegionHash(area);
		if (current == 0) {
			// cannot see the screen at all; fall back to the old blind wait
			// rather than spinning on a failing capture
			Wait(timeoutMs);
			return;
		}
		if (current != before) {
			break;
		}
		Wait(step);
	}

	// Phase 2: wait for it to stop changing. Two consecutive matching grabs,
	// not one - a single match is also what a panel that has not started
	// drawing yet looks like, and one extra poll interval is a far cheaper
	// price than reading a menu that is not on screen.
	int stable = 0;
	while (chrono::steady_clock::now() < deadline) {
		Wait(step);

		unsigned long long next = RegionHash(area);
		if (next != 0 && next == current) {
			if (++stable >= 2 && chrono::steady_clock::now() >= floorAt) {
				return;
			}
			continue;
		}

		stable = 0;
		current = next;
	}
}

void MarketScanner::ClickAndSettle(const ClickRect& target,
	const ClickRect& watch, int timeoutMs) {

	const unsigned long long before = RegionHash(watch);
	MoveAndClickRect(target);
	SettleRegion(watch, before, timeoutMs);
}


bool MarketScanner::ChooseCategory(const MarketCategory& category) {
	const CategoryLayout::Entry* entry = layout.categories.Find(category.key);
	if (entry == NULL) {
		cout << "  \"" << category.key << "\" (" << category.menuTop << " > "
			<< category.menuSub << ") has no measured coordinates - measure "
			"it with FindCoords.ps1 and add it to CategoryLayout in "
			"MarketScanner.h\n";
		return false;
	}

	ClickAndSettle(layout.typeButton, layout.typeList, Config.MenuOpenDelayMs);

	// The submenu opens on hover, not on click: rest on the parent row, then
	// slide straight right into the column beside it, holding the same row,
	// before dropping onto the sub-category row. Clicking the parent instead
	// would collapse the menu. Both rows are measured coordinate, not OCR -
	// see CategoryLayout.
	//
	// Every one of these three movements now lands somewhere different each
	// time. The two hovers used to go to the exact centre of their rectangle -
	// they were the only movements in the program that did, because they went
	// through MoveToScreenPoint with coordinates the caller had already
	// averaged down to a point. Widening the boxes in CategoryLayout would
	// have changed nothing at all while that stood.
	const ClickRect& parent = entry->parent;
	const ClickRect& sub = entry->sub;

	// The row that got hovered, in screen pixels. The slide sideways has to
	// hold it: leaving the row is what closes the submenu again.
	const int rowY = MoveToRect(parent);

	// Baseline the submenu column before the hover that opens it, for the same
	// reason ClickAndSettle takes its baseline before the click.
	const unsigned long long beforeSub = RegionHash(layout.subList);

	// Sideways into the child column, holding that row: x varies, y does not.
	MoveToRect(sub, rowY);

	// the submenu is drawn in response to the hover, so wait for it too
	SettleRegion(layout.subList, beforeSub, Config.MenuOpenDelayMs);

	MoveAndClickRect(sub);

	return true;
}

// Opens the tier dropdown and clicks the row for `tier`, by coordinate.
//
// This used to run OCR over the open dropdown and click wherever "Tier 5" was
// found. It was the worst-behaved part of the bot: tesseract reads that row
// back as "Tier S" often enough that whole tiers were skipped, and the retry
// loop that used to live here existed purely to paper over the misreads. The
// rows never move, so there was nothing for OCR to discover in the first
// place - see MarketUi.h.
//
// The cost of dropping OCR is that a tier click can no longer be verified: if
// the dropdown failed to open, this still reports success and the pass runs
// against whatever tier was already selected. The per-tier order counts printed
// by ScanCategory are what show that up.
bool MarketScanner::ChooseTier(int tier) {
	const ClickRect* row = layout.tiers.Row(tier);
	if (row == NULL) {
		cout << "  tier " << tier << " has no measured coordinates (only T"
			<< TierLayout::firstTier << "-T" << TierLayout::lastTier << " do)\n"
			<< "    measure it with FindCoords.ps1 and add it to MarketUi.h\n";
		return false;
	}

	// The dropdown has to be fully drawn before a row underneath it can be
	// clicked, or the click lands on whatever the menu has not covered yet.
	// Watched, not slept through: the panel is only observed to answer "has it
	// painted", never read - the row is still clicked by measured coordinate.
	ClickAndSettle(layout.tiers.button, layout.tierPanel,
		Config.MenuOpenDelayMs);

	MoveAndClickRect(*row);

	// Picking a tier makes the game send a fresh listing, so the feed is the
	// real signal that the filter has been applied - and it is a better one
	// than any pixel, because it is the same event the paging loop below is
	// about to wait for. Waiting on it here rather than sleeping a flat
	// menu_open_delay_ms means a fast server pays the round trip once instead
	// of the round trip plus a fixed pad.
	const unsigned long long mark = feed.MessagesSeen();
	const int step = Config.PollIntervalMs > 0 ? Config.PollIntervalMs : 15;
	const auto deadline = chrono::steady_clock::now() +
		chrono::milliseconds(Config.MenuOpenDelayMs);

	while (chrono::steady_clock::now() < deadline) {
		if (feed.MessagesSeen() > mark) {
			break;
		}
		Wait(step);
	}

	return true;
}

// Opens the enchantment dropdown and clicks the row for `level`, by
// coordinate, exactly as ChooseTier does - and for the same reason: the rows
// never move, and "Enchantment 0" is precisely the kind of text tesseract
// reads back as "Enchantment O".
//
// A misread here would be worse than the tier misreads that killed OCR off:
// a skipped tier loses data, but a wrongly picked enchantment row files real
// orders under the wrong level. That is also why this one is worth verifying,
// which ChooseTier never could - see the enchant check in ScanCategory.
bool MarketScanner::ChooseEnchantment(int level) {
	const ClickRect* row = layout.enchants.Row(level);
	if (row == NULL) {
		cout << "  enchantment " << level << " has no measured coordinates "
			<< "(only " << EnchantmentLayout::firstLevel << '-'
			<< EnchantmentLayout::lastLevel << " do)\n"
			<< "    measure it with FindCoords.ps1 and add it to MarketUi.h\n";
		return false;
	}

	ClickAndSettle(layout.enchants.button, layout.enchantPanel,
		Config.MenuOpenDelayMs);

	MoveAndClickRect(*row);

	// Same as ChooseTier: the filter change makes the game send a fresh
	// listing, and that packet is a better "it applied" signal than any pixel.
	const unsigned long long mark = feed.MessagesSeen();
	const int step = Config.PollIntervalMs > 0 ? Config.PollIntervalMs : 15;
	const auto deadline = chrono::steady_clock::now() +
		chrono::milliseconds(Config.MenuOpenDelayMs);

	while (chrono::steady_clock::now() < deadline) {
		if (feed.MessagesSeen() > mark) {
			break;
		}
		Wait(step);
	}

	return true;
}

// Clicks the pager until the feed stops reporting anything new, up to
// maxClicks times. The feed, not the screen, is the authority on whether
// there was more to load.
//
// "Nothing new" is only believed after quietClicksToStop consecutive silent
// clicks. A single one proves nothing: the intended end-of-listing signal and a
// slow round trip to a distant server are the same silence from in here, and
// treating the first quiet click as the end is what made scans give up after a
// few tabs with no error to show for it. Retrying costs one more click and one
// more timeout per category; stopping early costs most of the listing.
int MarketScanner::PageBudgetMs(bool firstTry) const {
	// Nothing measured yet - this is the first page of the run, so there is
	// no better answer than the configured worst case.
	if (observedPageMs < 0) {
		return pageTimeoutMs;
	}

	// Six times the observed latency plus a fixed floor. The multiplier is
	// deliberately generous: being early here means declaring the end of a
	// listing that had more in it, which loses data silently, while being late
	// only costs milliseconds. A repeat click is confirming silence on a
	// request the game has already had time to answer, so it needs far less.
	const int slack = firstTry ? 6 : 3;
	int budget = observedPageMs * slack + (firstTry ? 200 : 120);

	// never longer than the configured ceiling, and never so short that a
	// single scheduling hiccup reads as the end of the list
	const int floor = firstTry ? 250 : 150;
	if (budget < floor) {
		budget = floor;
	}
	if (budget > pageTimeoutMs) {
		budget = pageTimeoutMs;
	}
	return budget;
}

int MarketScanner::PageThroughList(int maxClicks) {
	int pages = 0;
	int quiet = 0;
	const int quietLimit = quietClicksToStop > 0 ? quietClicksToStop : 1;
	const int step = Config.PollIntervalMs > 0 ? Config.PollIntervalMs : 15;

	for (int i = 0; i < maxClicks; ++i) {
		// Before the next pager click, so a stop never leaves a tab request in
		// flight with nothing waiting to file what comes back.
		if (Abort::Requested()) {
			break;
		}

		unsigned long long before = feed.MessagesSeen();
		const unsigned long long ordersBefore = feed.OrdersSeen();

		MoveAndClickRect(layout.nextPage);

		// Timed from here, *after* the click has landed - not from before the
		// call. MoveAndClickRect spends move_min_ms..move_max_ms travelling the
		// cursor before the button is ever pressed, so a deadline started ahead
		// of it could be most of a second stale by the time there is anything to
		// wait for, and a short budget would already have expired. That reads as
		// "nothing arrived", which ends the listing early and drops the rest of
		// it with nothing in the log to say so.
		const auto clickedAt = chrono::steady_clock::now();

		// Give the game a moment before watching for data. On a far server
		// the click and the redraw are not instantaneous, and firing the
		// next tab click the instant the previous batch lands is faster than
		// the UI can keep up with.
		Wait(Config.PageClickDelayMs);

		const int budget = PageBudgetMs(quiet == 0);
		auto deadline = clickedAt + chrono::milliseconds(budget);
		bool gotMore = false;

		while (chrono::steady_clock::now() < deadline) {
			if (feed.MessagesSeen() > before) {
				gotMore = true;
				break;
			}
			// Waiting out a page budget is the longest a scan goes without
			// looking up, so poll here to catch a press that would otherwise
			// be released before the next checkpoint. Latch only: the tab
			// switch has already been clicked and the orders it brings back
			// are worth filing, so the actual stop happens at the top of the
			// next round.
			(void)Abort::Requested();
			Wait(step);
		}

		if (gotMore) {
			// Fold this round trip into the running estimate the next budget
			// is derived from. Weighted towards the older value so one slow
			// page does not blow the budget out for the rest of the scan, but
			// a genuine change of server settles within a handful of pages.
			//
			// Measured HERE, before the settle wait below, so it keeps meaning
			// "how long until the page started arriving" - which is what the
			// budget is a deadline for. Timing it after the page has finished
			// would inflate every future budget by the length of a page.
			const int elapsed = int(chrono::duration_cast<chrono::milliseconds>(
				chrono::steady_clock::now() - clickedAt).count());

			observedPageMs = observedPageMs < 0
				? elapsed
				: (observedPageMs * 3 + elapsed) / 4;

			++pages;
			quiet = 0;

			if (pageSize <= 0) {
				continue;
			}

			// Let the page finish arriving before counting it.
			//
			// The wait above returns on the FIRST packet and a page comes in
			// several, so a count taken there would be a fraction of the real
			// one - and a fraction of a full page looks exactly like a short
			// page. That is the one thing that must not be guessed wrong: a
			// wrong "short" ends the listing and drops the rest of it
			// silently, which is the failure this scanner has been bitten by
			// before. Keep extending while packets are still arriving and stop
			// at the first quiet interval, bounded by the same budget.
			unsigned long long seen = feed.MessagesSeen();
			const auto settleBy = chrono::steady_clock::now() +
				chrono::milliseconds(budget);

			while (chrono::steady_clock::now() < settleBy) {
				// Latch only. This loop is what decides whether a page was
				// short, and cutting it early would make a full page look
				// short - which ends the listing and drops the rest of it
				// silently, the one failure this settle wait exists to stop.
				(void)Abort::Requested();
				Wait(step);

				const unsigned long long now = feed.MessagesSeen();
				if (now == seen) {
					break;
				}
				seen = now;
			}

			const unsigned long long onPage = feed.OrdersSeen() - ordersBefore;

			// A full tab is exactly pageSize, so fewer means the listing ran
			// out on this one. Zero is NOT treated as short: no orders at all
			// after a click that did produce packets is something else
			// entirely, and the quiet-click test is the honest way to handle
			// it.
			if (onPage > 0 && onPage < (unsigned long long)pageSize) {
				cout << "    tab " << pages << " held " << onPage
					<< " of " << pageSize
					<< ", so the listing ends here\n";
				break;
			}
			continue;
		}

		if (++quiet < quietLimit) {
			cout << "    nothing within " << budget << "ms after tab "
				<< pages << ", trying again (" << quiet << '/' << quietLimit
				<< ")\n";
			continue;
		}

		// Say why the walk stopped, and how confident that is. The budget is
		// derived from the latency actually measured during this run, so if
		// this count is lower than the number of tabs really present in game,
		// raise page_timeout_ms (its ceiling) or page_quiet_clicks.
		cout << "    stopped after " << pages << " tab switch"
			<< (pages == 1 ? "" : "es") << ": " << quietLimit
			<< " clicks in a row with nothing new within " << budget
			<< "ms each";
		if (observedPageMs >= 0) {
			cout << " (pages were arriving in ~" << observedPageMs << "ms)";
		}
		cout << '\n';
		break;
	}

	return pages;
}

bool MarketScanner::ScanName(const string& searchText,
	vector<FeedOrder>& batch) {

	batch.clear();
	if (searchText.empty()) {
		return false;
	}

	// Clearing the field causes its own unfiltered market refresh. Let that
	// finish and throw it away before establishing the mark for this name;
	// otherwise the first search can be declared complete by data belonging to
	// the previous item (the same failure CityBuyer::SearchFor guards against).
	ClearItemName();
	Wait(Config.MenuOpenDelayMs);
	vector<FeedOrder> stale;
	feed.Drain(stale);

	const unsigned long long messagesBefore = feed.MessagesSeen();
	const unsigned long long ordersBefore = feed.OrdersSeen();

	MoveAndClickRect(layout.search);
	TypeString(searchText);

	const int step = Config.PollIntervalMs > 0 ? Config.PollIntervalMs : 15;
	const auto deadline = chrono::steady_clock::now() +
		chrono::milliseconds(pageTimeoutMs);
	bool refreshed = false;

	while (chrono::steady_clock::now() < deadline) {
		if (feed.MessagesSeen() > messagesBefore) {
			refreshed = true;
			break;
		}
		(void)Abort::Requested();
		Wait(step);
	}

	if (!refreshed) {
		// Do not let a late response to this failed attempt become the next
		// name's result. The next call also clears/drains, but emptying here makes
		// the ownership of this attempt explicit.
		feed.Drain(stale);
		return false;
	}

	// A listing arrives as a burst of packets. Wait for the first quiet poll so
	// both the short-page decision and the batch handed to the caller describe
	// the complete first page rather than its first fragment.
	unsigned long long seen = feed.MessagesSeen();
	const auto settleBy = chrono::steady_clock::now() +
		chrono::milliseconds(pageTimeoutMs);
	while (chrono::steady_clock::now() < settleBy) {
		(void)Abort::Requested();
		Wait(step);
		const unsigned long long now = feed.MessagesSeen();
		if (now == seen) {
			break;
		}
		seen = now;
	}

	const unsigned long long firstPageOrders =
		feed.OrdersSeen() - ordersBefore;
	int pages = 0;

	// Zero is a conclusive empty response here: it came from the search itself,
	// not from a speculative pager click. A non-empty short first page is also
	// the end. Only a full page can have anything behind it.
	if (firstPageOrders > 0 &&
		(pageSize <= 0 || firstPageOrders >= (unsigned long long)pageSize)) {
		pages = PageThroughList(maxPagesPerTier);
	}

	feed.Drain(batch);
	cout << "    first page " << firstPageOrders << " orders, " << pages
		<< " extra tab switch" << (pages == 1 ? "" : "es") << '\n';
	return true;
}

bool MarketScanner::ScanCategory(const MarketCategory& category,
	const vector<int>& tiers, const vector<int>& enchants,
	const StepStartFn& onStart, const StepDoneFn& onDone) {

	cout << "Scanning " << category.menuTop << " > " << category.menuSub
		<< '\n';

	// The black market is never filtered by enchantment, so an enchant list
	// passed alongside an empty tier list is ignored rather than obeyed.
	const bool splitByEnchant = !tiers.empty() && !enchants.empty();

	// Wipe whatever the previous category left open first, so this scan starts
	// from a clean list. Done up front rather than as cleanup afterwards: if a
	// category fails partway through, the next one still begins from a known
	// state.
	// Watch the result area rather than sleeping through the redraw: clearing
	// the tabs repaints the listing, so there is an observable event to wait
	// for and no reason to pay a flat delay when it lands early.
	ClickAndSettle(layout.resetTabs, layout.typeList, Config.MenuOpenDelayMs);

	if (!ChooseCategory(category)) {
		// The category itself never opened - the game did not react to the
		// clicks at all (lag, or the window lost focus). Every step planned
		// for this category is unreachable, so all of them are reported as
		// failed rather than silently skipped.
		if (tiers.empty()) {
			if (onStart) onStart(0);
			if (onDone) onDone(0, false);
		}
		else {
			for (int tier : tiers) {
				// one report per planned step, and a step is one
				// (tier, enchantment) pair - see the loops below
				const size_t steps = splitByEnchant ? enchants.size() : 1;
				for (size_t i = 0; i < steps; ++i) {
					if (onStart) onStart(tier);
					if (onDone) onDone(tier, false);
				}
			}
		}
		return false;
	}

	unsigned long long startOrders = feed.OrdersSeen();

	if (tiers.empty()) {
		// Black market: never touch the tier dropdown, but keep switching tab
		// until the feed goes quiet, exactly like the city market does. The
		// tab button is what walks the listing, so stopping after one click
		// leaves most of the black market unscanned. The category opened
		// fine, so this always counts as done rather than failed.
		if (onStart) onStart(0);
		int pages = PageThroughList(maxPagesPerTier);
		cout << "  " << pages << " tab switches, "
			<< (feed.OrdersSeen() - startOrders) << " orders\n";
		if (onDone) onDone(0, true);
	}
	else {
		// One tier at a time, in the given order. Each pass is
		//   open the tier dropdown -> click that tier
		//   -> page the list to the bottom
		//   -> back up to the dropdown for the next tier
		// The paging has to finish before the next tier is picked, otherwise
		// the rest of the current tier's listings are never requested and
		// the feed never sees them.
		//
		// With enchantments split out, each tier is walked once per level
		// instead of once in total: the tier is picked, then the enchantment,
		// then the list is paged, and only then does the next level start.
		// That is the whole point - one tier of a popular item overruns the
		// game's twenty-tab cap when every level is listed together, and the
		// overflow is dropped silently.
		for (int tier : tiers) {
			// Without this the run would still walk every remaining tier,
			// opening the dropdown for each one, only for PageThroughList to
			// return straight back out. The steps left behind stay Pending on
			// the scanning screen, which is what they are.
			if (Abort::Latched()) {
				break;
			}

			if (!splitByEnchant) {
				if (onStart) onStart(tier);

				if (!ChooseTier(tier)) {
					// say so loudly: a silently skipped tier is a hole in the
					// scan, not a tier the game simply does not offer
					cout << "  !! tier " << tier << " SKIPPED - nothing scanned for it\n";
					if (onDone) onDone(tier, false);
					continue;
				}

				int pages = PageThroughList(maxPagesPerTier);
				cout << "  tier " << tier << ": " << pages << " extra pages, "
					<< (feed.OrdersSeen() - startOrders) << " orders so far\n";
				if (onDone) onDone(tier, true);
				continue;
			}

			// The tier dropdown is reopened for every level rather than once
			// per tier. It costs one extra menu click, and it means a tier
			// filter the game quietly dropped somewhere in the middle of the
			// enchantment walk cannot poison the levels that follow it.
			for (int enchant : enchants) {
				if (Abort::Latched()) {
					break;
				}

				if (onStart) onStart(tier);

				if (!ChooseTier(tier)) {
					cout << "  !! tier " << tier << " SKIPPED - nothing scanned for it\n";
					if (onDone) onDone(tier, false);
					continue;
				}

				if (!ChooseEnchantment(enchant)) {
					cout << "  !! t" << tier << '.' << enchant
						<< " SKIPPED - nothing scanned for it\n";
					if (onDone) onDone(tier, false);
					continue;
				}

				const unsigned long long beforeStep = feed.OrdersSeen();
				int pages = PageThroughList(maxPagesPerTier);
				const unsigned long long got = feed.OrdersSeen() - beforeStep;

				cout << "  t" << tier << '.' << enchant << ": " << pages
					<< " extra pages, " << got << " orders\n";

				// Nothing on screen can confirm which row the enchantment
				// click landed on, but the orders themselves carry the level,
				// so a wrong row shows up here rather than as mislabelled data
				// discovered weeks later. Only meaningful when this step
				// actually brought something back.
				const int arrived = feed.LastOrderEnchant();
				if (got > 0 && arrived >= 0 && arrived != enchant) {
					cout << "    !! asked for enchantment " << enchant
						<< " but orders came back at " << arrived
						<< " - the dropdown click probably missed;"
						" re-measure EnchantmentLayout in MarketUi.h\n";
				}

				if (onDone) onDone(tier, true);
			}
		}
	}

	cout << "  done, " << (feed.OrdersSeen() - startOrders)
		<< " orders from this category\n";
	return true;
}
