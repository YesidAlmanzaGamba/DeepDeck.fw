# Ship the current web app in the firmware

- **Status:** done
- **Roadmap item:** R1.1
- **Areas:** both

## Goal

The device serves the web app from `firmware/spiffs_image`, packed into the `www_0` SPIFFS partition.
That folder still held a 2023 build, so the gesture and mouse UI removed from `web/` was still on the
device. One command should rebuild the web app and replace the image, on Windows and elsewhere.

## Scope

In scope: build script, image size, regenerating `spiffs_image`.
Out of scope: changing the partition table, and the "Color" tab, which is R1.2.

## Design

- The firmware server (`rest_common_get_handler` in `server.c`) serves `<path>`, and falls back to
  `<path>.gz` with `Content-Encoding: gzip`. The content type comes from the original extension, so
  js/css/svg can be stored gzipped.
- `web/scripts/deploy-to-firmware.js` uses only Node built-ins, so there are no new dependencies. It:
  - copies the root files of `dist/esp-frontend`
  - copies only the `assets/...` files referenced by the built html/js/css, plus the `url()`s inside
    those css files
  - gzips js, css and svg
  - checks SPIFFS name length (31 characters) and the `www_0` size read from `partitions.csv`
  - prepares everything in memory, so a failed check leaves `spiffs_image` untouched
  - then replaces the folder
- `npm run build-deepdeck` = `build-prod` + `deploy-to-firmware`. It replaces the old
  `compress-artifacts` and `move-artifacts`, which used `gzip-all`, `rimraf` and Windows `move`.

### Problem found: the image did not fit

The first deploy came to about 660 KB for a 600 KB partition. The cause was two oversized images in `web/src`:

| File | Before | After |
|------|--------|-------|
| `favicon.ico` | 104 KB: a 512x512 PNG renamed to `.ico` | 9 KB: real ICO, 16/32/48 px |
| `assets/logo/logo.png` | 53 KB, 1772x412, shown at 80–120 px | 15 KB, 240x56 |

Both keep the new artwork. Enlarging `www_0` was the alternative: about 330 KB at the end of the
4 MB flash is unused. It was not needed.

## Steps

1. Write `deploy-to-firmware.js` and wire `build-deepdeck`.
2. Shrink the favicon and logo.
3. Run `npm run build-deepdeck`, then rebuild the firmware.

## Verification

- `npm run build-deepdeck`: the image is 530,833 of 614,400 bytes (86.4%).
- The ESP-IDF 4.4.7 firmware build regenerates `build/www_0.bin` with the new files. The old `56.js.gz`
  is gone, and `401.js.gz`, `851.js.gz` and `assets/logo/logo.png` are present.
- Every `src`/`href` in `index.html` resolves to a file or `.gz` in `spiffs_image`, and so does every
  lazy chunk.
- **Not yet tested on hardware**: flash, connect to the DeepDeck WiFi, and check that the page loads
  with no Gestures or Mouse tab.

## Result

Commit `60af76f`. See [CHANGELOG](../traceability/CHANGELOG.md) CH-009.
Open: at 86% of `www_0` there is little headroom. If the web app grows, enlarge `www_0` into the
unused end of the flash, or drop `fonts.woff2` (103 KB, the Material Icons font).
