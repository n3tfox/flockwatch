# FlockWatch - Native M5StickC Plus 2 Surveillance Detector

FlockWatch is a passive RF detector designed for wearable M5StickC Plus 2 hardware (specifically matching the Watch Kit format). It passively listens to public Wi-Fi management broadcasts and BLE advertisements emitted by surveillance hardware, such as ALPRs (Automated License Plate Readers) and acoustic gunshot detection sensors, to help map public surveillance infrastructure.

This project is built natively in C++ using PlatformIO and comes with a desktop companion client app.

---

## ⚡ Core Features

- **Passive Wi-Fi Sniffing:** Sniffs raw 802.11 management frames (Probe Requests, Beacons, Probe Responses) cycling through channels 1, 6, and 11.
- **BLE Beacon Scanning:** Scans for BLE company manufacturer IDs, device names, and service UUIDs matching known ALPR and Raven acoustic units.
- **Buzzer & LED Indicators:** Plays an audible double-chirp tone (buzzer) and flashes the red LED on match detections.
- **Double-Buffered UI:** Double-buffered rendering via an in-RAM `LGFX_Sprite` canvas, providing a crisp, zero-flicker landscape layout.
- **Power Saver Sleep:** Short press the **Power Button (BtnPWR)** to toggle screen sleep, cutting the backlight and bypassing draw loops to maximize battery life while sniffing remains active.
- **Wireless Log Downloader:** BLE Nordic UART Service (NUS) server streaming logs wirelessly in 200-byte packets to your PC.
- **Companion GUI Client:** A Tkinter python app that connects via Bleak to sync logs, auto-detects paired watches via `bluetoothctl`, displays table records, and links directly to DeFlock portals.

### 🕵️ Heuristics & Matching Signatures

- **Flock Safety ALPR Cameras:**
  - **BLE Scanning:** Captures Bluetooth Low Energy advertisements containing Company ID `0x09C8` (XUNTONG electronic tag chipsets utilized on camera beacons) or broadcasting device names matching keywords `flock` or `fs ext battery`.
  - **Wi-Fi Sniffing:** Sniffs raw 802.11 management frames (Probe Requests, Beacons, and Probe Responses) targeting SSID substrings like `flock`, `flck`, or `test_flck`, and flags OUIs associated with Flock uplink cellular router equipment.
- **Acoustic Gunshot Detection (SoundThinking/ShotSpotter Raven units):**
  - **BLE Scanning:** Matches UUID ranges starting with `00003100` to `00003500`, and legacy ranges `00001809`, `00001819`, `0000180a` corresponding to local Raven service endpoints.

---

## ⌚ Hardware & Pinout (M5StickC Plus 2)

- **Button A (M5 Front Key):** GPIO 37 (Select/Settings)
- **Button B (Side Scroll Key):** GPIO 39 (Scroll down / Reset dashboard stats)
- **Button C / BtnPWR (Left Power Key):** GPIO 35 (Short press: Toggle screen on/off)
- **Buzzer:** GPIO 2 (PWM modulated audio)
- **Red LED:** GPIO 19 (Match flash indicator)
- **Screen:** ST7789v2 135x240 LCD (landscape rotations 1 or 3)

---

## 🚀 Building & Flashing

### Firmware (C++)
To compile and flash the firmware using PlatformIO:
```bash
# Compile project
pio run

# Flash to device
pio run -t upload
```

### Companion App (Python)
1. Install dependencies:
   ```bash
   pip install -r companion/requirements.txt
   ```
2. Run app:
   ```bash
   python companion/app.py
   ```
3. Set the watch to **Start BLE Transfer** under the settings menu.
4. (Linux Auto-detect): The companion app queries your Bluetooth settings automatically via `bluetoothctl`. If you paired the watch to your OS, it will connect immediately. Otherwise, enter the watch's MAC address directly in the Entry field and hit **Sync Logs via BLE**.
