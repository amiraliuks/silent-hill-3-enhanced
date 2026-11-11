param(
    [Parameter(Mandatory = $true)] [string] $MessageArchive,
    [Parameter(Mandatory = $true)] [string] $RasterRoot,
    [Parameter(Mandatory = $true)] [string] $JapaneseFontPath,
    [Parameter(Mandatory = $true)] [string] $KoreanFontPath,
    [Parameter(Mandatory = $true)] [string] $ChineseFontPath
)

$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -ne 'Desktop') {
    throw 'Run this rasterizer with Windows PowerShell 5.1 (powershell.exe), which provides Windows GDI+.'
}

Add-Type -AssemblyName System.Drawing
Add-Type -Path (Join-Path $PSScriptRoot 'CjkFontRasterizer.cs') `
    -ReferencedAssemblies 'System.Drawing.dll', 'System.Core.dll'

$written = [CjkFontRasterizer]::RenderRasters(
    (Resolve-Path -LiteralPath $MessageArchive).Path,
    (Resolve-Path -LiteralPath $JapaneseFontPath).Path,
    (Resolve-Path -LiteralPath $KoreanFontPath).Path,
    (Resolve-Path -LiteralPath $ChineseFontPath).Path,
    [System.IO.Path]::GetFullPath($RasterRoot))

Write-Output "Rendered $written Japanese/Hangul/Han glyphs into $([System.IO.Path]::GetFullPath($RasterRoot))."
