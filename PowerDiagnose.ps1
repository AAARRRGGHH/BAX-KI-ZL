param([string]$Port,[switch]$ReadTrace)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$pythonPath = Join-Path $projectRoot '.tools/idf-tools/python_env/idf5.5_py3.12_env/Scripts/python.exe'
if (!(Test-Path -LiteralPath $pythonPath)) { throw 'Projektlokale ESP-IDF-Python-Umgebung fehlt.' }
if (!$Port) { $Port = Read-Host 'COM-Port des Waveshare (z.B. COM3)' }
if ($Port -notmatch '^COM[0-9]+$') { throw 'Ungueltiger COM-Port; erwartet z.B. COM3.' }
if ($ReadTrace) {
    & $pythonPath (Join-Path $projectRoot 'tools/read_power_trace.py') --port $Port
    if ($LASTEXITCODE) { throw 'RAM-Aufzeichnung konnte nicht vollstaendig gelesen werden. Details siehe oben.' }
    return
}
$logDirectory = Join-Path $projectRoot 'logs/pwr'
$null = New-Item -ItemType Directory -Force -Path $logDirectory
Write-Host 'PWR-Diagnose: kein Flashen, kein angeforderter Reset, automatische Logdatei.'
Write-Host 'USB anschliessen und Fehler nachstellen: PWR lange, danach erneut kurz druecken.'
Write-Host 'Wenn noetig mit offenem Monitor ausschalten und ueber PWR einschalten.'
Write-Host 'Beenden: Strg+] (alternativ Strg+T, dann X).'
Write-Host "Logdateien: $logDirectory"
Push-Location $logDirectory
try {
    & $pythonPath -m esp_idf_monitor --port $Port --baud 115200 --target esp32s3 --no-reset --save-log --timestamps --disable-address-decoding --decode-coredumps disable --disable-auto-color
    if ($LASTEXITCODE) { throw "Monitor beendet mit Fehler $LASTEXITCODE. Anderen Monitor schliessen und COM-Port pruefen." }
} finally {
    Pop-Location
    Write-Host "Die Aufzeichnung liegt unter: $logDirectory"
}
