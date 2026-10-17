#!/usr/bin/env bash
# ==============================================================================
# minicut Uninstaller Script
# Uninstalls ONLY minicut core files. Does NOT touch any system dependencies.
# ==============================================================================

set -e

echo "=========================================="
echo "Uninstalling minicut core files..."
echo "=========================================="

REMOVED_ANY=false

# 1. Remove User-level installations (~/.local)
if [ -f "$HOME/.local/bin/minicut" ]; then
    echo "Removing $HOME/.local/bin/minicut..."
    rm -f "$HOME/.local/bin/minicut"
    REMOVED_ANY=true
fi

if [ -L "./minicut" ] || [ -f "./minicut" ]; then
    rm -f "./minicut" 2>/dev/null || true
fi

for dname in "minicut.desktop" "org.minicut.desktop"; do
    if [ -f "$HOME/.local/share/applications/$dname" ]; then
        echo "Removing $HOME/.local/share/applications/$dname..."
        rm -f "$HOME/.local/share/applications/$dname"
        REMOVED_ANY=true
    fi
done

if [ -f "$HOME/.local/bin/minicut.desktop" ]; then
    rm -f "$HOME/.local/bin/minicut.desktop"
fi

if [ -f "/usr/share/applications/org.minicut.desktop" ]; then
    echo "Masking system desktop launcher /usr/share/applications/org.minicut.desktop..."
    cat << 'EOF' > "$HOME/.local/share/applications/org.minicut.desktop"
[Desktop Entry]
Type=Application
Name=minicut
NoDisplay=true
EOF
fi

for size in 256x256 128x128 64x64 48x48 32x32; do
    if [ -f "$HOME/.local/share/icons/hicolor/$size/apps/minicut.png" ]; then
        echo "Removing $HOME/.local/share/icons/hicolor/$size/apps/minicut.png..."
        rm -f "$HOME/.local/share/icons/hicolor/$size/apps/minicut.png"
        REMOVED_ANY=true
    fi
done

if [ -f "$HOME/.local/share/pixmaps/minicut.png" ]; then
    echo "Removing $HOME/.local/share/pixmaps/minicut.png..."
    rm -f "$HOME/.local/share/pixmaps/minicut.png"
    REMOVED_ANY=true
fi

if [ -f "$HOME/.local/share/mime/packages/minicut.xml" ]; then
    echo "Removing $HOME/.local/share/mime/packages/minicut.xml..."
    rm -f "$HOME/.local/share/mime/packages/minicut.xml"
    REMOVED_ANY=true
fi

if [ -d "$HOME/.local/share/minicut" ]; then
    echo "Removing $HOME/.local/share/minicut..."
    rm -rf "$HOME/.local/share/minicut"
    REMOVED_ANY=true
fi

# Refresh user databases
if command -v update-mime-database > /dev/null 2>&1; then
    update-mime-database "$HOME/.local/share/mime" 2>/dev/null || true
fi

if command -v update-desktop-database > /dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
fi

if command -v gtk-update-icon-cache > /dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
fi

# 2. Check and remove system-level installations (/usr/local) if present & writable
if [ -w "/usr/local/bin" ] && [ -f "/usr/local/bin/minicut" ]; then
    echo "Removing /usr/local/bin/minicut..."
    rm -f "/usr/local/bin/minicut"
    REMOVED_ANY=true
fi

if [ -w "/usr/local/share/applications" ] && [ -f "/usr/local/share/applications/minicut.desktop" ]; then
    echo "Removing /usr/local/share/applications/minicut.desktop..."
    rm -f "/usr/local/share/applications/minicut.desktop"
    REMOVED_ANY=true
fi

for size in 256x256 128x128 64x64 48x48 32x32; do
    if [ -w "/usr/local/share/icons/hicolor/$size/apps" ] && [ -f "/usr/local/share/icons/hicolor/$size/apps/minicut.png" ]; then
        echo "Removing /usr/local/share/icons/hicolor/$size/apps/minicut.png..."
        rm -f "/usr/local/share/icons/hicolor/$size/apps/minicut.png"
        REMOVED_ANY=true
    fi
done

if [ -w "/usr/local/share/pixmaps" ] && [ -f "/usr/local/share/pixmaps/minicut.png" ]; then
    echo "Removing /usr/local/share/pixmaps/minicut.png..."
    rm -f "/usr/local/share/pixmaps/minicut.png"
    REMOVED_ANY=true
fi

if [ -w "/usr/local/share/mime/packages" ] && [ -f "/usr/local/share/mime/packages/minicut.xml" ]; then
    echo "Removing /usr/local/share/mime/packages/minicut.xml..."
    rm -f "/usr/local/share/mime/packages/minicut.xml"
    REMOVED_ANY=true
fi

if [ -w "/usr/local/share" ] && [ -d "/usr/local/share/minicut" ]; then
    echo "Removing /usr/local/share/minicut..."
    rm -rf "/usr/local/share/minicut"
    REMOVED_ANY=true
fi

# Refresh KDE Plasma sycoca cache
if command -v kbuildsycoca6 > /dev/null 2>&1; then
    kbuildsycoca6 --noincremental 2>/dev/null || true
elif command -v kbuildsycoca5 > /dev/null 2>&1; then
    kbuildsycoca5 --noincremental 2>/dev/null || true
fi

echo "=========================================="
if [ "$REMOVED_ANY" = true ]; then
    echo "minicut core files have been cleanly uninstalled."
else
    echo "No installed minicut core files found."
fi
echo "System dependencies (FFmpeg, SDL2, GLEW, etc.) remain untouched."
echo "=========================================="
