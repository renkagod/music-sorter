#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::path::{Path, PathBuf};
use std::process::{Child, Command};
use std::sync::{Arc, Mutex};
use tauri::{Manager, RunEvent};

#[cfg(windows)]
use std::os::windows::process::CommandExt;

#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x08000000;

fn log_file(msg: &str) {
    if let Ok(mut f) = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(r"d:\Coding\Projects\music-sorter\tauri_startup.log")
    {
        use std::io::Write;
        let _ = writeln!(f, "{}", msg);
    }
}

#[allow(dead_code)]
struct CoreProcess(Arc<Mutex<Option<Child>>>);

fn find_core_executable() -> Option<PathBuf> {
    // 1. Current working directory
    if let Ok(cwd) = std::env::current_dir() {
        let p = cwd.join("music-sorter-core.exe");
        if p.exists() {
            return Some(p);
        }
        if let Some(parent) = cwd.parent() {
            let p = parent.join("music-sorter-core.exe");
            if p.exists() {
                return Some(p);
            }
        }
    }

    // 2. Executable parent directory (installed / packaged bundle)
    if let Ok(exe) = std::env::current_exe() {
        if let Some(parent) = exe.parent() {
            let p = parent.join("music-sorter-core.exe");
            if p.exists() {
                return Some(p);
            }
            // Cargo target/debug or target/release
            if let Some(p2) = parent.parent() {
                if let Some(p3) = p2.parent() {
                    let p = p3.join("music-sorter-core.exe");
                    if p.exists() {
                        return Some(p);
                    }
                    if let Some(p4) = p3.parent() {
                        let p = p4.join("music-sorter-core.exe");
                        if p.exists() {
                            return Some(p);
                        }
                    }
                }
            }
        }
    }

    // 3. Fallback to known development root
    let dev_root = PathBuf::from(r"d:\Coding\Projects\music-sorter\music-sorter-core.exe");
    if dev_root.exists() {
        return Some(dev_root);
    }

    None
}

fn main() {
    std::panic::set_hook(Box::new(|info| {
        log_file(&format!("PANIC: {}", info));
    }));
    log_file("=== App starting ===");

    #[cfg(windows)]
    std::env::remove_var("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS");

    let core_child: Arc<Mutex<Option<Child>>> = Arc::new(Mutex::new(None));
    let child_clone = core_child.clone();

    let exe_path = find_core_executable();
    if let Some(ref path) = exe_path {
        let working_dir = path.parent().unwrap_or_else(|| Path::new("."));
        let my_pid = std::process::id();
        log_file(&format!("Spawning core at {:?}", path));
        let mut cmd = Command::new(path);
        cmd.current_dir(working_dir)
            .arg("--parent-pid")
            .arg(my_pid.to_string());

        #[cfg(windows)]
        cmd.creation_flags(CREATE_NO_WINDOW);

        match cmd.spawn() {
            Ok(child) => {
                log_file(&format!("Core spawned PID {}", child.id()));
                *core_child.lock().unwrap() = Some(child);
            }
            Err(e) => {
                log_file(&format!("Failed to start core: {:?}", e));
            }
        }
    } else {
        log_file("Core executable not found");
    }

    log_file("Starting Tauri application...");
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .manage(CoreProcess(child_clone))
        .setup(|_app| {
            log_file("Tauri setup hook executed cleanly.");
            Ok(())
        })
        .on_window_event(move |_window, event| {
            if let tauri::WindowEvent::Destroyed = event {
                log_file("Window destroyed, shutting down core...");
                let _ = Command::new("curl.exe")
                    .args(["-s", "-X", "POST", "http://127.0.0.1:8765/api/shutdown"])
                    .output();

                if let Ok(mut lock) = core_child.lock() {
                    if let Some(mut child) = lock.take() {
                        let _ = child.kill();
                        log_file("Headless core process terminated cleanly.");
                    }
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
