import fs from 'node:fs';
import path from 'node:path';
import {gzipSync, gunzipSync} from 'node:zlib';

const costumes = [
  ['ops2', 'chhuk'], ['gamespy', 'chhqq'], ['13emerue', 'chhpp'], ['psm', 'chhoo'],
  ['opm', 'chhnn'], ['gamepro', 'chhmm'], ['gameinformer', 'chhll'], ['ign', 'chhkk'],
  ['gmr', 'chhjj'], ['play', 'chhii'], ['gn', 'chhhh'], ['ps2ro', 'chhgg'],
  ['gamereactor', 'chhff'], ['egm', 'chhee'],
];

function fail(message) { throw new Error(message); }
function align(value, boundary) { return Math.ceil(value / boundary) * boundary; }
function readU32(buffer, offset) { return buffer.readUInt32LE(offset); }
function writeU32(buffer, offset, value) {
  if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) fail('Archive field exceeds the 32-bit format limit.');
  buffer.writeUInt32LE(value, offset);
}
function writeU16(buffer, offset, value) {
  if (!Number.isInteger(value) || value < 0 || value > 0xffff) fail('Catalog field exceeds the 16-bit format limit.');
  buffer.writeUInt16LE(value, offset);
}

function readRecords(catalogBytes) {
  if (catalogBytes.readUInt32LE(0) !== 0x20030417) fail('Unsupported master catalog signature.');
  const clusters = [], files = [], records = [];
  let counts = null;
  for (let offset = 16; offset < catalogBytes.length;) {
    if (offset + 4 > catalogBytes.length) fail('Truncated master catalog record.');
    const type = catalogBytes.readUInt16LE(offset), size = catalogBytes.readUInt16LE(offset + 2);
    if (size < 4 || offset + size > catalogBytes.length) fail('Invalid master catalog record size.');
    const raw = Buffer.from(catalogBytes.subarray(offset, offset + size));
    if (type === 1) counts = {offset, raw};
    else if (type === 2) clusters.push({offset, raw, name: raw.subarray(8).toString('utf8').split('\0')[0], count: readU32(raw, 4)});
    else if (type === 3) files.push({offset, raw, index: raw.readUInt16LE(4), cluster: raw.readUInt16LE(6), name: raw.subarray(8).toString('utf8').split('\0')[0]});
    else fail(`Unknown master catalog record ${type}.`);
    records.push(raw);
    offset += size;
  }
  if (!counts || clusters.length !== readU32(counts.raw, 4) || files.length !== readU32(counts.raw, 8))
    fail('Master catalog record counts do not match its header.');
  for (const file of files) {
    if (!clusters[file.cluster] || file.index >= clusters[file.cluster].count) fail('Master catalog contains an invalid cluster entry.');
  }
  return {clusters, files, records, counts};
}

function catalogWithAdditions(input, additions, clusterIndex) {
  const parsed = readRecords(gunzipSync(input, {maxOutputLength: 16 * 1024 * 1024}));
  const cluster = parsed.clusters[clusterIndex];
  if (!cluster || cluster.name.toLowerCase() !== 'chrpl') fail('The expected chrpl catalog cluster is missing.');
  const existing = new Set(parsed.files.map(file => file.name.toLowerCase()));
  for (const item of additions) if (existing.has(item.name.toLowerCase())) fail(`Catalog path already exists: ${item.name}`);
  const totalCount = parsed.files.length + additions.length;
  if (totalCount > 0xffff) fail('Master catalog file count exceeds its format limit.');

  const counts = Buffer.from(parsed.counts.raw);
  writeU32(counts, 8, totalCount);
  const clusterRecord = Buffer.from(cluster.raw);
  writeU32(clusterRecord, 4, cluster.count + additions.length);
  const files = parsed.records.map(record => {
    const type = record.readUInt16LE(0);
    if (type === 1) return counts;
    if (type === 2 && record === cluster.raw) return clusterRecord;
    return record;
  });
  const appended = additions.map((item, i) => {
    const name = Buffer.from(item.name + '\0', 'utf8');
    const result = Buffer.alloc(8 + name.length);
    writeU16(result, 0, 3); writeU16(result, 2, result.length);
    writeU16(result, 4, cluster.count + i); writeU16(result, 6, clusterIndex);
    name.copy(result, 8);
    return result;
  });
  const output = Buffer.concat([Buffer.alloc(16), ...files, ...appended]);
  writeU32(output, 0, 0x20030417); writeU32(output, 4, 0); writeU32(output, 8, 0); writeU32(output, 12, 0);
  return gzipSync(output, {level: 9, mtime: 0});
}

function appendArc(input, additions) {
  if (readU32(input, 0) !== 0x20030507) fail('Unsupported chrpl ARC signature.');
  const count = readU32(input, 4), extra = readU32(input, 8), tableEnd = 16 + count * 16;
  if (!count || count + additions.length > 1000 || tableEnd + extra > input.length) fail('Invalid chrpl ARC directory.');
  const records = [];
  for (let i = 0; i < count; i++) {
    const at = 16 + i * 16;
    const offset = readU32(input, at), blockTable = readU32(input, at + 4), size = readU32(input, at + 8), size2 = readU32(input, at + 12);
    if (offset < tableEnd + extra || offset + size > input.length || size !== size2)
      fail('The chrpl ARC contains a compressed or invalid entry; refusing to rebuild it.');
    records.push({offset, blockTable, size, size2});
  }
  const largest = Math.max(...additions.map(item => item.data.length));
  const blockBytes = align(Math.ceil(largest / 65536) * 4, 16);
  const newCount = count + additions.length, tableShift = additions.length * 16;
  const newExtra = extra + blockBytes;
  const oldDirectoryEnd = tableEnd + extra;
  const newDirectoryEnd = 16 + newCount * 16 + newExtra;
  const directoryShift = newDirectoryEnd - oldDirectoryEnd;
  let appendCursor = align(input.length + directoryShift, 16);
  for (const item of additions) appendCursor = align(appendCursor, 16) + item.data.length;
  const output = Buffer.alloc(appendCursor);
  writeU32(output, 0, 0x20030507); writeU32(output, 4, newCount); writeU32(output, 8, newExtra); writeU32(output, 12, readU32(input, 12));

  input.copy(output, tableEnd + tableShift, tableEnd, oldDirectoryEnd);
  input.copy(output, oldDirectoryEnd + directoryShift, oldDirectoryEnd);
  for (let i = 0; i < count; i++) {
    const entry = records[i], tableAt = 16 + i * 16;
    writeU32(output, tableAt, entry.offset + directoryShift);
    writeU32(output, tableAt + 4, entry.blockTable + tableShift);
    writeU32(output, tableAt + 8, entry.size); writeU32(output, tableAt + 12, entry.size2);
  }

  const sharedBlockTable = tableEnd + tableShift + extra;
  let cursor = align(input.length + directoryShift, 16);
  for (let i = 0; i < additions.length; i++) {
    const item = additions[i];
    cursor = align(cursor, 16);
    const tableAt = 16 + (count + i) * 16;
    writeU32(output, tableAt, cursor); writeU32(output, tableAt + 4, sharedBlockTable);
    writeU32(output, tableAt + 8, item.data.length); writeU32(output, tableAt + 12, item.data.length);
    item.data.copy(output, cursor); cursor += item.data.length;
  }
  return output;
}

function main() {
  const [gameDirectory, modelDirectory, outputDirectory] = process.argv.slice(2);
  if (!gameDirectory || !modelDirectory || !outputDirectory) {
    console.error('Usage: node tools/prepare-ps2-costume-archives.mjs <game-folder> <pc-model-folder> <output-folder>');
    process.exitCode = 2; return;
  }
  const catalogPath = path.join(gameDirectory, 'data', 'arc.arc');
  const archivePath = path.join(gameDirectory, 'data', 'chrpl.arc');
  const catalog = readRecords(gunzipSync(fs.readFileSync(catalogPath), {maxOutputLength: 16 * 1024 * 1024}));
  const clusterIndex = catalog.clusters.findIndex(item => item.name.toLowerCase() === 'chrpl');
  if (clusterIndex < 0) fail('The PC master catalog has no chrpl cluster.');
  const files = catalog.files.filter(file => file.cluster === clusterIndex).sort((a, b) => a.index - b.index);
  if (files.length !== catalog.clusters[clusterIndex].count || files.some((file, i) => file.index !== i)) fail('chrpl catalog entries are not a contiguous archive index.');
  const additions = [];
  for (const [key, sourceName] of costumes) {
    const primary = path.join(modelDirectory, `${sourceName}_pc_prototype.mdl`);
    const alternate = path.join(modelDirectory, `${sourceName}_pc_chhaa_prototype.mdl`);
    for (const [suffix, source] of [['', primary], ['r', alternate]]) {
      if (!fs.existsSync(source)) fail(`Missing converted model: ${source}`);
      const data = fs.readFileSync(source);
      if (!data.length || data.length > 256 * 1024 * 1024) fail(`Invalid converted model size: ${source}`);
      additions.push({name: `data/pcchr/pl/sh3e_${suffix}${key}.mdl`, data, key});
    }
  }
  const lowerNames = new Set(catalog.files.map(file => file.name.toLowerCase()));
  for (const item of additions) if (lowerNames.has(item.name.toLowerCase())) fail(`Archive path already exists: ${item.name}`);

  const catalogBytes = fs.readFileSync(catalogPath), archiveBytes = fs.readFileSync(archivePath);
  const newArchive = appendArc(archiveBytes, additions);
  const newCatalog = catalogWithAdditions(catalogBytes, additions, clusterIndex);
  fs.mkdirSync(outputDirectory, {recursive: true});
  fs.writeFileSync(path.join(outputDirectory, 'chrpl.arc'), newArchive);
  fs.writeFileSync(path.join(outputDirectory, 'arc.arc'), newCatalog);
  fs.writeFileSync(path.join(outputDirectory, 'costume-paths.json'), JSON.stringify(additions.map((item, index) => ({index, key: item.key, path: item.name})), null, 2));
  console.log(`Prepared ${additions.length} appended costume models for chrpl.arc and arc.arc.`);
}

try { main(); }
catch (error) { console.error(error.stack || error.message); process.exitCode = 1; }
