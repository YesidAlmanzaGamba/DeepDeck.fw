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
| R4.0 | Spike: reach the DeepDeck over BLE (while connected as a keyboard) and USB from Tauri | in progress |
| R4.1 | Firmware config core (`config_api`, `config_task`, WiFi/BLE coexistence) | proposed |
| R4.2 | Firmware transports: BLE GATT config service, UART0 framing | proposed |
| R4.3 | Command Center app: Tauri, transport layer, Connect and WiFi pages | proposed |
| R4.4 | Installers for Windows, macOS and Linux (CI) | proposed |
| R4.5 | Remove web server, SPIFFS, AP mode; repartition | proposed |

## 1. Finish what is half-done

| ID | Item | Status | Plan |
|----|------|--------|------|
| R1.1 | Ship the current web app in the firmware SPIFFS image with one cross-platform command | done | [plan](2026-10-17-web-to-firmware-pipeline.md) |
| R1.2 | Per-key LED "Color" tab in the web app: implement in firmware or remove | proposed | |
| R1.3 | Knob macros use the hardcoded `macros[]` table instead of the user's macros | proposed | |
| R1.4 | API safety: knob JSON arrays not bounds-checked, WiFi password logged, `Access-Control-Allow-Origin: *` | proposed | |

## 2. Code health (no visible change)

| ID | Item | Status | Plan |
|----|------|--------|------|
| R2.1 | Make `keypress_handles.c` and `keymap.c` real compilation units, instead of `.c` files `#include`d into `deepdeck_tasks.c` | proposed | |
| R2.2 | Remove dead components: plugins (hardcoded WiFi credentials), MQTT, ESP-NOW | proposed | |
| R2.3 | Document the REST API shared by firmware and web | proposed | |

## 3. Features (each needs its own plan)

| ID | Item | Status | Plan |
|----|------|--------|------|
| R3.1 | Tap-dance and mod-tap (claimed by upstream commit f5810e8, never implemented) | proposed | |
| R3.2 | Macros v2: sequences with delays, text strings | proposed | |
| R3.3 | Firmware update over WiFi from the web app (OTA partitions already exist) | proposed | |
| R3.4 | OLED menu: WiFi status and IP, BLE device management, sleep settings, Pomodoro | proposed | |
| R3.5 | Integrations: keys that publish MQTT messages or send HTTP requests (Home Assistant) | proposed | |
| R3.6 | Automatic layer switching from a companion app on the computer | proposed | |
