//! Desktop integration (R6.4): tray icon with the link and layer, closing to the tray, start with
//! Windows (hidden), and a notification when the link to the DeepDeck is lost.

use std::sync::Mutex;

use serde::{Deserialize, Serialize};
use tauri::menu::{Menu, MenuItem, PredefinedMenuItem};
use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
use tauri::{AppHandle, Listener, Manager};
use tauri_plugin_notification::NotificationExt;

/// Command-line flag used by the autostart entry: start in the tray
pub const HIDDEN_FLAG: &str = "--hidden";
const TRAY_ID: &str = "main";

#[derive(Serialize, Deserialize, Clone)]
pub struct Settings {
    /// Closing the window keeps the app running in the tray
    pub close_to_tray: bool,
    /// Notify when the link to the DeepDeck is lost
    pub notify_link_lost: bool,
}

impl Desktop {
    pub fn new(settings: Settings) -> Self {
        Desktop { settings: Mutex::new(settings), ..Default::default() }
    }
}

impl Default for Settings {
    fn default() -> Self {
        Settings { close_to_tray: true, notify_link_lost: true }
    }
}

#[derive(Default)]
pub struct Desktop {
    pub settings: Mutex<Settings>,
    link: Mutex<String>,  // "Bluetooth" / "USB" / "" when not connected
    layer: Mutex<String>, // last known active layer
}

#[derive(Serialize, Deserialize)]
pub struct DesktopState {
    #[serde(flatten)]
    pub settings: Settings,
    pub autostart: bool,
}

fn settings_path(app: &AppHandle) -> Option<std::path::PathBuf> {
    app.path().app_config_dir().ok().map(|dir| dir.join("desktop.json"))
}

pub fn load_settings(app: &AppHandle) -> Settings {
    settings_path(app)
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|text| serde_json::from_str(&text).ok())
        .unwrap_or_default()
}

pub fn save_settings(app: &AppHandle, settings: &Settings) -> Result<(), String> {
    let path = settings_path(app).ok_or("no config folder")?;
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|err| err.to_string())?;
    }
    std::fs::write(path, serde_json::to_string_pretty(settings).map_err(|err| err.to_string())?).map_err(|err| err.to_string())
}

pub fn show_window(app: &AppHandle) {
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.unminimize();
        let _ = window.show();
        let _ = window.set_focus();
    }
}

fn refresh_tooltip(app: &AppHandle) {
    let desktop = app.state::<Desktop>();
    let link = desktop.link.lock().unwrap().clone();
    let layer = desktop.layer.lock().unwrap().clone();
    let text = if link.is_empty() {
        "DeepDeck: not connected".to_string()
    } else if layer.is_empty() {
        format!("DeepDeck: connected ({link})")
    } else {
        format!("DeepDeck: {layer} ({link})")
    };
    if let Some(tray) = app.tray_by_id(TRAY_ID) {
        let _ = tray.set_tooltip(Some(text));
    }
}

pub fn setup(app: &AppHandle) -> tauri::Result<()> {
    let open = MenuItem::with_id(app, "open", "Open DeepDeck Command Center", true, None::<&str>)?;
    let quit = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
    let menu = Menu::with_items(app, &[&open, &PredefinedMenuItem::separator(app)?, &quit])?;
    let mut tray = TrayIconBuilder::with_id(TRAY_ID)
        .tooltip("DeepDeck: not connected")
        .menu(&menu)
        .show_menu_on_left_click(false)
        .on_menu_event(|app, event| match event.id.as_ref() {
            "open" => show_window(app),
            "quit" => app.exit(0),
            _ => {}
        })
        .on_tray_icon_event(|tray, event| {
            if let TrayIconEvent::Click { button: MouseButton::Left, button_state: MouseButtonState::Up, .. } = event {
                show_window(tray.app_handle());
            }
        });
    if let Some(icon) = app.default_window_icon() {
        tray = tray.icon(icon.clone());
    }
    tray.build(app)?;

    // link status -> tooltip, and a notification when an established link is lost
    let handle = app.clone();
    app.listen("cc-status", move |event| {
        let Ok(status) = serde_json::from_str::<serde_json::Value>(event.payload()) else { return };
        let desktop = handle.state::<Desktop>();
        let was = desktop.link.lock().unwrap().clone();
        let connected = status["connected"].as_bool().unwrap_or(false);
        let link = match (connected, status["kind"].as_str()) {
            (true, Some("ble")) => "Bluetooth",
            (true, Some("usb")) => "USB",
            (true, _) => "connected",
            _ => "",
        };
        *desktop.link.lock().unwrap() = link.to_string();
        if !connected {
            desktop.layer.lock().unwrap().clear();
            let reason = status["reason"].as_str().unwrap_or("");
            let notify = desktop.settings.lock().unwrap().notify_link_lost;
            // "disconnected" is the user's own Disconnect: no notice for that
            if !was.is_empty() && notify && reason != "disconnected" {
                let _ = handle
                    .notification()
                    .builder()
                    .title("DeepDeck disconnected")
                    .body(format!("{reason}. The app reconnects by itself when the DeepDeck is back."))
                    .show();
            }
        }
        refresh_tooltip(&handle);
    });

    // active layer -> tooltip (layer.changed events, and device.info answers)
    let handle = app.clone();
    app.listen("cc-line", move |event| {
        let Ok(payload) = serde_json::from_str::<serde_json::Value>(event.payload()) else { return };
        let Ok(line) = serde_json::from_str::<serde_json::Value>(payload["line"].as_str().unwrap_or("")) else { return };
        let name = if line["event"] == "layer.changed" {
            line["data"]["name"].as_str()
        } else {
            line["result"]["layer"]["name"].as_str()
        };
        if let Some(name) = name {
            *handle.state::<Desktop>().layer.lock().unwrap() = name.to_string();
            refresh_tooltip(&handle);
        }
    });
    Ok(())
}

// ------------------------------------------------------------------------------------------------
// Start with Windows: HKCU\Software\Microsoft\Windows\CurrentVersion\Run

#[cfg(windows)]
mod autostart {
    use windows::core::{HSTRING, PCWSTR};
    use windows::Win32::Foundation::ERROR_SUCCESS;
    use windows::Win32::System::Registry::{
        RegCloseKey, RegDeleteValueW, RegOpenKeyExW, RegQueryValueExW, RegSetValueExW, HKEY, HKEY_CURRENT_USER, KEY_READ, KEY_SET_VALUE,
        REG_SZ,
    };

    const RUN_KEY: &str = r"Software\Microsoft\Windows\CurrentVersion\Run";
    const VALUE: &str = "DeepDeck Command Center";

    fn open(access: windows::Win32::System::Registry::REG_SAM_FLAGS) -> Option<HKEY> {
        let mut key = HKEY::default();
        let res = unsafe { RegOpenKeyExW(HKEY_CURRENT_USER, &HSTRING::from(RUN_KEY), None, access, &mut key) };
        (res == ERROR_SUCCESS).then_some(key)
    }

    pub fn enabled() -> bool {
        let Some(key) = open(KEY_READ) else { return false };
        let res = unsafe { RegQueryValueExW(key, &HSTRING::from(VALUE), None, None, None, None) };
        unsafe { let _ = RegCloseKey(key); }
        res == ERROR_SUCCESS
    }

    pub fn set(on: bool) -> Result<(), String> {
        let key = open(KEY_SET_VALUE).ok_or("cannot open the Run key")?;
        let res = if on {
            let exe = std::env::current_exe().map_err(|err| err.to_string())?;
            let command = format!("\"{}\" {}", exe.display(), super::HIDDEN_FLAG);
            let wide: Vec<u16> = command.encode_utf16().chain(std::iter::once(0)).collect();
            let bytes = unsafe { std::slice::from_raw_parts(wide.as_ptr() as *const u8, wide.len() * 2) };
            unsafe { RegSetValueExW(key, &HSTRING::from(VALUE), None, REG_SZ, Some(bytes)) }
        } else {
            let res = unsafe { RegDeleteValueW(key, PCWSTR(HSTRING::from(VALUE).as_ptr())) };
            if res.0 == 2 { ERROR_SUCCESS } else { res } // ERROR_FILE_NOT_FOUND: already off
        };
        unsafe { let _ = RegCloseKey(key); }
        if res == ERROR_SUCCESS { Ok(()) } else { Err(format!("registry: {res:?}")) }
    }
}

#[cfg(not(windows))]
mod autostart {
    pub fn enabled() -> bool {
        false
    }
    pub fn set(_on: bool) -> Result<(), String> {
        Err("start at login is only supported on Windows for now".into())
    }
}

pub fn state(app: &AppHandle) -> DesktopState {
    DesktopState { settings: app.state::<Desktop>().settings.lock().unwrap().clone(), autostart: autostart::enabled() }
}

pub fn set_state(app: &AppHandle, value: DesktopState) -> Result<(), String> {
    if value.autostart != autostart::enabled() {
        autostart::set(value.autostart)?;
    }
    save_settings(app, &value.settings)?;
    *app.state::<Desktop>().settings.lock().unwrap() = value.settings;
    Ok(())
}
