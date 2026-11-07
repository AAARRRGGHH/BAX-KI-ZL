param([switch]$InstallDependencies)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if ($InstallDependencies) {
    & python -m pip install --target .tools/launcher-python -r launcher/requirements-build.txt
    if ($LASTEXITCODE) { throw 'Companion-Abhängigkeiten konnten nicht installiert werden.' }
}
if (!(Test-Path .tools/launcher-python/PyInstaller)) {
    throw 'Einmal Build-Companion.ps1 -InstallDependencies ausführen.'
}
$env:PYTHONPATH = Join-Path $PSScriptRoot '.tools/launcher-python'
& python -m unittest discover -s tests -p test_companion.py -v
if ($LASTEXITCODE) { throw 'Companion-Tests fehlgeschlagen.' }
& python tools/build_companion.py
if ($LASTEXITCODE) { throw 'Companion-Build fehlgeschlagen.' }
& python -m unittest discover -s tests -p test_companion_ui.py -v
if ($LASTEXITCODE) { throw 'Companion-Oberflaechentests fehlgeschlagen.' }
& python -m unittest discover -s tests -p test_companion_button_art.py -v
if ($LASTEXITCODE) { throw 'Button-Darstellungstest fehlgeschlagen.' }
& python tests/check_companion_embedding.py
if ($LASTEXITCODE) { throw 'Eingebetteter Simulator-Test fehlgeschlagen.' }
