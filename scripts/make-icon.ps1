# Generates vsproject\resources\PlugIn.ico (VST3 bundle folder icon + installer icon).
# Uses assets\classicSynth.png (square artwork) when present, otherwise renders
# assets\logo.png onto a dark rounded tile.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\make-icon.ps1
param(
    [string]$Art  = "$PSScriptRoot\..\assets\classicSynth.png",
    [string]$Logo = "$PSScriptRoot\..\assets\logo.png",
    [string]$Out  = "$PSScriptRoot\..\vsproject\resources\PlugIn.ico"
)
Add-Type -AssemblyName System.Drawing

$useArt = Test-Path $Art
$srcImg = [System.Drawing.Image]::FromFile((Resolve-Path ($(if ($useArt) { $Art } else { $Logo }))))
$sizes = 256, 48, 32, 16
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)

    $r = [Math]::Max(2, [int]($s * 0.18))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $w = $s - 1
    $path.AddArc(0, 0, $r * 2, $r * 2, 180, 90)
    $path.AddArc($w - $r * 2, 0, $r * 2, $r * 2, 270, 90)
    $path.AddArc($w - $r * 2, $w - $r * 2, $r * 2, $r * 2, 0, 90)
    $path.AddArc(0, $w - $r * 2, $r * 2, $r * 2, 90, 90)
    $path.CloseFigure()

    if ($useArt) {
        # Full-bleed square artwork clipped to a rounded tile.
        $g.SetClip($path)
        $g.DrawImage($srcImg, 0, 0, $s, $s)
        $g.ResetClip()
    } else {
        $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 28, 28, 35))), $path)
        $lw = $s * 0.86
        $lh = $lw * $srcImg.Height / $srcImg.Width
        $g.DrawImage($srcImg, [float](($s - $lw) / 2), [float](($s - $lh) / 2), [float]$lw, [float]$lh)
    }
    $g.DrawPath((New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 106, 34, 184)), ([Math]::Max(1, $s / 96))), $path)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += , $ms.ToArray()
    $bmp.Dispose()
}
$srcImg.Dispose()

# ICO container with PNG-compressed entries (Windows Vista+).
$fs = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$pngs[$i].Length); $bw.Write([UInt32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Flush()
[System.IO.File]::WriteAllBytes((Join-Path (Resolve-Path (Split-Path $Out)) (Split-Path $Out -Leaf)), $fs.ToArray())
Write-Host "Wrote $Out (source: $(if ($useArt) { 'classicSynth.png' } else { 'logo.png' }))"
