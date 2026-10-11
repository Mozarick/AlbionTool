#pragma once
#include <string>
#include <vector>

#include "UiKit.h"

using namespace std;


// The main menu, grouped by CONSEQUENCE rather than laid out as ten identical
// boxes.
//
// The old grid put `Plan` and `BuyCity` side by side looking exactly alike,
// and only one of them spends real silver. That was already the most dangerous
// thing about the screen; the four groups below are what fixes it, and the
// grouping is the reason this is a list rather than a prettier grid.
//
// `command` is what MenuConroller matches on and MUST NOT change - it is the
// routing id. `label` is only ever read by a human, so it is free to be
// reworded or translated without touching a single comparison.
struct MenuEntry {
	const char* command;
	const char* label;
	Ui::Tone tone;
	bool chevron;
};

struct MenuSection {
	const char* header;
	vector<MenuEntry> entries;
};

// Order matters twice over: top-to-bottom is the order the work is actually
// done in, and the destructive group is last because nothing below it can be
// reached by overshooting.
inline const vector<MenuSection>& MenuSections() {
	static const vector<MenuSection> sections = {
		{ "MARKET DATA", {
			{ "Scan",     "Scan markets",  Ui::Tone::Default, true },
			{ "ScanName", "Scan by item name", Ui::Tone::Default, false },
			{ "Settings", "Scan settings", Ui::Tone::Default, true },
			// The single most common cure for "scan returns 0 orders".
			{ "Capture",  "Reconnect capture", Ui::Tone::Default, true }
		} },

		// Both of these are dry runs. Saying so in the header is cheaper than
		// hoping the reader remembers which of the six buttons cost money.
		{ "PREVIEW \xC2\xB7 SPENDS NOTHING", {
			{ "Plan",  "Plan",  Ui::Tone::Default, true },
			{ "Deals", "Find deals", Ui::Tone::Default, true },
			{ "Shop",  "Manual shopping", Ui::Tone::Default, true }
		} },

		{ "SPENDS SILVER", {
			{ "BuyCity", "Auto-buy in city",          Ui::Tone::Default, true },
			{ "SellBM",  "Auto-sell to Black Market", Ui::Tone::Default, true }
		} },

		// Clear wipes the ledger too, and the goods it forgets do not leave
		// the bag. Red, and on its own, for that reason.
		{ "DANGER", {
			{ "Clear", "Clear Everything", Ui::Tone::Destructive, false },
			{ "Exit",  "Exit",             Ui::Tone::Destructive, false }
		} }
	};
	return sections;
}


// One group of the menu, with the geometry it was laid out at.
struct MenuBlock {
	const MenuSection* section;
	vector<Ui::Row> rows;
	Ui::GroupGeom geom;
};

// The four groups, two per column.
//
// Called by the drawing AND by the hit test, and that is the point: the old
// menu matched clicks by comparing the label string, so a reworded label would
// silently stop routing. Here a click can only land in a box this function
// produced, and each box carries its own command id.
//
// It lives in the header rather than in Game.cpp so the preview harness can
// render the real menu instead of a copy that drifts.
inline vector<MenuBlock> MenuBlocks(size_t shopCount) {
	const float colW = 420.f;
	const float colX[2] = { Ui::Inset, Ui::Inset + colW + 20.f };
	const float firstTop = 314.f;
	const float gapY = 26.f;

	const vector<MenuSection>& sections = MenuSections();
	vector<MenuBlock> blocks;
	blocks.reserve(sections.size());

	float rowTop = firstTop;
	float rowBottom = firstTop;

	for (size_t i = 0; i < sections.size(); ++i) {
		// Two columns, so every second section starts a new row. The row
		// advances by the TALLEST group in it, not by a fixed step - the
		// groups hold different numbers of rows.
		if (i > 0 && (i % 2) == 0) {
			rowTop = rowBottom + gapY;
		}

		MenuBlock b;
		b.section = &sections[i];
		for (const MenuEntry& e : sections[i].entries) {
			Ui::Row r;
			r.command = e.command;
			r.label = e.label;
			r.chevron = e.chevron;
			r.tone = e.tone;

			// The one row that carries a value: how much is waiting on the
			// shopping list, visible without opening it.
			if (r.command == "Shop" && shopCount > 0) {
				r.value = to_string(shopCount) +
					(shopCount == 1 ? " item" : " items");
			}
			b.rows.push_back(r);
		}

		b.geom = Ui::LayoutGroup(colX[i % 2], rowTop, colW, true, b.rows.size());
		if (b.geom.bottom > rowBottom) {
			rowBottom = b.geom.bottom;
		}
		blocks.push_back(b);
	}
	return blocks;
}
