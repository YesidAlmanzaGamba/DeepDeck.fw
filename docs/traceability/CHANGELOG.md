# Change log

Every change to firmware or web, newest first.

- **Commits:** the `git` commits that implement the change, on branch `monorepo-and-cleanup` unless noted.
- **Verified:** what was checked. `build` means a firmware build with ESP-IDF 4.4.7 and/or `npm run build-prod`.
- **Hardware:** whether it was tested on the physical DeepDeck.

Add new entries on top, with the next `CH-` number.

| ID | Date | Change | Area | Commits | Verified | Hardware |
|----|------|--------|------|---------|----------|----------|
| CH-029 | 2026-10-18 | GitHub Actions: firmware (ESP-IDF 4.4.7 container), Windows installer, Rust check on Linux and macOS, draft release on `v*` tags | CI | (branch `phase4`) | YAML parsed; the same steps run locally: clean firmware build from sdkconfig.defaults, `npm ci`, `cargo check --locked` | not run on GitHub yet |
| CH-028 | 2026-10-18 | Screen brightness, screen off after N idle minutes, LEDs off with it (`device.settings.*`); Device page card | firmware, app | (branch `phase4`) | screen off at 60 s with a 1-minute timeout, back on with a layer change, page writes the setting | yes (automated); owner: brightness by eye |
| CH-027 | 2026-10-18 | Tray icon (link and layer), close to tray, start with Windows (hidden), single instance, notification when the link is lost | app | (branch `phase4`) | autostart registry on/off, close hides, second start shows the window, `--hidden`, reconnect after a device restart | partly (owner: tray and notification by eye) |
| CH-026 | 2026-10-18 | Layers by application: the app switches the DeepDeck's layer when another program comes to the front | app | (branch `phase4`) | fallback and a matching rule switched the layer on the device | yes (automated) |
| CH-025 | 2026-10-18 | New keymap editor (4x4 keys + 2 knobs, key search, drag and drop); knob macros use the user's macros (R1.3); macro index bounds-checked | web, firmware | (branch `phase4`) | edit by click and by drag, knob action, save over BLE, read back over USB, layer restored identical | yes (automated) |
| CH-024 | 2026-10-18 | Firmware update from the app: `fw.*`, rollback, Update page, `cc_cli update`; BLE writes capped at 512 bytes and serialised; UART ISR in IRAM; BLE Data Length Extension | firmware, app, tools | (branch `phase4`) | updates over USB and BLE, corrupt / foreign images refused, crashing image rolled back, update from the app's page | yes (automated) |
| CH-023 | 2026-10-18 | Remove the WiFi portal: web server, SPIFFS image, setup network; WiFi station joins without restart, `wifi.forget`; shared BLE access in the app; I2C mutex created before BLE; fw 0.7.0 | firmware, web, app | (see branch `phase4`) | build, flashed, selftest 0 failures over USB+BLE, heap 64 KB (was 38), app + cc_cli on BLE together | yes (automated); `wifi.set`/`forget`: owner |
| CH-022 | 2026-10-18 | README rewritten for the reworked project and the app; suggested roadmap R6; `main` fast-forwarded to `command-center` and pushed | docs | (this commit) | links checked | n/a |
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

### CH-029: Automatic builds (R6.6)
- **What:** `.github/workflows/build.yml`, on every push and pull request:
  - `firmware`: `idf.py -C firmware build` in `espressif/idf:v4.4.7`; artifact with `DeepDeck.bin`,
    bootloader, partition table, `ota_data_initial.bin` and `flash_args`;
  - `app-windows`: `npm ci`, then `npx @tauri-apps/cli@2.12.3 build`; artifact: the NSIS installer;
  - `app-check`: `cargo check --locked` on Ubuntu and macOS (the Rust side only);
  - `release` (tags `v*`): a **draft** GitHub release with the installer, `DeepDeck-<tag>.bin`
    (for the app's Update page) and a zip of the images for esptool.
- **Checked locally:** the YAML parses; a firmware build from `sdkconfig.defaults` only (no local
  `sdkconfig`, as on CI) succeeds with rollback, IRAM UART and coexistence on; `npm ci` installs
  from the lockfile; `cargo check --locked` passes.
- **Not yet:** it has never run on GitHub (the branch is not pushed). The first run may need fixes,
  most likely in the Linux packages list or the macOS check.
- **To make a release:** `git tag v0.7.0 && git push origin v0.7.0`, then publish the draft.

### CH-028: Screen and idle settings (R6.5)
- **What:** `main/device_settings.c` (NVS namespace `settings`): screen brightness (OLED contrast),
  screen off after 0 (never) to 1440 idle minutes, LEDs dark with the screen. Keys, knobs and
  layer changes (also from the app or layers by application) count as activity. The OLED task
  applies it, as it owns the display. `device.settings.get/set` (with `idle_s` and `screen_off`
  for diagnostics); `device.info.battery` is `null`. Device page: card "Screen and LEDs".
- **Not done on purpose (D-031):** battery level and deep sleep.
- **Verified:** with a 1-minute timeout the screen went off at 60–64 s idle and came back with a
  layer change; the page stored "2 min" on the device; defaults restored (never).
- **Owner to check:** the brightness slider by eye; LEDs going dark with the screen.

### CH-027: Tray, start with Windows, link-lost notice (R6.4)
- **What (`src-tauri/src/desktop.rs`):**
  - Tray icon: the tooltip shows the link and the active layer (from `layer.changed` events and
    `device.info` answers). Left click opens the window; the menu has Open and Quit.
  - Closing the window hides it to the tray (setting, on by default), so layers by application
    keep working. Quit from the tray menu.
  - Start with Windows: `HKCU\...\CurrentVersion\Run` value "DeepDeck Command Center" with
    `--hidden`. The window starts hidden in that case (`visible: false` in tauri.conf, shown at
    setup otherwise).
  - Single instance (`tauri-plugin-single-instance`): starting the app again shows the running one.
  - Notification (`tauri-plugin-notification`) when an established link is lost, but not after the
    user's own Disconnect.
  - Settings in `<app config dir>/desktop.json`; Device page, card "This computer".
- **Verified:** defaults read; autostart on writes the Run value with `--hidden`, off removes it;
  WM_CLOSE hides the window and the process stays; a second start exits and shows the first
  window; `--hidden` starts without a window; restarting the DeepDeck while connected over BLE:
  the link drops and comes back by itself.
- **Owner to check:** the tray icon and tooltip, and the Windows notification when the DeepDeck
  is switched off (a development build may show it under another app name).

### CH-026: Layers by application (R6.3)
- **What:**
  - `src-tauri/src/app_layers.rs`: a thread checks the foreground window's program every 400 ms
    (Win32 `GetForegroundWindow`, `QueryFullProcessImageNameW`). When it changes and a rule matches
    (or the "other programs" layer is set), it sends `layers.set_active` itself, once per change,
    so it works with the window hidden. The Command Center itself never switches layers.
  - Rules in `<app config dir>/app-layers.json` (`app_layers_get` / `app_layers_set`).
  - Apps page: on/off, rules (program → layer), recently seen programs to add with one click, a
    layer for other programs, the last switch.
  - Windows only for now; elsewhere the watcher sees nothing (R4.4).
- **Verified:** with the DeepDeck on Chrome, "other programs → Media" switched it to Media, and
  "windowsterminal.exe → Numpad" switched it to Numpad (read back over USB); rules cleared and
  the layer restored afterwards.
- **Note:** Windows only gives the foreground to programs the user brings forward; a program
  started in the background (a script) does not count until clicked.

### CH-025: New keymap editor (R6.2) and knob macros (R1.3)
- **Why:** the old Keymap screens (a table, then a form per layer with tabs) came from the web
  portal; the owner asked for an editor in the app's style.
- **What:**
  - `keymap-editor.component`: layer chips (+ new, the one on the device marked), name, "in the
    layer rotation", the 2 knobs (turn left/right, press, double, long) and the 4x4 keys as on the
    device; the selected slot's action and screen label; a searchable key list in groups
    (letters, numbers, editing, symbols, navigation, F1–F24, numpad, modifiers, media, layers,
    macros by name). Click a slot then a key, or drag a key onto a slot. Save / Revert,
    duplicate, delete, show on the DeepDeck, restore defaults.
  - `key-catalog.ts`: the codes the firmware handles (media: only the 7 it sends).
  - The old `modules/keymap` (and its non-working Color tab) is removed.
  - `tauri.conf.json`: `dragDropEnabled: false`, otherwise WebView2 swallows HTML5 drag and drop.
  - Firmware: knob actions that are macros used the built-in `macros[]` table instead of the
    user's macros; keys and knobs now both use `user_macros`, and check the index against
    `total_macros` (a key pointing past the stored macros read beyond the array).
- **Verified:** in the app over BLE: key 16 set by search and click (F5), key 15 by drag and drop
  (F6), left knob turn-left (Mute), saved; read back over USB; the layer then restored from a
  backup and compared identical. Selftest 0 failures.
- **Owner to check:** a macro on a knob now runs your macro.

### CH-024: Firmware update from the app (R6.1)
- **Why:** updating needed ESP-IDF, esptool and the cable.
- **What:**
  - Firmware: `fw.info`, `fw.begin`, `fw.chunk`, `fw.end`, `fw.abort` (`config_api.c`). The image goes
    into the other OTA slot (`ota_0`/`ota_1` already existed); the first chunk must be a DeepDeck
    image, `esp_ota_end` validates it and its SHA-256 must match the app's.
  - Rollback on (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, new bootloader): the firmware marks itself
    valid at the end of `app_main`; one that crashes before is replaced by the previous one.
  - The image's version (`esp_app_desc_t.version`) now comes from `FIRMWARE_VERSION`
    (`firmware/CMakeLists.txt`), so the app shows the version of a file before installing it.
  - App: Update page (installed version and slot, file check, progress, speed, time left, cancel,
    result after the restart); `open_firmware_file` checks the image in Rust (`firmware.rs`).
  - `cc_cli.py update <bin> [--corrupt]`.
- **Bugs found on the way:**
  - BLE lines longer than about 514 bytes never arrived: clients wrote MTU−3 = 514-byte chunks, but
    an attribute value is at most 512 bytes, and write-without-response drops the rest silently.
    Clients now cap chunks at 512 (app, cc_cli, protocol.md).
  - Two BLE requests sent at once could interleave their chunks (app and cc_cli): one line at a
    time now.
  - The UART interrupt was not in IRAM: bytes arriving over USB while flash was written (a layer
    save, an update chunk) were lost. `CONFIG_UART_ISR_IN_IRAM=y`.
  - BLE links now use Data Length Extension (251-byte packets) and the host's parameters are
    logged. Neither made BLE faster: 15 ms interval, latency 0, about 2 KB/s, bound by the host.
  - Tried and dropped: pausing WiFi during a BLE update (coexistence). It broke the BLE session.
- **Verified on the device:**
  - update over USB (`cc_cli`, 3 in flight: 8.2 KB/s) and over BLE (one at a time: 2 KB/s);
  - device restarts into the other slot, `fw.info` says `valid`;
  - wrong SHA-256 and a non-DeepDeck image (the bootloader .bin) refused, nothing installed;
  - an image that calls `abort()` at start: installed, crashed, rolled back to the previous slot,
    which started normally;
  - the whole flow from the app's Update page over USB.
- **Owner to check:** an update from the app over Bluetooth (about 11 minutes).

### CH-023: Remove the WiFi portal (R4.5)
- **Why:** the Command Center does everything the portal did; the portal cost RAM and flash, and
  its setup network ("AHUYAMA", password "DeepDeck") was open to anyone nearby.
- **What:**
  - Firmware: deleted `server.c`, `spiffs.c`, `firmware/spiffs_image`, the AP mode and mDNS use.
  - `wifi_handles.c` rewritten as a station: joins the stored network at boot, retries 5 times
    at once and then every 30 s, never opens a setup network.
  - `wifi.set` joins at once without a restart; new `wifi.forget`; `wifi.status` says `not_set`.
  - The WiFi password was still logged at boot ("save pass: ..."): gone.
  - The I2C mutex is created before BLE starts: BLE connection events drew on the OLED before it
    existed ("mutex handle invalid" at every boot).
  - Web: removed the Settings page, the HTTP fallback in `DeviceService`, the HTTP-only
    `ApiService` methods and `deploy-to-firmware.js`; the WiFi page has Forget.
  - App: BLE opens the service in shared mode. A Command Center window left open made every
    other client (cc_cli, a second app) fail with AccessDenied.
  - Partition table unchanged (D-027).
- **Numbers:** app image 1,391,632 to 1,318,208 bytes; free heap 38 KB to 64 KB at runtime.
- **Verified:** flashed, selftest 0 failures over USB and BLE, joins the stored network at boot
  without a restart, app self-test, `cc_cli` over BLE while the app is connected.
- **Owner to check:** `wifi.set` to another network from the WiFi page (no restart; Bluetooth
  stays connected), and Forget.

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
