# Changelog

All notable changes to the **AttentioLight-1 Wireless Module Firmware** project will be documented in this file.

**Version Format:** MAJOR.MINOR.PATCH
- **MAJOR:** Incompatible API/protocol changes
- **MINOR:** New features (backward compatible)
- **PATCH:** Bug fixes (backward compatible)

[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Development] (2026-06-03)

BLE smoke test. Added a host-side test that drives the BLE control path end to end (CLAIM → SET_RGB → EVT_BUTTON → RELEASE) by writing CRC-correct Attentio Protocol frames to the GATT byte pipe, so the wireless path can be verified against real hardware. Built to stay useful where no BLE radio is reachable (e.g. the devcontainer): offline modes generate and self-check the frame bytes; the live mode needs a BLE adapter.

Added

- `scripts/test/ble_smoke.py`, `bleak`-based BLE smoke test. Reuses the firmware's wire definitions: the verbatim CRC-8/CCITT table from `attentio_protocol.c`, an `ApReassembler` port of `al1_ble/ap_reasm.c` (notifications fragment at MTU-3, so one AP frame can span several), and the GATT UUIDs / command IDs. Three modes: `--selftest` (offline CRC/frame/reassembly checks, 18 checks, exits non-zero on failure), `--print-frames` (offline; emits the exact frame hex to paste into nRF Connect), and the default live run (scan/connect, CLAIM with Just-Works pairing on first write, SET_RGB cycle, button-event wait, RELEASE, and a bond-reuse reconnect).
- `scripts/test/requirements.txt`, the `bleak` dependency for the live run only (offline modes are pure stdlib).

Changed

- `README.md`, added a **BLE smoke test (on hardware)** subsection to **Tests** (offline, laptop-`bleak`, and phone/nRF Connect procedures) and listed `scripts/test/` in the project structure.

---

## [Development] (2026-06-02)

BLE GATT bridge. Brought up the ESP32-C3 as a NimBLE peripheral exposing a custom Attentio BLE Service and bridged it to the STM32 over the `al1_link` `AP_CTRL` channel, so a phone/host can drive the device with the same Attentio Protocol used over USB. Single active BLE session (one controller at a time, matching the STM32 MICB) with per-connection command gating.

Added

- `fw_al1_wmod/components/al1_ble/`, NimBLE peripheral + Attentio Protocol bridge:
- `al1_ble.c`, NimBLE host bring-up (NVS init; GAP/SM with **Just-Works pairing + LE Secure Connections + bonding persisted in NVS**), advertises as `AttentioLight-1`, active-connection registry, and per-connection MTU-fragmented notifications (`al1_ble_notify_conn` / `al1_ble_notify_all`).
- `al1_ble_gatt.c`, custom Attentio GATT service (UUIDs `1209EEA1-0001/0002/0003-…`): a TX write characteristic (encryption-required) and an RX notify characteristic.
- `ap_reasm.{c,h}`, pure host-testable length-only AP frame reassembler (recovers whole AP frames from BLE writes by the AP `LEN` field; CRC + semantics stay with the STM32).
- `al1_ble_ap_bridge.c`, couples BLE ⇄ `al1_link`: reassembles client writes and forwards them on `AP_CTRL`, and routes inbound responses/events back to the owning connection. **Single active session**, `CLAIM` binds a connection (caches the 2-byte session-id from `OK`), non-controlling connections are gated locally (`ap_cmd_requires_claim` → `AP_ERR_NOT_CONTROLLER`), and a controller disconnect synthesizes a `RELEASE` so the MICB session does not leak.
- `test/host_test_ap_reasm.c`, host unit test for the reassembler (24 checks).
- `fw_al1_wmod/components/attentio_protocol/`, the shared Attentio Protocol wire core (`attentio_protocol.{c,h}`) copied **verbatim** from the STM32 repo (packet format, CRC-8, command IDs, `ap_build_*`, `ap_cmd_requires_claim`) so both ends share one definition; `README.md` documents the verbatim-copy invariant.

Changed

- `fw_al1_wmod/main/main.c`, brings up BLE after the link and wires the AP bridge; routes inbound `AP_CTRL` frames to it.
- `fw_al1_wmod/main/CMakeLists.txt`, `main` now requires `al1_ble`.
- `README.md`, added a **Tests** section (offline host tests for `al1_link` and `al1_ble`), an `idf.py fullclean` line in Quick Start, and an index of the repo's other README files.

---

## [Development] (2026-06-01)

STM32↔ESP32 UART link. Implemented the `al1_link` transport and verified a bidirectional round-trip against the STM32 host on real hardware, each side decodes the other's 1 Hz `tick=N` heartbeat with no CRC errors after sync.

Added

- `fw_al1_wmod/components/al1_link/`, UART link transport. Frame format `[SYNC 0xA5][VER][CHANNEL][SEQ][LEN_HI][LEN_LO][PAYLOAD][CRC16_HI][CRC16_LO]`, CRC-16/CCITT-FALSE over `VER..PAYLOAD`, per-channel sequence numbers, channel mux (`AP_CTRL` / `LOG` / `EVT` / `BULK` / `KEEPALIVE`). Split into a pure, host-testable core (`al1_frame.c` + `crc16_ccitt.c`, no IDF deps) and an ESP-IDF runtime (`al1_link.c`: UART0 driver install + RX/TX FreeRTOS tasks). The pure core is identical with the STM32 side.
- `fw_al1_wmod/components/al1_link/test/host_test.c`, host unit test (CRC known-answers, frame build/parse round-trip, SEQ wrap, corruption/resync, oversized-LEN).
- `fw_al1_wmod/main/main.c`, link bring-up, internal-UART-loopback boot self-test (`uart_set_loop_back`), a 1 Hz `LOG` heartbeat, and console diagnostics (`TX LOG[...]` per send + periodic `stats: tx/rx/crc/resync`).

Changed

- `fw_al1_wmod/sdkconfig.defaults`, console routed to the built-in USB-Serial-JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). The link uses UART0, whose pads are **GPIO21 (U0TXD) / GPIO20 (U0RXD)**. Do not allow the console to drive them.
- `fw_al1_wmod/main/CMakeLists.txt`, `main` now requires the `al1_link` component.

---

## [Development] (2026-05-30)

Verified the hello-world firmware end-to-end on real ESP32-C3 hardware: `idf.py build` → flash → boot banner (`Hello from AttentioLight-1 Wireless Module Firmware`) + chip info + 1 Hz heartbeat, plus OpenOCD/JTAG attach over the built-in USB-Serial-JTAG (gdb server on `:3333`, target halt/resume). The module enumerates only after the STM32 host asserts its enable line.

Changed

- WiFi made explicit: the WiFi stack stays compiled in (`CONFIG_ESP_WIFI_ENABLED=y`) but the radio is held off at runtime, the firmware never calls `esp_wifi_init()` / `esp_wifi_start()`. Documented in `fw_al1_wmod/sdkconfig.defaults` and `fw_al1_wmod/main/main.c`. To be used for future implementation.
- `.vscode/tasks.json`: `flash` / `monitor` / `Serial (minicom)` tasks no longer hardcode the serial port, they prompt with a live picker (`${input:serialPort}`, used with `augustocdias.tasks-shell-input` VS Code extension), so the port is chosen at run time. The ESP32-C3 USB-Serial-JTAG re-enumerates on every reset, so its `ttyACMx` number is not stable.
- `.vscode/settings.json`: `settings.serial.port` fallback default changed to the stable `/dev/serial/by-id/…` symlink for the ESP32-C3 USB-JTAG.
- `fw_al1_wmod/main/main.c` and `fw_al1_wmod/partitions.csv`: comments clarified and delay added.

Added

- `.vscode/scripts/list_serial_ports.sh`: enumerates USB serial ports (`lsusb` + sysfs) and emits `label|port-path` lines for the VS Code task picker, prefers the stable `/dev/serial/by-id/` path and lists the Espressif (303a) device first.
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
