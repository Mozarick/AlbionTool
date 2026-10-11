#pragma once
#include <SFML/Graphics.hpp>

// Shared desktop palette. Scan states retain their own semantic colors.
namespace Theme {
	// --- ground and surfaces ---------------------------------------------
	// systemBackground / secondarySystemGroupedBackground / tertiary.
	const sf::Color Background(11, 14, 20);         // #000000
	const sf::Color Panel(21, 27, 37);           // #1C1C1E  grouped list, card
	const sf::Color PanelRaised(31, 40, 54);     // #2C2C2E  pressed row, track

	// --- lines ------------------------------------------------------------
	const sf::Color Border(40, 50, 65);          // #38383A  hairline separator
	const sf::Color BorderStrong(60, 75, 94);    // #48484A  outlined control

	// --- text -------------------------------------------------------------
	// label / secondaryLabel / tertiaryLabel. The last two keep their real iOS
	// alpha rather than a flattened approximation: SFML blends alpha when it
	// draws, so there is no reason to lose it, and it stays correct over both
	// Background and Panel.
	const sf::Color TextPrimary(255, 255, 255);          // #FFFFFF
	const sf::Color TextSecondary(191, 203, 220);   // #EBEBF5 @60%
	const sf::Color TextMuted(132, 148, 170);        // #EBEBF5 @30%

	// --- tint -------------------------------------------------------------
	const sf::Color Accent(79, 152, 255);        // #0A84FF systemBlue
	const sf::Color AccentText(79, 152, 255);    // one blue, as iOS has

	// Back used to be orange so it could not be confused with the accent or
	// with any scan state. It is blue now because it lives in a fixed
	// nav-bar position, and position disambiguates it better than colour
	// ever did - which frees orange to belong to Running alone. Before this,
	// Back and Running were both orange and both on screen during a scan.
	const sf::Color Back(79, 152, 255);

	// --- scan states ------------------------------------------------------
	const sf::Color Pending(99, 99, 102);        // #636366 systemGray2
	const sf::Color Running(255, 159, 10);       // #FF9F0A systemOrange
	const sf::Color Ok(62, 221, 161);             // #30D158 systemGreen
	const sf::Color Failed(255, 69, 58);         // #FF453A systemRed
	const sf::Color Destructive(255, 69, 58);    // Clear / Exit

	// --- tints ------------------------------------------------------------
	// Each state colour at 18% over Panel, worked out once here instead of
	// composited at draw time. Solid on purpose: the same values are in the
	// Figma file as solid variables, because Figma instances silently drop a
	// paint's opacity and SFML has no compositing model to lose it in either.
	// One number, one meaning, both sides.
	const sf::Color RunningTint(69, 52, 26);     // #45341A
	const sf::Color OkTint(32, 61, 40);          // #203D28
	const sf::Color FailedTint(69, 35, 35);      // #452323
	const sf::Color AccentTint(25, 47, 70);      // #192F46
	const sf::Color DestructiveTint(69, 35, 35); // #452323
}
