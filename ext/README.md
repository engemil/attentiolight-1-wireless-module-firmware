# External Dependencies (`ext/`)

This directory holds third-party dependencies pulled in as git submodules.

## Current submodules

| Submodule | Path | Pinned branch / tag | Purpose |
|-----------|------|---------------------|---------|
| ESP-IDF   | `ext/esp-idf` | `release/v5.3` | Espressif IoT Development Framework — toolchain, build system, BLE/WiFi stack, drivers |

## First-time setup

After cloning the repo:

```bash
git submodule update --init --recursive
./ext/esp-idf/install.sh esp32c3
. ./ext/esp-idf/export.sh   # sources IDF_PATH and toolchain into the current shell
```

In the devcontainer, `postCreateCommand` in `.devcontainer/devcontainer.json` runs the first two steps automatically. The `export.sh` step is sourced from `~/.bashrc` on every new shell (see `.devcontainer/Dockerfile`).

## Bumping the ESP-IDF version

```bash
cd ext/esp-idf
git fetch origin
git checkout release/v5.4              # or another release branch / tag
cd ../..
git add ext/esp-idf
git commit -m "Bump ESP-IDF to v5.4"
./ext/esp-idf/install.sh esp32c3       # re-install matching tools
```

Always re-run `install.sh` after a version bump so the tool versions in `~/.espressif/` match the new ESP-IDF source.

## Adding the submodule (one-time, if not yet present)

If the submodule has not been added yet:

```bash
git submodule add -b release/v5.3 https://github.com/espressif/esp-idf.git ext/esp-idf
git commit -m "Add ESP-IDF as submodule (release/v5.3)"
```
