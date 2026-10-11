param([switch]$TestsOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath
if (!$vs) { throw 'Visual Studio C++ Build Tools are required.' }
$msvc = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$deps = Join-Path (Split-Path -Parent $repo) '.build-deps'
$sdkInclude = Get-ChildItem (Join-Path $deps 'winsdk\c\Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$vcpkg = if ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) { $env:VCPKG_ROOT } else { 'D:\vcpkg' }
$opencv = Join-Path $deps 'opencv\opencv\build'
$env:INCLUDE = "$($msvc.FullName)\include;$($sdkInclude.FullName)\ucrt;$($sdkInclude.FullName)\um;$($sdkInclude.FullName)\shared"
$env:LIB = "$($msvc.FullName)\lib\x64;$deps\winsdk-x64\c\ucrt\x64;$deps\winsdk-x64\c\um\x64"
$env:PATH = "$repo\x64\Release;$repo\lib\SFML-2.5.1\bin;$($msvc.FullName)\bin\Hostx64\x64;$env:PATH"
Push-Location $repo
try {
    if (!$TestsOnly) {
        & (Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe') SFML.sln /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 "/p:VCPKG_ROOT=$vcpkg" "/p:OPENCV_DIR=$opencv" "/p:ForceImportBeforeCppProps=$deps\local-sdk.props" "/p:ForceImportAfterCppTargets=$deps\winsdk\build\Microsoft.Windows.SDK.cpp.targets"
        if ($LASTEXITCODE) { throw "App build failed ($LASTEXITCODE)." }
    }
    $out = Join-Path $PSScriptRoot 'tool-artifacts'
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    $compiler = Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe'
    [xml]$project = Get-Content SFML\SFML.vcxproj
    $sources = $project.Project.ItemGroup.ClCompile.Include | Where-Object { $_ -and $_ -ne 'Source.cpp' }
    $objects = $sources | ForEach-Object { Join-Path $repo ('SFML\x64\Release\' + [IO.Path]::GetFileNameWithoutExtension($_) + '.obj') }
    foreach ($test in @('PlanTest', 'ShopListTest', 'ToolRegressionTest', 'StudioProductTest')) {
        & $compiler /nologo /std:c++17 /EHsc /O2 /MD /W4 /DNOMINMAX /D_HAS_STD_BYTE=0 "/I$vcpkg\installed\x64-windows\include" "/I$repo\lib\SFML-2.5.1\include" "/I$opencv\include" "/Fo$out\" "/Fe$out\$test.exe" "tools\$test.cpp" $objects /link "/LIBPATH:$repo\lib\SFML-2.5.1\lib" "/LIBPATH:$vcpkg\installed\x64-windows\lib" "/LIBPATH:$opencv\x64\vc14\lib" sfml-graphics.lib sfml-window.lib sfml-system.lib opencv_world455.lib tesseract55.lib ws2_32.lib user32.lib gdi32.lib shell32.lib /LTCG
        if ($LASTEXITCODE) { throw "$test build failed ($LASTEXITCODE)." }
        # Isolate files from live scan profile and holdings.
        $runtime = Join-Path $out 'runtime'
        New-Item -ItemType Directory -Path $runtime -Force | Out-Null
        foreach ($name in @('items.tsv', 'categories.tsv')) {
            Copy-Item -LiteralPath (Join-Path $repo "SFML\$name") -Destination $runtime -Force
        }
        if (!(Test-Path (Join-Path $runtime 'fonts'))) {
            Copy-Item -LiteralPath (Join-Path $repo 'SFML\fonts') -Destination $runtime -Recurse
        }
        Push-Location $runtime
        try {
            & "$out\$test.exe"
            if ($LASTEXITCODE) { throw "$test failed ($LASTEXITCODE)." }
        } finally { Pop-Location }
    }
} finally { Pop-Location }
