# Firmware Technical Manual -- All-in-One Weather Station (Srisaket Version)

**Document ID:** WS-SKT-TRM-001
**Firmware:** v2.3.6 | **Build:** 26-05-2026 | **Revision:** A
**Platform:** Seeed XIAO ESP32-C3 | **Framework:** Arduino (PlatformIO)
**Organization:** IDEA Laboratory @ KMUTT

---

## Revision History

| Rev | Date | Author | Description |
|-----|------|--------|-------------|
| A | 2026-05-26 | -- | Initial release for firmware v2.3.6. Consolidates and supersedes Modules 1--6. |

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [System Requirements](#2-system-requirements)
3. [Hardware/Software Interface](#3-hardwaresoftware-interface)
4. [Software Architecture](#4-software-architecture)
5. [Module Design](#5-module-design)
6. [Communication Protocols](#6-communication-protocols)
7. [Data Storage and Management](#7-data-storage-and-management)
8. [OTA Firmware Update](#8-ota-firmware-update)
9. [Configuration Management](#9-configuration-management)
10. [Error Handling and Recovery](#10-error-handling-and-recovery)
11. [Deployment and Maintenance](#11-deployment-and-maintenance)

Appendix A: Modbus Register Maps
Appendix B: MQTT JSON Payload Schema
Appendix C: BLE NUS JSON Protocol
Appendix D: NVS Configuration Key Reference

---

## 1. System Overview

### 1.1 Purpose and Scope

The Srisaket Weather Station is a battery-powered, duty-cycled environmental monitoring
device. It wakes on a hardware timer, reads local sensors, uploads data over GSM/GPRS,
and powers itself back down within a single duty cycle typically lasting 2--5 minutes.

### 1.2 Primary Functions

| Function | Description |
|----------|-------------|
| Soil sensing | 7-in-1 RS-485: humidity, temperature, EC, pH, N, P, K |
| Weather sensing | 9-in-1 RS-485: wind speed/direction, air temp/humidity, CO2, pressure, illuminance, rainfall, solar |
| BLE gateway | Nordic UART Service (NUS) client for Sniffer Portal BLE devices |
| MQTT batch publish | Compact JSON with auto-chunking for SIM800L payload limits |
| InfluxDB v2 upload | Line-protocol write over GSM/GPRS (per-reading mode) |
| Local storage | LittleFS CSV files with automatic rollover |
| WiFi AP portal | Web configuration, live data, BLE management, OTA trigger |
| Remote OTA | HTTP firmware update over GSM |
| Email alerts | SMTP email for login-failure alarms via SIM800 AT commands |

### 1.3 Key Specifications

| Parameter | Value |
|-----------|-------|
| MCU | ESP32-C3 (RISC-V, single-core, 160 MHz) |
| Flash | 4 MB (dual-OTA + 720 KB LittleFS) |
| Firmware version | 2.3.6 |
| Framework | Arduino (PlatformIO) |
| BLE stack | NimBLE |
| WDT timeout | 45 seconds |
| Debug baud | 115200 |
| Duty cycle | 2--5 minutes (TPL5110 timer-controlled) |

### 1.4 External Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| `4-20ma/ModbusMaster` | ^2.0.1 | Modbus RTU master |
| `knolleary/PubSubClient` | ^2.8 | MQTT client |
| `vshymanskyy/TinyGSM` | ^0.12.0 | SIM800 abstraction |
| `h2zero/NimBLE-Arduino` | ^1.4.2 | BLE 5.0 client |

---

## 2. System Requirements

### 2.1 Functional Requirements

| ID | Requirement |
|----|-------------|
| FR-01 | Read soil sensor (7 parameters) via Modbus RTU RS-485 with median-of-3 filtering |
| FR-02 | Read weather sensor (9 parameters) via Modbus RTU RS-485 with median-of-3 filtering |
| FR-03 | Connect to BLE NUS devices and parse JSON sensor payloads |
| FR-04 | Store all sensor data (RS-485 + BLE) to LittleFS CSV with date-stamped filenames |
| FR-05 | Batch-publish stored data via MQTT with auto-chunking to fit SIM800L payload limits |
| FR-06 | Optionally push data to InfluxDB v2 via line protocol over HTTP |
| FR-07 | Provide WiFi AP web portal for configuration, live monitoring, and file management |
| FR-08 | Synchronize time via GSM RTC or NTP with CSV-timestamp fallback |
| FR-09 | Support remote OTA firmware update over GSM HTTP |
| FR-10 | Monitor battery voltage and cut power below 3200 mV threshold |
| FR-11 | Pulse TPL5110 DONE pin to cut system power at end of each cycle |

### 2.2 Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| NFR-01 | Complete one measurement cycle within 5 minutes |
| NFR-02 | Survive 45-second WDT timeout in any blocking operation |
| NFR-03 | Handle RS-485 bus errors with automatic UART recovery |
| NFR-04 | Handle GSM network failures with reconnection logic (120 s timeout) |
| NFR-05 | Manage LittleFS storage with rollover at configurable threshold |
| NFR-06 | MQTT payload per chunk shall not exceed 950 bytes (SIM800L limit) |

### 2.3 Constraints

| Constraint | Value | Reason |
|------------|-------|--------|
| SIM800L TCP payload | ~1024 bytes | Modem hardware limit; firmware uses 950-byte ceiling |
| Battery cutoff | < 3200 mV | Prevents deep discharge of Li-ion cell |
| TPL5110 power gating | External resistor-set interval | Not firmware-configurable |
| LittleFS partition | 720 KB | Flash partition layout fixed at compile time |
| BLE/WiFi shared radio | Single 2.4 GHz radio | Coexistence managed by ESP32-C3 RF layer |

---

## 3. Hardware/Software Interface

### 3.1 MCU Specifications

| Parameter | Value |
|-----------|-------|
| Core | ESP32-C3, RISC-V single-core, 32-bit, 160 MHz |
| Flash | 4 MB external quad-SPI |
| SRAM | 400 KB |
| Bluetooth | BLE 5 (NimBLE stack) |
| Wi-Fi | 2.4 GHz 802.11 b/g/n |
| USB | USB-C (CDC serial + programming) |

### 3.2 Partition Layout

| Partition | Type | Offset | Size | Purpose |
|-----------|------|--------|------|---------|
| nvs | data/nvs | 0x9000 | 20 KB | NVS key-value store |
| otadata | data/ota | 0xE000 | 8 KB | OTA slot selector |
| app0 | app/ota_0 | 0x10000 | 1.625 MB | Firmware slot A |
| app1 | app/ota_1 | 0x1B0000 | 1.625 MB | Firmware slot B |
| spiffs | data/spiffs | 0x350000 | 720 KB | LittleFS data storage |

### 3.3 Pin Mapping

| Pin | GPIO | Function | Direction |
|-----|------|----------|-----------|
| D4 | 4 | RS-485 UART1 RX | Input |
| D10 | 10 | RS-485 UART1 TX | Output |
| D7 | 7 | GSM UART0 RX | Input |
| D6 | 6 | GSM UART0 TX | Output |
| D2 | 2 | TPL5110 DONE signal | Output |
| A0 | 0 | Battery voltage ADC | Input |

### 3.4 UART Allocation

| UART | Peripheral | Baud | Pins |
|------|-----------|------|------|
| UART0 | SIM800 GSM | 9600 | D7 (RX), D6 (TX) |
| UART1 | RS-485 Modbus | 9600 | D4 (RX), D10 (TX) |
| USB CDC | Debug | 115200 | USB-C |

UART0 is remapped to D7/D6 for the GSM modem. USB CDC is a separate endpoint and does not conflict.

### 3.5 Battery Voltage Divider

```
Vbat ---[ R1=100k ]---+--- A0 (ADC)
                       |
                 [ R2=100k ]
                       |
                      GND
```

- Divider ratio: (R1+R2)/R2 = 2.0
- Sampling: 64-sample average, calibration factor 1.000
- Low battery cutoff: 3200 mV

### 3.6 Power Architecture

```
Battery (3.0--4.2V) --> TPL5110 Timer --> Voltage Regulator --> 3.3V rail
       |                    |                                         |
       +--> R1/R2 --> A0    +<-- DONE (D2) <---- ESP32-C3            |
                           (TPL5110 cuts power when DONE asserted)    |
                                                                   +--> SIM800 (3.4--4.4V)
```

The TPL5110 removes all power between cycles. No deep sleep is used. Only continuous draw: TPL5110 quiescent current (~35 nA) and voltage divider (~15--21 uA).

---

## 4. Software Architecture

### 4.1 Module Dependency Graph

```
                     utilities.h (pins, constants, defaults)
                           ^
                           | included by all
    +----------------------+---------------------+
    |                      |                     |
sensor_v2             GsmHandler            WifiApServer
(Modbus + battery)    (GSM + MQTT)          (Web portal)
    ^                      ^                     ^
    |                      |                     |
    +--- Memory -----------+                     |
    |   (LittleFS CSV)     |                     |
    +----------------------+---------------------+
    |
    v
 main_1.cpp (state machine, BLE, InfluxDB, OTA)
```

### 4.2 File Map

| Source | Lines | Responsibility |
|--------|-------|---------------|
| `utilities.h` | ~110 | Compile-time constants, pin defs, defaults |
| `sensor_v2.cpp/.h` | ~382/~110 | Modbus RTU reads, median filter, battery ADC |
| `GsmHandler.cpp/.h` | ~320/~59 | SIM800 init, GPRS, MQTT, NTP, email |
| `Memory.cpp/.h` | ~348/~42 | LittleFS CSV operations, rollover, batch read |
| `WifiApServer.cpp/.h` | ~1643/~152 | WiFi AP web portal (30+ routes) |
| `main_1.cpp` | ~1472 | State machine, BLE client, InfluxDB, OTA, JSON builder |

### 4.3 State Machine

The firmware operates as a single-threaded state machine in `loop()`. Each state has a
maximum timeout; on expiry the machine advances to the fallback state.

```
  STATE_WIFI_AP (min 1 min, max 5 min)
       |
       v
  STATE_GSM_INIT (120 s timeout)
       |
       v
  STATE_NTP (30 s timeout)
       |
       v
  STATE_WEATHER (15 s settle + 30 s timeout)
       |
       v
  STATE_SOIL (15 s settle + 30 s timeout)
       |
       v
  STATE_SAVE (10 s timeout)
       |
       v
  STATE_RECONNECT (120 s timeout)
       |
       v
  STATE_PUBLISH (60 s timeout)
       |
       v
  STATE_FINISH (pulse TPL5110 DONE, infinite loop)
```

#### State Reference Table

| State | Purpose | Timeout | Fallback | Exit Condition |
|-------|---------|---------|----------|----------------|
| `STATE_WIFI_AP` | WiFi AP + BLE portal | 300 s | `GSM_INIT` | BLE data received (>=1 min), AP idle (>=1 min), OTA request (anytime), hard timeout |
| `STATE_GSM_INIT` | Init SIM800, GPRS | 120 s | `NTP` | Init + connect succeed or fail |
| `STATE_NTP` | Sync time | 30 s | `WEATHER` | GSM RTC / NTP / CSV fallback + OTA check |
| `STATE_WEATHER` | Read weather Modbus | 30 s | `SOIL` | Read success or timeout |
| `STATE_SOIL` | Read soil Modbus | 30 s | `SAVE` | Read success or timeout |
| `STATE_SAVE` | CSV + InfluxDB write | 10 s | `RECONNECT` | Save complete |
| `STATE_RECONNECT` | GSM reconnect if needed | 120 s | `FINISH` | GSM connected, or skip if not needed |
| `STATE_PUBLISH` | MQTT batch publish | 60 s | `FINISH` | Publish complete |
| `STATE_FINISH` | Power off via TPL5110 | None | -- | DONE pulse sent |

#### STATE_WIFI_AP Exit Logic

Before 60 seconds: only OTA request can exit.
After 60 seconds: BLE data received, AP idle timeout, or hard timeout (5 min).

#### STATE_RECONNECT Logic

- If `rtPubMode != 0` (InfluxDB mode): skip to `STATE_FINISH`
- If temp file has fewer than `rtPubBatchSize` records: skip to `STATE_FINISH`
- If GSM still connected: advance to `STATE_PUBLISH`
- If GSM down: attempt reconnect (restart modem, re-init, re-connect) within 120 s

#### STATE_PUBLISH Logic

- If `rtPubMode != 0`: skip to `STATE_FINISH`
- Count temp file records; if below batch threshold, skip
- Build compact JSON, auto-chunk to fit SIM800L 950-byte limit
- Publish each chunk via MQTT; remove published records from temp file

### 4.4 Data Structures

#### SensorData (33 bytes, packed)

| Offset | Field | Type | Scaling | Unit |
|--------|-------|------|---------|------|
| 0 | soil_humi | uint16_t | /10.0 | % |
| 2 | soil_temp | int16_t | /10.0 | C |
| 4 | soil_ec | uint16_t | raw | uS/cm |
| 6 | soil_ph | uint8_t | /10.0 | pH |
| 8 | soil_N | uint16_t | raw | mg/kg |
| 10 | soil_P | uint16_t | raw | mg/kg |
| 12 | soil_K | uint16_t | raw | mg/kg |
| 14 | windSpeed | uint16_t | /10.0 | m/s |
| 16 | windDir_Deg | uint16_t | raw | deg |
| 18 | air_humidity | uint16_t | /10.0 | % |
| 20 | air_temperature | int16_t | /10.0 | C |
| 22 | CO2 | uint16_t | raw | ppm |
| 24 | pressure | uint16_t | /10.0 | kPa |
| 26 | illuminance | uint32_t | raw | lux |
| 30 | rainfall | uint16_t | /10.0 | mm |
| 32 | solar | uint16_t | raw | W/m2 |

#### BleSensorData (16 bytes, packed)

| Field | Type | Scaling | Description |
|-------|------|---------|-------------|
| ble_temp | int16_t | /10.0 | Temperature (C) |
| ble_humi | uint16_t | /10.0 | Humidity (%) |
| ble_tmp117 | int16_t | /10.0 | TMP117 (C) |
| ble_delta | int16_t | /10.0 | Delta T (C) |
| ble_rain | uint16_t | raw | Rainfall |
| ble_leaf | uint16_t | raw | Leaf wetness |
| ble_par | uint16_t | raw | PAR light |
| ble_soil | uint16_t | raw | Soil moisture |

#### DataRecord (packed)

| Field | Type | Description |
|-------|------|-------------|
| date, month, year, hour, minute | uint8_t x5 | Timestamp |
| data | SensorData | Sensor readings |
| ble | BleSensorData | BLE readings |
| ble_valid | uint8_t | 1 if BLE data present |
| valid | uint8_t | 1 if record is valid |

---

## 5. Module Design

### 5.1 RS485sensor (sensor_v2)

**Read pipeline:** 5-stage noise rejection.

1. **Median of 3**: Three successful Modbus reads per sensor; median per register rejects outliers
2. **Retry 5x**: Up to `maxRetry` (5) failed Modbus transactions before giving up
3. **Zero-retry**: If critical fields are zero after first pass, re-read up to 5 more times
4. **UART recovery**: On error 0xE0 (InvalidSlaveID), full UART restart (end + begin + settle)
5. **Consecutive failure zero-out**: After 3 consecutive total failures, zero out the sensor section

**Write pipeline:** `writeSingleRegister()` with 5 retries.

### 5.2 Memory

| Method | Purpose |
|--------|---------|
| `write()` | Create file with content |
| `append()` | Append row to existing file |
| `saveData()` | Serialize SensorData + BleSensorData to CSV row |
| `countDataLines()` | Count data rows (excluding header) |
| `readDataRecords()` | Parse CSV rows back into DataRecord structs |
| `removeFirstDataLines()` | Remove published rows via swap file |
| `isUsageOverThreshold()` | Check LittleFS usage percentage |

### 5.3 GsmHandler

| Method | Purpose |
|--------|---------|
| `init()` | Initialize SIM800, check SIM |
| `connectNetwork()` | Register network, open GPRS |
| `getNetworkTime()` | Get time via GSM RTC or NTP |
| `mqttConnect()` | Connect to MQTT broker with retry |
| `mqttPublish()` | Publish with post-delivery drain loop |
| `sendEmail()` | SMTP via SIM800 AT commands |
| `setMqttConfig()` | Update MQTT parameters at runtime |

### 5.4 WifiApServer

HTTP web server with 30+ routes. Key subsystems:
- Session-based authentication (16-char hex token, 10-min timeout)
- Live data JSON APIs (`/api/live`, `/api/ble`)
- BLE scan/connect/disconnect UI
- File browser with download/delete
- Settings forms for all configurable parameters
- Test endpoints for MQTT, InfluxDB, Email, OTA

### 5.5 BLE Client (in main_1.cpp)

Two modes:
- **NUS (Nordic UART Service)**: Subscribe to TX notifications, accumulate bytes, parse JSON
- **GATT generic**: Service discovery, read all readable characteristics

Auto-reconnect: saved MAC in NVS, automatically connects on next boot.

---

## 6. Communication Protocols

### 6.1 Modbus RTU (RS-485)

- Function code: 0x03 (Read Holding Registers), 0x06 (Write Single Register)
- Baud: 9600, 8N1
- Post-transmission delay: 500 us (normal), 1000 us (after recovery)

See Appendix A for register maps.

### 6.2 MQTT

| Parameter | Value |
|-----------|-------|
| Broker | 119.59.103.220:1883 (configurable via NVS) |
| Client ID | PCB_TEST_1 |
| Buffer | 2048 bytes |
| Keep-alive | 90 s |
| QoS | 0 (fire-and-forget) |
| Max payload per chunk | 950 bytes |

**Topics:**

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `weather/Srisaket/Station_1` | Device->Broker | Sensor data (compact JSON) |
| `weather/Srisaket/Station_1/ping` | Broker->Device | Heartbeat request (reserved) |
| `weather/Srisaket/Station_1/pong` | Device->Broker | Heartbeat response |

See Appendix B for payload schema.

### 6.3 BLE Nordic UART Service (NUS)

| UUID | Role |
|------|------|
| `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | Service |
| `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | TX (notify, sniffer->gateway) |
| `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | RX (write, gateway->sniffer) |

Gateway sends `"live"` to RX to request data push. Notifications accumulate in 400-byte
buffer; parser extracts last complete JSON object.

See Appendix C for NUS JSON schema.

### 6.4 InfluxDB v2

Line-protocol POST over raw TCP (TinyGsmClient). Two measurements:
- `weather_station`: soil + weather + battery + GSM RSSI
- `sniffer_watchdog`: BLE NUS data (appended when connected)

### 6.5 WiFi AP

| Parameter | Value |
|-----------|-------|
| SSID | WeatherStation_AP |
| Password | 12345678 |
| Channel | 1 |
| IP | 192.168.4.1 |
| Auth | Session cookie (`sid`), 10-min timeout |

### 6.6 SMTP Email

Sent via SIM800 AT commands (`+SMTP*` series). Requires Gmail App Password.

### 6.7 NTP

`AT+CNTP="pool.ntp.org",0` with 60-second timeout. Fallback to GSM RTC, then CSV timestamp + 10 min.

---

## 7. Data Storage and Management

### 7.1 File Naming

| Pattern | Purpose |
|---------|---------|
| `/DD-MM-YYYY.csv` | Daily sensor data (soil + weather + BLE) |
| `/DATA_TEMP.csv` | Buffer for MQTT batch publish |
| `/BLE-DD-MM-YYYY.csv` | BLE sniffer data |
| `/BLE-data.csv` | BLE fallback (no date) |
| `/Event-DD-MM-YYYY.csv` | Event log |

### 7.2 CSV Format (Main Sensor)

```
Date,Time,Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,
WindSpeed,WindDirection,Air_Humidity,Air_Temperature,CO2,Pressure,Illuminance,Rainfall,Solar,
BLE_Temp,BLE_Humi,BLE_TMP117,BLE_DeltaT,BLE_Rain,BLE_Leaf,BLE_PAR,BLE_Soil
```

Example:
```csv
26/05/2026,08:30:00,45.0,25.8,320,6.5,120,45,180,1.5,270,78.0,29.5,420,101.3,35000,2.0,450,28.5,75.2,27.8,0.7,0.0,0,350,42.5
```

### 7.3 CSV Format (BLE Sniffer)

```
Date,Time,Temperature(C),Humidity(%),TMP117(C),DeltaT(C),Rainfall,LeafWetness,PAR,SoilMoisture
```

### 7.4 Memory Rollover

When LittleFS usage exceeds configurable threshold (default 80%):
1. Preserve last data row from current daily CSV
2. Delete the daily CSV
3. Recreate with header + preserved row

### 7.5 Batch MQTT Publish Flow

```
Sensor data saved to:
  - Daily CSV (persistent archive)
  - DATA_TEMP.csv (publish buffer)

STATE_RECONNECT: check if GSM connected
STATE_PUBLISH:
  1. Count records in DATA_TEMP.csv
  2. If >= rtPubBatchSize (default 6):
     a. Read records into DataRecord array
     b. Build compact JSON, chunk to fit 950-byte limit
     c. Publish each chunk via MQTT
     d. Remove published records from temp file
     e. Keep unpublished records for next cycle
```

---

## 8. OTA Firmware Update

### 8.1 Remote OTA (GSM HTTP)

**Request:**
```
GET /update HTTP/1.1
Host: <ota_server>
x-ESP32-version: 2.3.6
x-ESP32-device: All-in-One
x-ESP32-project: <project>
x-ESP32-password: <password>
```

**Response codes:**

| Code | Action |
|------|--------|
| 200 | Download firmware (512-byte chunks), verify MD5, flash, reboot |
| 304 | Already up to date |
| 401 | Auth failed, abort |

### 8.2 Trigger Conditions

- Boot check (`otaboot` NVS key)
- Periodic check (`otainterval` NVS key, default 24 h)
- Manual trigger from web portal (`otaCheckNow` flag)

### 8.3 Local ArduinoOTA

Available during WiFi AP state: `pio run -t upload --upload-port 192.168.4.1`

---

## 9. Configuration Management

### 9.1 NVS Namespace

All persistent configuration stored in namespace `"ws-cfg"` via Arduino `Preferences`.

### 9.2 Runtime Configuration

Loaded from NVS at each cycle start via `loadRuntimeConfig()`:

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `soilSlaveId` | UChar | 0x01 | Soil sensor Modbus slave address |
| `weathSlaveId` | UChar | 0x02 | Weather sensor Modbus slave address |
| `pubBatchSize` | UChar | 6 | Minimum records before MQTT publish |
| `pubMode` | UChar | 0 | 0=MQTT batch, 1=InfluxDB per-reading |

### 9.3 Complete NVS Key Reference

See Appendix D.

---

## 10. Error Handling and Recovery

### 10.1 State Timeout Chain

Each state has a timeout and fallback state. On timeout, the machine auto-advances,
ensuring the device never hangs permanently in one state.

### 10.2 GSM Reconnection

`STATE_RECONNECT` attempts to restart the modem and re-register on the network
within 120 seconds. WDT is fed throughout. If reconnection fails, temp data is
preserved for the next cycle.

### 10.3 RS-485 Recovery

- Error 0xE0: Full UART restart (end + begin + settle + flush)
- Other errors: 1000 ms delay, re-init ModbusMaster
- Consecutive failures (3): Zero sensor section to prevent stale data

### 10.4 WDT Protection

45-second hardware WDT, fed in all blocking loops:
- GSM network wait (every 2 s)
- NTP sync (every 2 s)
- MQTT connect/publish
- OTA download (per chunk)
- BLE operations (per characteristic)
- File operations (every 16 lines)
- `STATE_FINISH` infinite loop (every 1 s)

### 10.5 Low Battery Cutoff

If `batteryVoltage < 3200 mV` at boot, the firmware pulses TPL5110 DONE immediately
and enters an infinite WDT-feeding loop to prevent operation on depleted battery.

---

## 11. Deployment and Maintenance

### 11.1 Build and Flash

```bash
cd Srisaket_Version
pio run -t upload
pio device monitor -b 115200
```

### 11.2 First-Time Configuration

1. Connect to WiFi AP: SSID `WeatherStation_AP`, password `12345678`
2. Open `http://192.168.4.1`, login with admin/admin
3. Configure MQTT, InfluxDB, Email, OTA, BLE as needed
4. Change web portal password

### 11.3 Factory Reset

```bash
pio run -t erase   # Clear NVS + LittleFS
pio run -t upload   # Reflash firmware
```

### 11.4 Debug Serial Prefixes

| Prefix | Subsystem |
|--------|-----------|
| `[FW]` | Firmware version |
| `[WDT]` | Watchdog |
| `[GSM]` | GSM modem |
| `[NTP]` | Time sync |
| `[MQTT]` | MQTT publish |
| `[INFLUX]` | InfluxDB |
| `[MODBUS]` | RS-485 errors |
| `[BLE]` | BLE client |
| `[BLE-CSV]` | BLE CSV storage |
| `[SAVE]` | Data persistence |
| `[OTA]` | OTA update |
| `[WiFi]` | WiFi AP |
| `[FS]` | LittleFS |
| `[BATT]` | Battery |
| `[TMO]` | State timeout |
| `[DONE]` | TPL5110 power-off |

### 11.5 Known Limitations

| Limitation | Details |
|------------|---------|
| Time accuracy without GSM | Incremental time from CSV + 10 min; no RTC battery |
| BLE/WiFi shared radio | Single 2.4 GHz radio; coexistence during STATE_WIFI_AP |
| GPRS bandwidth | ~85 kbps; OTA download of ~1.3 MB takes 2--5 min |
| NUS buffer | 400 bytes; larger JSON payloads will be truncated |
| Leap year | `incrementTime()` does not account for leap years |
| No MQTT resume | Failed publish retains data in temp file for next cycle |

---

## Appendix A: Modbus Register Maps

### A.1 Soil Sensor -- Slave 0x01 (default, configurable via `soilSlaveId`)

Starting address: 0x0000, register count: 7.

| Reg | Index | Field | Type | Scaling | Unit |
|-----|-------|-------|------|---------|------|
| 0x0000 | [0] | soil_humi | uint16 | /10.0 | % |
| 0x0001 | [1] | soil_temp | int16 | /10.0 | C |
| 0x0002 | [2] | soil_ec | uint16 | raw | uS/cm |
| 0x0003 | [3] | soil_ph | uint16 | clamped 255, /10.0 | pH |
| 0x0004 | [4] | soil_N | uint16 | raw | mg/kg |
| 0x0005 | [5] | soil_P | uint16 | raw | mg/kg |
| 0x0006 | [6] | soil_K | uint16 | raw | mg/kg |

### A.2 Weather Sensor -- Slave 0x02 (default, configurable via `weathSlaveId`)

Starting address: 0x01F4 (500), register count: 16.

| Reg | Index | Field | Type | Scaling | Unit |
|-----|-------|-------|------|---------|------|
| 0x01F4 | [0] | windSpeed | uint16 | /10.0 | m/s |
| 0x01F5 | [1] | *(reserved)* | -- | -- | -- |
| 0x01F6 | [2] | *(reserved)* | -- | -- | -- |
| 0x01F7 | [3] | windDir_Deg | uint16 | raw | deg |
| 0x01F8 | [4] | air_humidity | uint16 | /10.0 | % |
| 0x01F9 | [5] | air_temperature | int16 | /10.0 | C |
| 0x01FA | [6] | *(reserved)* | -- | -- | -- |
| 0x01FB | [7] | CO2 | uint16 | raw | ppm |
| 0x01FC | [8] | *(reserved)* | -- | -- | -- |
| 0x01FD | [9] | pressure | uint16 | /10.0 | kPa |
| 0x01FE | [10] | illuminance (hi) | uint32 | (hi<<16)\|lo | lux |
| 0x01FF | [11] | illuminance (lo) | -- | -- | lux |
| 0x0200 | [12] | *(reserved)* | -- | -- | -- |
| 0x0201 | [13] | rainfall | uint16 | /10.0 | mm |
| 0x0202 | [14] | *(reserved)* | -- | -- | -- |
| 0x0203 | [15] | solar | uint16 | raw | W/m2 |

---

## Appendix B: MQTT JSON Payload Schema

### B.1 Heartbeat (pong topic)

```json
{"n":0,"alive":1,"vt":3650,"heap":54200,"uptime":142,"gsm_rssi":15,"fw":"2.3.6"}
```

| Field | Type | Description |
|-------|------|-------------|
| n | int | 0 = heartbeat |
| alive | int | Always 1 |
| vt | int | Battery voltage (mV) |
| heap | int | Free heap (bytes) |
| uptime | int | Seconds since boot |
| gsm_rssi | int | GSM signal quality |
| fw | string | Firmware version |

### B.2 Sensor Data (data topic)

```json
{
  "n":1,"seq":42,"vt":3650,"srs":15,"d":"260526",
  "r":[
    {
      "d":"260526","t":"0830",
      "sh":452,"st":268,"se":320,"ph":68,"sn":42,"sp":15,"sk":38,
      "ws":25,"wd":180,"ah":785,"at":312,"co2":410,"pr":1013,"il":32000,"rf":0,"so":340,
      "bt":285,"bh":752,"b117":278,"bd":7,"br":0,"blf":0,"bp":350,"bs":425
    }
  ]
}
```

#### Header fields

| Field | Description |
|-------|-------------|
| n | 1 = data record |
| seq | Monotonically increasing sequence number |
| vt | Battery voltage (mV) |
| srs | GSM signal quality |
| d | Date as DDMMYY |

#### Per-record fields

| Field | Source | Scaling | Unit |
|-------|--------|---------|------|
| d | date | DDMMYY | -- |
| t | time | HHMM | -- |
| sh | soil_humi | /10 | % |
| st | soil_temp | /10 | C |
| se | soil_ec | raw | uS/cm |
| ph | soil_ph | /10 | pH |
| sn | soil_N | raw | mg/kg |
| sp | soil_P | raw | mg/kg |
| sk | soil_K | raw | mg/kg |
| ws | windSpeed | /10 | m/s |
| wd | windDir_Deg | raw | deg |
| ah | air_humidity | /10 | % |
| at | air_temperature | /10 | C |
| co2 | CO2 | raw | ppm |
| pr | pressure | /10 | kPa |
| il | illuminance | raw | lux |
| rf | rainfall | /10 | mm |
| so | solar | raw | W/m2 |

#### BLE fields (per record, when ble_valid=1)

| Field | Source | Scaling | Description |
|-------|--------|---------|-------------|
| bt | ble_temp | /10 | BLE temperature (C) |
| bh | ble_humi | /10 | BLE humidity (%) |
| b117 | ble_tmp117 | /10 | TMP117 temperature (C) |
| bd | ble_delta | /10 | Delta T (C) |
| br | ble_rain | raw | BLE rainfall |
| blf | ble_leaf | raw | BLE leaf wetness |
| bp | ble_par | raw | BLE PAR light |
| bs | ble_soil | raw | BLE soil moisture |

---

## Appendix C: BLE NUS JSON Protocol

### C.1 Payload Format

The BLE NUS sniffer device sends JSON objects via notifications:

```json
{
  "ts":"2026-05-26 08:30:00",
  "temp":"26.5",
  "hum":"78.2",
  "tmp117":"25.8",
  "delta":"0.7",
  "rain":"0",
  "leaf":"234",
  "par":"450",
  "soil":"35.2"
}
```

| Key | Description |
|-----|-------------|
| ts | Timestamp YYYY-MM-DD HH:MM:SS |
| temp | Temperature (C) |
| hum | Humidity (%) |
| tmp117 | TMP117 temperature (C) |
| delta | Delta T (C) |
| rain | Rainfall |
| leaf | Leaf wetness |
| par | PAR light |
| soil | Soil moisture |

### C.2 Gateway Commands

| Command | Sent to RX characteristic | Purpose |
|---------|--------------------------|---------|
| `"live"` | 4 bytes, no null | Request immediate data push |

### C.3 Accumulator

- 400-byte buffer (`bleNusAccum`)
- Parser scans for last `{` before `}` to extract complete JSON
- Duplicate timestamps detected and skipped in CSV output

---

## Appendix D: NVS Configuration Key Reference

All keys in namespace `"ws-cfg"`, accessed via Arduino `Preferences`.

### D.1 Runtime Sensor Config

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `soilSlaveId` | UChar | 0x01 | Soil Modbus slave address |
| `weathSlaveId` | UChar | 0x02 | Weather Modbus slave address |
| `pubBatchSize` | UChar | 6 | Min records for MQTT batch |
| `pubMode` | UChar | 0 | 0=MQTT batch, 1=InfluxDB per-reading |

### D.2 BLE

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `bleEnable` | Bool | false | Enable BLE client |
| `bleSavedMac` | String | "" | Auto-reconnect MAC |

### D.3 MQTT

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `mqttEnable` | Bool | true | Enable MQTT |
| `mqttHost` | String | 119.59.103.220 | Broker host |
| `mqttPort` | UInt | 1883 | Broker port |
| `mqttUser` | String | kmutt | Username |
| `mqttPass` | String | kmutt@kmutt | Password |

### D.4 InfluxDB

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `influxEn` | Bool | false | Enable InfluxDB |
| `influxHost` | String | 119.59.103.220 | Server host |
| `influxPort` | UInt | 8086 | Server port |
| `influxToken` | String | *(see code)* | API token |
| `influxOrg` | String | Pamiang | Organization |
| `influxBucket` | String | Srisaket_Station_I | Bucket |

### D.5 OTA

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `otaserver` | String | "" | Server URL |
| `otaproject` | String | "" | Project name |
| `otadevice` | String | All-in-One | Device type |
| `otadlpass` | String | "" | Download password |
| `otapass` | String | admin | ArduinoOTA password |
| `otainterval` | UInt | 24 | Check interval (hours) |
| `otaboot` | Bool | false | Check on boot |
| `lastota` | String | Never | Last OTA timestamp |

### D.6 Email

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `emailEnable` | Bool | false | Enable email |
| `emailuser` | String | "" | Gmail sender |
| `emailpass` | String | "" | App password |
| `emailto` | String | "" | Primary recipient |
| `emaillightto` | String | "" | Lightning recipient |
| `emailFreqH` | UInt | 24 | Frequency (hours) |
| `emailalarm` | Bool | false | Login failure alarm |

### D.7 WiFi AP

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `webpass` | String | admin | Portal password |
| `apTimeout` | UInt | 5 | Idle timeout (minutes) |

### D.8 Memory

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `memRollover` | UInt | 80 | Threshold (%) |
| `memRolloverEn` | Bool | true | Enable rollover |
| `lastDailyCsv` | String | /DATA.csv | Current daily CSV path |

### D.9 Data Sources

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `srcModbus` | Bool | true | Enable RS-485 sensors |
| `srcBle` | Bool | false | Enable BLE source |
| `fileInterval` | UInt | 10 | File rotation (minutes) |
| `ntpEnable` | Bool | true | Enable NTP sync |

---

*End of Firmware Technical Manual*
