@echo off
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"

set VCPKGI=D:\vcpkg\installed\x64-windows

rem MarketScanner.h reaches ScreenCapture.h, so OpenCV's headers have to be on
rem the include path. Nothing is instantiated from them, so there is nothing to
rem link - this test only reads a text file and a table of numbers.
rem
rem _HAS_STD_BYTE=0: same collision PlanTest hits - Windows.h arrives through
rem MoveToPosition.h while std is a using-directive, and ::byte from rpcndr.h
rem then clashes with std::byte.
cl /nologo /EHsc /std:c++17 /MD /O2 /D_HAS_STD_BYTE=0 ^
   /I "%VCPKGI%\include" ^
   /I "%OPENCV_DIR%\include" ^
   CategoryTest.cpp ^
   ..\SFML\Sources\Config.cpp ^
   /Fe:CategoryTest.exe ^
   /link user32.lib

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK
