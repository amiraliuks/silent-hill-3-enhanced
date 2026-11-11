# SH3 Enhanced font pack

The ASI can read its own font pack format from:

```text
SH3 Enhanced Data\Fonts\fontdata.sh3f
```

The file is optional. If it is missing or invalid, the plugin leaves the game's original font and texture cache in use. `FontScale=0` skips the font runtime. Values `2` and `4` select the output coverage scale; the pack stores glyph coverage at 4× so both settings use the same source art. The current reader accepts format versions 1 and 2 and pack files up to 256 MiB.

## Binary layout

All integers are little-endian. The file contains no padding between records.

### Header (20 bytes)

| Offset | Type | Value |
| --- | --- | --- |
| 0 | 8 bytes | ASCII `SH3EFNT1` |
| 8 | `uint32` | Format version: `1` (shared glyph records) or `2` (language-bank records) |
| 12 | `uint32` | Source coverage scale: `4` |
| 16 | `uint32` | Number of glyph records, at most 65,536 |

### Glyph record

Each record starts with an 8-byte header followed by its coverage bytes:

| Field | Type | Meaning |
| --- | --- | --- |
| glyph ID | `uint16` | Character key passed by the game's font cache |
| font style ID | `uint8` | `0` for Normal, `1` for Small |
| language bank ID | `uint8` | Version 1 ignores this byte and treats the record as shared. Version 2: `0` Korean, `1` Japanese/Western, `2` Chinese, `255` shared |
| width | `uint8` | Logical glyph width in pixels, from 1 to 21 |
| height | `uint8` | Logical glyph height in pixels, from 1 to 30 |
| reserved | 2 bytes | Set to zero |
| coverage | `width × height × 16` bytes | Row-major 8-bit alpha coverage at 4× resolution |

The coverage array is `width * 4` pixels wide and `height * 4` pixels tall. Zero is transparent and 255 is fully covered. The 4× raster keeps the original glyph advance and placement metrics; the pack changes coverage only. The runtime selects version 2 records using the game's active language bank. Version 1 records remain shared across banks. Missing records continue to use the game's own bitmap.

The runtime scales the 4× source to the selected 2× or 4× cache and preserves the original font for glyphs not present in the pack.

The repository's font-pack tools package the manually authored Latin masks for all three language banks and vector-rasterized Japanese, Hangul, and Traditional Chinese glyphs. The CJK tool maps Japanese glyph IDs through the game's Shift-JIS order (starting at glyph 224; the special `0x8140` character is excluded) and maps Korean/Chinese glyph IDs through the language-bank offsets confirmed in `msg.arc`. It draws each Unicode character into the original 20×30 or 16×24 cell at 4× coverage, while leaving Korean/Chinese special private-use symbols in their original game artwork.

To generate CJK rasters, use the official Noto Serif CJK 2.003 KR and TC Medium subset fonts. Extract the fonts from the official release and run Windows PowerShell 5.1:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\render-cjk-fonts.ps1 `
  -MessageArchive "<game-folder>\data\msg.arc" `
  -RasterRoot "build\font-previews\book-antiqua-raster-allbanks" `
  -JapaneseFontPath "<Noto Serif CJK extraction>\JP\SubsetOTF\JP\NotoSerifJP-Medium.otf" `
  -KoreanFontPath "<Noto Serif CJK extraction>\KR\SubsetOTF\KR\NotoSerifKR-Medium.otf" `
  -ChineseFontPath "<Noto Serif CJK extraction>\TC\SubsetOTF\TC\NotoSerifTC-Medium.otf"
```

The raster root must already contain the six Latin raster directories. Then create the pack with the same archive and raster root:

```powershell
tools\build-font-pack.ps1 `
  -RasterRoot "build\font-previews\book-antiqua-raster-allbanks" `
  -MessageArchive "<game-folder>\data\msg.arc" `
  -RasterizedCjk `
  -OutputPath "build\fontdata.sh3f"
```

The CJK rasterizer uses the original archive only to enumerate glyph IDs and preserve special glyphs; it does not modify `msg.arc` or package its contents. Noto Serif CJK is licensed under the SIL Open Font License 1.1. The font files are build inputs and are not copied into the game's data folder. The source license is kept under `third_party/fonts/noto-cjk/`; builds and installs place its notice at `SH3 Enhanced Data/Licenses/Noto-Serif-CJK-OFL.txt`.
