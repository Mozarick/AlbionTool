param([string]$Destination)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$Destination) { $Destination = Join-Path $repo 'dist\MarketStudio-0.6.0' }
$release = [IO.Path]::GetFullPath($Destination)
if (!(Test-Path (Join-Path $repo 'x64\Release\SFML.exe'))) { throw 'Build-Tool.ps1 must run first.' }
# Packaging never erases a user's existing session or capture files.
New-Item -ItemType Directory -Path $release -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'x64\Release\SFML.exe') -Destination (Join-Path $release 'MarketStudio.exe') -Force
Get-ChildItem (Join-Path $repo 'x64\Release') -Filter *.dll -File | Copy-Item -Destination $release -Force
$data = Join-Path $release 'data'
New-Item -ItemType Directory -Path $data -Force | Out-Null
foreach ($name in @('Config.txt', 'scan_profile.tsv')) {
    $target = Join-Path $data $name
    if (!(Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath (Join-Path $repo "SFML\$name") -Destination $target }
}
foreach ($name in @('items.tsv', 'categories.tsv')) {
    Copy-Item -LiteralPath (Join-Path $repo "SFML\$name") -Destination $data -Force
}
New-Item -ItemType Directory -Path (Join-Path $data 'fonts') -Force | Out-Null
Get-ChildItem (Join-Path $repo 'SFML\fonts') -File | Copy-Item -Destination (Join-Path $data 'fonts') -Force
$capture = Join-Path $release 'capture\AlbionFeed'
New-Item -ItemType Directory -Path $capture -Force | Out-Null
Get-ChildItem 'D:\AlbionTool\bin\AlbionFeed' -File | Where-Object { $_.Extension -in @('.exe','.dll','.json') } | Copy-Item -Destination $capture -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'studio-run-feed.bat') -Destination (Join-Path $release 'capture\run-feed.bat') -Force
Copy-Item -LiteralPath (Join-Path $repo 'STUDIO-GUIDE.vi.md') -Destination (Join-Path $release 'HUONG-DAN.md') -Force
$licenses = Join-Path $release 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'lib\SFML-2.5.1\license.md') -Destination (Join-Path $licenses 'SFML.md') -Force
foreach ($copyright in Get-ChildItem 'D:\vcpkg\installed\x64-windows\share' -Filter copyright -Recurse -File) {
    Copy-Item -LiteralPath $copyright.FullName -Destination (Join-Path $licenses ($copyright.Directory.Name + '.txt')) -Force
}
$manifest = @(Get-ChildItem $release -File -Recurse | Where-Object {
    $_.FullName -notmatch '\\data\\(shopping_|holdings)' -and $_.Extension -ne '.log' -and $_.Name -ne 'manifest.json'
} | ForEach-Object {
    @{path=[IO.Path]::GetRelativePath($release,$_.FullName);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
@{product='Market Studio';version='0.6.0';files=$manifest} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $release 'manifest.json') -Encoding utf8
Write-Output "Packaged Market Studio: $release"
