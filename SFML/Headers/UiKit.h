#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

#include "Theme.h"

// The drawing vocabulary the screens are built from.
//
// It exists because the thing being drawn is no longer "a box with a label".
// A grouped inset list needs a section header, a row with a chevron and an
// optional value on the right, hairline separators inset from the left, and
// corners rounded only at the ends of the group. None of that is expressible
// as a TextBox, which is why TextBox could not simply be restyled.
//
// SFML 2.5 has no rounded-rectangle primitive, no type ramp and no notion of a
// design token, so those are the three things this file adds. Everything else
// is still plain SFML underneath.
namespace Ui {

	// ---------------------------------------------------------------- metrics
	// iOS numbers, from the Human Interface Guidelines. They are named rather
	// than repeated so a screen never invents its own row height.
	const float RowH = 44.f;      // the standard list row, and the min touch target
	const float HeaderH = 25.f;   // section header above a group
	const float NavH = 44.f;      // nav bar
	const float Inset = 20.f;     // page margin
	const float RowInset = 16.f;  // text inset inside a row
	const float ChipH = 32.f;
	const float ChipW = 206.f;    // four across a 900px window
	const float ChipGapX = 12.f;
	const float ChipPitchY = 42.f;
	const float RadiusGroup = 12.f;
	const float RadiusChip = 8.f;
	const float RadiusPill = 22.f;

	// ------------------------------------------------------------------ type
	// The iOS ramp, set in Inter. Weight is part of the style, not a separate
	// axis, because SFML loads one static face per weight - there is no
	// variable-font axis to reach for.
	enum class Text {
		LargeTitle,      // 34 Bold    screen title
		Title2,          // 22 Bold
		Headline,        // 17 SemiBold  card title, nav title
		Body,            // 17 Regular   list row label
		Callout,         // 16 Regular
		CalloutStrong,   // 16 SemiBold  the price line on a shop card
		Subhead,         // 15 Regular
		Footnote,        // 13 Regular   secondary detail
		FootnoteStrong,  // 13 SemiBold  section headers, chip labels
		Caption,         // 12 Regular
		Mono             // 11          the log panel; stays CascadiaCode
	};

	// Loads Inter (4 weights) and CascadiaCode from `dir`. Safe to call twice.
	// Returns false if a face is missing, and in that case every text call
	// falls back to the mono face rather than drawing nothing.
	bool LoadFonts(const std::string& dir);

	sf::Text Make(const std::string& s, Text style, const sf::Color& colour);
	void Draw(sf::RenderTarget&, const std::string& s, Text style,
		const sf::Color& colour, float x, float y);
	// Right-aligns the string so its right edge lands on `rightX`.
	void DrawRight(sf::RenderTarget&, const std::string& s, Text style,
		const sf::Color& colour, float rightX, float y);
	float Width(const std::string& s, Text style);
	float LineHeight(Text style);

	// ---------------------------------------------------------------- shapes
	// SFML has no rounded rect, so one is built out of a Shape subclass whose
	// point list walks four quarter-circles.
	void FillRounded(sf::RenderTarget&, const sf::FloatRect&, float radius,
		const sf::Color& fill);
	void StrokeRounded(sf::RenderTarget&, const sf::FloatRect&, float radius,
		const sf::Color& stroke, float weight = 1.f);

	// ----------------------------------------------------------------- lists
	enum class Tone { Default, Destructive, Accent };

	// One row of a grouped inset list.
	//
	// `command` is the routing id and is NEVER the text shown. The old UI
	// matched clicks by comparing the label string, which is what forced
	// invariant I8 (menu_sub must be globally unique) and what would have
	// broken silently the moment a label was reworded or translated: an
	// unmatched click returns an empty string, and an empty command is a
	// no-op, so the button would simply stop working with nothing in the log.
	struct Row {
		std::string command;
		std::string label;
		std::string value;      // right-aligned, before the chevron
		bool chevron = true;
		Tone tone = Tone::Default;
	};

	// Where a group's parts land. Computed once and handed to both the drawing
	// and the hit test, so the box a click falls in is by construction the box
	// that was drawn.
	struct GroupGeom {
		sf::FloatRect header;              // height 0 when there is no header
		std::vector<sf::FloatRect> rows;
		sf::FloatRect box;                 // the rounded container
		float bottom = 0.f;                // y just past the group
	};

	GroupGeom LayoutGroup(float x, float y, float w, bool hasHeader,
		size_t rowCount);

	void DrawGroup(sf::RenderTarget&, const GroupGeom&,
		const std::string& header, const std::vector<Row>& rows,
		const std::string& hotCommand);

	// Returns the command of the row under the point, or "" for a miss.
	std::string HitGroup(const GroupGeom&, const std::vector<Row>& rows,
		float px, float py);

	// --------------------------------------------------------------- buttons
	enum class ButtonStyle { Filled, Tinted, Destructive };

	// A 44px capsule. Pressed dims it, which is the whole press feedback -
	// no animation, because a run blocks the event thread anyway.
	void DrawButton(sf::RenderTarget&, const sf::FloatRect&,
		const std::string& label, ButtonStyle, bool pressed);

	// ----------------------------------------------------------------- chips
	// The 46 category cells. These five states are the reason Theme keeps
	// saturated colours at all.
	enum class ChipState { NotInRun, Pending, Running, Ok, Failed, Selected };

	void DrawChip(sf::RenderTarget&, const sf::FloatRect&,
		const std::string& label, ChipState);

	// Grid position of cell `index` in the 4-column category grid.
	sf::FloatRect ChipRect(size_t index, float x0, float y0);

	// y just past the last row of a `count`-cell grid starting at `top`.
	// Lives here rather than in Game.cpp so the preview harness measures the
	// same grid the app draws.
	float GridBottom(size_t count, float top);

	// Category cells are routed by KEY, prefixed so a category can never
	// collide with an action command like "CLEAR".
	//
	// Both ends go through these rather than spelling the prefix out, because
	// spelling it out at one end and not the other is exactly how every
	// category click stopped registering once already: the hit test moved to
	// keys, the controllers kept comparing display labels, nothing matched,
	// and an unmatched command is a silent no-op. Two functions cannot drift
	// the way two string literals can.
	std::string CategoryCommand(const std::string& key);
	// The key, or "" when this command is not a category.
	std::string CategoryKey(const std::string& command);

	// ------------------------------------------------------------ segmented
	void DrawSegmented(sf::RenderTarget&, const sf::FloatRect&,
		const std::vector<std::string>& labels, size_t selected);
	// Index of the segment under the point, or -1.
	int HitSegmented(const sf::FloatRect&, size_t count, float px, float py);

	// ------------------------------------------------------------- progress
	void DrawProgress(sf::RenderTarget&, const sf::FloatRect&, float fraction);

	// --------------------------------------------------------------- navbar
	// Back sits at a fixed left position in systemBlue. Position is what makes
	// it findable now, which is why it no longer needs a colour of its own.
	void DrawNavBar(sf::RenderTarget&, float width, const std::string& title,
		const std::string& backLabel, bool backPressed);
	sf::FloatRect NavBackRect();

	// ----------------------------------------------------------------- misc
	void DrawSeparator(sf::RenderTarget&, float x, float y, float w);
	void DrawPill(sf::RenderTarget&, const sf::FloatRect&,
		const std::string& label, bool on);
}
