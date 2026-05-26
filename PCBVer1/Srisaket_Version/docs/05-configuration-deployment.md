# Module 5 -- Configuration & Deployment

This module covers first-time provisioning, NVS configuration keys, the web portal settings interface, OTA firmware updates, and the external service requirements (MQTT, InfluxDB, Email). File system layout and CSV data formats are documented at the end.

*Credits: IDEA Laboratory @ KMUTT*

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

Follow these steps to flash and configure a new weather station board.

### Prerequisites

- PlatformIO installed (`pio` command available)
- USB cable connected to the XIAO ESP32-C3 board
- SIM card inserted in the SIM800L modem slot
- RS485 sensors connected (soil sensor, weather sensor)

### Step-by-Step Procedure

**Step 1 -- Connect USB**

Connect the XIAO ESP32-C3 to your computer via USB. The board should enumerate as a serial port.

**Step 2 -- Build and upload firmware**

```bash
cd Srisaket_Version
pio run -t upload
```

This compiles the firmware and writes it to the ESP32-C3 flash. The board configuration in `platformio.ini` targets `seeed_xiao_esp32c3` with the OTA partition scheme `partitions_ota_4mb.csv`.

**Step 3 -- Open serial monitor**

```bash
pio device monitor
```

The monitor baud rate is 115200. You should see boot output immediately.

**Step 4 -- Verify successful boot**

Look for the following two lines in the serial output:

```
[FW] All-in-One Weather Station v2.3.5
===== SETUP COMPLETE =====
```

The first line confirms firmware version. The second confirms that all subsystems (RS485 serial, GSM serial, LittleFS, BLE) initialized without fatal errors.

**Step 5 -- Connect to WiFi AP**

After setup completes, the device creates a WiFi access point:

| Parameter | Value |
|---|---|
| SSID | `WeatherStation_AP` |
| Password | `12345678` |
| Channel | 1 |
| Default IP | `192.168.4.1` |

Connect to this AP from your laptop or phone.

**Step 6 -- Open the web portal**

Browse to:

```
http://192.168.4.1
```

You will be redirected to the login page.

**Step 7 -- Log in**

Default credentials:

| Field | Default |
|---|---|
| Username | `admin` |
| Password | `admin` |

Change these immediately after first login (see [Section 3 -- Password](#3-web-portal-configuration)).

**Step 8 -- Configure GSM APN**

Navigate to the web portal settings and verify that the GSM APN is set correctly. The default APN is `internet`, which is suitable for most Thai mobile carriers (AIS, DTAC, TrueMove). Adjust if your SIM card provider requires a different APN.

**Step 9 -- Configure external services**

Set up at least one data destination:

- **MQTT broker** -- host, port, username, password
- **InfluxDB v2** -- host, port, token, org, bucket
- **Email** -- SMTP server, credentials, recipient

See sections 5, 6, and 7 below for service requirements.

**Step 10 -- Save settings**

All settings changes are persisted to NVS immediately upon saving. No separate "apply" or "reboot" step is needed for configuration changes (though BLE enable/disable requires a restart).

---

## 2. NVS Configuration Keys

All configuration is stored in the ESP32 NVS (Non-Volatile Storage) under the namespace `ws-cfg`. The web portal and firmware read and write these keys through the Arduino `Preferences` library.

### Core Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `bleEnable` | bool | `false` | Enable BLE client functionality (requires restart) |
| `bleSavedMac` | string | `""` | Saved BLE device MAC address for auto-reconnect |
| `lastDailyCsv` | string | `"/DATA.csv"` | Current daily CSV filename path |
| `lastota` | string | `"Never"` | Timestamp of last successful OTA update |
| `otaserver` | string | `""` | OTA firmware server base URL (e.g. `http://host:8889`) |
| `otainterval` | uint | `24` | OTA check interval in hours; 0 disables periodic checks |
| `otaboot` | bool | `false` | Check for OTA update on every boot |
| `otaproject` | string | `""` | OTA project identifier sent to server |
| `otadevice` | string | `"All-in-One"` | OTA device identifier sent to server |
| `otadlpass` | string | `""` | OTA download password (sent as `x-ESP32-password` header) |
| `otapass` | string | `"admin"` | ArduinoOTA password for local WiFi OTA |
| `influxEn` | bool | `false` | Enable InfluxDB v2 data upload |
| `influxHost` | string | `"119.59.103.220"` | InfluxDB server address |
| `influxPort` | uint | `8086` | InfluxDB server port |
| `influxToken` | string | *(token)* | InfluxDB v2 authentication token |
| `influxOrg` | string | `"Pamiang"` | InfluxDB organization name |
| `influxBucket` | string | `"Srisaket_Station_I"` | InfluxDB bucket name |
| `influxLastSync` | string | `"--"` | Timestamp of last successful InfluxDB write |
| `memRollover` | uint | `80` | Storage usage percentage threshold for rollover |
| `memRolloverEn` | bool | `true` | Enable automatic storage rollover when threshold is exceeded |
| `apTimeout` | uint | `5` | WiFi AP idle timeout in minutes; 0 = never timeout |
| `fileInterval` | uint | `10` | Data save interval in minutes |
| `webpass` | string | `"admin"` | Web portal login password |
| `ntpEnable` | bool | `true` | Enable NTP time sync on GSM connect |
| `srcModbus` | bool | `true` | Enable Modbus RS485 sensor reading |
| `srcBle` | bool | `false` | Enable BLE sensor data collection |

### MQTT Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `mqttEnable` | bool | `true` | Enable MQTT publishing |
| `mqttHost` | string | `"119.59.103.220"` | MQTT broker address |
| `mqttPort` | uint | `1883` | MQTT broker port |
| `mqttUser` | string | `"kmutt"` | MQTT username |
| `mqttPass` | string | `"kmutt@kmutt"` | MQTT password |

The MQTT client ID is hardcoded as `MQTT_CLIENT_ID` (`"PCB_TEST_1"`) in `utilities.h`.

### Email Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `emailEnable` | bool | `false` | Enable email notifications |
| `emailuser` | string | `""` | Email sender address (Gmail) |
| `emailpass` | string | `""` | Email sender password (Gmail app password) |
| `emailsmtp` | string | `"smtp.gmail.com"` | SMTP server address |
| `emailport` | uint | `587` | SMTP server port |
| `emailto` | string | `""` | Primary email recipient |
| `emaillightto` | string | `""` | Lightning report recipient |
| `emailFreqH` | uint | `24` | Email notification frequency in hours |
| `emailalarm` | bool | `false` | Send email on failed login attempts |

### Reading NVS Keys Programmatically

NVS keys are accessed through the Arduino `Preferences` library:

```cpp
Preferences nvs;
nvs.begin("ws-cfg", true);  // true = read-only
String value = nvs.getString("influxHost", "119.59.103.220");
bool enabled = nvs.getBool("influxEn", false);
nvs.end();
```

For the complete set of keys and their save handlers, see `src/WifiApServer.cpp` (settings handlers) and `src/main_1.cpp` (runtime reads).

---

## 3. Web Portal Configuration

The web portal is accessible at `http://192.168.4.1` while the WiFi AP is active. It provides a dark-themed, mobile-responsive interface for all station configuration.

### Settings Page Sections

The Settings page is organized into the following card sections:

#### OTA Update (via GSM)

| Field | Description |
|---|---|
| OTA Server URL | Base URL of the firmware server (e.g. `http://host:8889`) |
| Project name | Project identifier sent in the OTA check request |
| Device type | Device identifier (default: `All-in-One`) |
| Download password | Password for authenticated OTA downloads |
| ArduinoOTA password | Password for local WiFi-based OTA via ArduinoIDE |
| Auto-check interval | Hours between automatic OTA checks; 0 disables |
| Check on boot | Checkbox to check for updates on every boot |

Actions: **Save OTA**, **Test Server** (checks connectivity), **Update Now** (triggers immediate OTA).

#### WiFi AP

| Field | Description |
|---|---|
| AP idle timeout | Dropdown: 1, 5, 10, 15, 30, 60 minutes, or Never |

The AP closes automatically when no clients are connected for the selected duration. Connecting a client resets the countdown.

#### BLE Client

| Field | Description |
|---|---|
| Enable BLE Client | Checkbox to enable/disable BLE scanning and connection |
| Saved device | Shows the currently saved BLE MAC (if any) |
| Scan results | Table of discovered BLE devices with Name, MAC, RSSI |

Actions: **Save**, **Scan Now**, **Connect** (per device), **Forget** (remove saved MAC).

Note: Enabling/disabling BLE requires a device restart to take effect.

#### Data Sources

| Field | Description |
|---|---|
| Modbus RS485 | Checkbox to enable/disable RS485 sensor reading |
| BLE Sensor | Checkbox to enable/disable BLE sensor data collection |

Both sources can be enabled simultaneously.

#### File Intervals

| Field | Description |
|---|---|
| Data save interval | Number input, 1--1440 minutes (default: 10) |

Controls how often sensor data is written to the daily CSV file.

#### InfluxDB v2

| Field | Description |
|---|---|
| Enable InfluxDB | Checkbox to enable/disable InfluxDB uploads |
| Host / IP | InfluxDB server address |
| Port | InfluxDB server port (default: 8086) |
| API Token | InfluxDB v2 authentication token |
| Organization | InfluxDB organization |
| Bucket | InfluxDB bucket name |

Displays the timestamp of the last successful sync. Actions: **Save**, **Test Server**.

#### NTP Sync

| Field | Description |
|---|---|
| Sync time on GSM connect | Checkbox to enable/disable NTP time synchronization |

When enabled, the device syncs its clock from NTP each time the GSM connection is established.

#### Internal Memory

| Field | Description |
|---|---|
| Rollover threshold | Slider, 10--90% (default: 80%) |
| Enable rollover | Checkbox to enable automatic storage management |

Displays a usage bar showing current LittleFS usage. When rollover is enabled and storage exceeds the threshold, the current daily CSV is deleted and recreated with only the most recent data row preserved.

#### MQTT

| Field | Description |
|---|---|
| Enable MQTT | Checkbox to enable/disable MQTT publishing |
| Host / IP | MQTT broker address |
| Port | MQTT broker port (default: 1883) |
| Username | MQTT authentication username |
| Password | MQTT authentication password (blank = unchanged) |

Actions: **Save**, **Test Server**.

#### Email / Gmail Alarm

| Field | Description |
|---|---|
| Enable email notifications | Checkbox to enable/disable email alerts |
| Gmail Address (sender) | Sender email address |
| Gmail App Password | Sender email app password |
| Primary Recipient | Default email recipient |
| Notify every (hours) | Email frequency, 1--168 hours |
| Lightning Report Recipient | Optional recipient for lightning alerts |
| Login attempt alarm | Send email on failed web portal login attempts |

Actions: **Save Email**, **Test Email** (sends a test message via GSM).

#### Change Password

| Field | Description |
|---|---|
| New Password | Minimum 4 characters |
| Confirm Password | Must match new password |

Changes the web portal login password immediately. The username is fixed as `admin`.

---

## 4. OTA Update Process

The firmware supports Over-The-Air updates downloaded via the GSM connection. This allows remote firmware deployment without physical access to the device.

### Trigger Methods

**Automatic -- on boot**

If the NVS key `otaboot` is `true`, the firmware checks the OTA server on every boot.

**Automatic -- periodic**

If `otainterval` is non-zero, the firmware checks the OTA server at the configured hour interval (default: 24 hours).

**Manual -- web portal**

Navigate to Settings -- OTA Update section, then click **Update Now**. This triggers an immediate OTA check via GSM.

### OTA Server Protocol

The OTA server must host firmware at the `/update` endpoint. The device sends an HTTP GET request with the following custom headers:

```
GET /update HTTP/1.1
Host: <server>
Connection: close
x-ESP32-version: 2.3.5
x-ESP32-device: All-in-One
x-ESP32-project: <project>
x-ESP32-password: <download_password>
```

The server responds with one of:

| HTTP Status | Meaning |
|---|---|
| `200 OK` | New firmware available. Response body contains the binary. Must include `x-md5` header with the firmware MD5 checksum. |
| `304 Not Modified` | Firmware is already up to date. |
| `401 Unauthorized` | Download password is incorrect. |

### Download and Flash Process

1. Device connects to OTA server via GSM TCP socket (mux 1)
2. Sends HTTP GET with version and device headers
3. If 200, initializes the ESP32 OTA partition with `Update.begin(contentLen)`
4. Sets MD5 from the `x-md5` response header
5. Downloads firmware in 512-byte chunks
6. Writes each chunk directly to the OTA partition via `Update.write()`
7. Feeds the watchdog timer during download
8. After download completes, calls `Update.end(true)` to finalize
9. Writes the current timestamp to `lastota` NVS key
10. Reboots into the new firmware

If the download or write fails at any point, the update is aborted and the device continues running the current firmware.

---

## 5. MQTT Broker Requirements

The weather station publishes sensor data to an MQTT broker via the GSM connection using the TinyGSM library and PubSubClient.

### Protocol Requirements

| Requirement | Value |
|---|---|
| MQTT version | 3.1.1 |
| Default port | 1883 (non-TLS, plain TCP) |
| Authentication | Username/password |
| Client ID | `PCB_TEST_1` (hardcoded in `utilities.h`) |

### Topic Structure

The MQTT topic hierarchy follows the pattern `weather/<location>/<station_id>`:

```
weather/Srisaket/Station_1          -- sensor data (compact JSON)
weather/Srisaket/Station_1/ping     -- heartbeat request from server
weather/Srisaket/Station_1/pong     -- heartbeat response from device
```

These topics are defined as macros in `utilities.h`:

```cpp
#define MQTT_TOPIC      "weather/Srisaket/Station_1"
#define MQTT_PING_TOPIC "weather/Srisaket/Station_1/ping"
#define MQTT_PONG_TOPIC "weather/Srisaket/Station_1/pong"
```

### Data Payload Format

Sensor data is published as compact JSON to `MQTT_TOPIC`:

```json
{
  "n": 1,
  "seq": 42,
  "vt": 3300,
  "srs": -75,
  "d": "260526",
  "r": [
    [14, 30, 45.5, -3.0, 350, 6.8, 25, 30, 40, 1.2, 180, 75.0, 32.5, 410, 1013.0, 25000, 0.0, 350]
  ]
}
```

Heartbeat (pong) responses are published to `MQTT_PONG_TOPIC`:

```json
{
  "n": 0,
  "alive": 1,
  "vt": 3300,
  "heap": 45000,
  "uptime": 120,
  "gsm_rssi": -75,
  "fw": "2.3.5"
}
```

### Broker Configuration

Ensure the MQTT broker:

- Accepts connections on port 1883 (or the port configured in NVS)
- Has user accounts created matching `mqttUser` / `mqttPass`
- Permits publish and subscribe on the `weather/#` topic hierarchy
- Has sufficient message size limits (sensor payloads can exceed 1 KB with multiple records)

---

## 6. InfluxDB v2 Requirements

The station writes sensor data directly to InfluxDB v2 via its HTTP API using the GSM modem's TCP connection.

### Server Requirements

| Requirement | Detail |
|---|---|
| API endpoint | `/api/v2/write` |
| Precision | Millisecond (`precision=ms` query parameter) |
| Authentication | `Authorization: Token <token>` header |
| Content type | `text/plain; charset=utf-8` |
| HTTP method | POST |

### Write URL

```
POST /api/v2/write?org=<org>&bucket=<bucket>&precision=ms HTTP/1.1
Host: <host>
Authorization: Token <token>
Content-Type: text/plain; charset=utf-8
Content-Length: <len>
Connection: close
```

### Line Protocol Format

The station uses InfluxDB line protocol with the following measurements:

**`weather_station` measurement** -- RS485 sensor data:

```
weather_station,station=Srisaket soil_humi=45.5,soil_temp=-3.0,soil_ec=350,soil_ph=6.8,soil_N=25,soil_P=30,soil_K=40,wind_speed=1.2,wind_dir=180,air_humi=75.0,air_temp=32.5,co2=410,pressure=1013.0,illuminance=25000,rainfall=0.0,solar=350,battery=3.30,gsm_rssi=-75
```

**`sniffer_watchdog` measurement** -- BLE sniffer data (appended when a BLE NUS device is connected):

```
sniffer_watchdog,station=Srisaket,mac=AA:BB:CC:DD:EE:FF temp=25.3,hum=65.2,tmp117=24.8,delta=0.5,rain=0,leaf=2.1,par=450,soil=35.6
```

### Prerequisites

Before enabling InfluxDB uploads:

1. The InfluxDB v2 server must be running and accessible from the GSM network
2. The organization must be created in InfluxDB
3. The bucket must be created under that organization
4. An API token with write permissions to the target bucket must be generated
5. Configure `influxHost`, `influxPort`, `influxToken`, `influxOrg`, and `influxBucket` in the web portal

### Connection Behavior

The station makes up to 3 TCP connection attempts per write cycle. If all attempts fail, the write is skipped and the station continues normal operation. Successful write timestamps are stored in the `influxLastSync` NVS key.

---

## 7. Email SMTP Requirements

The station sends email notifications via the SIM800L modem's built-in SMTP client using AT commands.

### SMTP Server Requirements

| Requirement | Detail |
|---|---|
| Protocol | SMTP with AUTH LOGIN |
| Default port | 587 |
| Authentication | Username/password |
| TLS/SSL | Handled by SIM800L modem firmware (if supported) |

### AT Command Flow

The email sending process uses the following SIM800 AT commands:

1. `AT+EMAILCID=1` -- Bind email to GPRS context
2. `AT+EMAILTO=30` -- Set email timeout to 30 seconds
3. `AT+SMTPSRV="<server>",<port>` -- Configure SMTP server
4. `AT+SMTPAUTH=1,"<user>","<pass>"` -- Set authentication credentials
5. `AT+SMTPFROM="<user>","WeatherStation"` -- Set sender
6. `AT+SMTPRCPT=0,0,"<to>","Recipient"` -- Set recipient
7. `AT+SMTPSUBJECT="<subject>"` -- Set subject line
8. `AT+SMTPBODY=<length>` -- Initialize body, then stream content
9. `AT+SMTPSEND` -- Send the email

### Email Triggers

The station sends emails in these scenarios:

- **Periodic data reports** -- At the configured `emailFreqH` interval (default: 24 hours)
- **Lightning alerts** -- To the `emaillightto` recipient (if configured)
- **Login alarm** -- Failed web portal login attempts trigger an email to the primary recipient (if `emailalarm` is `true`)

### Gmail Configuration

For Gmail users, the web portal automatically sets:

- SMTP server: `smtp.gmail.com`
- SMTP port: `587`
- Authentication: Gmail address + App Password (not the account password)

You must generate a Gmail App Password at `https://myaccount.google.com/apppasswords` before configuring email.

---

## 8. File System Structure

The station uses LittleFS on the ESP32-C3 flash for persistent data storage. The partition scheme is defined in `partitions_ota_4mb.csv`.

### Directory Layout

```
/
├── DD-MM-YYYY.csv            Daily Modbus RS485 sensor data
├── BLE-DD-MM-YYYY.csv        Daily BLE sensor data (from connected BLE devices)
├── Event-DD-MM-YYYY.csv      Daily event log
├── /DATA_TEMP_SWAP.csv       Temporary file for CSV row removal (auto-managed)
└── /DATA_CLEAR_SWAP.csv      Temporary file for CSV data clearing (auto-managed)
```

### File Naming Convention

- **Sensor data**: `/<DD>-<MM>-<YYYY>.csv` (e.g., `/26-05-2026.csv`)
- **BLE data**: `/BLE-<DD>-<MM>-<YYYY>.csv` (e.g., `/BLE-26-05-2026.csv`)
- **Event logs**: `/Event-<DD>-<MM>-<YYYY>.csv` (e.g., `/Event-26-05-2026.csv`)

Date values in filenames are zero-padded (e.g., `05` for May, `03` for the 3rd).

### Storage Management

- **Minimum free space**: 10,000 bytes (defined as `MIN_FREE_SPACE_BYTES`)
- **Maximum file size**: 500 KB per file (defined as `MAX_FILE_SIZE_BYTES`)
- **Rollover threshold**: Default 80% of total LittleFS capacity

When rollover is enabled and storage usage exceeds the threshold:

1. The last data line from the current daily CSV is preserved
2. The daily CSV file is deleted
3. A new daily CSV is created with the header row and the preserved last line
4. This ensures continuity of data while freeing storage space

### File Creation

New daily CSV files are created automatically at the start of each day (when the date changes) or when the firmware starts and the current day's file does not exist. Each file begins with a header row followed by data rows.

---

## 9. CSV File Formats

All CSV files use CRLF (`\r\n`) line endings and are encoded in UTF-8.

### Sensor Data CSV

**Filename**: `/<DD>-<MM>-<YYYY>.csv`

**Header**:
```csv
Date,Time,Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,WindSpeed,WindDirection,Air_Humidity,Air_Temperature,CO2,Pressure,Illuminance,Rainfall,Solar
```

**Data row example**:
```csv
26/05/2026,14:30:00,45.5,-3.0,350,6.8,25,30,40,1.2,180,75.0,32.5,410,1013.0,25000,0.0,350
```

**Field reference**:

| Column | Unit | Format | Source |
|---|---|---|---|
| Date | DD/MM/YYYY | String | System clock (NTP or incremental) |
| Time | HH:MM:SS | String | System clock |
| Soil_Humidity | % | Float (1 decimal) | Soil sensor, Modbus register 0 |
| Soil_Temperature | degrees C | Float (1 decimal, can be negative) | Soil sensor, Modbus register 1 |
| EC | uS/cm | Integer | Soil sensor, Modbus register 2 |
| PH | pH | Float (1 decimal) | Soil sensor, Modbus register 3 |
| N | mg/kg | Integer | Soil sensor, Modbus register 4 |
| P | mg/kg | Integer | Soil sensor, Modbus register 5 |
| K | mg/kg | Integer | Soil sensor, Modbus register 6 |
| WindSpeed | m/s | Float (1 decimal) | Weather sensor, Modbus |
| WindDirection | degrees | Integer (0--360) | Weather sensor, Modbus |
| Air_Humidity | %RH | Float (1 decimal) | Weather sensor, Modbus |
| Air_Temperature | degrees C | Float (1 decimal) | Weather sensor, Modbus |
| CO2 | ppm | Integer | Weather sensor, Modbus |
| Pressure | hPa | Float (1 decimal) | Weather sensor, Modbus |
| Illuminance | lux | Integer | Weather sensor, Modbus |
| Rainfall | mm | Float (1 decimal) | Weather sensor, Modbus |
| Solar | W/m2 | Integer | Weather sensor, Modbus |

### BLE Data CSV

**Filename**: `/BLE-<DD>-<MM>-<YYYY>.csv`

**Header**:
```csv
Date,Time,Temperature(C),Humidity(%),TMP117(C),DeltaT(C),Rainfall,LeafWetness,PAR,SoilMoisture
```

**Data row example**:
```csv
26/05/2026,14:30:00,25.3,65.2,24.8,0.5,0,2.1,450,35.6
```

**Field reference**:

| Column | Unit | Description |
|---|---|---|
| Date | DD/MM/YYYY | Date parsed from BLE JSON timestamp |
| Time | HH:MM:SS | Time parsed from BLE JSON timestamp |
| Temperature(C) | degrees C | Air temperature from BLE sensor |
| Humidity(%) | %RH | Relative humidity from BLE sensor |
| TMP117(C) | degrees C | Precision temperature from TMP117 sensor |
| DeltaT(C) | degrees C | Temperature delta (air - surface) |
| Rainfall | mm | Rainfall measurement from BLE device |
| LeafWetness | unitless | Leaf wetness index from BLE device |
| PAR | umol/m2/s | Photosynthetically Active Radiation |
| SoilMoisture | % | Soil moisture from BLE device |

BLE data is received as JSON via the Nordic UART Service (NUS) and parsed into CSV rows. Duplicate timestamps are detected and skipped to prevent data duplication.

### Event Log CSV

**Filename**: `/Event-<DD>-<MM>-<YYYY>.csv`

Event logs are auto-generated by the web portal and firmware. Each entry contains a timestamp followed by a description of the event. Events include:

- Web portal logins (success and failure)
- Settings changes (which section was modified)
- File operations (delete, download)
- BLE events (scan, connect, disconnect, data received)
- OTA events (check, update, error)
- System events (boot, GSM connect, NTP sync)

Event log entries follow this format:

```csv
26/05/2026,14:30:00,Login SUCCESS user=admin ip=192.168.4.2
26/05/2026,14:32:00,OTA settings updated
26/05/2026,14:35:00,BLE connect request: AA:BB:CC:DD:EE:FF
```

---

*This concludes Module 5 -- Configuration & Deployment. Refer to Module 6 for troubleshooting and maintenance procedures.*
