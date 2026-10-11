@echo off
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"

cl /nologo /EHsc /std:c++17 /MD /O2 ^
   WipeTest.cpp ..\SFML\Sources\LogSink.cpp ^
   /Fe:WipeTest.exe

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK
