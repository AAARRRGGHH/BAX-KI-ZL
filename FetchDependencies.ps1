$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force vendor | Out-Null
function Get-Source([string]$Name,[string]$Url,[string]$Revision,[string]$Branch) {
    $target = Join-Path $PSScriptRoot "vendor/$Name"
    if (!(Test-Path -LiteralPath $target)) {
        if ($Name -eq 'waveshare') { & git clone --filter=blob:none --sparse $Url $target }
        else { & git clone --depth 1 --branch $Branch $Url $target }
        if ($LASTEXITCODE) { throw "Download fehlgeschlagen: $Name" }
    }
    $actual = & git -C $target rev-parse HEAD
    if ($actual -ne $Revision) { throw "$Name hat Revision $actual statt $Revision. Vorhandene Quellen werden nicht automatisch ersetzt." }
}
Get-Source 'lvgl' 'https://github.com/lvgl/lvgl.git' '7f07a129e8d77f4984fff8e623fd5be18ff42e74' 'v9.2.2'
Get-Source 'SDL' 'https://github.com/libsdl-org/SDL.git' '8236e01a9f758d15927624925c6043f84d8a261f' 'release-2.30.12'
$boardPath = Join-Path $PSScriptRoot 'vendor/waveshare-bsp-1.75c'
if (!(Test-Path -LiteralPath $boardPath)) {
    New-Item -ItemType Directory -Force .tools | Out-Null
    $archive = Join-Path $PSScriptRoot '.tools/board-1.75c.zip'
    Invoke-WebRequest -UseBasicParsing 'https://components.espressif.com/api/downloads/?object_type=component&object_id=4b7cbad1-7629-453a-9136-ce1b0a0ba09b' -OutFile $archive
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne 'E8F286AA0D0BE100FE88B6B1D5E3FDC6547392F8972FEE1B52BA71EE1DFB8A68') {
        throw 'Pruefsumme des offiziellen 1.75C-BSP 3.0.0 stimmt nicht.'
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $boardPath
}
Write-Host 'Alle Abhaengigkeiten liegen in den dokumentierten Revisionen vor.'
