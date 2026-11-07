param()
$ErrorActionPreference = 'Stop'
# Read-only: no installs, registry writes, device resets, pairing or service changes.
$os = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
Write-Host ("Windows: {0}, Build {1}.{2}, Architektur {3}" -f $os.DisplayVersion,$os.CurrentBuild,$os.UBR,$env:PROCESSOR_ARCHITECTURE)
Write-Host 'ProductName in der Registry kann bei Windows 11 noch Windows 10 melden.'
Write-Host "`nMIDI-Dienste:"
$services = @(Get-Service | Where-Object { $_.Name -match 'midi' -or $_.DisplayName -match 'midi' })
if ($services.Count) { $services | Format-Table Name,Status,DisplayName -AutoSize }
else { Write-Host 'Keine MIDI-Dienste gefunden. Das ist kein Nachweis fuer einen BLE-MIDI-Eingang.' }
Write-Host "`nWindows MIDI Services Tools:"
$toolsPath = Join-Path $env:ProgramFiles 'Windows MIDI Services'
if (Test-Path -LiteralPath $toolsPath) {
    Get-ChildItem -LiteralPath $toolsPath -Filter '*.exe' -Recurse -ErrorAction SilentlyContinue |
        Select-Object Name,@{Name='Version';Expression={$_.VersionInfo.FileVersion}} | Format-Table -AutoSize
} else { Write-Host 'Kein Windows-MIDI-Services-Tools-Verzeichnis gefunden.' }
Write-Host "`nBluetooth-Geraete (Vorhandensein beweist keine aktive BLE-MIDI-Verbindung):"
try {
    $devices = @(Get-PnpDevice -Class Bluetooth -PresentOnly -ErrorAction Stop)
    if ($devices.Count) { $devices | Select-Object Status,FriendlyName | Format-Table -AutoSize }
    else { Write-Host 'Keine vorhandenen Bluetooth-Geraete gemeldet. Adapterstatus im Geraetemanager pruefen.' }
} catch { Write-Host ('Bluetooth-Abfrage lieferte keine Liste: ' + $_.Exception.Message) }
Write-Host 'Einrichtung: docs/BLE_MIDI_ABLETON.md'
