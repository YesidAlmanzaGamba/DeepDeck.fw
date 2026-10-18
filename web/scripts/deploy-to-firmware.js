#!/usr/bin/env node
/*
 * Copies the production build of the web app into firmware/spiffs_image, which the
 * firmware build packs into the www_0 SPIFFS partition.
 *
 * - .js / .css / .svg are gzipped to <name>.gz: the firmware web server falls back to
 *   "<file>.gz" and sends it with Content-Encoding: gzip.
 * - only the files under assets/ that the app actually references are copied.
 * - the output folder is replaced, so files from older builds do not pile up.
 * - fails if a file name is too long for SPIFFS or the image does not fit in www_0.
 *
 * Usage (from web/):  npm run build-deepdeck   (or: node scripts/deploy-to-firmware.js)
 */
'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const WEB_DIR = path.resolve(__dirname, '..');
const DIST_DIR = path.join(WEB_DIR, 'dist', 'esp-frontend');
const FIRMWARE_DIR = path.resolve(WEB_DIR, '..', 'firmware');
const OUT_DIR = path.join(FIRMWARE_DIR, 'spiffs_image');
const PARTITIONS_CSV = path.join(FIRMWARE_DIR, 'partitions.csv');

const GZIP_EXTENSIONS = ['.js', '.css', '.svg'];
const SKIPPED_ROOT_FILES = ['3rdpartylicenses.txt', 'stats.json'];
// CONFIG_SPIFFS_OBJ_NAME_LEN in sdkconfig.defaults, includes the terminating NUL
const SPIFFS_OBJ_NAME_LEN = 32;
// spiffsgen needs room for its own metadata; warn well before the partition is full
const WARN_USAGE = 0.85;

function fail(message) {
  console.error(`\n[deploy-to-firmware] ERROR: ${message}`);
  process.exit(1);
}

function parseSize(text) {
  const match = /^(0x[0-9a-f]+|\d+)\s*([kKmM]?)$/.exec(text.trim());
  if (!match) return NaN;
  const value = match[1].startsWith('0x') ? parseInt(match[1], 16) : parseInt(match[1], 10);
  const unit = match[2].toUpperCase();
  return unit === 'K' ? value * 1024 : unit === 'M' ? value * 1024 * 1024 : value;
}

function readPartitionSize(name) {
  const lines = fs.readFileSync(PARTITIONS_CSV, 'utf8').split(/\r?\n/);
  for (const line of lines) {
    if (line.trim().startsWith('#')) continue;
    const cols = line.split(',').map((c) => c.trim());
    if (cols[0] === name) return parseSize(cols[4]);
  }
  return NaN;
}

function listFiles(dir, base = dir) {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(dir, entry.name);
    return entry.isDirectory() ? listFiles(full, base) : [path.relative(base, full).split(path.sep).join('/')];
  });
}

// assets/... paths referenced by the built html/js/css, plus url(...) references inside those assets
function referencedAssets(rootFiles) {
  const found = new Set();
  const textFiles = rootFiles.filter((f) => /\.(html|js|css)$/.test(f));
  for (const file of textFiles) {
    const text = fs.readFileSync(path.join(DIST_DIR, file), 'utf8');
    for (const match of text.matchAll(/assets\/[A-Za-z0-9_\-./]+/g)) {
      found.add(match[0].replace(/[.)]+$/, ''));
    }
  }
  for (const asset of [...found]) {
    if (!asset.endsWith('.css') || !fs.existsSync(path.join(DIST_DIR, asset))) continue;
    const text = fs.readFileSync(path.join(DIST_DIR, asset), 'utf8');
    for (const match of text.matchAll(/url\(\s*['"]?([^'")]+)['"]?\s*\)/g)) {
      if (/^(data:|https?:|\/)/.test(match[1])) continue;
      found.add(path.posix.normalize(path.posix.join(path.posix.dirname(asset), match[1])));
    }
  }
  return [...found].filter((asset) => {
    const exists = fs.existsSync(path.join(DIST_DIR, asset));
    if (!exists) console.warn(`[deploy-to-firmware] warning: ${asset} is referenced but not in the build`);
    return exists;
  });
}

function main() {
  if (!fs.existsSync(path.join(DIST_DIR, 'index.html'))) {
    fail(`no build found in ${DIST_DIR}. Run "npm run build-prod" first.`);
  }

  const partitionSize = readPartitionSize('www_0');
  if (!partitionSize) fail(`could not read the www_0 size from ${PARTITIONS_CSV}`);

  const allFiles = listFiles(DIST_DIR);
  const rootFiles = allFiles.filter((f) => !f.includes('/') && !SKIPPED_ROOT_FILES.includes(f));
  const files = [...rootFiles, ...referencedAssets(rootFiles)].sort();
  const skipped = allFiles.filter((f) => !files.includes(f) && !SKIPPED_ROOT_FILES.includes(f));

  // Prepare everything in memory first, so a failed check leaves spiffs_image untouched
  const outputs = files.map((file) => {
    const data = fs.readFileSync(path.join(DIST_DIR, file));
    const gzip = GZIP_EXTENSIONS.includes(path.extname(file));
    const name = gzip ? `${file}.gz` : file;
    const content = gzip ? zlib.gzipSync(data, { level: 9 }) : data;
    if (`/${name}`.length > SPIFFS_OBJ_NAME_LEN - 1) {
      fail(`"/${name}" is longer than ${SPIFFS_OBJ_NAME_LEN - 1} characters, SPIFFS cannot store it`);
    }
    return { name, content, original: data.length };
  });

  const total = outputs.reduce((sum, o) => sum + o.content.length, 0);
  if (total > partitionSize) {
    fail(`the web app needs ${total} bytes but www_0 is ${partitionSize} bytes`);
  }

  fs.rmSync(OUT_DIR, { recursive: true, force: true });
  for (const output of outputs) {
    const target = path.join(OUT_DIR, output.name);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, output.content);
  }

  console.log(`\n[deploy-to-firmware] ${path.relative(path.resolve(WEB_DIR, '..'), OUT_DIR)}`);
  for (const o of outputs) {
    console.log(`  ${String(o.content.length).padStart(8)}  ${o.name}${o.content.length !== o.original ? `  (from ${o.original})` : ''}`);
  }
  if (skipped.length) console.log(`  not referenced, skipped: ${skipped.join(', ')}`);
  const usage = total / partitionSize;
  console.log(`  total ${total} of ${partitionSize} bytes in www_0 (${(usage * 100).toFixed(1)}%)`);
  if (usage > WARN_USAGE) {
    console.warn(`  warning: above ${WARN_USAGE * 100}%, the SPIFFS image may not fit once metadata is added`);
  }
}

main();
