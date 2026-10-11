#include "../Headers/Game.h"
#include "../Headers/AbortSwitch.h"

// The shop screen's whole point is putting an item name where the game's
// search box can be given it, so the clipboard is load-bearing here rather
// than a convenience.
#include <SFML/Window/Clipboard.hpp>

// remove(), for the Clear button deleting stale scan leftovers. Included
// explicitly: <algorithm> is already in scope through Game.h and its
// std::remove is a range algorithm, so relying on one of them being visible
// by accident is how this ends up calling the wrong thing.
#include <cstdio>

// Entered figures are parsed with stoll into a long long, so this only has to
// stay well inside that. Twelve digits is 999 billion - past anything a budget
// or a profit floor will ever be, and the old nine-digit cap (a hair under one
// billion, chosen when the value was parsed into an int) was a ceiling a rich
// character could actually hit.
static const size_t MAX_ENTERED_DIGITS = 12;

// BACK sits in the same top-right corner on every screen, so it is always in
// the same place no matter what is being shown.
static Vector2f BackButtonSize() {
	return Vector2f(120.f, 26.f);
}

static Vector2f BackButtonPos() {
	return Vector2f(float(WIDTH) - BackButtonSize().x - 12.f, 8.f);
}

// Minimises the bot's own window for as long as this object is alive, then
// restores it.
//
// Every automated run drives the mouse at fixed *screen* coordinates in the
// game (buyFirstRow 1232,424; confirmBuy 844,722; the scanner's menus around
// 759-1020 x 268-521). Those coordinates do not depend on this window's size
// at all - but if the window happens to sit over one of them, the click lands
// on the bot instead of the game, and the scanner additionally ends up
// OCR'ing its own pixels and "finding" menus that were never open.
//
// Scoped rather than a pair of calls so an early return still puts the window
// back, and it sleeps briefly so the desktop has actually repainted before
// the first click goes out.
struct WindowHidden {
	HWND wnd;
	explicit WindowHidden(HWND handle) : wnd(handle) {
		ShowWindow(wnd, SW_MINIMIZE);
		Sleep(400);
	}
	~WindowHidden() {
		ShowWindow(wnd, SW_RESTORE);
	}
	WindowHidden(const WindowHidden&) = delete;
	WindowHidden& operator= (const WindowHidden&) = delete;
};

// Ledger of goods bought and not yet sold on.
static const char* const HOLDINGS_FILE = "holdings.tsv";


const char* const Game::SCAN_PROFILE_FILE = "scan_profile.tsv";

Game::Game(bool offline)
{
	LOG_DURATION("Load took: ");
	initWindow();
	InitEvent();
	InitFont();

	// Allocates `version`, which drawVersion dereferences every frame. It used
	// to be reached indirectly, as the last line of InitTextVar - so removing
	// the FPS text object took this with it and the first render crashed on a
	// garbage pointer. Called directly now, where it cannot be lost again.
	InitVersion();

	InitScanMenu();
	InitDataBase();
	current_wnd = "Menu";

	// Everything the old START.bat did by hand: bring the game and the
	// packet capture client up without a second window for either. Capture
	// still needs one UAC prompt (Npcap requires Administrator); the game
	// launcher needs none.
	if (!offline) {
		EnsureAlbionLauncherRunning();
		EnsureFeedClientRunning();
	} else current_wnd = "Menu";
	lastCaptureRestartTick = GetTickCount64();
}

Game::~Game()
{
	LOG_DURATION("Delete took: ");

	while (!wnd_stack.empty()) {
		delete wnd_stack.top();
		wnd_stack.pop();
	}

	DeleteDataBase();
	DeleteEvent();
	DeleteVersion();


}


// Init functions

void Game::initWindow()
{
	cout << "initWindow\n";
	window = new sf::RenderWindow(sf::VideoMode(WIDTH, HEIGHT), "Market Studio", sf::Style::Close);

	const HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
	if (icon) {
		SendMessageW(window->getSystemHandle(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
		SendMessageW(window->getSystemHandle(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
	}
	window->setFramerateLimit(60);
	window->setVerticalSyncEnabled(false);
	wnd_stack.push(window);
}

void Game::InitEvent() {
	cout << "InitEvent\n";
	sfEvent = new Event;
}

void Game::InitFont() {
	cout << "InitFont\n";

	// CascadiaCode is still loaded, and still monospace, because the plan
	// table is built with snprintf column widths and only lines up in a
	// monospace face. It is the log panel's font now rather than the whole
	// UI's; the chrome uses Inter, which is what the iOS type ramp needs and
	// what a proportional face gives that a coding font cannot.
	if (!font.loadFromFile("fonts/CascadiaCode.ttf")) {
		cout << "Font error!\n";
	}
	Ui::LoadFonts("fonts/");
}

void Game::InitDataBase() {
	cout << "InitDataBase\n";

	// Live prices come from AlbionFeed over HTTP - the bot never reads them off
	// the screen.
	itemTable.Load("items.tsv");

	string sessionError;
	if (!Studio::Load("shopping_session.json", "shopping_progress.json", shopSession, sessionError))
		Notice(sessionError, true);
	else if (!sessionError.empty()) Notice(sessionError, true);
	for (const auto& record : shopEstimate.History()) shopDone.insert(record.first);
	for (const auto& block : shopBlocks) {
		shopTotalProfit += block.profit; shopTotalUnits += block.quantity;
	}
	RebuildShopVisible();

	// Anything bought but not yet sold, so a restart between the buying trip
	// and the selling trip does not strand the goods.
	holdings.Load(HOLDINGS_FILE);

	if (!feed.Start(5000)) {
		cout << "MarketFeed did not start - port 5000 is already taken.\n"
			<< "  Something else is listening on it; close that, then restart.\n";
	}
}

void Game::InitVersion() {
	cout << "InitVersion\n";
	version = new Text;
	version->setFont(font);
	version->setString("Market Studio 0.6.0");
	version->setCharacterSize(12);
	// bottom-left, under the log panel rather than on top of it
	version->setPosition({ 5, float(HEIGHT) - 14 });
}

void Game::DeleteDataBase() {
	cout << "DeleteDataBase\n";
}

void Game::DeleteEvent() {
	cout << "DeleteEvent\n";
	delete sfEvent;
}

void Game::DeleteVersion() {
	cout << "DeleteVersion\n";
	delete version;
}

void Game::InitScanMenu() {
	cout << "InitScanMenu\n";

	// Despite the name this is the data load, not a menu build. The scan,
	// settings and scanning screens all draw straight out of catTable and
	// scanProfile now, so there are no boxes left to construct - but every one
	// of those screens is empty without what happens here.
	if (!catTable.Load("categories.tsv")) {
		cout << "  scan menu will be empty\n";
	}

	scanProfile.Load(SCAN_PROFILE_FILE);
	scanProfile.EnsureDefaults(catTable);
	scanProfile.Save(SCAN_PROFILE_FILE);

	if (!catTable.All().empty()) {
		settingsSelectedCategory = catTable.All().front().key;
	}
}


void Game::MenuConroller(const string& command) {
	// A click that hit no button must not wipe the log. Clearing
	// unconditionally meant a printed buy plan vanished the moment the user
	// clicked anywhere on the menu screen to look at it.
	if (command.empty()) {
		return;
	}

	GlobalLog.Clear();
	if (command == "Scan") {
		current_wnd = "Scan";
	}
	else if (command == "ScanName") {
		RunNameScan();
	}
	else if (command == "Settings") {
		current_wnd = "Settings";
	}
	else if (command == "SellBM") {
		// asks for the minimum profit % over cost the bot should require
		current_wnd = "EnterWndBM";
	}
	else if (command == "Plan") {
		// The dry run of BuyCity: the same four questions, the same
		// arithmetic, and nothing bought at the end. Sharing the chain is the
		// point - a preview that asks fewer questions than the run it previews
		// is previewing a different list.
		pendingBuyDryRun = true;
		current_wnd = "EnterWndBuyProfit";
	}
	else if (command == "Deals") {
		// No prompt: this spends nothing, so there is nothing to confirm, and
		// it reads the whole gate from Config.txt. Plan is the one to use to
		// try a gate without editing that file.
		RunDeals();
	}
	else if (command == "BuyCity") {
		// four-step: the margin, the silver floor, how they combine, and the
		// budget. Then it buys.
		pendingBuyDryRun = false;
		current_wnd = "EnterWndBuyProfit";
	}
	else if (command == "Shop") {
		// Reopens whatever Plan or BuyCity built last, without recomputing it.
		// Rebuilding here would quietly answer a different question: the store
		// has moved on, and after BuyCity the city half has been cleared
		// outright, so a fresh plan would come back empty and look like the
		// shortfall had been bought.
		current_wnd = "Shop";
	}
	else if (command == "Capture") {
		cout << "Restarting the market capture client.\n";
		RestartFeedClient();
		lastCaptureRestartTick = GetTickCount64();
	}
	else if (command == "Clear") {
		// Wipe, not tidy. Everything the bot has accumulated goes: the scanned
		// orders, the last scan's results, the log file, and the ledger.
		//
		// The ledger was deliberately spared at first, on the reasoning that
		// holdings.tsv is goods actually in the bag rather than history, so
		// forgetting it loses track of real silver. That is still true and it is
		// still the consequence - the user asked for it anyway, twice, and it is
		// their silver. What clearing it means in practice: SellBM will have
		// nothing to sell, and anything genuinely still in the bag has to be
		// sold by hand or re-entered into holdings.tsv.
		//
		// All of it happens before anything is printed, so the summary below is
		// the first line in the fresh log.
		const size_t bids = store.BlackMarketCount();
		const size_t offers = store.CityCount();
		const vector<string> cities = store.KnownCities();
		const int heldUnits = holdings.TotalUnits();
		const size_t heldStacks = holdings.All().size();

		store.Clear();
		shopBlocks.clear();
		shopDone.clear();
		shopEstimate.Reset();
		shopTotalProfit = shopTotalUnits = 0;
		shopScroll = 0.f;
		shopCopied = size_t(-1);
		shopVisible.clear(); shopQuery.clear(); shopPendingOnly = false;
		shopSession.generation = Studio::NextGeneration(shopSession.generation);
		shopSession.savedAt = static_cast<long long>(std::time(nullptr));
		string sessionError;
		if (!Studio::SavePlan("shopping_session.json", shopSession, sessionError)) Notice(sessionError, true);
		SaveShopProgress();
		nameScannedCityTiers.clear();
		scanProgress.Clear();

		// Cleared in memory AND rewritten on disk. Clearing only the object would
		// look like it worked until the next restart, when Load() read the old
		// file straight back in.
		holdings.Clear();
		holdings.Save(HOLDINGS_FILE);

		// Anything the feed had buffered but not yet filed belongs to the scan
		// being discarded, so it goes too. Otherwise the very next Absorb()
		// would quietly refill the store with exactly what was just cleared.
		vector<FeedOrder> pending;
		feed.Drain(pending);

		// Leftovers on disk. _save_data.txt is scan output from the OCR price
		// database that was deleted generations ago - nothing reads it any
		// more, but it is still literally old scan history sitting in the
		// folder. ocr_miss.png is the frame behind the last failed menu read.
		// Neither is missed, and both are regenerated if still relevant.
		const bool hadSaveData = ::remove("_save_data.txt") == 0;
		const bool hadOcrMiss = ::remove("ocr_miss.png") == 0;

		GlobalLog.Wipe();

		cout << "Cleared everything.\n";
		cout << "  black market bids (sell side): " << bids << "\n";
		cout << "  city offers       (buy side) : " << offers;
		if (!cities.empty()) {
			cout << "  (from location";
			for (const string& city : cities) {
				cout << ' ' << city;
			}
			cout << ")";
		}
		cout << "\n";
		cout << "  ledger            : " << heldUnits << " units in "
			<< heldStacks << " stacks\n";
		if (!pending.empty()) {
			cout << "  unfiled in the feed: " << pending.size() << "\n";
		}
		if (hadSaveData || hadOcrMiss) {
			cout << "  deleted";
			if (hadSaveData) cout << " _save_data.txt";
			if (hadOcrMiss) cout << " ocr_miss.png";
			cout << "\n";
		}
		cout << "  bot.log truncated - this is now line one\n";

		// Worth saying out loud rather than letting it be discovered later: the
		// goods do not disappear from the game when the ledger does.
		if (heldUnits > 0) {
			cout << "\n!! " << heldUnits << " units are still in the bag in game.\n"
				<< "   The bot no longer knows about them, so SellBM will not\n"
				<< "   touch them - sell them by hand, or buy again to rebuild\n"
				<< "   the ledger.\n";
		}
		cout << "\nNothing left from previous runs. Scan again before Plan.\n";
	}
	else if (command == "Exit") {
		wnd_stack.top()->close();
	}
}

void Game::ScanController(const string& command) {
	if (command.empty()) {
		return;
	}

	if (command == "BACK") {
		current_wnd = "Menu";
		return;
	}
	if (command == "CLEAR") {
		selectedScan.clear();
		return;
	}
	if (command == "SELECT ALL") {
		for (const auto& category : catTable.All()) selectedScan.insert(category.key);
		return;
	}
	if (command == "SCAN BM") {
		RunScan(true);
		return;
	}
	if (command == "SCAN MARKET") {
		RunScan(false);
		return;
	}

	// A category, addressed by KEY. HitCategory prefixes it so a category can
	// never be confused with an action, and so the label above it is free to
	// be reworded without touching this.
	//
	// This used to compare against `menu_sub`, and when the hit test moved to
	// keys and this did not, every category click stopped registering - no
	// error, no log line, just a screen that would not respond. That is the
	// exact failure the prefix and the key exist to prevent, arrived at by
	// changing one end and not the other.
	const string key = Ui::CategoryKey(command);
	if (!key.empty()) {
		if (selectedScan.count(key)) {
			selectedScan.erase(key);
		}
		else {
			selectedScan.insert(key);
		}
	}
}

void Game::ScanningController(const string& command) {
	if (command == "BACK") {
		current_wnd = "Menu";
		return;
	}
	if (command == "RETRY FAILED") {
		RunScanRetry();
		return;
	}
}

void Game::SettingsController(const string& command) {
	if (command == "BACK") {
		current_wnd = "Menu";
		return;
	}
	if (command == "BLACK MARKET") {
		settingsBlackMarket = true;
		return;
	}
	if (command == "CITY MARKET") {
		settingsBlackMarket = false;
		return;
	}
	if (command == "ONE PASS (NO TIER)") {
		scanProfile.SetTiers(settingsBlackMarket, settingsSelectedCategory, {});
		scanProfile.Save(SCAN_PROFILE_FILE);
		return;
	}
	if (command == "APPLY TO ALL") {
		vector<int> current = scanProfile.TiersFor(settingsBlackMarket, settingsSelectedCategory);
		vector<int> currentEnchants =
			scanProfile.EnchantsFor(settingsBlackMarket, settingsSelectedCategory);
		for (const auto& row : catTable.All()) {
			scanProfile.SetTiers(settingsBlackMarket, row.key, current);
			scanProfile.SetEnchants(settingsBlackMarket, row.key, currentEnchants);
		}
		scanProfile.Save(SCAN_PROFILE_FILE);
		return;
	}
	// Enchantment toggles. The black market is never filtered by enchantment,
	// so its buttons are drawn inert and ignored here too rather than writing
	// a setting the scanner would silently drop.
	if (!settingsBlackMarket && command.size() == 2 && command[0] == 'E'
		&& command[1] >= '0' && command[1] <= '4') {

		int level = command[1] - '0';
		vector<int> current =
			scanProfile.EnchantsFor(settingsBlackMarket, settingsSelectedCategory);
		auto it = find(current.begin(), current.end(), level);
		if (it != current.end()) {
			current.erase(it);
		}
		else {
			current.push_back(level);
			sort(current.begin(), current.end());
		}
		scanProfile.SetEnchants(settingsBlackMarket, settingsSelectedCategory, current);
		scanProfile.Save(SCAN_PROFILE_FILE);
		return;
	}
	if (command.size() >= 2 && command[0] == 'T') {
		try {
			int tier = stoi(command.substr(1));
			if (tier >= 1 && tier <= 8) {
				vector<int> current = scanProfile.TiersFor(settingsBlackMarket, settingsSelectedCategory);
				auto it = find(current.begin(), current.end(), tier);
				if (it != current.end()) {
					current.erase(it);
				}
				else {
					current.push_back(tier);
					sort(current.begin(), current.end());
				}
				scanProfile.SetTiers(settingsBlackMarket, settingsSelectedCategory, current);
				scanProfile.Save(SCAN_PROFILE_FILE);
			}
		}
		catch (...) {
			// not a tier button, fall through
		}
		return;
	}

	// Which category the tier and enchantment pickers below are editing,
	// addressed by KEY - see the note in ScanController.
	const string key = Ui::CategoryKey(command);
	if (!key.empty()) {
		settingsSelectedCategory = key;
	}
}

void Game::LaunchScan(vector<ScanGroup> groups, bool blackMarket) {
	if (!feed.IsRunning()) {
		GlobalLog.Clear();
		cout << "The market feed is not running - the bot never opened\n"
			<< "  127.0.0.1:5000. Restart the bot, then press Capture.\n";
		return;
	}
	if (scanProgress.IsRunning()) {
		return;
	}

	// One entry per step the scanner is about to run, in the same order it
	// runs them - ScanProgress only advances a cursor, so this has to mirror
	// the loops in ScanCategory exactly or the progress screen labels drift
	// off the work actually being done.
	vector<ScanStep> plan;
	for (const auto& group : groups) {
		if (group.tiers.empty()) {
			plan.push_back({ group.category->key,
				group.category->menuSub + " (1 pass)", 0, -1, StepState::Pending });
			continue;
		}

		const bool splitByEnchant = !group.enchants.empty();
		for (int tier : group.tiers) {
			if (!splitByEnchant) {
				plan.push_back({ group.category->key,
					group.category->menuSub + " t" + to_string(tier),
					tier, -1, StepState::Pending });
				continue;
			}
			for (int enchant : group.enchants) {
				plan.push_back({ group.category->key,
					group.category->menuSub + " t" + to_string(tier)
						+ '.' + to_string(enchant),
					tier, enchant, StepState::Pending });
			}
		}
	}
	if (plan.empty()) {
		GlobalLog.Clear();
		cout << "Nothing to scan.\n";
		return;
	}

	// Self-heal before committing to a scan that takes minutes.
	//
	// MessagesSeen is cumulative for the life of the process, so zero means
	// this bot has never once heard from the capture client - the client is
	// dead, or is a stale one posting somewhere else. Restarting it here costs
	// a UAC prompt and a few seconds; not restarting it costs a full scan that
	// clicks its way through every category and returns nothing, which is the
	// failure that kept getting mistaken for a broken scanner.
	//
	// Done before the window is minimised, so the UAC prompt is not fighting
	// the scan for focus. Only when the count is zero: once data has arrived
	// this session the client is known good, and restarting mid-session would
	// throw away a working capture.
	// The tick guard matters: startup already restarts the client, and a
	// freshly started client legitimately reports nothing until the game
	// sends a market packet. Without it every first scan would trigger a
	// second, pointless UAC prompt.
	const unsigned long long sinceRestart =
		GetTickCount64() - lastCaptureRestartTick;

	if (feed.MessagesSeen() == 0 && sinceRestart > 60000) {
		GlobalLog.Clear();
		cout << "No market data has arrived yet this session.\n"
			<< "Restarting the capture client first so the scan is not wasted.\n";
		RestartFeedClient();
		lastCaptureRestartTick = GetTickCount64();

		// give the client time to get past UAC and open its capture handle
		if (!WaitWithUi(6000)) return;
	}

	scanIsBlackMarket = blackMarket;
	scanProgress.Begin(std::move(plan));
	current_wnd = "Scanning";

	GlobalLog.Clear();
	cout << "Switch to the game window now.\n";
	cout << "Hold " << Abort::HotkeyName() << " at any point to stop.\n";

	WindowHidden hidden(wnd_stack.top()->getSystemHandle());
	Abort::Guard stoppable;

	// Clicks and window services share the main thread. The scanner pumps
	// messages between waits, preserving the existing input timing.
	Sleep(1500);

	const size_t bidsBefore = store.BlackMarketCount();
	const size_t offersBefore = store.CityCount();
	MarketScanner scanner(feed);
	scanner.SetService([this] { ServiceScan(true); });
	unsigned long long before = feed.OrdersSeen();
	unsigned long long messagesBefore = feed.MessagesSeen();

	for (const auto& group : groups) {
		// Between categories: the previous one has been paged to its end and
		// its orders are already in the feed's queue, so nothing is lost by
		// leaving here. Whatever was scanned before the stop is still absorbed
		// below - a stopped scan is a short scan, not a wasted one.
		if (Abort::Requested()) {
			cout << "\n" << Abort::HotkeyName()
				<< " - stopping. Categories not reached are left pending;\n"
				<< "  everything scanned up to here is kept.\n";
			break;
		}

		auto onStart = [this](int) {
			scanProgress.MarkNextRunning();
			ServiceScan(true);
		};
		auto onDone = [this](int, bool ok) {
			scanProgress.CompleteCurrent(ok);
			ServiceScan(true);
		};
		scanner.ScanCategory(*group.category, group.tiers, group.enchants,
			onStart, onDone);
	}

	// File the final batch; earlier batches were filed during waits.
	store.Absorb(feed);

	const size_t bidsAdded = store.BlackMarketCount() - bidsBefore;
	const size_t offersAdded = store.CityCount() - offersBefore;

	cout << "\nScan finished: " << (feed.OrdersSeen() - before)
		<< " orders received.\n";

	// The two halves are reported separately, always, because they are stored
	// separately and are never interchangeable: black market bids are what the
	// SELL side reads, city offers are what the BUY side reads. Which half an
	// order lands in is decided by the order itself - location 3003 plus "this
	// is a buy order" for one, anything else plus "this is a sell offer" for
	// the other - not by which scan button was pressed.
	cout << "  black market bids (sell side): +" << bidsAdded
		<< ", " << store.BlackMarketCount() << " held\n";
	cout << "  city offers       (buy side) : +" << offersAdded
		<< ", " << store.CityCount() << " held";

	vector<string> cities = store.KnownCities();
	if (!cities.empty()) {
		cout << " from location";
		for (const string& city : cities) {
			cout << ' ' << city;
		}
	}
	cout << "\n";

	// Scanning the wrong place is easy to do and used to be invisible: the run
	// looks perfectly healthy, the orders all get filed, they just go into the
	// half nobody was expecting. SCAN BM standing in a city fills the buy side
	// and leaves the sell side empty, and the first sign of it is SellBM
	// reporting that every stack has no bid.
	if (blackMarket && bidsAdded == 0 && offersAdded > 0) {
		cout << "\n!! SCAN BM was pressed but every order came back as a city\n"
			<< "   offer. That is what standing in a royal city looks like -\n"
			<< "   travel to the Black Market and scan again.\n";
	}
	else if (!blackMarket && offersAdded == 0 && bidsAdded > 0) {
		cout << "\n!! SCAN MARKET was pressed but every order came back as a\n"
			<< "   black market bid. Stand at a royal city market and scan again.\n";
	}

	// Zero messages is a different failure from zero orders, and the two used
	// to look identical from the outside. Orders can legitimately be 0 (the
	// listing really was empty), but the capture client posts a message for
	// every market packet it decodes, so *no messages at all* means nothing
	// ever reached us - the clicking and the OCR are not the problem, the
	// data source is. Worth saying outright: this state cost a long time to
	// track down precisely because "0 orders" read like a scanning bug.
	if (feed.MessagesSeen() == messagesBefore) {
		cout << "\n!! The capture client sent nothing at all during this scan.\n"
			<< "   The bot's own receiver is fine (it is listening on 127.0.0.1:5000),\n"
			<< "   so AlbionFeed is either dead or watching the wrong adapter.\n"
			<< "   Its own log says which:\n"
			<< "     D:\\AlbionTool\\bin\\AlbionFeed\\AlbionFeed.log\n"
			<< "   Fix: press Capture on the menu. It kills every capture client and\n"
			<< "   starts a fresh AlbionFeed (one UAC prompt), then scan again.\n";
	}

	scanProgress.Finish();
}

void Game::RunScan(bool blackMarket) {
	if (selectedScan.empty()) {
		GlobalLog.Clear();
		cout << "Select at least one category first.\n";
		return;
	}

	vector<ScanGroup> groups;
	for (const auto& row : catTable.All()) {
		if (!selectedScan.count(row.key)) {
			continue;
		}
		// The black market is never filtered by enchantment. Handing the group
		// an empty list rather than whatever is stored keeps the plan built
		// above and the scanner's own loops in step, which is the one thing
		// the progress cursor depends on.
		groups.push_back({ &row,
			scanProfile.TiersFor(blackMarket, row.key),
			blackMarket ? vector<int>() : scanProfile.EnchantsFor(blackMarket, row.key) });
	}

	LaunchScan(std::move(groups), blackMarket);
}

void Game::RunNameScan() {
	// File anything still waiting first. In particular, a just-finished Black
	// Market scan is the source of the names this pass is going to type.
	store.Absorb(feed);
	const vector<FeedOrder> bids = store.BlackMarketOrders();

	if (bids.empty()) {
		GlobalLog.Clear();
		cout << "Scan by Name needs Black Market bids first.\n"
			<< "  Run Scan > SCAN BM, travel to a city market, then press\n"
			<< "  Scan by Name here.\n";
		return;
	}

	struct NameTarget {
		string searchText;
		string groupTypeId;
		int enchant;
	};

	// One search per exact item/tier/enchantment, however many qualities and
	// individual buy orders the Black Market sent for it.
	map<pair<string, int>, NameTarget> unique;
	size_t unnamed = 0;
	for (const FeedOrder& bid : bids) {
		string group = bid.groupTypeId;
		if (group.empty()) {
			group = bid.itemTypeId;
			const size_t at = group.find('@');
			if (at != string::npos) {
				group.erase(at);
			}
		}

		const string text = itemTable.SearchString(bid.itemTypeId,
			bid.tier, bid.enchant);
		if (group.empty() || text.empty()) {
			++unnamed;
			continue;
		}

		unique.emplace(make_pair(group, bid.enchant),
			NameTarget{ text, group, bid.enchant });
	}

	if (unique.empty()) {
		GlobalLog.Clear();
		cout << "Scan by Name found no searchable item names in the held "
			<< "Black Market bids.\n";
		return;
	}

	// Same capture self-heal used by category scans. Do it before minimising so
	// a possible UAC prompt does not steal focus halfway through the first name.
	const unsigned long long sinceRestart =
		GetTickCount64() - lastCaptureRestartTick;
	if (feed.MessagesSeen() == 0 && sinceRestart > 60000) {
		GlobalLog.Clear();
		cout << "No market data has arrived yet this session.\n"
			<< "Restarting the capture client first so the scan is not wasted.\n";
		RestartFeedClient();
		lastCaptureRestartTick = GetTickCount64();
		if (!WaitWithUi(6000)) return;
	}

	GlobalLog.Clear();
	cout << "Scan by Name: " << unique.size()
		<< " exact items from the held Black Market bids.\n"
		<< "Stand at a city market. No category or tier dropdown will be used.\n"
		<< "Hold " << Abort::HotkeyName() << " at any point to stop.\n";
	if (unnamed > 0) {
		cout << "  " << unnamed << " bid rows had no name in items.tsv and were skipped.\n";
	}

	const size_t offersBefore = store.CityCount();
	WindowHidden hidden(wnd_stack.top()->getSystemHandle());
	Abort::Guard stoppable;
	Sleep(1500);

	MarketScanner scanner(feed);
	scanner.SetService([this] { ServiceScan(false); });
	size_t attempted = 0;
	size_t completed = 0;
	size_t matchedOrders = 0;
	bool sawBlackMarketResult = false;

	for (const auto& entry : unique) {
		if (Abort::Requested()) {
			cout << "\n" << Abort::HotkeyName()
				<< " - stopping after " << completed << " names.\n";
			break;
		}

		const NameTarget& target = entry.second;
		cout << "  [" << (++attempted) << '/' << unique.size() << "] "
			<< target.searchText << '\n';

		vector<FeedOrder> batch;
		if (!scanner.ScanName(target.searchText, batch)) {
			cout << "    no market refresh; skipped\n";
			continue;
		}

		vector<FeedOrder> matching;
		matching.reserve(batch.size());
		for (FeedOrder& order : batch) {
			string group = order.groupTypeId;
			if (group.empty()) {
				group = order.itemTypeId;
				const size_t at = group.find('@');
				if (at != string::npos) {
					group.erase(at);
				}
			}

			if (group != target.groupTypeId || order.enchant != target.enchant) {
				continue;
			}
			if (order.locationId == BLACK_MARKET_LOCATION && order.isBuyOrder) {
				sawBlackMarketResult = true;
			}
			else if (order.locationId != BLACK_MARKET_LOCATION &&
				!order.isBuyOrder && order.tier > 0) {
				nameScannedCityTiers.insert(order.tier);
			}
			matching.push_back(std::move(order));
		}

		matchedOrders += matching.size();
		store.Absorb(std::move(matching));
		++completed;
	}

	const size_t offersAdded = store.CityCount() - offersBefore;
	cout << "\nScan by Name finished: " << completed << '/' << unique.size()
		<< " names completed, " << matchedOrders << " matching orders received.\n"
		<< "  city offers (buy side): +" << offersAdded << ", "
		<< store.CityCount() << " held\n";

	if (offersAdded == 0 && sawBlackMarketResult) {
		cout << "\n!! The name searches returned Black Market bids, not city offers.\n"
			<< "   Travel to a royal city market and run Scan by Name again.\n";
	}
}

void Game::RunScanRetry() {
	vector<ScanStep> failed = scanProgress.FailedSteps();
	if (failed.empty()) {
		return;
	}

	// group consecutive entries with the same category (the plan was built
	// grouped by category to begin with, so they already are)
	vector<ScanGroup> groups;
	for (const auto& step : failed) {
		if (!groups.empty() && groups.back().category->key == step.categoryKey) {
			if (step.tier != 0) {
				groups.back().tiers.push_back(step.tier);
			}
			if (step.enchant >= 0) {
				groups.back().enchants.push_back(step.enchant);
			}
			continue;
		}
		const MarketCategory* cat = catTable.Find(step.categoryKey);
		if (!cat) {
			continue;
		}
		ScanGroup group;
		group.category = cat;
		if (step.tier != 0) {
			group.tiers.push_back(step.tier);
		}
		if (step.enchant >= 0) {
			group.enchants.push_back(step.enchant);
		}
		groups.push_back(std::move(group));
	}

	// A group walks the cross product of its tiers and its levels, so the same
	// tier arriving from two failed levels must not enter the list twice - that
	// would re-scan it once per failure instead of once. The cross product does
	// mean a retry can cover pairs that did not fail; re-scanning a listing is
	// harmless (the feed dedupes by order id) and the alternative is carrying
	// explicit pairs all the way through.
	for (auto& group : groups) {
		sort(group.tiers.begin(), group.tiers.end());
		group.tiers.erase(unique(group.tiers.begin(), group.tiers.end()),
			group.tiers.end());
		sort(group.enchants.begin(), group.enchants.end());
		group.enchants.erase(unique(group.enchants.begin(), group.enchants.end()),
			group.enchants.end());
	}

	LaunchScan(std::move(groups), scanIsBlackMarket);
}

// Tiers the CITY scan profile is set to walk, unioned across every category.
//
// Empty result = no restriction. That happens two ways, and both are correct:
// nothing is configured at all, or at least one category is set to ONE PASS.
// A one-pass scan never opens the tier dropdown, so it brings back every tier
// the listing holds - there is no honest way to claim the data is limited to
// anything, and pretending otherwise would silently drop lots the user can
// plainly see on screen.
set<int> Game::AllowedCityTiers() const {
	set<int> allowed;

	for (const MarketCategory& category : catTable.All()) {
		const vector<int>& tiers = scanProfile.TiersFor(false, category.key);
		if (tiers.empty()) {
			return set<int>();
		}
		allowed.insert(tiers.begin(), tiers.end());
	}
	allowed.insert(nameScannedCityTiers.begin(), nameScannedCityTiers.end());

	return allowed;
}

vector<FlipLot> Game::BuildPlan(long long budget, double minProfitRate,
	long long minProfitSilver, bool requireBoth) {
	// pick up anything that arrived since the last scan
	store.Absorb(feed);

	vector<FeedOrder> bids = store.BlackMarketOrders();
	vector<FeedOrder> offers = store.CityOffers();

	if (bids.empty()) {
		cout << "No black market bids held. Scan at the black market first.\n";
		return {};
	}
	if (offers.empty()) {
		cout << "No city offers held. Scan in a royal city first.\n";
		return {};
	}

	BuyPlan settings;
	settings.budget = budget;
	settings.minProfitRate = minProfitRate;
	settings.minProfitSilver = minProfitSilver;
	settings.requireBoth = requireBoth;
	settings.maxUnitPrice = Config.MaxUnitPrice;
	settings.allowedTiers = AllowedCityTiers();

	if (minProfitSilver > 0) {
		cout << "Keeping lots that make at least " << int(minProfitRate * 100)
			<< "% " << (requireBoth ? "AND" : "OR") << " at least "
			<< minProfitSilver << " silver of profit.\n";
	}

	if (!settings.allowedTiers.empty()) {
		cout << "Buying tiers";
		for (int tier : settings.allowedTiers) {
			cout << " T" << tier;
		}
		cout << " only, from the city scan settings. Anything outside that is\n"
			<< "  ignored however good the margin looks.\n";
	}

	CityBuyer buyer(feed, itemTable);
	return buyer.Plan(bids, offers, settings);
}

// BuyCity without the buying.
//
// The same four questions, the same BuildPlan call, the same table, and no
// silver spent. It used to ask only for a percentage and take the other three
// from Config.txt, which made it an exact duplicate of Deals whenever budget
// was 0 - the shipped value - and left no way to try a gate short of editing
// that file or running the real thing to find out.
void Game::RunPlan(long long budget, double minProfitRate,
	long long minProfitSilver, bool requireBoth) {
	GlobalLog.Clear();

	vector<FlipLot> lots = BuildPlan(budget, minProfitRate, minProfitSilver,
		requireBoth);
	if (lots.empty()) {
		// BuildPlan explains the two "nothing scanned yet" cases itself. This
		// is the third one: both halves are present, they just do not pair
		// into anything profitable. Returning silently here made the button
		// look broken - the plan table, the "nothing worth buying" line, every
		// bit of feedback was skipped because PrintPlan is only reached when
		// there is something to print.
		if (store.BlackMarketCount() > 0 && store.CityCount() > 0) {
			cout << "No profitable flips found.\n"
				<< "  Compared " << store.BlackMarketCount()
				<< " black market bids against " << store.CityCount()
				<< " city offers.\n"
				<< "  Required at least " << int(minProfitRate * 100)
				<< "% profit";
			if (minProfitSilver > 0) {
				cout << " " << (requireBoth ? "AND" : "OR") << " at least "
					<< minProfitSilver << " silver per lot";
			}
			cout << ".\n"
				<< "  A flip needs the SAME item on both sides, so scan the same\n"
				<< "  categories in the city and at the black market. Otherwise\n"
				<< "  try lower thresholds.\n";
		}
		return;
	}

	long long spend = 0;
	long long profit = 0;
	int units = 0;
	for (const FlipLot& lot : lots) {
		spend += lot.spend;
		profit += lot.profit;
		units += lot.quantity;
	}

	CityBuyer::PrintPlan(lots);

	cout << "\n" << lots.size() << " lots, " << units << " units, "
		<< spend << " silver to spend, " << profit << " profit if every one\n"
		<< "  of them fills. This IS the order BuyCity works through, top to\n"
		<< "  bottom.\n"
		<< "\nNothing has been bought. Press BuyCity and enter the same four\n"
		<< "  figures to act on it.\n";

	// Straight into the hand-shopping view, with the table still sitting in
	// the log behind it. The table is the per-lot truth and stays the thing to
	// check the arithmetic against; this is the same list arranged for a
	// person holding a mouse.
	ShowShopList(lots, "PLAN");
}

// Every deal the current data supports, ranked, with no budget applied.
//
// The point is to answer "what order will BuyCity work through?" without
// spending anything and without the budget hiding the tail of the list. Plan
// does nearly the same job but trims to Config.Budget, so a lot that would not
// fit simply vanishes from it - which reads as "the bot did not find that
// deal" rather than "the bot could not afford it".
//
// Ranked biggest-profit-first, which is the order Plan produces and the order
// Run buys in, so the table printed here IS the buy order.
void Game::RunDeals() {
	GlobalLog.Clear();

	// Budget 0 = no limit, so nothing is trimmed away.
	vector<FlipLot> lots = BuildPlan(0, Config.MinProfitRate,
		Config.MinProfitSilver, Config.RequireBothProfits);
	if (lots.empty()) {
		if (store.BlackMarketCount() > 0 && store.CityCount() > 0) {
			cout << "No deals at " << int(Config.MinProfitRate * 100)
				<< "% or better.\n"
				<< "  Compared " << store.BlackMarketCount()
				<< " black market bids against " << store.CityCount()
				<< " city offers.\n"
				<< "  Lower min_profit_percent in Config.txt to see thinner ones.\n";
		}
		return;
	}

	long long total = 0;
	for (const FlipLot& lot : lots) {
		total += lot.profit;
	}

	cout << "Deals at " << int(Config.MinProfitRate * 100)
		<< "% or better, best first. BuyCity buys them in this order.\n\n";

	CityBuyer::PrintPlan(lots);

	cout << "\n" << lots.size() << " deals, " << total
		<< " silver of profit if every one of them fills.\n"
		<< "No budget applied here - BuyCity asks for one and will stop\n"
		<< "  partway down this list when the silver runs out, which is why\n"
		<< "  the fattest deals are at the top.\n";

	// Deals fills the shopping list too. It is the "what is out there right
	// now" button, so it is the one most likely to be pressed by someone
	// deciding what to buy by hand - and having it print a table but leave the
	// Shop screen showing the last run's list is worse than useless, because
	// the stale list still looks current.
	ShowShopList(lots, "DEALS");
}

void Game::RunBuy(long long budget, double minProfitRate,
	long long minProfitSilver, bool requireBoth) {
	GlobalLog.Clear();

	if (!feed.IsRunning()) {
		cout << "The market feed is not running - the bot never opened\n"
			<< "  127.0.0.1:5000. Restart the bot, then press Capture.\n";
		return;
	}

	vector<FlipLot> lots = BuildPlan(budget, minProfitRate, minProfitSilver,
		requireBoth);
	if (lots.empty()) {
		return;
	}

	CityBuyer::PrintPlan(lots);

	cout << "\nSwitch to the game window now, standing at the city market.\n";
	cout << "Hold " << Abort::HotkeyName() << " at any point to stop.\n";
	Sleep(2000);

	BuyPlan settings;
	settings.budget = budget;
	settings.minProfitRate = minProfitRate;
	settings.minProfitSilver = minProfitSilver;
	settings.requireBoth = requireBoth;
	settings.maxUnitPrice = Config.MaxUnitPrice;

	BuyReport report;
	{
		// Buying clicks buyFirstRow (1232,424), confirmBuy (844,722) and the
		// quality menu - all of them inside the area this window can cover.
		WindowHidden hidden(wnd_stack.top()->getSystemHandle());
		Abort::Guard stoppable;

		CityBuyer buyer(feed, itemTable);
		report = buyer.Run(lots, settings, holdings);
	}
	holdings.Save(HOLDINGS_FILE);

	// prices and stock have moved, so the held offers are stale now
	store.ClearCities();
	nameScannedCityTiers.clear();

	// What the run came home without, re-costed as lots in its own right.
	//
	// This is the screen's real job. A run comes up short for reasons that are
	// nobody's fault - another player took the offer between the reading and
	// the click, the game lagged for a second, the stop was pressed - and the
	// goods it missed are still on the market for a few minutes more. Working
	// that out by re-running Plan does not work: ClearCities just ran, so a
	// fresh plan would come back empty and read as "nothing left", which is
	// the opposite of the truth.
	const vector<FlipLot> missed = CityBuyer::Shortfall(lots, report.unbought);

	if (missed.empty()) {
		cout << "\nNothing left over - every planned unit was bought.\n";
		current_wnd = "Menu";
		return;
	}

	long long missedProfit = 0;
	int missedUnits = 0;
	for (const FlipLot& lot : missed) {
		missedProfit += lot.profit;
		missedUnits += lot.quantity;
	}

	cout << "\n" << missedUnits << " units in " << missed.size()
		<< " lots were not bought, worth " << missedProfit << " profit.\n"
		<< "  Opening the shopping list for them - click an item to copy its\n"
		<< "  search text, then buy under the price shown.\n";

	ShowShopList(missed, report.aborted ? "STOPPED - NOT BOUGHT"
		: "NOT BOUGHT");
}



// Step 3 of the BuyCity entry: AND or OR.
//
// Only reached when a silver floor was actually entered. With the floor at
// zero there is nothing to combine, so the step is skipped rather than shown
// as a question whose answer changes nothing.
void Game::BuyCombineController(const string& command) {
	if (command == "BACK") {
		_profit = "";
		current_wnd = "Menu";
		return;
	}
	if (command != "AND" && command != "OR") {
		return;
	}

	pendingBuyRequireBoth = (command == "AND");
	_profit = "";
	current_wnd = "EnterWndBuy";
}

void Game::EnterToWindow() {

	if (sfEvent->key.code == Keyboard::Escape) {
		// On a number-entry screen Escape means "changed my mind", not "quit
		// the program". Those screens have no BACK button, so the only way
		// out of a mistyped BuyCity was closing the whole tool.
		if (current_wnd == "EnterWndBM" ||
			current_wnd == "EnterWndBuyProfit" ||
			current_wnd == "EnterWndBuyMinProfit" ||
			current_wnd == "EnterWndBuy") {
			_profit = "";
			current_wnd = "Menu";
			return;
		}

		// The shop screen has a BACK button, but it is the screen most likely
		// to be left in a hurry - the game is waiting on the other side of an
		// alt-tab. Escape closing the whole tool from here would throw the
		// list away.
		if (current_wnd == "Shop") {
			current_wnd = "Menu";
			return;
		}

		wnd_stack.top()->close();
		return;
	}
	if (current_wnd == "EnterWndBM" ||
		current_wnd == "EnterWndBuyProfit" ||
		current_wnd == "EnterWndBuyMinProfit" ||
		current_wnd == "EnterWndBuy") {

		// Num0..Num9 are consecutive in sf::Keyboard::Key
		if (sfEvent->key.code >= Keyboard::Num0 &&
			sfEvent->key.code <= Keyboard::Num9) {
			// MAX_ENTERED_DIGITS keeps the value inside long long range,
			// so the stoll below can never throw
			if (_profit.size() < MAX_ENTERED_DIGITS) {
				_profit += char('0' + (sfEvent->key.code - Keyboard::Num0));
			}
		}
		else if (sfEvent->key.code == Keyboard::Backspace) {
			if (_profit.size() > 0)
				_profit.erase(_profit.size() - 1);
		}
		else if (sfEvent->key.code == Keyboard::Enter) {

			if (_profit.empty()) {
				GlobalLog.Clear();
				cout << "Enter a number first\n";
				return;
			}

			const long long value = stoll(_profit);

			if (current_wnd == "EnterWndBM") {
				GlobalLog.Clear();
				cout << "Switch to the game window now, at the black market.\n";
				cout << "Hold " << Abort::HotkeyName()
					<< " at any point to stop.\n";
				Sleep(2000);

				{
					// Selling clicks search (586,264) and sell (1238,427),
					// both of which this window can be sitting on top of.
					WindowHidden hidden(wnd_stack.top()->getSystemHandle());
					Abort::Guard stoppable;

					// the entered figure is a profit %, not a raw silver price
					// - the bot checks it against what Holdings says was paid
					BlackMarketSeller seller(feed, itemTable);
					SellPlan plan;
					plan.minProfitRate = double(value) / 100.0;
					seller.Run(plan, holdings);
				}

				// Saved after the window is back, and outside the guard, so a
				// stopped run writes the ledger exactly like a finished one.
				// Everything sold before the stop is sold.
				holdings.Save(HOLDINGS_FILE);
			}
			else if (current_wnd == "EnterWndBuyProfit") {
				// step 1 of 4: the margin a lot has to clear
				pendingBuyProfitRate = double(value) / 100.0;
				current_wnd = "EnterWndBuyMinProfit";
				_profit = "";
				return;
			}
			else if (current_wnd == "EnterWndBuyMinProfit") {
				// step 2 of 4: the silver a lot has to earn. Zero skips the
				// AND/OR question entirely - there is nothing to combine, and
				// asking anyway would be a screen with no answer that matters.
				pendingBuyMinProfit = value;
				current_wnd = value > 0 ? "EnterWndBuyCombine" : "EnterWndBuy";
				_profit = "";
				return;
			}
			else if (current_wnd == "EnterWndBuy") {
				// step 4 of 4: the entered figure is the silver ceiling, and
				// 0 means no ceiling at all - the whole ranked list.
				if (pendingBuyDryRun) {
					RunPlan(value, pendingBuyProfitRate, pendingBuyMinProfit,
						pendingBuyRequireBoth);
				}
				else {
					RunBuy(value, pendingBuyProfitRate, pendingBuyMinProfit,
						pendingBuyRequireBoth);
				}
			}
			current_wnd = "Menu";
			_profit = "";
		}
	}


}





// Run
void Game::run()
{
	while (wnd_stack.top()->isOpen())
	{
		updateDt();
		update();
		if (wnd_stack.top()->isOpen()) render();
	}
}

// Update

// Keep Windows' message queue responsive without routing another command
// into an active scan. Category scans file batches incrementally. Name scans
// retain their buffer until their per-item filter has accepted it.
void Game::ServiceScan(bool absorb) {
	const auto now = GetTickCount64();
	if (now - scanServiceTick < 100) return;
	scanServiceTick = now;
	Event event;
	while (wnd_stack.top()->pollEvent(event)) {
		if (event.type == Event::Closed) {
			Abort::Request();
			wnd_stack.top()->close();
		}
	}
	if (absorb) store.Absorb(feed);
	if (wnd_stack.top()->isOpen() && !IsIconic(wnd_stack.top()->getSystemHandle())) {
		updateMousePos();
		render();
	}
}

bool Game::WaitWithUi(int milliseconds) {
	const auto deadline = GetTickCount64() + static_cast<unsigned long long>(max(0, milliseconds));
	while (wnd_stack.top()->isOpen() && GetTickCount64() < deadline) {
		ServiceScan(false);
		Sleep(25);
	}
	return wnd_stack.top()->isOpen();
}

void Game::update()
{
	updateMousePos();
	updateSFMLEvents();
	const auto tick = GetTickCount64();
	if (tick - feedPollTick >= 100) {
		feedPollTick = tick;
		store.Absorb(feed);
	}

}

void Game::updateSFMLEvents()
{
	while (wnd_stack.top()->pollEvent(*sfEvent))
	{
		switch (sfEvent->type) {
		case Event::Closed:
			wnd_stack.top()->close();
			break;
		case Event::KeyPressed:
			if (current_wnd != "Shop" || !ShopKey()) EnterToWindow();
			break;
		case Event::TextEntered:
			if (current_wnd == "Shop") ShopText(sfEvent->text.unicode);
			break;

		case Event::MouseButtonPressed:
			if (sfEvent->mouseButton.button != Mouse::Left) break;
			if (current_wnd == "EnterWndBuyCombine") {
				BuyCombineController(HitBuyCombine(float(sfEvent->mouseButton.x),
					float(sfEvent->mouseButton.y)));
			}
			else if (current_wnd == "Menu") {
				MenuConroller(HitMenu(float(sfEvent->mouseButton.x),
					float(sfEvent->mouseButton.y)));
			}
			else if (current_wnd == "Scan") {
				ScanController(HitScan(float(sfEvent->mouseButton.x),
					float(sfEvent->mouseButton.y)));
			}
			else if (current_wnd == "Scanning") {
				ScanningController(HitScanning(float(sfEvent->mouseButton.x),
					float(sfEvent->mouseButton.y)));
			}
			else if (current_wnd == "Shop") {
				ShopController();
			}
			else if (current_wnd == "Settings") {
				SettingsController(HitSettings(float(sfEvent->mouseButton.x),
					float(sfEvent->mouseButton.y)));
			}
			break;

		// The first scrolling screen in the program. Only the shop list has
		// more content than window, so this deliberately does not try to be a
		// general mechanism.
		case Event::MouseWheelScrolled:
			if (current_wnd == "Shop") {
				// delta is positive scrolling up, and one notch is 1.0. Just
				// under half a block per notch: enough to feel responsive,
				// small enough that a line being read does not jump the gap.
				shopScroll -= sfEvent->mouseWheelScroll.delta * 34.f;

				const float maxScroll = ShopMaxScroll();
				if (shopScroll > maxScroll) {
					shopScroll = maxScroll;
				}
				if (shopScroll < 0.f) {
					shopScroll = 0.f;
				}
			}
			break;




		}

	}
}

void Game::updateDt() {
	this->dt = this->dtClock.restart().asSeconds();
	// a frame can measure as 0 s, and int(1/0.f) is undefined
	int frames = dt > 0.f ? int(1 / dt) : 0;
	fpsLabel = "FPS " + to_string(frames);

}

void Game::updateMousePos()
{
	mPosX = Mouse::getPosition(*wnd_stack.top()).x;
	mPosY = Mouse::getPosition(*wnd_stack.top()).y;
}


// Render

void Game::render()
{
	// Flat black, not an image. systemBackground in dark mode IS black, and a
	// textured backdrop under translucent-looking chrome is the one thing that
	// stops an iOS layout reading as one.
	wnd_stack.top()->clear(Theme::Background);

	// Screens that have been rebuilt set this themselves; the ones still on
	// the old fixed layout want the original y.
	logTop = 540.f;

	// hover state first, otherwise the highlight always shows one frame late
	if (current_wnd == "Menu") {
		DrawMenuScreen();
	}
	else if (current_wnd == "Scan") {
		DrawScanScreen();
	}
	else if (current_wnd == "Scanning") {
		DrawScanningScreen();
	}
	else if (current_wnd == "Settings") {
		DrawSettingsScreen();
	}
	else if (current_wnd == "Shop") {
		DrawShopScreen();
	}
	else if (current_wnd == "EnterWndBuyCombine") {
		DrawBuyCombineScreen();
	}
	else if (current_wnd == "EnterWndBM" ||
		current_wnd == "EnterWndBuyProfit" ||
		current_wnd == "EnterWndBuyMinProfit" ||
		current_wnd == "EnterWndBuy") {
		drawEnterWnd();
	}


	//Loading();

	// The cursor read-out used to be drawn here in green, following the mouse.
	// It was a coordinate-measuring aid from when targets were found by hand,
	// and against the rebuilt UI it sits on top of whatever is under the
	// pointer - it was covering a tier pill in the first screenshot of the
	// running app. Coordinates are measured with a DPI-aware tool now (I9), so
	// it has nothing left to do.
	//
	// FPS moves to the bottom-right and goes muted: at the top-left it was
	// inside the nav bar, overlapping the back control.
	Ui::DrawRight(*wnd_stack.top(), fpsLabel, Ui::Text::Caption,
		Theme::TextMuted, float(WIDTH) - 8.f, float(HEIGHT) - 20.f);
	drawVersion();

	// The shop screen is the one screen that takes the whole window. It is
	// read while alt-tabbing to the game and back, so the room the log panel
	// costs buys three more entries visible at once - and the log behind it is
	// the plan table this screen was built from, which is the one thing there
	// is no reason to look at while working through it.
	if (current_wnd != "Shop") {
		DrawLog();
	}

	wnd_stack.top()->display();
}

void Game::drawEnterWnd() {
	RenderWindow& wnd = *wnd_stack.top();

	// Plan and BuyCity share these screens, so every one of them says which it
	// is. Four identical prompts where one spends real silver and the other
	// does not is exactly the kind of thing to get wrong once - which is why
	// the title is the loudest thing on the screen and says the word BUY.
	const bool selling = current_wnd == "EnterWndBM";
	const string who = selling ? "SELL" : (pendingBuyDryRun ? "PLAN" : "BUY");

	string step, prompt, detail;
	if (selling) {
		step = "";
		prompt = "Sell if profit over cost is at least";
		detail = "Checked against what Holdings says was paid, not the market price.";
	}
	else if (current_wnd == "EnterWndBuyProfit") {
		step = "step 1 of 4";
		prompt = "Profit % a lot must clear";
		detail = "A margin alone fills the list with 300% flips worth forty silver.";
	}
	else if (current_wnd == "EnterWndBuyMinProfit") {
		step = "step 2 of 4";
		prompt = "...and/or silver a lot must earn";
		detail = "Total profit for the lot, not per unit.  0 takes this out entirely.";
	}
	else {
		step = "step 4 of 4";
		prompt = "Silver budget";
		detail = "0 means no limit.";
	}

	const bool backHot = Ui::NavBackRect().contains(float(mPosX), float(mPosY));
	Ui::DrawNavBar(wnd, float(WIDTH),
		step.empty() ? who : (who + "   " + step), "Cancel", backHot);

	Ui::Draw(wnd, prompt, Ui::Text::Title2, Theme::TextPrimary, Ui::Inset, 84.f);
	Ui::Draw(wnd, detail, Ui::Text::Footnote, Theme::TextMuted, Ui::Inset, 118.f);

	// The field. Tinted while it holds something, so an empty box cannot be
	// mistaken for a typed zero - and zero means something specific on three
	// of these four screens.
	const FloatRect field(Ui::Inset, 150.f, 420.f, 64.f);
	Ui::FillRounded(wnd, field, Ui::RadiusGroup, Theme::Panel);
	Ui::StrokeRounded(wnd, field, Ui::RadiusGroup,
		_profit.empty() ? Theme::Border : Theme::Accent, 1.f);
	Ui::Draw(wnd, _profit.empty() ? "0" : _profit, Ui::Text::LargeTitle,
		_profit.empty() ? Theme::TextMuted : Theme::TextPrimary,
		field.left + 20.f, field.top + 11.f);

	Ui::Draw(wnd, "Enter to confirm     Esc to cancel", Ui::Text::Footnote,
		Theme::TextMuted, Ui::Inset, 228.f);

	logTop = 268.f;
}

void Game::DrawBuyCombineScreen() {
	RenderWindow& wnd = *wnd_stack.top();

	const string who = pendingBuyDryRun ? "PLAN" : "BUY";
	const bool backHot = Ui::NavBackRect().contains(float(mPosX), float(mPosY));
	Ui::DrawNavBar(wnd, float(WIDTH), who + "   step 3 of 4", "Cancel", backHot);

	Ui::Draw(wnd, "How do the two thresholds combine?", Ui::Text::Title2,
		Theme::TextPrimary, Ui::Inset, 84.f);

	Ui::DrawSegmented(wnd, FloatRect(Ui::Inset, 130.f, 300.f, 32.f),
		{ "AND", "OR" }, pendingBuyRequireBoth ? 0 : 1);

	// Spelled out rather than left to the words AND and OR, because getting
	// this backwards quietly buys the wrong things all run.
	char line[200];
	snprintf(line, sizeof(line),
		"AND   at least %d%% AND at least %lld silver of profit",
		int(pendingBuyProfitRate * 100), pendingBuyMinProfit);
	Ui::Draw(wnd, line, Ui::Text::Footnote, Theme::TextSecondary, Ui::Inset, 182.f);

	Ui::Draw(wnd, "OR    either one is enough on its own",
		Ui::Text::Footnote, Theme::TextSecondary, Ui::Inset, 202.f);
	Ui::Draw(wnd, "Either way the list is bought biggest profit first.",
		Ui::Text::Footnote, Theme::TextMuted, Ui::Inset, 228.f);

	logTop = 268.f;
}

string Game::HitBuyCombine(float px, float py) const {
	if (Ui::NavBackRect().contains(px, py)) {
		return "BACK";
	}
	const int seg = Ui::HitSegmented(FloatRect(Ui::Inset, 130.f, 300.f, 32.f),
		2, px, py);
	if (seg == 0) return "AND";
	if (seg == 1) return "OR";
	return "";
}

void Game::drawVersion() {
	wnd_stack.top()->draw(*version);
}


// ------------------------------------------------------- category grid

namespace {
	// Where the 46 category chips start on every screen that shows them.
	const float GRID_TOP_SCAN = 60.f;
	const float GRID_TOP_SETTINGS = 104.f;

}

// Routes by category KEY rather than by the label under the cursor.
//
// The old screens compared the clicked button's text against `menu_sub`, which
// is why that column has to be globally unique (invariant I8) - two categories
// sharing a submenu name would have toggled each other. Nothing on this path
// reads the label any more, so a duplicate would now be a cosmetic problem
// instead of a functional one.
string Game::HitCategory(float px, float py, float top) const {
	const vector<MarketCategory>& all = catTable.All();
	for (size_t i = 0; i < all.size(); ++i) {
		if (Ui::ChipRect(i, Ui::Inset, top).contains(px, py)) {
			return Ui::CategoryCommand(all[i].key);
		}
	}
	return "";
}

// ---------------------------------------------------------------- Scan

void Game::DrawScanScreen(RenderTarget* target) {
	RenderTarget& wnd = target ? *target : *wnd_stack.top();
	const vector<MarketCategory>& all = catTable.All();

	const bool backHot = Ui::NavBackRect().contains(float(mPosX), float(mPosY));
	Ui::DrawNavBar(wnd, float(WIDTH), "Scan", "Menu", backHot);

	for (size_t i = 0; i < all.size(); ++i) {
		const FloatRect r = Ui::ChipRect(i, Ui::Inset, GRID_TOP_SCAN);
		const bool on = selectedScan.count(all[i].key) != 0;
		Ui::DrawChip(wnd, r, all[i].menuSub,
			on ? Ui::ChipState::Selected : Ui::ChipState::Pending);
	}

	const float gb = Ui::GridBottom(all.size(), GRID_TOP_SCAN);

	Ui::Draw(wnd, to_string(selectedScan.size()) + " of " + to_string(all.size())
		+ " selected", Ui::Text::Footnote, Theme::TextMuted, Ui::Inset, gb + 6.f);

	const float by = gb + 30.f;
	Ui::DrawButton(wnd, FloatRect(Ui::Inset, by, 200.f, 44.f), "SCAN BM",
		Ui::ButtonStyle::Filled, false);
	Ui::DrawButton(wnd, FloatRect(Ui::Inset + 216.f, by, 200.f, 44.f), "SCAN MARKET",
		Ui::ButtonStyle::Filled, false);
	Ui::DrawButton(wnd, FloatRect(Ui::Inset + 432.f, by, 200.f, 44.f), "CLEAR",
		Ui::ButtonStyle::Tinted, false);
	Ui::DrawButton(wnd, FloatRect(Ui::Inset + 648.f, by, 212.f, 44.f), "Select all",
		Ui::ButtonStyle::Tinted, false);

	logTop = by + 60.f;
}

string Game::HitScan(float px, float py) const {
	if (Ui::NavBackRect().contains(px, py)) {
		return "BACK";
	}

	const float gb = Ui::GridBottom(catTable.All().size(), GRID_TOP_SCAN);
	const float by = gb + 30.f;
	if (FloatRect(Ui::Inset, by, 200.f, 44.f).contains(px, py))          return "SCAN BM";
	if (FloatRect(Ui::Inset + 216.f, by, 200.f, 44.f).contains(px, py))  return "SCAN MARKET";
	if (FloatRect(Ui::Inset + 432.f, by, 200.f, 44.f).contains(px, py))  return "CLEAR";
	if (FloatRect(Ui::Inset + 648.f, by, 212.f, 44.f).contains(px, py))  return "SELECT ALL";

	return HitCategory(px, py, GRID_TOP_SCAN);
}

// ------------------------------------------------------------ Scanning

void Game::DrawScanningScreen() {
	RenderWindow& wnd = *wnd_stack.top();
	const vector<MarketCategory>& all = catTable.All();
	vector<ScanStep> steps = scanProgress.Snapshot();

	// Worst-state-wins per category: running beats failed beats ok, so one
	// bad tier cannot be hidden by the eleven good ones next to it.
	map<string, StepState> aggregate;
	for (const auto& step : steps) {
		StepState& agg = aggregate[step.categoryKey];   // default Pending
		if (step.state == StepState::Running) {
			agg = StepState::Running;
		}
		else if (step.state == StepState::Failed && agg != StepState::Running) {
			agg = StepState::Failed;
		}
		else if (step.state == StepState::Ok &&
			agg != StepState::Running && agg != StepState::Failed) {
			agg = StepState::Ok;
		}
	}

	const bool backHot = Ui::NavBackRect().contains(float(mPosX), float(mPosY));
	Ui::DrawNavBar(wnd, float(WIDTH),
		scanProgress.IsRunning() ? "Scanning" : "Scan finished", "Menu", backHot);

	for (size_t i = 0; i < all.size(); ++i) {
		Ui::ChipState state = Ui::ChipState::NotInRun;   // not part of this run
		auto it = aggregate.find(all[i].key);
		if (it != aggregate.end()) {
			switch (it->second) {
			case StepState::Running: state = Ui::ChipState::Running; break;
			case StepState::Ok:      state = Ui::ChipState::Ok; break;
			case StepState::Failed:  state = Ui::ChipState::Failed; break;
			default:                 state = Ui::ChipState::Pending; break;
			}
		}
		Ui::DrawChip(wnd, Ui::ChipRect(i, Ui::Inset, GRID_TOP_SCAN),
			all[i].menuSub, state);
	}

	const float gb = Ui::GridBottom(all.size(), GRID_TOP_SCAN);
	const float pct = scanProgress.Percent();

	Ui::DrawProgress(wnd, FloatRect(Ui::Inset, gb + 8.f,
		float(WIDTH) - Ui::Inset * 2.f, 6.f), pct / 100.f);

	int ok = 0, failed = 0, pending = 0;
	scanProgress.Counts(ok, failed, pending);

	ostringstream info;
	info << (scanProgress.IsRunning() ? "Scanning: " : "Finished - ")
		<< scanProgress.CurrentLabel() << "   " << int(pct) << "%";
	Ui::Draw(wnd, info.str(), Ui::Text::Subhead, Theme::TextPrimary,
		Ui::Inset, gb + 22.f);

	ostringstream counts;
	counts << "OK " << ok << "    FAILED " << failed << "    PENDING " << pending;
	Ui::Draw(wnd, counts.str(), Ui::Text::Footnote, Theme::TextSecondary,
		Ui::Inset, gb + 44.f);

	const bool canRetry = failed > 0 && !scanProgress.IsRunning();
	if (canRetry) {
		Ui::Draw(wnd, "Red did not scan - the game may have lagged.",
			Ui::Text::Footnote, Theme::Failed, Ui::Inset + 240.f, gb + 44.f);
	}

	// Drawn muted when there is nothing to retry, rather than hidden: a button
	// that comes and goes is harder to find than one that is simply inert.
	Ui::DrawButton(wnd, FloatRect(Ui::Inset, gb + 66.f, 200.f, 44.f),
		"RETRY FAILED", Ui::ButtonStyle::Tinted, !canRetry);
	Ui::DrawButton(wnd, FloatRect(Ui::Inset + 216.f, gb + 66.f, 200.f, 44.f),
		"CRAFT PROFIT", Ui::ButtonStyle::Filled, scanProgress.IsRunning());

	logTop = gb + 122.f;
}

string Game::HitScanning(float px, float py) const {
	if (Ui::NavBackRect().contains(px, py)) {
		return "BACK";
	}
	const float gb = Ui::GridBottom(catTable.All().size(), GRID_TOP_SCAN);
	if (!scanProgress.IsRunning() && FloatRect(Ui::Inset + 216.f, gb + 66.f, 200.f, 44.f).contains(px, py)) return "CRAFT PROFIT";
	if (FloatRect(Ui::Inset, gb + 66.f, 200.f, 44.f).contains(px, py)) {
		return "RETRY FAILED";
	}
	return "";
}

// ------------------------------------------------------------- Settings

namespace {
	// The tier and enchantment pills, laid out once for drawing and hit test.
	FloatRect TierPill(int tier, float y) {
		return FloatRect(Ui::Inset + float(tier - 1) * 52.f, y, 44.f, 32.f);
	}
	FloatRect EnchantPill(int level, float y) {
		return FloatRect(Ui::Inset + float(level) * 52.f, y, 44.f, 32.f);
	}
	const float SEG_W = 300.f;
}

void Game::DrawSettingsScreen() {
	RenderWindow& wnd = *wnd_stack.top();
	const vector<MarketCategory>& all = catTable.All();

	const bool backHot = Ui::NavBackRect().contains(float(mPosX), float(mPosY));
	Ui::DrawNavBar(wnd, float(WIDTH), "Settings", "Menu", backHot);

	Ui::DrawSegmented(wnd, FloatRect(Ui::Inset, 56.f, SEG_W, 32.f),
		{ "BLACK MARKET", "CITY MARKET" }, settingsBlackMarket ? 0 : 1);

	const MarketCategory* cat = catTable.Find(settingsSelectedCategory);
	vector<int> tiers = scanProfile.TiersFor(settingsBlackMarket, settingsSelectedCategory);
	vector<int> enchants = scanProfile.EnchantsFor(settingsBlackMarket, settingsSelectedCategory);
	const bool onePass = tiers.empty();

	Ui::Draw(wnd, "Editing:  " + string(cat ? cat->menuSub : "(none)"),
		Ui::Text::Headline, Theme::TextPrimary, Ui::Inset + SEG_W + 24.f, 58.f);

	// An empty tier list does NOT mean "skip this category" - it means one
	// pass with no tier filter, which brings back every tier. Saying so here
	// is cheaper than having it inferred wrongly from eight dark buttons.
	Ui::Draw(wnd, onePass ? "one pass, no tier filter" : "tier by tier",
		Ui::Text::Footnote, Theme::TextMuted, Ui::Inset + SEG_W + 24.f, 78.f);

	for (size_t i = 0; i < all.size(); ++i) {
		const bool editing = all[i].key == settingsSelectedCategory;
		Ui::DrawChip(wnd, Ui::ChipRect(i, Ui::Inset, GRID_TOP_SETTINGS),
			all[i].menuSub,
			editing ? Ui::ChipState::Selected : Ui::ChipState::NotInRun);
	}

	const float gb = Ui::GridBottom(all.size(), GRID_TOP_SETTINGS);

	Ui::Draw(wnd, "TIERS", Ui::Text::FootnoteStrong, Theme::TextMuted,
		Ui::Inset, gb + 6.f);
	for (int t = 1; t <= 8; ++t) {
		const bool on = find(tiers.begin(), tiers.end(), t) != tiers.end();
		Ui::DrawPill(wnd, TierPill(t, gb + 26.f), "T" + to_string(t), on);
	}

	// The black market listing is not filtered by enchantment at all, so these
	// are drawn inert there rather than lit - a lit button would promise a
	// split the scanner would never perform.
	Ui::Draw(wnd, settingsBlackMarket
		? "ENCHANTMENTS   city market only"
		: "ENCHANTMENTS", Ui::Text::FootnoteStrong, Theme::TextMuted,
		Ui::Inset, gb + 68.f);
	for (int e = 0; e <= 4; ++e) {
		const bool on = !settingsBlackMarket &&
			find(enchants.begin(), enchants.end(), e) != enchants.end();
		Ui::DrawPill(wnd, EnchantPill(e, gb + 88.f), "E" + to_string(e), on);
	}

	Ui::DrawButton(wnd, FloatRect(470.f, gb + 26.f, 200.f, 44.f),
		"ONE PASS", Ui::ButtonStyle::Tinted, onePass);
	Ui::DrawButton(wnd, FloatRect(686.f, gb + 26.f, 194.f, 44.f),
		"APPLY TO ALL", Ui::ButtonStyle::Tinted, false);
	Ui::Draw(wnd, "Saved on every click - there is no Apply step.",
		Ui::Text::Footnote, Theme::TextMuted, 470.f, gb + 80.f);

	// No log here. Settings writes scan_profile.tsv on every click and has
	// nothing to report; the room goes to the grid instead.
	logTop = float(HEIGHT);
}

string Game::HitSettings(float px, float py) const {
	if (Ui::NavBackRect().contains(px, py)) {
		return "BACK";
	}

	const int seg = Ui::HitSegmented(FloatRect(Ui::Inset, 56.f, SEG_W, 32.f), 2, px, py);
	if (seg == 0) return "BLACK MARKET";
	if (seg == 1) return "CITY MARKET";

	const float gb = Ui::GridBottom(catTable.All().size(), GRID_TOP_SETTINGS);

	for (int t = 1; t <= 8; ++t) {
		if (TierPill(t, gb + 26.f).contains(px, py)) {
			return "T" + to_string(t);
		}
	}
	for (int e = 0; e <= 4; ++e) {
		if (EnchantPill(e, gb + 88.f).contains(px, py)) {
			return "E" + to_string(e);
		}
	}
	if (FloatRect(470.f, gb + 26.f, 200.f, 44.f).contains(px, py)) {
		return "ONE PASS (NO TIER)";
	}
	if (FloatRect(686.f, gb + 26.f, 194.f, 44.f).contains(px, py)) {
		return "APPLY TO ALL";
	}

	return HitCategory(px, py, GRID_TOP_SETTINGS);
}

// --------------------------------------------------------------- main menu

string Game::NextAction() const {
	if (!shopBlocks.empty()) return "Shop";
	if (!store.BlackMarketCount() || !store.CityCount()) return "Scan";
	return "Deals";
}

void Game::DrawMenuScreen(RenderTarget* target) {
	RenderTarget& wnd = target ? *target : *wnd_stack.top();
	Ui::Draw(wnd, "ALBION / DESKTOP", Ui::Text::Caption, Theme::Accent, Ui::Inset, 17.f);
	Ui::Draw(wnd, "Market Studio", Ui::Text::LargeTitle, Theme::TextPrimary, Ui::Inset, 35.f);
	const auto marketTick = feed.LastMarketTick();
	Ui::DrawRight(wnd, marketTick ? "Market data " + to_string((GetTickCount64() - marketTick) / 1000) + "s ago" :
		"Waiting for market data", Ui::Text::Caption, Theme::TextMuted, 880.f, 63.f);
	const bool ready = feed.IsRunning();
	Ui::FillRounded(wnd, FloatRect(670.f, 24.f, 210.f, 30.f), 15.f,
		ready ? Theme::OkTint : Theme::FailedTint);
	Ui::Draw(wnd, ready ? "Receiver ready" : "Receiver unavailable", Ui::Text::FootnoteStrong,
		ready ? Theme::Ok : Theme::Failed, 688.f, 31.f);

	Ui::FillRounded(wnd, FloatRect(Ui::Inset, 90.f, 860.f, 108.f), 16.f, Theme::Panel);
	Ui::FillRounded(wnd, FloatRect(Ui::Inset, 108.f, 3.f, 72.f), 1.f, Theme::Accent);
	const string next = NextAction();
	const string headline = next == "Shop" ? "Pick up where you left off." :
		(next == "Deals" ? "Your market data is ready." : "Turn market data into your next move.");
	const string detail = next == "Shop" ? to_string(shopBlocks.size()) + " searches saved. Copy, compare, keep moving." :
		(next == "Deals" ? "Find the strongest flips from your captured buy and sell orders." :
		"Scan the Black Market, then a city market. Build your shopping list.");
	Ui::Draw(wnd, headline, Ui::Text::Title2, Theme::TextPrimary, 44.f, 108.f);
	Ui::Draw(wnd, detail, Ui::Text::Footnote, Theme::TextSecondary, 44.f, 143.f);
	Ui::Draw(wnd, "Local data. Your prices stay on this computer.", Ui::Text::Caption, Theme::TextMuted, 44.f, 172.f);
	Ui::DrawButton(wnd, FloatRect(690.f, 132.f, 166.f, 40.f),
		next == "Shop" ? "Resume shopping" : (next == "Deals" ? "Find deals" : "Start scanning"),
		Ui::ButtonStyle::Filled, FloatRect(690.f, 132.f, 166.f, 40.f).contains(float(mPosX), float(mPosY)));

	const string labels[] = {"BLACK MARKET BIDS", "CITY OFFERS", "PROFIT ESTIMATED"};
	const string values[] = {Studio::Number(static_cast<long long>(store.BlackMarketCount())),
		Studio::Number(static_cast<long long>(store.CityCount())), (shopEstimate.Total() >= 0 ? "+" : "") + Studio::Number(shopEstimate.Total())};
	const string captions[] = {"Captured demand", "Captured supply", to_string(shopEstimate.Count()) + " copied searches"};
	for (int i = 0; i < 3; ++i) {
		const float x = 20.f + float(i) * 292.f;
		Ui::FillRounded(wnd, FloatRect(x, 214.f, 276.f, 80.f), 12.f, Theme::Panel);
		Ui::Draw(wnd, labels[i], Ui::Text::Caption, Theme::TextMuted, x + 16.f, 225.f);
		Ui::Draw(wnd, values[i], Ui::Text::Title2, i == 2 ? Theme::Ok : Theme::TextPrimary, x + 16.f, 243.f);
		Ui::Draw(wnd, captions[i], Ui::Text::Caption, Theme::TextMuted, x + 16.f, 275.f);
	}
	const string hot = HitMenu(float(mPosX), float(mPosY));
	float bottom = 0.f;
	for (const MenuBlock& block : MenuBlocks(shopBlocks.size())) {
		Ui::DrawGroup(wnd, block.geom, block.section->header, block.rows, hot);
		bottom = max(bottom, block.geom.bottom);
	}
	logTop = bottom + 22.f;
	if (!studioNotice.empty() && GetTickCount64() - studioNoticeTick < 8000)
		Ui::Draw(wnd, studioNotice, Ui::Text::Footnote, studioNoticeError ? Theme::Failed : Theme::Ok,
			Ui::Inset, logTop - 18.f);
}

string Game::HitMenu(float px, float py) const {
	if (FloatRect(690.f, 132.f, 166.f, 40.f).contains(px, py)) return NextAction();
	const vector<MenuBlock> blocks = MenuBlocks(shopBlocks.size());
	for (const MenuBlock& b : blocks) {
		const string hit = Ui::HitGroup(b.geom, b.rows, px, py);
		if (!hit.empty()) {
			return hit;
		}
	}
	return "";
}


void Game::DrawLog() {
	// Docked to the bottom of whatever the screen above it ended at. Rounded
	// and filled like every other surface, so it reads as part of the same
	// system rather than a console bolted underneath one.
	const float x = Ui::Inset;
	const float y = logTop;
	const float w = float(WIDTH) - Ui::Inset * 2.f;
	const float h = float(HEIGHT) - y - Ui::Inset;
	if (h < 40.f) {
		return;      // the screen above needs the room more than the log does
	}

	// As many lines as this panel can actually show, not a fixed twenty.
	//
	// Twenty was chosen when the panel was always 270px tall. It is a different
	// height on every screen now, and a printed plan is one header, one row per
	// lot and a total - so a twenty-line cap quietly cut the bottom off any
	// plan with more than about seventeen lots. Truncating a shopping list at
	// an arbitrary count is the wrong thing for a panel someone reads to decide
	// what to buy.
	const int fits = int((h - 24.f) / 15.f);
	vector<string> lines = GlobalLog.Lines(fits > 0 ? size_t(fits) : 1);
	if (lines.empty()) {
		return;
	}

	Ui::FillRounded(*wnd_stack.top(), FloatRect(x, y, w, h),
		Ui::RadiusGroup, Theme::Panel);

	// Still CascadiaCode at 11px: PrintPlan builds its table with snprintf
	// column widths, and those only line up in a monospace face.
	float lineY = y + 10.f;
	const float maxY = y + h - 14.f;
	for (const auto& text : lines) {
		if (lineY > maxY) {
			break;
		}
		Ui::Draw(*wnd_stack.top(), text, Ui::Text::Mono,
			Theme::TextSecondary, x + 14.f, lineY);
		lineY += 15.f;
	}
}

void Game::CapturePreview(const string& imagePath, int seconds) {
	auto& previewWindow = *wnd_stack.top();
	previewWindow.setVisible(false);
	const auto started = GetTickCount64();
	while (previewWindow.isOpen() && GetTickCount64() - started <
		static_cast<unsigned long long>(seconds) * 1000) {
		updateDt(); update();
		if (previewWindow.isOpen()) render();
		Sleep(16);
	}
	if (previewWindow.isOpen()) {
		// Hidden Windows windows do not provide a readable framebuffer.
		// Render the real menu into an offscreen target for CLI previews.
		current_wnd = "Menu";
		sf::RenderTexture frame;
		if (frame.create(WIDTH, HEIGHT)) {
			frame.clear(Theme::Background);
			DrawMenuScreen(&frame);
			frame.draw(*version);
			frame.display();
			frame.getTexture().copyToImage().saveToFile(imagePath);
		}
		previewWindow.close();
	}
}
