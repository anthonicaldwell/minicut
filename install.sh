#!/usr/bin/env bash
# ==============================================================================
# minicut Installer Script for Linux / Kubuntu KDE Plasma
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

INSTALL_MODE="user"

for arg in "$@"; do
    case "$arg" in
        --system)
            INSTALL_MODE="system"
            ;;
        --user)
            INSTALL_MODE="user"
            ;;
        --uninstall)
            "$SCRIPT_DIR/uninstall.sh"
            exit 0
            ;;
        -h|--help)
            echo "minicut installer"
            echo "Usage: ./install.sh [OPTION]"
            echo "Options:"
            echo "  --user       Install for current user into ~/.local (default)"
            echo "  --system     Install system-wide into /usr/local (requires root/sudo)"
            echo "  --uninstall  Remove installed minicut files"
            echo "  --help       Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Use ./install.sh --help for available options."
            exit 1
            ;;
    esac
done

if [ "$INSTALL_MODE" = "system" ]; then
    BIN_DIR="/usr/local/bin"
    APP_DIR="/usr/local/share/applications"
    ICON_DIR="/usr/local/share/icons/hicolor/256x256/apps"
    MIME_DIR="/usr/local/share/mime/packages"
    MIME_BASE_DIR="/usr/local/share/mime"
    ICON_BASE_DIR="/usr/local/share/icons/hicolor"
    DATA_DIR="/usr/local/share/minicut"
else
    BIN_DIR="$HOME/.local/bin"
    APP_DIR="$HOME/.local/share/applications"
    ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
    MIME_DIR="$HOME/.local/share/mime/packages"
    MIME_BASE_DIR="$HOME/.local/share/mime"
    ICON_BASE_DIR="$HOME/.local/share/icons/hicolor"
    DATA_DIR="$HOME/.local/share/minicut"
fi

echo "=========================================="
echo "Installing minicut ($INSTALL_MODE mode)..."
echo "=========================================="

# Build if needed
if [ ! -f "build/minicut" ]; then
    echo "Building minicut..."
    cmake -B build -S .
    cmake --build build -j"$(nproc)"
fi

# Ensure directories exist
mkdir -p "$BIN_DIR"
mkdir -p "$APP_DIR"
mkdir -p "$ICON_DIR"
mkdir -p "$MIME_DIR"

# Install binary
echo "Installing binary to $BIN_DIR/minicut..."
cp -f "build/minicut" "$BIN_DIR/minicut"
chmod +x "$BIN_DIR/minicut"

# Install icon
if [ -f "minicut.png" ]; then
    echo "Installing icon to $ICON_DIR/minicut.png..."
    cp -f "minicut.png" "$ICON_DIR/minicut.png"
fi

# Install desktop file with exact binary path for flawless launcher & task manager association
if [ -f "minicut.desktop" ]; then
    echo "Installing desktop launcher to $APP_DIR/minicut.desktop..."
    cat << EOF > "$APP_DIR/minicut.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=minicut
GenericName=Video Editor
Comment=Lightweight Multi-Track Desktop Video Editor
Exec=$BIN_DIR/minicut %F
Icon=$ICON_DIR/minicut.png
Terminal=false
Categories=AudioVideo;Video;AudioVideoEditing;Qt;KDE;
MimeType=application/x-minicut;video/mp4;video/x-matroska;video/quicktime;
Keywords=video;editor;cut;clip;ffmpeg;render;multitrack;
StartupWMClass=minicut
StartupNotify=true
EOF
    chmod +x "$APP_DIR/minicut.desktop"
fi

# Install MIME definition
if [ -f "minicut.xml" ]; then
    echo "Installing MIME package to $MIME_DIR/minicut.xml..."
    cp -f "minicut.xml" "$MIME_DIR/minicut.xml"
fi

# Install locales to ~/.config/minicut/locales and $DATA_DIR/locales
if [ -d "locales" ]; then
    echo "Installing locales to $HOME/.config/minicut/locales/ and $DATA_DIR/locales/..."
    mkdir -p "$HOME/.config/minicut/locales"
    cp -rf locales/* "$HOME/.config/minicut/locales/"
    mkdir -p "$DATA_DIR/locales"
    cp -rf locales/* "$DATA_DIR/locales/"
fi

# Install default scripts, autorun scripts, and icons
if [ -d "scripts" ]; then
    echo "Installing scripts to $HOME/.config/minicut/scripts/ and $DATA_DIR/scripts/..."
    mkdir -p "$HOME/.config/minicut/scripts"
    cp -rf scripts/* "$HOME/.config/minicut/scripts/"
    mkdir -p "$DATA_DIR/scripts"
    cp -rf scripts/* "$DATA_DIR/scripts/"
fi

if [ -d "autorun" ]; then
    echo "Installing autorun scripts to $HOME/.config/minicut/autorun/ and $DATA_DIR/autorun/..."
    mkdir -p "$HOME/.config/minicut/autorun"
    cp -rf autorun/* "$HOME/.config/minicut/autorun/"
    mkdir -p "$DATA_DIR/autorun"
    cp -rf autorun/* "$DATA_DIR/autorun/"
fi

if [ -d "icons" ]; then
    echo "Installing script icons to $HOME/.config/minicut/icons/ and $DATA_DIR/icons/..."
    mkdir -p "$HOME/.config/minicut/icons"
    cp -rf icons/* "$HOME/.config/minicut/icons/"
    mkdir -p "$DATA_DIR/icons"
    cp -rf icons/* "$DATA_DIR/icons/"
fi

# Refresh desktop, MIME, and icon databases
if command -v update-mime-database > /dev/null 2>&1; then
    update-mime-database "$MIME_BASE_DIR" 2>/dev/null || true
fi

if command -v update-desktop-database > /dev/null 2>&1; then
    update-desktop-database "$APP_DIR" 2>/dev/null || true
fi

if command -v xdg-mime > /dev/null 2>&1; then
    xdg-mime default minicut.desktop application/x-minicut 2>/dev/null || true
fi

if command -v gtk-update-icon-cache > /dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$ICON_BASE_DIR" 2>/dev/null || true
fi

# Refresh KDE Plasma Sycoca cache if running on KDE
if command -v kbuildsycoca6 > /dev/null 2>&1; then
    kbuildsycoca6 --noincremental 2>/dev/null || true
elif command -v kbuildsycoca5 > /dev/null 2>&1; then
    kbuildsycoca5 --noincremental 2>/dev/null || true
fi

echo "=========================================="
echo "minicut successfully installed!"
echo "Binary:  $BIN_DIR/minicut"
echo "Desktop: $APP_DIR/minicut.desktop"
echo "Icon:    $ICON_DIR/minicut.png"
echo "MIME:    $MIME_DIR/minicut.xml"
echo "=========================================="
