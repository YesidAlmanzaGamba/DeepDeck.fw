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
