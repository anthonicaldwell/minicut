#!/usr/bin/env bash
# ==============================================================================
# minicut Installer Script for Linux / Kubuntu KDE Plasma
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

INSTALL_MODE="user"
CHECK_DEPS=true
AUTO_YES=false
DEPS_ONLY=false
FORCE_REBUILD=false

for arg in "$@"; do
    case "$arg" in
        --system)
            INSTALL_MODE="system"
            ;;
        --user)
            INSTALL_MODE="user"
            ;;
        --deps-only)
            DEPS_ONLY=true
            ;;
        --no-deps|--skip-deps)
            CHECK_DEPS=false
            ;;
        -y|--yes)
            AUTO_YES=true
            ;;
        --rebuild)
            FORCE_REBUILD=true
            ;;
        --uninstall)
            "$SCRIPT_DIR/uninstall.sh"
            exit 0
            ;;
        -h|--help)
            echo "minicut installer"
            echo "Usage: ./install.sh [OPTIONS]"
            echo "Options:"
            echo "  --user       Install for current user into ~/.local (default)"
            echo "  --system     Install system-wide into /usr/local (requires root/sudo)"
            echo "  --deps-only  Check and install dependencies only, without building or installing"
            echo "  --no-deps    Skip checking and installing dependencies"
            echo "  -y, --yes    Automatically install missing dependencies without prompting"
            echo "  --rebuild    Force re-compiling minicut before installing"
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

# ==============================================================================
# Dependency Checking and Automatic Installation Subsystem
# ==============================================================================
check_and_install_dependencies() {
    echo "=========================================="
    echo "Checking required and recommended dependencies..."
    echo "=========================================="

    local missing_pkgs=()
    local pm=""

    if command -v apt-get >/dev/null 2>&1; then
        pm="apt"

        local apt_required=(
            "build-essential"
            "cmake"
            "pkg-config"
            "libsdl2-dev"
            "libglew-dev"
            "libgl1-mesa-dev"
            "libpng-dev"
        )
        for pkg in "${apt_required[@]}"; do
            if ! dpkg -s "$pkg" 2>/dev/null | grep -q "Status: install ok installed"; then
                missing_pkgs+=("$pkg")
            fi
        done

        # Freetype (libfreetype-dev or libfreetype6-dev)
        if ! dpkg -s libfreetype-dev 2>/dev/null | grep -q "Status: install ok installed" && \
           ! dpkg -s libfreetype6-dev 2>/dev/null | grep -q "Status: install ok installed"; then
            missing_pkgs+=("libfreetype-dev")
        fi

        # Curl development library (libcurl4-openssl-dev or libcurl4-gnutls-dev)
        if ! dpkg -s libcurl4-openssl-dev 2>/dev/null | grep -q "Status: install ok installed" && \
           ! dpkg -s libcurl4-gnutls-dev 2>/dev/null | grep -q "Status: install ok installed"; then
            missing_pkgs+=("libcurl4-openssl-dev")
        fi

        # FFmpeg development libraries
        for pkg in libavcodec-dev libavformat-dev libswscale-dev libswresample-dev libavutil-dev; do
            if ! dpkg -s "$pkg" 2>/dev/null | grep -q "Status: install ok installed"; then
                missing_pkgs+=("$pkg")
            fi
        done

        # Recommended: TTS voice engine
        if ! dpkg -s libespeak-ng-dev 2>/dev/null | grep -q "Status: install ok installed" && \
           ! dpkg -s libespeak-ng1 2>/dev/null | grep -q "Status: install ok installed" && \
           ! dpkg -s espeak-ng 2>/dev/null | grep -q "Status: install ok installed"; then
            missing_pkgs+=("libespeak-ng-dev")
        fi

        # Recommended: Multilingual CJK font support
        if ! dpkg -s fonts-noto-cjk 2>/dev/null | grep -q "Status: install ok installed"; then
            missing_pkgs+=("fonts-noto-cjk")
        fi

        # Recommended: KDE native dialog (kdialog) or fallback (zenity)
        if ! command -v kdialog >/dev/null 2>&1 && ! command -v zenity >/dev/null 2>&1; then
            missing_pkgs+=("kdialog")
        fi

        # Recommended: ffmpeg CLI tools
        if ! command -v ffmpeg >/dev/null 2>&1; then
            missing_pkgs+=("ffmpeg")
        fi

    elif command -v dnf >/dev/null 2>&1; then
        pm="dnf"
        local dnf_pkgs=(
            "gcc-c++"
            "cmake"
            "pkgconf-pkg-config"
            "make"
            "SDL2-devel"
            "glew-devel"
            "mesa-libGL-devel"
            "freetype-devel"
            "libpng-devel"
            "libcurl-devel"
            "espeak-ng-devel"
            "google-noto-sans-cjk-fonts"
            "kdialog"
            "ffmpeg"
        )
        for pkg in "${dnf_pkgs[@]}"; do
            if ! rpm -q "$pkg" >/dev/null 2>&1; then
                missing_pkgs+=("$pkg")
            fi
        done
        if ! rpm -q ffmpeg-devel >/dev/null 2>&1 && ! rpm -q ffmpeg-free-devel >/dev/null 2>&1; then
            missing_pkgs+=("ffmpeg-free-devel")
        fi

    elif command -v pacman >/dev/null 2>&1; then
        pm="pacman"
        local pac_pkgs=(
            "base-devel"
            "cmake"
            "pkgconf"
            "sdl2"
            "glew"
            "mesa"
            "ffmpeg"
            "freetype2"
            "libpng"
            "curl"
            "espeak-ng"
            "noto-fonts-cjk"
            "kdialog"
        )
        for pkg in "${pac_pkgs[@]}"; do
            if ! pacman -Q "$pkg" >/dev/null 2>&1; then
                missing_pkgs+=("$pkg")
            fi
        done

    elif command -v zypper >/dev/null 2>&1; then
        pm="zypper"
        local zyp_pkgs=(
            "gcc-c++"
            "cmake"
            "pkg-config"
            "make"
            "libSDL2-devel"
            "glew-devel"
            "Mesa-libGL-devel"
            "freetype2-devel"
            "libpng16-devel"
            "libcurl-devel"
            "espeak-ng-devel"
            "noto-sans-cjk-fonts"
            "kdialog"
            "ffmpeg"
        )
        for pkg in "${zyp_pkgs[@]}"; do
            if ! rpm -q "$pkg" >/dev/null 2>&1; then
                missing_pkgs+=("$pkg")
            fi
        done
    else
        pm="unknown"
    fi

    if [ ${#missing_pkgs[@]} -eq 0 ]; then
        echo "✓ All required and recommended dependencies are satisfied."
        return 0
    fi

    echo "Notice: ${#missing_pkgs[@]} missing package(s) detected:"
    for pkg in "${missing_pkgs[@]}"; do
        echo "  - $pkg"
    done
    echo ""

    if [ "$pm" = "unknown" ]; then
        echo "Warning: Unsupported package manager. Please install missing packages manually."
        return 0
    fi

    local do_install=false
    if [ "$AUTO_YES" = true ]; then
        do_install=true
    elif [ -t 0 ]; then
        # Interactive terminal prompt
        read -r -p "Would you like the installer to install missing packages now? [Y/n] " response
        case "$response" in
            [nN][oO]|[nN])
                do_install=false
                ;;
            *)
                do_install=true
                ;;
        esac
    else
        echo "Non-interactive shell detected. You can install missing packages by running:"
        if [ "$pm" = "apt" ]; then
            echo "  sudo apt-get update && sudo apt-get install -y ${missing_pkgs[*]}"
        elif [ "$pm" = "dnf" ]; then
            echo "  sudo dnf install -y ${missing_pkgs[*]}"
        elif [ "$pm" = "pacman" ]; then
            echo "  sudo pacman -Sy --noconfirm ${missing_pkgs[*]}"
        elif [ "$pm" = "zypper" ]; then
            echo "  sudo zypper install -y ${missing_pkgs[*]}"
        fi
        echo "Or run './install.sh -y' to automatically install them."
        return 0
    fi

    if [ "$do_install" = true ]; then
        echo "Installing missing dependencies using $pm..."
        local sudo_cmd=""
        if [ "$EUID" -ne 0 ]; then
            if command -v sudo >/dev/null 2>&1; then
                sudo_cmd="sudo"
            else
                echo "Error: Root privileges are required to install packages, but 'sudo' was not found."
                return 1
            fi
        fi

        if [ "$pm" = "apt" ]; then
            $sudo_cmd apt-get update
            $sudo_cmd apt-get install -y "${missing_pkgs[@]}"
        elif [ "$pm" = "dnf" ]; then
            $sudo_cmd dnf install -y "${missing_pkgs[@]}"
        elif [ "$pm" = "pacman" ]; then
            $sudo_cmd pacman -Sy --noconfirm "${missing_pkgs[@]}"
        elif [ "$pm" = "zypper" ]; then
            $sudo_cmd zypper install -y "${missing_pkgs[@]}"
        fi

        echo "✓ Dependencies installed successfully."
    else
        echo "Continuing without installing packages as requested."
    fi
}

if [ "$CHECK_DEPS" = true ]; then
    check_and_install_dependencies
    if [ "$DEPS_ONLY" = true ]; then
        echo "Dependency setup complete (--deps-only)."
        exit 0
    fi
fi

if [ "$INSTALL_MODE" = "system" ]; then
    BIN_DIR="/usr/local/bin"
    APP_DIR="/usr/local/share/applications"
    MIME_DIR="/usr/local/share/mime/packages"
    MIME_BASE_DIR="/usr/local/share/mime"
    ICON_BASE_DIR="/usr/local/share/icons/hicolor"
    PIXMAPS_DIR="/usr/local/share/pixmaps"
    DATA_DIR="/usr/local/share/minicut"
else
    BIN_DIR="$HOME/.local/bin"
    APP_DIR="$HOME/.local/share/applications"
    MIME_DIR="$HOME/.local/share/mime/packages"
    MIME_BASE_DIR="$HOME/.local/share/mime"
    ICON_BASE_DIR="$HOME/.local/share/icons/hicolor"
    PIXMAPS_DIR="$HOME/.local/share/pixmaps"
    DATA_DIR="$HOME/.local/share/minicut"
fi

echo "=========================================="
echo "Installing minicut ($INSTALL_MODE mode)..."
echo "=========================================="

# Determine binary source (pre-built binary package vs compiling from source)
SRC_BIN=""
if [ -f "CMakeLists.txt" ]; then
    if [ ! -f "build/minicut" ] || [ "$FORCE_REBUILD" = true ]; then
        echo "Building minicut..."
        cmake -B build -S .
        cmake --build build -j"$(nproc)"
    fi
    SRC_BIN="build/minicut"
elif [ -f "build/minicut" ]; then
    SRC_BIN="build/minicut"
elif [ -f "minicut" ]; then
    SRC_BIN="minicut"
else
    echo "Error: minicut executable binary not found and no CMakeLists.txt available to compile it!"
    exit 1
fi

# Ensure directories exist
mkdir -p "$BIN_DIR"
mkdir -p "$APP_DIR"
mkdir -p "$ICON_BASE_DIR"
mkdir -p "$PIXMAPS_DIR"
mkdir -p "$MIME_DIR"

# Install binary
echo "Installing binary to $BIN_DIR/minicut..."
cp -f "$SRC_BIN" "$BIN_DIR/minicut"
chmod 755 "$BIN_DIR/minicut"

# Ensure repository desktop file and any Desktop shortcut are marked executable (required by KDE Plasma)
for dtop in "desktop/minicut.desktop" "minicut.desktop" "$HOME/Desktop/minicut.desktop"; do
    if [ -f "$dtop" ]; then
        chmod 755 "$dtop" 2>/dev/null || true
    fi
done

# Install icons across standard resolutions for KDE Plasma / FreeDesktop compliance
DESKTOP_ICON=""
if [ -f "desktop/minicut.png" ]; then
    DESKTOP_ICON="desktop/minicut.png"
elif [ -f "icons/minicut.png" ]; then
    DESKTOP_ICON="icons/minicut.png"
elif [ -f "minicut.png" ]; then
    DESKTOP_ICON="minicut.png"
fi

if [ -n "$DESKTOP_ICON" ]; then
    echo "Installing icons to $ICON_BASE_DIR and $PIXMAPS_DIR from $DESKTOP_ICON..."
    cp -f "$DESKTOP_ICON" "$PIXMAPS_DIR/minicut.png"
    for size in 256x256 128x128 64x64 48x48 32x32; do
        target_dir="$ICON_BASE_DIR/$size/apps"
        mkdir -p "$target_dir"
        if [ "$size" = "256x256" ]; then
            cp -f "$DESKTOP_ICON" "$target_dir/minicut.png"
        elif command -v convert > /dev/null 2>&1; then
            convert "$DESKTOP_ICON" -resize "$size" "$target_dir/minicut.png" 2>/dev/null || cp -f "$DESKTOP_ICON" "$target_dir/minicut.png"
        elif command -v ffmpeg > /dev/null 2>&1; then
            dim="${size%x*}"
            ffmpeg -y -i "$DESKTOP_ICON" -vf "scale=${dim}:${dim}" "$target_dir/minicut.png" >/dev/null 2>&1 || cp -f "$DESKTOP_ICON" "$target_dir/minicut.png"
        else
            cp -f "$DESKTOP_ICON" "$target_dir/minicut.png"
        fi
    done
fi

# Install desktop file with exact binary path for flawless launcher & task manager association
if [ -f "desktop/minicut.desktop" ] || [ -f "minicut.desktop" ]; then
    echo "Installing desktop launcher to $APP_DIR/minicut.desktop..."
    cat << EOF > "$APP_DIR/minicut.desktop"
[Desktop Entry]
Version=1.0
Type=Application
Name=minicut
GenericName=Video Editor
Comment=Lightweight Multi-Track Desktop Video Editor
Exec=$BIN_DIR/minicut %F
Icon=minicut
Terminal=false
Categories=AudioVideo;Video;AudioVideoEditing;Qt;KDE;
MimeType=application/x-minicut;video/mp4;video/x-matroska;video/quicktime;video/ogg;audio/ogg;
Keywords=video;editor;cut;clip;ffmpeg;render;multitrack;
StartupWMClass=minicut
StartupNotify=true
EOF
    chmod +x "$APP_DIR/minicut.desktop"
fi

# Install MIME definition
MIME_FILE=""
if [ -f "desktop/minicut.xml" ]; then
    MIME_FILE="desktop/minicut.xml"
elif [ -f "minicut.xml" ]; then
    MIME_FILE="minicut.xml"
fi
if [ -n "$MIME_FILE" ]; then
    echo "Installing MIME package to $MIME_DIR/minicut.xml..."
    cp -f "$MIME_FILE" "$MIME_DIR/minicut.xml"
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

if [ -d "docs" ]; then
    echo "Installing documentation to $HOME/.config/minicut/docs/ and $DATA_DIR/docs/..."
    mkdir -p "$HOME/.config/minicut/docs"
    cp -rf docs/* "$HOME/.config/minicut/docs/"
    mkdir -p "$DATA_DIR/docs"
    cp -rf docs/* "$DATA_DIR/docs/"
fi

# Install default ImGui layout if not already customized
if [ -f "desktop/imgui.ini" ] && [ ! -f "$HOME/.config/minicut/imgui.ini" ]; then
    mkdir -p "$HOME/.config/minicut"
    cp -f "desktop/imgui.ini" "$HOME/.config/minicut/imgui.ini" 2>/dev/null || true
fi

# Install README files (root and localized docs)
cp -f README*.md "$HOME/.config/minicut/" 2>/dev/null || true
cp -f docs/README*.md "$HOME/.config/minicut/" 2>/dev/null || true
if [ "$INSTALL_MODE" = "system" ]; then
    mkdir -p "$DATA_DIR"
    cp -f README*.md "$DATA_DIR/" 2>/dev/null || true
    cp -f docs/README*.md "$DATA_DIR/" 2>/dev/null || true
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

# Fix ownership if user accidentally invoked install.sh with sudo in user mode
if [ "$INSTALL_MODE" = "user" ] && [ "$EUID" -eq 0 ] && [ -n "$SUDO_USER" ]; then
    echo "Fixing file ownership for $SUDO_USER..."
    chown -R "$SUDO_USER:$SUDO_USER" "$BIN_DIR/minicut" "$APP_DIR/minicut.desktop" "$HOME/.config/minicut" 2>/dev/null || true
fi

echo "=========================================="
echo "minicut successfully installed!"
echo "Binary:  $BIN_DIR/minicut"
echo "Desktop: $APP_DIR/minicut.desktop"
echo "Icon:    $PIXMAPS_DIR/minicut.png"
echo "MIME:    $MIME_DIR/minicut.xml"
echo "=========================================="

if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    echo ""
    echo "Notice: $BIN_DIR is not currently in your active PATH."
    echo "To launch 'minicut' directly from any terminal, add it to your PATH:"
    echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo "Or launch it via full path or project directory:"
    echo "  $BIN_DIR/minicut"
    echo "  ./minicut"
fi

