param([switch]$TestsOnly)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $cmakePath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (!(Test-Path -LiteralPath $cmakePath)) { throw 'CMake fehlt. Visual Studio 2022 mit Desktopentwicklung mit C++ und CMake installieren.' }
}
$buildDir = if ($TestsOnly) { 'build-core' } else { 'build' }
$sim = if ($TestsOnly) { 'OFF' } else { 'ON' }
& $cmakePath -S . -B $buildDir -G 'Visual Studio 17 2022' -A x64 "-DSMARTMIDI_SIMULATOR=$sim"
if ($LASTEXITCODE) { throw 'CMake-Konfiguration fehlgeschlagen.' }
& $cmakePath --build $buildDir --config Release --parallel
if ($LASTEXITCODE) { throw 'Build fehlgeschlagen.' }
$ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
& $ctestPath --test-dir $buildDir -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Tests fehlgeschlagen.' }
