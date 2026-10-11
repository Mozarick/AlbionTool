#include "../Headers/MoveToPosition.h"

#include <cmath>
#include <iostream>
#include <random>


// Mouse and Keyboard Events

// The button is held for ClickHoldMs because a press that goes down and up in
// the same tick tends to be dropped by the game. Both delays come from
// Config.txt: raise them if clicks start getting missed.
void MouseLeftClick() {
	INPUT inputs[2] = {};

	inputs[0].type = INPUT_MOUSE;
	inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
	inputs[1].type = INPUT_MOUSE;
	inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;

	UINT uSent = SendInput(1, &inputs[0], sizeof(INPUT));
	Sleep(Config.ClickHoldMs);
	uSent += SendInput(1, &inputs[1], sizeof(INPUT));

	if (uSent != 2) {
		// cout, not printf: printf goes to the C stdout, which is discarded in
		// a GUI binary. A failed SendInput means the click never reached the
		// game at all - exactly the thing worth seeing in the log.
		cout << "SendInput failed: 0x" << hex
			<< HRESULT_FROM_WIN32(GetLastError()) << dec << '\n';
	}

	Sleep(Config.ClickDelayMs);
}

static mt19937& RngEngine() {
	static mt19937 rng(random_device{}());
	return rng;
}

static double Uniform(double lo, double hi) {
	return uniform_real_distribution<double>(lo, hi)(RngEngine());
}

// Ease-in-ease-out: slow to start, fastest in the middle, slow to land.
// A real hand accelerates into a move and brakes before the target instead
// of travelling at a constant speed the whole way.
static double SmoothStep(double t) {
	return t * t * (3.0 - 2.0 * t);
}

// Moves the cursor to (target_x, target_y) along a slightly bowed path
// instead of jumping there in one SetCursorPos call. The path is a cubic
// Bezier bowed sideways by a random amount (so repeated moves don't retrace
// the exact same line), walked in short steps with a small perpendicular
// wobble that fades out near the target so the final position is still
// exact. Config.MoveJitterPx = 0 disables the bow/wobble and Config.MoveMinMs
// disables the pacing, without changing where the cursor ends up.
static void HumanMove(int target_x, int target_y) {
	POINT start;
	GetCursorPos(&start);

	double dx = target_x - start.x;
	double dy = target_y - start.y;
	double distance = sqrt(dx * dx + dy * dy);

	if (distance < 1.0) {
		SetCursorPos(target_x, target_y);
		return;
	}

	// unit vector perpendicular to the travel direction, used to bow the
	// curve and to place the wobble sideways rather than along the path
	double nx = -dy / distance;
	double ny = dx / distance;

	double jitter = double(max(0, Config.MoveJitterPx));
	double bow = jitter > 0 ? Uniform(-1.0, 1.0) * min(distance * 0.15, jitter * 6.0) : 0.0;

	// two control points a third and two-thirds of the way along, each
	// pushed a little further off the line than the last for a natural,
	// slightly asymmetric curve rather than a perfect arc
	double c1x = start.x + dx * 0.33 + nx * bow * 0.6;
	double c1y = start.y + dy * 0.33 + ny * bow * 0.6;
	double c2x = start.x + dx * 0.66 + nx * bow;
	double c2y = start.y + dy * 0.66 + ny * bow;

	int duration = int(distance / 100.0 * max(1, Config.MoveMsPer100px));
	duration = max(Config.MoveMinMs, min(Config.MoveMaxMs, duration));

	int steps = max(6, min(40, int(distance / 12.0)));
	int stepDelay = max(1, duration / steps);

	for (int i = 1; i <= steps; ++i) {
		double t = double(i) / steps;
		double te = SmoothStep(t);
		double it = 1.0 - te;

		// cubic Bezier: P0(1-t)^3 + 3*C1(1-t)^2*t + 3*C2(1-t)*t^2 + P3*t^3
		double x = it * it * it * start.x
			+ 3 * it * it * te * c1x
			+ 3 * it * te * te * c2x
			+ te * te * te * target_x;
		double y = it * it * it * start.y
			+ 3 * it * it * te * c1y
			+ 3 * it * te * te * c2y
			+ te * te * te * target_y;

		// wobble fades out at both ends of the path so the move starts and
		// lands exactly on target instead of overshooting it
		double wobble = jitter * sin(3.14159265 * t) * Uniform(-1.0, 1.0);
		x += nx * wobble;
		y += ny * wobble;

		SetCursorPos(int(lround(x)), int(lround(y)));
		Sleep(stepDelay);
	}

	SetCursorPos(target_x, target_y);
}

void MoveAndClick(int base_x, int base_y) {
	int target_x = int(Config.ScreenScale.first * base_x);
	int target_y = int(Config.ScreenScale.second * base_y);

	// a human never clicks the exact same pixel twice; nudge the aim point
	// itself before travelling to it, rather than arriving precisely and
	// only wobbling along the way
	int slop = max(0, Config.MoveTargetSlopPx);
	if (slop > 0) {
		target_x += int(lround(Uniform(-slop, slop)));
		target_y += int(lround(Uniform(-slop, slop)));
	}

	HumanMove(target_x, target_y);
	MouseLeftClick();
}

// Picks a point inside the rectangle, optionally scaling it from the 1920x1080
// reference frame first.
//
// Split out from the click so hovering can choose its point the same way.
// Cascading menus are entered by hovering a row rather than clicking it, and
// that hover had no business being the one movement in the program that always
// landed on an exact pixel.
static void PointInside(const ClickRect& area, bool scaleFromBase,
	int& pick_x, int& pick_y) {

	int lo_x = min(area.x1, area.x2);
	int hi_x = max(area.x1, area.x2);
	int lo_y = min(area.y1, area.y2);
	int hi_y = max(area.y1, area.y2);

	// stay a pixel off the border so a rounding slip cannot land outside
	if (hi_x - lo_x > 2) {
		++lo_x;
		--hi_x;
	}
	if (hi_y - lo_y > 2) {
		++lo_y;
		--hi_y;
	}

	pick_x = int(lround(Uniform(lo_x, hi_x)));
	pick_y = int(lround(Uniform(lo_y, hi_y)));

	if (scaleFromBase) {
		pick_x = int(Config.ScreenScale.first * pick_x);
		pick_y = int(Config.ScreenScale.second * pick_y);
	}
}

static void ClickInside(const ClickRect& area, bool scaleFromBase) {
	int pick_x = 0;
	int pick_y = 0;
	PointInside(area, scaleFromBase, pick_x, pick_y);

	// No extra aim error here on purpose. Picking a random point inside the
	// rectangle already gives the movement its human variation, and adding
	// MoveTargetSlopPx on top pushes the click outside thin targets - the
	// tier button is only 7px tall, so a 3px slop missed it regularly and the
	// dropdown never opened.
	HumanMove(pick_x, pick_y);
	MouseLeftClick();
}

void MoveToScreenPoint(int x, int y) {
	int slop = max(0, Config.MoveTargetSlopPx);
	if (slop > 0) {
		x += int(lround(Uniform(-slop, slop)));
		y += int(lround(Uniform(-slop, slop)));
	}
	HumanMove(x, y);
}

void SelectAllAndDelete() {
	INPUT inputs[6] = {};

	// ctrl down, a down, a up, ctrl up
	inputs[0].type = INPUT_KEYBOARD;
	inputs[0].ki.wVk = VK_CONTROL;

	inputs[1].type = INPUT_KEYBOARD;
	inputs[1].ki.wVk = 'A';

	inputs[2] = inputs[1];
	inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

	inputs[3] = inputs[0];
	inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

	// then wipe the selection
	inputs[4].type = INPUT_KEYBOARD;
	inputs[4].ki.wVk = VK_DELETE;

	inputs[5] = inputs[4];
	inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;

	UINT uSent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
	if (uSent != ARRAYSIZE(inputs)) {
		// cout, not printf: printf goes to the C stdout, which is discarded in
		// a GUI binary. A failed SendInput means the click never reached the
		// game at all - exactly the thing worth seeing in the log.
		cout << "SendInput failed: 0x" << hex
			<< HRESULT_FROM_WIN32(GetLastError()) << dec << '\n';
	}

	Sleep(Config.ClickHoldMs);
}

void MoveAndClickRect(const ClickRect& area) {
	ClickInside(area, true);
}

void MoveAndClickScreenRect(const ClickRect& area) {
	ClickInside(area, false);
}

int MoveToRect(const ClickRect& area, int holdRowY) {
	int pick_x = 0;
	int pick_y = 0;
	PointInside(area, true, pick_x, pick_y);

	// Sliding along a row rather than entering one: keep the y that was
	// already hovered, vary only the x.
	if (holdRowY >= 0) {
		pick_y = holdRowY;
	}

	// Same reasoning as ClickInside: the random point inside the rectangle is
	// the variation, so no MoveTargetSlopPx on top of it.
	HumanMove(pick_x, pick_y);
	return pick_y;
}

// One SendInput call per character, not one call for the whole string.
//
// The whole-string version looked identical from inside the game to a paste:
// every character landed in the same input batch, so the search box ended up
// holding the right text but its autocomplete never opened, because nothing
// fired the per-keystroke event that triggers it - the item tab just would
// not show up. Splitting it back into one call per character fixes that.
//
// This used to be the slow way (one call per character *plus a full
// click_delay_ms sleep after each one*, close to two seconds for a name).
// The delay between characters is Config.TypeCharDelayMs instead, which is
// meant to be small - it only has to be enough for the game to register each
// keystroke as its own event, not enough to look unhurried.
void TypeString(const string& text) {
	if (text.empty()) {
		return;
	}

	const int charDelay = max(0, Config.TypeCharDelayMs);
	INPUT down{};
	INPUT up{};

	down.type = INPUT_KEYBOARD;
	down.ki.dwFlags = KEYEVENTF_UNICODE;
	up.type = INPUT_KEYBOARD;
	up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

	for (size_t i = 0; i < text.size(); ++i) {
		down.ki.wScan = text[i];
		up.ki.wScan = text[i];

		UINT uSent = SendInput(1, &down, sizeof(INPUT));
		uSent += SendInput(1, &up, sizeof(INPUT));

		if (uSent != 2) {
			// cout, not printf: printf goes to the C stdout, which is
			// discarded in a GUI binary. A failed SendInput means the
			// keystroke never reached the game at all - exactly the thing
			// worth seeing in the log.
			cout << "SendInput failed: 0x" << hex
				<< HRESULT_FROM_WIN32(GetLastError()) << dec << '\n';
		}

		if (charDelay > 0 && i + 1 < text.size()) {
			Sleep(charDelay);
		}
	}

	Sleep(Config.ClickDelayMs);
}

void ClearItemName() {
	MoveAndClick(722, 270);
}
