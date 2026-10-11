#pragma once
#include <functional>
#include <string>
#include <vector>

#include "MarketFeed.h"
#include "MarketUi.h"
#include "MoveToPosition.h"
#include "ScreenCapture.h"


// One row of categories.tsv: a marketplace menu path, taken from the game's
// own data (items.xml shopcategory pairs, labelled from localization.xml)
// rather than guessed.
struct MarketCategory {
	string key;       // weapons_bow
	string menuTop;   // "Weapons"
	string menuSub;   // "Bow"
	string slugTop;   // weapons
	string slugSub;   // bow
};

class CategoryTable {
public:
	bool Load(const string& path);
	size_t Size() const;
	const vector<MarketCategory>& All() const;
	const MarketCategory* Find(const string& key) const;

private:
	vector<MarketCategory> rows;
};


// The item-type menu: one row per categories.tsv entry, measured by hand with
// FindCoords.ps1 - not OCR. This used to be read by OCR on the assumption
// that the entries move depending on what's listed; they don't. Both columns
// draw on a fixed 27px grid regardless of content, exactly like TierLayout
// below. `parent` is where to hover in the left-hand category column to open
// the submenu; `sub` is where to click in the right-hand column once open.
struct CategoryLayout {
	struct Entry {
		const char* key; // matches MarketCategory::key from categories.tsv
		ClickRect parent;
		ClickRect sub;
	};

	// Rows are on a 27px pitch and each submenu's "All" lines up with its own
	// parent row, so the whole table is one grid rather than 46 independent
	// measurements - which is what let the Capes column be derived and then
	// checked against a screenshot instead of measured row by row.
	//
	// Boxes are WIDE in x and tight in y, and that asymmetry is the point. The
	// old table was 2x2 px, which after ClickInside is nine pixels - a fixed
	// point in all but name. The reason given for it was that rows are only
	// 27px apart, but that is a constraint on Y alone: the menu columns are
	// ~130px wide and nothing sits beside a row to hit by mistake. So x varies
	// over 55px (left) and 75px (right), y stays inside +/-2 of the row centre,
	// and every click lands somewhere different without ever reaching a
	// neighbour.
	//
	// Order matches categories.tsv.
	Entry entries[46] = {
		{ "armors_cloth_armor",         { 790, 349, 845, 353 }, { 920, 376, 995, 380 } },
		{ "armors_leather_armor",       { 790, 349, 845, 353 }, { 920, 403, 995, 407 } },
		{ "armors_plate_armor",         { 790, 349, 845, 353 }, { 920, 430, 995, 434 } },
		{ "bags_bags",                  { 790, 484, 845, 488 }, { 920, 511, 995, 515 } },
		{ "bags_satchels",              { 790, 484, 845, 488 }, { 920, 538, 995, 542 } },
		{ "capes_cape",                 { 790, 457, 845, 461 }, { 920, 484, 995, 488 } },
		{ "capes_bridgewatch",          { 790, 457, 845, 461 }, { 920, 511, 995, 515 } },
		{ "capes_fortsterling",         { 790, 457, 845, 461 }, { 920, 538, 995, 542 } },
		{ "capes_lymhurst",             { 790, 457, 845, 461 }, { 920, 565, 995, 569 } },
		{ "capes_martlock",             { 790, 457, 845, 461 }, { 920, 592, 995, 596 } },
		{ "capes_thetford",             { 790, 457, 845, 461 }, { 920, 619, 995, 623 } },
		{ "capes_caerleon",             { 790, 457, 845, 461 }, { 920, 646, 995, 650 } },
		{ "capes_brecilien",            { 790, 457, 845, 461 }, { 920, 673, 995, 677 } },
		{ "capes_heretic",              { 790, 457, 845, 461 }, { 920, 700, 995, 704 } },
		{ "capes_undead",               { 790, 457, 845, 461 }, { 920, 727, 995, 731 } },
		{ "capes_keeper",               { 790, 457, 845, 461 }, { 920, 754, 995, 758 } },
		{ "capes_morgana",              { 790, 457, 845, 461 }, { 920, 781, 995, 785 } },
		{ "capes_avalonian",            { 790, 457, 845, 461 }, { 920, 808, 995, 812 } },
		{ "capes_demon",                { 790, 457, 845, 461 }, { 920, 835, 995, 839 } },
		{ "capes_smuggler",             { 790, 457, 845, 461 }, { 920, 862, 995, 866 } },
		{ "head_cloth_helmet",          { 790, 376, 845, 380 }, { 920, 403, 995, 407 } },
		{ "head_leather_helmet",        { 790, 376, 845, 380 }, { 920, 430, 995, 434 } },
		{ "head_plate_helmet",          { 790, 376, 845, 380 }, { 920, 457, 995, 461 } },
		{ "offhands_booktype",          { 790, 430, 845, 434 }, { 920, 457, 995, 461 } },
		{ "offhands_shieldtype",        { 790, 430, 845, 434 }, { 920, 511, 995, 515 } },
		{ "offhands_torchtype",         { 790, 430, 845, 434 }, { 920, 484, 995, 488 } },
		{ "shoes_cloth_shoes",          { 790, 403, 845, 407 }, { 920, 430, 995, 434 } },
		{ "shoes_leather_shoes",        { 790, 403, 845, 407 }, { 920, 457, 995, 461 } },
		{ "shoes_plate_shoes",          { 790, 403, 845, 407 }, { 920, 484, 995, 488 } },
		{ "weapons_arcanestaff",        { 790, 322, 845, 326 }, { 920, 619, 995, 623 } },
		{ "weapons_axe",                { 790, 322, 845, 326 }, { 920, 403, 995, 407 } },
		{ "weapons_bow",                { 790, 322, 845, 326 }, { 920, 349, 995, 353 } },
		{ "weapons_crossbow",           { 790, 322, 845, 326 }, { 920, 376, 995, 380 } },
		{ "weapons_cursestaff",         { 790, 322, 845, 326 }, { 920, 646, 995, 650 } },
		{ "weapons_dagger",             { 790, 322, 845, 326 }, { 920, 430, 995, 434 } },
		{ "weapons_firestaff",          { 790, 322, 845, 326 }, { 920, 673, 995, 677 } },
		{ "weapons_froststaff",         { 790, 322, 845, 326 }, { 920, 700, 995, 704 } },
		{ "weapons_hammer",             { 790, 322, 845, 326 }, { 920, 457, 995, 461 } },
		{ "weapons_holystaff",          { 790, 322, 845, 326 }, { 920, 727, 995, 731 } },
		{ "weapons_knuckles",           { 790, 322, 845, 326 }, { 920, 484, 995, 488 } },
		{ "weapons_mace",               { 790, 322, 845, 326 }, { 920, 511, 995, 515 } },
		{ "weapons_naturestaff",        { 790, 322, 845, 326 }, { 920, 754, 995, 758 } },
		{ "weapons_quarterstaff",       { 790, 322, 845, 326 }, { 920, 538, 995, 542 } },
		{ "weapons_shapeshifterstaff",  { 790, 322, 845, 326 }, { 920, 781, 995, 785 } },
		{ "weapons_spear",              { 790, 322, 845, 326 }, { 920, 565, 995, 569 } },
		{ "weapons_sword",              { 790, 322, 845, 326 }, { 920, 592, 995, 596 } },
	};

	// NULL when `key` was never measured - see the note in ChooseCategory.
	const Entry* Find(const string& key) const {
		for (const auto& entry : entries) {
			if (key == entry.key) {
				return &entry;
			}
		}
		return NULL;
	}
};


// Screen areas for driving the marketplace UI, in 1920x1080 coordinates.
struct ScannerLayout {
	// The market search field. Name scanning uses this instead of the category,
	// tier and enchantment dropdowns below.
	ClickRect search = { 586, 264, 697, 277 };

	// opens the item-type dropdown, and the list it drops down
	ClickRect typeButton = { 759, 268, 875, 278 };
	ClickRect typeList = { 759, 268, 876, 502 };

	// The second column that appears once a top-level entry is chosen. Only
	// ever hashed to notice that it has finished drawing - never read.
	//
	// Reaches y=890 because Capes is the longest submenu in the game: sixteen
	// rows ending at 864, where the old bound of 783 stopped at what was then
	// the deepest row anyone had measured (Shapeshifter Staff). A short watch
	// region still detects the menu opening - the top of it changes either way
	// - but it would go blind to a redraw that only touched the bottom half.
	ClickRect subList = { 889, 323, 1007, 890 };

	CategoryLayout categories;

	// The tier dropdown, button and rows, from MarketUi.h. No OCR: the rows
	// never move, so they are clicked by measured coordinate.
	TierLayout tiers;

	// The enchantment dropdown, same deal - see MarketUi.h for why a scan that
	// ignores it silently loses everything past the twenty-tab cap.
	EnchantmentLayout enchants;

	// pages the result list; clicked until the feed stops reporting new data
	ClickRect nextPage = { 986, 882, 997, 892 };

	// The area the tier dropdown paints over when it opens, with a little
	// margin around the measured rows (901,401 - 1020,521 in TierLayout).
	//
	// Nothing is ever read out of here - the rows are still clicked by measured
	// coordinate, exactly as MarketUi.h insists. It is watched only to answer
	// "has the menu finished drawing yet", which is the question the fixed
	// menu_open_delay_ms sleep used to guess the answer to.
	ClickRect tierPanel = { 895, 392, 1026, 530 };

	// Same, for the enchantment dropdown: margin around its measured rows
	// (1048,320 - 1162,442 in EnchantmentLayout), watched only to answer "has
	// it painted yet" and never read.
	ClickRect enchantPanel = { 1040, 311, 1170, 451 };

	// Clears the open tabs. Clicked before each category so the scan always
	// starts from a clean list instead of inheriting the previous filters.
	// Centred on 1335,272 with exactly one pixel of play on each axis: this
	// target is small and must not be missed.
	ClickRect resetTabs = { 1334, 271, 1336, 273 };
};


// Walks the marketplace menus for a category and lets the game load every
// page, so AlbionFeed sees the prices.
//
// Nothing here reads a price off the screen, and nothing here runs OCR any
// more: both the category menu and the tier dropdown draw on a fixed grid, so
// every row is clicked by measured coordinate (see CategoryLayout and
// TierLayout). The prices themselves arrive through MarketFeed, exact.
class MarketScanner {
	friend struct ToolRegressionAccess;
public:
	MarketScanner(MarketFeed& feed);
	// Services the app on the same thread during waits; no concurrent clicks.
	void SetService(function<void()> callback) { service = std::move(callback); }

	// Safety cap so a broken stop-condition cannot page forever.
	int maxPagesPerTier = 40;

	// How long to wait for the feed after clicking, before deciding the list
	// has no more pages. Seeded from Config.PageTimeoutMs in the constructor.
	int pageTimeoutMs;

	// How many silent pager clicks in a row mean the listing really has ended.
	//
	// One is not enough, and that is what made scans stop after a handful of
	// tabs for no visible reason. "No new data within page_timeout_ms" is the
	// intended end-of-listing signal, but a slow round trip to a distant server
	// produces exactly the same silence - so a single quiet click was being read
	// as "finished" when the game was merely late. Seeded from
	// Config.PageQuietClicks.
	int quietClicksToStop;

	// How many orders a full market tab holds. The game shows at most this
	// many, so a tab that comes back with FEWER is the last one - there is
	// nothing behind it to click for.
	//
	// Worth having because the quiet-click test is expensive by design: it
	// deliberately clicks again and waits the whole budget out before
	// believing silence, and a high-tier listing with one item in it pays that
	// cost for nothing. A short page is a positive signal that the listing has
	// ended rather than an absence of one, so it does not need confirming.
	//
	// Seeded from Config.PageSize; 0 turns the shortcut off and leaves only
	// the quiet-click test.
	int pageSize;

	// Fired right before each step is attempted - one step per (tier,
	// enchantment) pair, or once with tier == 0 for a one-pass category - so a
	// caller driving a progress bar knows what is "current" the moment work
	// starts on it rather than only when it ends.
	using StepStartFn = function<void(int tierOrZero)>;
	// Fired once that step is done. ok is false when the menu never opened -
	// the caller's signal that the game looked frozen/laggy for that step,
	// worth a manual re-scan later rather than trusting the (missing) data
	// silently.
	using StepDoneFn = function<void(int tierOrZero, bool ok)>;

	// tiers empty -> one pass, the tier dropdown is never touched at all.
	// tiers non-empty -> walked in the given order, each opening the tier
	// dropdown fresh.
	//
	// enchants empty -> the enchantment dropdown is never touched either, so
	// the listing arrives with every level mixed together exactly as it did
	// before this filter existed. Non-empty -> each tier is walked once per
	// listed level, which is the only way to get at what the twenty-tab cap
	// hides. Ignored entirely when tiers is empty: the black market does not
	// filter by enchantment.
	//
	// One (tier, enchant) pair is one step: the callbacks fire once per pair,
	// in the same nesting order the loops below run. Either may be empty.
	bool ScanCategory(const MarketCategory& category, const vector<int>& tiers,
		const vector<int>& enchants,
		const StepStartFn& onStart, const StepDoneFn& onDone);

	// Clears the market search field, types one exact display name (normally
	// "Boltcasters T4.2"), and walks every result page. The caller owns the
	// returned batch so it can discard autocomplete/late-clear packets that do
	// not belong to the requested item before filing the result.
	bool ScanName(const string& searchText, vector<FeedOrder>& batch);

private:
	bool ChooseCategory(const MarketCategory& category);
	bool ChooseTier(int tier);
	bool ChooseEnchantment(int level);

	// Cheap content fingerprint of a screen region: one BitBlt into the DIB
	// that is already allocated, then a hash of the pixels. Well under a
	// millisecond for the regions used here, which is what makes polling for a
	// redraw affordable. Returns 0 when the grab failed.
	unsigned long long RegionHash(const ClickRect& area);

	// Waits for a menu to actually finish drawing, instead of sleeping a fixed
	// menu_open_delay_ms and hoping.
	//
	// `before` is the region's hash from *before* the click that opens the
	// menu. Two phases, and both are needed:
	//   1. wait until the region stops looking like `before` - the menu has
	//      begun to paint. Without this the very first grab (taken before the
	//      game has reacted at all) matches the one after it and the region
	//      reads as "settled" while nothing has opened yet.
	//   2. wait until two consecutive grabs match - it has finished painting.
	//
	// Returns as soon as both hold, so a menu that draws in 40ms costs 40ms
	// rather than the full ceiling. timeoutMs is only a ceiling; on a laggy
	// frame this degrades to exactly the old fixed-sleep behaviour.
	void SettleRegion(const ClickRect& area, unsigned long long before,
		int timeoutMs);

	// Convenience: hash, click the rectangle, then settle. The order matters -
	// the baseline has to be taken before the click.
	void ClickAndSettle(const ClickRect& target, const ClickRect& watch,
		int timeoutMs);

	// Clicks the pager at most maxClicks times, stopping early the moment the
	// feed reports nothing new. The city market pages through a long tiered
	// listing, so it gets maxPagesPerTier; the black market only ever needs
	// the one click, so it gets 1 - see ScanCategory.
	int PageThroughList(int maxClicks);

	MarketFeed& feed;
	function<void()> service;
	void Wait(int milliseconds);
	ScannerLayout layout;
	ScreenCapturer capturer;

	// Measured click -> first-packet latency, smoothed, in milliseconds.
	//
	// page_timeout_ms has to cover the worst case or the scan stops early and
	// silently drops most of the listing, so it is set for a bad day on a
	// distant server. That makes it far too generous on a good day: the *end*
	// of every listing is detected by waiting the full timeout with nothing
	// arriving, so an over-large timeout is paid in full, per tier, per
	// category, on every scan.
	//
	// So it is measured instead of assumed. Each successful page records how
	// long the data actually took, and the wait is scaled off that with plenty
	// of headroom - see PageBudgetMs. On a 60ms server the timeout settles
	// around 400ms; on a 270ms one it opens back up on its own. -1 = nothing
	// measured yet, use the configured value.
	int observedPageMs = -1;

	// The wait to allow for the next page, derived from observedPageMs and
	// never above pageTimeoutMs. `firstTry` is false for the repeat clicks
	// that confirm the end of a listing: those only have to catch a straggler
	// from a request already sent, not a whole fresh round trip, so they get a
	// tighter budget.
	int PageBudgetMs(bool firstTry) const;
};
