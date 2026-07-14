import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import asyncio
from bleak import BleakScanner, BleakClient
import threading
import csv
import webbrowser
import os
import time

# UUIDs for Nordic UART Service (NUS)
NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

def get_paired_flockwatch():
    try:
        import subprocess
        # Run bluetoothctl to get all paired/known devices
        out = subprocess.check_output(["bluetoothctl", "devices"], text=True, timeout=2.0)
        for line in out.splitlines():
            parts = line.split()
            # Format: Device F0:24:F9:98:44:28 FlockWatch-4428
            if len(parts) >= 3 and parts[0] == "Device":
                mac = parts[1]
                name = " ".join(parts[2:])
                if name.startswith("FlockWatch-"):
                    return mac, name
    except Exception:
        pass
    return None

class FlockWatchCompanion:
    def __init__(self, root):
        self.root = root
        self.root.title("FlockWatch - Desktop Log Companion")
        self.root.geometry("900x600")
        
        # Apply dark theme
        self.style = ttk.Style()
        self.style.theme_use("clam")
        
        self.bg_color = "#121212"
        self.card_color = "#1e1e1e"
        self.accent_color = "#00dcb4"
        self.fg_color = "#ffffff"
        self.muted_color = "#888888"
        
        self.root.configure(bg=self.bg_color)
        
        # Option database overrides for standard/dialog widgets (fixes white-on-white text boxes)
        self.root.option_add("*Background", self.card_color)
        self.root.option_add("*Foreground", self.fg_color)
        self.root.option_add("*Entry.background", self.bg_color)
        self.root.option_add("*Entry.foreground", self.fg_color)
        self.root.option_add("*Entry.insertBackground", self.accent_color)
        self.root.option_add("*Listbox.background", self.bg_color)
        self.root.option_add("*Listbox.foreground", self.fg_color)
        
        self.style.configure(".", background=self.bg_color, foreground=self.fg_color)
        self.style.configure("TFrame", background=self.bg_color)
        self.style.configure("Card.TFrame", background=self.card_color, relief="flat")
        self.style.configure("TLabel", background=self.bg_color, foreground=self.fg_color, font=("Consolas", 10))
        self.style.configure("Card.TLabel", background=self.card_color, foreground=self.fg_color, font=("Consolas", 10))
        self.style.configure("Header.TLabel", background=self.bg_color, foreground=self.accent_color, font=("Consolas", 14, "bold"))
        self.style.configure("Title.TLabel", background=self.card_color, foreground=self.accent_color, font=("Consolas", 12, "bold"))
        
        # Custom button styles
        self.style.configure("Accent.TButton", background=self.accent_color, foreground=self.bg_color, borderwidth=0, font=("Consolas", 10, "bold"))
        self.style.map("Accent.TButton", background=[("active", "#00b392")])
        self.style.configure("TButton", background=self.card_color, foreground=self.fg_color, borderwidth=1, bordercolor=self.muted_color, font=("Consolas", 10))
        self.style.map("TButton", background=[("active", "#2a2a2a")])

        # State variables
        self.ble_loop = None
        self.ble_thread = None
        self.log_data = []
        self.downloaded_content = ""
        self.is_downloading = False
        self.total_expected_bytes = 0
        self.received_bytes = 0
        
        self.create_widgets()
        
    def create_widgets(self):
        # Main layout panels
        top_bar = ttk.Frame(self.root, padding=10)
        top_bar.pack(fill="x", side="top")
        
        ttk.Label(top_bar, text="FLOCKWATCH COMPANION", style="Header.TLabel").pack(side="left")
        
        # Control Panel
        control_panel = ttk.Frame(self.root, padding=10, style="Card.TFrame")
        control_panel.pack(fill="x", padx=15, pady=10)
        
        # Connection row
        ttk.Label(control_panel, text="BLE Operations:", style="Title.TLabel").grid(row=0, column=0, sticky="w", columnspan=2, pady=5)
        
        self.btn_sync = ttk.Button(control_panel, text="Sync Logs via BLE", style="Accent.TButton", command=self.start_ble_sync)
        self.btn_sync.grid(row=1, column=0, padx=5, pady=5)
        
        self.btn_load = ttk.Button(control_panel, text="Load Local CSV", command=self.load_local_csv)
        self.btn_load.grid(row=1, column=1, padx=5, pady=5)
        
        self.btn_clear_ble = ttk.Button(control_panel, text="Clear Device Logs", command=self.clear_device_logs)
        self.btn_clear_ble.grid(row=1, column=2, padx=5, pady=5)
        
        self.lbl_status = ttk.Label(control_panel, text="Status: Disconnected / Idle", style="Card.TLabel")
        self.lbl_status.grid(row=1, column=3, padx=20, pady=5, sticky="w")
        
        # Progress Bar
        self.progress = ttk.Progressbar(control_panel, orient="horizontal", length=120, mode="determinate")
        self.progress.grid(row=1, column=4, padx=5, pady=5, sticky="e")

        # MAC Address direct override
        ttk.Label(control_panel, text="Device MAC (Optional):", style="Card.TLabel").grid(row=1, column=5, padx=10, pady=5, sticky="w")
        self.ent_device_mac = tk.Entry(control_panel, background=self.bg_color, foreground=self.fg_color, insertbackground=self.accent_color, width=17, relief="flat", highlightthickness=1, highlightbackground=self.muted_color, highlightcolor=self.accent_color)
        self.ent_device_mac.grid(row=1, column=6, padx=5, pady=5, sticky="w")
        
        # Filters row
        ttk.Label(control_panel, text="Filters:", style="Title.TLabel").grid(row=2, column=0, sticky="w", columnspan=2, pady=10)
        
        ttk.Label(control_panel, text="Type:", style="Card.TLabel").grid(row=3, column=0, sticky="w", padx=5)
        self.cmb_filter_type = ttk.Combobox(control_panel, values=["ALL", "WIFI_AP", "WIFI_CLIENT", "BLE"], width=12, state="readonly")
        self.cmb_filter_type.set("ALL")
        self.cmb_filter_type.grid(row=3, column=1, padx=5)
        self.cmb_filter_type.bind("<<ComboboxSelected>>", self.apply_filters)
        
        ttk.Label(control_panel, text="Search MAC/SSID:", style="Card.TLabel").grid(row=3, column=2, sticky="w", padx=5)
        self.ent_search = tk.Entry(control_panel, background=self.bg_color, foreground=self.fg_color, insertbackground=self.accent_color, width=25, relief="flat", highlightthickness=1, highlightbackground=self.muted_color, highlightcolor=self.accent_color)
        self.ent_search.grid(row=3, column=3, padx=5, sticky="w")
        self.ent_search.bind("<KeyRelease>", self.apply_filters)
        
        # Logs Table
        table_frame = ttk.Frame(self.root, padding=10)
        table_frame.pack(fill="both", expand=True, padx=15, pady=5)
        
        columns = ("timestamp", "mac", "ssid", "rssi", "channel", "type", "reason")
        self.tree = ttk.Treeview(table_frame, columns=columns, show="headings", selectmode="browse")
        
        # Table Styling
        self.style.configure("Treeview", background=self.card_color, foreground=self.fg_color, fieldbackground=self.card_color, rowheight=22, font=("Consolas", 9))
        self.style.configure("Treeview.Heading", background=self.bg_color, foreground=self.accent_color, font=("Consolas", 10, "bold"))
        
        self.tree.heading("timestamp", text="Uptime (ms)", command=lambda: self.sort_column("timestamp", False))
        self.tree.heading("mac", text="MAC Address", command=lambda: self.sort_column("mac", False))
        self.tree.heading("ssid", text="SSID / Name", command=lambda: self.sort_column("ssid", False))
        self.tree.heading("rssi", text="RSSI (dBm)", command=lambda: self.sort_column("rssi", False))
        self.tree.heading("channel", text="Channel", command=lambda: self.sort_column("channel", False))
        self.tree.heading("type", text="Type", command=lambda: self.sort_column("type", False))
        self.tree.heading("reason", text="Match Reason", command=lambda: self.sort_column("reason", False))
        
        self.tree.column("timestamp", width=100, anchor="center")
        self.tree.column("mac", width=150, anchor="center")
        self.tree.column("ssid", width=180, anchor="w")
        self.tree.column("rssi", width=90, anchor="center")
        self.tree.column("channel", width=80, anchor="center")
        self.tree.column("type", width=100, anchor="center")
        self.tree.column("reason", width=180, anchor="w")
        
        scrollbar = ttk.Scrollbar(table_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        
        self.tree.pack(fill="both", expand=True, side="left")
        scrollbar.pack(fill="y", side="right")
        
        self.tree.bind("<<TreeviewSelect>>", self.on_select_record)
        
        # Map/Database Handoff Panel
        bottom_bar = ttk.Frame(self.root, padding=10, style="Card.TFrame")
        bottom_bar.pack(fill="x", side="bottom", padx=15, pady=15)
        
        self.lbl_selected = ttk.Label(bottom_bar, text="Select a surveillance record above to take action.", style="Card.TLabel")
        self.lbl_selected.pack(side="left", padx=10)
        
        self.btn_map = ttk.Button(bottom_bar, text="Open DeFlock Portal", state="disabled", command=self.open_deflock)
        self.btn_map.pack(side="right", padx=5)
        
        self.btn_osm = ttk.Button(bottom_bar, text="Open OSM Map", state="disabled", command=self.open_osm)
        self.btn_osm.pack(side="right", padx=5)

    def log_status(self, text):
        self.lbl_status.config(text=f"Status: {text}")
        
    def start_ble_sync(self):
        if self.is_downloading:
            return
        self.is_downloading = True
        self.btn_sync.config(state="disabled")
        self.progress["value"] = 0
        self.downloaded_content = ""
        
        # Start Bleak in a background thread to prevent Tkinter freezing
        self.ble_thread = threading.Thread(target=self.run_async_ble_task, args=(self.ble_sync_process,))
        self.ble_thread.daemon = True
        self.ble_thread.start()
        
    def clear_device_logs(self):
        if self.is_downloading:
            return
        if not messagebox.askyesno("Confirm Clear", "Are you sure you want to request the remote M5StickC to format and delete all logged data?"):
            return
            
        self.is_downloading = True
        self.btn_sync.config(state="disabled")
        self.progress["value"] = 0
        
        self.ble_thread = threading.Thread(target=self.run_async_ble_task, args=(self.ble_clear_process,))
        self.ble_thread.daemon = True
        self.ble_thread.start()

    def run_async_ble_task(self, func_coro, *args):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(func_coro(*args))
        except Exception as e:
            err_msg = str(e)
            self.root.after(0, lambda msg=err_msg: messagebox.showerror("BLE Error", f"Operation failed: {msg}"))
        finally:
            self.is_downloading = False
            self.root.after(0, lambda: self.btn_sync.config(state="normal"))
            self.root.after(0, lambda: self.log_status("Idle"))

    async def ble_sync_process(self):
        target_mac = self.ent_device_mac.get().strip()
        client_address = None
        
        if target_mac:
            self.root.after(0, lambda: self.log_status(f"Direct connecting to {target_mac}..."))
            client_address = target_mac
        else:
            # Check for paired device first
            paired_info = get_paired_flockwatch()
            if paired_info:
                mac, name = paired_info
                self.root.after(0, lambda: self.log_status(f"Auto-detected paired {name}..."))
                client_address = mac
            else:
                self.root.after(0, lambda: self.log_status("Scanning for FlockWatch..."))
                devices = await BleakScanner.discover(timeout=5.0)
                fw_device = None
                for d in devices:
                    if d.name and d.name.startswith("FlockWatch-"):
                        fw_device = d
                        break
                        
                if not fw_device:
                    self.root.after(0, lambda: self.log_status("Device not found!"))
                    self.root.after(0, lambda: messagebox.showwarning("Sync Warning", "Could not find any FlockWatch BLE device. Check that your M5StickC is in 'Start BLE Transfer' mode or enter its MAC address directly!"))
                    return
                client_address = fw_device.address
                self.root.after(0, lambda: self.log_status(f"Connecting to {fw_device.name}..."))
            
        async with BleakClient(client_address) as client:
            self.root.after(0, lambda: self.log_status("Connected. Ready to sync."))
            
            # Setup notification callback
            def notification_handler(sender, data):
                msg = data.decode("utf-8", errors="ignore")
                
                # Check for start marker
                if msg.startswith("[START:"):
                    # Format: [START:logs.csv:size]
                    parts = msg.strip().replace("[START:", "").replace("]", "").split(":")
                    if len(parts) >= 2:
                        self.total_expected_bytes = int(parts[1])
                    self.received_bytes = 0
                    self.downloaded_content = ""
                    self.root.after(0, lambda: self.log_status("Transferring file..."))
                # Check for end marker
                elif msg.startswith("[END]"):
                    self.root.after(0, self.save_downloaded_logs)
                # Else collect data
                else:
                    self.downloaded_content += msg
                    self.received_bytes += len(data)
                    if self.total_expected_bytes > 0:
                        pct = min(100, int((self.received_bytes * 100) / self.total_expected_bytes))
                        self.root.after(0, lambda: self.progress.configure(value=pct))
                        self.root.after(0, lambda: self.log_status(f"Downloading: {pct}%"))

            await client.start_notify(NUS_TX_UUID, notification_handler)
            
            # Request file transfer
            await client.write_gatt_char(NUS_RX_UUID, b"GET_LOGS\n")
            
            # Keep loop running until download finishes
            while self.is_downloading:
                await asyncio.sleep(0.5)
                
            await client.stop_notify(NUS_TX_UUID)

    async def ble_clear_process(self):
        target_mac = self.ent_device_mac.get().strip()
        client_address = None
        
        if target_mac:
            self.root.after(0, lambda: self.log_status(f"Direct connecting to {target_mac}..."))
            client_address = target_mac
        else:
            # Check for paired device first
            paired_info = get_paired_flockwatch()
            if paired_info:
                mac, name = paired_info
                self.root.after(0, lambda: self.log_status(f"Auto-detected paired {name}..."))
                client_address = mac
            else:
                self.root.after(0, lambda: self.log_status("Connecting to device..."))
                devices = await BleakScanner.discover(timeout=5.0)
                fw_device = None
                for d in devices:
                    if d.name and d.name.startswith("FlockWatch-"):
                        fw_device = d
                        break
                        
                if not fw_device:
                    self.root.after(0, lambda: self.log_status("Device not found!"))
                    return
                client_address = fw_device.address
            
        async with BleakClient(client_address) as client:
            self.root.after(0, lambda: self.log_status("Connected. Sending clear command..."))
            
            cleared_event = asyncio.Event()
            
            def clear_handler(sender, data):
                msg = data.decode("utf-8", errors="ignore").strip()
                if msg == "LOGS_CLEARED":
                    self.root.after(0, lambda: messagebox.showinfo("Clear Success", "Remote device logs cleared successfully!"))
                    cleared_event.set()
                elif msg == "ERROR_CLEAR_FAILED":
                    self.root.after(0, lambda: messagebox.showerror("Clear Error", "Failed to clear logs on the remote device filesystem!"))
                    cleared_event.set()
                    
            await client.start_notify(NUS_TX_UUID, clear_handler)
            await client.write_gatt_char(NUS_RX_UUID, b"CLEAR_LOGS\n")
            
            try:
                await asyncio.wait_for(cleared_event.wait(), timeout=10.0)
            except asyncio.TimeoutError:
                self.root.after(0, lambda: messagebox.showerror("Error", "Command timed out waiting for acknowledgment."))
                
            await client.stop_notify(NUS_TX_UUID)

    def save_downloaded_logs(self):
        file_path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
            title="Save Downloaded Logs File"
        )
        if file_path:
            try:
                with open(file_path, "w", encoding="utf-8") as f:
                    f.write(self.downloaded_content)
                self.load_csv_data(file_path)
                messagebox.showinfo("Sync Complete", f"Downloaded logs saved and loaded successfully!\nFile: {os.path.basename(file_path)}")
            except Exception as e:
                messagebox.showerror("Save Error", f"Could not write log file: {str(e)}")

    def load_local_csv(self):
        file_path = filedialog.askopenfilename(
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
            title="Select Log CSV File"
        )
        if file_path:
            self.load_csv_data(file_path)

    def load_csv_data(self, filepath):
        self.log_data = []
        try:
            with open(filepath, "r", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for row in reader:
                    self.log_data.append(row)
            self.apply_filters()
            self.log_status(f"Loaded {len(self.log_data)} logs from {os.path.basename(filepath)}")
        except Exception as e:
            messagebox.showerror("Parse Error", f"Failed to parse CSV log file:\n{str(e)}")

    def apply_filters(self, event=None):
        # Clear tree
        for item in self.tree.get_children():
            self.tree.delete(item)
            
        filter_type = self.cmb_filter_type.get()
        search_query = self.ent_search.get().lower()
        
        for idx, row in enumerate(self.log_data):
            # Check type filter
            if filter_type != "ALL" and row.get("type", "") != filter_type:
                continue
                
            # Check text search filter (MAC or SSID)
            mac = row.get("mac", "").lower()
            ssid = row.get("ssid", "").lower()
            reason = row.get("reason", "").lower()
            if search_query and (search_query not in mac and search_query not in ssid and search_query not in reason):
                continue
                
            # Insert to tree
            self.tree.insert("", "end", iid=str(idx), values=(
                row.get("timestamp", ""),
                row.get("mac", ""),
                row.get("ssid", ""),
                row.get("rssi", ""),
                row.get("channel", ""),
                row.get("type", ""),
                row.get("reason", "")
            ))
            
    def on_select_record(self, event):
        selected = self.tree.selection()
        if not selected:
            self.lbl_selected.config(text="Select a surveillance record above to take action.")
            self.btn_map.config(state="disabled")
            self.btn_osm.config(state="disabled")
            return
            
        row_id = selected[0]
        row_data = self.log_data[int(row_id)]
        
        mac = row_data.get("mac", "N/A")
        ssid = row_data.get("ssid", "N/A")
        rssi = row_data.get("rssi", "N/A")
        
        self.lbl_selected.config(text=f"Selected: MAC={mac} | SSID/Name={ssid} (RSSI={rssi})")
        
        # DeFlock does not need GPS since users crowdsource locations by MAC/SSID search.
        self.btn_map.config(state="normal")
        
        # OSM needs GPS, which is blank for now on M5, but we check if columns exist
        gps_lat = row_data.get("gps_lat", "")
        gps_lon = row_data.get("gps_lon", "")
        if gps_lat and gps_lon:
            self.btn_osm.config(state="normal")
        else:
            self.btn_osm.config(state="disabled")

    def open_deflock(self):
        selected = self.tree.selection()
        if not selected: return
        row_data = self.log_data[int(selected[0])]
        
        mac = row_data.get("mac", "")
        ssid = row_data.get("ssid", "")
        
        # Open DeFlock search page for the MAC address
        url = f"https://deflock.me/?search={mac}"
        webbrowser.open(url)

    def open_osm(self):
        selected = self.tree.selection()
        if not selected: return
        row_data = self.log_data[int(selected[0])]
        
        lat = row_data.get("gps_lat", "")
        lon = row_data.get("gps_lon", "")
        if lat and lon:
            url = f"https://www.openstreetmap.org/?mlat={lat}&mlon={lon}#map=17/{lat}/{lon}"
            webbrowser.open(url)

    def sort_column(self, col, reverse):
        l = [(self.tree.set(k, col), k) for k in self.tree.get_children("")]
        
        # Numeric conversions where applicable
        if col in ("rssi", "channel", "timestamp"):
            l.sort(key=lambda t: int(t[0]) if t[0].replace("-", "").isdigit() else 0, reverse=reverse)
        else:
            l.sort(reverse=reverse)
            
        for index, (val, k) in enumerate(l):
            self.tree.move(k, "", index)
            
        self.tree.heading(col, command=lambda: self.sort_column(col, not reverse))


if __name__ == "__main__":
    root = tk.Tk()
    app = FlockWatchCompanion(root)
    root.mainloop()
