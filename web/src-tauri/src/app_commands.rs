//! App commands: DeepDeck keys that run a command on the computer (key codes 0x5000 + n). The
//! DeepDeck sends the event `key.app {"id": n}` and this module runs command n, whichever window
//! is in front, e.g. `herdr pane focus --direction right`. Works while the app runs (tray included).
//! Commands live in `<app config dir>/app-commands.json`.

use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Listener, Manager};

pub const COMMANDS_NUM: usize = 32;
const RECENT_MAX: usize = 20;

#[derive(Serialize, Deserialize, Clone, Default)]
pub struct AppCommand {
    pub id: u8,
    pub name: String,
    /// Run by the system shell: cmd /C on Windows, sh -c elsewhere
    pub command: String,
}

#[derive(Serialize, Clone)]
pub struct Ran {
    pub id: u8,
    pub name: String,
    pub ok: bool,
    pub code: Option<i32>,
    pub output: String,
    pub at: u64,
}

#[derive(Default)]
pub struct AppCommands {
    commands: Mutex<Vec<AppCommand>>,
    recent: Mutex<Vec<Ran>>,
}

pub type SharedAppCommands = Arc<AppCommands>;

#[derive(Serialize)]
pub struct State {
    pub commands: Vec<AppCommand>,
    pub recent: Vec<Ran>,
}

fn path(app: &AppHandle) -> Option<PathBuf> {
    app.path().app_config_dir().ok().map(|d| d.join("app-commands.json"))
}

impl AppCommands {
    pub fn load(app: &AppHandle) -> SharedAppCommands {
        let commands = path(app)
            .and_then(|p| std::fs::read_to_string(p).ok())
            .and_then(|t| serde_json::from_str::<Vec<AppCommand>>(&t).ok())
            .unwrap_or_default();
        Arc::new(AppCommands { commands: Mutex::new(commands), ..Default::default() })
    }

    pub fn get(&self) -> State {
        State { commands: self.commands.lock().unwrap().clone(), recent: self.recent.lock().unwrap().clone() }
    }

    pub fn set(&self, app: &AppHandle, mut commands: Vec<AppCommand>) -> Result<(), String> {
        commands.retain(|c| (c.id as usize) < COMMANDS_NUM && !(c.name.trim().is_empty() && c.command.trim().is_empty()));
        let p = path(app).ok_or("no config folder")?;
        if let Some(dir) = p.parent() {
            std::fs::create_dir_all(dir).map_err(|err| err.to_string())?;
        }
        std::fs::write(&p, serde_json::to_string_pretty(&commands).map_err(|err| err.to_string())?).map_err(|err| err.to_string())?;
        *self.commands.lock().unwrap() = commands;
        Ok(())
    }

    /// Run command id in the background; the result goes to the log and to the window
    pub fn run(self: &Arc<Self>, app: &AppHandle, id: u8) {
        let Some(cmd) = self.commands.lock().unwrap().iter().find(|c| c.id == id).cloned() else { return };
        if cmd.command.trim().is_empty() {
            return;
        }
        let me = self.clone();
        let app = app.clone();
        std::thread::spawn(move || {
            let result = shell(&cmd.command).output();
            let at = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
            let ran = match result {
                Ok(out) => {
                    let mut text = String::from_utf8_lossy(&out.stdout).to_string();
                    text.push_str(&String::from_utf8_lossy(&out.stderr));
                    Ran { id, name: cmd.name.clone(), ok: out.status.success(), code: out.status.code(), output: text.trim().chars().take(600).collect(), at }
                }
                Err(err) => Ran { id, name: cmd.name.clone(), ok: false, code: None, output: err.to_string(), at },
            };
            {
                let mut recent = me.recent.lock().unwrap();
                recent.insert(0, ran.clone());
                recent.truncate(RECENT_MAX);
            }
            let _ = app.emit("cc-app-command", ran);
        });
    }

    /// Run the commands the DeepDeck asks for (event key.app on any link)
    pub fn start(self: &Arc<Self>, app: &AppHandle) {
        let me = self.clone();
        let handle = app.clone();
        app.listen("cc-line", move |event| {
            let Ok(payload) = serde_json::from_str::<serde_json::Value>(event.payload()) else { return };
            let line = payload["line"].as_str().unwrap_or("");
            if !line.contains("\"key.app\"") {
                return; // most lines are answers: skip the parsing
            }
            let Ok(msg) = serde_json::from_str::<serde_json::Value>(line) else { return };
            if msg["event"] == "key.app" {
                if let Some(id) = msg["data"]["id"].as_u64() {
                    me.run(&handle, id as u8);
                }
            }
        });
    }
}

#[cfg(windows)]
fn shell(command: &str) -> std::process::Command {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000; // no console window flashing up
    let mut c = std::process::Command::new("cmd");
    c.arg("/C").raw_arg(command).creation_flags(CREATE_NO_WINDOW);
    c
}

#[cfg(not(windows))]
fn shell(command: &str) -> std::process::Command {
    let mut c = std::process::Command::new("sh");
    c.arg("-c").arg(command);
    c
}
