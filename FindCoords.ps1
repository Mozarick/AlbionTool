# Hover the mouse over a button in-game and read the X,Y printed here.
# Run this while Albion Online is open at its normal resolution.
# Press Ctrl+C to stop.
#
# The bot is DPI-aware, so it works in real physical pixels. PowerShell is not
# by default: on a scaled display it would report a shrunken virtual desktop
# (1536x864 instead of 1920x1080 at 125%) and every coordinate measured here
# would be ~25% off. Opt in first so what you read matches what the bot uses.
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class DpiAware {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int n);
}
"@
[void][DpiAware]::SetProcessDPIAware()

Add-Type -AssemblyName System.Windows.Forms

$w = [DpiAware]::GetSystemMetrics(0)
$h = [DpiAware]::GetSystemMetrics(1)
Write-Host "Physical screen: ${w} x ${h}"
Write-Host "Hover a button, read the coordinates, Ctrl+C to stop.`n"

while ($true) {
    $p = [System.Windows.Forms.Cursor]::Position
    Write-Host "`r$($p.X), $($p.Y)          " -NoNewline
    Start-Sleep -Milliseconds 100
}
