param([Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
[xml]$svg = Get-Content -LiteralPath (Join-Path $root 'assets/boot_logo.svg') -Raw
$image = [Drawing.Bitmap]::new(2200,500)
$graphics = [Drawing.Graphics]::FromImage($image)
$graphics.Clear([Drawing.Color]::Black)
$graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.ScaleTransform(2,2)
foreach ($group in $svg.svg.g) {
    $color = if ($group.id.StartsWith('accent')) { [Drawing.ColorTranslator]::FromHtml('#2ee7e7') } else { [Drawing.Color]::White }
    $brush = [Drawing.SolidBrush]::new($color)
    foreach ($element in $group.ChildNodes) {
        if ($element.Name -eq 'circle') {
            $r=[single]$element.r
            $graphics.FillEllipse($brush,([single]$element.cx-$r),([single]$element.cy-$r),2*$r,2*$r)
            continue
        }
        if ($element.Name -ne 'path') { continue }
        $path = [Drawing.Drawing2D.GraphicsPath]::new([Drawing.Drawing2D.FillMode]::Alternate)
        $tokens = [regex]::Matches($element.d,'[MLCQZ]|-?\d+(?:\.\d+)?')
        $i=0; [single]$x=0; [single]$y=0
        while ($i -lt $tokens.Count) {
            $command=$tokens[$i++].Value
            if ($command -eq 'Z') { $path.CloseFigure(); continue }
            $count = switch($command) { 'M' {2} 'L' {2} 'C' {6} 'Q' {4} default { throw "Unsupported SVG command $command" } }
            $v=@(); for($j=0;$j -lt $count;$j++) { $v += [single]::Parse($tokens[$i++].Value,[Globalization.CultureInfo]::InvariantCulture) }
            switch ($command) {
                'M' { $path.StartFigure(); $x=$v[0]; $y=$v[1] }
                'L' { $path.AddLine($x,$y,$v[0],$v[1]); $x=$v[0]; $y=$v[1] }
                'C' { $path.AddBezier($x,$y,$v[0],$v[1],$v[2],$v[3],$v[4],$v[5]); $x=$v[4]; $y=$v[5] }
                'Q' { $path.AddBezier($x,$y,($x+2*($v[0]-$x)/3),($y+2*($v[1]-$y)/3),($v[2]+2*($v[0]-$v[2])/3),($v[3]+2*($v[1]-$v[3])/3),$v[2],$v[3]); $x=$v[2]; $y=$v[3] }
            }
        }
        if ($group.fill -ne 'none') { $graphics.FillPath($brush,$path) }
        else {
            $pen = [Drawing.Pen]::new($color,[single]$group.'stroke-width')
            $pen.StartCap=$pen.EndCap=[Drawing.Drawing2D.LineCap]::Round
            $pen.LineJoin=[Drawing.Drawing2D.LineJoin]::Round
            $graphics.DrawPath($pen,$path); $pen.Dispose()
        }
        $path.Dispose()
    }
    $brush.Dispose()
}
$graphics.Dispose()
$image.Save([IO.Path]::GetFullPath($OutputFile),[Drawing.Imaging.ImageFormat]::Png)
$image.Dispose()
