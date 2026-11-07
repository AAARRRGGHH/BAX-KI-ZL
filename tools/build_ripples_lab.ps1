param([uint32]$Seed=41,[ValidateRange(1,600)][int]$Seconds=120,[switch]$NoPiano,[switch]$NoOpen)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$cmakeCommand=Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath=if($cmakeCommand){$cmakeCommand.Source}else{'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'}
if(-not(Test-Path -LiteralPath $cmakePath)){throw 'CMake/Visual Studio C++ fehlt. Siehe docs/RIPPLES_LAB.md.'}
$ctestPath=Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
$buildPath=Join-Path $projectRoot 'build-ripples'
$outputPath=Join-Path $projectRoot 'proposals\ripples_lab_v1'
& $cmakePath -S $projectRoot -B $buildPath -G 'Visual Studio 17 2022' -A x64 -DSMARTMIDI_SIMULATOR=OFF
if($LASTEXITCODE){throw 'Konfiguration fehlgeschlagen.'}
& $cmakePath --build $buildPath --config Release --target RipplesLab ripples_tests
if($LASTEXITCODE){throw 'Build fehlgeschlagen.'}
& $ctestPath --test-dir $buildPath -C Release --output-on-failure -R '^ripples'
if($LASTEXITCODE){throw 'Ripples-Tests fehlgeschlagen.'}
& (Join-Path $buildPath 'Release\RipplesLab.exe') --suite --duration $Seconds --seed $Seed --out $outputPath
if($LASTEXITCODE){throw 'MIDI-Export fehlgeschlagen.'}
& python (Join-Path $PSScriptRoot 'check_ripples_export.py') $outputPath
if($LASTEXITCODE){throw 'MIDI-Dateipruefung fehlgeschlagen.'}
if(-not $NoPiano){
  & python (Join-Path $PSScriptRoot 'render_ripples_listening.py') --input $outputPath
  if($LASTEXITCODE){throw 'Pianovorschau fehlgeschlagen. Die MIDI-Dateien wurden bereits erzeugt.'}
  if(-not $NoOpen){Start-Process (Join-Path $outputPath 'index.html')}
}
Write-Host "Hoerlabor fertig: $outputPath"
