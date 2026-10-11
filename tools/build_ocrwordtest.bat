@echo off
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
set OPENCV=D:\OpenCV\opencv\build
set VCPKGI=D:\vcpkg\installed\x64-windows
cl /nologo /EHsc /std:c++17 /MD /O2 /D NOMINMAX /D _HAS_STD_BYTE=0 ^
   /I "%OPENCV%\include" /I "%VCPKGI%\include" ^
   OcrWordTest.cpp ..\SFML\Sources\Ocr.cpp ..\SFML\Sources\Config.cpp ^
   /Fe:OcrWordTest.exe ^
   /link /LIBPATH:"%OPENCV%\x64\vc14\lib" /LIBPATH:"%VCPKGI%\lib" opencv_world455.lib tesseract55.lib
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD OK