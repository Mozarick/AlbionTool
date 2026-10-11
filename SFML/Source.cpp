#include <Windows.h>

#include <SFML/Graphics.hpp>
#include <iostream>
#include <thread>
#include <filesystem>

#include "Headers/Game.h"
#include "Headers/LogSink.h"


using namespace sf;
using namespace std;


// сделать налог на покупку/продажу


// Windows lies to DPI-unaware processes: on a 125% display it presents the
// real 1920x1080 desktop as a virtual 1536x864 one. SetCursorPos coordinates
// then get scaled up (so every click lands ~25% off) and BitBlt hands back a
// downscaled, blurry copy of the screen (which wrecks the OCR on text that is
// only ~18px tall to begin with). Ask for real pixels before any window or
// device context exists.
static void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");

    if (user32 != NULL) {
        typedef BOOL(WINAPI* SetContextFn)(DPI_AWARENESS_CONTEXT);
        SetContextFn setContext = (SetContextFn)GetProcAddress(
            user32, "SetProcessDpiAwarenessContext");

        if (setContext != NULL &&
            setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }

    // Windows 8.1 and older
    SetProcessDPIAware();
}

// Config.txt, categories.tsv, items.tsv, fonts/, scan_profile.tsv,
// holdings.tsv - everything is loaded by a relative path assuming the
// working directory is the SFML/ folder next to the exe's own x64/<config>/
// folder, not wherever the exe happened to be launched from. START.bat used
// to paper over this with /D; now that the app is meant to be launched
// directly (double-click, a shortcut with no special "Start in", from the
// debugger), it has to find that folder itself.
static void SetWorkingDirectoryToBotFolder() {
    wchar_t path[32768];
    DWORD len = GetModuleFileNameW(nullptr, path, 32768);
    if (len == 0 || len >= 32768) return;
    const auto exeDir = std::filesystem::path(std::wstring(path, len)).parent_path();
    const std::filesystem::path candidates[] = {exeDir / L"data", exeDir,
        exeDir.parent_path().parent_path() / L"SFML"};
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate / L"items.tsv") &&
            std::filesystem::exists(candidate / L"fonts")) {
            SetCurrentDirectoryW(candidate.c_str()); return;
        }
    }
}

// Fatal diagnostics stay beside the local app data.
static void CrashLog(const string& msg) {
    ofstream f("crash.log", ios::app);
    if (f) {
        f << msg << "\n";
    }
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    ostringstream oss;
    oss << "Unhandled SEH exception, code 0x" << hex
        << info->ExceptionRecord->ExceptionCode
        << " at address 0x" << info->ExceptionRecord->ExceptionAddress;
    CrashLog(oss.str());
    return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char** argv)
{
    SetUnhandledExceptionFilter(CrashFilter);


    SetWorkingDirectoryToBotFolder();
    bool offline = false;
    string preview;
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        if (arg == "--offline") offline = true;
        else if (arg == "--data-dir" && i + 1 < argc) SetCurrentDirectoryA(argv[++i]);
        else if (arg == "--capture-preview" && i + 1 < argc) { preview = argv[++i]; offline = true; }
    }
    EnableDpiAwareness();


    // The window is now the SubSystem=Windows kind - there is no console to
    // print to. Redirect cout onto the in-window log panel (and bot.log)
    // before anything else has a chance to write to it.
    cout.rdbuf(&GlobalLog);


    try {
        Studio::SingleInstance instance(preview.empty() ? L"Local\\AlbionMarketStudio" : nullptr);
        if (instance.AlreadyRunning()) {
            if (const HWND existing = FindWindowW(nullptr, L"Market Studio")) {
                ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
            }
            return 0;
        }
        Game game(offline);

        if (preview.empty()) game.run();
        else game.CapturePreview(preview, 6);

    }
    catch (const std::exception& e) {
        CrashLog(string("std::exception: ") + e.what());
        MessageBoxW(nullptr, L"Market Studio could not start. Details were saved to crash.log in the app data folder.", L"Market Studio", MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (...) {
        CrashLog("unknown C++ exception");
        return 1;
    }




    return 0;
}



