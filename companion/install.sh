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

if ! python3 -c "import tkinter" &> /dev/null; then
    echo "Error: python3-tk (Tkinter) is not installed." >&2
    echo "Please install it using your package manager." >&2
    echo "On Debian/Ubuntu: sudo apt install python3-tk" >&2
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

# 4. Set up virtual environment and install python dependencies
echo "Setting up virtual environment in $INSTALL_DIR/venv..."
python3 -m venv "$INSTALL_DIR/venv"
"$INSTALL_DIR/venv/bin/pip" install -r "$INSTALL_DIR/requirements.txt"

# 5. Create wrapper launcher script
echo "Creating wrapper script in $BIN_DIR/flockwatch-companion..."
cat << 'EOF' > "$BIN_DIR/flockwatch-companion"
#!/bin/bash
# Check if tkinter is available
if ! "$HOME/.local/share/flockwatch-companion/venv/bin/python" -c "import tkinter" &> /dev/null; then
    MSG="Error: python3-tk (Tkinter) is missing. This package is required to run the FlockWatch Companion GUI.\n\nPlease install it using your package manager:\nOn Debian/Ubuntu: sudo apt install python3-tk"
    if command -v zenity &> /dev/null; then
        zenity --error --title="FlockWatch Companion" --text="$MSG" --width=400
    elif command -v kdialog &> /dev/null; then
        kdialog --error "$MSG" --title "FlockWatch Companion"
    elif command -v xmessage &> /dev/null; then
        echo -e "$MSG" | xmessage -file - -buttons OK
    else
        echo -e "$MSG" >&2
    fi
    exit 1
fi
exec "$HOME/.local/share/flockwatch-companion/venv/bin/python" "$HOME/.local/share/flockwatch-companion/app.py" "$@"
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
