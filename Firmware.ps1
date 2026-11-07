param(
    [ValidateSet('Build','Backup','Flash','Monitor')][string]$Action = 'Build',
    [string]$Port,
    [switch]$NoCache
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if ($Action -ne 'Build' -and $Port -notmatch '^COM[0-9]+$') {
    throw 'Bitte den Port angeben, z.B. -Port COM7 (Windows-Geraetemanager).'
}
$localIdf = Join-Path $PSScriptRoot '.tools/esp-idf'
if (Test-Path (Join-Path $localIdf 'export.ps1')) {
    $env:IDF_TOOLS_PATH = Join-Path $PSScriptRoot '.tools/idf-tools'
    . (Join-Path $localIdf 'export.ps1')
    if ($LASTEXITCODE) { throw 'ESP-IDF-Umgebung konnte nicht geladen werden.' }
} elseif (!(Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw 'Bitte aus einer ESP-IDF-5.5.4-PowerShell starten.'
}
function Invoke-Idf([string[]]$Arguments) {
    & python "$env:IDF_PATH/tools/idf.py" @Arguments
    if ($LASTEXITCODE) { throw 'ESP-IDF fehlgeschlagen. Ausgabe oben beachten.' }
}
function Enable-ProjectConfiguration {
    # sdkconfig.defaults does not override an existing Bluetooth-disabled sdkconfig.
    # Migrate explicit MIDI options and the internal RAM reserve. USB/BLE and
    # FreeRTOS stacks need internal RAM even when PSRAM has ample free space.
    $configPath = Join-Path $PSScriptRoot 'firmware/sdkconfig'
    if (!(Test-Path -LiteralPath $configPath)) { return }
    $config = [IO.File]::ReadAllText($configPath)
    $before = $config
    foreach ($disabled in @('CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG','CONFIG_ESP_CONSOLE_UART_DEFAULT','CONFIG_ESP_CONSOLE_USB_CDC',
                            'CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL','CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_DEFAULT','CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_IRAM_8BIT')) {
        $pattern = '(?m)^(?:# )?' + [regex]::Escape($disabled) + '(?:=[^\r\n]*| is not set)\r?$'
        $config = [regex]::Replace($config,$pattern,('# '+$disabled+' is not set'))
    }
    foreach ($setting in (Get-Content (Join-Path $PSScriptRoot 'firmware/sdkconfig.defaults'))) {
        if ($setting -match '^(CONFIG_(?:BT_[A-Z0-9_]+|TINYUSB_[A-Z0-9_]+|ESP_CONSOLE_NONE|SPIRAM_MALLOC_RESERVE_INTERNAL))=') {
            $key = $Matches[1]
            $pattern = '(?m)^(?:# )?' + [regex]::Escape($key) + '(?:=[^\r\n]*| is not set)\r?$'
            if ([regex]::IsMatch($config,$pattern)) { $config = [regex]::Replace($config,$pattern,$setting) }
            else { $config += "`n$setting`n" }
        }
    }
    $config = [regex]::Replace($config,'(?m)^CONFIG_BT_BLUEDROID_ENABLED=y\r?$','# CONFIG_BT_BLUEDROID_ENABLED is not set')
    if ($config -ne $before) {
        [IO.File]::WriteAllText($configPath,$config,(New-Object Text.UTF8Encoding($false)))
        Write-Host 'MIDI-/USB-Konfiguration aktiviert; andere sdkconfig-Einstellungen bleiben erhalten.'
    }
}
function Backup-Board {
    $backupDir = Join-Path $PSScriptRoot 'backups'
    New-Item -ItemType Directory -Force $backupDir | Out-Null
    $backup = Join-Path $backupDir ("1.75C-{0}-{1}.bin" -f $Port,(Get-Date -Format 'yyyyMMdd-HHmmss'))
    & python -m esptool --chip esp32s3 --port $Port --baud 921600 --after no_reset read_flash 0 0x2000000 $backup
    if ($LASTEXITCODE) { throw 'Sicherung fehlgeschlagen. Es wurde nichts geflasht.' }
    if ((Get-Item -LiteralPath $backup).Length -ne 0x2000000) { throw 'Sicherung ist unvollstaendig.' }
    Get-FileHash -LiteralPath $backup -Algorithm SHA256 | Format-List | Out-File "$backup.sha256.txt"
    Write-Host "32-MB-Sicherung: $backup"
}
if ($Action -in @('Build','Flash')) {
    & python (Join-Path $PSScriptRoot 'tools/prepare_idf_lock.py')
    if ($LASTEXITCODE) { throw 'Dependency lock preparation failed.' }
}
Push-Location firmware
try {
    switch ($Action) {
        Build { Enable-ProjectConfiguration; if($NoCache){Invoke-Idf @('--no-ccache','-DCCACHE_ENABLE=0','build')}else{Invoke-Idf @('build')} }
        Backup {
            $Port = & python (Join-Path $PSScriptRoot 'tools/usb_flash_port.py') prepare $Port
            if ($LASTEXITCODE) { throw 'USB-Bootloader nicht erreichbar.' }
            Backup-Board
        }
        Flash {
            Enable-ProjectConfiguration
            Invoke-Idf @('build')
            $Port = & python (Join-Path $PSScriptRoot 'tools/usb_flash_port.py') prepare $Port
            if ($LASTEXITCODE) { throw 'USB-Bootloader nicht erreichbar; nichts geflasht.' }
            Invoke-Idf @('-p',$Port,'flash')
            $monitorPort = & python (Join-Path $PSScriptRoot 'tools/usb_flash_port.py') monitor $Port
            if ($LASTEXITCODE) { throw 'Flash beendet; CDC-Monitorport bitte manuell im Geraetemanager pruefen.' }
            Invoke-Idf @('-p',$monitorPort,'monitor')
        }
        Monitor { Invoke-Idf @('-p',$Port,'monitor') }
    }
} finally { Pop-Location }
