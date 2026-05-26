# Module 5 -- Configuration and Deployment

Firmware v2.3.5 | Build 18-05-2026
IDEA Laboratory @ KMUTT

---

## Table of Contents

1. [First-Time Provisioning](#1-first-time-provisioning)
2. [NVS Configuration Keys](#2-nvs-configuration-keys)
3. [Web Portal Configuration](#3-web-portal-configuration)
4. [OTA Update Process](#4-ota-update-process)
5. [MQTT Broker Requirements](#5-mqtt-broker-requirements)
6. [InfluxDB v2 Requirements](#6-influxdb-v2-requirements)
7. [Email SMTP Requirements](#7-email-smtp-requirements)
8. [File System Structure](#8-file-system-structure)
9. [CSV File Formats](#9-csv-file-formats)

---

## 1. First-Time Provisioning

This section describes the complete procedure for flashing and configuring a new XIAO ESP32-C3 board.

### 1.1 Hardware Connections

| Interface | XIAO Pin | Function |
|-----------|----------|----------|
| RS485 RX  | D4       | Modbus sensor data receive |
| RS485 TX  | D10      | Modbus sensor data transmit |
| GSM RX    | D7       | SIM800 UART receive |
| GSM TX    | D6       | SIM800 UART transmit |
| Battery   | A0       | Voltage divider (R1=R2=100 kOhm) |
| TPL5110   | D2       | Done signal (power cut-off) |

### 1.2 Build and Flash

Prerequisites: PlatformIO installed, USB-C cable.

```bash
cd Srisaket_Version
pio run -t upload
```

The board uses the `partitions_ota_4mb.csv` partition table providing two 1.625 MB OTA app slots and a 720 KB LittleFS data partition.

### 1.3 Verify Boot Messages

Open a serial monitor at 115200 baud:

```bash
pio device monitor -b 115200
```

Expected boot output (abbreviated):

```
[WDT] Watchdog initialized
[FW] All-in-One Weather Station v2.3.5
[SERIAL] RS485 UART1 started
[SERIAL] GSM UART0 started
[FS] LittleFS mounted
[BLE] NimBLE client ready (scanner)       # if BLE enabled
[BATT] xxxx mV
===== SETUP COMPLETE =====
```

### 1.4 WiFi AP Connection

After boot, the station enters `STATE_WIFI_AP` and starts a WiFi access point:

| Parameter     | Value              |
|---------------|--------------------|
| SSID          | `WeatherStation_AP` |
| Password      | `12345678`         |
| Channel       | 1                  |
| IP (gateway)  | 192.168.4.1        |
| Min duration  | 60 seconds         |
| Default idle timeout | 5 minutes  |

Connect a computer or phone to the access point, then open a browser to `http://192.168.4.1`.

### 1.5 Web Portal Login

Default credentials:

| Field    | Value   |
|----------|---------|
| Username | `admin` |
| Password | `admin` |

After login, a session cookie (`sid`) is issued with a 10-minute idle timeout. The password should be changed immediately via Settings > Change Password.

### 1.6 Initial Configuration Checklist

1. **GSM APN** -- Hardcoded to `"internet"` in `utilities.h`. Override at compile time if the carrier requires a different APN.
2. **MQTT Broker** -- Settings > MQTT. Enter broker host/IP, port (default 1883), username, and password. Use the Test Server button to verify connectivity.
3. **InfluxDB v2** -- Settings > InfluxDB. Enable, then enter host, port (default 8086), API token, organization, and bucket. Use the Test Server button.
4. **Email** -- Settings > Email. Enter Gmail address (sender), app password, recipient, and notification frequency. Use the Test Email button.
5. **OTA Server** -- Settings > OTA. Enter the base URL of the OTA update server (e.g., `http://host:8889`). Set project name, device type, and download password.
6. **BLE Client** (optional) -- Settings > BLE Client. Enable, scan, and connect to a BLE sensor device.

---

## 2. NVS Configuration Keys

All persistent configuration is stored in the ESP32 NVS (Non-Volatile Storage) under the namespace `"ws-cfg"`. Keys are accessed via the Arduino `Preferences` library. The table below lists every key found in the source code.

### 2.1 Complete Key Reference

| NVS Key | Type | Default Value | Source | Description |
|---------|------|---------------|--------|-------------|
| `bleEnable` | `bool` | `false` | `main_1.cpp` L287, `WifiApServer.cpp` L877 | Enable BLE client scanning and connection |
| `bleSavedMac` | `String` | `""` | `main_1.cpp` L379/L427, `WifiApServer.cpp` L877 | Last connected BLE device MAC address (format `XX:XX:XX:XX:XX:XX`), used for auto-reconnect |
| `lastDailyCsv` | `String` | `"/DATA.csv"` | `main_1.cpp` L794, L1195 | Path to the current daily CSV file |
| `lastota` | `String` | `"Never"` | `main_1.cpp` L757, L704 | Timestamp of the last successful OTA update |
| `otaserver` | `String` | `""` | `WifiApServer.cpp` L807, `main_1.cpp` L640 | OTA server base URL (e.g., `http://host:8889`) |
| `otainterval` | `UInt` | `24` | `WifiApServer.cpp` L812, `main_1.cpp` L641 | Auto-check interval in hours; 0 = disabled |
| `otaboot` | `bool` | `false` | `WifiApServer.cpp` L813, `main_1.cpp` L642 | Check for firmware update on every boot |
| `otaproject` | `String` | `""` | `WifiApServer.cpp` L808, `main_1.cpp` L643 | OTA project identifier sent to server |
| `otadevice` | `String` | `"All-in-One"` | `WifiApServer.cpp` L809, `main_1.cpp` L644 | OTA device type identifier sent to server |
| `otadlpass` | `String` | `""` | `WifiApServer.cpp` L810, `main_1.cpp` L645 | OTA download password (sent as `x-ESP32-password` header) |
| `otapass` | `String` | `"admin"` | `WifiApServer.cpp` L811, L240 | ArduinoOTA password for local WiFi OTA |
| `apTimeout` | `UInt` | `5` | `WifiApServer.cpp` L244, L851 | WiFi AP idle timeout in minutes; 0 = never timeout |
| `webpass` | `String` | `"admin"` | `WifiApServer.cpp` L334 | Web portal login password (min 4 characters) |
| `srcModbus` | `bool` | `true` | `WifiApServer.cpp` L929 | Enable Modbus RS485 sensor data collection |
| `srcBle` | `bool` | `false` | `WifiApServer.cpp` L930 | Enable BLE sensor data collection |
| `fileInterval` | `UInt` | `10` | `WifiApServer.cpp` L948 | Data file rotation interval in minutes (1--1440) |
| `influxEn` | `bool` | `false` | `main_1.cpp` L484, `WifiApServer.cpp` L960 | Enable InfluxDB v2 data push |
| `influxHost` | `String` | `"119.59.103.220"` | `main_1.cpp` L485 | InfluxDB server host or IP |
| `influxPort` | `UInt` | `8086` | `main_1.cpp` L486 | InfluxDB server port |
| `influxToken` | `String` | *(see note)* | `main_1.cpp` L487 | InfluxDB v2 API authentication token |
| `influxOrg` | `String` | `"Pamiang"` | `main_1.cpp` L489 | InfluxDB organization name |
| `influxBucket` | `String` | `"Srisaket_Station_I"` | `main_1.cpp` L490 | InfluxDB bucket name |
| `influxLastSync` | `String` | `"--"` | `WifiApServer.cpp` L966 | Timestamp of last successful InfluxDB sync (display only) |
| `ntpEnable` | `bool` | `true` | `WifiApServer.cpp` L1003 | Enable NTP time sync via GSM |
| `memRollover` | `UInt` | `80` | `WifiApServer.cpp` L1017, `main_1.cpp` L1032 | Storage usage percentage threshold to trigger rollover (10--90) |
| `memRolloverEn` | `bool` | `true` | `WifiApServer.cpp` L1018, `main_1.cpp` L1033 | Enable automatic file rollover when storage threshold is reached |
| `mqttEnable` | `bool` | `true` | `WifiApServer.cpp` L1054 | Enable MQTT data publishing |
| `mqttHost` | `String` | `"119.59.103.220"` | `WifiApServer.cpp` L1055 | MQTT broker host or IP |
| `mqttPort` | `UInt` | `1883` | `WifiApServer.cpp` L1056 | MQTT broker port |
| `mqttUser` | `String` | `"kmutt"` | `WifiApServer.cpp` L1057 | MQTT authentication username |
| `mqttPass` | `String` | `"kmutt@kmutt"` | `WifiApServer.cpp` L1249 | MQTT authentication password |
| `emailEnable` | `bool` | `false` | `WifiApServer.cpp` L1088 | Enable email notifications |
| `emailsmtp` | `String` | `"smtp.gmail.com"` | `WifiApServer.cpp` L1528 | SMTP server hostname |
| `emailport` | `UInt` | `587` | `WifiApServer.cpp` L1267 | SMTP server port |
| `emailuser` | `String` | `""` | `WifiApServer.cpp` L1089 | Email sender address (Gmail) |
| `emailpass` | `String` | `""` | `WifiApServer.cpp` L1264 | Email app-specific password |
| `emailto` | `String` | `""` | `WifiApServer.cpp` L1090 | Primary email recipient address |
| `emaillightto` | `String` | `""` | `WifiApServer.cpp` L1091 | Lightning report recipient address |
| `emailFreqH` | `UInt` | `24` | `WifiApServer.cpp` L1092 | Email notification frequency in hours (1--168) |
| `emailalarm` | `bool` | `false` | `WifiApServer.cpp` L1093 | Send email alert on failed web portal login attempts |

**Note on `influxToken` default:** The source code at `main_1.cpp` line 487 contains a hardcoded default token string. This is a compile-time fallback; it should be replaced with the deployment-specific token via the web portal.

### 2.2 Hardcoded Constants (not in NVS)

These values are defined at compile time in `utilities.h` and cannot be changed at runtime:

| Constant | Value | Description |
|----------|-------|-------------|
| `GSM_APN` | `"internet"` | GPRS access point name |
| `MQTT_TOPIC` | `"weather/Srisaket/Station_1"` | MQTT publish topic |
| `MQTT_PING_TOPIC` | `"weather/Srisaket/Station_1/ping"` | MQTT heartbeat request topic |
| `MQTT_PONG_TOPIC` | `"weather/Srisaket/Station_1/pong"` | MQTT heartbeat response topic |
| `MQTT_CLIENT_ID` | `"PCB_TEST_1"` | MQTT client identifier |
| `WIFI_AP_SSID` | `"WeatherStation_AP"` | AP network name |
| `WIFI_AP_PASSWORD` | `"12345678"` | AP network password |
| `WEB_DEFAULT_USER` | `"admin"` | Portal username (fixed, not stored in NVS) |
| `SERIAL_BAUDRATE` | `115200` | Debug/console serial speed |
| `SERIAL_RS485` | `9600` | Modbus RS485 baud rate |
| `SERIAL_GSM` | `9600` | SIM800 UART baud rate |
| `WDT_TIMEOUT_SEC` | `45` | Hardware watchdog timeout (seconds) |
| `TIME_INCREMENT_MINUTES` | `10` | Minutes to add when NTP fails |

---

## 3. Web Portal Configuration

The web portal is served by a `WebServer` instance on port 80 over the WiFi AP interface. It provides five main sections: Live, Settings, Files, Log, and About.

### 3.1 Navigation Structure

```
/ (root)               -- Redirects to /live or /login
/login                 -- Authentication page (GET/POST)
/logout                -- Clear session, redirect to /login
/live                  -- Real-time sensor dashboard
/api/live              -- JSON API for sensor data (auto-refresh every 30 s)
/api/ble               -- JSON API for BLE data (auto-refresh every 5 s)
/files                 -- LittleFS file browser with download/delete
/download?f=<name>     -- Download a specific file
/settings              -- Configuration panels
/about                 -- Credits and firmware info
/reboot                -- Restart the device
```

### 3.2 Settings Sections

The Settings page is divided into the following cards, each with its own save form and optional test button.

#### 3.2.1 OTA Update (via GSM)

| Field | NVS Key | Input Type | Notes |
|-------|---------|------------|-------|
| OTA Server URL | `otaserver` | URL | Base URL only, e.g., `http://host:8889` |
| Project name | `otaproject` | Text | Identifier sent in `x-ESP32-project` header |
| Device type | `otadevice` | Text | Default `"All-in-One"`; sent in `x-ESP32-device` header |
| Download password | `otadlpass` | Password | Sent in `x-ESP32-password` header; optional |
| ArduinoOTA password | `otapass` | Password | For local PlatformIO OTA over WiFi |
| Auto-check interval | `otainterval` | Number | Hours; 0 disables periodic check |
| Check on boot | `otaboot` | Checkbox | If checked, queries server on every boot |

Actions: **Test Server** (sends GET `/update` via GSM), **Update Now** (triggers immediate OTA cycle).

#### 3.2.2 WiFi AP

| Field | NVS Key | Input Type | Options |
|-------|---------|------------|---------|
| Close AP after idle | `apTimeout` | Select | 1, 5, 10, 15, 30, 60 min, or Never |

The AP idle timer pauses when a WiFi client is connected and resumes only when no clients remain. The minimum AP duration is 60 seconds regardless of this setting.

#### 3.2.3 BLE Client

| Field | NVS Key | Input Type | Notes |
|-------|---------|------------|-------|
| Enable BLE Client | `bleEnable` | Checkbox | Requires restart to take effect |

Additional controls (not form-based):
- **Scan Now** -- triggers a 3-second BLE scan; results shown in a table with Name, MAC, RSSI, and Connect button.
- **Forget** -- clears `bleSavedMac` from NVS, disconnects current device.
- **Connect** -- initiates GATT or NUS connection to selected device MAC.
- **Disconnect** -- drops active BLE connection.

When connected, the BLE device MAC is persisted in `bleSavedMac` for automatic reconnection on the next boot cycle.

#### 3.2.4 Data Sources

| Field | NVS Key | Input Type | Default |
|-------|---------|------------|---------|
| Modbus RS485 | `srcModbus` | Checkbox | Enabled |
| BLE Sensor | `srcBle` | Checkbox | Disabled |

These toggles control which sensor interfaces are active during the data collection state.

#### 3.2.5 File Intervals

| Field | NVS Key | Input Type | Range |
|-------|---------|------------|-------|
| New data file every (minutes) | `fileInterval` | Number | 1--1440 |

Controls the rotation period for daily CSV files. At the configured interval, a new date-stamped CSV file is created.

#### 3.2.6 InfluxDB v2

| Field | NVS Key | Input Type | Default |
|-------|---------|------------|---------|
| Enable InfluxDB | `influxEn` | Checkbox | Disabled |
| Host / IP | `influxHost` | Text | `"119.59.103.220"` |
| Port | `influxPort` | Number | `8086` |
| API Token | `influxToken` | Text | *(compile-time default)* |
| Organization | `influxOrg` | Text | `"Pamiang"` |
| Bucket | `influxBucket` | Text | `"Srisaket_Station_I"` |

Action: **Test Server** (TCP connect test from the AP WiFi interface to the configured host/port).

#### 3.2.7 NTP Sync

| Field | NVS Key | Input Type | Default |
|-------|---------|------------|---------|
| Sync time on GSM connect | `ntpEnable` | Checkbox | Enabled |

When enabled, the station syncs its RTC via NTP (`pool.ntp.org`) or GSM network time during the `STATE_NTP` phase.

The **Sync Now** button (visible when a BLE NUS sniffer is connected) allows overriding the station clock from the BLE device's timestamp.

#### 3.2.8 Internal Memory

| Field | NVS Key | Input Type | Range |
|-------|---------|------------|-------|
| Rollover at (%) | `memRollover` | Range slider | 10--90 |
| Enable rollover | `memRolloverEn` | Checkbox | Enabled |

When LittleFS usage exceeds the configured percentage, the current data file is deleted and recreated with only the last data row preserved as a header anchor. The memory bar is color-coded: green (< 60%), yellow (60--80%), red (> 80%).

#### 3.2.9 MQTT

| Field | NVS Key | Input Type | Default |
|-------|---------|------------|---------|
| Enable MQTT | `mqttEnable` | Checkbox | Enabled |
| Host / IP | `mqttHost` | Text | `"119.59.103.220"` |
| Port | `mqttPort` | Number | `1883` |
| Username | `mqttUser` | Text | `"kmutt"` |
| Password | `mqttPass` | Password | *(unchanged if blank)* |

Password is only written to NVS when a non-empty value is submitted. After saving, the MQTT client configuration is updated at runtime immediately without requiring a reboot.

Action: **Test Server** (TCP connect test).

#### 3.2.10 Email / Gmail Alarm

| Field | NVS Key | Input Type | Default |
|-------|---------|------------|---------|
| Enable email | `emailEnable` | Checkbox | Disabled |
| Gmail Address (sender) | `emailuser` | Email | `""` |
| Gmail App Password | `emailpass` | Password | *(unchanged if blank)* |
| Primary Recipient | `emailto` | Email | `""` |
| Notify every (hours) | `emailFreqH` | Number | `24` (range 1--168) |
| Lightning Report Recipient | `emaillightto` | Email | `""` |
| Login attempt alarm | `emailalarm` | Checkbox | Disabled |

When a Gmail app password is provided, the SMTP server is automatically set to `smtp.gmail.com` on port 587.

Action: **Test Email** (sends a test email via the GSM/SIM800 modem).

#### 3.2.11 Change Password

| Field | Input Type | Validation |
|-------|------------|------------|
| New Password | Password | Min 4 characters |
| Confirm Password | Password | Must match |

Updates the `webpass` NVS key. The username `admin` is fixed and cannot be changed.

---

## 4. OTA Update Process

The firmware supports two OTA update mechanisms: remote OTA via GSM HTTP and local ArduinoOTA via WiFi.

### 4.1 Remote OTA via GSM (Primary)

Remote OTA uses an HTTP-based protocol to download new firmware over the GPRS connection.

#### Server Requirements

The OTA server must implement the following HTTP endpoints:

**GET /update** -- Check for new firmware.

Request headers sent by the device:

| Header | Value | Source |
|--------|-------|--------|
| `Host` | OTA server host | from `otaserver` |
| `x-ESP32-version` | Current firmware version (e.g., `2.3.5`) | `FIRMWARE_VERSION` |
| `x-ESP32-device` | Device type (e.g., `All-in-One`) | `otadevice` |
| `x-ESP32-project` | Project name | `otaproject` |
| `x-ESP32-password` | Download password (if set) | `otadlpass` |

Expected server response codes:

| HTTP Code | Meaning |
|-----------|---------|
| `200` | New firmware available. Response body is the firmware binary. |
| `304` | Firmware is up to date. No download. |
| `401` | Authentication failed (incorrect download password). |

Response headers (when 200):

| Header | Required | Description |
|--------|----------|-------------|
| `Content-Length` | Yes | Firmware binary size in bytes |
| `x-MD5` | No | MD5 checksum for verification |

#### Firmware Download Process

1. Device connects to GSM/GPRS.
2. Sends GET `/update` with version/device/project headers.
3. If server responds `200`, `Update.begin()` is called with the content length.
4. If `x-MD5` header is present, it is set via `Update.setMD5()`.
5. Firmware is streamed in 512-byte chunks with a 5-minute total timeout.
6. Watchdog is fed during download to prevent reset.
7. On successful `Update.end()`, the `lastota` NVS key is updated and the device reboots.
8. If download or write fails, the update is aborted and the current firmware continues running.

#### Automatic Check Triggers

- **Boot check**: If `otaboot` is `true`, the check runs once during `STATE_NTP` on every boot cycle.
- **Periodic check**: If `otainterval` > 0, the check runs when the interval has elapsed since the last check.
- **Manual trigger**: Via the web portal "Update Now" button. This sets a flag (`otaCheckNow`) that causes the main loop to exit the AP state, initialize GSM, and run the OTA check.
- **Both disabled**: If `otaboot` is `false` and `otainterval` is 0, no automatic checks occur.

### 4.2 Local ArduinoOTA via WiFi

While the WiFi AP is active, the device also runs an `ArduinoOTA` server for local firmware uploads over WiFi. This uses the PlatformIO OTA upload mechanism.

```bash
pio run -t upload --upload-port 192.168.4.1
```

The ArduinoOTA password is stored in the `otapass` NVS key (default: `"admin"`).

---

## 5. MQTT Broker Requirements

### 5.1 Protocol

| Parameter | Value |
|-----------|-------|
| Protocol | MQTT v3.1.1 |
| Transport | TCP (unencrypted) |
| Keep Alive | 90 seconds |
| Socket Timeout | 45 seconds |
| Buffer Size | 2048 bytes |
| Publish Timeout | 60 seconds |

### 5.2 Authentication

The device authenticates with username/password credentials stored in NVS (`mqttUser`, `mqttPass`). The client ID is hardcoded as `MQTT_CLIENT_ID` (`"PCB_TEST_1"`).

### 5.3 Topic Structure

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `weather/Srisaket/Station_1` | Device -> Broker | Sensor data (JSON payload) |
| `weather/Srisaket/Station_1/ping` | Broker -> Device | Heartbeat request (reserved) |
| `weather/Srisaket/Station_1/pong` | Device -> Broker | Heartbeat response |

### 5.4 Payload Format

#### Heartbeat (pong topic)

Published once per cycle during GSM initialization:

```json
{
  "n": 0,
  "alive": 1,
  "vt": 3245,
  "heap": 45200,
  "uptime": 125000,
  "gsm_rssi": 15,
  "fw": "2.3.5"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `n` | int | Message type (0 = heartbeat) |
| `alive` | int | Always 1 |
| `vt` | int | Battery voltage in mV |
| `heap` | int | Free heap memory in bytes |
| `uptime` | int | Milliseconds since boot |
| `gsm_rssi` | int | GSM signal quality (dBm) |
| `fw` | string | Firmware version |

#### Sensor Data (data topic)

The `buildCompactJSON()` function produces a payload with the following structure:

```json
{
  "n": 1,
  "seq": 42,
  "vt": 3245,
  "srs": 15,
  "d": "260526",
  "r": [
    {
      "d": "260526",
      "t": "0830",
      "sh": 450,
      "st": 258,
      "se": 320,
      "ph": 65,
      "sn": 120,
      "sp": 45,
      "sk": 180,
      "ws": 15,
      "wd": 270,
      "ah": 780,
      "at": 295,
      "co2": 420,
      "pr": 1013,
      "il": 35000,
      "rf": 20,
      "so": 450
    }
  ],
  "sniffer": {
    "mac": "AA:BB:CC:DD:EE:FF",
    "name": "SnifferPortal",
    "ts": "2026-05-26 08:30:00",
    "temp": "28.5",
    "hum": "75.2",
    "tmp117": "27.8",
    "delta": "0.7",
    "rain": "0.0",
    "leaf": "0",
    "par": "350",
    "soil": "42.5"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `n` | int | Message type (1 = sensor data) |
| `seq` | int | Monotonically increasing sequence number |
| `vt` | int | Battery voltage (mV) |
| `srs` | int | GSM signal quality |
| `d` | string | Date as `DDMMYY` |
| `r[]` | array | Array of sensor readings |
| `r[].d` | string | Reading date `DDMMYY` |
| `r[].t` | string | Reading time `HHMM` |
| `r[].sh` | int | Soil humidity (raw, divide by 10 for %) |
| `r[].st` | int | Soil temperature (raw, divide by 10 for deg C) |
| `r[].se` | int | Soil EC (uS/cm) |
| `r[].ph` | int | Soil pH (raw, divide by 10) |
| `r[].sn` | int | Soil nitrogen (mg/kg) |
| `r[].sp` | int | Soil phosphorus (mg/kg) |
| `r[].sk` | int | Soil potassium (mg/kg) |
| `r[].ws` | int | Wind speed (raw, divide by 10 for m/s) |
| `r[].wd` | int | Wind direction (degrees) |
| `r[].ah` | int | Air humidity (raw, divide by 10 for %) |
| `r[].at` | int | Air temperature (raw, divide by 10 for deg C) |
| `r[].co2` | int | CO2 concentration (ppm) |
| `r[].pr` | int | Atmospheric pressure (raw, divide by 10 for kPa) |
| `r[].il` | int | Illuminance (lux) |
| `r[].rf` | int | Rainfall (raw, divide by 10 for mm) |
| `r[].so` | int | Solar radiation (W/m2) |
| `sniffer` | object | Optional; present only when BLE NUS device is connected |
| `sniffer.mac` | string | BLE sniffer MAC address |
| `sniffer.name` | string | BLE sniffer device name |

Messages are published with `retain = false`.

---

## 6. InfluxDB v2 Requirements

### 6.1 Prerequisites

- InfluxDB v2 server accessible from the GSM network.
- An API token with write permission to the target organization and bucket.
- A bucket created for the station data (default: `Srisaket_Station_I`).

### 6.2 API Endpoint

```
POST /api/v2/write?org=<org>&bucket=<bucket>&precision=ms HTTP/1.1
Host: <host>
Authorization: Token <token>
Content-Type: text/plain; charset=utf-8
Connection: close
Content-Length: <length>
```

Organization and bucket names are URL-encoded (spaces replaced with `%20`).

### 6.3 Line Protocol Format

#### weather_station Measurement

```
weather_station,station=Srisaket soil_humi=45.0,soil_temp=25.8,soil_ec=320,soil_ph=6.5,soil_N=120,soil_P=45,soil_K=180,wind_speed=1.5,wind_dir=270,air_humi=78.0,air_temp=29.5,co2=420,pressure=101.3,illuminance=35000,rainfall=2.0,solar=450,battery=3.245,gsm_rssi=15
```

| Tag | Value |
|-----|-------|
| `station` | `Srisaket` |

| Field | Unit | Description |
|-------|------|-------------|
| `soil_humi` | % | Soil moisture |
| `soil_temp` | deg C | Soil temperature |
| `soil_ec` | uS/cm | Electrical conductivity |
| `soil_ph` | pH | Soil pH |
| `soil_N` | mg/kg | Nitrogen |
| `soil_P` | mg/kg | Phosphorus |
| `soil_K` | mg/kg | Potassium |
| `wind_speed` | m/s | Wind speed |
| `wind_dir` | degrees | Wind direction |
| `air_humi` | % | Air humidity |
| `air_temp` | deg C | Air temperature |
| `co2` | ppm | CO2 concentration |
| `pressure` | kPa | Atmospheric pressure |
| `illuminance` | lux | Light intensity |
| `rainfall` | mm | Rainfall |
| `solar` | W/m2 | Solar radiation |
| `battery` | V | Battery voltage |
| `gsm_rssi` | dBm | GSM signal strength |

#### sniffer_watchdog Measurement (BLE NUS)

When a BLE NUS sniffer device is connected, an additional measurement is appended:

```
sniffer_watchdog,station=Srisaket,mac=AA:BB:CC:DD:EE:FF temp=28.5,hum=75.2,tmp117=27.8,delta=0.7,rain=0.0,leaf=0,par=350,soil=42.5
```

### 6.4 Transmission

Data is sent over a raw TCP connection using `TinyGsmClient`. The connection is attempted up to 3 times with 2-second delays between retries. The response is parsed for an HTTP status code: `204` indicates success, `4xx`/`5xx` indicates an error.

---

## 7. Email SMTP Requirements

Email is sent via the SIM800 modem's built-in AT command email extensions. The SIM800 must support the `+SMTP*` command set.

### 7.1 Server Configuration

| Parameter | Default | NVS Key |
|-----------|---------|---------|
| SMTP Server | `smtp.gmail.com` | `emailsmtp` |
| SMTP Port | `587` | `emailport` |
| Username | *(user Gmail address)* | `emailuser` |
| Password | *(Gmail app password)* | `emailpass` |
| Sender Name | `"WeatherStation"` | hardcoded |

Gmail users must generate an app-specific password (standard Google account requirement for non-OAuth SMTP).

### 7.2 AT Command Sequence

The `sendEmail()` method in `GsmHandler.cpp` executes the following AT command sequence:

```
AT+EMAILCID=1                    -- Use GPRS context 1
AT+EMAILTO=30                    -- 30-second timeout
AT+SMTPSRV="smtp.gmail.com",587  -- Set SMTP server and port
AT+SMTPAUTH=1,"user","pass"      -- Enable SMTP authentication
AT+SMTPFROM="user","WeatherStation"  -- Set sender
AT+SMTPRCPT=0,0,"recipient","Recipient"  -- Add recipient
AT+SMTPSUBJECT="subject"         -- Set email subject
AT+SMTPBODY=<length>             -- Declare body length, enter data mode
<body text>                      -- Send body content
AT+SMTPSEND                      -- Trigger send
```

The device waits up to 30 seconds for `+SMTPSEND: 1` (success). If the response is not received, the send is considered failed.

### 7.3 Email Alarm

When `emailalarm` is enabled and a failed web portal login attempt occurs, the device automatically sends an email containing the attempted username, client IP address, and timestamp. This email is sent immediately via the GSM modem during the login request handling.

---

## 8. File System Structure

### 8.1 Partition Layout

The flash is divided according to `partitions_ota_4mb.csv`:

| Partition | Type | Offset | Size | Purpose |
|-----------|------|--------|------|---------|
| `nvs` | data/nvs | 0x9000 | 20 KB | Non-volatile configuration |
| `otadata` | data/ota | 0xe000 | 8 KB | OTA slot selection |
| `app0` | app/ota_0 | 0x10000 | 1.625 MB | Firmware slot A |
| `app1` | app/ota_1 | 0x1B0000 | 1.625 MB | Firmware slot B |
| `spiffs` | data/spiffs | 0x350000 | 720 KB | LittleFS data storage |

### 8.2 LittleFS Directory Layout

```
/
├── DATA.csv                       # Legacy/initial data file
├── DD-MM-YYYY.csv                 # Daily sensor data files (e.g., 26-05-2026.csv)
├── BLE-DD-MM-YYYY.csv             # Daily BLE sensor data files (e.g., BLE-26-05-2026.csv)
├── BLE-data.csv                   # Fallback BLE data (when date unavailable)
├── Event-DD-MM-YYYY.csv           # Daily event log files
├── DATA_TEMP_SWAP.csv             # Temporary swap file during line removal
└── DATA_CLEAR_SWAP.csv            # Temporary swap file during data clearing
```

### 8.3 Storage Management

| Parameter | Value | Description |
|-----------|-------|-------------|
| Minimum free space | 10,000 bytes | `saveData()` refuses to write if below this |
| Maximum single file | 500 KB | Warning logged if exceeded |
| Rollover threshold | 80% (configurable) | Triggers file deletion and recreation |
| Rollover behavior | Keep last row | Last data row is preserved as anchor after rollover |

The rollover mechanism deletes the current daily CSV, creates a new file with the standard header, and appends the last row from the deleted file.

---

## 9. CSV File Formats

### 9.1 Sensor Data CSV

**Filename pattern:** `DD-MM-YYYY.csv` (e.g., `26-05-2026.csv`), or `DATA.csv` as fallback.

**Header row:**
```csv
Date,Time,Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,WindSpeed,WindDirection,Air_Humidity,Air_Temperature,CO2,Pressure,Illuminance,Rainfall,Solar
```

**Data row format:**
```csv
DD/MM/YYYY,HH:MM:SS,<sh>,<st>,<ec>,<ph>,<N>,<P>,<K>,<ws>,<wd>,<ah>,<at>,<co2>,<pr>,<il>,<rf>,<so>
```

**Example:**
```csv
Date,Time,Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,WindSpeed,WindDirection,Air_Humidity,Air_Temperature,CO2,Pressure,Illuminance,Rainfall,Solar
26/05/2026,08:30:00,45.0,25.8,320,6.5,120,45,180,1.5,270,78.0,29.5,420,101.3,35000,2.0,450
26/05/2026,08:40:00,44.5,25.9,315,6.4,118,44,175,1.8,275,77.5,29.7,415,101.2,35500,2.1,460
```

**Column reference:**

| Column | Format | Unit | Notes |
|--------|--------|------|-------|
| Date | `DD/MM/YYYY` | -- | Zero-padded |
| Time | `HH:MM:SS` | -- | 24-hour, zero-padded |
| Soil_Humidity | `float` | % | One decimal place |
| Soil_Temperature | `float` | deg C | One decimal place, can be negative |
| EC | `int` | uS/cm | Electrical conductivity |
| PH | `float` | pH | One decimal place |
| N | `int` | mg/kg | Nitrogen |
| P | `int` | mg/kg | Phosphorus |
| K | `int` | mg/kg | Potassium |
| WindSpeed | `float` | m/s | One decimal place |
| WindDirection | `int` | degrees | 0--359 |
| Air_Humidity | `float` | % | One decimal place |
| Air_Temperature | `float` | deg C | One decimal place, can be negative |
| CO2 | `int` | ppm | CO2 concentration |
| Pressure | `float` | kPa | One decimal place |
| Illuminance | `int` | lux | Light intensity |
| Rainfall | `float` | mm | One decimal place |
| Solar | `int` | W/m2 | Solar radiation |

Lines are terminated with `\r\n`.

### 9.2 BLE Sensor Data CSV

**Filename pattern:** `BLE-DD-MM-YYYY.csv` (e.g., `BLE-26-05-2026.csv`), or `BLE-data.csv` as fallback.

**Header row:**
```csv
Date,Time,Temperature(C),Humidity(%),TMP117(C),DeltaT(C),Rainfall,LeafWetness,PAR,SoilMoisture
```

**Example:**
```csv
Date,Time,Temperature(C),Humidity(%),TMP117(C),DeltaT(C),Rainfall,LeafWetness,PAR,SoilMoisture
26/05/2026,08:30:15,28.5,75.2,27.8,0.7,0.0,0,350,42.5
26/05/2026,08:40:22,28.7,74.8,27.9,0.8,0.0,0,355,42.3
```

**Column reference:**

| Column | Format | Source Key | Description |
|--------|--------|------------|-------------|
| Date | `DD/MM/YYYY` | `ts` | Parsed from `YYYY-MM-DD` timestamp |
| Time | `HH:MM:SS` | `ts` | Parsed from timestamp |
| Temperature(C) | `float` | `temp` | Air temperature from BLE sensor |
| Humidity(%) | `float` | `hum` | Relative humidity from BLE sensor |
| TMP117(C) | `float` | `tmp117` | TMP117 precision temperature |
| DeltaT(C) | `float` | `delta` | Temperature delta (air - leaf) |
| Rainfall | `float` | `rain` | Rainfall measurement |
| LeafWetness | `int` | `leaf` | Leaf wetness index |
| PAR | `int` | `par` | Photosynthetically Active Radiation |
| SoilMoisture | `float` | `soil` | Soil moisture from BLE sensor |

Duplicate timestamps are detected and skipped to prevent repeated entries from NUS notifications.

### 9.3 Event Log CSV

**Filename pattern:** `Event-DD-MM-YYYY.csv` (e.g., `Event-26-05-2026.csv`).

**Header row:**
```csv
DateTime,Event
```

**Example:**
```csv
DateTime,Event
26/05/2026 08:25:00,Login SUCCESS user=admin ip=192.168.4.2
26/05/2026 08:25:15,BLE Client settings updated
26/05/2026 08:26:30,OTA settings updated
26/05/2026 08:30:00,OTA update requested via web portal
26/05/2026 08:35:00,Login FAILED user=root ip=192.168.4.5
26/05/2026 08:35:05,Email alarm sent
```

**Event types logged:**

| Event | Trigger |
|-------|---------|
| `Login SUCCESS user=X ip=Y` | Successful web portal login |
| `Login FAILED user=X ip=Y` | Failed web portal login |
| `Email alarm sent` | Alarm email dispatched after failed login |
| `User logout` | Web portal logout |
| `BLE Client settings updated` | BLE enable/disable saved |
| `BLE connect request: <MAC>` | BLE connection initiated |
| `BLE disconnected` | BLE device disconnected |
| `BLE forget saved device` | Saved MAC cleared |
| `Data sources updated` | Modbus/BLE source toggles changed |
| `File intervals updated` | File rotation interval changed |
| `InfluxDB settings updated` | InfluxDB configuration changed |
| `NTP settings updated` | NTP toggle changed |
| `NTP synced from BLE` | Time overridden from BLE NUS device |
| `Memory settings updated` | Rollover threshold changed |
| `MQTT settings updated` | MQTT configuration changed |
| `Email settings updated` | Email configuration changed |
| `OTA settings updated` | OTA configuration changed |
| `OTA update requested via web portal` | Manual OTA trigger |
| `Password changed` | Web portal password updated |
| `AP timeout updated` | WiFi AP timeout changed |
| `Delete file <path>` | Data file deleted via portal |
| `Delete ALL data files` | Bulk file deletion |
| `Manual reboot via web UI` | Device rebooted from portal |

---

*End of Module 5*
