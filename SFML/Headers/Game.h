#pragma once

#include <SFML/Graphics.hpp>
#include <iostream>

#include <sstream>
#include <fstream>
#include <ctime>
#include <algorithm>
#include <numeric>
#include <stack>
#include <thread>

// Kept first as a guard. profile.h does "using namespace std::chrono;", and
// sf::milliseconds() vs std::chrono::milliseconds becomes an ambiguous name
// once both are visible unqualified in the same translation unit. The header
// that used to drag "using namespace sf;" in ahead of it (MyImg.h, via
// Menus.h) is gone now, but the ordering costs nothing and the ambiguity is
// not obvious enough to want to rediscover.
#include "profile.h"

#include "BlackMarket.h"
#include "MarketScanner.h"
#include "MarketStore.h"
#include "Holdings.h"
#include "CityBuyer.h"
#include "Config.h"
#include "Menus.h"
#include "ScanProfile.h"
#include "ScanProgress.h"
#include "ShopList.h"
#include "LogSink.h"
#include "ProcessLauncher.h"
#include "Theme.h"
#include "StudioSession.h"


using namespace std;
using namespace sf;






class Game
{
	friend struct ToolRegressionAccess;
private:
	// Variables

	// Window
	stack<RenderWindow*> wnd_stack;
	RenderWindow* window;
	Event* sfEvent;
	string current_wnd = "Menu";

	// Live market data from AlbionFeed. Values, not pointers: they
	// own a thread and a socket, and letting the destructor run in order is
	// the whole point.
	MarketFeed feed;
	ItemTable itemTable;

	// Scanned orders, kept split into black market bids and city offers by
	// what the data says rather than by which button was pressed.
	MarketStore store;
	unsigned long long feedPollTick = 0;
	unsigned long long scanServiceTick = 0;
	void ServiceScan(bool absorb);
	bool WaitWithUi(int milliseconds);

	// What the bot bought and still has to sell; the black market run walks
	// this instead of the market's own order book.
	Holdings holdings;


	// Cursor position, for hit tests. The green on-screen read-out that used
	// to follow it is gone - it drew over whatever was under the pointer.
	int mPosX = 0;
	int mPosY = 0;

	// Clock 
	Clock dtClock;
	float dt = 0.f;
	string fpsLabel;

	
	// Font
	Font font;

	// Where the log panel starts on the screen being drawn. Each screen sets
	// it, because they no longer all end at the same height - the menu's four
	// groups and the scan grid's twelve rows finish in different places, and a
	// fixed y=540 left one screen with a gap and cramped another.
	float logTop = 540.f;

	// Scan menu: the marketplace's own category tree, from categories.tsv.
	CategoryTable catTable;
	set<string> selectedScan;

	// Which tiers to scan per (location, category), edited on the Settings
	// screen and persisted to scan_profile.tsv.
	ScanProfile scanProfile;
	static const char* const SCAN_PROFILE_FILE;

	// Progress of the running (or last) scan. A scan runs synchronously on
	// the event thread - see LaunchScan for why it is deliberately not on a
	// worker thread - so this is written and read from the same thread; the
	// locking inside ScanProgress is left in place but is not load-bearing.
	ScanProgress scanProgress;
	bool scanIsBlackMarket = false; // which profile the running/last scan used
	// City tiers actually collected by Scan by Name. Those searches do not use
	// the category scan profile, so BuildPlan must not discard their results
	// merely because an old profile did not have that tier selected.
	set<int> nameScannedCityTiers;

	// GetTickCount64 of the last capture-client (re)start, so the pre-scan
	// self-heal does not fire again on a client that was only just launched
	// and has simply not had anything to report yet.
	unsigned long long lastCaptureRestartTick = 0;

	// Settings screen: pick which tiers to scan for each category, split by
	// location because the black market and royal cities warrant different
	// tiers entirely.
	bool settingsBlackMarket = true;
	string settingsSelectedCategory; // category key currently shown in the tier picker

	// EnterWnd
	string _profit = "";

	// BuyCity is a four-step entry: profit %, then a floor on the silver a lot
	// must earn, then how those two combine, then the budget. Held here
	// between the steps.
	//
	// Two thresholds instead of one because neither answers the other's
	// complaint. A percentage alone fills the list with 300% flips worth forty
	// silver; a silver floor alone will happily tie up the whole budget for
	// 3%. AND is "worth my time and worth the silver", OR is "either will do".
	double pendingBuyProfitRate = 0.20;
	long long pendingBuyMinProfit = 0;     // 0 = percentage decides alone
	bool pendingBuyRequireBoth = true;     // true = AND, false = OR

	// Plan and BuyCity walk the SAME four screens; this is the only thing that
	// differs between them, and it is read once at the end.
	//
	// Plan used to ask for a percentage and nothing else, then plan against
	// whatever Config.txt happened to say about the other three. With budget:0
	// in the shipped config that made it an exact duplicate of Deals, and
	// worse, there was no way to see what a particular gate produced without
	// either editing Config.txt or spending real silver to find out. Sharing
	// the entry chain makes Plan the dry run of the thing it is previewing,
	// which is the only version of it worth keeping.
	bool pendingBuyDryRun = false;

	// The hand-shopping screen: the last plan, or the part of it a buying run
	// came home without, grouped into one entry per market search.
	//
	// Kept here because a plan is otherwise thrown away the moment it has been
	// printed, and this screen exists precisely for the minutes AFTER a run -
	// when the game lagged, the bot missed a few, and the rest has to be
	// bought by hand before the offers move.
	Studio::Session shopSession;
	vector<ShopBlock>& shopBlocks = shopSession.blocks;
	string& shopTitle = shopSession.title;
	ShopEstimate& shopEstimate = shopSession.estimate;
	vector<size_t> shopVisible;
	string shopQuery;
	bool shopSearchFocused = false;
	bool shopPendingOnly = false;
	string studioNotice;
	unsigned long long studioNoticeTick = 0;
	bool studioNoticeError = false;
	void Notice(const string& message, bool error = false);
	void RebuildShopVisible();
	void CopyShop(size_t index);
	void UndoShop();
	void ResetShop();
	void SaveShopProgress();
	bool ShopKey();
	void ShopText(unsigned unicode);
	string NextAction() const;
	long long shopTotalProfit = 0;
	long long shopTotalUnits = 0;
	float shopScroll = 0.f;   // pixels scrolled down from the top of the list

	// Blocks already clicked, so a list worked through top to bottom shows how
	// far it got. Indices into shopBlocks; cleared whenever the list is
	// rebuilt.
	set<size_t> shopDone;

	// Which block last had its search text copied, and when, for the brief
	// "COPIED" flash that confirms the clipboard actually took it.
	size_t shopCopied = size_t(-1);
	unsigned long long shopCopiedTick = 0;

	// Version
	Text* version;



	// Init functions
	void initWindow();
	void InitEvent();
	void InitFont();
	void InitDataBase();
	void InitVersion();
	void InitScanMenu();


	// Delete

	void DeleteDataBase();
	void DeleteEvent();
	void DeleteVersion();

	// Scanning
	struct ScanGroup {
		const MarketCategory* category;
		vector<int> tiers;    // empty = one pass
		vector<int> enchants; // empty = no enchantment filter; ignored on the BM
	};
	void LaunchScan(vector<ScanGroup> groups, bool blackMarket);


public:
	// Constr&Destr
	Game(bool offline = false);
	virtual ~Game();

	// Functions


	void MenuConroller(const string& command);
	void ScanController(const string& command);
	void ScanningController(const string& command);
	void SettingsController(const string& command);
	void RunScan(bool blackMarket);
	void RunScanRetry();
	// Experimental city scan: use the Black Market bids already held as the
	// shopping universe, then type each exact item name into the market search
	// box. No category, tier or enchantment dropdown is touched.
	void RunNameScan();

	// Tiers the buy side is allowed to touch, taken from the CITY scan
	// profile - what the Settings screen was set to scan for.
	//
	// Empty means no restriction, and that is also what a single category set
	// to ONE PASS produces: a one-pass scan never opens the tier dropdown, so
	// it pulls in every tier and there is nothing to hold the plan to.
	set<int> AllowedCityTiers() const;

	// Pairs the two halves and prints the shopping list. Spends nothing.
	//
	// minProfitSilver is a floor on the TOTAL profit one lot must earn, and
	// requireBoth says whether it and the percentage are an AND or an OR.
	// Zero silver takes that threshold out entirely and the percentage decides
	// alone - which is what Deals passes unless Config.txt sets one.
	vector<FlipLot> BuildPlan(long long budget, double minProfitRate,
		long long minProfitSilver, bool requireBoth);
	// The dry run: identical arithmetic to RunBuy, identical entry screens,
	// and it buys nothing.
	void RunPlan(long long budget, double minProfitRate,
		long long minProfitSilver, bool requireBoth);

	// Prints every deal found, ranked, with no budget applied - the order
	// BuyCity will work through, top to bottom. Spends nothing.
	void RunDeals();
	void RunBuy(long long budget, double minProfitRate,
		long long minProfitSilver, bool requireBoth);

	// The AND / OR screen between the two figures and the budget.
	void BuyCombineController(const string& command);
	void DrawBuyCombineScreen();
	string HitBuyCombine(float px, float py) const;

	// Groups lots into blocks and shows the hand-shopping screen.
	//
	// `title` says which list this is - the whole plan, or what a buying run
	// failed to get - because the two look identical once they are blocks and
	// acting on the wrong one means buying things twice.
	void ShowShopList(const vector<FlipLot>& lots, const string& title);
	void ShopController();
	void DrawShopScreen(RenderTarget* target = nullptr);

	// The menu as grouped inset lists. Both of these walk the same layout
	// helper, so the row a click lands in is by construction the row that was
	// drawn - the same rule ShopBlockRect follows.
	void DrawMenuScreen(RenderTarget* target = nullptr);
	string HitMenu(float px, float py) const;

	// Where block `index` sits at the current scroll position.
	//
	// One function, used by both the drawing and the click test, so the box a
	// click lands in is by construction the box that was drawn. Two copies of
	// this arithmetic would be two chances to copy the wrong item's name.
	FloatRect ShopBlockRect(size_t index) const;

	// How far the list can scroll before its last block sits at the bottom.
	float ShopMaxScroll() const;
	
	void EnterToWindow();
	

	// Run
	void run();
	// Diagnostic rendering uses the actual event loop and capture receiver.
	void CapturePreview(const string& imagePath, int seconds);

	// Update
	void update();
	void updateSFMLEvents();
	void updateDt();
	void updateMousePos();

	// render
	void render();
	void drawEnterWnd();
	void drawVersion();
	void DrawScanScreen(RenderTarget* target = nullptr);
	void DrawScanningScreen();

	// Each screen hit-tests its own geometry through the same helpers it draws
	// with. Categories route by KEY, not by the label under the cursor, which
	// is what took the click path off `menu_sub` uniqueness (I8).
	string HitScan(float px, float py) const;
	string HitScanning(float px, float py) const;
	string HitSettings(float px, float py) const;
	string HitCategory(float px, float py, float top) const;
	void DrawSettingsScreen();
	void DrawLog();

	
	


	

	
	

};
