using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

internal sealed class ArchiveRecord
{
    public ushort Type;
    public byte[] Data;
    public string Name;
    public uint Count;
    public ushort Index;
    public ushort Cluster;
}

internal sealed class CostumeModel
{
    public string Key;
    public string SourceName;
    public string ArchiveName;
    public long Length;
    public long Offset;
}

internal static class BuildPS2CostumeArchives
{
    private static readonly string[,] Costumes = {
        { "ops2", "chhuk" }, { "gamespy", "chhqq" }, { "13emerue", "chhpp" },
        { "psm", "chhoo" }, { "opm", "chhnn" }, { "gamepro", "chhmm" },
        { "gameinformer", "chhll" }, { "ign", "chhkk" }, { "gmr", "chhjj" },
        { "play", "chhii" }, { "gn", "chhhh" }, { "ps2ro", "chhgg" },
        { "gamereactor", "chhff" }, { "egm", "chhee" }
    };

    private static void Fail(string message) { throw new InvalidDataException(message); }
    private static ushort U16(byte[] b, int p) { return BitConverter.ToUInt16(b, p); }
    private static uint U32(byte[] b, int p) { return BitConverter.ToUInt32(b, p); }
    private static void PutU16(byte[] b, int p, ushort v) { Buffer.BlockCopy(BitConverter.GetBytes(v), 0, b, p, 2); }
    private static void PutU32(byte[] b, int p, uint v) { Buffer.BlockCopy(BitConverter.GetBytes(v), 0, b, p, 4); }
    private static long Align(long n, long boundary) { return ((n + boundary - 1) / boundary) * boundary; }

    private static List<ArchiveRecord> ReadCatalog(byte[] bytes, out int countsIndex,
        out int chrplRecordIndex, out ushort chrplClusterId)
    {
        if (bytes.Length < 16 || U32(bytes, 0) != 0x20030417) Fail("Unsupported master catalog signature.");
        var records = new List<ArchiveRecord>();
        countsIndex = -1;
        chrplRecordIndex = -1;
        chrplClusterId = 0;
        ushort clusterOrdinal = 0;
        uint declaredClusters = 0, declaredFiles = 0;
        for (int p = 16; p < bytes.Length;)
        {
            if (p + 4 > bytes.Length) Fail("Truncated master catalog record.");
            ushort type = U16(bytes, p), size = U16(bytes, p + 2);
            if (size < 4 || p + size > bytes.Length) Fail("Invalid master catalog record size.");
            byte[] raw = new byte[size];
            Buffer.BlockCopy(bytes, p, raw, 0, size);
            var record = new ArchiveRecord { Type = type, Data = raw };
            if (type == 1)
            {
                countsIndex = records.Count;
                declaredClusters = U32(raw, 4);
                declaredFiles = U32(raw, 8);
            }
            else if (type == 2)
            {
                record.Count = U32(raw, 4);
                record.Name = ReadCString(raw, 8);
                if (String.Equals(record.Name, "chrpl", StringComparison.OrdinalIgnoreCase))
                {
                    chrplRecordIndex = records.Count;
                    chrplClusterId = clusterOrdinal;
                }
                clusterOrdinal++;
            }
            else if (type == 3)
            {
                record.Index = U16(raw, 4);
                record.Cluster = U16(raw, 6);
                record.Name = ReadCString(raw, 8);
            }
            else Fail("Unknown master catalog record " + type + ".");
            records.Add(record);
            p += size;
        }
        uint actualClusters = 0, actualFiles = 0;
        foreach (ArchiveRecord r in records) { if (r.Type == 2) actualClusters++; if (r.Type == 3) actualFiles++; }
        if (countsIndex < 0 || actualClusters != declaredClusters || actualFiles != declaredFiles)
            Fail("Master catalog record counts do not match its header.");
        if (chrplRecordIndex < 0) Fail("The PC master catalog has no chrpl cluster.");
        var clusters = records.FindAll(x => x.Type == 2);
        foreach (ArchiveRecord r in records)
            if (r.Type == 3 && (r.Cluster >= clusters.Count || r.Index >= clusters[r.Cluster].Count))
                Fail("Master catalog contains an invalid cluster entry.");
        uint chrplCount = records[chrplRecordIndex].Count;
        uint actualChrplFiles = 0;
        var chrplEntries = new List<ArchiveRecord>();
        foreach (ArchiveRecord r in records) if (r.Type == 3 && r.Cluster == chrplClusterId) { actualChrplFiles++; chrplEntries.Add(r); }
        chrplEntries.Sort((a, b) => a.Index.CompareTo(b.Index));
        if (actualChrplFiles != chrplCount) Fail("chrpl catalog entry count does not match its cluster.");
        for (int i = 0; i < chrplEntries.Count; i++) if (chrplEntries[i].Index != i) Fail("chrpl catalog entries are not contiguous.");
        return records;
    }

    private static string ReadCString(byte[] bytes, int offset)
    {
        int end = offset;
        while (end < bytes.Length && bytes[end] != 0) end++;
        return Encoding.UTF8.GetString(bytes, offset, end - offset);
    }

    private static byte[] Gunzip(string path)
    {
        using (var file = File.OpenRead(path))
        using (var gzip = new GZipStream(file, CompressionMode.Decompress))
        using (var output = new MemoryStream())
        {
            byte[] buffer = new byte[65536];
            int read;
            while ((read = gzip.Read(buffer, 0, buffer.Length)) > 0)
            {
                if (output.Length + read > 16 * 1024 * 1024) Fail("Master catalog exceeds the supported size.");
                output.Write(buffer, 0, read);
            }
            return output.ToArray();
        }
    }

    private static List<CostumeModel> GetModels(string modelDirectory)
    {
        var result = new List<CostumeModel>();
        for (int i = 0; i < Costumes.GetLength(0); i++)
        {
            string key = Costumes[i, 0], source = Costumes[i, 1];
            for (int alternate = 0; alternate < 2; alternate++)
            {
                string suffix = alternate == 0 ? "" : "r";
                string fileName = source + (alternate == 0 ? "_pc_prototype.mdl" : "_pc_chhaa_prototype.mdl");
                string path = Path.Combine(modelDirectory, fileName);
                if (!File.Exists(path)) Fail("Missing converted costume model: " + fileName);
                long length = new FileInfo(path).Length;
                if (length <= 0 || length > 256L * 1024 * 1024) Fail("Invalid converted model size: " + fileName);
                result.Add(new CostumeModel { Key = key, SourceName = path, ArchiveName = "data/pcchr/pl/sh3e_" + suffix + key + ".mdl", Length = length });
            }
        }
        return result;
    }

    private static void WriteCatalog(string inputPath, string outputPath, List<CostumeModel> models)
    {
        byte[] decompressed = Gunzip(inputPath);
        int countsIndex, chrplRecordIndex;
        ushort chrplClusterId;
        List<ArchiveRecord> records = ReadCatalog(decompressed, out countsIndex, out chrplRecordIndex, out chrplClusterId);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        uint fileCount = 0;
        foreach (ArchiveRecord r in records) if (r.Type == 3) { names.Add(r.Name); fileCount++; }
        foreach (CostumeModel model in models) if (!names.Add(model.ArchiveName)) Fail("Catalog path already exists: " + model.ArchiveName);
        if (fileCount + models.Count > UInt16.MaxValue) Fail("Master catalog file count exceeds its format limit.");

        byte[] counts = (byte[])records[countsIndex].Data.Clone();
        PutU32(counts, 8, fileCount + (uint)models.Count);
        byte[] cluster = (byte[])records[chrplRecordIndex].Data.Clone();
        PutU32(cluster, 4, records[chrplRecordIndex].Count + (uint)models.Count);

        using (var raw = new MemoryStream())
        using (var writer = new BinaryWriter(raw, Encoding.UTF8, true))
        {
            writer.Write(0x20030417U); writer.Write(0U); writer.Write(0U); writer.Write(0U);
            foreach (ArchiveRecord r in records)
            {
                if (r.Type == 1 && records.IndexOf(r) == countsIndex) writer.Write(counts);
                else if (r.Type == 2 && records.IndexOf(r) == chrplRecordIndex) writer.Write(cluster);
                else writer.Write(r.Data);
            }
            uint nextIndex = records[chrplRecordIndex].Count;
            for (int i = 0; i < models.Count; i++)
            {
                byte[] name = Encoding.UTF8.GetBytes(models[i].ArchiveName + "\0");
                ushort size = checked((ushort)(8 + name.Length));
                writer.Write((ushort)3); writer.Write(size); writer.Write(checked((ushort)(nextIndex + i))); writer.Write(chrplClusterId); writer.Write(name);
            }
            writer.Flush();
            Directory.CreateDirectory(Path.GetDirectoryName(outputPath));
            using (var output = File.Create(outputPath))
            using (var gzip = new GZipStream(output, CompressionLevel.Optimal))
            {
                raw.Position = 0;
                raw.CopyTo(gzip);
            }
        }
    }

    private static void CopyRange(Stream source, Stream destination, long bytes)
    {
        byte[] buffer = new byte[1024 * 1024];
        while (bytes > 0)
        {
            int count = (int)Math.Min(buffer.Length, bytes);
            int read = source.Read(buffer, 0, count);
            if (read == 0) Fail("Unexpected end of chrpl.arc.");
            destination.Write(buffer, 0, read);
            bytes -= read;
        }
    }

    private static void WriteZeros(Stream output, long count)
    {
        byte[] zeros = new byte[4096];
        while (count > 0) { int n = (int)Math.Min(zeros.Length, count); output.Write(zeros, 0, n); count -= n; }
    }

    private static void WriteArchive(string inputPath, string outputPath, List<CostumeModel> models)
    {
        long inputLength = new FileInfo(inputPath).Length;
        using (var input = File.OpenRead(inputPath))
        using (var reader = new BinaryReader(input))
        {
            if (inputLength < 16 || reader.ReadUInt32() != 0x20030507) Fail("Unsupported chrpl ARC signature.");
            uint count = reader.ReadUInt32(), extra = reader.ReadUInt32(), unknown = reader.ReadUInt32();
            long tableEnd = 16L + count * 16L, oldDirectoryEnd = tableEnd + extra;
            if (count == 0 || count + models.Count > 1000 || oldDirectoryEnd > inputLength) Fail("Invalid chrpl ARC directory.");
            var offsets = new uint[count]; var blockTables = new uint[count]; var sizes = new uint[count]; var sizes2 = new uint[count];
            uint largest = 0;
            for (int i = 0; i < count; i++)
            {
                input.Position = 16L + i * 16L;
                offsets[i] = reader.ReadUInt32(); blockTables[i] = reader.ReadUInt32(); sizes[i] = reader.ReadUInt32(); sizes2[i] = reader.ReadUInt32();
                if (offsets[i] < oldDirectoryEnd || (long)offsets[i] + sizes[i] > inputLength || sizes[i] != sizes2[i])
                    Fail("The chrpl ARC contains a compressed or invalid entry; refusing to rebuild it.");
            }
            foreach (CostumeModel m in models) largest = Math.Max(largest, checked((uint)m.Length));
            long blockBytes = Align(((largest + 65535L) / 65536L) * 4L, 16);
            long tableShift = models.Count * 16L;
            long newCount = count + models.Count;
            long newExtra = extra + blockBytes;
            long newDirectoryEnd = 16L + newCount * 16L + newExtra;
            long directoryShift = newDirectoryEnd - oldDirectoryEnd;
            long cursor = Align(inputLength + directoryShift, 16);
            foreach (CostumeModel m in models) { cursor = Align(cursor, 16); m.Offset = cursor; cursor += m.Length; }
            if (cursor > UInt32.MaxValue || newExtra > UInt32.MaxValue) Fail("Prepared archive exceeds the 32-bit format limit.");

            Directory.CreateDirectory(Path.GetDirectoryName(outputPath));
            using (var output = File.Create(outputPath))
            using (var writer = new BinaryWriter(output))
            {
                writer.Write(0x20030507U); writer.Write(checked((uint)newCount)); writer.Write(checked((uint)newExtra)); writer.Write(unknown);
                for (int i = 0; i < count; i++)
                {
                    writer.Write(checked((uint)(offsets[i] + directoryShift)));
                    writer.Write(checked((uint)(blockTables[i] + tableShift)));
                    writer.Write(sizes[i]); writer.Write(sizes2[i]);
                }
                uint newBlockTable = checked((uint)(tableEnd + tableShift + extra));
                for (int i = 0; i < models.Count; i++)
                {
                    writer.Write(checked((uint)models[i].Offset)); writer.Write(newBlockTable);
                    writer.Write(checked((uint)models[i].Length)); writer.Write(checked((uint)models[i].Length));
                }
                input.Position = tableEnd;
                output.Position = 16L + newCount * 16L;
                CopyRange(input, output, extra);
                WriteZeros(output, blockBytes);
                input.Position = oldDirectoryEnd;
                output.Position = oldDirectoryEnd + directoryShift;
                CopyRange(input, output, inputLength - oldDirectoryEnd);
                foreach (CostumeModel m in models)
                {
                    output.Position = m.Offset;
                    using (var model = File.OpenRead(m.SourceName)) CopyRange(model, output, m.Length);
                }
                output.SetLength(cursor);
            }
        }
    }

    public static int Main(string[] args)
    {
        try
        {
            if (args.Length != 3)
            {
                Console.Error.WriteLine("Usage: BuildPS2CostumeArchives.exe <game-folder> <model-folder> <output-folder>");
                return 2;
            }
            string game = Path.GetFullPath(args[0]), modelDir = Path.GetFullPath(args[1]), outputDir = Path.GetFullPath(args[2]);
            string sourceCatalog = Path.Combine(game, "data", "arc.arc"), sourceArchive = Path.Combine(game, "data", "chrpl.arc");
            if (!File.Exists(sourceCatalog) || !File.Exists(sourceArchive)) Fail("Could not find data\\arc.arc and data\\chrpl.arc beside the selected sh3.exe.");
            List<CostumeModel> models = GetModels(modelDir);
            Directory.CreateDirectory(outputDir);
            WriteArchive(sourceArchive, Path.Combine(outputDir, "chrpl.arc"), models);
            WriteCatalog(sourceCatalog, Path.Combine(outputDir, "arc.arc"), models);
            Console.WriteLine("Prepared 28 PS2 costume models using copies of the installed PC archives.");
            return 0;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("Costume archive preparation failed: " + e.Message);
            return 1;
        }
    }
}
