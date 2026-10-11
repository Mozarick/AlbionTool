#pragma once
#include "MoveToPosition.h"

// The marketplace panel's two dropdowns, measured by hand.
//
// Scanning, buying and selling all drive the same panel, so the coordinates
// live here once instead of being copied into three layout structs that would
// then drift apart.
//
// **These are never located by OCR.** Reading a dropdown row's text and
// clicking where the text was found is what the tier selection used to do, and
// it was the single least reliable thing in the bot: tesseract returns "Tier S"
// for "Tier 5" often enough that whole tiers got skipped silently. The rows do
// not move - the dropdown always opens in the same place - so a measured
// rectangle is both simpler and correct every time. OCR is now only for the
// category menus, whose entries genuinely do move depending on what is listed.


// Item quality filter: the button that opens it, and the five rows beneath.
struct QualityLayout {
	ClickRect button = { 1192, 266, 1300, 278 };

	// index 0..4 = Normal, Good, Outstanding, Excellent, Masterpiece,
	// matching ItemQuality's 1..5 offset by one
	ClickRect item[5] = {
		{ 1192, 319, 1308, 331 },
		{ 1192, 345, 1305, 356 },
		{ 1191, 374, 1303, 388 },
		{ 1197, 402, 1302, 411 },
		{ 1197, 428, 1298, 438 },
	};

	// NULL when `quality` is outside 1..5, so a caller cannot turn a bad value
	// into a click somewhere unintended.
	const ClickRect* Row(int quality) const {
		if (quality < 1 || quality > 5) {
			return NULL;
		}
		return &item[quality - 1];
	}
};


// Tier filter: the button that opens it, and one row per tier.
//
// Only tiers 4 to 8 have been measured, because those are the ones worth
// flipping. Row() returns NULL for anything else rather than guessing: the rows
// are evenly spaced, so extrapolating upwards would look right and click the
// wrong row, which is exactly the silent-wrong-data failure this whole change
// set out to remove. If T1-T3 are ever wanted, measure them with FindCoords.ps1
// and add them here.
struct TierLayout {
	// Only 7px tall. ClickInside picks a random point inside a rectangle and
	// must NOT add extra aim slop on top of that, or clicks land outside this
	// and the dropdown never opens.
	ClickRect button = { 903, 268, 1015, 275 };

	static const int firstTier = 4;
	static const int lastTier = 8;

	ClickRect row[5] = {
		{ 904, 401, 1019, 411 },  // T4
		{ 904, 428, 1013, 441 },  // T5
		{ 902, 455, 1020, 467 },  // T6
		{ 901, 482, 1015, 493 },  // T7
		{ 901, 508, 1016, 521 },  // T8
	};

	const ClickRect* Row(int tier) const {
		if (tier < firstTier || tier > lastTier) {
			return NULL;
		}
		return &row[tier - firstTier];
	}
};


// Enchantment filter: the button that opens it, and one row per level.
//
// The game stops handing out pages after about twenty tab switches, but one
// tier of a popular item holds far more than that when every enchantment
// level is listed together - a T7 sword scan ran out of tabs somewhere around
// 7.2 and dropped the rest with nothing in the log to say so. Filtering to one
// enchantment at a time splits that single over-long listing into five that
// each fit under the cap.
//
// Only for the city market: the black market is never filtered by enchantment.
// Tiers 1-3 have none, so this is only ever opened for T4-T8 - which is every
// tier TierLayout can select anyway.
struct EnchantmentLayout {
	// Spans the whole word rather than a point, so the random pick inside it
	// lands somewhere different on each scan instead of hammering one pixel.
	ClickRect button = { 1050, 268, 1152, 276 };

	static const int firstLevel = 0;
	static const int lastLevel = 4;

	// "All" sits above these and is deliberately not listed: picking it is
	// what produces the over-long listing this struct exists to avoid.
	ClickRect row[5] = {
		{ 1052, 320, 1162, 331 },  // Enchantment 0
		{ 1048, 349, 1161, 356 },  // Enchantment 1
		{ 1048, 376, 1157, 382 },  // Enchantment 2
		{ 1048, 405, 1154, 412 },  // Enchantment 3
		{ 1050, 428, 1150, 442 },  // Enchantment 4
	};

	const ClickRect* Row(int level) const {
		if (level < firstLevel || level > lastLevel) {
			return NULL;
		}
		return &row[level - firstLevel];
	}
};
