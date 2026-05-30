# Changelog

All notable changes to the **AttentioLight-1 Wireless Module Firmware** project will be documented in this file.

**Version Format:** MAJOR.MINOR.PATCH
- **MAJOR:** Incompatible API/protocol changes
- **MINOR:** New features (backward compatible)
- **PATCH:** Bug fixes (backward compatible)

[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Development] (2026-05-30)

Verified the hello-world firmware end-to-end on real ESP32-C3 hardware: `idf.py build` → flash → boot banner (`Hello from AttentioLight-1 Wireless Module Firmware`) + chip info + 1 Hz heartbeat, plus OpenOCD/JTAG attach over the built-in USB-Serial-JTAG (gdb server on `:3333`, target halt/resume). The module enumerates only after the STM32 host asserts its enable line.

Changed

- WiFi made explicit: the WiFi stack stays compiled in (`CONFIG_ESP_WIFI_ENABLED=y`) but the radio is held off at runtime — the firmware never calls `esp_wifi_init()` / `esp_wifi_start()`. Documented in `fw_al1_wmod/sdkconfig.defaults` and `fw_al1_wmod/main/main.c`. To be used for future implementation.
- `.vscode/tasks.json`: `flash` / `monitor` / `Serial (minicom)` tasks no longer hardcode the serial port — they prompt with a live picker (`${input:serialPort}`, used with `augustocdias.tasks-shell-input` VS Code extension), so the port is chosen at run time. The ESP32-C3 USB-Serial-JTAG re-enumerates on every reset, so its `ttyACMx` number is not stable.
- `.vscode/settings.json`: `settings.serial.port` fallback default changed to the stable `/dev/serial/by-id/…` symlink for the ESP32-C3 USB-JTAG.
- `fw_al1_wmod/main/main.c` and `fw_al1_wmod/partitions.csv`: comments clarified and delay added.

Added

- `.vscode/scripts/list_serial_ports.sh`: enumerates USB serial ports (`lsusb` + sysfs) and emits `label|port-path` lines for the VS Code task picker — prefers the stable `/dev/serial/by-id/` path and lists the Espressif (303a) device first.
- `.devcontainer/devcontainer.json`: added the `augustocdias.tasks-shell-input` extension that powers the dynamic serial-port picker.

---

## [Development] (2026-05-30)

Changed

- `.devcontainer/Dockerfile` rewritten for ESP-IDF: Ubuntu 24.04, ESP-IDF build deps, `minicom` / `usbutils` / `dfu-util`, auto-source of `ext/esp-idf/export.sh` in `~/.bashrc`.
- `.devcontainer/devcontainer.json` updated with `postCreateCommand` (submodule init + `install.sh esp32c3`) and VS Code extensions.
- `.devcontainer/docker-compose.yml` updated with OpenOCD debug port mappings (3333 / 4444 / 6666).
- `README.md` rewritten to a trimmed first pass: Hardware / Dependencies / Setup / Quick Start / Project Structure / Companion repo / License.

Added

- `ext/esp-idf` added as git submodule (release/v5.3).
- `ext/README.md` added documenting submodule usage and version bump procedure.
- `.gitignore` added covering ESP-IDF build artifacts, sdkconfig, managed components.
- `fw_al1_wmod/` skeleton added: root `CMakeLists.txt`, `main/CMakeLists.txt`, `main/main.c` (hello-world + chip info + heartbeat), `sdkconfig.defaults` (NimBLE + LE Secure Connections, custom partition table, log level), `partitions.csv`, and `fw_al1_wmod/README.md`.
- `.vscode/settings.json` populated with parameterized `settings.project.*`, `settings.idf.*`, `settings.serial.*`, `settings.openocd.*`, Cortex-Debug binary paths, and terminal env injection for `IDF_PATH` / `IDF_TOOLS_PATH`.
- `.vscode/tasks.json` populated with `setup` / `clean` / `fullclean` / `build` / `flash` / `monitor` and composites (`rebuild`, `rebuild and flash`, `rebuild, flash, and monitor`), plus a dedicated-panel `Serial (minicom)` task, all driven by `settings.*` and run through `bash -lc` to inherit the auto-sourced ESP-IDF env.
- `.vscode/launch.json` populated with Cortex-Debug configs (`Attach (no rebuild)`, `Rebuild + Flash + Debug`) using `openocd-esp32` with the ESP32-C3 built-in USB-Serial-JTAG board file.
- `scripts/system/udev_rules_esp32.sh` and `scripts/system/dialout_group.sh` added (mirror al1mb1 host setup pattern, covers Espressif VID 303a / PIDs 1001 + 1002).
- ESP-IDF toolchain installed via `./ext/esp-idf/install.sh esp32c3` (ESP-IDF **v5.3.5-506-g0cf21f6beb**; `riscv32-esp-elf-gcc esp-13.2.0`; `openocd-esp32 v0.12.0-esp32-20260424`; Python venv under `~/.espressif/python_env/idf5.3_py3.12_env`).

---

## [Development] (2026-05-29)

- First commit
- License added
- README added
- .vscode folder added with empty files
- .devcontainer added with basic setup
