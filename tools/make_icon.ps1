# Renders icon.svg to icon.ico with Edge. From the project folder:
#   powershell -ExecutionPolicy Bypass -File tools\make_icon.ps1 -svg "$PWD\icon.svg" -outIco "$PWD\icon.ico"
param($svg, $outIco, $work = $env:TEMP)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$sizes = 16,24,32,48,64,128,256
$pngs = @()
$svgText = [IO.File]::ReadAllText($svg)
foreach ($s in $sizes) {
  $html = Join-Path $work "i$s.html"
  $png = Join-Path $work "i$s.png"
  $inner = $svgText.Replace('width="256" height="256"', "width=`"$s`" height=`"$s`" style=`"display:block`"")
  [IO.File]::WriteAllText($html, "<!doctype html><html><body style='margin:0'>$inner</body></html>")
  if (Test-Path $png) { Remove-Item $png }
  $url = 'file:///' + ([Uri]$html).AbsolutePath.TrimStart('/')
  $win = [Math]::Max($s, 500)
  $a = @('--headless=new','--disable-gpu','--hide-scrollbars','--force-device-scale-factor=1',"--window-size=$win,$win","--screenshot=`"$png`"","--user-data-dir=`"$work\edge`"",$url)
  Start-Process $edge -ArgumentList $a -Wait -WindowStyle Hidden
  $b = [Drawing.Bitmap]::FromFile($png)
  $c = $b.Clone((New-Object Drawing.Rectangle 0,0,$s,$s), [Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $b.Dispose()
  $c.Save($png, [Drawing.Imaging.ImageFormat]::Png)
  $c.Dispose()
  $pngs += ,[IO.File]::ReadAllBytes($png)
}
$ms = New-Object IO.MemoryStream
$w = New-Object IO.BinaryWriter $ms
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$off = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $s = $sizes[$i]
  $v = if ($s -ge 256) { 0 } else { $s }
  $w.Write([byte]$v); $w.Write([byte]$v); $w.Write([byte]0); $w.Write([byte]0)
  $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$off)
  $off += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
[IO.File]::WriteAllBytes($outIco, $ms.ToArray())
"wrote $outIco $($ms.Length) bytes"
