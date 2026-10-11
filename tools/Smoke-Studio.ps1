$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$release = Join-Path $repo 'dist\MarketStudio-0.6.0'
$executable = Join-Path $release 'MarketStudio.exe'
if (!(Test-Path -LiteralPath $executable)) { throw 'Package-Studio.ps1 must run first.' }
if (Get-NetTCPConnection -LocalPort 5000 -State Listen -ErrorAction SilentlyContinue) { throw 'Port 5000 is already occupied; close the other tool before this isolated smoke test.' }
$shot = Join-Path $PSScriptRoot 'tool-artifacts\packaged-home.png'
$originalPath = $env:PATH
try {
    # Prove DLL resolution and relative data discovery without development PATH
    # or a checkout working directory. Preview mode never launches the game/feed.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $process = Start-Process -FilePath $executable -ArgumentList @('--capture-preview',$shot) -WorkingDirectory $env:SystemRoot -WindowStyle Hidden -PassThru
} finally { $env:PATH = $originalPath }
$delivered = $false
for ($attempt = 0; $attempt -lt 25; $attempt++) {
    $listener = Get-NetTCPConnection -LocalPort 5000 -State Listen -ErrorAction SilentlyContinue
    if ($listener -and $listener.OwningProcess -eq $process.Id) {
        $body = @{Orders=@(
            @{Id=900001;ItemTypeId='T4_MAIN_SWORD';LocationId='1002';QualityLevel=1;EnchantmentLevel=0;Amount=10;UnitPriceSilver=1000000;AuctionType='offer'},
            @{Id=900002;ItemTypeId='T4_MAIN_SWORD';LocationId='3003';QualityLevel=1;EnchantmentLevel=0;Amount=10;UnitPriceSilver=2000000;AuctionType='request'}
        )} | ConvertTo-Json -Depth 4 -Compress
        Invoke-RestMethod -Uri 'http://127.0.0.1:5000/market' -Method Post -Body $body -ContentType 'application/json' -TimeoutSec 2 | Out-Null
        $delivered = $true; break
    }
    if ($process.HasExited) { throw "Packaged app exited before receiver startup (exit $($process.ExitCode))." }
    Start-Sleep -Milliseconds 100
}
if (!$delivered) { throw "Receiver did not become available. Inspect preview process $($process.Id) before retrying." }
if (!$process.WaitForExit(12000)) { throw "Preview is still running as process $($process.Id). Inspect it before retrying." }
if ($process.ExitCode -ne 0) { throw "Packaged executable failed ($($process.ExitCode))." }
if (!(Test-Path -LiteralPath $shot) -or (Get-Item -LiteralPath $shot).Length -lt 10000) { throw 'Packaged executable did not render the dashboard.' }
$manifest = Get-Content -LiteralPath (Join-Path $release 'manifest.json') -Raw | ConvertFrom-Json
foreach ($file in $manifest.files) {
    if ((Get-FileHash -LiteralPath (Join-Path $release $file.path) -Algorithm SHA256).Hash -ne $file.sha256) { throw "Release hash mismatch: $($file.path)" }
}
Write-Output "PACKAGED SMOKE PASSED: clean PATH, independent cwd, real HTTP, dashboard, clean exit, $($manifest.files.Count) verified files. Preview: $shot"
