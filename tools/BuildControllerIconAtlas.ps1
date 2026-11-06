param(
    [string]$AssetDirectory = (Join-Path $PSScriptRoot '..\third_party\kenney-input-prompts'),
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\third_party\kenney-input-prompts\controller_prompts.tga'),
    [string]$PreviewPath = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$cellSize = 64
$columns = 16
$rows = 8
$atlas = [System.Drawing.Bitmap]::new($cellSize * $columns, $cellSize * $rows, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($atlas)
$graphics.Clear([System.Drawing.Color]::Transparent)
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

function Load-Sheet([string]$filename)
{
    $imagePath = Join-Path $AssetDirectory $filename
    $xmlPath = [System.IO.Path]::ChangeExtension($imagePath, '.xml')
    [pscustomobject]@{
        Bitmap = [System.Drawing.Bitmap]::new($imagePath)
        Xml = [xml][System.IO.File]::ReadAllText($xmlPath)
    }
}

function Draw-Sprite($sheet, [string]$name, [int]$column, [int]$row)
{
    $sprite = $sheet.Xml.SelectSingleNode("/TextureAtlas/SubTexture[@name='$name']")
    if ($null -eq $sprite) { throw "Sprite '$name' was not found in the selected Kenney sheets." }

    # Kenney's atlas XML uses a bottom-left origin; PNG pixel coordinates start at top-left.
    $sourceY = $sheet.Bitmap.Height - [int]$sprite.y - [int]$sprite.height
    $source = [System.Drawing.Rectangle]::new([int]$sprite.x, $sourceY, [int]$sprite.width, [int]$sprite.height)
    $destination = [System.Drawing.Rectangle]::new($column * $cellSize, $row * $cellSize, $cellSize, $cellSize)
    $originalMode = $graphics.CompositingMode
    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $graphics.FillRectangle([System.Drawing.Brushes]::Transparent, $destination)
    $graphics.CompositingMode = $originalMode
    $graphics.DrawImage($sheet.Bitmap, $destination, $source, [System.Drawing.GraphicsUnit]::Pixel)
}

function Draw-GenericButton([int]$column, [int]$row, $genericSheet)
{
    Draw-Sprite $genericSheet 'generic_button_circle_fill' $column $row
}

$generic = Load-Sheet 'generic_sheet_default.png'
$xbox = Load-Sheet 'xbox-series_sheet_default.png'
$playstation = Load-Sheet 'playstation-series_sheet_default.png'
$nintendo = Load-Sheet 'nintendo-switch_sheet_default.png'

try
{
    for ($button = 1; $button -le $columns; ++$button)
    {
        Draw-GenericButton ($button - 1) 0 $generic
        Draw-GenericButton ($button - 1) 1 $generic
        Draw-GenericButton ($button - 1) 2 $generic
        Draw-GenericButton ($button - 1) 3 $generic
    }

    $xboxNames = @(
        'xbox_button_color_a', 'xbox_button_color_b', 'xbox_button_color_x', 'xbox_button_color_y',
        'xbox_lb', 'xbox_rb', 'xbox_lt', 'xbox_rt', 'xbox_button_back', 'xbox_button_start',
        'xbox_ls', 'xbox_rs'
    )
    for ($i = 0; $i -lt $xboxNames.Count; ++$i) { Draw-Sprite $xbox $xboxNames[$i] $i 1 }

    $playstationNames = @(
        'playstation_button_color_cross', 'playstation_button_color_circle',
        'playstation_button_color_square', 'playstation_button_color_triangle',
        'playstation_trigger_l1', 'playstation_trigger_r1', 'playstation_trigger_l2',
        'playstation_trigger_r2', 'playstation3_button_select', 'playstation3_button_start',
        'playstation_button_l3', 'playstation_button_r3'
    )
    for ($i = 0; $i -lt $playstationNames.Count; ++$i) { Draw-Sprite $playstation $playstationNames[$i] $i 2 }

    $switchNames = @(
        'switch_button_b', 'switch_button_a', 'switch_button_y', 'switch_button_x',
        'switch_button_l', 'switch_button_r', 'switch_button_zl', 'switch_button_zr',
        'switch_button_minus', 'switch_button_plus', 'switch_stick_l_press', 'switch_stick_r_press'
    )
    for ($i = 0; $i -lt $switchNames.Count; ++$i) { Draw-Sprite $nintendo $switchNames[$i] $i 3 }

    $specialNames = @(
        @('generic_button_circle_fill', 'generic_stick_horizontal', 'generic_stick_vertical',
          'generic_stick_horizontal', 'generic_stick_vertical', 'generic_joystick'),
        @('generic_button_circle_fill', 'xbox_stick_l_horizontal', 'xbox_stick_l_vertical',
          'xbox_stick_r_horizontal', 'xbox_stick_r_vertical', 'xbox_dpad'),
        @('generic_button_circle_fill', 'playstation_stick_l_horizontal', 'playstation_stick_l_vertical',
          'playstation_stick_r_horizontal', 'playstation_stick_r_vertical', 'playstation_dpad'),
        @('generic_button_circle_fill', 'switch_stick_l_horizontal', 'switch_stick_l_vertical',
          'switch_stick_r_horizontal', 'switch_stick_r_vertical', 'switch_dpad')
    )
    $specialSheets = @($generic, $xbox, $playstation, $nintendo)
    for ($style = 0; $style -lt 4; ++$style) {
        $specialRow = 4 + $style
        Draw-Sprite $generic 'generic_button_circle_fill' 0 $specialRow
        for ($column = 1; $column -lt 6; ++$column)
        {
            Draw-Sprite $specialSheets[$style] $specialNames[$style][$column] $column $specialRow
        }
        for ($column = 12; $column -lt 16; ++$column) {
            Draw-GenericButton $column $specialRow $generic
        }
    }

    if ($PreviewPath)
    {
        $preview = [System.IO.Path]::GetFullPath($PreviewPath)
        [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($preview)) | Out-Null
        $previewBitmap = [System.Drawing.Bitmap]::new($atlas.Width, $atlas.Height,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $previewGraphics = [System.Drawing.Graphics]::FromImage($previewBitmap)
        $previewGraphics.Clear([System.Drawing.Color]::FromArgb(28, 28, 28))
        $previewGraphics.DrawImageUnscaled($atlas, 0, 0)
        $previewBitmap.Save($preview, [System.Drawing.Imaging.ImageFormat]::Png)
        $previewGraphics.Dispose()
        $previewBitmap.Dispose()
    }

    $output = [System.IO.Path]::GetFullPath($OutputPath)
    $outputDirectory = [System.IO.Path]::GetDirectoryName($output)
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
    $stream = [System.IO.File]::Create($output)
    $writer = [System.IO.BinaryWriter]::new($stream)

    try
    {
        $header = [byte[]]::new(18)
        $header[2] = 2 # Uncompressed true-color TGA.
        $header[12] = [byte](($cellSize * $columns) -band 0xFF)
        $header[13] = [byte](($cellSize * $columns) -shr 8)
        $header[14] = [byte](($cellSize * $rows) -band 0xFF)
        $header[15] = [byte](($cellSize * $rows) -shr 8)
        $header[16] = 32
        $header[17] = 0x28 # Top-left origin and 8 alpha bits.
        $writer.Write($header)

        for ($y = 0; $y -lt ($cellSize * $rows); ++$y)
        {
            for ($x = 0; $x -lt ($cellSize * $columns); ++$x)
            {
                $pixel = $atlas.GetPixel($x, $y)
                $writer.Write([byte]$pixel.B)
                $writer.Write([byte]$pixel.G)
                $writer.Write([byte]$pixel.R)
                $writer.Write([byte]$pixel.A)
            }
        }
    }
    finally
    {
        $writer.Dispose()
        $stream.Dispose()
    }
}
finally
{
    $graphics.Dispose()
    $atlas.Dispose()
    $generic.Bitmap.Dispose()
    $xbox.Bitmap.Dispose()
    $playstation.Bitmap.Dispose()
    $nintendo.Bitmap.Dispose()
}

Write-Output "Wrote $OutputPath (${columns}x${rows} sprites, ${cellSize}px each)."