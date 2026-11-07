param([ValidateSet('DE','EN','all')][string]$Lang='all')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
& python (Join-Path $project 'tools/print_manual.py') --lang $Lang
if ($LASTEXITCODE) { throw 'Manual PDF export failed; inspect build/manual-render-DE.log or -EN.log.' }
