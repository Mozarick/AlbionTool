#include "../Headers/AbortSwitch.h"

#include <Windows.h>

#include "../Headers/Config.h"


namespace {

	bool armed = false;
	bool latched = false;

	// Set at Arm() when the combo is still held from a previous stop. Watching
	// resumes only after a poll sees it released.
	bool waitingForRelease = false;

	// Currently held, as opposed to "pressed at some point since the last
	// call". Only the high bit is reliable here: the low bit is consumed by
	// whoever reads it first, and the game is reading the same keyboard.
	bool Down(int vk) {
		return (GetAsyncKeyState(vk) & 0x8000) != 0;
	}

	bool ComboDown() {
		if (Config.AbortCtrl && !Down(VK_CONTROL)) {
			return false;
		}
		if (Config.AbortShift && !Down(VK_SHIFT)) {
			return false;
		}
		if (Config.AbortAlt && !Down(VK_MENU)) {
			return false;
		}
		return Config.AbortVk != 0 && Down(Config.AbortVk);
	}
}


namespace Abort {
	void Request() { if (armed) latched = true; }

	void Arm() {
		latched = false;
		armed = true;

		// Holding the keys down from the run that was just stopped must not
		// stop the next one before it has done anything.
		waitingForRelease = ComboDown();
	}

	void Disarm() {
		armed = false;
		latched = false;
		waitingForRelease = false;
	}

	bool Latched() {
		return latched;
	}

	bool Requested() {
		if (latched) {
			return true;
		}
		if (!armed) {
			return false;
		}

		if (waitingForRelease) {
			if (ComboDown()) {
				return false;
			}
			waitingForRelease = false;
		}

		if (ComboDown()) {
			latched = true;
		}
		return latched;
	}

	const string& HotkeyName() {
		return Config.AbortHotkeyName;
	}
}
