# DeepDeck

Firmware, configurator and desktop app for the **DeepDeck Ahuyama** macropad: 16 keys, 2 knobs, a
128x64 OLED and per-key RGB LEDs on an ESP32, connected to the computer over Bluetooth.

This is a fork of the original DeepSea Developments projects, reworked as one repository so the
firmware, the protocol and the apps change together.

| Folder | What it is | Built with |
|--------|------------|------------|
| [`firmware/`](firmware/README.md) | ESP32 firmware: keys, knobs, OLED, LEDs, BLE keyboard, configuration protocol | ESP-IDF **4.4.7** |
| [`web/`](web/README.md) | Angular screens of the Command Center | Node + Angular 15 |
| `web/src-tauri/` | **DeepDeck Command Center**, the desktop app (Tauri v2, Rust) | Rust + Tauri CLI |
| [`docs/protocol.md`](docs/protocol.md) | The configuration protocol (BLE, USB and HTTP) | |
| `tools/cc-cli/` | Command-line protocol client and hardware self-test | Python |
| [`docs/plans/`](docs/plans/ROADMAP.md), [`docs/traceability/`](docs/traceability/CHANGELOG.md) | Roadmap, plans, change log and decision log | |

## What changed from the original

- **Configure from a desktop app, not the WiFi portal.**
  - Every setting is one method of a JSON protocol (`firmware/main/config_api.c`).
  - That protocol runs over Bluetooth and the USB cable.
  - The WiFi setup network and the web page on the device are gone (firmware 0.7.0). That freed
    26 KB of RAM, and WiFi now only joins your network, set from the app without a restart.
- **Lighting v2:**
  - 11 effects: off, reactive, colour cycle, rainbow wave, solid, per-key, breathing, ripple,
    gradient, twinkle, and layer colour;
  - brightness, speed, two colours, and lighting up pressed keys with any effect;
  - **key colours per layer**;
  - live preview from the app.

  The old per-key "Color" tab never worked, and the old rainbow effect flickered.
- **New main screen.** The yellow band shows the active layer's name, its position and the
  Bluetooth and WiFi icons, and the 16 key names fill the rest of the screen as a 4x4 grid.
- **Settings menu** (press a knob): **Device info** has 3 pages (Bluetooth, WiFi, System).
  LED configuration and Restart are also there.
- **The active layer is remembered** across restarts and sleep, and layer-hold keys return to
  the previous layer.
- **Removed:** the APDS-9960 gesture sensor and mouse actions.
- **Bugs fixed on the way:** memory leaks, an out-of-bounds layer index, unchecked API input, the
  WiFi password in the log, and a firmware that did not link upstream.

Every change, with how it was verified, is in the [change log](docs/traceability/CHANGELOG.md);
every product decision is in the [decision log](docs/traceability/DECISIONS.md).

## DeepDeck Command Center

| Page | What it does |
|------|--------------|
| Connect | Lists paired DeepDecks (Bluetooth) and USB ports; reconnects by itself to the last one |
| Dashboard | Firmware, memory, uptime, connection, WiFi; click a layer to make it active (follows the device live) |
| Keymap, Macros | Edit layers, keys, knobs and macros |
| Lighting | Effect gallery with live preview, sliders and colours; paint key colours per layer (click or drag, fill, copy from another layer) |
| WiFi | Scan, pick a network, join |
| Device | Rename, restart, **back up** everything to a JSON file and restore it, restore the defaults |
| Log | The firmware's log (over USB) |

**Install.** Build the installer (see below). It ends up at
`web/src-tauri/target/release/bundle/nsis/DeepDeck Command Center_<version>_x64-setup.exe`.

**First connection:**
- Bluetooth: pair the DeepDeck in the computer's Bluetooth settings, as for a keyboard, then pick it
  in Connect. A computer that was paired with firmware older than 0.6.0 must remove the DeepDeck
  and pair it again once, because Windows caches the old services.
- USB: plug the DeepDeck's USB cable in. It shows as a CP210x port.

**Platforms.** Windows: Bluetooth and USB. macOS and Linux: USB now, Bluetooth later (roadmap R4.4).

## Building

### Firmware

ESP-IDF **4.4.7** (5.x does not build this code).

```sh
idf.py -C firmware build
idf.py -C firmware -p COM3 flash monitor
```

ESP-IDF 4.4 breaks when a component *inside the project* has a `Kconfig` file and the project
path contains spaces. Keep project components free of `Kconfig` files (use `#ifndef` defaults
in the source instead) so the firmware builds from any folder.

### Desktop app

You need Rust (stable), plus the MSVC Build Tools on Windows. From `web/`:

```sh
npx --yes @tauri-apps/cli@2.12.3 dev     # development, with live reload
npx --yes @tauri-apps/cli@2.12.3 build   # release exe and installer
```

### Testing with the device

```sh
python -I tools/cc-cli/cc_cli.py --usb COM3 --ble selftest     # protocol self-test, restores what it changes
DD_CC_SELFTEST=1 web/src-tauri/target/release/deepdeck-command-center.exe   # app link self-test
```

The self-test script needs `bleak` and `pyserial`.

## History

`firmware/` comes from [DeepDeck.Ahuyama.fw](https://github.com/DeepSea-Developments/DeepDeck.Ahuyama.fw)
and `web/` from the `develop` branch of
[DeepDeck.Web](https://github.com/DeepSea-Developments/DeepDeck.Web), imported with `git subtree`
so both histories are preserved.
