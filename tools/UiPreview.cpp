// Renders the real screens to PNG files without opening a window.
//
// The bot's own startup launches the Albion launcher and an elevated capture
// client, which is far too much ceremony for "does the menu look right". This
// draws through the same UiKit calls, the same MenuBlocks() layout and the
// same Ui::ChipRect / Ui::GridBottom arithmetic the app uses, into a
// RenderTexture, and writes the result out. If it looks right here it looks
// right there - the only thing it cannot prove is the event routing.
//
// Run it from SFML/ so the font and categories.tsv paths resolve.

#include <SFML/Graphics.hpp>

#include "../SFML/Headers/UiKit.h"
#include "../SFML/Headers/Menus.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// The real category names, so the grid is measured against the real longest
// label ("Satchels of Insight") rather than made-up short ones.
static std::vector<std::string> LoadCategories() {
	std::vector<std::string> out;
	std::ifstream fin("categories.tsv");
	std::string line;
	bool first = true;
	while (std::getline(fin, line)) {
		if (first) { first = false; continue; }   // header row
		std::istringstream ss(line);
		std::string key, top, sub;
		if (std::getline(ss, key, '\t') && std::getline(ss, top, '\t') &&
			std::getline(ss, sub, '\t')) {
			out.push_back(sub);
		}
	}
	return out;
}

static void LogPanel(sf::RenderTarget& t, float top,
	const std::vector<std::string>& lines) {

	const float x = Ui::Inset;
	const float w = 900.f - Ui::Inset * 2.f;
	const float h = 820.f - top - Ui::Inset;
	if (h < 40.f) return;
	Ui::FillRounded(t, sf::FloatRect(x, top, w, h), Ui::RadiusGroup, Theme::Panel);

	float y = top + 10.f;
	for (const std::string& s : lines) {
		if (y > top + h - 14.f) break;
		Ui::Draw(t, s, Ui::Text::Mono, Theme::TextSecondary, x + 14.f, y);
		y += 15.f;
	}
}

static bool Save(sf::RenderTexture& rt, const char* name) {
	rt.display();
	if (!rt.getTexture().copyToImage().saveToFile(name)) {
		std::cout << "could not write " << name << "\n";
		return false;
	}
	std::cout << "wrote " << name << "\n";
	return true;
}


int main() {
	if (!Ui::LoadFonts("fonts/")) {
		std::cout << "fonts missing - run this from the SFML/ directory\n";
		return 1;
	}
	const std::vector<std::string> cats = LoadCategories();
	if (cats.empty()) {
		std::cout << "categories.tsv not found - run this from the SFML/ directory\n";
		return 1;
	}
	std::cout << cats.size() << " categories loaded\n";

	sf::RenderTexture rt;
	if (!rt.create(900, 820)) {
		std::cout << "cannot create a render texture\n";
		return 1;
	}

	// ---- 1. Menu, exactly as Game::DrawMenuScreen draws it ---------------
	rt.clear(Theme::Background);
	Ui::Draw(rt, "Albion", Ui::Text::LargeTitle, Theme::TextPrimary, Ui::Inset, 18.f);
	std::vector<MenuBlock> blocks = MenuBlocks(14);
	float bottom = 0.f;
	for (const MenuBlock& b : blocks) {
		Ui::DrawGroup(rt, b.geom, b.section->header, b.rows, "BuyCity");
		if (b.geom.bottom > bottom) bottom = b.geom.bottom;
	}
	LogPanel(rt, bottom + 26.f, {
		"item                          T en qty  qual     avg buy     BM pays      profit",
		"Boltcasters                   4  2  19   q2*       12,301      18,940     125,133",
		"Broadsword                    6  0  12   q1        34,110      49,880     178,470",
		"TOTAL                                              701,441                903,603"
	});
	if (!Save(rt, "ui-menu.png")) return 1;

	// ---- 2. Scan ---------------------------------------------------------
	const float GRID = 60.f;
	rt.clear(Theme::Background);
	Ui::DrawNavBar(rt, 900.f, "Scan", "Menu", false);
	for (size_t i = 0; i < cats.size(); ++i) {
		// the weapons, which is what a real run usually selects
		const bool on = i >= 29;
		Ui::DrawChip(rt, Ui::ChipRect(i, Ui::Inset, GRID), cats[i],
			on ? Ui::ChipState::Selected : Ui::ChipState::Pending);
	}
	float gb = Ui::GridBottom(cats.size(), GRID);
	Ui::Draw(rt, "17 of 46 selected", Ui::Text::Footnote, Theme::TextMuted,
		Ui::Inset, gb + 6.f);
	Ui::DrawButton(rt, sf::FloatRect(Ui::Inset, gb + 30.f, 200.f, 44.f),
		"SCAN BM", Ui::ButtonStyle::Filled, false);
	Ui::DrawButton(rt, sf::FloatRect(Ui::Inset + 216.f, gb + 30.f, 200.f, 44.f),
		"SCAN MARKET", Ui::ButtonStyle::Filled, false);
	Ui::DrawButton(rt, sf::FloatRect(Ui::Inset + 432.f, gb + 30.f, 200.f, 44.f),
		"CLEAR", Ui::ButtonStyle::Tinted, false);
	LogPanel(rt, gb + 90.f, { "Pick the categories to scan, then choose which market." });
	if (!Save(rt, "ui-scan.png")) return 1;

	// ---- 3. Scanning -----------------------------------------------------
	rt.clear(Theme::Background);
	Ui::DrawNavBar(rt, 900.f, "Scanning", "Menu", false);
	for (size_t i = 0; i < cats.size(); ++i) {
		Ui::ChipState st = Ui::ChipState::NotInRun;
		if (i >= 29) {
			st = Ui::ChipState::Ok;
			if (i == 38) st = Ui::ChipState::Running;
			else if (i == 33 || i == 41) st = Ui::ChipState::Failed;
			else if (i >= 43) st = Ui::ChipState::Pending;
		}
		Ui::DrawChip(rt, Ui::ChipRect(i, Ui::Inset, GRID), cats[i], st);
	}
	gb = Ui::GridBottom(cats.size(), GRID);
	Ui::DrawProgress(rt, sf::FloatRect(Ui::Inset, gb + 8.f, 860.f, 6.f), 0.76f);
	Ui::Draw(rt, "Scanning: Holy Staff t6.2   76%", Ui::Text::Subhead,
		Theme::TextPrimary, Ui::Inset, gb + 22.f);
	Ui::Draw(rt, "OK 12    FAILED 2    PENDING 3", Ui::Text::Footnote,
		Theme::TextSecondary, Ui::Inset, gb + 44.f);
	Ui::Draw(rt, "Red did not scan - the game may have lagged.", Ui::Text::Footnote,
		Theme::Failed, Ui::Inset + 240.f, gb + 44.f);
	Ui::DrawButton(rt, sf::FloatRect(Ui::Inset, gb + 66.f, 200.f, 44.f),
		"RETRY FAILED", Ui::ButtonStyle::Tinted, false);
	LogPanel(rt, gb + 122.f, {
		"  t6.2: 3 extra pages, 412 orders",
		"  !! t6.4 SKIPPED - nothing scanned for it",
		"Hold Ctrl+Shift+Q at any point to stop."
	});
	if (!Save(rt, "ui-scanning.png")) return 1;

	// ---- 4. Settings -----------------------------------------------------
	const float SGRID = 104.f;
	rt.clear(Theme::Background);
	Ui::DrawNavBar(rt, 900.f, "Settings", "Menu", false);
	Ui::DrawSegmented(rt, sf::FloatRect(Ui::Inset, 56.f, 300.f, 32.f),
		{ "BLACK MARKET", "CITY MARKET" }, 1);
	Ui::Draw(rt, "Editing:  Sword", Ui::Text::Headline, Theme::TextPrimary,
		Ui::Inset + 324.f, 58.f);
	Ui::Draw(rt, "tier by tier", Ui::Text::Footnote, Theme::TextMuted,
		Ui::Inset + 324.f, 78.f);
	for (size_t i = 0; i < cats.size(); ++i) {
		Ui::DrawChip(rt, Ui::ChipRect(i, Ui::Inset, SGRID), cats[i],
			i == 45 ? Ui::ChipState::Selected : Ui::ChipState::NotInRun);
	}
	gb = Ui::GridBottom(cats.size(), SGRID);
	Ui::Draw(rt, "TIERS", Ui::Text::FootnoteStrong, Theme::TextMuted, Ui::Inset, gb + 6.f);
	for (int t = 1; t <= 8; ++t) {
		Ui::DrawPill(rt, sf::FloatRect(Ui::Inset + float(t - 1) * 52.f, gb + 26.f, 44.f, 32.f),
			"T" + std::to_string(t), t >= 4 && t <= 7);
	}
	Ui::Draw(rt, "ENCHANTMENTS", Ui::Text::FootnoteStrong, Theme::TextMuted,
		Ui::Inset, gb + 68.f);
	for (int e = 0; e <= 4; ++e) {
		Ui::DrawPill(rt, sf::FloatRect(Ui::Inset + float(e) * 52.f, gb + 88.f, 44.f, 32.f),
			"E" + std::to_string(e), e <= 2);
	}
	Ui::DrawButton(rt, sf::FloatRect(470.f, gb + 26.f, 200.f, 44.f),
		"ONE PASS", Ui::ButtonStyle::Tinted, false);
	Ui::DrawButton(rt, sf::FloatRect(686.f, gb + 26.f, 194.f, 44.f),
		"APPLY TO ALL", Ui::ButtonStyle::Tinted, false);
	Ui::Draw(rt, "Saved on every click - there is no Apply step.", Ui::Text::Footnote,
		Theme::TextMuted, 470.f, gb + 80.f);
	if (!Save(rt, "ui-settings.png")) return 1;

	// ---- 5. Shopping list ------------------------------------------------
	rt.clear(Theme::Background);
	Ui::DrawNavBar(rt, 900.f, "Shopping List", "Menu", false);
	Ui::Draw(rt, "NOT BOUGHT   6 searches, 71 units, 2,904,118 profit",
		Ui::Text::Subhead, Theme::TextPrimary, Ui::Inset, 54.f);
	Ui::Draw(rt, "Click an item to copy its search text, then paste it into the "
		"market search box.   Wheel scrolls.",
		Ui::Text::Footnote, Theme::TextMuted, Ui::Inset, 78.f);

	struct Card { const char* name; const char* profit; const char* meta;
		const char* price; bool done; };
	const Card cards[] = {
		{ "Boltcasters T4.2", "+1,240,000", "T4   ench 2   up to 37 units   quality Good or better", "BUY UNDER  12,450  each", false },
		{ "Broadsword T6.0", "+890,412", "T6   ench 0   up to 12 units   quality Normal or better", "BUY UNDER  34,800  each", false },
		{ "Great Nature Staff T5.1", "+412,900", "T5   ench 1   up to 9 units   quality Good or better   (2 bands merged)", "BUY UNDER  21,300  each", false },
		{ "Soldier Helmet T6.1", "+201,551", "T6   ench 1   up to 6 units   quality Outstanding or better", "BUY UNDER  18,020  each", true },
		{ "Cleric Robe T5.0", "+109,255", "T5   ench 0   up to 4 units   quality Normal or better", "BUY UNDER  9,940  each", false },
		{ "Hunter Hood T4.3", "+50,000", "T4   ench 3   up to 3 units   quality Good or better", "BUY UNDER  7,110  each", false }
	};
	for (int i = 0; i < 6; ++i) {
		const Card& c = cards[i];
		const sf::FloatRect box(Ui::Inset, 106.f + float(i) * 94.f, 860.f - 12.f, 86.f);
		Ui::FillRounded(rt, box, Ui::RadiusGroup, Theme::Panel);
		Ui::StrokeRounded(rt, box, Ui::RadiusGroup,
			c.done ? Theme::Border : Theme::BorderStrong, 1.f);
		Ui::Draw(rt, (c.done ? std::string("* ") : std::string()) + c.name,
			Ui::Text::Headline, c.done ? Theme::TextMuted : Theme::TextPrimary,
			box.left + 16.f, box.top + 8.f);
		Ui::DrawRight(rt, c.profit, Ui::Text::Headline,
			c.done ? Theme::TextMuted : Theme::Ok, box.left + box.width - 16.f, box.top + 8.f);
		Ui::Draw(rt, c.meta, Ui::Text::Footnote,
			c.done ? Theme::TextMuted : Theme::TextSecondary, box.left + 16.f, box.top + 33.f);
		Ui::Draw(rt, c.price, Ui::Text::CalloutStrong,
			c.done ? Theme::TextMuted : Theme::AccentText, box.left + 16.f, box.top + 55.f);
	}
	Ui::FillRounded(rt, sf::FloatRect(874.f, 106.f, 6.f, 694.f), 3.f, Theme::PanelRaised);
	Ui::FillRounded(rt, sf::FloatRect(874.f, 106.f, 6.f, 340.f), 3.f, Theme::BorderStrong);
	if (!Save(rt, "ui-shop.png")) return 1;

	// ---- 6. Number entry, the screen that spends real silver -------------
	rt.clear(Theme::Background);
	Ui::DrawNavBar(rt, 900.f, "BUY   step 1 of 4", "Cancel", false);
	Ui::Draw(rt, "Profit % a lot must clear", Ui::Text::Title2, Theme::TextPrimary,
		Ui::Inset, 84.f);
	Ui::Draw(rt, "A margin alone fills the list with 300% flips worth forty silver.",
		Ui::Text::Footnote, Theme::TextMuted, Ui::Inset, 118.f);
	const sf::FloatRect field(Ui::Inset, 150.f, 420.f, 64.f);
	Ui::FillRounded(rt, field, Ui::RadiusGroup, Theme::Panel);
	Ui::StrokeRounded(rt, field, Ui::RadiusGroup, Theme::Accent, 1.f);
	Ui::Draw(rt, "25", Ui::Text::LargeTitle, Theme::TextPrimary,
		field.left + 20.f, field.top + 11.f);
	Ui::Draw(rt, "Enter to confirm     Esc to cancel", Ui::Text::Footnote,
		Theme::TextMuted, Ui::Inset, 228.f);
	LogPanel(rt, 268.f, { "Keeping lots that make at least 25% AND at least 50000 silver." });
	if (!Save(rt, "ui-entry.png")) return 1;

	return 0;
}
