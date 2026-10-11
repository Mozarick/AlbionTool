@echo off
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"

set SFMLROOT=..\lib\SFML-2.5.1

rem Renders the real screens to PNG with no window and no game. UiKit.cpp is
rem the only project source needed - it depends on SFML and Theme.h, nothing
rem else - which is exactly why the drawing vocabulary lives in its own file.
cl /nologo /EHsc /std:c++17 /MD /O2 ^
   /I "%SFMLROOT%\include" ^
   UiPreview.cpp ^
   ..\SFML\Sources\UiKit.cpp ^
   /Fe:UiPreview.exe ^
   /link /LIBPATH:"%SFMLROOT%\lib" ^
   sfml-graphics.lib sfml-window.lib sfml-system.lib

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK

rem It has to run from SFML\ so "fonts/" and "categories.tsv" resolve, so it
rem is copied in, run, and removed again - the app's working directory is not
rem a place to leave a build artefact lying around.
copy /y UiPreview.exe ..\SFML\UiPreview.exe >nul
pushd ..\SFML
.\UiPreview.exe
popd
del ..\SFML\UiPreview.exe >nul 2>&1
echo PNGs written to SFML\
