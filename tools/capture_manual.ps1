param([switch]$SkipCapture)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$project = Split-Path $PSScriptRoot -Parent
$captures = Join-Path $project 'build/manual-captures'
$destination = Join-Path $project 'docs/manual-assets'
if (!$SkipCapture) {
    $simulator = Join-Path $project 'build/Release/SmartMidiSimulator.exe'
    if (!(Test-Path -LiteralPath $simulator)) { throw 'Build the simulator first.' }
    & $simulator --manual-screenshots $captures
    if ($LASTEXITCODE) { throw 'Manual capture failed; inspect simulator output.' }
}
$null = New-Item -ItemType Directory -Force -Path $destination
Get-ChildItem -LiteralPath $captures -Filter '*.bmp' | ForEach-Object {
    $bitmap = [Drawing.Image]::FromFile($_.FullName)
    try { $bitmap.Save((Join-Path $destination ($_.BaseName+'.png')),[Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
}
Copy-Item -LiteralPath (Join-Path $project 'assets/fonts/OFL.txt') -Destination (Join-Path $destination 'Barlow-OFL.txt')
Write-Host 'Manual PNG screenshots refreshed.'
