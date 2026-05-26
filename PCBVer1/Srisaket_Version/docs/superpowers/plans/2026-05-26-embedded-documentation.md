# Embedded Technical Manual — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a 6-module technical manual for embedded engineers maintaining the Srisaket Weather Station.

**Architecture:** Six standalone Markdown documents in `docs/`, each covering one aspect of the system. Content is derived entirely from source code analysis — real pin numbers, register maps, NVS keys, and JSON payloads.

**Tech Stack:** Markdown documentation, source code analysis from PlatformIO/Arduino ESP32-C3 project.

---

### Task 1: System Overview (`docs/01-system-overview.md`)

**Files:**
- Create: `docs/01-system-overview.md`
- Reference: `src/main_1.cpp`, `include/utilities.h`

- [ ] **Step 1: Write Module 1 — System Overview**

Create `docs/01-system-overview.md` with the following sections:

1. **Project Introduction** — Describe the All-in-One Weather Station (firmware v2.3.5, build 18-05-2026). Battery-powered, duty-cycled weather station using ESP32-C3 with TPL5110 power timer. Reads soil (7-in-1) and weather (9-in-1) sensors via Modbus RS-485, publishes data via GSM/GPRS (MQTT + InfluxDB), provides WiFi AP web portal for configuration, and collects data from BLE sensor devices.

2. **Architecture Block Diagram** — ASCII art block diagram:
```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-C3 (Seeed XIAO)                     │
│                                                              │
│  ┌──────────┐  UART1 (9600)  ┌─────────────────┐           │
│  │ RS-485   │◄──────────────►│ Soil Sensor 0x03│           │
│  │ Transceiver│               │ Weather Stn 0x01│           │
│  └──────────┘                 └─────────────────┘           │
│                                                              │
│  ┌──────────┐  UART0 (9600)  ┌─────────────────┐           │
│  │ SIM800   │◄──────────────►│ GSM/GPRS Modem  │           │
│  │ GSM      │                 │ (MQTT/NTP/SMTP/ │           │
│  └──────────┘                 │  OTA/InfluxDB)  │           │
│                                └─────────────────┘           │
│  ┌──────────┐                                                │
│  │ NimBLE   │  BLE 2.4GHz    ┌─────────────────┐           │
│  │ Client   │◄──────────────►│ Sniffer Portal  │           │
│  └──────────┘                 │ (NUS/GATT)      │           │
│                                └─────────────────┘           │
│  ┌──────────┐                                                │
│  │ WiFi AP  │  802.11 Ch1     ┌─────────────────┐           │
│  │ Server   │◄──────────────►│ Web Browser     │           │
│  └──────────┘                 │ (Config/Live)   │           │
│                                └─────────────────┘           │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │ LittleFS │  │ NVS Prefs│  │ ADC A0   │                  │
│  │ (CSV)    │  │ (Config) │  │ (Battery)│                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
│                                                              │
│  TPL5110 ──► DONE Pin (D2) ──► Power Cut                   │
└─────────────────────────────────────────────────────────────┘
```

3. **State Machine Flow** — Document each state with its timeout and transition:
```
STATE_WIFI_AP (5 min max, 1 min min)
  │ exits: BLE data received / AP idle timeout / OTA requested
  ▼
STATE_GSM_INIT (120s)
  │ initializes SIM800, connects GPRS, publishes heartbeat
  │ fallback on failure: skip to STATE_NTP
  ▼
STATE_NTP (30s)
  │ syncs time via GSM RTC or NTP; fallback: CSV last timestamp + 10 min
  ▼
STATE_WEATHER (30s, 15s settle)
  │ reads weather sensor via Modbus (Slave 0x01)
  ▼
STATE_SOIL (30s, 15s settle)
  │ reads soil sensor via Modbus (Slave 0x03)
  ▼
STATE_SAVE (10s)
  │ saves to daily CSV, sends to InfluxDB, handles storage rollover
  ▼
STATE_RECONNECT → STATE_FINISH
STATE_FINISH
  │ pulses TPL5110 DONE pin (LOW→HIGH, 2s hold)
  │ if power not cut: infinite loop feeding WDT
```

4. **Module Dependency Graph** — Show which source files depend on which:
```
main_1.cpp
  ├── sensor_v2.cpp/h  (RS485sensor, dataProcess, batteryRead)
  ├── Memory.cpp/h      (LittleFS CSV read/write/rotate)
  ├── GsmHandler.cpp/h  (SIM800, MQTT, NTP, SMTP)
  ├── WifiApServer.cpp/h (WiFi AP, web server, BLE UI)
  ├── NimBLE-Arduino     (BLE scan/connect/NUS/GATT)
  ├── utilities.h        (pins, constants, defaults)
  └── libraries: ModbusMaster, PubSubClient, TinyGSM
```

5. **Boot Sequence** — Step-by-step from power-on:
   1. Serial init (115200 baud USB CDC)
   2. WDT init (45s timeout)
   3. Battery ADC pin (A0) + TPL5110 DONE pin (D2) configured
   4. RS485 UART1 init (9600 baud, D4 RX / D10 TX)
   5. GSM UART0 init (9600 baud, D7 RX / D6 TX)
   6. LittleFS mount
   7. BLE init (if enabled in NVS `bleEnable`)
   8. Load saved BLE MAC from NVS for auto-reconnect
   9. Populate `SystemStatus` struct
   10. Read battery voltage
   11. Enter main loop at `STATE_WIFI_AP`

6. **Duty Cycle / Power Lifecycle** — Explain TPL5110 behavior:
   - TPL5110 timer wakes ESP32 at configurable interval (set by external resistor)
   - ESP32 runs full state machine
   - At STATE_FINISH: DONE pin pulsed LOW then HIGH with 2s hold
   - TPL5110 cuts power to ESP32
   - If TPL5110 fails: infinite WDT-fed loop (safe fallback)

- [ ] **Step 2: Verify completeness**

Check that all states are documented, all modules mentioned, boot sequence covers all `setup()` calls, and TPL5110 behavior matches source code.

- [ ] **Step 3: Commit**

```bash
git add docs/01-system-overview.md
git commit -m "docs: add system overview module for embedded technical manual"
```

---

### Task 2: Hardware Reference (`docs/02-hardware-reference.md`)

**Files:**
- Create: `docs/02-hardware-reference.md`
- Reference: `include/utilities.h`, `include/sensor_v2.h`

- [ ] **Step 1: Write Module 2 — Hardware Reference**

Create `docs/02-hardware-reference.md` with the following sections:

1. **MCU Specifications** — Table:
   | Parameter | Value |
   |---|---|
   | MCU | ESP32-C3 (RISC-V, single-core) |
   | Clock | 160 MHz |
   | Flash | 4 MB (OTA partition scheme) |
   | Framework | Arduino (PlatformIO) |
   | Board | Seeed XIAO ESP32-C3 |
   | Partition | `partitions_ota_4mb.csv` (OTA-enabled) |
   | Debug | USB CDC, 115200 baud |
   | WDT | 45-second hardware watchdog |

2. **Pin Assignment Table** — Complete mapping:
   | Pin | Function | Direction | Notes |
   |---|---|---|---|
   | D4 | RS-485 RX | Input | UART1, 9600 baud, 8N1 |
   | D10 | RS-485 TX | Output | UART1, 9600 baud, 8N1 |
   | D7 | GSM RX | Input | UART0, 9600 baud, 8N1 |
   | D6 | GSM TX | Output | UART0, 9600 baud, 8N1 |
   | A0 | Battery ADC | Input | Voltage divider midpoint |
   | D2 | TPL5110 DONE | Output | LOW→HIGH pulse to cut power |

3. **Battery Voltage Divider** — Circuit description:
   - R1 = 100 kΩ (between Vbat and A0)
   - R2 = 100 kΩ (between A0 and GND)
   - Formula: `V_battery = V_pin × (R1 + R2) / R2 = V_pin × 2`
   - ADC reads 64 samples averaged
   - Battery voltage returned in millivolts (uint16_t)

4. **RS-485 Bus Wiring** — Half-duplex connection:
   - UART1 at 9600 baud, 8N1
   - Two-wire RS-485 (A/B) to Modbus sensors
   - Up to 2 devices on the bus (soil + weather)
   - No explicit DE/RE pin control in software (ModbusMaster library handles via pre/post transmission callbacks)
   - Termination: follow RS-485 best practices (120Ω at bus ends)

5. **GSM Modem Wiring** — SIM800 connection:
   - UART0 at 9600 baud, 8N1
   - SIM800 requires stable power supply (peak current up to 2A during TX burst)
   - AT command interface via TinyGSM library

6. **Soil Sensor (7-in-1)** — Hardware specs:
   - Interface: Modbus RTU over RS-485
   - Slave ID: 0x03
   - Register start: 0x0000, length: 7
   - Measurements: Moisture (%), Temperature (°C), EC (μS/cm), pH, Nitrogen (mg/kg), Phosphorus (mg/kg), Potassium (mg/kg)
   - All values stored as integer ×10 (except EC, N, P, K which are direct integers)

7. **Weather Station (9-in-1)** — Hardware specs:
   - Interface: Modbus RTU over RS-485
   - Slave ID: 0x01
   - Register start: 0x01F4 (500), length: 16
   - Measurements: Wind Speed (m/s), Wind Direction (°), Air Humidity (%), Air Temperature (°C), CO2 (ppm), Pressure (kPa), Illuminance (lux), Rainfall (mm), Solar Radiation (W/m²)
   - Values stored as integer ×10 (except Wind Direction, CO2, Illuminance, Solar which are direct)

8. **TPL5110 Power Timer** — DONE pin behavior:
   - DONE pin: D2 (GPIO output)
   - Sequence: Set LOW, wait 50ms, set HIGH, hold 2 seconds
   - TPL5110 cuts power after DONE pulse received
   - Wake interval set by external resistor on TPL5110 (not software-configurable)

9. **Power Supply Architecture** — Block diagram:
```
Battery ──► Voltage Regulator ──► 3.3V Rail
                                      ├── ESP32-C3
                                      ├── RS-485 Transceiver
                                      └── TPL5110 (controls power to entire system)

TPL5110 DRV pin ──► Enable regulator output
TPL5110 DONE ◄── ESP32-C3 D2 (signals "work complete")
```

10. **Debug Port** — USB CDC serial at 115200 baud. Set `CORE_DEBUG_LEVEL=0` in build flags (debug output via `Serial.printf` with `DEBUG=1` compile flag).

- [ ] **Step 2: Verify completeness**

Check all pin values against `utilities.h`, all sensor register addresses match, battery formula matches `batteryRead()` in `sensor_v2.cpp`.

- [ ] **Step 3: Commit**

```bash
git add docs/02-hardware-reference.md
git commit -m "docs: add hardware reference module for embedded technical manual"
```

---

### Task 3: Software Architecture (`docs/03-software-architecture.md`)

**Files:**
- Create: `docs/03-software-architecture.md`
- Reference: All source files

- [ ] **Step 1: Write Module 3 — Software Architecture**

Create `docs/03-software-architecture.md` with the following sections:

1. **File/Module Map** — Table with each file's responsibility:
   | File | Responsibility |
   |---|---|
   | `main_1.cpp` | State machine, BLE, InfluxDB, OTA, heartbeat, time management |
   | `sensor_v2.cpp` / `.h` | Modbus RS-485 sensor reads, median filtering, battery ADC |
   | `GsmHandler.cpp` / `.h` | SIM800 modem init, GPRS, MQTT, NTP, SMTP email |
   | `Memory.cpp` / `.h` | LittleFS CSV file read/write/rotate, data record serialization |
   | `WifiApServer.cpp` / `.h` | WiFi AP, HTTP web server, BLE scan/connect UI, settings |
   | `utilities.h` | Central config: pins, baud rates, constants, defaults |

2. **State Machine Detail** — For each state, document:
   - Purpose
   - Timeout value
   - Entry conditions
   - Exit conditions (success and timeout/failure)
   - Fallback behavior

   Cover all 8 states: STATE_WIFI_AP, STATE_GSM_INIT, STATE_NTP, STATE_WEATHER, STATE_SOIL, STATE_SAVE, STATE_RECONNECT, STATE_FINISH (plus STATE_PUBLISH as placeholder).

3. **Data Structures** — Document each struct with field-by-field description:

   **`SensorData`** (33 bytes, packed):
   | Field | Type | Unit | Scale |
   |---|---|---|---|
   | `soil_humi` | uint16_t | % | ×10 |
   | `soil_temp` | int16_t | °C | ×10 |
   | `soil_ec` | uint16_t | μS/cm | direct |
   | `soil_ph` | uint8_t | pH | ×10 (0–25.5) |
   | `soil_N` | uint16_t | mg/kg | direct |
   | `soil_P` | uint16_t | mg/kg | direct |
   | `soil_K` | uint16_t | mg/kg | direct |
   | `windSpeed` | uint16_t | m/s | ×10 |
   | `windDir_Deg` | uint16_t | degrees | direct |
   | `air_humidity` | uint16_t | % | ×10 |
   | `air_temperature` | int16_t | °C | ×10 |
   | `CO2` | uint16_t | ppm | direct |
   | `pressure` | uint16_t | kPa | ×10 |
   | `illuminance` | uint32_t | lux | direct |
   | `rainfall` | uint16_t | mm | ×10 |
   | `solar` | uint16_t | W/m² | direct |

   **`timeStruct`** — date/day/month/year/hour/minute/second + formatted strings.

   **`DataRecord`** (packed) — timestamp + SensorData + valid flag.

   **`SystemStatus`** — pointer-based struct linking web server to live data.

   **`BLEConnectedDevice`** — MAC, name, connection status, characteristic data array.

4. **Data Flow Diagram**:
```
Modbus Sensors (RS-485)
    │
    ▼
RS485sensor.read() → SensorData (median of 3, retry up to 5×)
    │
    ├──► Memory.saveData() → Daily CSV on LittleFS
    │
    ├──► sendToInfluxDB() → InfluxDB v2 via GSM TCP (if GSM available)
    │
    └──► (Future: MQTT batch publish from stored CSV records)

BLE NUS/GATT Sensor
    │
    ▼
parseNusJson() → BLEConnectedDevice
    │
    └──► saveBleDataToCsv() → BLE daily CSV on LittleFS
```

5. **Sensor Reading Pipeline** — Explain the multi-stage noise rejection:
   - Stage 1: Take 3 samples (bubble sort → median)
   - Stage 2: If Modbus fails, retry up to 5 times total
   - Stage 3: If key values are zero after read, re-read with settle delay
   - Stage 4: On error code 0xE0 (invalid slave), perform UART recovery (flush + re-init)
   - Stage 5: After 3 consecutive total failures, zero out sensor data

6. **Time Management** — Three-tier time source:
   - Primary: GSM RTC (`getGSMDateTime`) or NTP (`+CNTP` to `pool.ntp.org`)
   - Fallback: Read last timestamp from most recent CSV file + increment by `TIME_INCREMENT_MINUTES` (10 min)
   - BLE NTP: Optional sync from BLE NUS device via web portal
   - Time stored in `timeStruct` with formatted `dateStr` (DD/MM/YYYY) and `timeStr` (HH:MM:SS)

7. **Watchdog Management** — 45-second WDT fed at:
   - Every long-running GSM wait (every 2s during network registration)
   - Before and after BLE operations
   - During file I/O (every 16 lines)
   - During OTA download (every chunk)
   - During InfluxDB HTTP wait
   - In STATE_FINISH infinite loop

8. **NVS Preferences Keys** — Table of all `ws-cfg` namespace keys:
   | Key | Type | Default | Purpose |
   |---|---|---|---|
   | `bleEnable` | bool | false | Enable BLE client |
   | `bleSavedMac` | string | "" | Last connected BLE MAC for auto-reconnect |
   | `lastDailyCsv` | string | "/DATA.csv" | Path to current daily CSV |
   | `lastota` | string | "Never" | Last OTA update timestamp |
   | `otaserver` | string | "" | OTA server URL |
   | `otainterval` | uint | 24 | OTA check interval (hours) |
   | `otaboot` | bool | false | Check OTA on boot |
   | `otaproject` | string | "" | OTA project name |
   | `otadevice` | string | "All-in-One" | OTA device name |
   | `otadlpass` | string | "" | OTA download password |
   | `influxEn` | bool | false | Enable InfluxDB upload |
   | `influxHost` | string | "119.59.103.220" | InfluxDB server |
   | `influxPort` | uint | 8086 | InfluxDB port |
   | `influxToken` | string | (long token) | InfluxDB auth token |
   | `influxOrg` | string | "Pamiang" | InfluxDB organization |
   | `influxBucket` | string | "Srisaket_Station_I" | InfluxDB bucket |
   | `memRollover` | uint | 80 | Storage rollover threshold (%) |
   | `memRolloverEn` | bool | true | Enable storage rollover |

- [ ] **Step 2: Verify completeness**

Check all struct fields match headers, all NVS keys match source code usage, all state machine timeouts match `utilities.h`.

- [ ] **Step 3: Commit**

```bash
git add docs/03-software-architecture.md
git commit -m "docs: add software architecture module for embedded technical manual"
```

---

### Task 4: Communication Protocols (`docs/04-communication-protocols.md`)

**Files:**
- Create: `docs/04-communication-protocols.md`
- Reference: `src/sensor_v2.cpp`, `src/GsmHandler.cpp`, `src/main_1.cpp`, `src/WifiApServer.cpp`

- [ ] **Step 1: Write Module 4 — Communication Protocols**

Create `docs/04-communication-protocols.md` with the following sections:

1. **Modbus RTU (RS-485)** — Frame format: 9600 baud, 8N1, half-duplex.

   **Soil Sensor Register Map (Slave 0x03):**
   | Register | Offset | Length | Field | Unit | Scale |
   |---|---|---|---|---|---|
   | 0x0000 | 0 | 1 | Moisture | % | ×10 |
   | 0x0001 | 1 | 1 | Temperature | °C | ×10 (signed) |
   | 0x0002 | 2 | 1 | EC | μS/cm | direct |
   | 0x0003 | 3 | 1 | pH | — | ×10 (clamped 0–25.5) |
   | 0x0004 | 4 | 1 | Nitrogen | mg/kg | direct |
   | 0x0005 | 5 | 1 | Phosphorus | mg/kg | direct |
   | 0x0006 | 6 | 1 | Potassium | mg/kg | direct |

   Read: Function code 0x03, Start 0x0000, Quantity 7.

   **Weather Station Register Map (Slave 0x01):**
   | Register | Offset | Length | Field | Unit | Scale |
   |---|---|---|---|---|---|
   | 0x01F4 | 0 | 1 | Wind Speed | m/s | ×10 |
   | 0x01F5–0x01F6 | 1–2 | 2 | (reserved) | — | — |
   | 0x01F7 | 3 | 1 | Wind Direction | degrees | direct |
   | 0x01F8 | 4 | 1 | Air Humidity | % | ×10 |
   | 0x01F9 | 5 | 1 | Air Temperature | °C | ×10 (signed) |
   | 0x01FA–0x01FB | 6–7 | 1+1 | CO2 / (reserved) | ppm | CO2 direct |
   | 0x01FC | 8 | — | (offset) | — | — |
   | 0x01FD | 9 | 1 | Pressure | kPa | ×10 |
   | 0x01FE–0x01FF | 10–11 | 2 | Illuminance | lux | 32-bit direct |
   | 0x0200–0x0201 | 12–13 | 1+1 | (reserved) / Rainfall | mm | ×10 |
   | 0x0202–0x0203 | 14–15 | 1+1 | (reserved) / Solar | W/m² | direct |

   Read: Function code 0x03, Start 0x01F4, Quantity 16.

   Field mapping in code (`rawBuffer` index → field):
   - `[0]` → windSpeed, `[3]` → windDir_Deg, `[4]` → air_humidity, `[5]` → air_temperature
   - `[7]` → CO2, `[9]` → pressure, `[10:11]` → illuminance (32-bit), `[13]` → rainfall, `[15]` → solar

   Error handling: 0xE0 (invalid slave ID) triggers UART recovery (flush buffers + re-initialize Modbus). Retry up to 5 times with median of 3 samples each.

2. **MQTT** — Configuration:
   - Broker: `119.59.103.220:1883`
   - User/Pass: `kmutt` / `kmutt@kmutt`
   - Client ID: `PCB_TEST_1`

   **Topics:**
   | Topic | Direction | Purpose |
   |---|---|---|
   | `weather/Srisaket/Station_1/ping` | Subscribe | Incoming ping (triggers heartbeat) |
   | `weather/Srisaket/Station_1/pong` | Publish | Heartbeat response |
   | `weather/Srisaket/Station_1` | Publish | Sensor data |

   **Heartbeat JSON** (published to pong topic after GSM init):
   ```json
   {"n":0,"alive":1,"vt":3200,"heap":45000,"uptime":65,"gsm_rssi":15,"fw":"2.3.5"}
   ```
   Fields: `n`=0 (heartbeat marker), `alive`=1, `vt`=battery mV, `heap`=free heap bytes, `uptime`=seconds since boot, `gsm_rssi`=GSM signal quality, `fw`=firmware version.

   **Sensor Data JSON** (compact format with `buildCompactJSON`):
   ```json
   {
     "n":1,"seq":42,"vt":3200,"srs":15,"d":"260526",
     "r":[{
       "d":"260526","t":"1430",
       "sh":455,"st":-3,"se":350,"ph":68,
       "sn":25,"sp":30,"sk":40,
       "ws":12,"wd":180,"ah":750,"at":325,
       "co2":410,"pr":10130,"il":25000,"rf":0,"so":350
     }],
     "sniffer":{"mac":"AA:BB:CC:DD:EE:FF","name":"Sniffer1",
       "ts":"2026-05-26T14:30:00","temp":"25.3","hum":"65.2",
       "tmp117":"24.8","delta":"0.5","rain":"0","leaf":"2.1",
       "par":"450","soil":"35.6"
     }
   }
   ```
   Field mapping: `sh`=soil_humi, `st`=soil_temp, `se`=soil_ec, `ph`=soil_ph, `sn`=soil_N, `sp`=soil_P, `sk`=soil_K, `ws`=windSpeed, `wd`=windDir, `ah`=air_humidity, `at`=air_temp, `co2`=CO2, `pr`=pressure, `il`=illuminance, `rf`=rainfall, `so`=solar. All values are raw integers (divide by 10 for actual where applicable).

3. **InfluxDB v2 HTTP** — Line protocol format:
   ```
   weather_station,station=Srisaket soil_humi=45.5,soil_temp=-3.0,soil_ec=350,soil_ph=6.8,soil_N=25,soil_P=30,soil_K=40,wind_speed=1.2,wind_dir=180,air_humi=75.0,air_temp=32.5,co2=410,pressure=1013.0,illuminance=25000,rainfall=0.0,solar=350,battery=3.200,gsm_rssi=15
   ```

   When BLE NUS data is available, appends:
   ```
   sniffer_watchdog,station=Srisaket,mac=AA:BB:CC:DD:EE:FF temp=25.3,hum=65.2,tmp117=24.8,...
   ```

   HTTP request: `POST /api/v2/write?org=<org>&bucket=<bucket>&precision=ms` with `Authorization: Token <token>` header. Uses SIM800 mux ID 1 (mux 0 reserved for MQTT). TCP with up to 3 connect retries, 10s response timeout.

4. **BLE NUS (Nordic UART Service)** — For Sniffer Portal devices:
   - Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
   - TX Characteristic (notify): `6e400003-b5a3-f393-e0a9-e50e24dcca9e`
   - RX Characteristic (write): `6e400002-b5a3-f393-e0a9-e50e24dcca9e`

   **NUS JSON Notification Payload:**
   ```json
   {"ts":"2026-05-26T14:30:00","temp":"25.3","hum":"65.2","tmp117":"24.8","delta":"0.5","rain":"0","leaf":"2.1","par":"450","soil":"35.6"}
   ```
   Fields: `ts`=ISO timestamp, `temp`=temperature (°C), `hum`=humidity (%), `tmp117`=TMP117 temperature (°C), `delta`=delta T (°C), `rain`=rainfall, `leaf`=leaf wetness, `par`=PAR light, `soil`=soil moisture.

   Client sends `"live"` command via RX characteristic to request data push.

   Auto-reconnect: MAC saved to NVS `bleSavedMac`, auto-connect on next boot.

5. **BLE GATT** — Generic characteristic reading:
   - Discovers all services on connected device
   - Reads all readable characteristics (max 16)
   - Displays raw hex and printable ASCII
   - Max 10 scanned devices per scan, 3-second active scan

6. **SMTP Email (via SIM800 AT)** — AT command sequence:
   ```
   AT+SMTPSRV="<server>",<port>
   AT+SMTPAUTH=1,"<user>","<pass>"
   AT+SMTPFROM="<from>","<from_name>"
   AT+SMTPRCPT=0,"<to>"
   AT+SMTPSUB="<subject>"
   AT+SMTPBODY="<body>"
   AT+SMTPSEND
   ```
   Default port: 587. Used for login failure alerts and test emails.

7. **NTP (via SIM800 AT)** — Command sequence:
   ```
   AT+CNTP="pool.ntp.org",0
   AT+CNTP
   ```
   Fallback to GSM RTC time if NTP fails.

8. **OTA HTTP (via GSM)** — Request format:
   ```
   GET /update HTTP/1.1
   Host: <server>
   Connection: close
   x-ESP32-version: 2.3.5
   x-ESP32-device: All-in-One
   x-ESP32-project: <project>
   x-ESP32-password: <password>
   ```

   Response codes:
   | Code | Meaning | Action |
   |---|---|---|
   | 200 | New firmware available | Download, flash, verify MD5, reboot |
   | 304 | Already up to date | No action |
   | 401 | Auth failed | Log error, skip |
   | Other | Error | Log error, skip |

   MD5 verification via `x-md5` response header. Chunk download (512 bytes), 5-minute total timeout. Firmware written to OTA partition via `Update` library.

9. **WiFi AP HTTP API** — Endpoint reference table:
   | Path | Method | Auth | Purpose |
   |---|---|---|---|
   | `/login` | GET/POST | No | Login page/form |
   | `/live` | GET | Yes | Live sensor dashboard |
   | `/api/live` | GET | Yes | JSON sensor data (polled every 30s) |
   | `/api/ble` | GET | Yes | JSON BLE data (polled every 5s) |
   | `/ble/scan` | GET | Yes | Trigger BLE scan |
   | `/ble/connect?mac=...` | GET | Yes | Connect to BLE device |
   | `/ble/disconnect` | GET | Yes | Disconnect BLE |
   | `/ble/forget` | GET | Yes | Forget saved BLE device |
   | `/ble/refresh` | GET | Yes | Request NUS data push |
   | `/files` | GET | Yes | List/download/delete CSV files |
   | `/log` | GET | Yes | Event log viewer |
   | `/settings` | GET | Yes | Configuration page |
   | `/reboot` | GET | Yes | Reboot device |
   | `/otaupdate` | GET | Yes | Trigger OTA update |
   | `/testinflux` | GET | Yes | Test InfluxDB connection |
   | `/testmqtt` | GET | Yes | Test MQTT connection |
   | `/testemail` | GET | Yes | Send test email |
   | `/testota` | GET | Yes | Test OTA server |
   | `/about` | GET | Yes | About page |

   Session: 16-char hex cookie token, 10-minute timeout. Default login: `admin` / `admin`.

- [ ] **Step 2: Verify completeness**

Check register offsets against `sensor_v2.cpp` rawBuffer indexing, MQTT topic strings against `utilities.h`, InfluxDB line protocol against `sendToInfluxDB()`, NUS UUIDs and JSON fields against `main_1.cpp`.

- [ ] **Step 3: Commit**

```bash
git add docs/04-communication-protocols.md
git commit -m "docs: add communication protocols module for embedded technical manual"
```

---

### Task 5: Configuration & Deployment (`docs/05-configuration-deployment.md`)

**Files:**
- Create: `docs/05-configuration-deployment.md`
- Reference: All source files, `platformio.ini`

- [ ] **Step 1: Write Module 5 — Configuration & Deployment**

Create `docs/05-configuration-deployment.md` with the following sections:

1. **First-Time Provisioning** — Step-by-step:
   1. Connect USB to XIAO ESP32-C3
   2. Build and upload firmware: `pio run -t upload`
   3. Open serial monitor: `pio device monitor` (115200 baud)
   4. Verify boot: look for `[FW] All-in-One Weather Station v2.3.5` and `===== SETUP COMPLETE =====`
   5. Device creates WiFi AP `WeatherStation_AP` (password: `12345678`)
   6. Connect to AP from laptop/phone, browse to `http://192.168.4.1`
   7. Login with `admin` / `admin`
   8. Go to Settings → configure GSM APN (default: `internet`)
   9. Configure MQTT broker, InfluxDB, or email as needed
   10. Save settings (persisted to NVS immediately)

2. **NVS Configuration Keys** — Complete table (all in namespace `ws-cfg`):
   | Key | Type | Default | Description |
   |---|---|---|---|
   | `bleEnable` | bool | false | Enable BLE client functionality |
   | `bleSavedMac` | string | "" | Saved BLE MAC for auto-reconnect |
   | `lastDailyCsv` | string | "/DATA.csv" | Current daily CSV filename |
   | `lastota` | string | "Never" | Timestamp of last OTA update |
   | `otaserver` | string | "" | OTA firmware server URL |
   | `otainterval` | uint | 24 | OTA check interval in hours |
   | `otaboot` | bool | false | Check OTA on every boot |
   | `otaproject` | string | "" | OTA project identifier |
   | `otadevice` | string | "All-in-One" | OTA device identifier |
   | `otadlpass` | string | "" | OTA download password |
   | `influxEn` | bool | false | Enable InfluxDB v2 upload |
   | `influxHost` | string | "119.59.103.220" | InfluxDB server address |
   | `influxPort` | uint | 8086 | InfluxDB port |
   | `influxToken` | string | (token) | InfluxDB v2 auth token |
   | `influxOrg` | string | "Pamiang" | InfluxDB organization |
   | `influxBucket` | string | "Srisaket_Station_I" | InfluxDB bucket |
   | `memRollover` | uint | 80 | Storage rollover threshold % |
   | `memRolloverEn` | bool | true | Enable automatic storage rollover |
   | `aptimeout` | ulong | 300000 | WiFi AP timeout in ms (5 min) |
   | `fileinterval` | uint | 10 | Data save interval in minutes |
   | `webUser` | string | "admin" | Web portal username |
   | `webPass` | string | "admin" | Web portal password |

   (Note: Additional keys may exist for email and data source settings — check `WifiApServer.cpp` for the full set.)

3. **Web Portal Configuration** — Walkthrough of Settings page sections:
   - **WiFi AP**: SSID, password, timeout duration
   - **BLE**: Enable/disable BLE client
   - **Data Sources**: Toggle soil/weather sensor reading
   - **File Intervals**: Data save interval (minutes)
   - **InfluxDB**: Host, port, token, org, bucket, enable/disable
   - **MQTT**: Broker, port, user, password, client ID
   - **Email**: SMTP server, port, user, password, recipient
   - **OTA**: Server URL, project name, device name, download password, check interval, boot check
   - **Memory**: Rollover threshold %, enable/disable auto-rollover
   - **Password**: Change web portal login credentials

4. **OTA Update Process** — Two trigger methods:
   - **Automatic**: On boot if `otaboot=true`, or at `otainterval` hour intervals
   - **Manual**: Web portal → Settings → "Update Firmware" button
   - Server must host firmware at `/update` endpoint
   - Server responds with 200 + binary + `x-md5` header, or 304 if current
   - Device downloads in 512-byte chunks, verifies MD5, writes to OTA partition, reboots

5. **MQTT Broker Requirements**:
   - Must support MQTT v3.1.1
   - Default port: 1883 (non-TLS)
   - Authentication: username/password
   - Topic structure: `weather/<location>/<station_id>[/ping|/pong]`

6. **InfluxDB v2 Requirements**:
   - Must support HTTP API v2 write endpoint (`/api/v2/write`)
   - Authentication via token in `Authorization: Token <token>` header
   - Org and bucket must be pre-created
   - Line protocol format with millisecond precision

7. **Email SMTP Requirements**:
   - SMTP server supporting AUTH LOGIN
   - Default port: 587
   - SIM800 handles TLS/SSL negotiation (if supported by modem firmware)

8. **File System Structure** — LittleFS layout:
   ```
   /
   ├── DD-MM-YYYY.csv          (daily sensor data)
   ├── BLE-DD-MM-YYYY.csv      (daily BLE sensor data)
   ├── Event-DD-MM-YYYY.csv    (daily event logs)
   └── /DATA_TEMP_SWAP.csv     (temp file for row removal)
   ```

9. **CSV File Formats**:

   **Sensor Data CSV:**
   ```csv
   Date,Time,Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,WindSpeed,WindDirection,Air_Humidity,Air_Temperature,CO2,Pressure,Illuminance,Rainfall,Solar
   26/05/2026,14:30:00,455,-30,350,68,25,30,40,12,180,750,325,410,10130,25000,0,350
   ```

   **BLE Data CSV:**
   ```csv
   Date,Time,Temperature(C),Humidity(%),TMP117(C),DeltaT(C),Rainfall,LeafWetness,PAR,SoilMoisture
   26/05/2026,14:30:00,25.3,65.2,24.8,0.5,0,2.1,450,35.6
   ```

   **Event Log CSV:** Auto-generated, contains timestamped events (logins, settings changes, file operations, BLE events).

- [ ] **Step 2: Verify completeness**

Check NVS key names match source code, CSV headers match `updateDailyCsv()` and `saveBleDataToCsv()`, provisioning steps match `setup()` sequence.

- [ ] **Step 3: Commit**

```bash
git add docs/05-configuration-deployment.md
git commit -m "docs: add configuration & deployment module for embedded technical manual"
```

---

### Task 6: Troubleshooting & Maintenance (`docs/06-troubleshooting-maintenance.md`)

**Files:**
- Create: `docs/06-troubleshooting-maintenance.md`
- Reference: All source files

- [ ] **Step 1: Write Module 6 — Troubleshooting & Maintenance**

Create `docs/06-troubleshooting-maintenance.md` with the following sections:

1. **Common Issues and Solutions** — Table:
   | Symptom | Likely Cause | Solution |
   |---|---|---|
   | `[TMO] State X timeout` in serial | State exceeded its timeout | Check sensor wiring, GSM signal, network availability |
   | Modbus 0xE0 error | Invalid slave ID response or UART corruption | Automatic UART recovery (flush + re-init). Check RS-485 wiring, baud rate, slave ID |
   | `[NTP] Fallback:` in serial | GSM time sync failed | Check SIM card, GSM antenna, network registration. Fallback uses CSV timestamp + 10 min |
   | `[INFLUX] TCP connect failed` | GPRS not connected or server unreachable | Check GSM signal quality, APN setting, InfluxDB server address/port |
   | `[OTA] Auth failed` | Wrong download password | Update `otadlpass` in web portal Settings |
   | `[OTA] TCP connect failed` | OTA server unreachable | Check server URL, GSM connectivity |
   | `[BLE] Connect FAILED after 3 attempts` | BLE device out of range or not advertising | Move closer, ensure device is powered on and advertising |
   | `[FS] LittleFS FAILED` at boot | Flash filesystem corruption | Reformat: connect serial, send factory reset command |
   | `[WARN] TPL5110 did not cut power` | TPL5110 not connected or faulty | Check TPL5110 DONE pin wiring, check TPL5110 power supply |
   | `[GSM] Network registration timeout` | No SIM, no signal, wrong APN | Check SIM card inserted, GSM antenna connected, APN matches carrier |
   | MQTT publish timeout | Broker unreachable or auth failure | Check broker address, port, credentials in Settings |
   | Zero sensor readings | Sensor not connected or wrong slave ID | Check RS-485 wiring, verify sensor slave ID matches config (0x03 soil, 0x01 weather) |
   | Login failure email alarm | Repeated failed web portal logins | Check if unauthorized access attempts; email sent via GSM SMTP |

2. **Debug Serial Console** — Usage:
   - Connect USB cable, open serial monitor at 115200 baud
   - Build flag `DEBUG=1` enables verbose sensor reading output
   - `CORE_DEBUG_LEVEL=0` in `platformio.ini` suppresses ESP-IDF debug spam
   - Key serial prefixes: `[FW]` firmware info, `[WDT]` watchdog, `[SERIAL]` UART, `[FS]` filesystem, `[BLE]` BLE operations, `[NTP]` time sync, `[INFLUX]` InfluxDB, `[OTA]` firmware update, `[SAVE]` CSV storage, `[DONE]` TPL5110, `[TMO]` timeout, `[BATT]` battery voltage

3. **Event Log Format** — Event CSV files (`/Event-DD-MM-YYYY.csv`) contain:
   - Timestamped entries for: login/logout, settings changes, file operations, BLE connect/disconnect, OTA checks, email sends, reboots
   - Accessible via web portal `/log` page or `/files` download
   - Auto-created daily

4. **Storage Management**:
   - Check free space: web portal `/settings` shows available space
   - Storage rollover: when usage exceeds threshold (default 80%), daily CSV is recreated with only the last row preserved
   - Min free space warning: 10 KB (`MIN_FREE_SPACE_BYTES`)
   - Max file size warning: 500 KB (`MAX_FILE_SIZE_BYTES`)
   - Manual cleanup: web portal `/files` → delete individual files or all data

5. **Web Portal Monitoring**:
   - `/live` dashboard: real-time sensor data, auto-refresh every 30 seconds
   - `/api/ble` endpoint: BLE device data, auto-refresh every 5 seconds
   - `/log` page: event log viewer for diagnostics

6. **Watchdog Reset Analysis**:
   - WDT timeout: 45 seconds
   - If WDT triggers, device reboots and starts from `STATE_WIFI_AP`
   - Common causes: GSM network registration taking too long, BLE operation blocking, OTA download stall
   - WDT is fed at strategic points throughout the code to prevent false resets during normal operation

7. **Factory Reset**:
   - Connect via serial monitor
   - To clear NVS: use `pio run -t erase` to flash erase, then re-upload firmware
   - To clear LittleFS: delete all files via web portal `/files` → "Delete All", or reformat via serial command
   - After factory reset: all settings revert to compiled defaults from `utilities.h`

8. **Known Limitations**:
   - Time accuracy degrades when operating without GSM (CSV fallback + 10 min increment)
   - BLE and WiFi share the 2.4 GHz radio — simultaneous heavy use may cause intermittent issues
   - SIM800 GPRS bandwidth is limited (~85 kbps downlink) — large OTA updates may time out
   - LittleFS has no wear leveling beyond what ESP32 flash provides — constant writes to same file will eventually wear the flash sector
   - Battery voltage reading is only taken once at boot — not updated during the cycle
   - BLE NUS JSON parser is simple string matching — malformed JSON may produce `--` values
   - Max 16 BLE characteristics per device, max 10 scanned devices
   - CSV file names use date at time of creation — date rollover at midnight is not handled (device powers off between cycles)

- [ ] **Step 2: Verify completeness**

Check all error messages match actual `Serial.printf` strings in source, all thresholds match `utilities.h`, troubleshooting steps are actionable.

- [ ] **Step 3: Commit**

```bash
git add docs/06-troubleshooting-maintenance.md
git commit -m "docs: add troubleshooting & maintenance module for embedded technical manual"
```

---

## Self-Review Checklist

After completing all tasks, verify:

- [ ] All pin numbers match `utilities.h`
- [ ] All register addresses and offsets match `sensor_v2.cpp`
- [ ] All NVS keys match usage across `main_1.cpp` and `WifiApServer.cpp`
- [ ] All JSON field names match `buildCompactJSON()` and `parseNusJson()`
- [ ] All state machine timeouts match `utilities.h` constants
- [ ] All baud rates and serial ports match `utilities.h`
- [ ] CSV headers match `updateDailyCsv()` and `saveBleDataToCsv()`
- [ ] InfluxDB line protocol matches `sendToInfluxDB()`
- [ ] BLE UUIDs match `main_1.cpp` defines
- [ ] MQTT topics match `utilities.h` defines
