//! DeepDeck Command Center: configures the DeepDeck over BLE or the USB cable.
//! The Angular app (web/) is the UI; this side owns the link (link.rs) and native dialogs.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

// BLE: WinRT on Windows (D-015), btleplug on macOS and Linux (R4.4); the same API in both
#[cfg(windows)]
#[path = "ble_win.rs"]
mod ble;
#[cfg(not(windows))]
#[path = "ble_btleplug.rs"]
mod ble;
mod app_commands;
mod app_layers;
mod desktop;
mod firmware;
mod link;
mod selftest;

use std::sync::Arc;

use link::{DeviceEntry, Links, Status, Target};
use tauri::{AppHandle, Manager, State};

pub type SharedLinks = Arc<Links>;

#[tauri::command]
async fn list_devices() -> Vec<DeviceEntry> {
    link::list_devices().await
}

#[tauri::command]
async fn connect(app: AppHandle, links: State<'_, SharedLinks>, target: Target) -> Result<Status, String> {
    links.inner().clone().connect(&app, target).await
}

#[tauri::command]
fn disconnect(app: AppHandle, links: State<'_, SharedLinks>) {
    links.disconnect(&app);
}

#[tauri::command]
fn link_status(links: State<'_, SharedLinks>) -> Status {
    links.status()
}

#[tauri::command]
async fn send(app: AppHandle, links: State<'_, SharedLinks>, line: String) -> Result<(), String> {
    links.inner().clone().send(&app, line).await
}

/// Save text through a native "Save as" dialog. Returns the path, or None if cancelled.
#[tauri::command]
async fn save_text_file(default_name: String, contents: String) -> Result<Option<String>, String> {
    // DD_CC_SAVE_FILE: tests write here instead of showing the dialog
    if let Ok(path) = std::env::var("DD_CC_SAVE_FILE") {
        return std::fs::write(&path, contents).map(|_| Some(path)).map_err(|err| err.to_string());
    }
    tauri::async_runtime::spawn_blocking(move || {
        match rfd::FileDialog::new().set_file_name(&default_name).add_filter("DeepDeck backup", &["json"]).save_file() {
            Some(path) => std::fs::write(&path, contents)
                .map(|_| Some(path.display().to_string()))
                .map_err(|err| err.to_string()),
            None => Ok(None),
        }
    })
    .await
    .map_err(|err| err.to_string())?
}

/// Pick and read a text file through a native "Open" dialog. Returns None if cancelled.
#[tauri::command]
async fn open_text_file() -> Result<Option<String>, String> {
    // DD_CC_OPEN_FILE: tests read this instead of showing the dialog
    if let Ok(path) = std::env::var("DD_CC_OPEN_FILE") {
        return std::fs::read_to_string(path).map(Some).map_err(|err| err.to_string());
    }
    tauri::async_runtime::spawn_blocking(move || {
        match rfd::FileDialog::new().add_filter("DeepDeck backup", &["json"]).pick_file() {
            Some(path) => std::fs::read_to_string(path).map(Some).map_err(|err| err.to_string()),
            None => Ok(None),
        }
    })
    .await
    .map_err(|err| err.to_string())?
}

/// Pick a firmware .bin and check it (DeepDeck image, version). Returns None if cancelled.
#[tauri::command]
async fn open_firmware_file() -> Result<Option<firmware::FirmwareFile>, String> {
    // DD_CC_FIRMWARE_FILE: tests pick this file instead of showing the dialog
    if let Ok(path) = std::env::var("DD_CC_FIRMWARE_FILE") {
        return firmware::read(std::path::Path::new(&path)).map(Some);
    }
    tauri::async_runtime::spawn_blocking(move || {
        match rfd::FileDialog::new().add_filter("DeepDeck firmware", &["bin"]).pick_file() {
            Some(path) => firmware::read(&path).map(Some),
            None => Ok(None),
        }
    })
    .await
    .map_err(|err| err.to_string())?
}

#[tauri::command]
fn app_layers_get(rules: State<'_, app_layers::SharedAppLayers>) -> app_layers::State {
    rules.get()
}

#[tauri::command]
fn app_layers_set(app: AppHandle, rules: State<'_, app_layers::SharedAppLayers>, value: app_layers::Rules) -> Result<(), String> {
    rules.set(&app, value)
}

#[tauri::command]
fn desktop_get(app: AppHandle) -> desktop::DesktopState {
    desktop::state(&app)
}

#[tauri::command]
fn desktop_set(app: AppHandle, value: desktop::DesktopState) -> Result<(), String> {
    desktop::set_state(&app, value)
}

/// The firmware images the Update page can install without browsing (bundled + history)
#[tauri::command]
fn firmware_list(app: AppHandle) -> Vec<firmware::FirmwareEntry> {
    firmware::list(&app)
}

/// Read one image of the list for installing (checked like a picked file)
#[tauri::command]
async fn firmware_load(path: String) -> Result<firmware::FirmwareFile, String> {
    tauri::async_runtime::spawn_blocking(move || firmware::read(std::path::Path::new(&path)))
        .await
        .map_err(|err| err.to_string())?
}

#[tauri::command]
fn firmware_remember(app: AppHandle, path: String) -> Result<(), String> {
    firmware::remember(&app, &path)
}

#[tauri::command]
fn firmware_forget(app: AppHandle, path: String) -> Result<(), String> {
    firmware::forget(&app, &path)
}

#[tauri::command]
fn app_commands_get(cmds: State<'_, app_commands::SharedAppCommands>) -> app_commands::State {
    cmds.get()
}

#[tauri::command]
fn app_commands_set(app: AppHandle, cmds: State<'_, app_commands::SharedAppCommands>, value: Vec<app_commands::AppCommand>) -> Result<(), String> {
    cmds.set(&app, value)
}

/// Test a command from the window, as if its key was pressed
#[tauri::command]
fn app_commands_run(app: AppHandle, cmds: State<'_, app_commands::SharedAppCommands>, id: u8) {
    cmds.inner().clone().run(&app, id);
}

fn main() {
    tauri::Builder::default()
        // a second start (Start menu, autostart) shows the running app instead
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| desktop::show_window(app)))
        .plugin(tauri_plugin_notification::init())
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                let keep = window.app_handle().state::<desktop::Desktop>().settings.lock().unwrap().close_to_tray;
                if keep {
                    api.prevent_close(); // stays in the tray (per-application layers keep working)
                    let _ = window.hide();
                }
            }
        })
        .setup(|app| {
            app.manage(desktop::Desktop::new(desktop::load_settings(app.handle())));
            desktop::setup(app.handle())?;
            // the window starts hidden (tauri.conf); show it unless started by the autostart entry
            if !std::env::args().any(|a| a == desktop::HIDDEN_FLAG) {
                desktop::show_window(app.handle());
            }
            let links: SharedLinks = Arc::new(Links::default());
            links.start_reconnect_loop(app.handle().clone());
            let app_rules = app_layers::AppLayers::load(app.handle());
            app_rules.start(app.handle().clone(), links.clone());
            app.manage(app_rules);
            let commands = app_commands::AppCommands::load(app.handle());
            commands.start(app.handle());
            app.manage(commands);
            app.manage(links);
            if std::env::var("DD_CC_SELFTEST").is_ok() {
                selftest::run(app.handle().clone());
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            list_devices,
            connect,
            disconnect,
            link_status,
            send,
            save_text_file,
            open_text_file,
            open_firmware_file,
            app_layers_get,
            app_layers_set,
            desktop_get,
            desktop_set,
            firmware_list,
            firmware_load,
            firmware_remember,
            firmware_forget,
            app_commands_get,
            app_commands_set,
            app_commands_run
        ])
        .run(tauri::generate_context!())
        .expect("error while running the DeepDeck Command Center");
}
