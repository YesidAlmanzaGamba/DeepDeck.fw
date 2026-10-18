// Put the latest firmware image into the Command Center bundle (src-tauri/firmware/), so the app's
// Update page can install it without browsing for a file. Run before `tauri build` (tauri.conf).
// Without a firmware build the app is still built, just without a bundled image.
const fs = require('fs');
const path = require('path');

const source = path.join(__dirname, '..', '..', 'firmware', 'build', 'DeepDeck.bin');
const targetDir = path.join(__dirname, '..', 'src-tauri', 'firmware');

fs.mkdirSync(targetDir, { recursive: true });
for (const f of fs.readdirSync(targetDir)) {
  if (f.endsWith('.bin')) fs.unlinkSync(path.join(targetDir, f));
}
if (fs.existsSync(source)) {
  fs.copyFileSync(source, path.join(targetDir, 'DeepDeck.bin'));
  console.log(`bundled firmware: ${source} (${fs.statSync(source).size} bytes)`);
} else {
  console.log(`no firmware at ${source}: the app is built without a bundled firmware`);
}
