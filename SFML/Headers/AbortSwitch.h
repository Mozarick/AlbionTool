#pragma once
#include <string>

using namespace std;

// The emergency stop.
//
// A scan or a buying run blocks the event thread for its whole duration (see
// the note in Game::LaunchScan on why it is deliberately not on a worker
// thread), and the window minimises itself while it runs. Both of those mean
// SFML's own Event::KeyPressed can never arrive during a run: nothing is
// pumping the message queue, and the keystrokes are going to the game anyway.
// So the stop key has to be read from the OS with GetAsyncKeyState, polled
// from inside the loops themselves.
//
// The consequence to remember: the game receives the keystroke too. Whatever
// the hotkey is, it must be harmless inside Albion.
namespace Abort {

	// Noticing and acting are two different things.
	//
	// Requested() LATCHES: the first poll that sees the combo down sets a flag
	// and every later call returns true without polling again. That is what
	// lets the poll live in places where stopping would be unsafe - the middle
	// of a purchase, waiting for the server to answer a Buy click - while the
	// actual break happens at a boundary where nothing is half-finished.
	//
	// Without the latch the key would have to be held down until a checkpoint
	// happened to run, and the long waits are exactly where a person presses it.
	bool Requested();
	// Request the same safe stop when the control window is closed.
	void Request();

	// The latch, read without polling. For code that must finish what it
	// started and only then check whether to stop.
	bool Latched();

	// Clears the latch and starts watching. Call at the top of every run.
	//
	// If the combo is already held down at this moment, watching does not begin
	// until it has been released once - otherwise stopping one run would
	// instantly stop the next one started while the keys were still down.
	void Arm();

	// Stops watching and clears the latch. Nothing outside a run can trip it.
	void Disarm();

	// "Ctrl+Shift+Q", for printing. Comes from Config.AbortHotkeyName.
	const string& HotkeyName();

	// Arm on the way in, Disarm on the way out, whichever way the scope exits.
	// Modelled on WindowHidden in Game.cpp, and used in the same places.
	struct Guard {
		Guard() { Arm(); }
		~Guard() { Disarm(); }
		Guard(const Guard&) = delete;
		Guard& operator= (const Guard&) = delete;
	};
}
