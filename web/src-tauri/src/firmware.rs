//! Firmware files for the update page (fw.* in docs/protocol.md): pick a .bin, check that it is a
//! DeepDeck image and read its version, before anything is sent to the device.

use base64::Engine;
use sha2::{Digest, Sha256};

#[derive(serde::Serialize)]
pub struct FirmwareFile {
    pub path: String,
    pub size: usize,
    /// SHA-256 of the whole file, hex; the device checks it at fw.end
    pub sha256: String,
    /// From the image's app description (esp_app_desc_t)
    pub project: String,
    pub version: String,
    pub built: String,
    /// The file, base64: the page sends it in fw.chunk pieces
    pub data: String,
}

// ESP32 app image: 24-byte image header + 8-byte segment header, then esp_app_desc_t
const DESC_AT: usize = 24 + 8;
const DESC_MAGIC: u32 = 0xABCD_5432;

fn c_string(bytes: &[u8]) -> String {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    String::from_utf8_lossy(&bytes[..end]).to_string()
}

pub fn read(path: &std::path::Path) -> Result<FirmwareFile, String> {
    let image = std::fs::read(path).map_err(|err| err.to_string())?;
    if image.len() < DESC_AT + 256 || image[0] != 0xE9 {
        return Err("this is not an ESP32 firmware image (.bin)".into());
    }
    let desc = &image[DESC_AT..DESC_AT + 256];
    if u32::from_le_bytes([desc[0], desc[1], desc[2], desc[3]]) != DESC_MAGIC {
        return Err("this image has no app description: not a DeepDeck firmware".into());
    }
    // esp_app_desc_t: magic, secure_version, reserv1[2], version[32], project_name[32], time[16], date[16]
    let version = c_string(&desc[16..48]);
    let project = c_string(&desc[48..80]);
    let built = format!("{} {}", c_string(&desc[96..112]), c_string(&desc[80..96]));
    if project != "DeepDeck" {
        return Err(format!("this firmware is for \"{project}\", not the DeepDeck"));
    }
    Ok(FirmwareFile {
        path: path.display().to_string(),
        size: image.len(),
        sha256: Sha256::digest(&image).iter().map(|b| format!("{b:02x}")).collect(),
        project,
        version,
        built,
        data: base64::engine::general_purpose::STANDARD.encode(&image),
    })
}

// ------------------------------------------------------------------------------------------------
// Firmware library: the image bundled with the app, and the ones installed before (history)

use tauri::Manager;

const HISTORY_MAX: usize = 10;

#[derive(serde::Serialize)]
pub struct FirmwareEntry {
    pub path: String,
    pub file_name: String,
    pub version: String,
    pub built: String,
    pub size: usize,
    pub sha256: String,
    /// "bundled" (comes with this app) or "history" (installed from this computer before)
    pub source: String,
    /// When it was added to the history (seconds since 1970), for display
    pub added: Option<u64>,
}

fn history_dir(app: &tauri::AppHandle) -> Option<std::path::PathBuf> {
    app.path().app_data_dir().ok().map(|d| d.join("firmware"))
}

fn bundled_dir(app: &tauri::AppHandle) -> Option<std::path::PathBuf> {
    app.path().resource_dir().ok().map(|d| d.join("firmware"))
}

fn entries_in(dir: &std::path::Path, source: &str) -> Vec<FirmwareEntry> {
    let Ok(files) = std::fs::read_dir(dir) else { return Vec::new() };
    let mut out = Vec::new();
    for f in files.flatten() {
        let path = f.path();
        if path.extension().and_then(|e| e.to_str()) != Some("bin") {
            continue;
        }
        if let Ok(file) = read(&path) {
            let added = f.metadata().ok().and_then(|m| m.modified().ok()).and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map(|d| d.as_secs());
            out.push(FirmwareEntry {
                file_name: path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default(),
                path: file.path,
                version: file.version,
                built: file.built,
                size: file.size,
                sha256: file.sha256,
                source: source.to_string(),
                added,
            });
        }
    }
    out
}

/// Bundled image first, then the history, newest first; the same image (SHA-256) only once
pub fn list(app: &tauri::AppHandle) -> Vec<FirmwareEntry> {
    let mut all = Vec::new();
    if let Some(dir) = bundled_dir(app) {
        all.extend(entries_in(&dir, "bundled"));
    }
    if let Some(dir) = history_dir(app) {
        let mut history = entries_in(&dir, "history");
        history.sort_by(|a, b| b.added.cmp(&a.added));
        all.extend(history);
    }
    let mut seen = std::collections::HashSet::new();
    all.retain(|e| seen.insert(e.sha256.clone()));
    all
}

/// Keep a copy of an installed image in the history (the oldest beyond 10 are removed)
pub fn remember(app: &tauri::AppHandle, path: &str) -> Result<(), String> {
    let file = read(std::path::Path::new(path))?;
    let dir = history_dir(app).ok_or("no data folder")?;
    std::fs::create_dir_all(&dir).map_err(|err| err.to_string())?;
    let target = dir.join(format!("DeepDeck-{}-{}.bin", file.version, &file.sha256[..8]));
    if !target.exists() {
        std::fs::copy(path, &target).map_err(|err| err.to_string())?;
    } else {
        // installed again: move it to the top of the history
        let data = std::fs::read(&target).map_err(|err| err.to_string())?;
        std::fs::write(&target, data).map_err(|err| err.to_string())?;
    }
    let mut history = entries_in(&dir, "history");
    history.sort_by(|a, b| b.added.cmp(&a.added));
    for old in history.iter().skip(HISTORY_MAX) {
        let _ = std::fs::remove_file(&old.path);
    }
    Ok(())
}

/// Remove an image from the history (never the bundled one)
pub fn forget(app: &tauri::AppHandle, path: &str) -> Result<(), String> {
    let dir = history_dir(app).ok_or("no data folder")?;
    let p = std::path::Path::new(path);
    if p.parent() != Some(dir.as_path()) {
        return Err("only images in the history can be removed".into());
    }
    std::fs::remove_file(p).map_err(|err| err.to_string())
}
