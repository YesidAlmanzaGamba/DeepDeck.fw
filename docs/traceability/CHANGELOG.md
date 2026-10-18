# Change log

Every change to firmware or web, newest first.

- **Commits:** the `git` commits that implement the change, on branch `monorepo-and-cleanup` unless noted.
- **Verified:** what was checked. `build` means a firmware build with ESP-IDF 4.4.7 and/or `npm run build-prod`.
- **Hardware:** whether it was tested on the physical DeepDeck.

Add new entries on top, with the next `CH-` number.

| ID | Date | Change | Area | Commits | Verified | Hardware |
|----|------|--------|------|---------|----------|----------|
| CH-011 | 2026-10-17 | Spike R4.0: BLE and USB config channel, firmware plus Tauri (branch `spike/command-center`) | both | 087ee6b, 650fc87, (spike app) | echo over BLE and USB from Python and Tauri | **yes** |
| CH-010 | 2026-10-17 | Add CLAUDE.md, docs/plans, docs/traceability | docs | 91bfc61 | n/a | n/a |
| CH-009 | 2026-10-17 | Ship the current web app in the firmware SPIFFS image | both | 60af76f | build, image contents | no |
| CH-008 | 2026-10-16 | Build from project paths that contain spaces | firmware | 012671a | build in `D:\Personal Yesid\...` | no |
| CH-007 | 2026-10-16 | Remember the active layer across reboot and deep sleep; fix layer hold | firmware | 13a9f89 | build, symbol check | no |
| CH-006 | 2026-10-16 | Remove mouse actions | both | 0f7fe02 | build (both) | no |
| CH-005 | 2026-10-16 | Remove the APDS-9960 gesture sensor | both | fb2c6e9 | build (both) | no |
| CH-004 | 2026-10-16 | Monorepo README; web build output points to `firmware/` | docs, web | e53c0d2 | n/a | n/a |
| CH-003 | 2026-10-16 | Import DeepDeck.Web `develop` into `web/` | web | 097ba48 | `npm run build-prod` | n/a |
| CH-002 | 2026-10-16 | Move the firmware into `firmware/` | firmware | bda7f6d | build, same binary size | n/a |
| CH-001 | 2026-10-16 | Restore the I2C bus mutex deleted upstream | firmware | 2e7abe0 | build | no |

## Details

### CH-010: Project documentation
`CLAUDE.md` at the root: build setup, architecture traps, workflow. `docs/plans/`: template, roadmap
and one plan per piece of work. `docs/traceability/`: this log and the decision log.
`.gitattributes` keeps `firmware/spiffs_image` byte for byte, with no line-ending conversion.

### CH-009: Ship the current web app in the firmware SPIFFS image
- **Why:** `firmware/spiffs_image` was a 2023 build, so the device still showed the Gestures and Mouse UI.
- **What:** `web/scripts/deploy-to-firmware.js` and `npm run build-deepdeck`. The favicon (a renamed
  512 px PNG, 104 KB) and the logo (1772 px, 53 KB) were shrunk so the image fits in `www_0`.
  `spiffs_image` was regenerated: 531 of 600 KB. The header logo is now included; it was missing before.
- **Plan:** [2026-10-17-web-to-firmware-pipeline](../plans/2026-10-17-web-to-firmware-pipeline.md)
- **To test on hardware:** the page loads over the DeepDeck WiFi, with no Gestures or Mouse tab, and
  the logo and favicon show.

### CH-008: Build from project paths that contain spaces
- **Why:** ESP-IDF 4.4 writes the Kconfig paths of project components unquoted into `build/kconfigs.in`.
  `D:/Personal Yesid/...` failed with `'D:/Personal' not found`. The owner keeps the repo in that folder.
- **What:** removed `components/bus/Kconfig`; its options became `#ifndef` defaults with the same values.
  Removed `components/mdns`, an older copy of the mdns already in ESP-IDF 4.4.7 (mdns is off,
  `USE_MDNS`).
- Also, outside git: `firmware/.vscode/settings.json` points the VS Code ESP-IDF extension at 4.4.7,
  with `idf.pythonInstallPath` = Python 3.8.7.

### CH-007: Remember the active layer; fix layer hold
- **Why:** the active layer only lived in RAM, so every boot or wake-up started on layer 0. Releasing a
  layer-hold key always went to layer 0, and its check compared the wrong keycode.
- **What:**
  - `nvs_save/load_current_layer`, with the NVS key `cur_layer`. Saves are debounced by 3 s and flushed
    before deep sleep and before the WiFi restart.
  - `set_current_layout()` is now the single place that changes the layer.
  - The saved layer is restored on boot if it still exists and is active.
  - Layer hold returns to the previous layer, is never saved, and ignores out-of-range layers.
- **To test on hardware:** change layer, wait more than 3 s, then power-cycle. The same layer must come
  back and the log must show `restored layer N`. Hold a layer-hold key and release it: you must return
  to the previous layer.

### CH-006: Remove mouse actions
- **Why:** the owner does not want mouse actions. They also didn't work on the 16 keys, which sent
  mouse codes as bogus key presses.
- **What:**
  - Firmware: removed the mouse and joystick BLE tasks and queues, and the mouse branch of
    `encoder_command`. `halBLEInit(keyboard, media)`.
  - 0xF0–0xFF stays a reserved range that is ignored, because old layers may still contain it.
  - The HID descriptor is unchanged, so there is no re-pairing.
  - Web: removed the Mouse tab and the key list.

### CH-005: Remove the APDS-9960 gesture sensor
- **Why:** the owner does not want it. Its handler also sent macros and layer actions a second time,
  as truncated key presses.
- **What:**
  - Firmware: removed the driver component, the task and ISR, `GESTURE_*`, and `gesture_map` from
    `dd_layer` and from the API.
  - The I2C bus and mutex stay, because the OLED needs them.
  - Old `dd_layer_v1` blobs are migrated on boot (`nvs_migrate_layer_v1`).
  - `nvs_read_layers` now passes an initialised blob size.
  - Web: removed the Gestures tab, the defaults and the validation. The web validation rejected layers
    without `gesture_map`, so both sides had to change together.
- **To test on hardware:** the first boot after flashing logs `migrated from the old format` once per
  layer, and the existing layers are intact.

### CH-001 to CH-004: Monorepo set-up
- **CH-001:** upstream `main` did not link. Commit f5810e8, titled "added tapdance", deleted
  `i2s_user_lock`/`i2s_user_unlock` and the mutex creation, while the OLED and `i2c_bus` still call them.
  They were restored unchanged.
- **CH-002 to CH-004:** pure move of the firmware into `firmware/`, then `git subtree add` of
  DeepDeck.Web `develop` (the branch with the LED settings UI that matches `/api/led`), and a README.
