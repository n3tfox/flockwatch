#!/bin/bash
set -e

INSTALL_DIR="$HOME/.local/share/flockwatch-companion"
BIN_DIR="$HOME/.local/bin"
DESKTOP_DIR="$HOME/.local/share/applications"

echo "=== FlockWatch Companion App Uninstaller ==="

# Remove files
echo "Removing application files..."
rm -rf "$INSTALL_DIR"

echo "Removing wrapper script..."
rm -f "$BIN_DIR/flockwatch-companion"

echo "Removing desktop shortcut..."
rm -f "$DESKTOP_DIR/flockwatch-companion.desktop"

# Refresh desktop database if command exists
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database "$DESKTOP_DIR" || true
fi

echo "=== Uninstallation Complete! ==="
