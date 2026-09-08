#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::process::{Child, Command};
use std::sync::{Arc, Mutex};
use tauri::RunEvent;

#[allow(dead_code)]
struct CoreProcess(Arc<Mutex<Option<Child>>>);

fn main() {
    let core_child: Arc<Mutex<Option<Child>>> = Arc::new(Mutex::new(None));
    let child_clone = core_child.clone();

    // Check if music-sorter-core.exe exists
    let mut exe_path = std::env::current_dir()
        .unwrap_or_default()
        .join("music-sorter-core.exe");

    if !exe_path.exists() {
        if let Ok(parent) = std::env::current_exe() {
            let p_exe = parent.parent().unwrap_or(&parent).join("music-sorter-core.exe");
            if p_exe.exists() {
                exe_path = p_exe;
            }
        }
    }

    if exe_path.exists() {
        match Command::new(&exe_path).spawn() {
            Ok(child) => {
                println!("[TAURI] Launched headless core: {:?}", exe_path);
                *core_child.lock().unwrap() = Some(child);
            }
            Err(e) => {
                eprintln!("[TAURI ERROR] Failed to start core process: {:?}", e);
            }
        }
    } else {
        println!("[TAURI] Core executable not found at {:?}, expecting standalone daemon", exe_path);
    }

    let app = tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .manage(CoreProcess(child_clone))
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    app.run(move |_app_handle, event| {
        if let RunEvent::ExitRequested { .. } = event {
            if let Ok(mut lock) = core_child.lock() {
                if let Some(mut child) = lock.take() {
                    let _ = child.kill();
                    let _ = child.wait();
                    println!("[TAURI] Headless core process terminated cleanly.");
                }
            }
        }
    });
}
