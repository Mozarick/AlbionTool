@echo off
setlocal
taskkill /F /IM AlbionFeed.exe >nul 2>&1
taskkill /F /IM albiondata-client.exe >nul 2>&1
set STUDIO_CAPTURE_DIR=%~dp0AlbionFeed
powershell -NoProfile -Command "Start-Process -FilePath (Join-Path $env:STUDIO_CAPTURE_DIR 'AlbionFeed.exe') -ArgumentList '--port','5000' -WorkingDirectory $env:STUDIO_CAPTURE_DIR -WindowStyle Hidden"
endlocal
