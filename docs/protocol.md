# DeepDeck configuration protocol, version 1

The single contract between the firmware (`firmware/main/config_api.c`) and its clients: the
Command Center app and `tools/cc-cli/cc_cli.py`. **Change this file in the same commit as the code.**

The HTTP transport and the on-device web page were removed with the WiFi portal in firmware 0.7.0 (R4.5).

## Transports and framing

Every message is **one JSON object on one line** (UTF-8, terminated by `\n`, no newline inside).
Lines are at most **4096 bytes** in either direction.

| Transport | How a line travels |
|-----------|--------------------|
| BLE | GATT service `dd0e0001-7f4b-4c1a-9d3e-2a6b1c5e8f00`. The app writes to RX `dd0e0002-…` (write or write-without-response) in chunks of at most MTU−3 bytes **and never more than 512** (the largest BLE attribute value; longer writes are dropped silently); the device notifies on TX `dd0e0003-…` in chunks of at most MTU−3 bytes. Both sides join chunks until `\n`. Both characteristics require an encrypted (bonded) link. The device asks for MTU 517. |
| USB | UART0 at 115200 8N1, shared with the log console. Protocol lines start with `@DD ` in both directions; every other line is log output. Open the port with **DTR and RTS de-asserted**, otherwise the auto-reset circuit reboots the board. |

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
  - 0x2000–0x27FF: mod-tap, `0x2000 + modifier * 256 + key` (modifier 0 LCtrl, 1 LShift, 2 LAlt,
    3 LGui, 4–7 the right ones): tap = the key, held over 200 ms (or with another key) = the modifier
  - 0x3000–0x300F: tap-dance 1–16 (`keys.tapdance.*`)
  - 0x4000–0x401F: sequences 1–32 (`keys.sequences.*`)

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
| `wifi.status` | none | `{"state":"connected"\|"connecting"\|"not_set"\|"off","ssid":"...","ip":"...","rssi":-50}`; `not_set`: no network stored (0.7.0; older firmware said `ap` for its setup network) |
| `wifi.scan` | none | `{"aps":[{"ssid":"...","rssi":-40,"channel":6,"auth":"WPA2_PSK"}]}`; takes about 2–3 s |
| `wifi.set` | `{"ssid":"...","pass":"..."}` | `{}`; the network is stored (the password is never logged) and joined at once, without a restart (0.7.0; older firmware restarted). `wifi.changed` follows when it gets an address. A network that cannot be reached is retried 5 times at once, then every 30 s |
| `wifi.forget` | none | `{}`; the stored network is erased and WiFi disconnects |
| `device.settings.get` | none | `{"screen_brightness":255,"screen_off_minutes":0,"leds_off_with_screen":false}` |
| `device.settings.set` | any of those fields (`screen_off_minutes` 0 = never, up to 1440) | the stored settings; applied at once. Keys, knobs and layer changes wake the screen. `device.info.battery` is `null`: battery monitoring is off on this hardware |
| `keys.tapdance.list` | none | `{"tapdance":[{"id":0,"key_code":12288,"tap":4,"double":5,"hold":225}, ... 16]}` |
| `keys.tapdance.set` | `{"id":0,"tap":4,"double":5,"hold":225}` (codes; 0 = nothing; omitted = unchanged) | the entry. A tap-dance cannot contain a tap-dance or mod-tap (sequences are fine) |
| `keys.sequences.list` | none | `{"sequences":[{"id":0,"key_code":16384,"name":"Sign","steps":[...],"bytes":21}, ... 32]}` |
| `keys.sequences.set` | `{"id":0,"name":"Sign","steps":[{"text":"Best regards"},{"delay":200},{"keys":[224,6]}]}`; empty name and steps delete it | the entry; `limit` past 240 encoded bytes. `keys`: 1–6 HID codes pressed together (modifiers 224–231 included); `delay`: 0–60000 ms; `text`: up to 200 ASCII characters, typed as on a US layout |
| `keys.sequences.run` | `{"id":0}` | `{}`; the DeepDeck types the sequence (into the computer's focused window) |
| `fw.info` | none | `{"version":"0.7.0","idf":"v4.4.7","built":"Oct 18 2026 03:14:05","running":"app0","state":"valid","slot_size":1536000,"chunk_max":2048,"other_version":"0.7.0"}` |
| `fw.begin` | `{"size":1328672,"sha256":"<64 hex>"}` (SHA-256 of the whole file) | `{"chunk_max":2048,"slot":"app1"}`; starts an update into the other slot (a previous unfinished one is dropped) and asks the BLE host for a fast link |
| `fw.chunk` | `{"offset":0,"data":"<base64>"}`: at most `chunk_max` bytes, offsets in order | `{"written":2048}`. The first chunk must be a DeepDeck image (`bad_request` otherwise and the update is dropped) |
| `fw.end` | none | `{"version":"0.7.1"}`, then the device restarts into the new firmware. `bad_request` if the image is incomplete, corrupt, or its SHA-256 differs |
| `fw.abort` | none | `{}`; drops an unfinished update |

**Firmware update.** `fw.begin`, one `fw.chunk` per piece, `fw.end`. The new firmware must mark
itself valid when it has started (end of `app_main`); one that crashes before that is rolled back
to the previous firmware by the bootloader. Measured: about 8 KB/s over USB with 3 chunks in flight
(the UART interrupt runs from IRAM, so nothing is lost while flash is written); about 2 KB/s over
BLE, one chunk at a time (more in flight is not faster). Layers, macros and settings (NVS) are not
touched.

## Events

| Event | Data | When |
|-------|------|------|
| `layer.changed` | `{"index":1,"uuid":"...","name":"Numpad","hold":false}` | Any layer change: keys, knobs, the app, or a layer-hold key (`hold: true` while held) |
| `wifi.changed` | `{"state":"connected","ip":"192.168.1.20"}` | The WiFi station connects or loses its IP |

## Versioning

`device.info.proto` is 1. New methods, new optional params and new result fields do not change it;
clients must ignore fields they do not know. Removing or changing the meaning of something does.
