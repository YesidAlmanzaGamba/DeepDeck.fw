# DeepDeck – guide for Claude

Firmware and web configurator for the **DeepDeck Ahuyama**: a 4x4-key ESP32 macropad with 2 rotary
encoders, a 128x64 SSD1306 OLED and per-key WS2812 RGB LEDs. It connects to the computer over
**BLE HID only** (the ESP32 has no native USB) and serves a web configurator over WiFi.

This repository is a fork that is being reworked as a whole. The upstream repos
(DeepSea-Developments `DeepDeck.Ahuyama.fw` and `DeepDeck.Web`) are dormant.

## Layout

| Path | Content |
|------|---------|
| `firmware/` | ESP-IDF project (C, FreeRTOS). `main/` = app, `components/` = drivers and libraries |
| `firmware/spiffs_image/` | **Generated.** Built web app, packed into the `www_0` SPIFFS partition. Never edit by hand |
| `web/` | Angular 15 configurator (`develop` branch of DeepDeck.Web, imported with `git subtree`) |
| `docs/protocol.md` | **The configuration protocol** (methods, objects, events, BLE/USB/HTTP framing). Change it in the same commit as the code |
| `docs/plans/` | Plans: one file per piece of work, written **before** implementing it |
| `docs/traceability/` | `CHANGELOG.md` (every change, with commits and how it was verified) and `DECISIONS.md` |
| `tools/cc-cli/cc_cli.py` | Protocol client: `--usb COM3` / `--ble`, `call <method> [json]`, `watch`, `selftest` |
| `web/src-tauri/` | The **Command Center** desktop app (Tauri v2, Rust): BLE/USB link (`link.rs`, `ble_win.rs`), native file dialogs. The UI is the Angular app in `web/` |

## Configuration core (Command Center, R4)

The firmware is being configured more and more from a desktop app (Tauri, Phase 3) instead of the
on-device web portal, which is removed in R4.5. Plan: `docs/plans/2026-10-17-command-center.md`.

- `firmware/main/config_api.c` holds **every** configuration action as a method (`layers.update`,
  `wifi.scan`, ...).
  - BLE (`components/ble/config_service.c`), USB (`main/serial_transport.c`) and the `/api` HTTP
    handlers (`main/server.c`, thin wrappers) all call it.
  - Add new configuration features there, not in `server.c`.
- `config_task.c` runs BLE and USB requests one at a time; never do the work in the BLE callback.
  - Events (`layer.changed`, `wifi.changed`) go through `config_task_broadcast_event()`.
  - Methods that restart the device use `defer()`, so the reply is sent first.
- `key_layouts` and `user_macros` are swapped when the configuration changes. Read them under
  `layouts_lock()`/`layouts_unlock()`, and write them only through the `nvs_*` load functions.
- The BLE config service registers only **after** the HID service has started: Bluedroid builds one
  attribute table at a time. Hosts paired with older firmware must re-pair once (Windows caches GATT).
- Test after firmware changes: `python -I tools/cc-cli/cc_cli.py --usb COM3 --ble selftest` must give
  0 failures. It restores everything it changes.
- Free heap is about 38–39 KB with BLE, WiFi and the web server running. Check `device.info.heap` after
  adding features, and look for leaks with repeated writes.

## Building

### Firmware: ESP-IDF **4.4.7** only

- Installed at `D:\ESP-IDF\esp-idf-4.4.7`, tools in `D:\ESP-IDF\Espressif`, Python venv
  `python_env\idf4.4_py3.8_env`. ESP-IDF 5.x is also installed on this machine but **does not build
  this code** (legacy drivers: PCNT, RMT, ADC, I2C, `gpio_pad_select_gpio`, ...).
- VS Code: open `firmware/`. `firmware/.vscode/settings.json` (git-ignored, machine specific) points the
  ESP-IDF extension 1.11 at 4.4.7. The extension derives the venv from `idf.pythonInstallPath`
  (`idf<IDF>_py<python>_env`), so that setting must be the **Python 3.8.7** base interpreter.
- CLI: `idf.py -C firmware build`, `idf.py -C firmware -p COM3 flash monitor` (COM3 = CP210x on this PC).
  To flash without reconfiguring, run `esptool.py --chip esp32 -p COM3 -b 460800 write_flash @flash_args`
  from `firmware/build`. Close the VS Code serial monitor first, otherwise COM3 is locked.
- `firmware/sdkconfig` is generated and git-ignored. When you change `sdkconfig.defaults`, either mirror
  the change into `sdkconfig` (including the deprecated alias lines) or delete `sdkconfig`. Otherwise
  the old value stays.
- The project path contains a space (`D:\Personal Yesid\...`). ESP-IDF 4.4 breaks if a **project
  component has a `Kconfig` file** in such a path. Do not add `Kconfig` files under
  `firmware/components/`; use `#ifndef` defaults in the source instead.

### Web

```sh
cd web
npm install                # Node 22 works despite Angular 15 targeting 14/16/18
npm run build-deepdeck     # ng build (production) + scripts/deploy-to-firmware.js
```

`deploy-to-firmware.js` gzips js/css/svg, copies only referenced assets, replaces
`firmware/spiffs_image`, and fails if the image is larger than `www_0` (600 KB, about 86% used).
Then rebuild and flash the firmware: flashing writes `www_0` too.

`npm install` rewrites `package-lock.json` and `yarn.lock`. Revert them unless dependencies changed on purpose.

### Command Center app (Tauri)

Rust and the VS Build Tools are under `D:\DevTools` (`RUSTUP_HOME=D:\DevTools\Rust\rustup`,
`CARGO_HOME=D:\DevTools\Rust\cargo`, add `cargo\bin` to `PATH`). From `web/`:

```sh
npx --yes @tauri-apps/cli@2.12.3 dev      # ng serve + the app window
npx --yes @tauri-apps/cli@2.12.3 build    # release exe + NSIS installer (runs build-prod first)
```

The CLI is run through `npx` on purpose (D-026). Checks:
- `DD_CC_SELFTEST=1 [DD_CC_SELFTEST_KIND=usb] deepdeck-command-center.exe` connects to the first
  DeepDeck and calls `device.info`, `layers.list`, `led.get`, `led.effects`, then exits.
- The pages can be driven by script through the WebView2 DevTools port: start the exe with
  `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS="--remote-debugging-port=9333 --remote-allow-origins=*"`.

In the Angular code, talk to the DeepDeck only through `core/services/device/device.service.ts`
(`call(method, params)`, `on(event)`). It uses the app's link inside Tauri and maps onto `/api` in
a plain browser. The rxjs version is 6: no `firstValueFrom`, `throwError(() => ...)` or ES2019 array
methods.

## Architecture notes that are easy to miss

- `deepdeck_tasks.c` `#include`s `keypress_handles.c`, which `#include`s `keymap.c`. Both are also listed
  as sources, but the linker only uses the copy inside `deepdeck_tasks.o`. Keep that in mind when adding globals.
- Keycodes are `uint16_t` ranges, see `firmware/main/keyboard_config.h`:
  - 0x00–0xEF: HID keys and media keys
  - 0xF0–0xFF: reserved, the old mouse keys, which are ignored
  - 291–308: layer hold
  - 401–402: layer up/down
  - 500–600: macros
- Layers are NVS blobs (`dd_layer`, namespace `layers`, keys `layer_N`), and the active layer is
  stored under `cur_layer`. **Changing `dd_layer` changes the blob size:** add a migration like
  `nvs_migrate_layer_v1()` so layers already on the device are not lost.
- Change layers only through `set_current_layout()`. It refreshes the OLED, tells the LED task
  (`rgb_led_layer_changed()`) and saves the layer 3 s after the last change.
- LEDs: `components/rgb_led` renders frames in its own task. Change the mode with `rgb_led_apply()`,
  never by drawing pixels elsewhere. Per-layer key colours are in NVS namespace `led_keys`
  (`k_<uuid>`); deleting or restoring layers must erase them (`rgb_layer_colors_erase*`).
- The REST API (`firmware/main/server.c`) and `web/src/app/core/services/api/api.service.ts` are one
  contract: change both in the **same commit**.
- The BLE HID report descriptor still declares mouse and joystick reports. That is on purpose:
  changing it forces every paired host to re-pair.
- The I2C bus and the `i2s_user_lock` mutex in `main.c` are required by the OLED, even though the
  gesture sensor is gone.

## Removed on purpose, do not re-add

- APDS-9960 gesture sensor
- Mouse actions

See `docs/traceability/DECISIONS.md`.

## Workflow

1. Non-trivial work starts with a plan in `docs/plans/YYYY-MM-DD-<topic>.md`, using the template in
   `docs/plans/README.md`. Update its status as work progresses.
2. Build after every step. The firmware must build with ESP-IDF 4.4.7. The web app must pass
   `npm run build-prod`.
3. Every change gets an entry in `docs/traceability/CHANGELOG.md`: what changed, why, the commits, how it
   was verified, and whether it was tested **on hardware**. Every product or architecture decision goes
   in `DECISIONS.md`.
4. Work on a branch, never directly on `main`. Do not push without the owner asking. Git has no global
   identity on this PC, so commit with
   `git -c user.name="yesid.almanza0108" -c user.email="yesid.almanza0108@gmail.com" commit ...`.
