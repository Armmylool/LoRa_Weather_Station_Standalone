# Module 6: Troubleshooting and Maintenance

**Firmware:** v2.3.5 | **Platform:** Seeed XIAO ESP32-C3 | **IDEA Laboratory @ KMUTT**

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

The table below maps observed symptoms to their most likely root cause and corrective action. All error strings and thresholds are taken directly from the firmware source.

| Symptom / Serial Message | Likely Cause | Solution |
|---|---|---|
| `[TMO] State N timeout ...` | A state machine step exceeded its maximum allowed duration. The system auto-advances to the fallback state. | Check the state number and elapsed time. Timeout values: WIFI_AP 300 s, GSM_INIT 120 s, NTP 30 s, WEATHER 30 s, SOIL 30 s, RECONNECT 120 s, PUBLISH 60 s. Investigate the subsystem responsible for that state. |
| `[MODBUS] Slave 0x%02X read FAILED, err=0x%02X` | RS485 communication failure. Common error codes: 0xE0 (InvalidSlaveID / bus collision), 0xE2 (timeout), 0xE1 (CRC error). | Verify RS485 wiring (D4=RX, D10=TX). Check sensor power supply. For 0xE0, the firmware performs full UART recovery automatically. Confirm slave IDs: soil=0x03, weather=0x01. |
| `[MODBUS] InvalidSlaveID -- full UART recovery` | Error code 0xE0 triggered UART bus recovery. | This is an automatic recovery. If it recurs, check for bus contention, incorrect termination resistors, or baud-rate mismatch (expected 9600). |
| `[ERROR] No successful reads! (consecutive: X/3)` | All read attempts and retries failed for a sensor. The firmware tracks consecutive failures and clears the sensor section after 3 consecutive total failures. | Check sensor wiring and power. The sensor section data will be zeroed after 3 consecutive total failures to avoid stale data. |
| `[WARN] Soil humi/temp/EC zero, retry #N` | Soil sensor returned zero for critical fields. Firmware re-reads up to `maxRetry` (5) times. | Sensor may still be settling after power-up. The 15 s settle delay (`SOIL_SETTLE_DELAY`) may be insufficient in cold conditions. |
| `[ERROR] Soil humi/temp/EC still zero after retries` | Soil sensor consistently returns zero despite retries. | Replace or reseat the RS485 soil sensor. Check for water ingress in the sensor connector. |
| `[WARN] Air humidity/temp/CO2 zero, retry #N` | Weather sensor returned zero for critical fields. | Same pattern as soil. Check weather sensor RS485 connection and power. |
| `[ERROR] Air humidity/temp/CO2 still zero after 3 retries` | Weather sensor consistently returns zero. | Inspect weather sensor wiring. Verify sensor slave ID is 0x01 and register address is 0x01F4. |
| `[GSM] Modem init FAILED` | SIM800 modem did not respond to AT initialization. | Check GSM UART wiring (D7=RX, D6=TX) at 9600 baud. Verify modem power supply (3.4--4.4 V). Check SIM card insertion. |
| `[GSM] SIM card not detected!` | SIM card not present or not seated properly. | Reseat or replace the SIM card. Ensure SIM is unlocked (no PIN required). |
| `[GSM] Network registration FAILED` | Modem could not register to a cellular network within 120 s (`GSM_INIT_TIMEOUT_MS`). | Check antenna connection. Verify cellular coverage at the deployment site. Try a different SIM/operator. The firmware feeds WDT every 2 s during this wait. |
| `[GSM] GPRS connect FAILED` | Modem registered to network but GPRS data session failed. | Verify APN setting (default: `internet`). Check that the SIM has an active data plan. |
| `[NTP] RTC time invalid, syncing via NTP...` | GSM RTC returned an invalid or unparsable date/time string. | Normal fallback behavior. The firmware will attempt NTP sync via `pool.ntp.org`. |
| `[NTP] NTP sync FAILED` / `[NTP] Sync TIMEOUT` | NTP sync failed or timed out after 60 s (`NTP_TIMEOUT_MS`). | Ensure GPRS data is active. Check DNS resolution for `pool.ntp.org`. The firmware falls back to time increment. |
| `[NTP] Time read FAILED after sync` | NTP reported success but RTC still returned invalid time. | Modem firmware issue. Consider updating SIM800 firmware. The system will use incremental time from last known backup. |
| `[NTP] Fallback: ...` | Time sync failed entirely; system is using incremented time from last backup. | No GSM time available. Time accuracy degrades by up to the polling interval (default 10 min) per cycle. |
| `[MQTT] Connection FAILED (timeout)` | MQTT broker unreachable within 60 s (`MQTT_PUBLISH_TIMEOUT`). | Verify broker IP (default: `119.59.103.220:1883`). Check GPRS connectivity. Verify MQTT credentials (user: `kmutt`, default password). |
| `[MQTT] Publish FAILED (state=X, len=Y)` | MQTT publish returned failure. State codes: -4=connection lost, -3=connect failed, -2=socket failed, -1=socket timeout. | Reconnect GSM/GPRS and retry. Check payload size (buffer set to 2048 bytes). |
| `[INFLUX] TCP connect failed HOST:PORT` | InfluxDB TCP connection failed after 3 attempts. | Verify InfluxDB host, port (default 8086), and GPRS connectivity. Check firewall rules. |
| `[INFLUX] GSM unavailable, skip` | InfluxDB write was attempted but GSM was not connected. | Non-critical; data is stored locally in CSV and will be sent on next successful GSM cycle. |
| `[OTA] Auth failed -- check download password` | OTA server returned HTTP 401. | Verify the download password in Settings > OTA. The `x-ESP32-password` header must match the server configuration. |
| `[OTA] TCP connect failed HOST:PORT after 3 attempts` | OTA server TCP connection failed. | Check OTA server URL and port. Verify GPRS data session is active. |
| `[OTA] GSM unavailable, skip` | OTA check was due but GSM was not initialized. | The firmware will retry on the next cycle or boot if `otaboot` is enabled. |
| `[BLE] Connect FAILED after 3 attempts` | BLE connection to target device failed after 3 retries. | Verify the BLE sensor is powered and advertising. Check distance (BLE range ~10 m). The firmware retries with 1 s delay between attempts. |
| `[BLE] Retry connect attempt N/3 (5s delay)` | Individual BLE connection attempt failed. | Transient RF issue. The firmware auto-retries. If persistent, check for interference from WiFi AP (same 2.4 GHz band). |
| `[BLE] Connection lost` | Previously connected BLE device disconnected. | Normal if sensor battery died or moved out of range. The web portal will show "Not connected." |
| `[FS] LittleFS FAILED` | LittleFS filesystem failed to mount. | Flash corruption. Reflash firmware (includes LittleFS format). Use `pio run -t erase` to fully reset, then reflash. |
| `LittleFS almost full! Available: X bytes` | Free space dropped below 10,000 bytes (`MIN_FREE_SPACE_BYTES`). Data save is skipped. | Connect to web portal at `/files` and delete old data files. Enable rollover in Settings. |
| `File too large (>500KB), consider rotation` | A single data file exceeded 500 KB (`MAX_FILE_SIZE_BYTES`). | Delete or download the file via web portal. The rollover mechanism should prevent this if enabled. |
| `[WARN] TPL5110 did not cut power` | The DONE signal was pulsed but the TPL5110 timer did not disconnect power. | Check TPL5110 wiring on pin D2. Verify the TPL5110 EN/ONE-SHOT jumper is configured correctly. The firmware enters an infinite WDT-feeding loop after this message. |
| `[WiFi] softAP FAILED` | WiFi AP could not be started. | Hardware issue with ESP32-C3 WiFi radio. If BLE is active, try disabling BLE (radio sharing conflict). Power cycle the device. |
| `Login FAILED user=X ip=X` (event log) | Invalid web portal login attempt. | Check credentials (default: admin/admin). If `emailalarm` is enabled, a notification email is sent. |
| `[EMAIL] SMTPSRV failed` / `[EMAIL] AUTH failed` / `[EMAIL] Send FAILED` | Email notification failed at various stages. | Verify SMTP settings. For Gmail, use an App Password (not the account password). Default SMTP port is 587. |
| `[WDT] Watchdog initialized` followed by unexpected reboot | Hardware watchdog triggered a reset (45 s timeout). | See [Section 6: Watchdog Reset Analysis](#6-watchdog-reset-analysis). |

---

## 2. Debug Serial Console

### 2.1 Connection

| Parameter | Value |
|---|---|
| Interface | USB-CDC (USB port on XIAO ESP32-C3) |
| Baud rate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |

Use any serial terminal: PlatformIO Serial Monitor (`pio device monitor`), PuTTY, TeraTerm, or Arduino Serial Monitor.

### 2.2 Build Flags for Debug Output

The firmware has a compile-time debug flag that controls verbose sensor read output.

| Flag | Location | Effect |
|---|---|---|
| `DEBUG=1` | `include/utilities.h` line 17 | Enables per-field sensor data printout after successful reads (e.g., "Weather Success!" with all values). Set to `0` to suppress. |
| `CORE_DEBUG_LEVEL=0` | `platformio.ini` build flag | Suppresses ESP-IDF framework debug output. Set to `1`--`4` for increasing verbosity (1=error, 2=warning, 3=info, 4=debug). |
| `ARDUINO_USB_MODE=1` | `platformio.ini` build flag | Required for XIAO ESP32-C3 USB-CDC mode. Do not change. |

### 2.3 Serial Output Prefix Reference

All diagnostic output uses bracketed prefixes to identify the subsystem. The table below is exhaustive based on source code analysis.

| Prefix | Source File | Meaning |
|---|---|---|
| `[WDT]` | `src/main_1.cpp` | Watchdog timer events (initialization). |
| `[FW]` | `src/main_1.cpp` | Firmware version banner printed at boot. |
| `[SERIAL]` | `src/main_1.cpp` | UART initialization for RS485 and GSM serial ports. |
| `[FS]` | `src/main_1.cpp`, `src/Memory.cpp` | LittleFS filesystem mount status and file creation. |
| `[BATT]` | `src/main_1.cpp`, `src/sensor_v2.cpp` | Battery voltage reading. Reports pin voltage, battery voltage, and millivolts. |
| `[TMO]` | `src/main_1.cpp` | State machine timeout. Format: `[TMO] State N timeout X ms -> M` where N is the current state enum and M is the fallback state. |
| `[WiFi]` | `src/WifiApServer.cpp` | WiFi AP start, stop, IP address, timeout, and OTA-triggered shutdown. |
| `[GSM]` | `src/GsmHandler.cpp` | GSM modem initialization, SIM status, network registration, GPRS connection, signal quality, modem reset, and power off. |
| `[NTP]` | `src/GsmHandler.cpp`, `src/main_1.cpp` | Network time acquisition: GSM RTC, NTP sync, fallback time increment. |
| `[MQTT]` | `src/GsmHandler.cpp` | MQTT broker connection, publish status, disconnect, state codes. |
| `[EMAIL]` | `src/GsmHandler.cpp` | Email sending: SMTP server config, authentication, body upload, send status. |
| `[MODBUS]` | `src/sensor_v2.cpp` | Modbus RS485 read failures with error code and retry count. |
| `[ERROR]` | `src/sensor_v2.cpp`, `src/Memory.cpp` | Critical failures: no successful sensor reads, buffer overflow, null pointers, LittleFS full. |
| `[WARN]` | `src/sensor_v2.cpp`, `src/main_1.cpp` | Warnings: zero sensor values, max consecutive failures, TPL5110 did not cut power. |
| `[INFLUX]` | `src/main_1.cpp` | InfluxDB v2 operations: line protocol output, TCP connect status, HTTP response code, error body. |
| `[OTA]` | `src/main_1.cpp` | Over-the-air update: server check, HTTP status (200=update available, 304=up to date, 401=auth failed), download progress, write status. |
| `[BLE]` | `src/main_1.cpp` | BLE client lifecycle: initialization, scan, connect attempts, service discovery (NUS/GATT), connection loss, data reception. |
| `[BLE-CSV]` | `src/main_1.cpp` | BLE sniffer data saved to CSV, including duplicate timestamp detection. |
| `[SAVE]` | `src/main_1.cpp` | Sensor data saved to daily CSV file. |
| `[DONE]` | `src/main_1.cpp` | TPL5110 DONE signal pulse (end of measurement cycle). |

### 2.4 Boot Sequence Output (Expected Order)

A normal boot produces serial output in this sequence:

```
[WDT] Watchdog initialized
[FW] All-in-One Weather Station v2.3.5
[SERIAL] RS485 UART1 started
[SERIAL] GSM UART0 started
[FS] LittleFS mounted
[BATT] Reading Battery Voltage...
Pin A0: X.XXX V | Battery: X.XXX V (XXXX mV)
[BATT] XXXX mV
[BLE] NimBLE client ready (scanner)      (if BLE enabled)
[BLE] Auto-reconnect scheduled: XX:XX...  (if saved MAC)
===== SETUP COMPLETE =====
[WiFi] Starting AP: WeatherStation_AP
[WiFi] AP IP: 192.168.4.1
[WiFi] Web server started (timeout: 5 min)
```

---

## 3. Event Log Format

### 3.1 File Naming

Event logs are stored as CSV files on LittleFS with the naming convention:

```
/Event-DD-MM-YYYY.csv
```

Where `DD-MM-YYYY` is the current date from the system clock. A new file is created automatically for each calendar day.

If the system clock has not been set (no GSM time), the fallback name is `/Event-00-00-0000.csv`.

### 3.2 File Format

Each file begins with a header row, followed by one row per event:

```csv
DateTime,Event
26/05/2026 14:30:05,Login SUCCESS user=admin ip=192.168.4.2
26/05/2026 14:32:10,BLE connect request: AA:BB:CC:DD:EE:FF
26/05/2026 14:35:22,Password changed
26/05/2026 14:40:00,OTA update requested via web portal
```

### 3.3 Events Logged

The following events are recorded by the web portal:

| Event String | Trigger |
|---|---|
| `Login SUCCESS user=X ip=Y` | Successful web portal authentication |
| `Login FAILED user=X ip=Y` | Failed authentication attempt |
| `Email alarm sent` | Email notification sent after failed login (when alarm enabled) |
| `User logout` | Manual logout |
| `BLE connect request: XX:XX:...` | BLE connection initiated from web UI |
| `BLE disconnected` | BLE disconnection via web UI |
| `BLE forget saved device` | Saved BLE device cleared |
| `BLE Client settings updated` | BLE enable/disable changed |
| `Delete file /X` | Individual file deleted via web portal |
| `Delete ALL data files` | Bulk file deletion |
| `Password changed` | Web portal password updated |
| `AP timeout updated` | WiFi AP idle timeout changed |
| `Data sources updated` | Modbus/BLE source toggle changed |
| `File intervals updated` | Data file rotation interval changed |
| `InfluxDB settings updated` | InfluxDB configuration changed |
| `NTP synced from BLE` | Manual NTP sync via BLE NUS |
| `NTP settings updated` | NTP enable/disable changed |
| `Memory settings updated` | Storage rollover threshold changed |
| `MQTT settings updated` | MQTT broker configuration changed |
| `Email settings updated` | Email/alarm configuration changed |
| `OTA settings updated` | OTA server configuration changed |
| `OTA update requested via web portal` | Manual OTA trigger |
| `Manual reboot via web UI` | Reboot triggered from About page |

### 3.4 Accessing Event Logs

**Web portal** -- Navigate to the **Log** tab (`/log`). The page displays:
- A list of all event log files with download and delete buttons.
- The last 20 lines of today's event log in a table.
- Download button for each log file (`/logdl?f=Event-DD-MM-YYYY.csv`).

**File download** -- Use the `/logdl?f=<filename>` endpoint to download any event log as a CSV file.

---

## 4. Storage Management

### 4.1 Flash Partition Layout

The 4 MB ESP32-C3 flash is partitioned as follows:

| Partition | Offset | Size | Purpose |
|---|---|---|---|
| NVS | 0x9000 | 20 KB | Preferences (key-value settings) |
| OTA Data | 0xE000 | 8 KB | Active OTA slot marker |
| App 0 | 0x10000 | 1.625 MB | Firmware slot A |
| App 1 | 0x1B0000 | 1.625 MB | Firmware slot B |
| LittleFS | 0x350000 | 720 KB | Data files, event logs, BLE data |

### 4.2 Storage Thresholds

| Parameter | Value | Source |
|---|---|---|
| Minimum free space | 10,000 bytes (`MIN_FREE_SPACE_BYTES`) | `include/utilities.h` |
| Maximum single file size | 500 KB (`MAX_FILE_SIZE_BYTES`) | `include/utilities.h` |
| Rollover threshold (default) | 80% (`STORAGE_ROLLOVER_PERCENT`) | `include/utilities.h` |
| Configurable rollover range | 10% -- 90% | Web portal settings slider |

### 4.3 Free Space Check

Before each data save, `Memory::saveData()` checks available LittleFS space:

```
availableSpace = totalBytes - usedBytes
```

If `availableSpace < MIN_FREE_SPACE_BYTES` (10 KB), the save is rejected and the following message is printed:

```
LittleFS almost full! Available: N bytes
```

### 4.4 Rollover Behavior

When rollover is enabled (default: on) and LittleFS usage reaches the configured threshold:

1. The last data line from the current daily CSV is preserved.
2. The daily CSV file is deleted.
3. A new daily CSV is created with the standard header.
4. The preserved last line is appended to the new file.

This ensures at least one data point survives rollover while freeing storage space.

### 4.5 File Size Warning

If a single data file exceeds `MAX_FILE_SIZE_BYTES` (500 KB), a warning is printed:

```
File too large (>500KB), consider rotation
```

This is advisory only; the file is not truncated automatically.

### 4.6 Manual Cleanup

Use the web portal to manage stored files:

| Action | URL | Description |
|---|---|---|
| List files | `/files` | Shows all data files (matching `DD-MM-YYYY.csv` or `DATA.csv`) with size |
| Download file | `/download?f=<name>` | Downloads a single CSV file |
| Delete file | `/delete` (POST) | Removes a single file |
| Delete all | `/deleteall` (POST) | Removes all data files at once |

Data file detection matches filenames in the pattern `DD-MM-YYYY.csv` or the literal `DATA.csv`. Event logs and BLE data files are not affected by the "Delete ALL" operation.

---

## 5. Web Portal Monitoring

### 5.1 Access

| Parameter | Value |
|---|---|
| SSID | `WeatherStation_AP` |
| Password | `12345678` |
| Channel | 1 |
| IP Address | 192.168.4.1 |
| Default login | admin / admin |
| Session timeout | 10 minutes (`WEB_SESSION_TIMEOUT_MS`, refreshed on each request) |

### 5.2 Live Dashboard (`/live`)

The live dashboard provides real-time sensor and system data. Two JavaScript auto-refresh intervals operate concurrently:

| Data Source | Refresh Interval | Endpoint |
|---|---|---|
| Modbus sensor data (soil + weather) | 30 seconds | `/api/live` |
| BLE sensor data | 5 seconds | `/api/ble` |

**Dashboard sections:**
- System Status -- GSM connection state, signal strength (dBm), WiFi AP client count, BLE active state.
- Battery -- Current battery voltage in millivolts.
- Memory -- LittleFS usage percentage with color coding (green < 60%, yellow 60--80%, red > 80%).
- Soil Sensor -- Moisture (%), temperature (C), EC (uS/cm), pH, N/P/K (mg/kg).
- Weather Sensor -- Wind speed (m/s), wind direction (deg), humidity (%), temperature (C), CO2 (ppm), pressure (kPa), illuminance (lux), rainfall (mm), solar (W/m2).
- BLE Sensor Data -- Live BLE data with NUS notification support. Shows connected device MAC, name, and characteristic values. Includes manual "Request Update" button for NUS devices.
- OTA Last Update -- Timestamp of the most recent successful OTA firmware update.

### 5.3 BLE Live API (`/api/ble`)

Returns JSON with the following structure:

```json
{
  "connected": true,
  "nus": false,
  "hasData": true,
  "age": 5,
  "mac": "AA:BB:CC:DD:EE:FF",
  "name": "SnifferPortal",
  "chars": [
    {"uuid": "Temperature (C)", "hex": "...", "ascii": "26.5"},
    ...
  ]
}
```

The `age` field indicates seconds since the last BLE data update. Data older than 30 seconds is displayed with reduced opacity on the dashboard.

### 5.4 Event Log Viewer (`/log`)

See [Section 3: Event Log Format](#3-event-log-format).

---

## 6. Watchdog Reset Analysis

### 6.1 Configuration

| Parameter | Value |
|---|---|
| Timeout | 45 seconds (`WDT_TIMEOUT_SEC`) |
| Action | System reset (hardware watchdog, panic handler) |
| Initialized in | `setup()` via `esp_task_wdt_init()` + `esp_task_wdt_add()` |
| Post-reset state | `STATE_WIFI_AP` (initial state in `main_1.cpp`) |

### 6.2 Where the WDT is Fed

The watchdog is reset (`esp_task_wdt_reset()`) in the following locations:

| Location | Context |
|---|---|
| `feedWDT()` in `main_1.cpp` | Called at the start of every `loop()` iteration and in long-running operations |
| `sensor_v2.cpp` Modbus read loop | During each sensor read attempt and zero-retry loop |
| `GsmHandler.cpp` network wait | Every 2 seconds during network registration |
| `GsmHandler.cpp` MQTT connect | Inside the MQTT connection retry loop |
| `GsmHandler.cpp` MQTT publish | After publish, during 10-iteration drain loop |
| `GsmHandler.cpp` NTP sync | Inside the NTP response wait loop |
| `WifiApServer.cpp` `handleClient()` | At the start of each web server client handling cycle |
| `WifiApServer.cpp` file download | Before streaming files to clients |
| `Memory.cpp` file operations | Every 16 lines during CSV read/write/trim operations |
| `main_1.cpp` InfluxDB send | During TCP connect and HTTP response wait |
| `main_1.cpp` OTA download | During firmware download chunk reads |
| `main_1.cpp` BLE operations | During connect, service discovery, and characteristic reads |
| `main_1.cpp` battery read | Before and during ADC sampling |
| `main_1.cpp` STATE_FINISH | Infinite loop after TPL5110 DONE pulse |

### 6.3 Common Causes of WDT Resets

| Cause | Details |
|---|---|
| Extended GSM network registration | If network registration exceeds 45 s without a WDT feed point being reached. The firmware feeds WDT every 2 s in the registration loop, but a SIM800 firmware hang could prevent the loop from progressing. |
| BLE blocking operations | NimBLE service discovery or characteristic reads may block if the connected device is unresponsive. The firmware feeds WDT between reads, but a BLE stack deadlock could trigger a reset. |
| OTA download stall | If the OTA server stops sending data for more than 45 s. The download loop has a 300 s total timeout with WDT feeds, but a TCP-level stall could bypass feed points. |
| LittleFS corruption | Corrupted flash can cause blocking file operations that never return. |
| InfluxDB HTTP response hang | If the InfluxDB server accepts the TCP connection but never responds. The firmware waits up to 10 s with WDT feeds, so this alone should not trigger WDT, but combined with other delays it may contribute. |

### 6.4 Identifying WDT Resets

After a WDT reset, the device boots into `STATE_WIFI_AP` and the serial output starts from the beginning of `setup()`. There is no persistent WDT reset counter. Look for:

1. Unexpected reboot with no `[DONE] Pulsing TPL5110` message before the restart.
2. Serial output resuming at `[WDT] Watchdog initialized` without a prior `[WARN] TPL5110 did not cut power`.

---

## 7. Factory Reset

### 7.1 NVS (Preferences) Erase

This clears all stored settings (WiFi AP timeout, MQTT credentials, InfluxDB tokens, email configuration, BLE saved MAC, OTA settings, etc.):

```bash
pio run -t erase
```

After erasing, reflash the firmware:

```bash
pio run -t upload
```

**Post-erase defaults** (restored from compiled-in constants):

| Setting | Default Value |
|---|---|
| Web portal username | `admin` (`WEB_DEFAULT_USER`) |
| Web portal password | `admin` (`WEB_DEFAULT_PASS`) |
| WiFi AP SSID | `WeatherStation_AP` |
| WiFi AP password | `12345678` |
| AP idle timeout | 5 minutes |
| OTA server URL | (empty) |
| OTA password | `admin` |
| OTA check interval | 24 hours |
| OTA device type | `All-in-One` |
| MQTT broker | `119.59.103.220:1883` |
| MQTT user | `kmutt` |
| MQTT password | `kmutt@kmutt` |
| MQTT client ID | `PCB_TEST_1` |
| Data file interval | 10 minutes |
| Memory rollover | 80% (enabled) |
| InfluxDB | Disabled |
| Email | Disabled |
| NTP sync | Enabled |
| BLE client | Disabled |
| Modbus source | Enabled |
| BLE source | Disabled |

### 7.2 LittleFS (Data Files) Erase

There are two methods:

**Method A -- Web Portal**

1. Connect to `WeatherStation_AP` (password: `12345678`).
2. Log in at `http://192.168.4.1` (admin/admin).
3. Navigate to **Files** (`/files`).
4. Click **Delete ALL Files** to remove all data CSVs.
5. Optionally navigate to **Log** (`/log`) and delete individual event log files.

**Method B -- Reflash with Format**

Reflashing the firmware triggers LittleFS to remount with `formatOnFail=true`. To force a full format, use the PlatformIO erase target first, then reflash. Note that `LittleFS.begin(true)` is called at boot with the format-on-fail flag, so a corrupt filesystem will be auto-formatted.

### 7.3 Verification

After a factory reset, verify by checking the serial console at boot:

```
[WDT] Watchdog initialized
[FW] All-in-One Weather Station v2.3.5
[SERIAL] RS485 UART1 started
[SERIAL] GSM UART0 started
[FS] LittleFS mounted
```

Then connect to the web portal and confirm all settings have reverted to defaults on the Settings page.

---

## 8. Known Limitations

| Limitation | Details |
|---|---|
| Time accuracy without GSM | If GSM is unavailable, system time is incremented by `TIME_INCREMENT_MINUTES` (10 min) from the last known backup. There is no RTC backup battery. Time drift accumulates with each cycle. The backup is read from the last line of the most recent daily CSV file. |
| BLE/WiFi radio sharing | The ESP32-C3 has a single 2.4 GHz radio shared between BLE and WiFi. Simultaneous BLE scanning/connections and WiFi AP operation may cause intermittent connectivity issues on either interface. The firmware runs BLE and WiFi AP concurrently during `STATE_WIFI_AP` only. |
| GPRS bandwidth | SIM800 GPRS provides approximately 85 kbps downlink (class 10 GPRS). OTA firmware downloads of ~1.3 MB may take 2--5 minutes. The firmware has a 300 s download timeout. |
| Flash wear | LittleFS is stored on the same SPI flash as the firmware. With 720 KB available and default 10-minute data intervals, the flash may experience significant write cycles over years of operation. The rollover mechanism helps mitigate this. |
| Single battery reading per cycle | Battery voltage is read once in `setup()` via ADC with 64-sample averaging. There is no periodic battery monitoring during the measurement cycle. A voltage divider with two 100 k resistors (R1=R2=100 k) provides a 2:1 ratio. |
| BLE NUS simple parser | The NUS JSON parser (`parseNusJson`) uses simple string matching (`strstr`) rather than a full JSON parser. Malformed JSON or nested objects may be parsed incorrectly. The parser looks for the last `{` before a `}` to extract the JSON object. |
| Maximum BLE characteristics | A maximum of 16 characteristics per connected device (`BLE_CHAR_MAX`) can be stored. Reading more than 16 characteristics from a GATT device will silently truncate results. |
| Maximum BLE scan results | A maximum of 10 BLE devices (`BLE_MAX_DEVICES`) can be stored from a single scan. Additional devices are silently ignored. |
| No midnight date rollover handling | The `incrementTime()` function handles hour overflow but relies on a simple day-in-month table without leap-year calculation. A cycle spanning midnight may produce an incorrect date if crossing month boundaries in a leap year. |
| MQTT keep-alive and socket timeout | MQTT keep-alive is set to 90 seconds and socket timeout to 45 seconds. Slow GPRS connections may cause premature disconnections. |
| InfluxDB error handling | InfluxDB writes are fire-and-forget from the state machine perspective. If the HTTP POST returns an error code other than 204, the error is logged to serial but the data is not retransmitted. The data remains in the local CSV file. |
| OTA no resume | If an OTA download is interrupted, it cannot be resumed. The firmware must start a new download from the beginning on the next cycle. |
| WiFi AP timeout granularity | The AP idle timeout counts only when zero clients are connected. A connected but idle client (e.g., browser tab left open) prevents timeout. |
| BLE NUS buffer size | The NUS accumulator buffer is 400 bytes (`NUS_BUF`). JSON payloads larger than this will be truncated, potentially losing the closing `}` and preventing parsing. |
| SIM800 multiplex constraint | The firmware uses SIM800 mux 0 for MQTT and mux 1 for InfluxDB/OTA. Only two simultaneous TCP connections are supported. The firmware explicitly closes mux 1 before opening new connections. |
| Login failure alarm requires GSM | The email alarm on failed login only fires if GSM is already connected. During the initial WiFi AP state before GSM initialization, failed logins are recorded in the event log but no email is sent. |
