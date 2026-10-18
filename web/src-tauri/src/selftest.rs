//! DD_CC_SELFTEST=1 [DD_CC_SELFTEST_KIND=ble|usb]: connect to the first DeepDeck found, run a few
//! read-only requests through the same link the pages use, print RESULT lines and exit.

use std::sync::mpsc;
use std::time::{Duration, Instant};

use tauri::{AppHandle, Listener, Manager};

use crate::link::{self, Kind, Target};
use crate::SharedLinks;

pub fn run(app: AppHandle) {
    let (tx, rx) = mpsc::channel::<String>();
    app.listen("cc-line", move |event| {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(event.payload()) {
            let _ = tx.send(v["line"].as_str().unwrap_or("").to_string());
        }
    });

    std::thread::spawn(move || {
        let kind = match std::env::var("DD_CC_SELFTEST_KIND").as_deref() {
            Ok("usb") => Kind::Usb,
            _ => Kind::Ble,
        };
        let devices = tauri::async_runtime::block_on(link::list_devices());
        let Some(dev) = devices.into_iter().find(|d| d.likely && d.kind == kind) else {
            println!("RESULT no DeepDeck found over {kind:?}");
            app.exit(1);
            return;
        };
        let links: SharedLinks = app.state::<SharedLinks>().inner().clone();
        let target = Target { kind: dev.kind, id: dev.id.clone(), name: dev.name.clone() };
        match tauri::async_runtime::block_on(links.clone().connect(&app, target)) {
            Ok(status) => println!("RESULT connected {:?} {}", status.kind, status.name.unwrap_or_default()),
            Err(err) => {
                println!("RESULT connect failed: {err}");
                app.exit(1);
                return;
            }
        }

        let mut failures = 0;
        // ids far above the ones the app window uses: it may be sending requests at the same time
        for (id, method) in [(900001, "device.info"), (900002, "layers.list"), (900003, "led.get"), (900004, "led.effects")] {
            let line = format!("{{\"id\":{id},\"method\":\"{method}\"}}");
            let started = Instant::now();
            if let Err(err) = tauri::async_runtime::block_on(links.send(&app, line)) {
                println!("RESULT {method}: send failed: {err}");
                failures += 1;
                continue;
            }
            let answer = loop {
                match rx.recv_timeout(Duration::from_secs(5)) {
                    Ok(l) if l.contains(&format!("\"id\":{id},")) => break Some(l),
                    Ok(_) => continue, // events or other answers
                    Err(_) => break None,
                }
            };
            match answer {
                Some(l) if l.contains("\"result\"") => {
                    let head: String = l.chars().take(90).collect();
                    println!("RESULT {method}: ok in {} ms, {} bytes: {head}", started.elapsed().as_millis(), l.len())
                }
                Some(l) => {
                    println!("RESULT {method}: error {l}");
                    failures += 1;
                }
                None => {
                    println!("RESULT {method}: no answer within 5 s");
                    failures += 1;
                }
            }
        }
        println!("RESULT {failures} failure(s)");
        app.exit(if failures == 0 { 0 } else { 1 });
    });
}
