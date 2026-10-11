@echo off
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"

set VCPKGI=D:\vcpkg\installed\x64-windows

cl /nologo /EHsc /std:c++17 /MD /O2 ^
   /I "%VCPKGI%\include" ^
   FeedTest.cpp ..\SFML\Sources\MarketFeed.cpp ^
   /Fe:FeedTest.exe ^
   /link ws2_32.lib

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK
