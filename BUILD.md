# Build setup

For this Windows checkout with the existing local dependencies, run
`pwsh -NoProfile -File tools\Build-Tool.ps1`. This builds Release x64 and
runs the planner, shopping-list, clipboard, UI, and large-market checks.
Use `-TestsOnly` after a successful app build to rerun those checks.
The executable is `x64\Release\SFML.exe`.

Version 0.6.0 removes Craft Profit. The manual Shopping List displays
`Profit estimated` and adds the displayed item profit on every left-click
copy, including repeated copies of one item. Reset clears the running total;
reopening Shop preserves it, while a new plan or Clear Everything clears it.
The list and copy history now survive restart. Undo, search, Pending filtering,
CSV export and a receiver dashboard are documented in `STUDIO-GUIDE.vi.md`.
Run `tools/Package-Studio.ps1` for the local release, then
`tools/Smoke-Studio.ps1` to verify its runtime, real HTTP and file hashes.

Category scans now file feed batches and service the window during scanner
waits. Name scans service the window while retaining their existing per-item
filter before storing results. Large shopping lists render only visible rows.
The planner indexes alternative quality bands by item and enchantment rather
than searching all market groups for each lot.

The bot now links tesseract as a library instead of shelling out to
`tesseract.exe` for every price it reads, so the build needs the tesseract
development files (headers + import libraries), not just the installer.

## 1. OpenCV

Unchanged from before. Install OpenCV 4.5.5 and set the `OPENCV_DIR`
environment variable to its build folder, for example:

```
setx OPENCV_DIR C:\opencv\build
```

The project reads `$(OPENCV_DIR)\include` and `$(OPENCV_DIR)\x64\vc14\lib`.

## 2. SFML

Nothing to do. SFML 2.5.1 ships inside this repository under `lib/SFML-2.5.1`
and the project now points at it with a relative path. The old instructions
about editing the SFML path no longer apply.

## 3. Tesseract (this is the new part)

The UB-Mannheim installer only gives you `tesseract.exe` and the language
data. To link against tesseract you also need its headers and `.lib` files.
Get them with vcpkg:

```
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install tesseract:x64-windows
```

Then set `VCPKG_ROOT` to wherever you cloned it, the project reads that
property the same way it reads `OPENCV_DIR`:

```
setx VCPKG_ROOT C:\vcpkg
```

The project's include/lib paths already point at
`$(VCPKG_ROOT)\installed\x64-windows\...` and its Release/Debug configs link
`tesseract55.lib` / `tesseract55d.lib`. This was verified end to end: cloning
vcpkg, building tesseract from source (~35 min the first time, mostly
leptonica's own dependency chain — libpng, libjpeg-turbo, tiff, openssl,
libcurl, libarchive), and rebuilding the solution produced a clean
`SFML.exe` with zero errors and only pre-existing warnings unrelated to the
tesseract change.

`vcpkg integrate install` also exists and is worth running, but calling
MSBuild directly (as opposed to building from the Visual Studio IDE) did not
pick up its machine-wide hook reliably in testing, which is why the project
now points at `$(VCPKG_ROOT)` explicitly instead of depending on it.

### Runtime DLLs

`tesseract55.dll` pulls in `leptonica-*.dll`, `libpng16.dll`, `zlib`,
`libwebp`, `tiff`, `jpeg`, `openjp2`, `gif`, `archive.dll`, `libcurl.dll`, and
a few more from `$(VCPKG_ROOT)\installed\x64-windows\bin`. Visual Studio's
default debugger working directory is the project folder (`SFML\`), and
Windows' DLL search checks the current directory, so running via **F5** or
**Ctrl+F5** from the IDE works once those DLLs — and `opencv_world455.dll`
from `$(OPENCV_DIR)\x64\vc14\bin` — are reachable that way. The simplest fix
if either is missing at startup is adding both bin folders to `PATH`:

```
setx PATH "%PATH%;%VCPKG_ROOT%\installed\x64-windows\bin;%OPENCV_DIR%\x64\vc14\bin"
```

(open a new terminal / restart Visual Studio afterwards for `setx` to take
effect). Double-clicking the built `.exe` directly from `x64\Release\` only
works once PATH is set this way, since a plain double-click's working
directory is the `.exe`'s own folder, not the project folder.

## 4. Language data

Tesseract needs `eng.traineddata`. Point `Config.txt` at the folder containing
it:

```
tessdata_path: C:\Program Files\Tesseract-OCR\tessdata
```

Leave the value empty to fall back to the `TESSDATA_PREFIX` environment
variable instead. If the path is wrong the bot prints a clear error on
startup and every price read returns -1.

## 5. Build

Open `SFML.sln`, select **Release / x64**, build. The project is set to C++17.

`Albion.vcxproj` in the `SFML` folder is not part of the solution and points at
file paths that no longer exist. Ignore it.

## Tuning

`Config.txt` now holds the timing knobs. The bot waits for the price panel to
actually redraw instead of sleeping a fixed 500 ms, but it still needs the
game to register its clicks:

| key | default | meaning |
| --- | --- | --- |
| `click_hold_ms` | 25 | how long the mouse button stays down |
| `click_delay_ms` | 50 | pause after each click |
| `poll_interval_ms` | 15 | how often the price panel is re-checked |
| `wait_timeout_ms` | 1200 | give up waiting for a redraw after this |
| `menu_open_delay_ms` | 350 | **ceiling** on the wait for a dropdown to draw |
| `page_timeout_ms` | 1200 | **ceiling** on the wait for a page of market data |

The last two are ceilings rather than sleeps: menus are polled until they stop
repainting, and the page wait is derived from the round trip actually measured
during the run. See HANDOVER.md §6 for why, and for what to raise if you move
to a more distant server.

If the bot starts misclicking or picking the wrong tier, raise
`click_hold_ms` and `click_delay_ms` first — those are the values that were
effectively 120 ms and 200 ms in the old code.
