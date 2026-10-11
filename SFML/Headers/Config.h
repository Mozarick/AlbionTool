#pragma once
#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <utility>

using namespace std;


// Big enough for the settings screen (category list + tier picker), the scan
// progress bar, and the log panel that replaced the console window - a
// printed buy plan is one row per lot and needs the vertical room. Window
// size no longer has to stay modest to keep clear of the game: the window
// minimises itself for the duration of a scan, so it cannot land on top of
// the region the scanner reads pixels from.
const int WIDTH = 900;
const int HEIGHT = 820;

// Settings read from Config.txt. Every field keeps its default when the file
// is missing or a line cannot be parsed, so a broken config never stops the
// program from starting.
struct MyConfig {

	MyConfig();

	void Load(const string& path);

	// Screen the game runs at. All the click coordinates in the code are
	// written for 1920x1080 and scaled by ScreenScale at runtime.
	pair<int, int> ScreenResolution = { 1920, 1080 };
	pair <float, float> ScreenScale = { 1.f, 1.f };

	// Folder holding eng.traineddata. Empty means "use TESSDATA_PREFIX".
	string TessdataPath = "";

	// How long to hold a mouse button down, and how long to let the game
	// react afterwards. Raise these if the bot starts missing clicks.
	int ClickHoldMs = 25;
	int ClickDelayMs = 50;

	// Pause between each character while typing into a text field.
	//
	// Not cosmetic: the search box's autocomplete only reacts to individual
	// keystrokes. Sending the whole string in one SendInput call is what a
	// paste looks like at the OS level, and the game's input handling treats
	// it as one - the box ends up with the right text in it but the item tab
	// never opens, because nothing fired the per-keystroke event that opens
	// it. Small, not zero: item names are short (~20 characters including the
	// tier suffix), so even a few ms per character is a fraction of one
	// click_delay_ms.
	int TypeCharDelayMs = 10;

	// How often to re-grab a screen region while waiting for the UI to
	// redraw, and how long to keep waiting before giving up on it.
	int PollIntervalMs = 15;
	int WaitTimeoutMs = 1200;

	// How long to wait for fresh market data after clicking the tab/pager
	// button before deciding the listing has ended. Too low and a laggy game
	// looks identical to "no more tabs", so the scan quits early and silently
	// leaves most of the listing unread.
	int PageTimeoutMs = 1200;

	// Extra pause after each tab/pager click, on top of the usual click
	// delay. Tuned for a low-latency server (~60ms); raise this back toward
	// 100 on a distant server (~270ms) if tabs start getting skipped.
	int PageClickDelayMs = 40;

	// How many pager clicks in a row must come back silent before the listing
	// is believed to have ended. One is not enough: a slow round trip looks
	// exactly like the end of the list from inside the bot, which made scans
	// stop after a handful of tabs with nothing in the log to explain it.
	int PageQuietClicks = 2;

	// How many orders a full market tab holds. The game shows at most 50, so a
	// tab that comes back with fewer is the last one and there is nothing
	// behind it worth clicking for.
	//
	// This is a shortcut past the quiet-click test, which is expensive by
	// design - it clicks again and waits the whole budget out before believing
	// silence. A high-tier listing holding one item paid that cost for
	// nothing. A short page is positive evidence the listing ended, so unlike
	// silence it does not need confirming.
	//
	// 0 turns the shortcut off and leaves only the quiet-click test. Change it
	// if the game ever changes how many rows a tab holds; a value that is too
	// LOW is the dangerous direction, because then a full page reads as short
	// and the rest of the listing is dropped.
	int PageSize = 50;

	// How long a dropdown needs to finish drawing before it is worth reading.
	// OCR run too early sees the half-drawn menu (or the page still behind
	// it) and matches the wrong row, which looks like the bot skipping from
	// tier 4 straight to tier 7.
	int MenuOpenDelayMs = 350;

	// The cursor travels to a click target along a curved path instead of
	// teleporting there. MoveMsPer100px scales the travel time with
	// distance; MoveJitterPx is how far the path is allowed to wander
	// sideways off the straight line, both the bow of the curve and a
	// per-step wobble. Set MoveJitterPx to 0 to go back to a straight line.
	int MoveMsPer100px = 45;
	int MoveMinMs = 150;
	int MoveMaxMs = 700;
	int MoveJitterPx = 3;

	// A real click never lands on the exact same pixel twice. Each move
	// target is nudged by up to this many pixels before the cursor travels
	// to it. Keep this small: the tightest click targets in the game's UI
	// (dropdown list rows) are only ~26px apart, so anything much above 3-4
	// risks landing on the wrong row.
	int MoveTargetSlopPx = 3;

	// How many units the bot may take with a single Buy click.
	//
	// One, by default, and that is not timidity. The market lists one row per
	// seller, so wanting five of something usually means five separate rows of
	// one - and the quantity box in the buy dialog only exists when the row
	// itself holds more than one unit. Typing a number into a dialog that has
	// no quantity box derails the click sequence that follows, which is what
	// "purchase did not register" used to mean. Clicking the top row once per
	// unit needs no quantity box at all, and the next row slides up into the
	// same place afterwards.
	//
	// Raise it only if the goods being flipped are genuinely listed in stacks;
	// the bot still never asks for more than the top row actually holds.
	int BuyUnitsPerClick = 1;

	// How long to wait for AlbionFeed to report that the game acted on a buy
	// click. This is a round trip to the game server, not a screen redraw, so
	// it is generous compared with the UI timeouts above.
	int PurchaseConfirmMs = 1500;

	// How many times to click Buy again on a row the game never acknowledged
	// before abandoning the lot. A click that vanishes is usually the window
	// losing focus for a moment, which the next click fixes.
	int BuyRetries = 2;

	// How many times to wait again for a searched listing before deciding the
	// goods are gone.
	//
	// One was not enough. A search result arrives as several packets and the
	// market panel sends unrelated ones in between, so a single wait-and-read
	// can easily land on a batch that holds none of the item being bought -
	// and that reads as "gone from the market", which abandons a lot whose
	// goods are still sitting on screen. In the run that prompted this, 19 of
	// 33 lots ended that way.
	int SearchRetries = 3;

	// How many purchases at a price the bot never got to check it will absorb
	// in one lot before giving up on that lot.
	//
	// When the row being aimed at is taken by another player, the one below
	// slides up and the click buys that instead. That is normal in a busy
	// market and is not on its own a reason to abandon the rest of the lot -
	// but a purchase whose price is not in the listing the bot holds is one
	// whose margin was never checked, so those are counted and capped.
	int StrayBuyLimit = 3;

	// Flipping limits. Budget 0 means "spend whatever is in the plan"; the
	// BuyCity button asks for a figure anyway, which overrides this.
	long long Budget = 0;
	// minimum margin, as a fraction: 0.20 is 20%
	double MinProfitRate = 0.20;

	// Floor on the total net profit ONE LOT must earn, in silver, and whether
	// it and the percentage above are an AND or an OR.
	//
	// BuyCity asks for both at the time and those win. These exist so Plan and
	// Deals - which spend nothing and never prompt - can preview the same list
	// BuyCity would produce, instead of a wider one the buying run then
	// silently narrows.
	long long MinProfitSilver = 0;
	bool RequireBothProfits = true;
	// refuse to touch anything dearer than this per unit, 0 for no ceiling
	long long MaxUnitPrice = 0;

	// The emergency stop combination, read straight from the OS while a run is
	// blocking the event thread - see AbortSwitch.h for why it cannot be an
	// SFML key event.
	//
	// Held as modifier flags plus a virtual-key code rather than a string, so
	// the polling in the run loops stays a couple of GetAsyncKeyState calls
	// with no parsing. AbortVk is a Windows VK code; for letters and digits
	// that is just the uppercase character, which is why no Windows header is
	// needed here.
	//
	// Ctrl+Shift+Q by default. A combination rather than a single key because
	// the game receives the keystroke as well: it has to be something no
	// Albion binding and no Steam overlay will answer, and something that
	// cannot be hit by accident mid-run. Set with abort_hotkey in Config.txt.
	bool AbortCtrl = true;
	bool AbortShift = true;
	bool AbortAlt = false;
	int AbortVk = 'Q';
	string AbortHotkeyName = "Ctrl+Shift+Q";
};


// defined in Config.cpp
extern MyConfig Config;
