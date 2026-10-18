//! DeepDeck Command Center: configures the DeepDeck over BLE or the USB cable.
//! The Angular app (web/) is the UI; this side owns the link (link.rs) and native dialogs.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

#[cfg(windows)]
mod ble_win;
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

fn main() {
    tauri::Builder::default()
        .setup(|app| {
            let links: SharedLinks = Arc::new(Links::default());
            links.start_reconnect_loop(app.handle().clone());
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
            open_firmware_file
        ])
        .run(tauri::generate_context!())
        .expect("error while running the DeepDeck Command Center");
}
