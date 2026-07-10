#!/bin/bash
set -e

# Target paths
INSTALL_DIR="$HOME/.local/share/flockwatch-companion"
BIN_DIR="$HOME/.local/bin"
DESKTOP_DIR="$HOME/.local/share/applications"

echo "=== FlockWatch Companion App Installer ==="

# 1. Verification
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 is not installed. Please install it first." >&2
    exit 1
fi

# 2. Create directories
mkdir -p "$INSTALL_DIR"
mkdir -p "$BIN_DIR"
mkdir -p "$DESKTOP_DIR"

# Get directory containing this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 3. Copy application files
echo "Copying application files..."
cp "$SCRIPT_DIR/app.py" "$INSTALL_DIR/"
cp "$SCRIPT_DIR/requirements.txt" "$INSTALL_DIR/"

# 4. Install python dependencies
echo "Installing Python dependencies..."
python3 -m pip install -r "$INSTALL_DIR/requirements.txt"

# 5. Create wrapper launcher script
echo "Creating wrapper script in $BIN_DIR/flockwatch-companion..."
cat << 'EOF' > "$BIN_DIR/flockwatch-companion"
#!/bin/bash
exec python3 "$HOME/.local/share/flockwatch-companion/app.py" "$@"
EOF
chmod +x "$BIN_DIR/flockwatch-companion"

# 6. Create Desktop Entry
echo "Creating desktop shortcut..."
cat << EOF > "$DESKTOP_DIR/flockwatch-companion.desktop"
[Desktop Entry]
Type=Application
Name=FlockWatch Companion
Comment=BLE log sync and browser for FlockWatch RF sniffer
Exec=$BIN_DIR/flockwatch-companion
Icon=network-wireless
Terminal=false
Categories=Utility;Network;
EOF
chmod +x "$DESKTOP_DIR/flockwatch-companion.desktop"

# Refresh desktop database if command exists
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database "$DESKTOP_DIR" || true
fi

echo "=== Installation Successful! ==="
echo "You can now launch the app by searching for 'FlockWatch Companion' in your desktop menu,"
echo "or by running the command: flockwatch-companion"
echo "Make sure '$BIN_DIR' is in your PATH."
