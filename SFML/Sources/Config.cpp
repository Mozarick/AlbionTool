#include "../Headers/Config.h"


MyConfig Config;


static string Trim(const string& s) {
	size_t begin = s.find_first_not_of(" \t\r\n");
	if (begin == string::npos) {
		return "";
	}
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(begin, end - begin + 1);
}

// Deliberately hand-rolled instead of stoi: a malformed Config.txt used to
// throw before main() even started.
static bool ToInt(const string& s, int& out) {
	if (s.empty() || s.size() > 9) {
		return false;
	}
	for (const auto& c : s) {
		if (c < '0' || c > '9') {
			return false;
		}
	}
	out = stoi(s);
	return true;
}

// Turns "ctrl+shift+q" into modifier flags and a virtual-key code.
//
// The VK codes are written as numeric literals on purpose: this file includes
// nothing but Config.h, and Config.h is included nearly everywhere, so pulling
// Windows.h in here to spell VK_PAUSE would drag it along with it. The four
// values below are fixed by the Windows API and are not going to move.
//
// Returns false and touches nothing on anything it does not understand, so a
// typo leaves the working default in place rather than an unpressable key.
static bool ParseHotkey(const string& spec, bool& ctrl, bool& shift,
	bool& alt, int& vk, string& name) {

	bool wantCtrl = false;
	bool wantShift = false;
	bool wantAlt = false;
	int wantVk = 0;
	string pretty;

	size_t at = 0;
	while (at <= spec.size()) {
		size_t plus = spec.find('+', at);
		string token = Trim(spec.substr(at,
			plus == string::npos ? string::npos : plus - at));
		at = plus == string::npos ? spec.size() + 1 : plus + 1;

		if (token.empty()) {
			return false;
		}
		for (auto& c : token) {
			if (c >= 'A' && c <= 'Z') {
				c = char(c - 'A' + 'a');
			}
		}

		string label;
		if (token == "ctrl" || token == "control") {
			wantCtrl = true;
			label = "Ctrl";
		}
		else if (token == "shift") {
			wantShift = true;
			label = "Shift";
		}
		else if (token == "alt") {
			wantAlt = true;
			label = "Alt";
		}
		else if (wantVk != 0) {
			// two non-modifier keys: not a combination this can poll
			return false;
		}
		else if (token.size() == 1 && token[0] >= 'a' && token[0] <= 'z') {
			wantVk = token[0] - 'a' + 'A';
			label = string(1, char(wantVk));
		}
		else if (token.size() == 1 && token[0] >= '0' && token[0] <= '9') {
			wantVk = token[0];
			label = token;
		}
		else if (token == "pause" || token == "break") {
			wantVk = 0x13;   // VK_PAUSE
			label = "Pause";
		}
		else if (token == "scrolllock" || token == "scroll") {
			wantVk = 0x91;   // VK_SCROLL
			label = "ScrollLock";
		}
		else if (token == "insert") {
			wantVk = 0x2D;   // VK_INSERT
			label = "Insert";
		}
		else if (token == "end") {
			wantVk = 0x23;   // VK_END
			label = "End";
		}
		else if (token.size() >= 2 && token[0] == 'f') {
			int n = 0;
			if (!ToInt(token.substr(1), n) || n < 1 || n > 12) {
				return false;
			}
			wantVk = 0x70 + n - 1;   // VK_F1..VK_F12
			label = "F" + to_string(n);
		}
		else {
			return false;
		}

		if (!pretty.empty()) {
			pretty += '+';
		}
		pretty += label;
	}

	// Modifiers alone cannot be a stop key; something has to be pressed.
	if (wantVk == 0) {
		return false;
	}

	ctrl = wantCtrl;
	shift = wantShift;
	alt = wantAlt;
	vk = wantVk;
	name = pretty;
	return true;
}


static void ReadInt(const string& key, const string& value, int& target) {
	int parsed = 0;
	if (ToInt(value, parsed)) {
		target = parsed;
	}
	else {
		cout << "Config: bad value for " << key
			<< ", keeping " << target << '\n';
	}
}


MyConfig::MyConfig() {
	Load("Config.txt");
}

void MyConfig::Load(const string& path) {
	ifstream fin(path);

	if (!fin) {
		cout << "Config.txt not found, using defaults\n";
	}

	string line;
	while (getline(fin, line)) {
		string stripped = Trim(line);
		if (stripped.empty() || stripped[0] == '#') {
			continue;
		}

		// split on the first colon only, so Windows paths keep their drive
		size_t sep = line.find(':');
		if (sep == string::npos) {
			continue;
		}

		string key = Trim(line.substr(0, sep));
		string value = Trim(line.substr(sep + 1));
		if (key.empty()) {
			continue;
		}

		if (key == "screen_resolution") {
			size_t comma = value.find(',');
			int w = 0;
			int h = 0;
			if (comma != string::npos &&
				ToInt(Trim(value.substr(0, comma)), w) &&
				ToInt(Trim(value.substr(comma + 1)), h) &&
				w > 0 && h > 0) {
				ScreenResolution = { w, h };
			}
			else {
				cout << "Config: bad value for screen_resolution, keeping "
					<< ScreenResolution.first << 'x'
					<< ScreenResolution.second << '\n';
			}
		}
		else if (key == "tessdata_path") {
			TessdataPath = value;
		}
		else if (key == "click_hold_ms") {
			ReadInt(key, value, ClickHoldMs);
		}
		else if (key == "click_delay_ms") {
			ReadInt(key, value, ClickDelayMs);
		}
		else if (key == "type_char_delay_ms") {
			ReadInt(key, value, TypeCharDelayMs);
		}
		else if (key == "poll_interval_ms") {
			ReadInt(key, value, PollIntervalMs);
		}
		else if (key == "wait_timeout_ms") {
			ReadInt(key, value, WaitTimeoutMs);
		}
		else if (key == "menu_open_delay_ms") {
			ReadInt(key, value, MenuOpenDelayMs);
		}
		else if (key == "page_timeout_ms") {
			ReadInt(key, value, PageTimeoutMs);
		}
		else if (key == "page_click_delay_ms") {
			ReadInt(key, value, PageClickDelayMs);
		}
		else if (key == "page_quiet_clicks") {
			ReadInt(key, value, PageQuietClicks);
		}
		else if (key == "page_size") {
			ReadInt(key, value, PageSize);
		}
		else if (key == "move_ms_per_100px") {
			ReadInt(key, value, MoveMsPer100px);
		}
		else if (key == "move_min_ms") {
			ReadInt(key, value, MoveMinMs);
		}
		else if (key == "move_max_ms") {
			ReadInt(key, value, MoveMaxMs);
		}
		else if (key == "move_jitter_px") {
			ReadInt(key, value, MoveJitterPx);
		}
		else if (key == "move_target_slop_px") {
			ReadInt(key, value, MoveTargetSlopPx);
		}
		else if (key == "buy_units_per_click") {
			ReadInt(key, value, BuyUnitsPerClick);
		}
		else if (key == "purchase_confirm_ms") {
			ReadInt(key, value, PurchaseConfirmMs);
		}
		else if (key == "buy_retries") {
			ReadInt(key, value, BuyRetries);
		}
		else if (key == "search_retries") {
			ReadInt(key, value, SearchRetries);
		}
		else if (key == "stray_buy_limit") {
			ReadInt(key, value, StrayBuyLimit);
		}
		else if (key == "budget") {
			int parsed = 0;
			if (ToInt(value, parsed)) {
				Budget = parsed;
			}
		}
		else if (key == "max_unit_price") {
			int parsed = 0;
			if (ToInt(value, parsed)) {
				MaxUnitPrice = parsed;
			}
		}
		else if (key == "min_profit_silver") {
			int parsed = 0;
			if (ToInt(value, parsed)) {
				MinProfitSilver = parsed;
			}
		}
		else if (key == "profit_combine") {
			// "and" or "or"; anything else keeps the current setting rather
			// than silently picking the looser of the two.
			if (value == "and" || value == "AND") {
				RequireBothProfits = true;
			}
			else if (value == "or" || value == "OR") {
				RequireBothProfits = false;
			}
			else {
				cout << "Config: profit_combine wants and/or, keeping "
					<< (RequireBothProfits ? "and" : "or") << '\n';
			}
		}
		else if (key == "abort_hotkey") {
			if (!ParseHotkey(value, AbortCtrl, AbortShift, AbortAlt, AbortVk,
				AbortHotkeyName)) {
				cout << "Config: abort_hotkey not understood, keeping "
					<< AbortHotkeyName << '\n';
			}
		}
		else if (key == "min_profit_percent") {
			int parsed = 0;
			if (ToInt(value, parsed)) {
				MinProfitRate = double(parsed) / 100.0;
			}
			else {
				cout << "Config: bad value for min_profit_percent, keeping "
					<< int(MinProfitRate * 100) << "\n";
			}
		}
	}

	fin.close();

	// PollIntervalMs of 0 would spin the CPU flat out in the wait loops
	if (PollIntervalMs < 1) {
		PollIntervalMs = 1;
	}

	// Zero here would mean "buy nothing, forever", and the buying loop would
	// spin through its round limit doing nothing at all.
	if (BuyUnitsPerClick < 1) {
		BuyUnitsPerClick = 1;
	}
	if (BuyRetries < 0) {
		BuyRetries = 0;
	}

	// Zero would mean the listing is never read at all, so every lot would
	// report itself gone from the market.
	if (SearchRetries < 1) {
		SearchRetries = 1;
	}
	if (StrayBuyLimit < 0) {
		StrayBuyLimit = 0;
	}

	// Zero would mean the first quiet pager click ends the listing, which is
	// the behaviour this setting exists to stop.
	if (PageQuietClicks < 1) {
		PageQuietClicks = 1;
	}

	// Negative is meaningless; 0 is the documented "turn the shortcut off".
	if (PageSize < 0) {
		PageSize = 0;
	}

	ScreenScale = { ScreenResolution.first / 1920.f,
		ScreenResolution.second / 1080.f };

	cout << "Current Resolution: " << ScreenResolution.first << ' '
		<< ScreenResolution.second << '\n';
	cout << "Current Scale: " << ScreenScale.first << ' '
		<< ScreenScale.second << '\n';
}
