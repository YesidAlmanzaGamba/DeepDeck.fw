# Change log

Every change to firmware or web, newest first.

- **Commits:** the `git` commits that implement the change, on branch `monorepo-and-cleanup` unless noted.
- **Verified:** what was checked. `build` means a firmware build with ESP-IDF 4.4.7 and/or `npm run build-prod`.
- **Hardware:** whether it was tested on the physical DeepDeck.

Add new entries on top, with the next `CH-` number.

| ID | Date | Change | Area | Commits | Verified | Hardware |
|----|------|--------|------|---------|----------|----------|
| CH-021 | 2026-10-18 | Command Center: Lighting (effects, live preview, per-layer key painter), WiFi, Device (backup/restore), Log pages | web | 3f93b42 | `ng build`, every page loaded with real data over BLE in the built app | yes (app, automated) |
| CH-020 | 2026-10-18 | Command Center app shell: Tauri, BLE/USB link manager, DeviceService, Connect and Dashboard | web, app | c06b36e, 0848184 | `cargo build`, app self-test over BLE (0 failures) | yes (automated) |
| CH-019 | 2026-10-18 | Lighting v2: 11 effects, brightness, two colours, press highlight, per-layer key colours, live preview; protocol `led.*` | firmware, tools | 1824fd6 | build, flashed, selftest 0 failures over USB+BLE, heap flat over 30 `led.keys.set` | yes (automated); effects by eye: owner |
| CH-018 | 2026-10-18 | OLED main screen: layer name and status icons in the yellow band, 4x4 grid; Device info pages and Restart in the menu | firmware | 66fc46f | build, flashed, boots without errors | owner to check the screen |
| CH-017 | 2026-10-18 | Phase 3 plan (app, lighting v2, OLED) | docs | 13148fc | n/a | n/a |
| CH-016 | 2026-10-18 | Swap layers/macros under a lock; fix an NVS handle leaked on every layer write | firmware | abd1a59 | stress test, heap flat over 30 rounds | yes (automated) |
| CH-015 | 2026-10-18 | Macro, LED, device and WiFi methods; all `/api` handlers become wrappers | firmware | 796b2d2 | selftest over USB+BLE, HTTP, wifi.scan, restart | yes (automated) |
| CH-014 | 2026-10-18 | Layer methods in the configuration core; `layer.changed` events | firmware | 73a3266 | USB/BLE/HTTP, write round trip | yes (automated) |
| CH-013 | 2026-10-18 | Config channel over BLE and USB, `device.info`, coexistence, reboot on panic, fw 0.6.0 | firmware, tools | e19647e | `device.info` over USB and BLE | yes (automated) |
| CH-012 | 2026-10-18 | Protocol v1 (`docs/protocol.md`) and Phase 1 plan | docs | 0f5ae47 | n/a | n/a |
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

### CH-017 to CH-021: Phase 3 (Command Center app, lighting v2, OLED redesign)
- **Plan:** [2026-10-18-app-lighting-oled](../plans/2026-10-18-app-lighting-oled.md). Branch `command-center`.
- **CH-018, screen and menu (R5.1):**
  - main screen: the yellow band shows the active layer's name, `i/n` and the Bluetooth and WiFi
    icons, instead of the fixed "Ahuyama" and the IP; the large layer name is gone; the 16 key names
    use a 4x4 grid of 32x12 px cells over the whole blue area;
  - redrawn on layer change, BLE connect/disconnect and WiFi IP changes only;
  - settings menu: "Device info" (pages Bluetooth, WiFi, System; knob flips, press returns) and
    "Restart" (replaces "DancingBerlin"). The values come from `device_status_get()`, the same as
    `device.info`.
- **CH-019, lighting v2 (R5.2):**
  - `rgb_led.c` rewritten as a 50 fps frame renderer: no `clear()` between frames (the flicker),
    `hsv2rgb` argument order fixed, brightness applied once;
  - 11 effects (ids 0-5 keep their meaning, so stored settings need no migration), press highlight
    usable with any effect, notification LEDs in the layer colour;
  - per-layer key colours in NVS namespace `led_keys` (51-byte blob per layer), erased with the layer
    and by `layers.restore`;
  - protocol: `led.get/set` new fields plus `save:false` for preview, `led.effects`,
    `led.keys.get/set/reset`; `device.info` gains `ble_connected` and `app_connected`.
- **CH-020, app shell (R4.3):** see the commit message; BLE is Windows-only for now (D-024), USB works
  on every OS.
- **CH-021, app pages:** the Lighting page is the fix for "custom RGB does not work": the old per-key
  "Color" tab of the key editor stored colours the firmware never kept.
- **Verified:**
  - firmware: flashed, 45 s boot without errors, `cc_cli.py --usb COM3 --ble selftest` 0 failures
    including the new LED tests (11 effects, preview not stored, `led.keys` round trip, bad input
    rejected), heap 37.7-38.3 KB flat over 30 rounds of key colour writes and effect previews;
  - app: built (debug and release), self-test over BLE (`DD_CC_SELFTEST=1`), then every page driven
    in the built app through the WebView2 DevTools port: dashboard, 11 effects, 3 layers with key
    colours, WiFi scan, device name.
- **Still to check by the owner:**
  - the new main screen and the 3 Device info pages;
  - each effect on the keys, and the press highlight;
  - painting key colours in the app: they show at once (Per-key effect) and survive a restart after Save;
  - backup to a file and restore from it.

### CH-012 to CH-016: Command Center Phase 1 (configuration core)
- **Why:** every configuration action lived in HTTP handlers, so only the web page could use it. The
  app needs the same actions over BLE and USB. Plan:
  [2026-10-18-command-center-phase1](../plans/2026-10-18-command-center-phase1.md); protocol:
  [protocol.md](../protocol.md).
- **What:**
  - `config_api.c`: 20 methods, plus a dispatcher with one lock;
  - `config_task.c`: the BLE and USB queue, and events;
  - transports ported from the spike;
  - `server.c` is now thin wrappers (1,740 lines down to 619);
  - `layouts_lock`;
  - `tools/cc-cli/cc_cli.py`.
- **Bugs fixed on the way:**
  - unchecked encoder maps (a memory overwrite) and NULL dereferences in the layer handlers;
  - encoder actions were taken by JSON key order;
  - every edit jumped to layer 0;
  - macro delete did nothing, and `nvs_delete_macro` wrote into the layers namespace;
  - the macro key range check was always true, and macro create trusted the client's keycode;
  - LED saturation was validated on hue;
  - the WiFi password was logged;
  - leaks: `nvs_load_macros` (every update), `json_response` (every call), and an NVS handle on every
    layer write;
  - out-of-bounds layer index after deleting the active last layer;
  - the stored BLE name was never used;
  - `main.c` checked the wrong return value when reading the name.
- **Settings:**
  - WiFi/BLE coexistence on;
  - print the crash and reboot (it used to halt in the GDB stub);
  - firmware 0.6.0.
- **Verified on the device (automated):**
  - `cc_cli.py --usb COM3 --ble selftest`: 0 failures, identical results over both transports;
  - HTTP endpoints, and the web page (layer list, edit, Save leaves the layer byte-identical);
  - `wifi.scan` over BLE while on WiFi;
  - restart and BLE reconnect;
  - 10 rounds deleting the active layer;
  - heap flat over 30 write rounds.
- **Still to check by the owner:**
  - `layer.changed` when pressing a layer key on the device (`cc_cli.py --ble watch`);
  - `wifi.set` over BLE: the device joins the network and BLE comes back;
  - power cycle keeps the configuration;
  - keys type normally with this firmware.

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
