param([string]$ConverterDirectory = '.tools/lv_font_conv', [int[]]$Sizes = (@(14) + @(18..38) + @(43,48)))
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$nodeCommand = Get-Command node -ErrorAction SilentlyContinue
$nodePath = if ($nodeCommand) { $nodeCommand.Source } else { 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe' }
if (!(Test-Path -LiteralPath $nodePath)) { throw 'Node.js fehlt. Nur zum Regenerieren der Schriftdateien erforderlich.' }
$converterPath = Join-Path $ConverterDirectory 'lv_font_conv.js'
if (!(Test-Path -LiteralPath $converterPath)) { throw 'LVGL Font Converter fehlt. Siehe assets/fonts/README.md.' }
foreach ($fontSize in $Sizes) {
    $glyphRange = if ($fontSize -gt 32 -and $fontSize -le 38) { '0x30-0x39' } else { '0x20-0x7e,0xa0-0xff' }
    & $nodePath $converterPath --font assets/fonts/Barlow-Regular.ttf --size $fontSize --bpp 4 --format lvgl --range $glyphRange --no-compress --lv-include lvgl.h --lv-font-name "barlow_$fontSize" --output "ui/fonts/barlow_$fontSize.c"
    if ($LASTEXITCODE) { throw "Schriftkonvertierung fehlgeschlagen: $fontSize" }
}
