#![windows_subsystem = "windows"]

use eframe::egui;
use rfd::FileDialog;
use std::ffi::{CStr, CString};
use std::fs;
use std::io::{BufRead, BufReader};
use std::os::raw::c_char;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::mpsc::{self, Receiver, Sender};
use std::thread;
use sysinfo::{System};

// FFI Definitions
type LogCallback = extern "C" fn(*mut std::ffi::c_void, *const c_char);

extern "C" fn vrm_log_callback(user_data: *mut std::ffi::c_void, msg: *const c_char) {
    if user_data.is_null() || msg.is_null() {
        return;
    }
    let sender: &Sender<String> = unsafe { &*(user_data as *const Sender<String>) };
    let text = unsafe { CStr::from_ptr(msg) }.to_string_lossy().into_owned();
    let _ = sender.send(text);
}

#[derive(Debug, PartialEq, Clone, Copy)]
enum DependencyStatus {
    Unknown,
    Checking,
    Ready,
    IssuesFound,
}

struct SfmModel {
    name: String,
    selected: bool,
}

fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([540.0, 520.0])
            .with_resizable(true),
        ..Default::default()
    };
    eframe::run_native(
        "VRM to SFM Converter V4",
        options,
        Box::new(|_cc| Ok(Box::new(VrmApp::default()))),
    )
}

struct VrmApp {
    vrm_file: Option<PathBuf>,
    scale: f32,
    output_dir: String,
    studiomdl_path: String,
    logs: Vec<String>,
    is_converting: bool,
    progress: f32,
    log_receiver: Option<Receiver<String>>,
    latest_version: Option<String>,
    version_receiver: Option<Receiver<String>>,
    dep_status: DependencyStatus,
    dep_log: String,
    sfm_running: bool,
    sfm_models: Vec<SfmModel>,
    show_manager: bool,
}

impl Default for VrmApp {
    fn default() -> Self {
        let studiomdl = Self::load_config();
        
        let (tx_ver, rx_ver) = mpsc::channel();
        thread::spawn(move || {
            let client = reqwest::blocking::Client::builder()
                .user_agent("VRM2MDL-GUI")
                .timeout(std::time::Duration::from_secs(5))
                .build()
                .unwrap_or_default();
            if let Ok(res) = client.get("https://api.github.com/repos/DoomGame221/VRM2MDL/releases/latest").send() {
                if let Ok(json) = res.json::<serde_json::Value>() {
                    if let Some(tag) = json["tag_name"].as_str() {
                        let _ = tx_ver.send(tag.to_string());
                    }
                }
            }
        });

        Self {
            vrm_file: None,
            scale: 39.37,
            output_dir: "./output".to_string(),
            studiomdl_path: studiomdl,
            logs: vec!["Welcome to VRM2MDL V4".to_string()],
            is_converting: false,
            progress: 0.0,
            log_receiver: None,
            latest_version: None,
            version_receiver: Some(rx_ver),
            dep_status: DependencyStatus::Unknown,
            dep_log: String::new(),
            sfm_running: false,
            sfm_models: Vec::new(),
            show_manager: false,
        }
    }
}

impl VrmApp {
    fn check_sfm_status(&mut self) {
        let mut sys = System::new_all();
        sys.refresh_processes();
        self.sfm_running = sys.processes().values().any(|p| {
            let name = p.name().to_lowercase();
            name.contains("sfm.exe") || name.contains("hlmv.exe")
        });
    }

    fn refresh_sfm_models(&mut self) {
        if self.studiomdl_path.is_empty() { return; }
        
        let mut models = Vec::new();
        let mut usermod_models = PathBuf::new();
        if let Some(pos) = self.studiomdl_path.find("/game/bin/") {
            usermod_models = PathBuf::from(&self.studiomdl_path[..pos]).join("game/usermod/models");
        } else if let Some(pos) = self.studiomdl_path.find("\\game\\bin\\") {
            usermod_models = PathBuf::from(&self.studiomdl_path[..pos]).join("game/usermod/models");
        }

        if usermod_models.exists() {
            if let Ok(entries) = fs::read_dir(usermod_models) {
                for entry in entries.flatten() {
                    if entry.file_type().map(|t| t.is_dir()).unwrap_or(false) {
                        let name = entry.file_name().to_string_lossy().into_owned();
                        if name != "player" && name != "workshop" {
                            models.push(SfmModel { name, selected: false });
                        }
                    }
                }
            }
        }
        self.sfm_models = models;
    }

    fn delete_selected_models(&mut self) {
        if self.studiomdl_path.is_empty() { return; }
        
        let mut base_path = String::new();
        if let Some(pos) = self.studiomdl_path.find("/game/bin/") {
            base_path = self.studiomdl_path[..pos].to_string();
        } else if let Some(pos) = self.studiomdl_path.find("\\game\\bin\\") {
            base_path = self.studiomdl_path[..pos].to_string();
        }

        if base_path.is_empty() { return; }

        for model in &self.sfm_models {
            if model.selected {
                let mdl_path = PathBuf::from(&base_path).join("game/usermod/models").join(&model.name);
                let mat_path = PathBuf::from(&base_path).join("game/usermod/materials/models").join(&model.name);
                
                let _ = fs::remove_dir_all(mdl_path);
                let _ = fs::remove_dir_all(mat_path);
                self.logs.push(format!("🗑 Deleted: {}", model.name));
            }
        }
        self.refresh_sfm_models();
    }

    fn check_dependencies(&mut self) {
        self.dep_status = DependencyStatus::Checking;
        let mut report = String::new();
        let mut missing = 0;

        // 1. Check Library (DLL)
        let mut lib_found = false;
        let core_names = ["vrm_core", "assimp_core"];
        for name in core_names {
            let lib_name = if cfg!(target_os = "windows") { format!("{}.dll", name) } else { format!("lib{}.so", name) };
            let candidates = ["./release/cores/", "./cores/", "./bin/", "./"];
            for c in candidates {
                if Path::new(c).join(&lib_name).exists() {
                    lib_found = true;
                    break;
                }
            }
            if lib_found { break; }
        }

        if lib_found {
            report.push_str("✅ Core Libraries Found\n");
        } else {
            report.push_str("❌ Core Libraries Missing (vrm_core.dll)\n");
            missing += 1;
        }

        // 2. Check studiomdl
        if !self.studiomdl_path.is_empty() && Path::new(&self.studiomdl_path).exists() {
            report.push_str("✅ studiomdl.exe Ready\n");
        } else {
            report.push_str("⚠️ studiomdl.exe not found. Deep scan recommended.\n");
            missing += 1;
        }

        // 3. Check Wine (Linux only)
        if cfg!(target_os = "linux") {
            match Command::new("wine").arg("--version").output() {
                Ok(_) => report.push_str("✅ Wine installed\n"),
                Err(_) => {
                    report.push_str("❌ Wine not found (Required for studiomdl)\n");
                    missing += 1;
                }
            }
        }

        self.dep_log = report;
        self.dep_status = if missing == 0 { DependencyStatus::Ready } else { DependencyStatus::IssuesFound };
    }

    fn run_deep_scan(&mut self) {
        self.dep_status = DependencyStatus::Checking;
        self.dep_log = "🔍 Scanning all drives for studiomdl.exe...\n".to_string();
        
        let (tx, rx) = mpsc::channel();
        self.log_receiver = Some(rx);
        let tx_copy = tx.clone();

        thread::spawn(move || {
            let mut found_path = None;
            let steam_suffixes = [
                "Steam/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe",
                "SteamLibrary/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe",
                "Program Files (x86)/Steam/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe",
                "Program Files/Steam/steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe",
            ];

            #[cfg(target_os = "windows")]
            {
                let drives = unsafe {
                    let mut buffer = [0u8; 256];
                    let len = windows_sys::Win32::Storage::FileSystem::GetLogicalDriveStringsA(buffer.len() as u32, buffer.as_mut_ptr());
                    if len > 0 {
                        std::str::from_utf8(&buffer[..len as usize])
                            .unwrap_or("")
                            .split('\0')
                            .filter(|s| !s.is_empty())
                            .map(|s| s.to_string())
                            .collect::<Vec<_>>()
                    } else {
                        vec!["C:\\".to_string()]
                    }
                };

                for drive in drives {
                    let _ = tx_copy.send(format!("Checking drive {}...", drive));
                    for suffix in &steam_suffixes {
                        let full_path = Path::new(&drive).join(suffix);
                        if full_path.exists() {
                            found_path = Some(full_path);
                            break;
                        }
                    }
                    if found_path.is_some() { break; }
                }
            }

            #[cfg(target_os = "linux")]
            {
                if let Ok(home) = std::env::var("HOME") {
                    let search_dirs = [
                        format!("{}/.local/share/Steam", home),
                        format!("{}/.steam/steam", home),
                        format!("{}/.steam/root", home),
                    ];
                    for base in search_dirs {
                        let full_path = Path::new(&base).join("steamapps/common/SourceFilmmaker/game/bin/studiomdl.exe");
                        if full_path.exists() {
                            found_path = Some(full_path);
                            break;
                        }
                    }
                }
            }

            if let Some(p) = found_path {
                let path_str = p.to_string_lossy().to_string();
                let _ = tx_copy.send(format!("###PATH_FOUND###{}", path_str));
            } else {
                let _ = tx_copy.send("###SCAN_FAILED###".to_string());
            }
        });
    }

    fn load_config() -> String {
        let candidates = [
            std::env::current_exe().ok().and_then(|p| p.parent().map(|d| d.join("config.txt"))),
            Some(PathBuf::from("config.txt")),
        ];
        for candidate in candidates {
            if let Some(path) = candidate {
                if path.exists() {
                    if let Ok(content) = fs::read_to_string(path) {
                        return content.trim().to_string();
                    }
                }
            }
        }
        String::new()
    }

    fn save_config(path: &str) {
        let _ = fs::write("config.txt", path);
    }

    fn start_conversion(&mut self) {
        let file = match self.vrm_file.clone() {
            Some(f) => f,
            None => return,
        };

        self.is_converting = true;
        self.progress = 0.0;
        self.logs.clear();
        self.logs.push(format!("Starting: {} (scale={})", file.display(), self.scale));

        let (tx, rx): (Sender<String>, Receiver<String>) = mpsc::channel();
        self.log_receiver = Some(rx);

        let scale_val = self.scale;
        let output_dir = self.output_dir.clone();
        let studiomdl_path = self.studiomdl_path.clone();

        thread::spawn(move || {
            let ext = file.extension().unwrap_or_default().to_string_lossy().to_lowercase();
            let core_name = if ext == "fbx" || ext == "obj" { "assimp_core" } else { "vrm_core" };
            let lib_name = if cfg!(target_os = "windows") { format!("{}.dll", core_name) } else { format!("lib{}.so", core_name) };

            let mut lib_candidates = Vec::new();
            if let Ok(self_exe) = std::env::current_exe() {
                if let Some(exe_dir) = self_exe.parent() {
                    lib_candidates.push(exe_dir.join(&lib_name));
                    lib_candidates.push(exe_dir.join("cores").join(&lib_name));
                }
            }
            lib_candidates.push(PathBuf::from(format!("./cores/{}", lib_name)));
            lib_candidates.push(PathBuf::from(&lib_name));

            let core_lib = lib_candidates.iter().find(|p| p.exists()).cloned().unwrap_or_else(|| PathBuf::from(&lib_name));
            let _ = tx.send(format!("🚀 Loading Core: {}", core_lib.display()));

            let result = unsafe {
                let lib = match libloading::Library::new(&core_lib) {
                    Ok(l) => l,
                    Err(e) => {
                        let _ = tx.send(format!("❌ Failed to load library: {}", e));
                        let _ = tx.send("###ERROR###".to_string());
                        return;
                    }
                };

                let vrm2mdl_convert: libloading::Symbol<
                    unsafe extern "C" fn(*const c_char, *const c_char, f32, LogCallback, *mut std::ffi::c_void) -> std::os::raw::c_int,
                > = match lib.get(b"vrm2mdl_convert\0") {
                    Ok(f) => f,
                    Err(e) => {
                        let _ = tx.send(format!("❌ Symbol not found: {}", e));
                        let _ = tx.send("###ERROR###".to_string());
                        return;
                    }
                };

                let c_vrm_path = CString::new(file.to_str().unwrap_or_default()).unwrap();
                let c_output_dir = CString::new(output_dir.clone()).unwrap();
                let user_data = &tx as *const Sender<String> as *mut std::ffi::c_void;

                vrm2mdl_convert(c_vrm_path.as_ptr(), c_output_dir.as_ptr(), scale_val, vrm_log_callback, user_data)
            };

            if result != 0 {
                let _ = tx.send(format!("❌ Conversion failed: code {}", result));
                let _ = tx.send("###ERROR###".to_string());
                return;
            }

            // studiomdl phase
            if !studiomdl_path.is_empty() && PathBuf::from(&studiomdl_path).exists() {
                let _ = tx.send("[2/3] Compiling MDL...".to_string());
                let mdl_name = file.file_stem().unwrap_or_default().to_string_lossy().replace(" ", "_");
                let base_dir = PathBuf::from(&output_dir).join(&mdl_name);
                let qc_file = base_dir.join("modelsrc").join(format!("{}.qc", mdl_name));

                let gameinfo_content = "\"GameInfo\"\n{\n\tgame \"vrm2mdl Local Compile\"\n\ttype singleplayer_only\n\tFileSystem\n\t{\n\t\tSteamAppId 1840\n\t\tSearchPaths\n\t\t{\n\t\t\tgame \".\"\n\t\t}\n\t}\n}\n";
                let _ = fs::write(base_dir.join("gameinfo.txt"), gameinfo_content);

                let mut cmd = if cfg!(target_os = "linux") {
                    let mut c = Command::new("wine");
                    if let Ok(home) = std::env::var("HOME") {
                        c.env("WINEPREFIX", format!("{}/.local/share/Steam/steamapps/compatdata/1840/pfx", home));
                    }
                    c.env("WINEDEBUG", "-all");
                    c.arg(&studiomdl_path);
                    c
                } else {
                    Command::new(&studiomdl_path)
                };

                fn clean_path_for_cmd(p: PathBuf) -> String {
                    let s = p.to_string_lossy().to_string();
                    if s.starts_with(r"\\?\") { s[4..].to_string() } else { s }
                }

                cmd.arg("-nop4").arg("-game").arg(clean_path_for_cmd(base_dir.clone())).arg(clean_path_for_cmd(qc_file));
                cmd.stdout(Stdio::piped()).stderr(Stdio::piped());

                let mut child = match cmd.spawn() {
                    Ok(c) => c,
                    Err(e) => {
                        let _ = tx.send(format!("❌ Failed to run studiomdl: {}", e));
                        let _ = tx.send("###ERROR###".to_string());
                        return;
                    }
                };

                if let Some(stdout) = child.stdout.take() {
                    let reader = BufReader::new(stdout);
                    for line in reader.lines().map_while(Result::ok) { let _ = tx.send(line); }
                }

                match child.wait() {
                    Ok(s) if s.success() => {
                        let _ = tx.send("[OK] MDL Compiled Successfully!".to_string());
                        let _ = tx.send("[3/3] Deploying to SFM...".to_string());
                        let mut deploy_dir = String::new();
                        if let Some(pos) = studiomdl_path.find("/game/bin/") {
                            deploy_dir = format!("{}/game/usermod", &studiomdl_path[..pos]);
                        } else if let Some(pos) = studiomdl_path.find("\\game\\bin\\") {
                            deploy_dir = format!("{}\\game\\usermod", &studiomdl_path[..pos]);
                        }

                        if !deploy_dir.is_empty() {
                            let deploy_path = PathBuf::from(&deploy_dir);
                            let d_models = deploy_path.join("models").join(&mdl_name);
                            let d_mats = deploy_path.join("materials").join("models").join(&mdl_name);
                            let _ = fs::remove_dir_all(&d_models);
                            let _ = fs::remove_dir_all(&d_mats);
                            
                            fn copy_dir_all(src: impl AsRef<Path>, dst: impl AsRef<Path>) -> std::io::Result<()> {
                                fs::create_dir_all(&dst)?;
                                for entry in fs::read_dir(src)? {
                                    let entry = entry?;
                                    if entry.file_type()?.is_dir() { copy_dir_all(entry.path(), dst.as_ref().join(entry.file_name()))?; }
                                    else { fs::copy(entry.path(), dst.as_ref().join(entry.file_name()))?; }
                                }
                                Ok(())
                            }

                            let src_models = base_dir.join("models").join(&mdl_name);
                            let src_mats = base_dir.join("materials").join("models").join(&mdl_name);
                            if let Err(e) = copy_dir_all(&src_models, &d_models) { let _ = tx.send(format!("❌ Model deploy error: {}", e)); }
                            if let Err(e) = copy_dir_all(&src_mats, &d_mats) { let _ = tx.send(format!("❌ Material deploy error: {}", e)); }
                            let _ = tx.send(format!("✅ Deployed to: {}", deploy_dir));
                        }
                    }
                    _ => { let _ = tx.send("❌ Compilation failed.".to_string()); let _ = tx.send("###ERROR###".to_string()); return; }
                }
            }
            let _ = tx.send("###FINISHED###".to_string());
        });
    }
}

impl eframe::App for VrmApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let mut found_path = None;
        let mut scan_failed = false;

        if let Some(rx) = &self.log_receiver {
            while let Ok(msg) = rx.try_recv() {
                if msg.starts_with("###PATH_FOUND###") { found_path = Some(msg.replace("###PATH_FOUND###", "")); }
                else if msg == "###SCAN_FAILED###" { scan_failed = true; }
                else if msg == "###FINISHED###" { self.is_converting = false; self.progress = 1.0; self.logs.push("✅ All operations complete.".to_string()); }
                else if msg == "###ERROR###" { self.is_converting = false; }
                else {
                    if msg.contains("[1/3]") { self.progress = 0.33; }
                    else if msg.contains("[2/3]") { self.progress = 0.66; }
                    else if msg.contains("[3/3]") { self.progress = 0.90; }
                    self.logs.push(msg);
                }
            }
        }

        if let Some(p) = found_path { self.studiomdl_path = p; Self::save_config(&self.studiomdl_path); self.check_dependencies(); }
        if scan_failed { self.check_dependencies(); }
        if let Some(rx) = &self.version_receiver { if let Ok(ver) = rx.try_recv() { self.latest_version = Some(ver); } }

        // Refresh SFM status every few frames
        if ui.input(|i| i.time % 2.0 < 0.1) { self.check_sfm_status(); }

        if self.is_converting { ui.ctx().request_repaint(); }

        egui::CentralPanel::default().show_inside(ui, |ui| {
            ui.vertical_centered(|ui| {
                ui.add_space(5.0);
                ui.heading("🎭 VRM → Source Engine MDL");
                ui.add_space(5.0);
            });
            ui.separator();

            if self.sfm_running {
                ui.group(|ui| {
                    ui.horizontal(|ui| {
                        ui.colored_label(egui::Color32::RED, "🚨 SFM/HLMV is currently running!");
                        ui.small("Please close them before converting.");
                    });
                });
                ui.add_space(5.0);
            }

            egui::Grid::new("main_grid").num_columns(2).spacing([15.0, 10.0]).show(ui, |ui| {
                ui.label("Input Model:");
                ui.horizontal(|ui| {
                    if ui.button("📂 Browse").clicked() {
                        if let Some(path) = FileDialog::new().add_filter("3D Models", &["vrm", "fbx", "obj"]).pick_file() { self.vrm_file = Some(path); }
                    }
                    if let Some(file) = &self.vrm_file { ui.label(file.file_name().unwrap_or_default().to_string_lossy()); }
                    else { ui.colored_label(egui::Color32::GRAY, "No file selected"); }
                });
                ui.end_row();

                ui.label("Scale Factor:");
                ui.add(egui::DragValue::new(&mut self.scale).speed(0.1).range(1.0..=200.0));
                ui.end_row();

                ui.label("studiomdl Path:");
                ui.horizontal(|ui| {
                    ui.add_sized([300.0, 20.0], egui::TextEdit::singleline(&mut self.studiomdl_path));
                    if ui.button("📂").clicked() {
                        if let Some(path) = FileDialog::new().add_filter("studiomdl", &["exe"]).pick_file() {
                            self.studiomdl_path = path.to_string_lossy().to_string();
                            Self::save_config(&self.studiomdl_path);
                        }
                    }
                });
                ui.end_row();
            });

            ui.add_space(10.0);
            ui.horizontal(|ui| {
                if ui.button("⚙️ Manage SFM Models").clicked() {
                    self.show_manager = !self.show_manager;
                    if self.show_manager { self.refresh_sfm_models(); }
                }
            });

            if self.show_manager {
                ui.group(|ui| {
                    ui.label("Imported Models (usermod):");
                    egui::ScrollArea::vertical().max_height(100.0).show(ui, |ui| {
                        for model in &mut self.sfm_models {
                            ui.checkbox(&mut model.selected, &model.name);
                        }
                    });
                    ui.horizontal(|ui| {
                        if ui.button("🗑 Delete Selected").clicked() { self.delete_selected_models(); }
                        if ui.button("🔄 Refresh").clicked() { self.refresh_sfm_models(); }
                    });
                });
            }

            ui.add_space(15.0);
            let (status_text, status_color) = match self.dep_status {
                DependencyStatus::Unknown => ("Unknown", egui::Color32::GRAY),
                DependencyStatus::Checking => ("Checking...", egui::Color32::LIGHT_BLUE),
                DependencyStatus::Ready => ("Ready", egui::Color32::GREEN),
                DependencyStatus::IssuesFound => ("Action Required", egui::Color32::YELLOW),
            };

            ui.group(|ui| {
                ui.vertical(|ui| {
                    ui.horizontal(|ui| {
                        ui.label("System Status:");
                        ui.colored_label(status_color, status_text);
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if ui.button("🔍 Check Now").clicked() { self.check_dependencies(); }
                            if self.dep_status == DependencyStatus::IssuesFound {
                                if ui.button("🚀 Deep Scan").clicked() { self.run_deep_scan(); }
                            }
                        });
                    });
                    if !self.dep_log.is_empty() { ui.add_space(5.0); ui.small(&self.dep_log); }
                });
            });

            ui.add_space(15.0);
            ui.horizontal(|ui| {
                let can_convert = self.vrm_file.is_some() && !self.is_converting && !self.studiomdl_path.is_empty() && !self.sfm_running;
                if ui.add_enabled(can_convert, egui::Button::new("🚀 Start Conversion").min_size([140.0, 32.0].into())).clicked() { self.start_conversion(); }
                if self.is_converting { ui.spinner(); ui.label("Converting..."); }
            });

            ui.add_space(10.0);
            ui.add(egui::ProgressBar::new(self.progress).show_percentage().animate(self.is_converting));

            ui.add_space(15.0);
            ui.separator();
            ui.horizontal(|ui| {
                ui.label("Log:");
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.button("🗑 Clear").clicked() { self.logs.clear(); }
                });
            });
            egui::ScrollArea::vertical().stick_to_bottom(true).max_height(80.0).show(ui, |ui| {
                for line in &self.logs { ui.monospace(line); }
            });

            ui.with_layout(egui::Layout::bottom_up(egui::Align::RIGHT), |ui| {
                ui.add_space(5.0);
                ui.horizontal(|ui| {
                    ui.colored_label(egui::Color32::DARK_GRAY, format!("v{}", env!("CARGO_PKG_VERSION")));
                    if let Some(ver) = &self.latest_version {
                        if ver.trim_start_matches('v') != env!("CARGO_PKG_VERSION") { ui.colored_label(egui::Color32::GOLD, format!("Update available: {}", ver)); }
                    }
                });
            });
        });
    }
}
