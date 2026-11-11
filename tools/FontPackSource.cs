using System;
using System.Collections.Generic;
using System.IO;

public static class FontPackSource
{
    private static readonly byte[] CoverageLevels = { 0, 95, 127, 159, 191, 223, 255 };

    private sealed class Glyph
    {
        public int Id;
        public int Width;
        public int Height;
        public int Offset;
        public int End;
    }

    private sealed class BankFont
    {
        public byte[] Data;
        public int Start;
        public int End;
        public int Style;
        public int Bank;
        public int Width;
        public int Height;
        public List<Glyph> Glyphs;
    }

    public static int CountCjkRecords(string messageArchive)
    {
        var count = 0;
        foreach (var bank in new[] { 0, 2 })
            foreach (var style in new[] { 0, 1 })
                count += ReadBankFont(ReadArcFontData(messageArchive, bank), bank, style).Glyphs.Count;
        return count;
    }

    public static int WriteCjkRecords(BinaryWriter writer, string messageArchive)
    {
        var count = 0;
        foreach (var bank in new[] { 0, 2 })
        {
            var data = ReadArcFontData(messageArchive, bank);
            foreach (var style in new[] { 0, 1 })
            {
                var font = ReadBankFont(data, bank, style);
                foreach (var glyph in font.Glyphs)
                {
                    var source = DecodeGlyph(font, glyph);
                    var coverage = UpscaleBilinear(source, font.Width, font.Height, 4);
                    writer.Write((ushort)glyph.Id);
                    writer.Write((byte)style);
                    writer.Write((byte)bank);
                    writer.Write((byte)font.Width);
                    writer.Write((byte)font.Height);
                    writer.Write(new byte[2]);
                    writer.Write(coverage);
                    count++;
                }
            }
        }
        return count;
    }

    public static int CountCjkSupplementalRecords(string messageArchive)
    {
        var count = 0;
        foreach (var bank in new[] { 0, 2 })
            foreach (var style in new[] { 0, 1 })
                foreach (var glyph in ReadBankFont(ReadArcFontData(messageArchive, bank), bank, style).Glyphs)
                    if (!HasUnicodeReplacement(bank, glyph.Id))
                        count++;
        return count;
    }

    public static int WriteCjkSupplementalRecords(BinaryWriter writer, string messageArchive)
    {
        var count = 0;
        foreach (var bank in new[] { 0, 2 })
        {
            var data = ReadArcFontData(messageArchive, bank);
            foreach (var style in new[] { 0, 1 })
            {
                var font = ReadBankFont(data, bank, style);
                foreach (var glyph in font.Glyphs)
                {
                    if (HasUnicodeReplacement(bank, glyph.Id))
                        continue;

                    var source = DecodeGlyph(font, glyph);
                    var coverage = UpscaleBilinear(source, font.Width, font.Height, 4);
                    writer.Write((ushort)glyph.Id);
                    writer.Write((byte)style);
                    writer.Write((byte)bank);
                    writer.Write((byte)font.Width);
                    writer.Write((byte)font.Height);
                    writer.Write(new byte[2]);
                    writer.Write(coverage);
                    count++;
                }
            }
        }
        return count;
    }

    private static bool HasUnicodeReplacement(int bank, int glyphId)
    {
        var codepoint = bank == 0 ? glyphId + 0xAB20 : glyphId + 0x4D20;
        if (bank == 0)
            return codepoint >= 0xAC00 && codepoint <= 0xD7A3;
        return (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||
            (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
            (codepoint >= 0xF900 && codepoint <= 0xFAFF);
    }

    private static byte[] ReadArcFontData(string path, int bank)
    {
        var entryIndex = bank == 0 ? 277 : 343;
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
            reader.ReadUInt32(); // native block-table offset
            var size = reader.ReadUInt32();
            var unpackedSize = reader.ReadUInt32();
            if (size != unpackedSize || offset < 16L + entryCount * 16L || offset + size > stream.Length || size > int.MaxValue)
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

        var glyphs = new List<Glyph>();
        for (var id = 224; id < count; id++)
        {
            var offset = OffsetFor(data, start, pages, id);
            if (offset == 0)
                continue;
            if (offset < firstOffset || start + offset >= end)
                throw new InvalidDataException("A CJK glyph points outside its language font section.");
            glyphs.Add(new Glyph { Id = id, Offset = offset, Width = style == 0 ? 20 : 16, Height = style == 0 ? 30 : 24 });
        }

        for (var i = 0; i < glyphs.Count; i++)
            glyphs[i].End = i + 1 < glyphs.Count ? glyphs[i + 1].Offset : end - start;

        return new BankFont
        {
            Data = data,
            Start = start,
            End = end,
            Style = style,
            Bank = bank,
            Width = style == 0 ? 20 : 16,
            Height = style == 0 ? 30 : 24,
            Glyphs = glyphs
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

    private static byte[] DecodeGlyph(BankFont font, Glyph glyph)
    {
        var pixels = new byte[glyph.Width * glyph.Height];
        var bit = 0;
        var cursor = 0;
        while (cursor < pixels.Length)
        {
            var value = Read3(font, glyph, ref bit);
            if (value < 7)
            {
                pixels[cursor++] = CoverageLevels[value];
                continue;
            }

            var run = Read3(font, glyph, ref bit);
            if (run == 0)
            {
                run = Read3(font, glyph, ref bit);
                if (run != 0)
                    run += 7;
                else
                {
                    run = Read3(font, glyph, ref bit);
                    if (run != 0)
                        run += 14;
                    else
                    {
                        run = Read3(font, glyph, ref bit) | (Read3(font, glyph, ref bit) << 3);
                        if (run != 0)
                            run += 21;
                        else
                            run = 84 + Read3(font, glyph, ref bit) +
                                (Read3(font, glyph, ref bit) << 3) +
                                (Read3(font, glyph, ref bit) << 6);
                    }
                }
            }
            run++;
            if (cursor + run > pixels.Length)
                throw new InvalidDataException("An encoded CJK blank run exceeds the glyph bounds.");
            cursor += run;
        }
        return pixels;
    }

    private static int Read3(BankFont font, Glyph glyph, ref int bit)
    {
        var offset = glyph.Offset + (bit >> 3);
        if (offset >= glyph.End)
            throw new InvalidDataException("A CJK glyph bitstream is truncated.");
        var shift = bit & 7;
        var value = (int)font.Data[font.Start + offset];
        if (shift > 5)
        {
            if (offset + 1 >= glyph.End)
                throw new InvalidDataException("A CJK glyph bitstream is truncated.");
            value |= font.Data[font.Start + offset + 1] << 8;
        }
        bit += 3;
        return (value >> shift) & 7;
    }

    private static byte[] UpscaleBilinear(byte[] source, int width, int height, int scale)
    {
        var output = new byte[width * height * scale * scale];
        for (var y = 0; y < height * scale; y++)
        {
            var sourceY = Math.Max(0.0, Math.Min(height - 1.0, (y + 0.5) / scale - 0.5));
            var y0 = (int)Math.Floor(sourceY);
            var y1 = Math.Min(y0 + 1, height - 1);
            var fy = sourceY - y0;
            for (var x = 0; x < width * scale; x++)
            {
                var sourceX = Math.Max(0.0, Math.Min(width - 1.0, (x + 0.5) / scale - 0.5));
                var x0 = (int)Math.Floor(sourceX);
                var x1 = Math.Min(x0 + 1, width - 1);
                var fx = sourceX - x0;
                var top = source[y0 * width + x0] * (1 - fx) + source[y0 * width + x1] * fx;
                var bottom = source[y1 * width + x0] * (1 - fx) + source[y1 * width + x1] * fx;
                output[y * width * scale + x] = (byte)Math.Max(0, Math.Min(255, Math.Round(top * (1 - fy) + bottom * fy)));
            }
        }
        return output;
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
