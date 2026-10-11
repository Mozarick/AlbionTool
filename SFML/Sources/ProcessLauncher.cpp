#include "../Headers/ProcessLauncher.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <shellapi.h>

#include <iostream>
#include <filesystem>

using namespace std;

static bool IsProcessRunning(const wchar_t* exeName) {
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		return false;
	}

	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);

	bool found = false;
	if (Process32FirstW(snap, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, exeName) == 0) {
				found = true;
				break;
			}
		} while (Process32NextW(snap, &entry));
	}

	CloseHandle(snap);
	return found;
}

bool EnsureFeedClientRunning() {
	// Leave a running feed alone.
	//
	// This check was briefly made unconditional - always kill and relaunch -
	// back when the machine's installed Albion Data Client had a Scheduled
	// Task starting its own copy at logon WITHOUT -p http://127.0.0.1:5000.
	// That copy fed the public servers instead of this bot, and since an
	// elevated process will not reveal its command line to an unelevated one,
	// there was no way to tell the two apart; taking ownership every launch
	// was the only reliable answer.
	//
	// Two things changed since. That task is disabled, and AlbionFeed takes no
	// arguments that could be wrong in the first place - if it is running, it
	// is posting to 127.0.0.1:5000, because that is the only thing it does.
	// Relaunching regardless just meant the capture died and restarted on every
	// single launch of this tool - dozens of times an hour during development,
	// each one throwing away a live capture and costing another UAC prompt.
	//
	// If a wrong client ever does appear again, two things still catch it:
	// LaunchScan restarts capture when a scan is about to run with no data ever
	// received, and the Capture button forces it by hand. run-feed.bat also
	// kills albiondata-client every time it runs.
	if (IsProcessRunning(L"AlbionFeed.exe")) {
		cout << "AlbionFeed already running, leaving it alone\n"
			<< "  (press Capture on the menu to force a fresh one)\n";
		return true;
	}

	return RestartFeedClient();
}

bool RestartFeedClient() {
	cout << "starting AlbionFeed (approve the UAC prompt)\n";

	SHELLEXECUTEINFOW sei{};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = L"runas";
	const auto packaged = std::filesystem::current_path().parent_path() / L"capture" / L"run-feed.bat";
	const auto script = std::filesystem::exists(packaged) ? packaged :
		std::filesystem::path(L"D:\\AlbionTool\\bin\\run-feed.bat");
	const std::wstring scriptName = script.wstring();
	const std::wstring scriptDirectory = script.parent_path().wstring();
	sei.lpFile = scriptName.c_str();
	sei.lpDirectory = scriptDirectory.c_str();
	sei.nShow = SW_HIDE;

	if (!ShellExecuteExW(&sei)) {
		DWORD err = GetLastError();
		if (err == ERROR_CANCELLED) {
			cout << "  UAC prompt was declined - no market data until it is started\n";
		}
		else {
			cout << "  could not start AlbionFeed, error " << err << '\n';
		}
		return false;
	}

	if (sei.hProcess) {
		CloseHandle(sei.hProcess);
	}

	cout << "  the script kills any old capture client first, then starts\n"
		<< "  AlbionFeed pointed at 127.0.0.1:5000. Give it a few seconds.\n"
		<< "  Capture log: " << (script.parent_path() / "AlbionFeed" / "AlbionFeed.log").string() << '\n';
	return true;
}

bool IsAlbionOnlineRunning() {
	return IsProcessRunning(L"Albion-Online.exe") ||
		IsProcessRunning(L"AlbionLauncher.exe");
}

void EnsureAlbionLauncherRunning() {
	if (IsAlbionOnlineRunning()) {
		cout << "Albion Online already running\n";
		return;
	}

	const wchar_t* launcherPath = L"D:\\albion\\launcher\\AlbionLauncher.exe";
	if (GetFileAttributesW(launcherPath) == INVALID_FILE_ATTRIBUTES) {
		cout << "Albion launcher not found at D:\\albion\\launcher\\AlbionLauncher.exe - start the game yourself\n";
		return;
	}

	cout << "starting the Albion launcher\n";
	ShellExecuteW(NULL, L"open", launcherPath, NULL,
		L"D:\\albion\\launcher", SW_SHOWNORMAL);
}
