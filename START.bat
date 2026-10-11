@echo off
setlocal
title Albion bot launcher

REM One click brings the whole stack up:
REM   1. AlbionFeed  - sniffs the game's packets, needs Administrator, POSTs
REM                    decoded market data to the bot and to nowhere else
REM   2. the bot     - opens the feed port and the control window
REM
REM Mostly redundant now: the bot starts both of these itself. Kept because it
REM is still the quickest way to bring everything up from a cold machine, and
REM because a desktop shortcut somewhere probably still points at it.
REM
REM Only the capture client is elevated. The bot deliberately is not: it has to
REM drive the mouse in the game window, and an elevated process is fine either
REM way, but leaving it unelevated keeps the UAC prompts down to one.

set ROOT=%~dp0
set FEED=D:\AlbionTool\bin
set BOT=%ROOT%x64\Release\SFML.exe
set BOTDIR=%ROOT%SFML

REM DLLs for tesseract/leptonica and OpenCV live outside the project
set PATH=D:\vcpkg\installed\x64-windows\bin;D:\OpenCV\opencv\build\x64\vc14\bin;%PATH%

echo ================================================
echo  Albion bot
echo ================================================
echo.

if not exist "%BOT%" (
  echo [X] Bot not built yet: %BOT%
  echo     Build SFML.sln as Release^|x64 first.
  echo.
  pause
  exit /b 1
)

REM --- the game ------------------------------------------------------------
REM Only the launcher is started; logging in is left to you. The capture
REM client and the bot do not care how long that takes, they just sit idle.
set LAUNCHER=D:\albion\launcher\AlbionLauncher.exe

tasklist /FI "IMAGENAME eq Albion-Online.exe" 2>nul | find /I "Albion-Online.exe" >nul
if errorlevel 1 (
  tasklist /FI "IMAGENAME eq AlbionLauncher.exe" 2>nul | find /I "AlbionLauncher.exe" >nul
  if errorlevel 1 (
    if exist "%LAUNCHER%" (
      echo [..] starting the Albion launcher
      start "" "%LAUNCHER%"
    ) else (
      echo [!] launcher not found at %LAUNCHER%
      echo     start the game yourself.
    )
  ) else (
    echo [ok] Albion launcher already open
  )
) else (
  echo [ok] Albion Online is running
)

REM --- capture client ------------------------------------------------------
REM run-feed.bat kills every other capture client first, albiondata-client
REM included: that one uploads everything it sees to the public Albion Data
REM Project servers, which is exactly what AlbionFeed exists to avoid.
tasklist /FI "IMAGENAME eq AlbionFeed.exe" 2>nul | find /I "AlbionFeed.exe" >nul
if errorlevel 1 (
  echo [..] starting AlbionFeed ^(approve the UAC prompt^)
  powershell -NoProfile -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','%FEED%\run-feed.bat' -Verb RunAs"
  REM give it a moment to get past UAC and open its capture handle
  timeout /t 6 /nobreak >nul
) else (
  echo [ok] AlbionFeed already running
)

tasklist /FI "IMAGENAME eq AlbionFeed.exe" 2>nul | find /I "AlbionFeed.exe" >nul
if errorlevel 1 (
  echo [X] AlbionFeed did not start - was the UAC prompt declined?
  echo     Without it there is no market data and Scan/SellBM will do nothing.
  echo     Its own log: %FEED%\AlbionFeed\AlbionFeed.log
  echo.
)

REM --- the bot -------------------------------------------------------------
tasklist /FI "IMAGENAME eq SFML.exe" 2>nul | find /I "SFML.exe" >nul
if errorlevel 1 (
  echo [..] starting the bot
  start "" /D "%BOTDIR%" "%BOT%"
) else (
  echo [ok] bot already running
)

echo.
echo Done. The bot window is the 900x820 one.
echo Close this window whenever you like, it is only a launcher.
echo.
timeout /t 5 /nobreak >nul
endlocal
