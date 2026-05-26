# Module 6: Troubleshooting and Maintenance

Srisaket All-in-One Weather Station -- Embedded Technical Manual

IDEA Laboratory @ KMUTT

---

## Table of Contents

1. [Common Issues and Solutions](#1-common-issues-and-solutions)
2. [Debug Serial Console](#2-debug-serial-console)
3. [Event Log Format](#3-event-log-format)
4. [Storage Management](#4-storage-management)
5. [Web Portal Monitoring](#5-web-portal-monitoring)
6. [Watchdog Reset Analysis](#6-watchdog-reset-analysis)
7. [Factory Reset](#7-factory-reset)
8. [Known Limitations](#8-known-limitations)

---

## 1. Common Issues and Solutions

The table below maps commonly observed symptoms (as they appear in the serial console) to their likely cause and recommended corrective action.

| Symptom | Likely Cause | Solution |
|---|---|---|
| `[TMO] State X timeout` in serial | A state machine step exceeded its maximum allowed duration (see Section 6 for per-state timeouts). | Check sensor wiring, GSM signal strength, and network availability. The firmware automatically falls through to the next state, so data loss is limited to the timed-out operation. |
| `[MODBUS] ... err=0x02` or general Modbus error | UART communication failure with the RS-485 sensor. | Check RS-485 wiring (A/B lines, GND), verify baud rate matches sensor (9600), and confirm slave ID is correct. |
| `[MODBUS] InvalidSlaveID -- full UART recovery` (0xE0) | Invalid slave ID response or UART corruption. The firmware performs automatic UART recovery (flush + re-init). | Verify the sensor slave ID matches the firmware configuration (soil = 0x03, weather = 0x01). Check RS-485 wiring for loose connections or electrical noise. |
| `[NTP] Fallback:` in serial | GSM time synchronization failed (NTP server unreachable or GPRS not connected). | Check SIM card insertion, GSM antenna connection, and network registration. When NTP fails, the firmware falls back to the last known CSV timestamp + 10-minute increment. |
| `[INFLUX] TCP connect failed` | GPRS data connection is not established, or the InfluxDB server is unreachable. | Check GSM signal quality (`[GSM] Signal:` output), verify APN setting matches the carrier, and confirm the InfluxDB server address and port are correct in Settings. |
| `[OTA] Auth failed -- check download password` | The OTA download password is incorrect or not set on the server. | Update `otadlpass` in the web portal Settings page to match the server-side download password. |
| `[OTA] TCP connect failed` | OTA server is unreachable via GPRS. | Verify the OTA server URL in Settings, check GSM connectivity, and confirm GPRS data is active. |
| `[BLE] Connect FAILED after 3 attempts` | BLE target device is out of range, not advertising, or not powered on. | Move closer to the BLE device. Ensure the device is powered on and advertising. Verify the MAC address in Settings is correct. |
| `[FS] LittleFS FAILED` at boot | Flash filesystem corruption (power loss during write, worn flash sector). | Reformat: connect via serial and perform a factory reset (see Section 7). |
| `[WARN] TPL5110 did not cut power` | TPL5110 timer is not connected, or the DONE pin wiring is faulty. | Check TPL5110 DONE pin wiring (connected to `D2` on the ESP32-C3). Verify the TPL5110 power supply is stable. |
| `[GSM] Network registration FAILED` | No SIM card, no GSM signal, or wrong APN configuration. | Check SIM card is properly inserted. Verify GSM antenna is connected. Confirm the APN string matches the mobile carrier (`internet` by default). |
| `[MQTT] Publish FAILED` or MQTT timeout | MQTT broker unreachable, authentication failure, or GPRS not connected. | Check broker address and port (default: `119.59.103.220:1883`). Verify MQTT credentials in Settings (`kmutt` / `kmutt@kmutt` by default). Ensure GPRS data is active. |
| Zero sensor readings (all zeros in CSV) | Sensor not connected, wrong slave ID, or RS-485 bus failure. | Check RS-485 wiring (A, B, GND). Verify sensor slave ID matches config: soil sensor = 0x03, weather sensor = 0x01. Confirm sensor is powered on. |
| `[WARN] Soil humi/temp/EC zero` or `[WARN] Air humidity/temp/CO2 zero` | Sensor responded but returned all-zero data, possibly still initializing. | Allow sensor settle time (firmware waits 15 seconds). Check sensor power supply. Retry on next cycle. |
| `[MODBUS] Max consecutive failures reached` | Persistent RS-485 communication failure across multiple cycles. | The firmware clears the failed sensor section to prevent stale data. Investigate RS-485 bus integrity: check for loose wires, incorrect baud rate, or sensor damage. |
| Login failure email alarm | Repeated failed web portal login attempts detected. | Check if unauthorized access is being attempted. The firmware sends an email alarm via GSM SMTP when `emailalarm` is enabled in Settings. |

---

## 2. Debug Serial Console

### Connecting

1. Connect a USB cable from the ESP32-C3 board to the development computer.
2. Open a serial monitor (PlatformIO Serial Monitor, Arduino IDE, or `minicom`/`picocom`) at **115200 baud** (8N1).
3. Power cycle the device to observe the full boot sequence.

### Build Flags

| Flag | Location | Effect |
|---|---|---|
| `DEBUG=1` | `utilities.h` | Enables verbose sensor reading output, including raw register values and retry details. |
| `CORE_DEBUG_LEVEL=0` | `platformio.ini` | Suppresses ESP-IDF internal debug output, keeping only application-level messages. |

### Serial Output Prefixes

All application-level serial output uses bracketed prefixes to identify the originating subsystem. Use these prefixes to filter logs during debugging.

| Prefix | Source File | Meaning |
|---|---|---|
| `[FW]` | `main_1.cpp` | Firmware version information printed at boot. |
| `[WDT]` | `main_1.cpp` | Watchdog timer events (initialization, feeding). |
| `[SERIAL]` | `main_1.cpp` | UART peripheral initialization (RS-485, GSM). |
| `[FS]` | `main_1.cpp`, `Memory.cpp` | LittleFS filesystem operations (mount, create, write). |
| `[BLE]` | `main_1.cpp` | BLE scan, connect, read, and disconnect operations. |
| `[BLE-CSV]` | `main_1.cpp` | BLE data CSV save operations and duplicate detection. |
| `[NTP]` | `main_1.cpp`, `GsmHandler.cpp` | Network time synchronization (NTP sync, fallback). |
| `[INFLUX]` | `main_1.cpp` | InfluxDB TCP connection, HTTP POST, and response codes. |
| `[OTA]` | `main_1.cpp` | Firmware OTA update checks, downloads, and flash writes. |
| `[SAVE]` | `main_1.cpp` | CSV data storage operations. |
| `[DONE]` | `main_1.cpp` | TPL5110 DONE pin pulse (end of cycle, power cut). |
| `[TMO]` | `main_1.cpp` | State machine timeout warnings with state ID and duration. |
| `[BATT]` | `main_1.cpp` | Battery voltage reading at boot (millivolts). |
| `[WiFi]` | `main_1.cpp`, `WifiApServer.cpp` | WiFi AP start/stop events and web server status. |
| `[GSM]` | `GsmHandler.cpp` | GSM modem init, SIM detection, GPRS connection, signal quality. |
| `[MQTT]` | `GsmHandler.cpp` | MQTT broker connection, publish, and disconnect events. |
| `[MODBUS]` | `sensor_v2.cpp` | Modbus register read results, error codes, retry counts. |
| `[WARN]` | `sensor_v2.cpp`, `main_1.cpp` | Warnings for sensor failures, TPL5110 issues, consecutive error thresholds. |
| `[ERROR]` | `sensor_v2.cpp` | Critical errors (zero successful reads, consecutive failure counts). |

---

## 3. Event Log Format

### File Naming

Event logs are stored as daily CSV files with the naming convention:

```
/Event-DD-MM-YYYY.csv
```

For example: `/Event-26-05-2026.csv`.

A new event log file is created automatically each day when the first event occurs. If the file already exists, new entries are appended.

### Log Contents

Each event log contains timestamped entries for the following event types:

| Event Category | Example Entries |
|---|---|
| Authentication | `User login`, `User logout`, `FAILED login attempt (user=...)` |
| Settings changes | `Password changed`, `BLE Client settings updated`, `InfluxDB settings updated`, `OTA settings updated`, `Email settings updated`, `MQTT settings updated`, `Memory settings updated`, `NTP settings updated` |
| File operations | `Deleted: <filename>`, `Delete ALL data files` |
| BLE operations | `BLE disconnected`, `BLE forget saved device` |
| OTA operations | `OTA update requested via web portal` |
| Email operations | `Email alarm sent` |
| System | `Manual reboot via web UI` |

### Accessing Event Logs

- **Web portal `/log` page**: Browse event logs in the browser with download links for individual files.
- **Web portal `/logdl?f=<filename>`**: Direct download of a specific event log file.
- **Web portal `/files` page**: Event log files are listed alongside data files and can be deleted individually.

---

## 4. Storage Management

All persistent data is stored in the ESP32-C3 internal flash using the LittleFS filesystem.

### Checking Free Space

- The web portal **Settings** page (`/settings`) displays the available flash space.
- The firmware checks available space before every write operation.

### Storage Thresholds

| Parameter | Value | Constant | Description |
|---|---|---|---|
| Minimum free space | 10 KB | `MIN_FREE_SPACE_BYTES` | Writes are rejected if free space falls below this threshold. |
| Maximum file size warning | 500 KB | `MAX_FILE_SIZE_BYTES` | A warning is printed if any single data file exceeds this size. |
| Storage rollover threshold | 80% | `STORAGE_ROLLOVER_PERCENT` | When usage exceeds this percentage, daily CSV files are recreated. |

### Storage Rollover Behavior

When filesystem usage exceeds the rollover threshold (default 80%, configurable via Settings):

1. The last data row from the current daily CSV is preserved in memory.
2. The existing daily CSV file is deleted.
3. A new daily CSV is created with the standard header row.
4. The preserved last data row is appended as the only data entry.
5. New data continues to be appended normally.

This ensures the most recent reading is always retained while freeing storage space.

### Manual Cleanup

Use the web portal `/files` page to manage stored data:

- **Delete individual files**: Click the delete button next to any file.
- **Delete all data files**: Use the "Delete All" button to remove all data files at once. Event log files must be deleted individually.

---

## 5. Web Portal Monitoring

The WiFi Access Point web portal provides real-time monitoring and management capabilities.

### Connecting to the Portal

1. Connect to the WiFi network `WeatherStation_AP` (password: `12345678`).
2. Open a browser and navigate to `http://192.168.4.1`.
3. Log in with the configured credentials (default: `admin` / `admin`).

### Dashboard Pages

| Page | URL | Description | Refresh Rate |
|---|---|---|---|
| Live Dashboard | `/live` | Real-time sensor data display showing all current readings (soil, weather, battery). | Auto-refresh every 30 seconds via `/api/live` JSON endpoint. |
| BLE Data | `/live` (lower section) | BLE-connected device data including NUS stream and GATT characteristic values. | Auto-refresh every 5 seconds via `/api/ble` JSON endpoint and `/ble/refresh` trigger. |
| Event Log | `/log` | View and download event log files for diagnostics and audit trail. | Manual refresh. |
| File Manager | `/files` | Browse, download, and delete stored CSV data files and event logs. | Manual refresh. |
| Settings | `/settings` | Configure all system parameters (network, sensors, MQTT, OTA, email, memory). | Manual refresh. |

### API Endpoints

| Endpoint | Method | Returns |
|---|---|---|
| `/api/live` | GET | JSON object with current sensor readings and system status. |
| `/api/ble` | GET | JSON object with BLE device data (connected device info, characteristic values). |

---

## 6. Watchdog Reset Analysis

### Watchdog Configuration

- **Timeout**: 45 seconds (`WDT_TIMEOUT_SEC` in `utilities.h`).
- **Action**: Hardware reset (ESP32-C3 reboots entirely).
- **Initialization**: Configured early in `setup()` with `esp_task_wdt_init()`.

### After a Watchdog Reset

When the watchdog triggers, the device reboots and resumes operation from `STATE_WIFI_AP` (the first state in the state machine). No state is persisted across resets. The full operational cycle runs again from the beginning.

### Common Causes of Watchdog Resets

| Cause | Explanation |
|---|---|
| GSM network registration taking too long | The GSM init state has a 120-second timeout (`GSM_INIT_TIMEOUT_MS`), but if the modem itself becomes unresponsive, the WDT may trigger first. |
| BLE operation blocking | BLE connect/read operations that stall without returning can consume the WDT window. The firmware feeds the WDT at key points during BLE operations to mitigate this. |
| OTA download stall | If the OTA HTTP download stops receiving data, the 45-second WDT may trigger before the read timeout expires. |
| LittleFS corruption | A corrupted filesystem may cause blocking calls during file operations. |

### WDT Feeding Points

The firmware calls `feedWDT()` (which invokes `esp_task_wdt_reset()`) at strategic points throughout the code to prevent false resets during legitimate long-running operations:

- State machine loop iteration
- BLE connect and read operations
- InfluxDB HTTP request/response cycle
- OTA download progress
- File read/write operations (every 16 lines in CSV processing)
- GSM modem initialization

### Analyzing WDT Resets

To diagnose recurring watchdog resets:

1. Connect the serial monitor and observe the last output before the reboot.
2. The firmware prints `[FW] All-in-One Weather Station v...` immediately after boot -- if this appears unexpectedly, a WDT reset occurred.
3. Look for the last prefix in the serial output before the reboot to identify which operation was running when the WDT triggered.
4. Check `[TMO]` messages from previous cycles to see if any states were approaching their timeouts.

---

## 7. Factory Reset

Factory reset restores all settings to their compiled defaults (defined in `utilities.h`) and clears stored data.

### Clearing NVS (Non-Volatile Storage)

NVS stores all user-configured settings (WiFi password, MQTT credentials, OTA settings, email configuration, etc.).

1. Connect the ESP32-C3 via USB.
2. Run the PlatformIO erase command:
   ```
   pio run -t erase
   ```
3. Re-upload the firmware:
   ```
   pio run -t upload
   ```
4. After re-upload, all settings revert to compiled defaults from `utilities.h`.

### Clearing LittleFS (Filesystem Data)

LittleFS stores CSV data files, BLE data files, and event logs.

**Option A -- Via Web Portal:**
1. Connect to `WeatherStation_AP` and log in.
2. Navigate to `/files`.
3. Click "Delete All" to remove all data files, or delete individual files as needed.

**Option B -- Via Serial:**
1. Connect via serial monitor at 115200 baud.
2. Send a factory reset command (if available in the current firmware version).
3. Alternatively, use `pio run -t erase` which clears the entire flash partition, then re-upload.

### Post-Reset Defaults

After a factory reset, the following defaults are restored:

| Setting | Default Value |
|---|---|
| Web portal username | `admin` |
| Web portal password | `admin` |
| WiFi AP SSID | `WeatherStation_AP` |
| WiFi AP password | `12345678` |
| GSM APN | `internet` |
| Soil sensor slave ID | `0x03` |
| Weather sensor slave ID | `0x01` |
| OTA download password | (empty) |
| Email alarm | disabled |
| MQTT broker | `119.59.103.220:1883` |
| Storage rollover threshold | 80% |

---

## 8. Known Limitations

| Limitation | Detail |
|---|---|
| Time accuracy without GSM | When GSM is unavailable, the firmware falls back to the last CSV timestamp plus a 10-minute increment. Over multiple cycles without GSM, timestamps drift progressively. |
| BLE and WiFi radio sharing | BLE and WiFi share the 2.4 GHz radio on the ESP32-C3. Simultaneous heavy use (e.g., active BLE streaming while serving web portal requests) may cause intermittent connectivity issues. |
| SIM800 GPRS bandwidth | The SIM800 GPRS connection provides approximately 85 kbps downlink. Large OTA firmware updates may time out if the binary exceeds practical download size for this bandwidth. |
| LittleFS wear | LittleFS relies on the ESP32-C3 internal flash wear leveling. Constant writes to the same file (daily CSV) will eventually wear the flash sector. The storage rollover mechanism mitigates but does not eliminate this risk. |
| Battery voltage reading | Battery voltage is read exactly once at boot via the voltage divider on pin `A0`. The value is not updated during the cycle. |
| BLE NUS JSON parser | The BLE NUS (Nordic UART Service) JSON parser uses simple string matching. Malformed JSON from the BLE device may produce `--` placeholder values in the output. |
| BLE device and characteristic limits | Maximum 10 scanned BLE devices (`BLE_MAX_DEVICES`) and 16 characteristics per connected device (`BLE_CHAR_MAX`). Devices or characteristics exceeding these limits are ignored. |
| CSV date rollover | CSV file names use the date at time of creation. Date rollover at midnight is not handled because the device powers off between cycles (TPL5110-controlled). Each cycle creates or appends to the file corresponding to the current timestamp. |
| WDT and long GSM operations | The 45-second watchdog timeout may conflict with GSM operations that legitimately exceed this window (e.g., GPRS attachment in poor signal areas). The firmware feeds the WDT at key points, but sustained blocking in the TinyGSM library can still trigger a reset. |
| No SSL/TLS for InfluxDB | The InfluxDB HTTP connection uses plain TCP. Data is transmitted unencrypted over GPRS. |

---

*End of Module 6 -- Troubleshooting and Maintenance*

*This is the final module of the Srisaket All-in-One Weather Station Embedded Technical Manual.*

*IDEA Laboratory @ KMUTT*
