# AttentioLight-1 Wireless Module Firmware

This is the source code (firmware) for the **Wireless Module** (ESP32-C3 WROOM) soldered onto the **AttentioLight-1 MainBoard-1** (`al1mb1`) PCB. It bridges the STM32C071RB host firmware ([`attentiolight-1-firmware`](https://github.com/engemil/attentiolight-1-firmware)) to wireless capabilities (BLE and WiFi).

## Table of Contents

- [Dependencies](#dependencies)
- [Setup Repository](#setup-repository)
- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Additional Sources](#additional-sources)
- [License](#license)


## Dependencies

The recommended (and supported) development environment is the provided Devcontainer. It installs ESP-IDF + toolchain + serial / USB tools inside an Ubuntu 24.04 container so the host only needs Docker + VS Code.

### Required tools (host)

- **Docker Engine** (or Docker Desktop)
- **VS Code** with the **Dev Containers** extension (`ms-vscode-remote.remote-containers`)
- **git** (with submodule support)

### Inside the container (auto-installed)

- ESP-IDF (release/v5.3, pulled in as a submodule at `ext/esp-idf` and installed for target `esp32c3` on first start)
- `riscv32-esp-elf-gcc` / `gdb`, `openocd-esp32`, `esptool.py` (via ESP-IDF tools)
- `cmake`, `ninja`, `ccache`, `python3`
- `minicom`, `dfu-util`, `usbutils`

### Host udev rules (one-time)

To flash / monitor the ESP32 from the devcontainer (which bind-mounts `/dev`), grant non-root access on the host:

```bash
sudo ./scripts/system/udev_rules_esp32.sh
sudo ./scripts/system/dialout_group.sh
# Log out and back in to apply group changes.
```


## Setup Repository

```bash
git clone https://github.com/engemil/attentiolight-1-wireless-module-firmware.git
cd attentiolight-1-wireless-module-firmware
git submodule update --init --recursive
```

Then open the folder in VS Code and choose **"Reopen in Container"**. The container's `postCreateCommand` runs `./ext/esp-idf/install.sh esp32c3` on first start (a few minutes the first time).

The ESP-IDF environment is auto-sourced in every new bash shell inside the container. If you need to source it manually:

```bash
get_idf            # alias for: . /workspace/ext/esp-idf/export.sh
```


## Quick Start

From inside the devcontainer:

```bash
cd fw_al1_wmod
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Or via VS Code: **Ctrl+Shift+P → Tasks: Run Task →** `rebuild, flash, and monitor`.


## Project Structure

```
.
├── fw_al1_wmod/                # Application firmware
│   ├── main/                   # app_main(), component glue
│   │   ├── CMakeLists.txt
│   │   └── main.c
│   ├── components/             # al1_link, al1_ble, al1_session_map
│   ├── CMakeLists.txt
│   ├── partitions.csv
│   ├── sdkconfig.defaults
│   └── README.md
├── ext/
│   ├── esp-idf/                # Submodule, pinned to release/v5.3
│   └── README.md
├── scripts/
│   └── system/
│       ├── udev_rules_esp32.sh # Host udev rules
│       └── dialout_group.sh    # Add current user to dialout group
├── .devcontainer/
│   ├── Dockerfile              # Ubuntu 24.04 + ESP-IDF deps
│   ├── devcontainer.json       # postCreateCommand runs install.sh
│   └── docker-compose.yml      # Privileged, /dev bind, GDB ports exposed
├── .vscode/
│   ├── settings.json           # Parameterized (settings.project.*, settings.idf.*)
│   ├── tasks.json              # setup / build / flash / monitor + composites
│   └── launch.json             # Cortex-Debug → openocd-esp32
├── .gitignore
├── .gitmodules
├── CHANGELOG.md
├── LICENSE
└── README.md
```


## Additional Sources

- ESP-IDF Programming Guide (v5.3, ESP32-C3): https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32c3/index.html
- NimBLE host docs: https://mynewt.apache.org/latest/network/index.html
- openocd-esp32: https://github.com/espressif/openocd-esp32


## License

MIT License — see [`LICENSE`](LICENSE) for details.

Portions of this project incorporate code from:
- **ESP-IDF** (Apache License 2.0)
- **NimBLE** (Apache License 2.0)

For submodule licenses, see individual repository LICENSE files.
