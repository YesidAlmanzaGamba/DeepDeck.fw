//! The one link to a DeepDeck, over BLE (Windows) or the USB cable, and its events for the page:
//!   "cc-line"   a protocol line from the device (JSON, see docs/protocol.md)
//!   "cc-status" {connected, kind, id, name, reason}
//!   "cc-log"    a log line printed by the firmware on the USB cable
//! The user picks a device once; when the link drops it is re-opened every 2 s until it is back.

use std::io::{BufRead, BufReader, Write};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter};

const USB_PREFIX: &str = "@DD ";
const CP210X_VID: u16 = 0x10C4;
const CP210X_PID: u16 = 0xEA60;
const RECONNECT_EVERY: Duration = Duration::from_secs(2);

#[derive(Serialize, Deserialize, Clone, Copy, PartialEq, Eq, Debug)]
#[serde(rename_all = "lowercase")]
pub enum Kind {
    Ble,
    Usb,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct Target {
    pub kind: Kind,
    pub id: String,
    pub name: String,
}

#[derive(Serialize, Clone)]
pub struct DeviceEntry {
    pub kind: Kind,
    pub id: String,
    pub name: String,
    pub detail: String,
    /// looks like a DeepDeck (CP210x USB bridge, or a paired BLE device called Ahuyama/DeepDeck)
    pub likely: bool,
}

#[derive(Serialize, Clone, Default)]
pub struct Status {
    pub connected: bool,
    pub kind: Option<Kind>,
    pub id: Option<String>,
    pub name: Option<String>,
    pub reason: Option<String>,
}

#[derive(Serialize, Clone)]
struct Line {
    via: Kind,
    line: String,
}

enum Handle {
    #[cfg(windows)]
    Ble(Arc<crate::ble_win::BleConn>),
    Usb(UsbHandle),
}

type SharedPort = Arc<Mutex<Box<dyn serialport::SerialPort>>>;

/// The reader thread keeps its own clone of the port: it must stop when the link is dropped,
/// otherwise the port stays open and cannot be opened again
struct UsbHandle {
    port: SharedPort,
    alive: Arc<AtomicBool>,
}

impl Drop for UsbHandle {
    fn drop(&mut self) {
        self.alive.store(false, Ordering::SeqCst);
    }
}

struct Active {
    target: Target,
    handle: Handle,
    generation: u64,
}

#[derive(Default)]
pub struct Links {
    active: Mutex<Option<Active>>,
    wanted: Mutex<Option<Target>>,
    generation: AtomicU64,
}

fn emit_status(app: &AppHandle, status: Status) {
    let _ = app.emit("cc-status", status);
}

impl Links {
    pub fn status(&self) -> Status {
        match self.active.lock().unwrap().as_ref() {
            Some(a) => Status {
                connected: true,
                kind: Some(a.target.kind),
                id: Some(a.target.id.clone()),
                name: Some(a.target.name.clone()),
                reason: None,
            },
            None => Status::default(),
        }
    }

    /// The link of `generation` broke: forget it, the reconnect loop takes over
    fn lost(&self, app: &AppHandle, generation: u64, reason: String) {
        let mut active = self.active.lock().unwrap();
        if active.as_ref().map(|a| a.generation) == Some(generation) {
            let target = active.take().map(|a| a.target);
            drop(active);
            emit_status(
                app,
                Status {
                    connected: false,
                    kind: target.as_ref().map(|t| t.kind),
                    id: target.as_ref().map(|t| t.id.clone()),
                    name: target.map(|t| t.name),
                    reason: Some(reason),
                },
            );
        }
    }

    pub async fn connect(self: &Arc<Self>, app: &AppHandle, target: Target) -> Result<Status, String> {
        self.close();
        *self.wanted.lock().unwrap() = Some(target.clone());
        self.open(app, target).await
    }

    pub fn disconnect(&self, app: &AppHandle) {
        *self.wanted.lock().unwrap() = None;
        self.close();
        emit_status(app, Status { reason: Some("disconnected".into()), ..Status::default() });
    }

    fn close(&self) {
        // dropping the handle closes the port / releases the GATT objects
        self.active.lock().unwrap().take();
    }

    async fn open(self: &Arc<Self>, app: &AppHandle, mut target: Target) -> Result<Status, String> {
        let generation = self.generation.fetch_add(1, Ordering::SeqCst) + 1;
        let handle = match target.kind {
            Kind::Usb => Handle::Usb(open_usb(self.clone(), app.clone(), &target.id, generation)?),
            #[cfg(windows)]
            Kind::Ble => {
                let lines = crate::ble_win::LineBuffer::default();
                let app_lines = app.clone();
                let (links, app_lost) = (self.clone(), app.clone());
                let (conn, _mtu, name) = crate::ble_win::connect(
                    &target.id,
                    move |bytes| {
                        for line in lines.push(&bytes) {
                            let _ = app_lines.emit("cc-line", Line { via: Kind::Ble, line });
                        }
                    },
                    move || links.lost(&app_lost, generation, "Bluetooth connection lost".into()),
                )
                .await?;
                if !name.is_empty() {
                    target.name = name;
                }
                Handle::Ble(Arc::new(conn))
            }
            #[cfg(not(windows))]
            Kind::Ble => return Err("Bluetooth is only supported on Windows for now: use the USB cable".into()),
        };
        *self.active.lock().unwrap() = Some(Active { target, handle, generation });
        let status = self.status();
        emit_status(app, status.clone());
        Ok(status)
    }

    pub async fn send(&self, app: &AppHandle, line: String) -> Result<(), String> {
        enum Out {
            #[cfg(windows)]
            Ble(Arc<crate::ble_win::BleConn>),
            Usb(SharedPort),
        }
        let (out, generation) = {
            let active = self.active.lock().unwrap();
            let a = active.as_ref().ok_or("not connected to a DeepDeck")?;
            let out = match &a.handle {
                #[cfg(windows)]
                Handle::Ble(conn) => Out::Ble(conn.clone()),
                Handle::Usb(usb) => Out::Usb(usb.port.clone()),
            };
            (out, a.generation)
        };
        let result = match out {
            #[cfg(windows)]
            Out::Ble(conn) => conn.send(&line).await,
            Out::Usb(port) => port
                .lock()
                .unwrap()
                .write_all(format!("{USB_PREFIX}{line}\n").as_bytes())
                .map_err(|err| err.to_string()),
        };
        if let Err(err) = &result {
            self.lost(app, generation, format!("send failed: {err}"));
        }
        result
    }

    /// Re-open the wanted device while it is not connected
    pub fn start_reconnect_loop(self: &Arc<Self>, app: AppHandle) {
        let links = self.clone();
        tauri::async_runtime::spawn(async move {
            loop {
                tokio_sleep(RECONNECT_EVERY).await;
                let wanted = links.wanted.lock().unwrap().clone();
                let connected = links.active.lock().unwrap().is_some();
                if let (Some(target), false) = (wanted, connected) {
                    let _ = links.open(&app, target).await;
                }
            }
        });
    }
}

async fn tokio_sleep(duration: Duration) {
    let _ = tauri::async_runtime::spawn_blocking(move || std::thread::sleep(duration)).await;
}

fn open_usb(links: Arc<Links>, app: AppHandle, port_name: &str, generation: u64) -> Result<UsbHandle, String> {
    // DTR/RTS drive EN/IO0 through the auto-reset circuit: keep both de-asserted
    let mut port = serialport::new(port_name, 115_200)
        .timeout(Duration::from_millis(200))
        .flow_control(serialport::FlowControl::None)
        .dtr_on_open(false)
        .open()
        .map_err(|err| format!("{port_name}: {err}"))?;
    port.write_data_terminal_ready(false).map_err(|err| err.to_string())?;
    port.write_request_to_send(false).map_err(|err| err.to_string())?;
    let reader = port.try_clone().map_err(|err| err.to_string())?;
    let alive = Arc::new(AtomicBool::new(true));
    let reader_alive = alive.clone();

    std::thread::spawn(move || {
        let mut reader = BufReader::new(reader);
        let mut buf = Vec::new();
        while reader_alive.load(Ordering::SeqCst) {
            match reader.read_until(b'\n', &mut buf) {
                Ok(0) => {}
                Ok(_) if buf.ends_with(b"\n") => {
                    let text = String::from_utf8_lossy(&buf).trim_end().to_string();
                    buf.clear();
                    match text.strip_prefix(USB_PREFIX) {
                        Some(line) => {
                            let _ = app.emit("cc-line", Line { via: Kind::Usb, line: line.to_string() });
                        }
                        None if !text.is_empty() => {
                            let _ = app.emit("cc-log", strip_ansi(&text));
                        }
                        None => {}
                    }
                }
                Ok(_) => {}
                Err(err) if err.kind() == std::io::ErrorKind::TimedOut => {}
                Err(err) => {
                    links.lost(&app, generation, format!("USB: {err}"));
                    break;
                }
            }
        }
    });
    Ok(UsbHandle { port: Arc::new(Mutex::new(port)), alive })
}

/// The firmware colours its log lines with ANSI escapes
fn strip_ansi(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\u{1b}' {
            while let Some(&n) = chars.peek() {
                chars.next();
                if n.is_ascii_alphabetic() {
                    break;
                }
            }
        } else {
            out.push(c);
        }
    }
    out
}

pub async fn list_devices() -> Vec<DeviceEntry> {
    let mut list = Vec::new();
    if let Ok(ports) = serialport::available_ports() {
        for p in ports {
            let (likely, detail) = match &p.port_type {
                serialport::SerialPortType::UsbPort(usb) => (
                    usb.vid == CP210X_VID && usb.pid == CP210X_PID,
                    usb.product.clone().unwrap_or_else(|| format!("USB {:04x}:{:04x}", usb.vid, usb.pid)),
                ),
                serialport::SerialPortType::BluetoothPort => (false, "Bluetooth serial".into()),
                _ => (false, "serial port".into()),
            };
            list.push(DeviceEntry { kind: Kind::Usb, id: p.port_name.clone(), name: p.port_name, detail, likely });
        }
    }
    #[cfg(windows)]
    if let Ok(paired) = crate::ble_win::paired().await {
        for d in paired {
            let lower = d.name.to_lowercase();
            let likely = lower.contains("ahuyama") || lower.contains("deepdeck");
            list.push(DeviceEntry { kind: Kind::Ble, id: d.id, name: d.name, detail: "paired Bluetooth device".into(), likely });
        }
    }
    // DeepDeck candidates first
    list.sort_by_key(|d| !d.likely);
    list
}
