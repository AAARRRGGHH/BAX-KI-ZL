$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$command=Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath=if($command){$command.Source}else{'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'}
if(-not(Test-Path -LiteralPath $cmakePath)){throw 'Visual Studio 2022 mit C++ und CMake erforderlich.'}
$buildPath=Join-Path $projectRoot 'build'
& $cmakePath -S $projectRoot -B $buildPath -G 'Visual Studio 17 2022' -A x64 -DSMARTMIDI_SIMULATOR=ON
if($LASTEXITCODE){throw 'Konfiguration fehlgeschlagen.'}
& $cmakePath --build $buildPath --config Release --target BounceSimulator bounce_tests bounce_harmony_tests bounce_clock_tests bounce_menu_tests bounce_integration_tests bounce_render_tests
if($LASTEXITCODE){throw 'Build fehlgeschlagen.'}
& (Join-Path (Split-Path $cmakePath) 'ctest.exe') --test-dir $buildPath -C Release --output-on-failure -R '^bounce_'
if($LASTEXITCODE){throw 'Bounce-Tests fehlgeschlagen.'}
