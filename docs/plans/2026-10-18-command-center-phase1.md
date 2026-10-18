# Command Center, Phase 1: one configuration core over BLE, USB and HTTP

- **Status:** in progress
- **Roadmap items:** R4.1, R4.2
- **Areas:** firmware, tools

## Context

The Command Center (R4) replaces the WiFi AP plus on-device web portal with a desktop app that talks to
the DeepDeck over BLE and USB. The overall plan is in `docs/plans/2026-10-17-command-center.md`.

**Phase 0 (spike) passed on 2026-10-17.** A Tauri app reached the paired DeepDeck over BLE while it was
typing as a keyboard, and over USB without resetting it. The owner confirmed that the keys keep working.
Results are in `docs/plans/2026-10-17-command-center-spike-results.md`.

**Phase 1 is the firmware side.** Today every configuration action (layers, macros, LEDs, WiFi) lives
inside an HTTP handler in `firmware/main/server.c`, mixed with HTTP parsing. Only the web page can use
it. Phase 1 moves that logic into one core, so that **BLE, USB and the existing web page all call the
same code**.
- Nothing new is visible to the user yet: the web page keeps working exactly as before.
- At the end, every app function (Phase 3) has a tested firmware command behind it.

Two changes compared with the original plan:
- **The transports move into this phase.** The original plan had them in Phase 2, but the spike already
  proved them (`config_service.c`, `serial_transport.c`).
- **The owner asked for extra useful functions in the app.** The protocol therefore adds a few commands
  the web page never had, listed below.

## What gets built

### 1. Protocol v1: `docs/protocol.md`, the single source of truth for firmware and app

- **Framing:** one JSON object per line.
  - BLE: RX write and TX notify, chunks of MTU-3, `\n` terminated.
  - USB: lines prefixed with `@DD ` in both directions.
  - HTTP: the existing endpoints, unchanged.
- **Messages:**
  - request `{"id":N,"method":"...","params":{...}}`
  - response `{"id":N,"result":...}` or `{"id":N,"error":{"code":"...","message":"..."}}`
  - device-initiated event (no `id`): `{"event":"...","data":{...}}`
- **Error codes:** `bad_request`, `not_found`, `limit`, `busy`, `internal`.
- **Results reuse today's REST JSON shapes**, so the Angular models do not change. For example,
  `layers.list` returns `{"data":[...]}` and `macros.list` returns `{"macros":[...]}`.

| Method | Params | Source today |
|--------|--------|--------------|
| `device.info` | none | `config_url_handler`. Returns proto 1, firmware version, BLE name, BLE MAC, uptime, free heap, active layer, WiFi state/SSID/IP/RSSI |
| `device.restart` | none | new |
| `device.set_name` | `name` | new. Writes NVS `config_c/btname`, which `main.c` already reads; applies after restart |
| `layers.list` | none | `get_layerName_url_handler` |
| `layers.get` | `uuid` | `get_layer_url_handler` |
| `layers.create` / `layers.update` | layer JSON | create and update handlers |
| `layers.delete` | `uuid` | `delete_layer_url_handler` |
| `layers.restore` | none | restore handler (also restores macros, as today) |
| `layers.set_active` | `uuid` | new, via `set_current_layout()` |
| `macros.list` / `macros.create` / `macros.update` | macro JSON | macro handlers |
| `macros.delete` | `keycode` | **fixed**: it does nothing today |
| `macros.restore` | none | restore handler |
| `led.get` | none | new, `nvs_load_led_mode` |
| `led.set` | mode, H, S, V, speed, rgb | `change_keyboard_led_handler` |
| `wifi.status` | none | new |
| `wifi.scan` | none | new. Returns records; `wifi_scan_sta` only prints them |
| `wifi.set` | `ssid`, `pass` | `connect_url_handler`. Same behaviour: saves the credentials, then restarts |

**Events:**
- `layer.changed {index,uuid,name}`, from `set_current_layout()` and from layer hold;
- `wifi.changed {state,ip}`, from the WiFi event handler.

Backup and restore of the whole configuration needs no firmware command: the app builds it from
`layers.list`, `layers.get` and `macros.list`.

### 2. Firmware

- **`firmware/main/config_api.c/.h` (new):**
  - `cJSON *config_api_call(const char *method, cJSON *params, config_err_t *err)` is a table-driven
    dispatcher, plus `config_api_handle_line()`, which parses a line, dispatches it and builds the response.
  - The logic is moved out of the `server.c` handlers and reuses the `nvs_*` functions in
    `components/nvsfuncs`, `set_current_layout()` in `main/keypress_handles.c`, and
    `wifi_init_sta`/`resetWifi` in `main/wifi_handles.c`.
  - It is validated as it moves, which fixes the bugs found while reading the handlers:
    - layer JSON: missing `uuid`/`row*` dereferences NULL; `left_encoder_map` and `right_encoder_map`
      are written without a bounds check (memory overwrite, R1.4); `return 1` with no HTTP response;
      `temp_layout` is uninitialised;
    - macros: the key range check `x >= 0 || x <= 1000` is always true; name limits are 6 or 7
      depending on the handler; `dd_macros` is not zeroed; `macros.delete` does nothing, and
      `nvs_delete_macro` writes into the **layers** namespace;
    - LED: saturation is validated by checking `hue`;
    - leaks: `layer_names` does `malloc(100)` and then overwrites the pointer; `get_layer` leaks `buf`
      on its early return.
- **`firmware/main/config_task.c/.h` (new, grown from the spike's `spike_echo.c`):**
  - one queue and one task that run every BLE and USB request in order;
  - the HTTP handlers call `config_api_call` under the same mutex, so all three transports are serialised;
  - `config_task_broadcast_event()` sends events to the connected BLE client and to USB.
- **`firmware/main/server.c`:** every handler becomes a thin wrapper. It turns the query or body into
  `params`, calls the core, and sends the result with the same HTTP status and body as today.
- **Ported from the spike:**
  - `components/ble/config_service.c/.h`, with the spike-only advertise-while-connected `#define` and
    the extra logging removed;
  - registering it after the HID service has started (`hid_device_le_prf.c`);
  - `esp_ble_gatt_set_local_mtu(517)` in `hal_ble.c`;
  - the Service Changed indication after encryption. It is harmless, and helps hosts that do honour it.
    Re-pairing is still documented.
  - `main/serial_transport.c/.h`;
  - `sys_evt` stack set to 4096.

  Only the code moves, not the spike branch, which stays as a record.
- **Safe access to `key_layouts`:**
  - Today `nvs_load_layouts()` frees and re-mallocs `key_layouts` while `key_reports`, the encoder task
    and the OLED read it. Edits from the app make this more frequent.
  - Fix: it builds the new array first, then swaps the pointer under a short `layouts_lock`, which the
    readers also take.
- **`sdkconfig.defaults`:**
  - turn on WiFi/BLE software coexistence (`CONFIG_SW_COEXIST_ENABLE=y`, renamed to
    `CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE`);
  - **print the crash and reboot** instead of freezing in the GDB stub (`CONFIG_ESP32_PANIC_PRINT_REBOOT=y`,
    `CONFIG_ESP32_PANIC_GDBSTUB=`);
  - mirror both into the local, git-ignored `firmware/sdkconfig`.

### 3. Test tool: `tools/cc-cli/cc_cli.py` (Python, pyserial + bleak; not a spike)

- `cc_cli.py --usb COM3 call device.info`, `cc_cli.py --ble call layers.list`, and `watch`, which prints
  events.
- `selftest` runs a read-only round over every method, plus a reversible write round: rename a layer and
  restore it, change the LED and restore it. It compares BLE, USB and HTTP results.
- The web page is checked through the real AP or STA IP.

## Steps (one commit each, firmware builds with ESP-IDF 4.4.7 after each)

1. Branch `command-center` (has the plan docs). Write `docs/protocol.md`.
2. Port the transports from the spike, plus the registration order, MTU, `sys_evt` stack, coexistence
   and panic reboot. `config_task` initially answers only `device.info`. Flash, check over BLE and USB.
3. `config_api.c` with the layer methods; turn the layer HTTP handlers into wrappers.
4. Macro, LED, device and WiFi methods; the remaining HTTP handlers become wrappers; events.
5. `layouts_lock`.
6. `cc_cli.py` with `selftest`. Run it over BLE and USB, and run the web page on the device.
7. Docs: plan status, `CHANGELOG` CH-012 onwards, `DECISIONS`, `ROADMAP` (R4.1 and R4.2 done), `CLAUDE.md`.

## Not in Phase 1

- The app UI, which is Phase 3. The owner delegated the choice of extra app functions; the proposal is
  recorded in the plan doc for Phase 3:
  - device dashboard;
  - set the active layer, with a live indicator driven by events;
  - WiFi scan and connect;
  - rename the device;
  - backup and restore to a file;
  - USB log console;
  - restart.
- Later candidates (roadmap): firmware update from the app (merges with R3.3), and a key tester (needs a
  key event).
- Removing the web portal is Phase 5.

## Verification

- **Each step:** clean firmware build. Flash to COM3 (the VS Code monitor must be closed), then
  45 s of boot logging with no resets.
- **`cc_cli.py selftest`:** every method answers over BLE and USB with identical results; the write round
  restores the original state; `layer.changed` arrives when a layer key on the device is pressed.
- **Web page on the device:** list, edit and save a layer, change a macro, change the LED, all as before.
- **Owner on hardware:** the keys type while the app and CLI are connected; WiFi credentials set over BLE
  connect without dropping BLE (coexistence); a power cycle keeps the configuration.
