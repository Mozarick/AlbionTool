@echo off
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"

set VCPKGI=D:\vcpkg\installed\x64-windows

rem _HAS_STD_BYTE=0: CityBuyer.h pulls in Windows.h through
rem MoveToPosition.h while std is a using-directive, and ::byte from
rem rpcndr.h then collides with std::byte. The GUI project never hits
rem this because SFML gets included first.
cl /nologo /EHsc /std:c++17 /MD /O2 /D_HAS_STD_BYTE=0 ^
   /I "%VCPKGI%\include" ^
   ShopListTest.cpp ^
   ..\SFML\Sources\ShopList.cpp ^
   ..\SFML\Sources\AbortSwitch.cpp ^
   ..\SFML\Sources\CityBuyer.cpp ^
   ..\SFML\Sources\BlackMarket.cpp ^
   ..\SFML\Sources\Holdings.cpp ^
   ..\SFML\Sources\MarketFeed.cpp ^
   ..\SFML\Sources\MoveToPosition.cpp ^
   ..\SFML\Sources\Config.cpp ^
   /Fe:ShopListTest.exe ^
   /link ws2_32.lib user32.lib

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK
