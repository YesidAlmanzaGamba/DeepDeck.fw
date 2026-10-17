# DeepDeck

Firmware and web configurator for the DeepDeck Ahuyama macropad (ESP32), kept in one
repository so changes to the API between them land in a single commit.

| Folder | What it is | Built with |
|--------|------------|------------|
| [`firmware/`](firmware/README.md) | ESP32 firmware (keys, knobs, OLED, LEDs, BLE HID, WiFi + REST API) | ESP-IDF **v4.4** |
| [`web/`](web/README.md) | Angular configurator served by the DeepDeck from its SPIFFS partition | Node + Angular CLI |

## Building the firmware

```sh
idf.py -C firmware build
idf.py -C firmware -p COM3 flash monitor
```

ESP-IDF 4.4 breaks when a component *inside the project* has a `Kconfig` file and the project
path contains spaces. Keep project components free of `Kconfig` files (use `#ifndef` defaults
in the source instead) so the firmware builds from any folder.

## Building the web app into the firmware

```sh
cd web
npm install
npm run build-deepdeck   # builds, gzips and copies the output into firmware/spiffs_image
```

The next firmware build packs `firmware/spiffs_image` into the `www_0` partition.

## History

`firmware/` comes from [DeepDeck.Ahuyama.fw](https://github.com/DeepSea-Developments/DeepDeck.Ahuyama.fw)
and `web/` from the `develop` branch of
[DeepDeck.Web](https://github.com/DeepSea-Developments/DeepDeck.Web), imported with `git subtree`
so both histories are preserved.
