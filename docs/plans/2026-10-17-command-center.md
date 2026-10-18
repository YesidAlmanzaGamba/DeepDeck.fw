# DeepDeck Command Center: desktop app over BLE and USB instead of the WiFi web portal

- **Status:** approved. Phase 0 done ([results](2026-10-17-command-center-spike-results.md)); next is Phase 1
- **Roadmap item:** R4
- **Areas:** both

## Context

Today, configuring the DeepDeck means:
1. joining its open WiFi AP (`AHUYAMA` / `DeepDeck`, from `main/wifi_handles.h`),
2. opening the web page that the ESP32 serves from SPIFFS,
3. entering the home WiFi credentials,
4. switching the PC back to its normal network.

The owner finds this awkward. The web app must also be compiled into a 600 KB SPIFFS partition that is
86% full, and anyone near the device can join the AP and reconfigure it.

**Goal:** a cross-platform desktop app, the "Command Center", that talks to the DeepDeck over **BLE**
(the link it already has with the PC as a keyboard) and over the **USB cable** (CP210x serial). It
configures layers, macros, LEDs and WiFi credentials. Once it covers everything, the on-device web
server, the SPIFFS partition and the AP mode are removed.

**Owner decisions (2026-10-17):**
- Tauri app reusing the current Angular screens.
- Windows, macOS and Linux.
- Keep the web server as a fallback until the app works, then remove it.
- BLE and USB transports, both now.

## Design

### One protocol, three transports

The firmware handles **JSON messages**, one per line (NDJSON, `cJSON_PrintUnformatted` output contains no
newlines). The messages mirror the existing REST API, so the Angular models and the layer/macro JSON do
not change:

```
-> {"id":7,"method":"layers.get","params":{"uuid":"..."}}
<- {"id":7,"result":{...same JSON as GET /api/layers today...}}
<- {"id":7,"error":{"code":"not_found","message":"..."}}
```

Methods map to today's handlers in `firmware/main/server.c`:

| Method(s) | Current handler |
|-----------|-----------------|
| `device.info` | `config_url_handler`, extended with protocol version 1, BLE name, WiFi status and IP |
| `wifi.set`, `wifi.status` | `connect_url_handler`, `wifiInit` state |
| `layers.names` | `get_layerName_url_handler` |
| `layers.get`, `layers.create`, `layers.update`, `layers.delete`, `layers.restore` | the layer handlers |
| `macros.list`, `macros.create`, `macros.update`, `macros.delete`, `macros.restore` | the macro handlers |
| `led.set` | `change_keyboard_led_handler` |

### Transports

- **HTTP:** the existing endpoints become thin wrappers around the same functions, and stay until Phase 5.
- **BLE:** a new GATT service is registered as a third app next to battery and HID
  (`esp_ble_gatts_app_register` in `components/ble/hal_ble.c`). It has:
  - a 128-bit custom UUID;
  - an **RX** characteristic (write and write-without-response) and a **TX** characteristic (notify),
    both requiring an **encrypted/bonded** link (`ESP_GATT_PERM_*_ENCRYPTED`);
  - messages split into chunks of `MTU-3` bytes and reassembled until `\n`.

  `MAX_MTU 517` is defined in `hal_ble.c` but `esp_ble_gatt_set_local_mtu` is never called. Add the call.
- **USB serial (UART0, 115200):** UART0 is also the log console. Install the UART driver, add a reader
  task, and prefix protocol lines with a marker (`@DD `) written in one `uart_write_bytes` call, so the
  app can tell them apart from interleaved `ESP_LOG` lines. The app must open the port with
  **DTR/RTS de-asserted**: the CP210x auto-reset circuit reboots the ESP32 otherwise.

### Execution

All transports put requests on one queue, and a single `config_task` runs them in order. That avoids
concurrent edits of `key_layouts`: `nvs_load_layouts` frees and re-mallocs it while `key_reports` reads it.

### WiFi and BLE together

`CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE` is **off** in the current `sdkconfig`, although WiFi and BLE already
run together. Enable it in `firmware/sdkconfig.defaults`, then delete `firmware/sdkconfig` so it is
regenerated. This is required for "send WiFi credentials over BLE, then connect while BLE stays up".

### App (Tauri v2)

- `web/src-tauri/`, the standard Tauri layout. `frontendDist` = `../dist/esp-frontend`.
- **Rust side:**
  - BLE: own WinRT module on Windows (btleplug cannot reach a connected keyboard, D-015), btleplug on Linux, macOS to be decided; connect, subscribe to TX, write to RX.
  - the `serialport` crate for USB: list ports and open them with DTR/RTS off.
  - Commands `transport_list`, `transport_connect`, `transport_send`, and an event per received line.
- **Angular side:**
  - a `DeviceTransport` interface with `request(method, params): Observable`;
  - implementations `TauriBleTransport`, `TauriSerialTransport` and a temporary `HttpTransport`;
  - `ApiService` (`web/src/app/core/services/api/api.service.ts`) keeps its public methods and calls
    the transport instead of `HttpClient`;
  - the IP-address/network page becomes a **Connect** page (pick the DeepDeck over BLE or USB) plus a
    **WiFi** page (SSID and password, sent with `wifi.set`, then live status).
- **Installers:** GitHub Actions with `tauri-action`, on a Windows, macOS and Linux matrix. macOS and Linux
  cannot be built on the owner's Windows PC.

### Main technical risk

A BLE HID keyboard connected to the PC usually **stops advertising** (`hal_ble.c` only restarts
advertising on disconnect), and the OS owns the link. Can a desktop app still open the custom GATT
service on Windows, macOS and Linux?
- Windows: WinRT `BluetoothLEDevice.FromBluetoothAddressAsync` on a paired device.
- macOS: `retrieveConnectedPeripherals`.
- Linux: BlueZ lists connected devices.

btleplug's coverage of these cases is not guaranteed. **Phase 0 checks this first.** If btleplug fails,
the fallback is a small per-OS path in Rust, or keeping advertising on while connected
(`CONFIG_BTDM_CONTROLLER_BLE_MAX_CONN` = 3 allows it).

## Phases

Each phase gets its own plan file in `docs/plans/`, `CHANGELOG.md` entries, and a firmware build with
ESP-IDF 4.4.7 plus a web build after every step.

0. **Spike (throwaway):**
   - firmware: a minimal echo GATT service;
   - a bare Tauri app with btleplug that finds the bonded DeepDeck while it types as a keyboard, then
     sends and receives a line;
   - the same over USB serial without resetting the board.

   Test on Windows; on macOS/Linux when available. **Gate:** go or no-go, and the BLE approach per OS.
   Prerequisite: install Rust (rustup, MSVC C++ build tools). WebView2 ships with Windows 11.
1. **Firmware config core:**
   - new `firmware/main/config_api.c/.h`: a method dispatcher taking `cJSON*` and returning `cJSON*`,
     with the logic moved out of the `server.c` handlers;
   - the HTTP handlers call it, so the web page keeps working;
   - add `config_task` and its queue;
   - enable coexistence;
   - `device.info` reports protocol version 1.
2. **Firmware transports:**
   - `firmware/components/ble/config_service.c`: GATT service, MTU, chunking;
   - `firmware/main/serial_transport.c`: UART0 reader and `@DD ` framing.
3. **Command Center app:** Tauri project, Rust transport commands, Angular transport layer, and the
   Connect and WiFi pages. The layer, macro and LED pages are reused unchanged.
4. **Release:** CI installers for the three OSes, README and CLAUDE.md updates.
5. **Remove the web portal**, after the owner confirms the app covers everything:
   - delete the HTTP server, SPIFFS, `spiffs_image`, `deploy-to-firmware.js` and AP mode; STA only, with
     credentials from the app;
   - repartition: drop `www_0` and keep the `nvs` offset, so layers survive. The freed flash goes to the
     OTA app slots;
   - this needs a full flash.

## Critical files

- **Firmware:**
  - `firmware/main/server.c`: logic moves to `config_api.c`
  - `firmware/main/wifi_handles.c`: AP/STA, credentials
  - `firmware/components/ble/hal_ble.c` and `hid_device_le_prf.c`: GATT app registration, MTU, advertising
  - `firmware/main/main.c`: task start-up
  - `firmware/sdkconfig.defaults`, `firmware/partitions.csv` (Phase 5)
- **Web/app:**
  - `web/src/app/core/services/api/api.service.ts`
  - `web/src/app/modules/network/`: becomes Connect and WiFi
  - new `web/src/app/core/services/transport/`
  - new `web/src-tauri/`
- **Docs:** `docs/plans/` (one plan per phase), `docs/plans/ROADMAP.md` (new item R4), and
  `docs/traceability/DECISIONS.md` (D-010 to D-013: app over portal, Tauri, three OSes, BLE+USB, web
  removal later).

Reuse what exists: the cJSON building and parsing in the `server.c` handlers, the `nvs_*` functions in
`components/nvsfuncs`, `set_current_layout()` in `main/keypress_handles.c`, `wifi_init_sta` and
`wifi_scan_sta` in `main/wifi_handles.c`, and every existing Angular page.

## Verification

- **Each phase:** firmware build (ESP-IDF 4.4.7), `npm run build-prod`, and `npm run tauri build` from
  Phase 3.
- **On the device (owner):**
  - over BLE with the DeepDeck connected as a keyboard: read layers, edit a key, save, and see it on the
    OLED; the DeepDeck keeps typing during and after;
  - same over USB, and the board does not reset when the port is opened;
  - set WiFi credentials from the app: the DeepDeck joins the home network and BLE stays connected
    (checks coexistence);
  - power-cycle: the config persists;
  - after Phase 5: no `AHUYAMA` AP is visible, and the existing layers survive the repartition.
