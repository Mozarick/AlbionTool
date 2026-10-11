#pragma once
#include <Windows.h>
#include <string>
#include <map>
#include <set>
#include <vector>

#include "Config.h"

using namespace std;

// Mouse and Keyboard Events

// Moves the cursor to a point given in 1920x1080 coordinates and clicks it.
// Everything below is written against that reference resolution and scaled
// through Config.ScreenScale here, in one place. The move itself travels a
// slightly bowed, eased path from wherever the cursor currently is instead
// of teleporting, tuned by the move_* keys in Config.txt.
void MoveAndClick(int base_x, int base_y);

// A clickable area in 1920x1080 coordinates, given as its top-left and
// bottom-right corners.
struct ClickRect {
	int x1;
	int y1;
	int x2;
	int y2;
};

// Clicks a point picked at random inside the rectangle rather than always the
// exact same pixel. Real clicks scatter across a button; hitting one pixel
// every time is the sort of thing that stands out.
void MoveAndClickRect(const ClickRect& area);

// Same, but the rectangle is already in real screen pixels and must not be
// scaled again. OCR hits arrive this way: they are measured on a captured
// frame, which is already at the screen's own resolution.
void MoveAndClickScreenRect(const ClickRect& area);

// Hovers a random point inside the rectangle without pressing anything, and
// returns the screen y it settled on.
//
// For cascading menus, which open on hover: the parent row is hovered, then the
// cursor slides sideways into the child column HOLDING THE SAME ROW - which is
// why the y comes back rather than being recomputed by the caller. Recomputing
// it from the rectangle's centre is what made this the one movement in the
// program that always landed on an exact pixel.
int MoveToRect(const ClickRect& area, int holdRowY = -1);

// Moves the cursor without pressing anything, in real screen pixels.
// Cascading menus open on hover, so entering a submenu means hovering the
// parent row and sliding sideways into the child column rather than clicking.
void MoveToScreenPoint(int x, int y);

void MouseLeftClick();

// Types the whole string in a single SendInput call.
void TypeString(const string& text);

// Ctrl+A then Delete, for text fields that already hold a value. Typing into
// the quantity box without clearing it first turns a default "1" and an
// intended "3" into "13", which buys thirteen of something.
void SelectAllAndDelete();

// Clicks the market's own clear-search button, next to the search box - more
// reliable than selecting and deleting the text by hand.
void ClearItemName();
