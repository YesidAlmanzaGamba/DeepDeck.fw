//! BLE link to the DeepDeck configuration service on macOS and Linux, through btleplug (R4.4).
//! Same API as ble_win.rs (main.rs picks one as `ble`).
//!
//! Not tested on hardware yet. Known limits:
//! - macOS: CoreBluetooth only lists devices that advertise. A DeepDeck connected to the Mac as a
//!   keyboard does not, so it may not be found; use the USB cable then.
//! - Linux (BlueZ): paired devices are listed without a scan.
//! - Writes go with response, in 180-byte chunks: within the usual MTU of macOS (185) and BlueZ
//!   (517). The DeepDeck ignores long (prepared) writes, so a chunk must stay under MTU-3.

use std::sync::{Arc, Mutex};
use std::time::Duration;

use btleplug::api::{Central, CentralEvent, Characteristic, Manager as _, Peripheral as _, ScanFilter, WriteType};
use btleplug::platform::{Adapter, Manager, Peripheral};
use futures::StreamExt;
use uuid::Uuid;

pub const SERVICE: Uuid = Uuid::from_u128(0xdd0e0001_7f4b_4c1a_9d3e_2a6b1c5e8f00);
pub const RX: Uuid = Uuid::from_u128(0xdd0e0002_7f4b_4c1a_9d3e_2a6b1c5e8f00);
pub const TX: Uuid = Uuid::from_u128(0xdd0e0003_7f4b_4c1a_9d3e_2a6b1c5e8f00);
const CHUNK: usize = 180;

fn e(err: impl std::fmt::Display) -> String {
    err.to_string()
}

#[derive(serde::Serialize, Clone)]
pub struct PairedDevice {
    pub name: String,
    pub id: String,
}

async fn adapter() -> Result<Adapter, String> {
    let manager = Manager::new().await.map_err(e)?;
    manager.adapters().await.map_err(e)?.into_iter().next().ok_or_else(|| "no Bluetooth adapter".to_string())
}

/// Known devices (BlueZ: paired ones), plus what a short scan finds
pub async fn paired() -> Result<Vec<PairedDevice>, String> {
    let adapter = adapter().await?;
    let _ = adapter.start_scan(ScanFilter::default()).await;
    tokio::time::sleep(Duration::from_secs(2)).await;
    let _ = adapter.stop_scan().await;
    let mut list = Vec::new();
    for p in adapter.peripherals().await.map_err(e)? {
        let name = p.properties().await.ok().flatten().and_then(|props| props.local_name);
        if let Some(name) = name {
            list.push(PairedDevice { name, id: p.id().to_string() });
        }
    }
    Ok(list)
}

pub struct BleConn {
    peripheral: Peripheral,
    rx: Characteristic,
    /// One line at a time: the chunks of two lines must not interleave
    send_lock: tauri::async_runtime::Mutex<()>,
    tasks: Vec<tauri::async_runtime::JoinHandle<()>>,
}

impl Drop for BleConn {
    fn drop(&mut self) {
        // stop listening; never disconnect: the OS uses the same link for the keyboard
        for task in &self.tasks {
            task.abort();
        }
    }
}

pub async fn connect(
    id: &str,
    on_bytes: impl Fn(Vec<u8>) + Send + Sync + 'static,
    on_lost: impl Fn() + Send + Sync + 'static,
) -> Result<(BleConn, u16, String), String> {
    let adapter = adapter().await?;
    let mut found = None;
    for attempt in 0..2 {
        if attempt == 1 {
            let _ = adapter.start_scan(ScanFilter::default()).await;
            tokio::time::sleep(Duration::from_secs(3)).await;
            let _ = adapter.stop_scan().await;
        }
        found = adapter.peripherals().await.map_err(e)?.into_iter().find(|p| p.id().to_string() == id);
        if found.is_some() {
            break;
        }
    }
    let peripheral = found.ok_or("the DeepDeck was not found: is it on and in range?")?;
    if !peripheral.is_connected().await.map_err(e)? {
        peripheral.connect().await.map_err(e)?;
    }
    peripheral.discover_services().await.map_err(e)?;
    let chars = peripheral.characteristics();
    let rx = chars.iter().find(|c| c.uuid == RX && c.service_uuid == SERVICE).cloned();
    let tx = chars.iter().find(|c| c.uuid == TX && c.service_uuid == SERVICE).cloned();
    let (Some(rx), Some(tx)) = (rx, tx) else {
        return Err("the DeepDeck has no configuration service: update its firmware, or pair it again".into());
    };
    peripheral.subscribe(&tx).await.map_err(e)?;
    let name = peripheral.properties().await.ok().flatten().and_then(|p| p.local_name).unwrap_or_default();

    let mut tasks = Vec::new();
    let mut notifications = peripheral.notifications().await.map_err(e)?;
    tasks.push(tauri::async_runtime::spawn(async move {
        while let Some(n) = notifications.next().await {
            if n.uuid == TX {
                on_bytes(n.value);
            }
        }
    }));
    let mut events = adapter.events().await.map_err(e)?;
    let my_id = peripheral.id();
    tasks.push(tauri::async_runtime::spawn(async move {
        while let Some(event) = events.next().await {
            if let CentralEvent::DeviceDisconnected(gone) = event {
                if gone == my_id {
                    on_lost();
                    break;
                }
            }
        }
    }));
    Ok((BleConn { peripheral, rx, send_lock: Default::default(), tasks }, 0, name))
}

impl BleConn {
    pub async fn send(&self, line: &str) -> Result<(), String> {
        let mut data = line.as_bytes().to_vec();
        data.push(b'\n');
        let _one_line_at_a_time = self.send_lock.lock().await;
        for part in data.chunks(CHUNK) {
            self.peripheral.write(&self.rx, part, WriteType::WithResponse).await.map_err(e)?;
        }
        Ok(())
    }
}

/// Joins notification chunks into lines
#[derive(Clone, Default)]
pub struct LineBuffer(Arc<Mutex<Vec<u8>>>);

impl LineBuffer {
    pub fn push(&self, bytes: &[u8]) -> Vec<String> {
        let mut buf = self.0.lock().unwrap();
        buf.extend_from_slice(bytes);
        let mut lines = Vec::new();
        while let Some(pos) = buf.iter().position(|&b| b == b'\n') {
            let line: Vec<u8> = buf.drain(..=pos).collect();
            lines.push(String::from_utf8_lossy(&line[..line.len() - 1]).into_owned());
        }
        lines
    }
}
