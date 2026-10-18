# Command Center Phase 3, new lighting system, OLED redesign

- **Status:** done (2026-10-18); owner checks on the device are listed in CHANGELOG CH-017 to CH-021
- **Roadmap items:** R4.3, R5.1 (OLED and menu), R5.2 (lighting v2)
- **Areas:** firmware, app

## Context

Phase 1 (the configuration core, protocol v1, firmware 0.6.0) is done and pushed. The owner asked for
the next phase, the desktop app, plus three things they find lacking on the device:

1. **Custom RGB colours do not work and the effects are boring.**
   - The web app's "Color" tab stores a colour per key, but `dd_layer` has no field for it, so the
     firmware drops it.
   - Mode 5 lights every assigned key in one colour.
   - Modes 2 and 3 pass V and S to `hsv2rgb` in swapped order.
   - "Rainbow" calls `clear()` between frames, so it flickers.
   - Effects are hard-coded branches in `components/rgb_led/src/rgb_led.c`.
2. **The settings menu shows nothing about the device.** It only has LED modes, "DancingBerlin" and
   Exit (`components/menu/src/menu.c`).
3. **The main screen wastes space** (`components/oled/oled_tasks.c`, SSD1306 128x64 with a 16 px yellow
   band):
   - the yellow band shows the fixed `GATTS_TAG` ("Ahuyama") and the IP;
   - the layer name is drawn in `courB18`, using about a third of the screen;
   - the 16 key names are squeezed into the bottom 28 px.

**Owner choices (2026-10-18):**
- LED effects: all proposed ones (breathing, smooth rainbow wave, ripple, gradient, twinkle, layer colour)
  on top of off, solid, per-key and reactive.
- **Per-key colours per layer.**
- Screen: layer name and status icons in yellow, a 4x4 grid in blue.
- Device info: 3 pages (Bluetooth, WiFi, System).

## Part A: screen and Device info menu (firmware, R5.1)

### Main screen (`oled_tasks.c`)

One `draw_main_screen()` replaces the drawing in `ble_connected_oled()` and `update_oled()`.

```
+--------------------------------+
| Media         2/3  BT  WiFi    |  yellow, y 0-15: layer name (helvB10), index/count, icons
+--------------------------------+
|  Play | next  | mute  | layer  |  blue, y 16-63: 4 rows of 12 px; names (5x7) centred
|-------+-------+-------+--------|  in 32 px cells, separators at x 32/64/96 and y 27/39/51
| nWind | PWind |  F11  | PrtSC  |
...
```

- The Bluetooth icon is solid when connected and an outline when not. The WiFi icon is shown only when
  connected.
- NUM/CAPS lock show as small letters in the yellow band. They used to be drawn over the key area at y 24.
- The battery percentage stays behind `BATT_STAT` (off).
- When to redraw: a layer change (the existing `layer_recieve_q`), Bluetooth connect or disconnect, WiFi
  got or lost IP, and LED lock changes.
- `waiting_oled()`: the BLE name (`halBLEGetDeviceName()`) in yellow, then "Waiting for connection".
  `wifi_connected_oled()` only updates the WiFi state.

### Device info menu (`menu.c`)

- A new main-menu item, "Device info", opens an info screen with 3 pages:
  - **Bluetooth:** name, MAC, connected yes/no, config app connected yes/no.
  - **WiFi:** state, SSID, IP, RSSI.
  - **System:** firmware, free heap, uptime, number of layers, active layer.
- Turning a knob changes the page; a press or long press goes back. The page refreshes every second.
- "DancingBerlin" is replaced by "Restart".
- The data comes from one `device_status_get(device_status_t *)` in `main/config_api.c`.
  - `device.info` reuses it, so the screen and the app show the same values.
  - The menu component gets it through a registered callback (`menu_set_info_provider`), so
    `components/menu` does not depend on `main`.
  - This mirrors how `config_service` hands lines to `config_task`.

## Part B: lighting system v2 (firmware, R5.2)

### Renderer (`rgb_led.c`)

- `key_led_modes()` becomes a frame loop at about 50 fps:
  - each frame computes 16 colours from (effect, time, parameters);
  - adds the reactive overlay;
  - applies brightness;
  - then calls `set_pixel` and `refresh` once. There is **no `clear()` between frames**, which fixes
    the flicker.
- Fix the `hsv2rgb` argument order.
- The 2 notification LEDs show the layer colour, and are off when the effect is off.

### Effects

Ids 0–5 keep their old meaning, so stored settings need no migration.

| id | effect | id | effect |
|----|--------|----|--------|
| 0 | off | 6 | breathing (color1) |
| 1 | reactive: keys light on press and fade | 7 | ripple: a ring spreads from the pressed key |
| 2 | colour cycle (whole board) | 8 | gradient: color1 to color2, moving |
| 3 | rainbow wave, smooth | 9 | twinkle: random keys sparkle in color1/color2 |
| 4 | solid color1 | 10 | layer colour: the whole board in the active layer's colour |
| 5 | per-key colours of the active layer | | |

**Parameters:** `effect`, `brightness` 0–255, `speed` 0–255, `color1` and `color2` as `[r,g,b]`, and
`reactive`, a press-highlight overlay usable with any effect.
- Stored in the existing `led_mode` NVS namespace: the existing keys plus `c2r/c2g/c2b`, `bright` and
  `react`.
- `rgb_mode_t` grows accordingly. Old values map as follows: `V` becomes `brightness` when `bright` is
  missing, and `rgb` becomes color1.

### Per-layer key colours

- **Storage:** NVS namespace `led_keys`, blob `k_<layer uuid>`, 51 bytes: 16 key colours plus 1
  layer colour.
  - This is kept out of `dd_layer`, so there is no layer-format migration.
  - `layers.delete` removes the blob, and `layers.restore` clears the namespace.
- **Default when no blob exists:**
  - keys with a key code take color1, and empty keys stay off, which matches the old mode 5;
  - the layer colour comes from a fixed palette by layer index.
- The LED task caches the active layer's colours and reloads them on a layer change.
  `set_current_layout()` notifies it through a small queue.
- The ripple effect needs the position and time of each press. `rgb_key_led_press()` records them.

### Protocol additions (`docs/protocol.md`, `config_api.c`)

- `led.get` and `led.set` gain the new fields, plus `save` (default true).
  - `save:false` applies without writing flash, for live preview while dragging sliders.
  - The legacy `mode/H/S/V/rgb` fields are still accepted and returned, so the web page keeps working.
- `led.keys.get {uuid}` returns `{uuid, keys:[[r,g,b] x16], layer:[r,g,b]}`.
- `led.keys.set {uuid, keys?, layer?, save?}`.
- `led.effects` returns the effect list with names and the parameters each one uses, so the app does
  not hard-code it.
- The on-device LED menu lists the new effects.

## Part C: the Command Center app (R4.3)

### Tauri v2 in `web/src-tauri/` (Rust)

- **Transport module, from the spike:**
  - `ble_win.rs` on Windows: paired device by id, cached GATT discovery;
  - `serialport` on every OS, with DTR/RTS off, CP210x ports (VID 10C4, PID EA60) listed first.
- **Commands:** `list_devices`, `connect {kind,id}`, `disconnect`, `send {line}`, `save_file` and
  `open_file` (native dialogs through the `rfd` crate, for backup and restore).
- **Events:**
  - `cc-line`: protocol lines;
  - `cc-status`: connected or disconnected, with the reason;
  - `cc-log`: non-protocol USB lines, for the log console.
- **Auto-reconnect** every 2 s while the user wants a connection.
- **BLE on macOS and Linux is not in this phase.** USB works on all three OSes. BLE there needs
  btleplug or CoreBluetooth and testing on those machines (R4.4).

### Angular (reuse the existing pages)

- **`core/services/device/device.service.ts`:**
  - request/response matching by `id`, timeouts, and an event stream;
  - uses `TauriTransport` when `window.__TAURI__` exists, and otherwise an `HttpTransport` that maps
    methods onto the existing `/api` endpoints, so the on-device web page keeps working until R4.5.
- **`ApiService`** keeps its public methods and calls `DeviceService`; the shapes are unchanged
  (D-014). The keymap and macro pages are reused as they are.
- **New pages:**
  - **Connect:** device list, last device remembered, auto-connect.
  - **Dashboard:** `device.info`, and the active layer, live (`layer.changed`), clickable to switch.
  - **Lighting:**
    - an effect gallery with live preview (`save:false`);
    - brightness and speed sliders, two colour pickers, and a reactive toggle;
    - **a per-key painter on a 4x4 grid** for the selected layer (brush, fill, copy from another layer,
      layer colour);
    - a Save button.

    It replaces the key editor's non-working "Color" tab.
  - **WiFi:** `wifi.scan` list, connect, live status.
  - **Device:** rename, restart, backup and restore of every layer, macro, LED setting and key colour
    to a JSON file.
  - **Log:** the USB console.
- **`package.json`:** add `@tauri-apps/cli` and `@tauri-apps/api`, plus `tauri dev` and `tauri build`
  scripts. `frontendDist` is `../dist/esp-frontend` and `devUrl` is `ng serve`.

## Order of work (one commit or more per step; builds after each)

1. **A:** screen and Device info menu. Flash; the owner looks at the screen.
2. **B:** lighting engine, storage and protocol, plus `cc_cli` tests (effects preview, `led.keys`
   round trip). Flash; the owner checks each effect.
3. **C1:** Tauri shell, Rust transports, `DeviceService`, the rewired `ApiService`, and the Connect and
   Dashboard pages. `npm run tauri build` produces a Windows installer.
4. **C2:** the Lighting, WiFi, Device and Log pages.
5. **Docs:**
   - plan files in `docs/plans/`;
   - `CHANGELOG` CH-017 onwards;
   - `DECISIONS`: effect list, per-layer colours in their own namespace, OLED layout, BLE Windows-first;
   - `ROADMAP`: R4.3, and the new R5.1/R5.2;
   - `protocol.md`, `CLAUDE.md`.

## Verification

- **Firmware:**
  - ESP-IDF 4.4.7 build with no new warnings, then flash;
  - 45 s with no reboot;
  - `cc_cli.py --usb COM3 --ble selftest` with 0 failures, extended with LED preview/save and a
    `led.keys` round trip that restores the original;
  - heap still about 38 KB, and flat over repeated `led.keys.set`.
- **Web page on the device** (Playwright): layers and LED settings still work through HTTP.
- **App:** `npm run tauri build` succeeds. A `CC_SELFTEST`-style startup check in the app connects over
  BLE and USB and calls `device.info`, `layers.list` and `led.get`.
- **Owner:**
  - the new screen layout and the 3 info pages;
  - every effect on the hardware;
  - painting per-key colours in the app, which then show on the device;
  - switching layer from the keys updates the app live;
  - the keys still type.
