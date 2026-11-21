[CmdletBinding()]
param([switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio 2026 with Desktop development with C++, CMake, and the Windows SDK.' }
$bin = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
if (!(Test-Path (Join-Path $root '.deps/lib/cmake/Qt6/Qt6Config.cmake'))) {
    & "$PSScriptRoot/setup-qt.ps1"
}
& "$bin/cmake.exe" -S $root -B "$root/build" -G 'Visual Studio 18 2026' -A x64
if ($LASTEXITCODE) { throw 'Configuration failed.' }
& "$bin/cmake.exe" --build "$root/build" --config Release --parallel
if ($LASTEXITCODE) { throw 'Build failed.' }
if ($Test) {
    $env:PATH = "$root/.deps/bin;$env:PATH"
    & "$bin/ctest.exe" --test-dir "$root/build" -C Release --output-on-failure --no-tests=error
    if ($LASTEXITCODE) { throw 'Tests failed.' }
}
