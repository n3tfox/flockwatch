#!/usr/bin/env python3
"""
FlockWatch Crash Log Grabber
Sends 'D' over serial to dump the persistent crash log from the ESP32.
"""

import sys
import time
import serial
import serial.tools.list_ports

def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    # List of common driver keywords
    keywords = ["ch340", "cp210", "usb", "ftdi", "m5stick", "silicon labs", "jlink"]
    for port in ports:
        desc = port.description.lower()
        hwid = port.hwid.lower()
        device = port.device.lower()
        if any(kw in desc or kw in hwid or kw in device for kw in keywords):
            return port.device
    if ports:
        # Fallback to the first available port if no keyword matches
        return ports[0].device
    return None

def main():
    port = None
    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        print("Scanning for USB serial ports...")
        port = find_esp32_port()
        if not port:
            print("Error: No serial port found. Please specify the port manually, e.g.:")
            print(f"  python3 {sys.argv[0]} /dev/ttyUSB0")
            sys.exit(1)
    
    print(f"Connecting to {port} at 115200 baud...")
    try:
        # Open port. timeout=2s. We disable DTR/RTS to avoid auto-resetting the ESP32 on connect
        ser = serial.Serial(port, 115200, timeout=2, dsrdtr=False, rtscts=False)
        ser.dtr = False
        ser.rts = False
        
        # Settle connection
        time.sleep(0.5)
        
        # Clear buffers
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        print("Sending debug request symbol 'D'...")
        ser.write(b'D')
        ser.flush()
        
        print("Reading crash log (timeout 5s)...")
        log_lines = []
        started = False
        finished = False
        start_time = time.time()
        
        while time.time() - start_time < 5.0:
            line_bytes = ser.readline()
            if not line_bytes:
                continue
            line = line_bytes.decode('utf-8', errors='ignore').strip()
            
            if "--- BEGIN CRASH LOG ---" in line:
                started = True
                print("\n" + "="*40)
                print("[Capturing Crash Log...]")
                print("="*40)
                continue
            
            if "--- END CRASH LOG ---" in line:
                finished = True
                break
                
            if started:
                log_lines.append(line)
                print(line)
        
        if finished:
            log_content = "\n".join(log_lines)
            filename = "captured_crash_log.txt"
            with open(filename, "w") as f:
                f.write(log_content + "\n")
            print("="*40)
            print(f"Success: Crash log saved to {filename}")
            print("="*40)
        elif started:
            print("\nWarning: Log capture started but did not receive end marker.")
        else:
            print("\nError: Did not receive crash log from device.")
            print("Please ensure the device is powered on, connected, and the firmware is running.")
            
        ser.close()
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
