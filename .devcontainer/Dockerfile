FROM ubuntu:24.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Locales setup (ESP-IDF expects UTF-8)
RUN apt update && apt install -y locales \
    && locale-gen en_US.UTF-8 \
    && update-locale LANG=en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

# ESP-IDF build/install dependencies.
# Source: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/linux-macos-setup.html
RUN apt install -y \
    git wget flex bison gperf \
    python3 python3-pip python3-venv \
    cmake ninja-build ccache \
    libffi-dev libssl-dev \
    dfu-util libusb-1.0-0

# Serial / USB tooling (matches al1mb1 dev experience)
RUN apt install -y minicom usbutils sudo curl

# Clean up apt lists to shrink image
RUN rm -rf /var/lib/apt/lists/*

# Prepare user
ARG USERNAME=engemil

# Remove default ubuntu user if it exists (clears UID/GID conflicts)
RUN userdel -r ubuntu || true
RUN groupadd $USERNAME
RUN useradd -m -s /bin/bash -g $USERNAME $USERNAME
RUN usermod -aG sudo $USERNAME
# Passwordless sudo (typical for dev containers)
RUN echo "engemil ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Run as user from here
USER $USERNAME

# AI agent(s) setup
RUN curl -fsSL https://claude.ai/install.sh | bash
RUN curl -fsSL https://opencode.ai/install | bash
RUN echo 'export PATH="/home/engemil/.local/bin:$PATH"' >> /home/$USERNAME/.bashrc
RUN echo 'alias claude-unchained="claude --dangerously-skip-permissions"' >> /home/$USERNAME/.bashrc

# Pre-create the workspace directory so the bind mount lands cleanly.
# ESP-IDF lives at /workspace/ext/esp-idf (git submodule). It is installed
# on first container start via devcontainer.json's postCreateCommand
# (so the bind mount has been applied first).
RUN sudo mkdir -p /workspace && sudo chown $USERNAME:$USERNAME /workspace
WORKDIR /workspace

# Auto-source ESP-IDF on every interactive shell (no-op until install.sh has run).
RUN echo '[ -f /workspace/ext/esp-idf/export.sh ] && . /workspace/ext/esp-idf/export.sh > /dev/null 2>&1 || true' \
    >> /home/$USERNAME/.bashrc

# get_idf shortcut, matches the esp_idf_devcontainer convention
RUN echo "alias get_idf='. /workspace/ext/esp-idf/export.sh'" >> /home/$USERNAME/.bashrc
