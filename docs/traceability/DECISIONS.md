# Decision log

Product and architecture decisions, newest first. A decision stays valid until a later entry replaces it.

| ID | Date | Decision | Decided by |
|----|------|----------|------------|
| D-026 | 2026-10-18 | The Tauri CLI runs through `npx @tauri-apps/cli@2.12.3`, not as an npm dependency | Claude |
| D-025 | 2026-10-18 | The web page's Settings page stays in the browser only; the app has Lighting, WiFi and Device pages | Claude |
| D-024 | 2026-10-18 | App BLE is Windows-first (WinRT); macOS and Linux get USB now and BLE in R4.4 | Claude |
| D-023 | 2026-10-18 | Per-layer key colours live in their own NVS namespace (`led_keys`), not in `dd_layer` | Claude |
| D-022 | 2026-10-18 | Lighting v2: 11 effects, ids 0-5 unchanged; OLED: layer name and icons in yellow, 4x4 grid; Device info as 3 pages | owner |
| D-021 | 2026-10-18 | A deleted macro is emptied, not removed; `macros.create` assigns the keycode | Claude |
| D-020 | 2026-10-18 | On a crash the firmware prints and reboots instead of halting in the GDB stub | Claude |
| D-019 | 2026-10-18 | Firmware version 0.6.0 for the configuration core | Claude |
| D-018 | 2026-10-18 | BLE and USB transports are built in Phase 1 (they were Phase 2) | Claude |
| D-017 | 2026-10-18 | Extra app functions chosen by Claude, as the owner delegated: dashboard, active layer, WiFi scan, rename, backup/restore, USB log, restart | owner, Claude |
| D-016 | 2026-10-17 | Install Rust and the VS Build Tools under `D:\DevTools` | owner |
| D-015 | 2026-10-17 | BLE on Windows through our own WinRT module, not btleplug | Claude |
| D-014 | 2026-10-17 | One JSON-per-line protocol mirroring the REST API, shared by BLE, USB and (temporarily) HTTP | Claude |
| D-013 | 2026-10-17 | Keep the web server until the app covers everything, then remove it with SPIFFS and AP mode | owner |
| D-012 | 2026-10-17 | The app talks over BLE and USB serial, both from the start | owner |
| D-011 | 2026-10-17 | Tauri v2 app reusing the Angular screens, for Windows, macOS and Linux | owner |
| D-010 | 2026-10-17 | Configure the DeepDeck from a desktop app instead of the WiFi AP plus web portal | owner |
| D-009 | 2026-10-17 | Plans live in `docs/plans/` and every change is logged in `docs/traceability/` | owner |
| D-008 | 2026-10-17 | Shrink web images instead of enlarging the `www_0` partition (for now) | Claude |
| D-007 | 2026-10-17 | Roadmap order: group 1 (finish half-done work) first, starting with R1.1 | owner |
| D-006 | 2026-10-16 | The repo stays in `D:\Personal Yesid\...`: no move, no junction. The code must build from paths with spaces | owner |
| D-005 | 2026-10-16 | Stay on ESP-IDF 4.4.x; no port to 5.x for now | Claude |
| D-004 | 2026-10-16 | Keep the BLE HID descriptor (mouse/joystick reports) unchanged | Claude |
| D-003 | 2026-10-16 | Remove mouse actions | owner |
| D-002 | 2026-10-16 | Remove the APDS-9960 gesture sensor | owner |
| D-001 | 2026-10-16 | One repository for firmware and web, with both histories kept | owner |

## Details

**D-026:** adding `@tauri-apps/cli` to `package.json` rewrites `package-lock.json` and `yarn.lock`
for the whole Angular 15 tree. Pinning the version on the `npx` line gives the same result without
touching the lockfiles.

**D-025:** the web page cannot scan WiFi or preview lighting, and it goes away in R4.5. The app's
pages need protocol methods that HTTP does not have.

**D-024:** btleplug cannot reach a keyboard Windows is already connected to (spike, D-015). macOS
(CoreBluetooth) and Linux (BlueZ) must be written and tested on those machines.

**D-023:** changing `dd_layer` changes the blob size and needs a migration of every stored layer.
A separate blob per layer needs none, and a missing blob simply means "defaults".

**D-022:** chosen by the owner when asked (2026-10-18). Keeping ids 0-5 means LED settings stored
by older firmware still mean the same effect.

**D-021:** a macro's key code is its position (500 + index): removing one would renumber every later
macro, and the keys that point at it.

**D-020:** a frozen keyboard needs a replug and shows nothing. The crash is still printed on the USB log.

**D-017:** the firmware side of these functions is in protocol v1. Backup and restore needs no firmware
method: the app builds it from `layers.get` and `macros.list`.

**D-015:** btleplug on Windows only knows devices it saw advertising, and discovers services uncached, which Windows refuses on a connected HID device. See the [spike results](../plans/2026-10-17-command-center-spike-results.md).

**D-016:** Visual Studio 2022 Build Tools (C++) are in `D:\DevTools\VSBuildTools` (the Windows SDK still goes to C:). Rust is in `D:\DevTools\Rust`, with `RUSTUP_HOME` and `CARGO_HOME` set for the user.

**D-010 to D-014:** see [2026-10-17-command-center](../plans/2026-10-17-command-center.md).
- **Why (owner):** setting WiFi through the device's own AP means switching the PC's network back and
  forth, and the web app has to be compiled into the firmware.
- **Side benefits:**
  - closes the open AP (fixed password `DeepDeck`);
  - frees the 600 KB `www_0` partition;
  - the BLE link is already paired and encrypted.
- **Risk checked first, in Phase 0:** can a desktop app reach a custom GATT service of a BLE keyboard
  that the OS is already connected to?

**D-009:** each non-trivial piece of work gets a plan before implementation. Every change gets a
CHANGELOG entry with commits, verification and hardware status.

**D-008:** the favicon and logo were far larger than needed (157 KB together, now 24 KB).
Enlarging `www_0` into the unused about 330 KB at the end of the 4 MB flash remains the next option
if the web app grows. It means rewriting the partition table; NVS is not touched.

**D-006:** consequence: never add `Kconfig` files to project components (see CH-008).

**D-005:** the code relies on legacy ESP-IDF 4 drivers (PCNT, RMT, ADC, I2C) and APIs.
Porting to 5.x is a project of its own. Revisit when a needed feature requires it.

**D-004:** changing the descriptor makes every paired computer forget the DeepDeck, which then has
to be paired again. The firmware simply never sends those reports.

**D-003 / D-002:** do not re-add them. Codes 0xF0–0xFF stay reserved and are ignored, and old
layers with `gesture_map` are migrated.

**D-001:** the REST API between `firmware/main/server.c` and `web/` changes in single commits.
`web/` was imported from the DeepDeck.Web `develop` branch, not `main`, because only `develop` has
the LED settings UI that matches the firmware.
