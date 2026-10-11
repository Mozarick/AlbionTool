@echo off
REM Builds the icon diagnostic. Uses the same OpenCV + tesseract that the bot
REM links against, nothing extra is downloaded.
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

REM cd into the script's folder so no path needs a trailing backslash: "%~dp0"
REM would end in \" and eat the closing quote.
cd /d "%~dp0"

set OPENCV=D:\OpenCV\opencv\build
set VCPKGI=D:\vcpkg\installed\x64-windows

cl /nologo /EHsc /std:c++17 /MD /O2 ^
   /I "%OPENCV%\include" /I "%VCPKGI%\include" ^
   TierTest.cpp ^
   /Fe:TierTest.exe /Fo:TierTest.obj ^
   /link /LIBPATH:"%OPENCV%\x64\vc14\lib" /LIBPATH:"%VCPKGI%\lib" ^
   opencv_world455.lib tesseract55.lib

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK
