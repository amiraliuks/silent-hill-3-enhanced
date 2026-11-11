using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Text;

public static class CjkFontRasterizer
{
    private sealed class BankFont
    {
        public int Width;
        public int Height;
        public List<int> GlyphIds;
    }

    public static int RenderRasters(string messageArchive, string japaneseFontPath,
        string koreanFontPath, string chineseFontPath, string rasterRoot)
    {
        if (!File.Exists(messageArchive))
            throw new FileNotFoundException("The SH3 message archive was not found.", messageArchive);
        if (!File.Exists(japaneseFontPath))
            throw new FileNotFoundException("The Japanese font was not found.", japaneseFontPath);
        if (!File.Exists(koreanFontPath))
            throw new FileNotFoundException("The Korean font was not found.", koreanFontPath);
        if (!File.Exists(chineseFontPath))
            throw new FileNotFoundException("The Traditional Chinese font was not found.", chineseFontPath);

        Directory.CreateDirectory(rasterRoot);
        using (var japaneseFonts = new PrivateFontCollection())
        using (var koreanFonts = new PrivateFontCollection())
        using (var chineseFonts = new PrivateFontCollection())
        {
            japaneseFonts.AddFontFile(japaneseFontPath);
            koreanFonts.AddFontFile(koreanFontPath);
            chineseFonts.AddFontFile(chineseFontPath);
            if (japaneseFonts.Families.Length == 0 || koreanFonts.Families.Length == 0 ||
                chineseFonts.Families.Length == 0)
                throw new InvalidDataException("A CJK font file could not be loaded.");

            var count = 0;
            var japaneseGlyphs = BuildShiftJisGlyphMap();
            foreach (var bank in new[] { 1, 0, 2 })
            {
                var fontData = ReadArcFontData(messageArchive, bank);
                var family = bank == 1 ? japaneseFonts.Families[0] :
                    bank == 0 ? koreanFonts.Families[0] : chineseFonts.Families[0];
                foreach (var style in new[] { 0, 1 })
                {
                    var font = ReadBankFont(fontData, bank, style);
                    var directory = StyleDirectory(rasterRoot, bank, style);
                    Directory.CreateDirectory(directory);

                    foreach (var glyphId in font.GlyphIds)
                    {
                        int codepoint;
                        var mapped = bank == 1
                            ? japaneseGlyphs.TryGetValue(glyphId, out codepoint)
                            : TryMapCodepoint(bank, glyphId, out codepoint);
                        if (!mapped)
                            continue;

                        var path = Path.Combine(directory, "glyph_" + glyphId + ".png");
                        RenderGlyph(family, codepoint, font.Width, font.Height, path);
                        count++;
                    }
                }
            }
            return count;
        }
    }

    // The PC font bank uses dense glyph IDs in Shift-JIS lead/trail order. The
    // PS2 dictionary confirms that 0x8141 starts at glyph 224 and 0x829F is
    // glyph 432; 0x8140 is a special control glyph and is not in that sequence.
    private static Dictionary<int, int> BuildShiftJisGlyphMap()
    {
        var map = new Dictionary<int, int>();
        var encoding = Encoding.GetEncoding(932, EncoderFallback.ExceptionFallback,
            DecoderFallback.ExceptionFallback);
        var glyphId = 224;
        var leads = new List<int>();
        for (var lead = 0x81; lead <= 0x9F; lead++)
            leads.Add(lead);
        for (var lead = 0xE0; lead <= 0xEA; lead++)
            leads.Add(lead);

        foreach (var lead in leads)
        {
            for (var trail = 0x40; trail <= 0xFC; trail++)
            {
                if (trail == 0x7F || (lead == 0x81 && trail == 0x40))
                    continue;

                string character;
                try
                {
                    character = encoding.GetString(new[] { (byte)lead, (byte)trail });
                }
                catch (DecoderFallbackException)
                {
                    continue;
                }

                if (character.Length != 1 || character[0] == '\uFFFD')
                    continue;

                map[glyphId++] = character[0];
            }
        }
        return map;
    }

    private static string StyleDirectory(string rasterRoot, int bank, int style)
    {
        var bankName = bank == 1 ? "j" : bank == 0 ? "k" : "c";
        var styleName = style == 0 ? "normal" : "small";
        return Path.Combine(rasterRoot, bankName + "_" + styleName);
    }

    private static void RenderGlyph(FontFamily family, int codepoint, int width, int height, string outputPath)
    {
        const int scale = 4;
        var pixelWidth = width * scale;
        var pixelHeight = height * scale;
        using (var bitmap = new Bitmap(pixelWidth, pixelHeight, PixelFormat.Format32bppArgb))
        using (var graphics = Graphics.FromImage(bitmap))
        using (var path = new GraphicsPath())
        using (var brush = new SolidBrush(Color.White))
        {
            graphics.Clear(Color.Transparent);
            graphics.SmoothingMode = SmoothingMode.AntiAlias;
            graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
            graphics.CompositingQuality = CompositingQuality.HighQuality;

            var emSize = (float)pixelWidth;
            path.AddString(Char.ConvertFromUtf32(codepoint), family, (int)FontStyle.Regular,
                emSize, new PointF(0, 0), StringFormat.GenericTypographic);
            var bounds = path.GetBounds();
            if (bounds.Width <= 0 || bounds.Height <= 0)
                throw new InvalidDataException("The CJK font produced an empty glyph for U+" + codepoint.ToString("X4") + ".");

            var fitScale = Math.Min(1f, Math.Min((pixelWidth - 2f) / bounds.Width,
                (pixelHeight - 2f) / bounds.Height));
            if (fitScale < 1f)
            {
                path.Reset();
                path.AddString(Char.ConvertFromUtf32(codepoint), family, (int)FontStyle.Regular,
                    emSize * fitScale, new PointF(0, 0), StringFormat.GenericTypographic);
                bounds = path.GetBounds();
            }

            graphics.TranslateTransform((pixelWidth - bounds.Width) / 2f - bounds.X,
                (pixelHeight - bounds.Height) / 2f - bounds.Y);
            graphics.FillPath(brush, path);
            graphics.ResetTransform();
            bitmap.Save(outputPath, ImageFormat.Png);
        }
    }

    private static bool TryMapCodepoint(int bank, int glyphId, out int codepoint)
    {
        codepoint = bank == 0 ? glyphId + 0xAB20 : glyphId + 0x4D20;
        if (bank == 0)
            return codepoint >= 0xAC00 && codepoint <= 0xD7A3;
        return (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||
            (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
            (codepoint >= 0xF900 && codepoint <= 0xFAFF);
    }

    private static byte[] ReadArcFontData(string path, int bank)
    {
        var entryIndex = bank == 1 ? 276 : bank == 0 ? 277 : 343;
        using (var stream = File.OpenRead(path))
        using (var reader = new BinaryReader(stream))
        {
            if (stream.Length < 16 || reader.ReadUInt32() != 0x20030507)
                throw new InvalidDataException("The source is not the expected Silent Hill 3 PC ARC archive.");
            var entryCount = reader.ReadUInt32();
            if (entryCount <= entryIndex || 16L + entryCount * 16L > stream.Length)
                throw new InvalidDataException("The message archive entry table is truncated or unsupported.");

            stream.Position = 16L + entryIndex * 16L;
            var offset = reader.ReadUInt32();
            reader.ReadUInt32();
            var size = reader.ReadUInt32();
            var unpackedSize = reader.ReadUInt32();
            if (size != unpackedSize || offset < 16L + entryCount * 16L ||
                offset + size > stream.Length || size > int.MaxValue)
                throw new InvalidDataException("The selected language font is compressed or outside the archive bounds.");

            stream.Position = offset;
            var data = reader.ReadBytes((int)size);
            if (data.Length != size)
                throw new EndOfStreamException("The language font ended unexpectedly.");
            return data;
        }
    }

    private static BankFont ReadBankFont(byte[] data, int bank, int style)
    {
        if (data.Length < 16 || U32(data, 0) != 16)
            throw new InvalidDataException("The source entry is not an SH3 PC fontdata BIN.");
        var start = checked((int)U32(data, style * 4));
        var end = checked((int)U32(data, style * 4 + 4));
        if (start < 16 || end <= start || end > data.Length || start + 240 > end)
            throw new InvalidDataException("The language font section is invalid.");

        var pages = new ushort[8];
        for (var page = 0; page < pages.Length; page++)
            pages[page] = U16(data, start + page * 2);

        var firstOffset = int.MaxValue;
        for (var id = 0; id < 224; id++)
        {
            var offset = OffsetFor(data, start, pages, id);
            if (offset != 0)
                firstOffset = Math.Min(firstOffset, offset);
        }
        if (firstOffset == int.MaxValue || firstOffset < 240 || (firstOffset - 240) % 2 != 0)
            throw new InvalidDataException("The language font glyph table could not be sized.");
        var count = (firstOffset - 240) / 2;
        if (start + 240L + count * 2L > end)
            throw new InvalidDataException("The language font glyph table exceeds its section.");

        var ids = new List<int>();
        for (var id = 224; id < count; id++)
            if (OffsetFor(data, start, pages, id) != 0)
                ids.Add(id);

        return new BankFont
        {
            Width = style == 0 ? 20 : 16,
            Height = style == 0 ? 30 : 24,
            GlyphIds = ids
        };
    }

    private static int OffsetFor(byte[] data, int start, ushort[] pages, int id)
    {
        var stored = U16(data, start + 240 + id * 2);
        if (stored == 0)
            return 0;
        var page = 0;
        for (var index = 0; index < pages.Length; index++)
            if (pages[index] != 0 && pages[index] <= id)
                page = index + 1;
        return stored * 4 + page * 0x40000;
    }

    private static ushort U16(byte[] data, int offset)
    {
        if (offset < 0 || offset + 2 > data.Length)
            throw new InvalidDataException("Font table is truncated.");
        return BitConverter.ToUInt16(data, offset);
    }

    private static uint U32(byte[] data, int offset)
    {
        if (offset < 0 || offset + 4 > data.Length)
            throw new InvalidDataException("Font section header is truncated.");
        return BitConverter.ToUInt32(data, offset);
    }
}
