# DeepDeck configuration protocol, version 1

The single contract between the firmware (`firmware/main/config_api.c`) and its clients: the
Command Center app, `tools/cc-cli/cc_cli.py`, and the web page (through HTTP). **Change this file
in the same commit as the code.**

## Transports and framing

Every message is **one JSON object on one line** (UTF-8, terminated by `\n`, no newline inside).
Lines are at most **4096 bytes** in either direction.

| Transport | How a line travels |
|-----------|--------------------|
| BLE | GATT service `dd0e0001-7f4b-4c1a-9d3e-2a6b1c5e8f00`. The app writes to RX `dd0e0002-…` (write or write-without-response) in chunks of at most MTU−3 bytes; the device notifies on TX `dd0e0003-…` in chunks of at most MTU−3 bytes. Both sides join chunks until `\n`. Both characteristics require an encrypted (bonded) link. The device asks for MTU 517. |
| USB | UART0 at 115200 8N1, shared with the log console. Protocol lines start with `@DD ` in both directions; every other line is log output. Open the port with **DTR and RTS de-asserted**, otherwise the auto-reset circuit reboots the board. |
| HTTP | The existing `/api/...` endpoints of the web page, kept until the portal is removed (R4.5). They call the same code and return the same bodies as the `result` below; errors come back as `{"error":{...}}` with status 400, 404 or 500. `/api/config` also carries the legacy `FWVersion`, `Mac` and `ssid` fields. |

Hosts that paired with firmware older than the Command Center must **remove and pair the DeepDeck
again once**. Windows caches the old GATT database and does not see the config service otherwise.

## Messages

| Kind | Shape |
|------|-------|
| Request (app → device) | `{"id":7,"method":"layers.get","params":{"uuid":"..."}}`. `params` may be omitted. `id` is any integer chosen by the app. |
| Response | `{"id":7,"result":{...}}` or `{"id":7,"error":{"code":"not_found","message":"layer not found"}}` |
| Event (device → app) | `{"event":"layer.changed","data":{...}}`, with no `id` |

Requests are executed one at a time, in arrival order, across all transports.

Error codes:
- `bad_request`: malformed JSON or missing or invalid params
- `not_found`
- `limit`: for example, deleting the last layer, or a full layer table
- `unknown_method`
- `internal`

## Objects

**Layer:**
```json
{"uuid":"5b1c...","name":"Media","active":true,
 "row0":[{"name":"Play","key_code":232},...4 keys], "row1":[...], "row2":[...], "row3":[...],
 "left_encoder_map": {"cw":0,"ccw":0,"single_press":0,"long_press":0,"double_press":0},
 "right_encoder_map":{"cw":0,"ccw":0,"single_press":0,"long_press":0,"double_press":0}}
```
- Key names are at most 6 characters, and the layer name 1 to 14 (the web page limits it to 8). The
  encoder actions are matched by name; missing ones are 0.
- Key codes are listed in `firmware/main/keyboard_config.h`:
  - 0–239: HID keys and media keys
  - 240–255: reserved and ignored
  - 291–308: layer hold
  - 401–402: layer up and down
  - 500–600: macros

**Macro:**
```json
{"keycode":500,"name":"Copy","key":[224,6,0,0,0]}
```
- `keycode` is in 500–600 and is the macro's position (500 + index), `name` is at most 9 characters,
  and `key` holds up to 5 HID key codes (0–255, modifiers 224–231) pressed together (0 = none).

**LED mode** (lighting v2, firmware 0.6.0 and later):
```json
{"effect":6,"brightness":128,"speed":120,"color1":[255,136,0],"color2":[255,255,255],"reactive":true,
 "mode":6,"H":0,"S":255,"V":128,"rgb":[255,136,0]}
```
- Effects: 0 off, 1 reactive, 2 colour cycle, 3 rainbow wave, 4 solid, 5 per-key (the active layer's
  key colours), 6 breathing, 7 ripple, 8 gradient, 9 twinkle, 10 layer colour. `led.effects` lists them
  with the parameters each one uses; ids 0–5 kept their old meaning.
- `brightness`, `speed`: 0–255. `color1`, `color2`: `[r,g,b]`. `reactive`: pressed keys light in
  `color2` on top of any effect.
- `mode`, `H`, `S`, `V` and `rgb` are the old fields, still returned and accepted for the web page:
  `mode` is `effect`, `rgb` is `color1`, and `V` sets the brightness when `brightness` is not given.

**Key colours of a layer:**
```json
{"uuid":"87402f","keys":[[255,0,0], ... 16 colours, row by row],"layer":[255,136,0],"custom":true}
```
- `layer` is the layer's own colour (effect 10 and the 2 notification LEDs).
- `custom` is false while the layer has no stored colours. The defaults are then `color1` on keys
  that have a key code, off on empty keys, and a fixed colour per layer position.
- Stored in NVS namespace `led_keys`, one blob per layer, outside the layer itself.

## Methods

| Method | Params | Result |
|--------|--------|--------|
| `device.info` | none | `{"proto":1,"fw":"0.6.0","name":"Ahuyama","mac":"c0:49:ef:51:06:7a","uptime_s":120,"heap":43000,"layer":{"index":0,"uuid":"...","name":"Media"},"wifi":{"state":"connected","ssid":"...","ip":"192.168.1.20","rssi":-50},"ble_connected":true,"app_connected":false}`; `ble_connected`: a computer uses it as a keyboard, `app_connected`: an app is on the BLE config service |
| `device.restart` | none | `{}`; the device restarts about 300 ms later |
| `device.set_name` | `{"name":"Ahuyama"}` (1–20 characters) | `{}`; the BLE name changes after the next restart |
| `layers.list` | none | `{"data":[{"name":"Media","pos":0,"active":true,"uuid":"..."}]}` |
| `layers.get` | `{"uuid":"..."}` | Layer |
| `layers.create` | Layer; `uuid` (up to 6 characters) is optional and generated when missing | `{"uuid":"..."}`; `limit` past 10 layers |
| `layers.update` | Layer with `uuid` | `{}`; the active layer stays selected |
| `layers.delete` | `{"uuid":"..."}` | `{}`; `limit` if it is the last layer |
| `layers.restore` | none | `{}`; restores the default layers **and** macros |
| `layers.set_active` | `{"uuid":"..."}` | `{}`, then a `layer.changed` event |
| `macros.list` | none | `{"macros":[Macro,...]}` |
| `macros.create` | Macro without `keycode` | `{"keycode":540}`: the next free slot; `limit` past 600 |
| `macros.update` | Macro with `keycode` | `{}` |
| `macros.delete` | `{"keycode":512}` | `{}`; the macro is emptied (no keys, empty name) |
| `macros.restore` | none | `{}` |
| `led.get` | none | LED mode |
| `led.set` | LED mode (fields may be omitted), plus `save` (default `true`) | `{}`; applied at once. `save:false` only previews: nothing is written to flash and the next restart shows the stored mode |
| `led.effects` | none | `{"effects":[{"id":6,"name":"Breathing","uses":["speed","color1"]}, ...]}` |
| `led.keys.get` | `{"uuid":"..."}` | Key colours of that layer |
| `led.keys.set` | `{"uuid":"...","keys":[16 x [r,g,b]],"layer":[r,g,b],"save":true}`; `keys` and `layer` may be omitted | `{}`; shown at once if it is the active layer; `save:false` previews |
| `led.keys.reset` | `{"uuid":"..."}` | `{}`; back to the defaults |
| `wifi.status` | none | `{"state":"connected"\|"connecting"\|"ap"\|"off","ssid":"...","ip":"...","rssi":-50}` |
| `wifi.scan` | none | `{"aps":[{"ssid":"...","rssi":-40,"channel":6,"auth":"WPA2_PSK"}]}`; takes about 2–3 s |
| `wifi.set` | `{"ssid":"...","pass":"..."}` | `{}`; the credentials are stored (the password is never logged) and the device restarts to join the network |

## Events

| Event | Data | When |
|-------|------|------|
| `layer.changed` | `{"index":1,"uuid":"...","name":"Numpad","hold":false}` | Any layer change: keys, knobs, the app, or a layer-hold key (`hold: true` while held) |
| `wifi.changed` | `{"state":"connected","ip":"192.168.1.20"}` | The WiFi station connects or loses its IP |

## Versioning

`device.info.proto` is 1. New methods, new optional params and new result fields do not change it;
clients must ignore fields they do not know. Removing or changing the meaning of something does.
