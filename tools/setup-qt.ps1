[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$cache = Join-Path $root '.deps'
$archive = Join-Path $cache 'qtbase-6.12.0.7z'
$url = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6120/qt6_6120_msvc2022_64/qt.qt6.6120.win64_msvc2022_64/6.12.0-0-202609280346qtbase-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
$hash = '219d1b86def3be418e31d704572bc4acc91b2ec373c65595c9fb12b42bf098dc'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    & curl.exe --fail --silent --show-error --location --retry 2 --output "$archive.part" $url
    if ($LASTEXITCODE) { throw 'Qt download failed' }
    Move-Item -LiteralPath "$archive.part" -Destination $archive -Force
}
if ((Get-FileHash -LiteralPath $archive).Hash -ne $hash) { throw "Qt archive verification failed at $archive" }
& tar.exe -xf $archive -C $cache
if ($LASTEXITCODE) { throw 'Qt extraction failed' }
