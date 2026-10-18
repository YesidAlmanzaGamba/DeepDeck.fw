//! Per-application layers (R6.3): when another program comes to the front, switch the DeepDeck to
//! the layer chosen for it. Runs in the app's own thread, so it also works with the window hidden
//! in the tray. Rules live in `<app config dir>/app-layers.json`.

use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager};

use crate::SharedLinks;

#[derive(Serialize, Deserialize, Clone, Default)]
pub struct Rule {
    /// Executable file name, lower case, e.g. "chrome.exe"
    pub exe: String,
    pub uuid: String,
    /// For display only; the uuid is what counts
    #[serde(default)]
    pub layer_name: String,
}

#[derive(Serialize, Deserialize, Clone, Default)]
pub struct Rules {
    pub enabled: bool,
    pub rules: Vec<Rule>,
    /// Layer for programs without a rule; None keeps the current layer
    #[serde(default)]
    pub fallback: Option<String>,
}

#[derive(Serialize, Clone, Default)]
pub struct State {
    #[serde(flatten)]
    pub rules: Rules,
    /// Programs seen in front recently, newest first, to pick from
    pub recent: Vec<String>,
    pub current: String,
}

#[derive(Serialize, Clone)]
struct Switched {
    exe: String,
    uuid: String,
}

#[derive(Default)]
pub struct AppLayers {
    state: Mutex<State>,
    last_sent: Mutex<String>,
}

pub type SharedAppLayers = Arc<AppLayers>;

const OWN_EXE: &str = "deepdeck-command-center.exe";
const RECENT_MAX: usize = 12;
// ids of the requests this module sends; the window ignores answers it did not ask for
const REQUEST_ID_BASE: u64 = 700_000;

fn rules_path(app: &AppHandle) -> Option<PathBuf> {
    app.path().app_config_dir().ok().map(|dir| dir.join("app-layers.json"))
}

impl AppLayers {
    pub fn load(app: &AppHandle) -> SharedAppLayers {
        let rules = rules_path(app)
            .and_then(|p| std::fs::read_to_string(p).ok())
            .and_then(|text| serde_json::from_str::<Rules>(&text).ok())
            .unwrap_or_default();
        Arc::new(AppLayers { state: Mutex::new(State { rules, ..State::default() }), ..AppLayers::default() })
    }

    pub fn get(&self) -> State {
        self.state.lock().unwrap().clone()
    }

    pub fn set(&self, app: &AppHandle, mut rules: Rules) -> Result<(), String> {
        for r in &mut rules.rules {
            r.exe = r.exe.trim().to_lowercase();
        }
        rules.rules.retain(|r| !r.exe.is_empty() && !r.uuid.is_empty());
        let path = rules_path(app).ok_or("no config folder")?;
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir).map_err(|err| err.to_string())?;
        }
        let text = serde_json::to_string_pretty(&rules).map_err(|err| err.to_string())?;
        std::fs::write(&path, text).map_err(|err| err.to_string())?;
        self.state.lock().unwrap().rules = rules;
        self.last_sent.lock().unwrap().clear(); // apply to the current program on the next check
        Ok(())
    }

    /// Watch the foreground program and switch layers by the rules
    pub fn start(self: &Arc<Self>, app: AppHandle, links: SharedLinks) {
        let me = self.clone();
        std::thread::spawn(move || {
            let mut seen = String::new();
            let mut next_id = REQUEST_ID_BASE;
            loop {
                std::thread::sleep(Duration::from_millis(400));
                let Some(exe) = foreground_exe() else { continue };
                if exe == OWN_EXE {
                    continue; // configuring the DeepDeck: keep the layer as it is
                }
                if exe != seen {
                    seen = exe.clone();
                    me.last_sent.lock().unwrap().clear(); // a new program: its layer is still to apply
                    let mut st = me.state.lock().unwrap();
                    st.current = exe.clone();
                    st.recent.retain(|e| e != &exe);
                    st.recent.insert(0, exe.clone());
                    st.recent.truncate(RECENT_MAX);
                }
                let target = {
                    let st = me.state.lock().unwrap();
                    if !st.rules.enabled {
                        continue;
                    }
                    st.rules
                        .rules
                        .iter()
                        .find(|r| r.exe == exe)
                        .map(|r| r.uuid.clone())
                        .or_else(|| st.rules.fallback.clone())
                };
                let Some(uuid) = target else { continue };
                // once per program change (or rules change); while not connected, try again later
                if !me.last_sent.lock().unwrap().is_empty() || !links.status().connected {
                    continue;
                }
                next_id += 1;
                let line = serde_json::json!({ "id": next_id, "method": "layers.set_active", "params": { "uuid": uuid } }).to_string();
                if tauri::async_runtime::block_on(links.send(&app, line)).is_ok() {
                    *me.last_sent.lock().unwrap() = uuid.clone();
                    let _ = app.emit("cc-app-layer", Switched { exe, uuid });
                }
            }
        });
    }
}

/// File name of the program owning the foreground window, lower case
#[cfg(windows)]
fn foreground_exe() -> Option<String> {
    use windows::core::PWSTR;
    use windows::Win32::Foundation::CloseHandle;
    use windows::Win32::System::Threading::{OpenProcess, QueryFullProcessImageNameW, PROCESS_NAME_WIN32, PROCESS_QUERY_LIMITED_INFORMATION};
    use windows::Win32::UI::WindowsAndMessaging::{GetForegroundWindow, GetWindowThreadProcessId};

    unsafe {
        let hwnd = GetForegroundWindow();
        if hwnd.0.is_null() {
            return None;
        }
        let mut pid = 0u32;
        GetWindowThreadProcessId(hwnd, Some(&mut pid));
        if pid == 0 {
            return None;
        }
        let process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid).ok()?;
        let mut buf = [0u16; 1024];
        let mut len = buf.len() as u32;
        let ok = QueryFullProcessImageNameW(process, PROCESS_NAME_WIN32, PWSTR(buf.as_mut_ptr()), &mut len).is_ok();
        let _ = CloseHandle(process);
        if !ok {
            return None;
        }
        let path = String::from_utf16_lossy(&buf[..len as usize]);
        path.rsplit(['\\', '/']).next().map(|name| name.to_lowercase())
    }
}

#[cfg(not(windows))]
fn foreground_exe() -> Option<String> {
    None // macOS and Linux: not yet (R4.4)
}
