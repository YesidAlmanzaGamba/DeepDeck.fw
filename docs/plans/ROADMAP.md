# Roadmap

Proposed on 2026-10-17 after the initial audit. Order agreed with the owner: group 1 first.

## Done before the roadmap (2026-10-16)

Merged the two repos into one, removed the gesture sensor and mouse actions, fixed the
active-layer bug, and made the firmware build from paths with spaces.
See [CHANGELOG](../traceability/CHANGELOG.md) CH-001 to CH-008.

## 4. Command Center: desktop app over BLE and USB (priority since 2026-10-17)

Replaces the WiFi AP plus on-device web portal. Plan: [2026-10-17-command-center](2026-10-17-command-center.md).
R1.2–R1.4 wait until this is done; R1.4 (API safety) is largely covered, because the portal goes away.

| ID | Item | Status |
|----|------|--------|
| R4.0 | Spike: reach the DeepDeck over BLE (while connected as a keyboard) and USB from Tauri | done: [results](2026-10-17-command-center-spike-results.md) |
| R4.1 | Firmware config core (`config_api`, `config_task`, WiFi/BLE coexistence) | done: [plan](2026-10-18-command-center-phase1.md) |
| R4.2 | Firmware transports: BLE GATT config service, UART0 framing | done (in Phase 1) |
| R4.3 | Command Center app: Tauri, transport layer, Connect and WiFi pages, plus dashboard, active layer, rename, backup/restore, USB log, restart (D-017) | done on Windows: [plan](2026-10-18-app-lighting-oled.md); owner to try it |
| R4.4 | Installers for Windows, macOS and Linux (CI); app BLE on macOS (CoreBluetooth) and Linux (BlueZ) (D-024) | BLE written (btleplug), untested; Windows installer in CI; macOS/Linux installers not yet |
| R4.5 | Remove web server, SPIFFS, AP mode (no repartition, D-027) | done: [plan](2026-10-18-phase4.md) |

## 5. Device experience (asked by the owner on 2026-10-18)

Plan: [2026-10-18-app-lighting-oled](2026-10-18-app-lighting-oled.md)

| ID | Item | Status |
|----|------|--------|
| R5.1 | OLED: layer name and status in the yellow band, 4x4 key grid; Device info pages in the settings menu | done; owner to check on the device |
| R5.2 | Lighting v2: 11 effects, per-layer key colours, live preview from the app (replaces R1.2) | done; tested by the owner (2026-10-18) |

## 6. Phase 4 (asked by the owner on 2026-10-18: all, in order)

Plan: [2026-10-18-phase4](2026-10-18-phase4.md)

In suggested order. R4.5 (remove the portal) comes first because it frees flash and RAM for R6.1.

| ID | Item | Status |
|----|------|--------|
| R6.1 | Firmware update from the app (over BLE or USB; WiFi download later, D-030) | done |
| R6.2 | New keymap editor in the app's style: visual 4x4 + knobs, keycode search, drag and drop | done |
| R6.3 | Per-application layers: the app switches layer by the focused window | done (Windows) |
| R6.4 | Tray icon and start with Windows; layer status and link-lost notice | done |
| R6.5 | Battery level, sleep timeout and OLED dimming set from the app | done: screen brightness and idle off; battery and deep sleep deferred (D-031) |
| R6.6 | CI: build firmware, web and installer on every push, releases with the installer (with R4.4) | written, not yet run on GitHub |
| R6.7 | Tap-dance and mod-tap (upstream WIP); macros with delays and typed text | done (sequences); owner to try mod-tap and tap-dance on the keys |
| R6.8 | Share layers: export/import single layers from the backup format | done |

## 1. Finish what is half-done

| ID | Item | Status | Plan |
|----|------|--------|------|
| R1.1 | Ship the current web app in the firmware SPIFFS image with one cross-platform command | done | [plan](2026-10-17-web-to-firmware-pipeline.md) |
| R1.2 | Per-key LED "Color" tab in the web app: implement in firmware or remove | superseded by R5.2 | |
| R1.3 | Knob macros use the hardcoded `macros[]` table instead of the user's macros | done (CH-025) | |
| R1.4 | API safety: knob JSON arrays not bounds-checked, WiFi password logged, `Access-Control-Allow-Origin: *` | mostly done in Phase 1 (validation, no password log); CORS goes with the portal (R4.5) | |

## 2. Code health (no visible change)

| ID | Item | Status | Plan |
|----|------|--------|------|
| R2.1 | Make `keypress_handles.c` and `keymap.c` real compilation units, instead of `.c` files `#include`d into `deepdeck_tasks.c` | proposed | |
| R2.2 | Remove dead components: plugins (hardcoded WiFi credentials), MQTT, ESP-NOW | proposed | |
| R2.3 | Document the REST API shared by firmware and web | done: [protocol.md](../protocol.md) | |

## 3. Features (each needs its own plan)

| ID | Item | Status | Plan |
|----|------|--------|------|
| R3.1 | Tap-dance and mod-tap (claimed by upstream commit f5810e8, never implemented) | proposed | |
| R3.2 | Macros v2: sequences with delays, text strings | proposed | |
| R3.3 | Firmware update over WiFi from the web app (OTA partitions already exist) | proposed | |
| R3.4 | OLED menu: WiFi status and IP, BLE device management, sleep settings, Pomodoro | proposed | |
| R3.5 | Integrations: keys that publish MQTT messages or send HTTP requests (Home Assistant) | proposed | |
| R3.6 | Automatic layer switching from a companion app on the computer | proposed | |
