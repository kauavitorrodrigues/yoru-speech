#!/bin/bash
# Yoru Speech — install script
# Arch Linux only. Builds the daemon, installs it as a systemd user
# service, and sets up push-to-talk dictation. Safe to run repeatedly.

set -euo pipefail

YORU_SPEECH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$YORU_SPEECH_DIR/build"
BIN_DIR="$HOME/.local/bin"
SYSTEMD_USER_DIR="$HOME/.config/systemd/user"
MODELS_DIR="$HOME/.local/share/yoru-speech/models"
AUR_MODEL_PKG="whisper.cpp-model-large-v3-turbo-q5_0"
AUR_MODEL_FILE="/usr/share/whisper.cpp-model-large-v3-turbo-q5_0/ggml-large-v3-turbo-q5_0.bin"

# ── colors ────────────────────────────────────────────────────────────────────
GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✔${NC}  $*"; }
warn() { echo -e "${YELLOW}  !${NC}  $*"; }
err()  { echo -e "${RED}  ✘${NC}  $*"; exit 1; }
step() { echo -e "\n${GREEN}▶${NC}  $*"; }

# ── arch check ────────────────────────────────────────────────────────────────
check_arch() {
    if ! command -v pacman &>/dev/null; then
        err "Yoru Speech currently only supports Arch Linux."
    fi
}

# ── install dependencies ──────────────────────────────────────────────────────
install_deps() {
    step "Installing dependencies"

    local pacman_pkgs=(
        base-devel cmake pkgconf
        whisper-cpp-vulkan miniaudio tomlplusplus nlohmann-json
        wl-clipboard
    )
    local to_install=()
    for pkg in "${pacman_pkgs[@]}"; do
        if pacman -Q "$pkg" &>/dev/null; then
            ok "$pkg"
        else
            to_install+=("$pkg")
        fi
    done
    if (( ${#to_install[@]} )); then
        step "Installing via pacman: ${to_install[*]}"
        sudo pacman -S --needed --noconfirm "${to_install[@]}"
    fi
}

# ── recognition model (AUR, optional) ─────────────────────────────────────────
install_model() {
    step "Installing a starter recognition model"

    if [[ -f "$AUR_MODEL_FILE" ]]; then
        ok "$AUR_MODEL_PKG"
    elif command -v yay &>/dev/null; then
        yay -S --needed --noconfirm "$AUR_MODEL_PKG"
        ok "$AUR_MODEL_PKG"
    else
        warn "yay not found — skipping the starter model ($AUR_MODEL_PKG, from the AUR)."
        warn "Transcription will fail until a ggml model is placed in $MODELS_DIR"
        return
    fi

    mkdir -p "$MODELS_DIR"
    local link="$MODELS_DIR/$(basename "$AUR_MODEL_FILE")"
    if [[ -L "$link" && "$(readlink -f "$link")" == "$AUR_MODEL_FILE" ]]; then
        ok "model already linked → $link"
    else
        ln -sf "$AUR_MODEL_FILE" "$link"
        ok "Linked $AUR_MODEL_FILE → $link"
    fi
}

# ── build ─────────────────────────────────────────────────────────────────────
build() {
    step "Building yoru-speech (Release, no tests)"
    cmake -S "$YORU_SPEECH_DIR" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release -DYORU_BUILD_TESTS=OFF
    cmake --build "$BUILD_DIR" --target yoru-speech -j"$(nproc)"
    ok "Built $BUILD_DIR/yoru-speech"
}

# ── install binary + dictate helper ──────────────────────────────────────────
install_binaries() {
    step "Installing binaries to $BIN_DIR"
    mkdir -p "$BIN_DIR"

    local was_running=0
    systemctl --user is-active --quiet yoru-speech 2>/dev/null && was_running=1

    # A currently-running daemon has this file open; overwriting it in
    # place would fail with "Text file busy". Copying to a temp file and
    # renaming it into place swaps the directory entry atomically instead
    # — the running process keeps its already-open (old) inode until it's
    # restarted below, and the new binary is in place for that restart.
    cp "$BUILD_DIR/yoru-speech" "$BIN_DIR/yoru-speech.new"
    mv -f "$BIN_DIR/yoru-speech.new" "$BIN_DIR/yoru-speech"
    ok "yoru-speech → $BIN_DIR/yoru-speech"

    cp "$YORU_SPEECH_DIR/packaging/hyprland/yoru-dictate" "$BIN_DIR/yoru-dictate"
    chmod +x "$BIN_DIR/yoru-dictate"
    ok "yoru-dictate → $BIN_DIR/yoru-dictate"

    if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
        warn "$BIN_DIR is not on your PATH — add it in your shell's rc file."
    fi

    # Remember whether the service was running before we overwrote its
    # binary, so setup_service can restart it with the new build.
    echo "$was_running" > "$BUILD_DIR/.was_running"
}

# ── systemd user service ──────────────────────────────────────────────────────
setup_service() {
    step "Installing systemd user service"
    mkdir -p "$SYSTEMD_USER_DIR"
    cp "$YORU_SPEECH_DIR/packaging/systemd/yoru-speech.service" "$SYSTEMD_USER_DIR/"
    systemctl --user daemon-reload
    systemctl --user enable yoru-speech >/dev/null

    local was_running=0
    [[ -f "$BUILD_DIR/.was_running" ]] && was_running="$(cat "$BUILD_DIR/.was_running")"
    rm -f "$BUILD_DIR/.was_running"

    if [[ "$was_running" == "1" ]]; then
        systemctl --user restart yoru-speech
        ok "Restarted yoru-speech (new binary picked up)"
    else
        systemctl --user start yoru-speech
        ok "Started yoru-speech"
    fi
}

# ── run ───────────────────────────────────────────────────────────────────────
echo -e "\n${GREEN}Yoru Speech installer${NC}"
echo "────────────────────────────────────────"

check_arch
install_deps
install_model
build
install_binaries
setup_service

echo -e "\n${GREEN}────────────────────────────────────────${NC}"
echo -e "${GREEN}  Done!${NC}  Yoru Speech is running as a systemd user service.\n"
echo -e "  Check it any time with:\n"
echo -e "    systemctl --user status yoru-speech\n"
echo -e "  For dictation in Hyprland, add to hyprland.conf:\n"
echo -e "    bind = \$mainMod, D, exec, yoru-dictate toggle\n"
echo -e "  Then: hyprctl reload"
echo -e "  See $YORU_SPEECH_DIR/packaging/hyprland/README.md for details,"
echo -e "  including the push-to-talk (hold/release) alternative.\n"
