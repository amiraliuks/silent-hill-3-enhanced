param(
    [Parameter(Mandatory = $true)] [string] $RasterRoot,
    [Parameter(Mandatory = $true)] [string] $OutputPath,
    [string] $MessageArchive,
    [switch] $RasterizedCjk
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Runtime.InteropServices

$styles = @(
    @{ Directory = 'j_normal'; BankId = 1; Id = 0; Width = 20; Height = 30 },
    @{ Directory = 'j_small';  BankId = 1; Id = 1; Width = 16; Height = 24 },
    @{ Directory = 'k_normal'; BankId = 0; Id = 0; Width = 20; Height = 30 },
    @{ Directory = 'k_small';  BankId = 0; Id = 1; Width = 16; Height = 24 },
    @{ Directory = 'c_normal'; BankId = 2; Id = 0; Width = 20; Height = 30 },
    @{ Directory = 'c_small';  BankId = 2; Id = 1; Width = 16; Height = 24 }
)

$cjkGlyphCount = 0
if ($RasterizedCjk -and -not $MessageArchive) {
    throw '-RasterizedCjk requires -MessageArchive so native special glyphs can be retained.'
}
if ($MessageArchive) {
    if (-not (Test-Path -LiteralPath $MessageArchive -PathType Leaf)) {
        throw "Message archive not found: $MessageArchive"
    }
    Add-Type -Path (Join-Path $PSScriptRoot 'FontPackSource.cs')
    if ($RasterizedCjk) {
        $cjkGlyphCount = [FontPackSource]::CountCjkSupplementalRecords($MessageArchive)
    }
    else {
        $cjkGlyphCount = [FontPackSource]::CountCjkRecords($MessageArchive)
    }
}

$glyphFiles = foreach ($style in $styles) {
    $directory = Join-Path $RasterRoot $style.Directory
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Missing raster directory: $directory"
    }

    foreach ($file in Get-ChildItem -LiteralPath $directory -File -Filter 'glyph_*.png') {
        if ($file.BaseName -notmatch '^glyph_(\d+)$') {
            throw "Unexpected glyph filename: $($file.Name)"
        }

        [pscustomobject]@{
            File = $file
            BankId = $style.BankId
            StyleId = $style.Id
            Width = $style.Width
            Height = $style.Height
            GlyphId = [int]$Matches[1]
        }
    }
}

if ($glyphFiles.Count -eq 0) {
    throw 'No glyph PNGs were found.'
}

$parent = Split-Path -Parent $OutputPath
if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
$stream = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('SH3EFNT1'))
    $writer.Write([uint32]2)
    $writer.Write([uint32]4)
    $writer.Write([uint32]($glyphFiles.Count + $cjkGlyphCount))

    foreach ($entry in $glyphFiles | Sort-Object StyleId, GlyphId) {
        $bitmap = [System.Drawing.Bitmap]::FromFile($entry.File.FullName)
        try {
            $expectedWidth = $entry.Width * 4
            $expectedHeight = $entry.Height * 4
            if ($bitmap.Width -ne $expectedWidth -or $bitmap.Height -ne $expectedHeight) {
                throw "Unexpected dimensions for $($entry.File.FullName): $($bitmap.Width)x$($bitmap.Height), expected ${expectedWidth}x${expectedHeight}."
            }

            $rectangle = [System.Drawing.Rectangle]::new(0, 0, $bitmap.Width, $bitmap.Height)
            $locked = $bitmap.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                $coverage = [byte[]]::new($bitmap.Width * $bitmap.Height)
                $row = [byte[]]::new($bitmap.Width * 4)
                for ($y = 0; $y -lt $bitmap.Height; $y++) {
                    $rowPointer = [IntPtr]::Add($locked.Scan0, $y * $locked.Stride)
                    [System.Runtime.InteropServices.Marshal]::Copy($rowPointer, $row, 0, $row.Length)
                    for ($x = 0; $x -lt $bitmap.Width; $x++) {
                        $coverage[$y * $bitmap.Width + $x] = $row[$x * 4 + 3]
                    }
                }
            }
            finally {
                $bitmap.UnlockBits($locked)
            }

            $writer.Write([uint16]$entry.GlyphId)
            $writer.Write([byte]$entry.StyleId)
            $writer.Write([byte]$entry.BankId)
            $writer.Write([byte]$entry.Width)
            $writer.Write([byte]$entry.Height)
            $writer.Write([byte[]]::new(2))
            $writer.Write($coverage)
        }
        finally {
            $bitmap.Dispose()
        }
    }

    if ($MessageArchive) {
        if ($RasterizedCjk) {
            $writtenCjkCount = [FontPackSource]::WriteCjkSupplementalRecords($writer, $MessageArchive)
        }
        else {
            $writtenCjkCount = [FontPackSource]::WriteCjkRecords($writer, $MessageArchive)
        }
        if ($writtenCjkCount -ne $cjkGlyphCount) {
            throw "Expected $cjkGlyphCount Korean/Chinese supplemental glyphs but wrote $writtenCjkCount."
        }
    }
}
finally {
    $writer.Dispose()
    $stream.Dispose()
}

if ($RasterizedCjk) {
    Write-Output "Wrote $($glyphFiles.Count + $cjkGlyphCount) glyphs to $OutputPath (raster: $($glyphFiles.Count), native CJK special glyphs: $cjkGlyphCount)."
}
else {
    Write-Output "Wrote $($glyphFiles.Count + $cjkGlyphCount) glyphs to $OutputPath (Latin: $($glyphFiles.Count), Korean/Chinese: $cjkGlyphCount)."
}
