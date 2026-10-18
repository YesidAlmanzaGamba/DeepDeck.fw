# Command Center spike (R4.0): results

- **Status:** done, the gate passed (go)
- **Roadmap item:** R4.0
- **Branch:** `spike/command-center`. The code is throwaway; what carries over is listed below.
- **Parent plan:** [2026-10-17-command-center](2026-10-17-command-center.md)

## Question

Can a desktop app reach a configuration channel on the DeepDeck:
- over **BLE**, while the OS is connected to it as a keyboard, and
- over the **USB cable**, without resetting the board?

## What was built

- **Firmware:**
  - `components/ble/config_service.c`: GATT service `dd0e0001-7f4b-4c1a-9d3e-2a6b1c5e8f00`. It has an RX
    characteristic (write and write-without-response) and a TX characteristic (notify); both require
    encryption. Lines are joined until `\n`.
  - `main/serial_transport.c`: UART0 driver, with the console routed through it and `@DD ` line prefix.
  - `main/spike_echo.c`: answers every line with an echo, from its own task.
- **Host:**
  - `tools/spike/cc_spike.py`: Python and bleak.
  - `tools/spike/cc-tauri/`: Tauri 2 app, with WinRT for BLE and `serialport` for USB. It has a
    `CC_SPIKE_SELFTEST=1` mode.

## Results (Windows 11, DeepDeck paired as "Ahuyama")

| Link | Short line (24 B) | Long line (1,549 B, non-ASCII) | Notes |
|------|------------------|---------------------------------|-------|
| BLE from Tauri | 31 ms, identical | 370 ms, identical | MTU 517, no scan, keyboard stays connected |
| USB from Tauri | 116 ms, identical | 297 ms, identical | DTR/RTS off, **no reset** on open |
| BLE from Python (bleak) | 39 ms | 162 ms (644 B), 1,557 B identical | same firmware |
| USB from Python (pyserial) | 116 ms | 220 ms (644 B) | `board rebooted on open: False` |

## Findings that change the plan

1. **btleplug cannot be used on Windows** (D-015). Its WinRT backend only knows devices seen
   advertising, `add_peripheral` is "NotSupported", and it discovers services *uncached*. Windows
   refuses that on a connected HID device (`E_UNEXPECTED`). The app uses its own small WinRT module
   (`ble_win.rs`), which:
   - finds paired devices with `GetDeviceSelectorFromPairingState(true)`;
   - opens one with `FromIdAsync`;
   - uses **cached** service discovery.

   On Linux, BlueZ lists paired devices, so btleplug should work. On macOS, `add_peripheral` is also
   unsupported; it will need either "advertise while connected" (already in the firmware behind a
   `#define`) or a small CoreBluetooth `retrieveConnectedPeripherals` path. Neither Linux nor macOS has
   been tested yet.
2. **Windows caches the GATT database of bonded devices.** Hosts paired with older firmware do not see
   the new service. A Service Changed indication sent after encryption was not enough. **Re-pairing once
   fixes it.** The release notes must say so. Windows hides the HID service (`0x1812`) from apps, as expected.
3. **Two firmware bugs found, fixes carried into Phase 2:**
   - Bluedroid builds one attribute table at a time. Registering the config service in parallel with
     HID asserted in `future_ready` and froze the device. It now registers only after the HID service
     has started.
   - `sys_evt` (2 KB stack) overflowed. The WiFi code starts the web server from the event handler,
     and logging through the UART driver costs a little more stack. Raised to 4 KB in `sdkconfig.defaults`.
4. `CONFIG_ESP_SYSTEM_PANIC_GDBSTUB=y` makes the device **freeze** after any crash, waiting for a
   debugger, instead of rebooting. Proposal for Phase 1: print the crash and reboot.
5. Free heap with BLE, WiFi, the web server and the spike running: about 43–58 KB.

## Carried into Phase 1 and 2

- `config_service.c` (minus the spike `#define`), `serial_transport.c`, the registration order, the MTU,
  and the `sys_evt` stack.
- The single-queue, single-task request handling of `spike_echo.c`, which becomes `config_task`.
- `ble_win.rs`, and the serial open sequence (DTR/RTS off) from the Tauri spike.

## Still to check on hardware (owner)

- The keyboard keeps typing normally while the app is connected.
