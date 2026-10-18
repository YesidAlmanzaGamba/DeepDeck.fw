# Installing the DeepDeck Command Center

The Command Center is the desktop app that configures your DeepDeck: keys, knobs, lighting, WiFi,
firmware updates. It runs on **Windows, macOS and Linux**, and connects to the DeepDeck over
**Bluetooth** or the **USB cable**.

| | Windows 10/11 | macOS 12 or later | Linux (Ubuntu 22.04+, Debian 12+, Fedora…) |
|---|---|---|---|
| Installer | `DeepDeck Command Center_<version>_x64-setup.exe` | `DeepDeck Command Center_<version>_aarch64.dmg` (Apple silicon) | `.deb` or `.AppImage` |
| USB cable | yes | yes | yes |
| Bluetooth | yes (tested) | yes (not tested yet) | yes (not tested yet) |
| Layers by application, start with the computer | yes | not yet | not yet |

## 1. Download

- **Releases** (recommended): <https://github.com/YesidAlmanzaGamba/DeepDeck.fw/releases>, then the
  file for your system under *Assets*.
- **Latest build** of any branch: the repository's *Actions* tab, the newest green run of
  **build**, then *Artifacts* at the bottom: `command-center-Windows`, `command-center-macOS`,
  `command-center-Linux` (zip files with the installer inside). You must be signed in to GitHub.

The installer already contains the newest DeepDeck firmware, so you can update the DeepDeck from
the app without downloading anything else.

## 2. Install

### Windows

1. Run `DeepDeck Command Center_<version>_x64-setup.exe`.
2. Windows SmartScreen may say *"Windows protected your PC"*, because the installer is not signed
   yet. Click **More info**, then **Run anyway**.
3. The installer adds the app to the Start menu. It needs Microsoft Edge WebView2, which Windows
   10 and 11 already have; if not, the installer downloads it.

### macOS

1. Open the `.dmg` and drag **DeepDeck Command Center** to **Applications**.
2. The app is not notarized yet, so the first time macOS says it *"cannot be opened"*:
   open **System Settings → Privacy & Security**, scroll down and click **Open Anyway**. (Or in a
   terminal: `xattr -dr com.apple.quarantine "/Applications/DeepDeck Command Center.app"`.)
3. When the app first uses Bluetooth, macOS asks for permission: click **Allow**.
4. The USB cable uses a CP210x serial chip. macOS 11 and later include its driver; if no port
   appears, install the *CP210x VCP driver* from Silicon Labs.

### Linux

- **Debian / Ubuntu**: `sudo apt install ./deepdeck-command-center_<version>_amd64.deb`
- **Any distribution**: `chmod +x DeepDeck*.AppImage` and run it. (Older systems may need
  `libfuse2`: `sudo apt install libfuse2`.)
- **USB cable**: your user must be allowed to open serial ports, once:
  `sudo usermod -aG dialout $USER` (Fedora/Arch: `uucp`), then log out and in again.
- **Bluetooth** uses BlueZ, which desktop distributions already run.

## 3. First connection

1. **Bluetooth**: pair the DeepDeck in your computer's Bluetooth settings like a keyboard (it
   advertises as *Ahuyama* or the name you gave it). Then open the app: the **Connect** page lists
   it; click **Connect**.
   - If it is paired but does not connect: remove it from the Bluetooth settings and pair it again.
     This is needed once after updating from a firmware older than 0.6.0.
   - On macOS, a DeepDeck already connected to the Mac as a keyboard may not appear in the list;
     use the USB cable in that case.
2. **USB cable**: plug the DeepDeck in. The Connect page shows a port marked **DeepDeck**
   (COM3, `/dev/ttyUSB0` or `/dev/cu.usbserial-…`); click **Connect**.

The app remembers the device and reconnects by itself next time. The status in the top right
corner shows the link; click it to go back to Connect.

## 4. Keep the DeepDeck up to date

**Update** page → **Ready to install** lists the firmware that came with the app and every firmware
installed before from this computer; click **Install**. Over the USB cable it takes under 3
minutes, over Bluetooth about 11. Keep the DeepDeck connected until it restarts. If a new
firmware does not start, the DeepDeck goes back to the previous one by itself. Your layers,
macros and settings are kept.

To install another file, use **Choose a .bin file** (e.g. `DeepDeck-<version>.bin` from a release).

### A DeepDeck with an old firmware (before 0.7.0)

Firmware updates from the app need firmware 0.7.0 or later on the DeepDeck. An older DeepDeck must
be flashed once over the USB cable with esptool:

1. Install Python 3, then `pip install esptool`.
2. Download `DeepDeck-firmware-<version>.zip` from the release and unzip it.
3. In that folder (close the Command Center first, it uses the port):
   `esptool.py --chip esp32 -p <port> -b 460800 --before default_reset --after hard_reset write_flash @flash_args`
   where `<port>` is `COM3`, `/dev/ttyUSB0` or `/dev/cu.usbserial-…`.

This writes the bootloader, the partition table and the firmware, but not the settings area:
layers and macros stay.

## 5. Running in the background

Closing the window keeps the app in the tray (Windows: next to the clock; macOS: menu bar), so
layers by application keep working. Quit from the tray icon's menu. **Device** page → *This
computer*: start with Windows, closing to the tray, the notification when the DeepDeck
disconnects.

## 6. Uninstall

- Windows: *Settings → Apps → DeepDeck Command Center → Uninstall*.
- macOS: drag the app from Applications to the Bin.
- Linux: `sudo apt remove deepdeck-command-center`, or delete the AppImage.

The app keeps its settings (rules, firmware history) in your user folder:
`%APPDATA%\co.deepdeck.commandcenter` (Windows), `~/Library/Application Support/co.deepdeck.commandcenter`
(macOS), `~/.config/co.deepdeck.commandcenter` and `~/.local/share/co.deepdeck.commandcenter` (Linux).

## Building it yourself

See [the README](../README.md#building): firmware with ESP-IDF 4.4.7, the app with Node 22, Rust and
`npx @tauri-apps/cli@2.12.3 build`.
