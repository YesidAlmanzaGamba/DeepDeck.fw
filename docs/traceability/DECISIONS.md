# Decision log

Product and architecture decisions, newest first. A decision stays valid until a later entry replaces it.

| ID | Date | Decision | Decided by |
|----|------|----------|------------|
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
