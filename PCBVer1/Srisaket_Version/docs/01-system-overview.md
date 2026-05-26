# 1. System Overview

**All-in-One Weather Station -- Srisaket Version**
Firmware v2.3.5 | Build 18-05-2026 | IDEA Laboratory @ KMUTT

---

## 1.1 Project Introduction

The All-in-One Weather Station is a battery-powered, duty-cycled environmental
monitoring platform built around the ESP32-C3 (Seeed XIAO) microcontroller. It
is designed for unattended deployment in agricultural and meteorological field
stations.

The firmware implements a run-once-per-wake-cycle state machine governed by the
TPL5110 nanotimer, which supplies power at a configurable interval set by an
external resistor. On each wake, the station:

1. Opens a WiFi access point and simultaneously scans for / reconnects to a BLE
   sensor device (the "Sniffer Portal") to collect auxiliary microclimate data.
2. Initializes the SIM800 GSM/GPRS modem, connects to the cellular network, and
   publishes a heartbeat over MQTT.
3. Synchronises the real-time clock via GSM RTC or NTP.
4. Reads a 9-in-1 weather station sensor (wind speed/direction, air temperature
   /humidity, CO2, barometric pressure, illuminance, rainfall, solar radiation)
   over Modbus RS-485 (slave address 0x01).
5. Reads a 7-in-1 soil sensor (moisture, temperature, EC, pH, N, P, K) over
   the same RS-485 bus (slave address 0x03).
6. Persists readings to daily CSV files on LittleFS and pushes them to InfluxDB
   v2 over the GSM data link.
7. Pulses the TPL5110 DONE pin so the nanotimer cuts power until the next cycle.

Communication channels:

| Channel   | Protocol     | Purpose                                       |
|-----------|-------------|-----------------------------------------------|
| UART1     | Modbus RTU   | RS-485 soil and weather sensors               |
| UART0     | AT commands  | SIM800 GSM/GPRS (MQTT, NTP, SMTP, OTA)       |
| BLE 2.4 GHz | NimBLE    | Sniffer Portal via Nordic UART Service (NUS)  |
| WiFi AP   | HTTP         | Web configuration portal (channel 1)          |
| LittleFS  | CSV          | Local data buffering and storage rollover     |
| ADC A0    | Voltage divider | Battery voltage monitoring                 |

The PlatformIO project targets the `seeed_xiao_esp32c3` board under the Arduino
framework with a 4 MB OTA partition scheme.

---

## 1.2 Architecture Block Diagram

```
+-------------------------------------------------------------+
|                    ESP32-C3 (Seeed XIAO)                     |
|                                                              |
|  +----------+  UART1 (9600)  +------------------+           |
|  | RS-485   |<-------------->| Soil Sensor 0x03 |           |
|  | Transceiver               | Weather Stn 0x01 |           |
|  +----------+                +------------------+           |
|                                                              |
|  +----------+  UART0 (9600)  +------------------+           |
|  | SIM800   |<-------------->| GSM/GPRS Modem   |           |
|  | GSM      |                | (MQTT/NTP/SMTP/  |           |
|  +----------+                |  OTA/InfluxDB)   |           |
|                               +------------------+           |
|  +----------+                                               |
|  | NimBLE   |  BLE 2.4 GHz  +------------------+           |
|  | Client   |<-------------->| Sniffer Portal   |           |
|  +----------+                | (NUS/GATT)       |           |
|                               +------------------+           |
|  +----------+                                               |
|  | WiFi AP  |  802.11 Ch1    +------------------+           |
|  | Server   |<-------------->| Web Browser      |           |
|  +----------+                | (Config/Live)    |           |
|                               +------------------+           |
|  +----------+  +----------+  +----------+                   |
|  | LittleFS |  | NVS Prefs|  | ADC A0   |                   |
|  | (CSV)    |  | (Config) |  | (Battery)|                   |
|  +----------+  +----------+  +----------+                   |
|                                                              |
|  TPL5110 --> DONE Pin (D2) --> Power Cut                    |
+-------------------------------------------------------------+
```

### Pin Assignment

| Function         | GPIO  | Direction | Notes                          |
|------------------|-------|-----------|--------------------------------|
| RS-485 RX        | D4    | Input     | UART1, 9600 baud, 8N1         |
| RS-485 TX        | D10   | Output    | UART1, 9600 baud, 8N1         |
| GSM RX           | D7    | Input     | UART0, 9600 baud, 8N1         |
| GSM TX           | D6    | Output    | UART0, 9600 baud, 8N1         |
| TPL5110 DONE     | D2    | Output    | Active-HIGH pulse to cut power |
| Battery ADC      | A0    | Input     | Voltage divider (100k/100k)    |
| USB CDC Serial   | USB   | --        | Debug console, 115200 baud     |

---

## 1.3 State Machine Flow

The firmware implements a linear state machine. On each power cycle the ESP32
enters `STATE_WIFI_AP` and advances through each state in sequence. Every state
has a maximum timeout; on expiry the machine falls through to the next state so
the cycle always completes.

```
STATE_WIFI_AP (5 min max, 1 min min)
  | Exits when: BLE data received after min window / AP idle timeout / OTA requested
  v
STATE_GSM_INIT (120 s)
  | Initializes SIM800 modem, registers on cellular network, opens GPRS session.
  | Publishes heartbeat (MQTT pong) with battery voltage, free heap, uptime,
  | GSM signal strength, and firmware version.
  | On failure: sets gsmAvailable = false and skips to STATE_NTP.
  v
STATE_NTP (30 s)
  | Attempts GSM RTC time first; falls back to NTP (pool.ntp.org).
  | If both fail: reads last timestamp from the previous daily CSV and adds
  | 10 minutes.  Creates/rotates the daily CSV file name.
  | Triggers remote OTA check if configured.
  v
STATE_WEATHER (30 s total, 15 s settle)
  | Waits 15 s for the RS-485 transceiver to settle after any preceding
  | activity, then reads 16 holding registers from slave 0x01 (address 0x01F4).
  | Uses median-of-3 filtering across up to 3 read attempts per sample.
  | On failure: zeroes weather fields and continues.
  v
STATE_SOIL (30 s total, 15 s settle)
  | Waits 15 s settle, then reads 7 holding registers from slave 0x03
  | (address 0x0000).  Same median filtering as weather.
  | On persistent failure (3 consecutive misses): zeroes soil fields.
  v
STATE_SAVE (10 s)
  | Checks LittleFS usage against configurable rollover threshold (default 80%).
  | If over threshold: rebuilds the CSV preserving only the most recent row.
  | Appends the current sensor record to the daily CSV.
  | Sends InfluxDB v2 line-protocol POST (appends sniffer data if BLE active).
  v
STATE_RECONNECT (120 s timeout, exits immediately)
  | Placeholder for buffered-data re-publish; currently transitions directly
  | to STATE_FINISH.
  v
STATE_FINISH
  | 3 s delay, flushes Serial.
  | DONE pin: LOW for 50 ms, then HIGH for 2 s.
  | If TPL5110 does not cut power: enters infinite loop feeding WDT every 1 s.
```

### State Timeout Summary

| State             | Timeout    | Settle Delay | Fallback State  |
|-------------------|------------|--------------|-----------------|
| STATE_WIFI_AP     | 300 000 ms | --           | STATE_GSM_INIT  |
| STATE_GSM_INIT    | 120 000 ms | --           | STATE_NTP       |
| STATE_NTP         |  30 000 ms | --           | STATE_WEATHER   |
| STATE_WEATHER     |  30 000 ms | 15 000 ms    | STATE_SOIL      |
| STATE_SOIL        |  30 000 ms | 15 000 ms    | STATE_SAVE      |
| STATE_SAVE        |  10 000 ms | --           | STATE_RECONNECT |
| STATE_RECONNECT   | 120 000 ms | --           | STATE_FINISH    |
| STATE_PUBLISH     |  60 000 ms | --           | STATE_FINISH    |
| STATE_FINISH      | none       | --           | (terminal)      |

### State Machine Diagram

```
                     Power On
                        |
                        v
                 +---------------+
                 | STATE_WIFI_AP |<--- min 60 s, max 300 s
                 | WiFi AP + BLE |
                 +-------+-------+
                         |
          +--------------+--------------+
          |              |              |
     BLE data       AP timeout     OTA request
          |              |              |
          +--------------+--------------+
                         |
                         v
                 +---------------+
                 | STATE_GSM_INIT|--- fail --> STATE_NTP
                 | SIM800 + GPRS |
                 +-------+-------+
                         | success
                         v
                 +---------------+
                 |   STATE_NTP   |
                 | GSM RTC / NTP |
                 +-------+-------+
                         |
                         v
                 +---------------+
                 | STATE_WEATHER |
                 | Modbus 0x01   |
                 +-------+-------+
                         |
                         v
                 +---------------+
                 |  STATE_SOIL   |
                 | Modbus 0x03   |
                 +-------+-------+
                         |
                         v
                 +---------------+
                 |  STATE_SAVE   |
                 | CSV + InfluxDB|
                 +-------+-------+
                         |
                         v
                 +---------------+
                 |STATE_RECONNECT|
                 +-------+-------+
                         |
                         v
                 +---------------+
                 | STATE_FINISH  |--- DONE pin --> Power Off
                 | TPL5110 DONE  |--- (fallback) --> WDT loop
                 +---------------+
```

---

## 1.4 Module Dependency Graph

```
main_1.cpp
  |
  +-- sensor_v2.cpp / sensor_v2.h
  |     RS485sensor       - Modbus RTU read/write with median filtering
  |     dataProcess       - Median calculation helpers (16-bit and 32-bit)
  |     batteryRead()     - ADC battery voltage with 64-sample averaging
  |     |
  |     +-- ModbusMaster (4-20ma/ModbusMaster@^2.0.1)
  |
  +-- Memory.cpp / Memory.h
  |     Memory             - LittleFS CSV write/append/read/rotate
  |     |
  |     +-- LittleFS (ESP-IDF built-in)
  |     +-- sensor_v2.h    - DataRecord, SensorData, timeStruct types
  |
  +-- GsmHandler.cpp / GsmHandler.h
  |     GsmHandler         - SIM800 init, GPRS, MQTT, NTP, SMTP, OTA
  |     |
  |     +-- TinyGSM (vshymanskyy/TinyGSM@^0.12.0)
  |     +-- PubSubClient (knolleary/PubSubClient@^2.8)
  |     +-- utilities.h    - Pin definitions, timeouts, MQTT defaults
  |
  +-- WifiApServer.cpp / WifiApServer.h
  |     WifiApServer       - WiFi AP, HTTP web server, auth sessions
  |     |
  |     +-- WiFi, WebServer (ESP-IDF built-in)
  |     +-- Preferences    - NVS key-value store
  |     +-- Memory         - File download / delete handlers
  |     +-- GsmHandler     - Settings forms (MQTT, InfluxDB, OTA)
  |     +-- sensor_v2.h    - SystemStatus, SensorData types
  |
  +-- NimBLE-Arduino (h2zero/NimBLE-Arduino@^1.4.2)
  |     BLE scan, connect, NUS subscribe, GATT characteristic read
  |
  +-- utilities.h
  |     Pin assignments, baud rates, timeouts, default credentials,
  |     firmware version string
  |
  +-- Platform libraries
        Arduino.h, HardwareSerial, esp_task_wdt, Preferences,
        HTTPClient, Update (OTA), LittleFS
```

### Source File Summary

| File               | Lines | Purpose                                           |
|--------------------|-------|---------------------------------------------------|
| `main_1.cpp`       | 1252  | State machine, BLE client logic, InfluxDB, OTA    |
| `sensor_v2.cpp`    | 382   | Modbus sensor reads, median filtering, battery ADC |
| `Memory.cpp`       | 348   | LittleFS CSV I/O, record parsing, storage rotation |
| `GsmHandler.cpp`   | 320   | SIM800 driver, MQTT client, NTP sync, SMTP email  |
| `WifiApServer.cpp` | 1800+ | WiFi AP web portal, settings, file browser, auth   |
| `utilities.h`      | 109   | Pin map, constants, default configuration          |

### External Library Dependencies

| Library                     | Version  | Purpose                        |
|-----------------------------|----------|--------------------------------|
| ModbusMaster                | ^2.0.1   | Modbus RTU master protocol     |
| PubSubClient                | ^2.8     | MQTT publish/subscribe         |
| TinyGSM                     | ^0.12.0  | SIM800 AT command abstraction  |
| NimBLE-Arduino              | ^1.4.2   | BLE central (scan/connect/NUS) |

---

## 1.5 Boot Sequence

From power-on to entering the main loop, the firmware executes the following
steps in `setup()`:

```
 1. Serial.begin(115200)                  -- USB CDC debug console
    delay(500 ms)

 2. esp_task_wdt_init(45, true)           -- Hardware watchdog, 45 s timeout
    esp_task_wdt_add(NULL)                -- Register current task

 3. pinMode(A0, INPUT)                    -- Battery voltage ADC
    pinMode(D2, OUTPUT)
    digitalWrite(D2, LOW)                 -- TPL5110 DONE held LOW initially

 4. RS485Serial.begin(9600, 8N1, D4, D10) -- UART1 for Modbus sensors
    modbusSensor.begin(&RS485Serial)      -- Initialise ModbusMaster instance

 5. GSM_SERIAL.begin(9600, 8N1, D7, D6)  -- UART0 for SIM800 modem

 6. LittleFS.begin(true)                  -- Mount flash filesystem
                                           -- (format on failure)

 7. initBLE()                             -- Read NVS key "bleEnable"
                                           -- If true: NimBLEDevice::init()
                                           -- Configure active scan params

 8. NVS: read "bleSavedMac"               -- Load previously paired BLE MAC
    If valid (17 chars):                   -- Set bleConnPending = true
      schedule auto-reconnect              -- for STATE_WIFI_AP

 9. Populate SystemStatus struct          -- Wire global pointers:
    sysStatus.sensor  = &modbusSensor.currentSensor
    sysStatus.time    = &currentTime
    sysStatus.battMv  = &batteryVoltage
    sysStatus.gsmAvail= &gsmAvailable
    sysStatus.memory  = &internalMemory
    sysStatus.gsm     = &gsmHandler
    (and BLE device arrays, NVS strings, etc.)

10. batteryVoltage = batteryRead()         -- 64-sample ADC average
                                           -- Voltage divider: (R1+R2)/R2
                                           -- R1 = R2 = 100 kOhm

11. Enter loop() at STATE_WIFI_AP          -- Main state machine begins
```

The initial state is always `STATE_WIFI_AP`. The watchdog timer is fed at every
state transition, during long-running operations (GSM init, file I/O, BLE
scans), and explicitly within the main loop.

---

## 1.6 Duty Cycle / Power Lifecycle

The station uses the TPL5110 nanotimer as its primary power management
controller. This design eliminates quiescent current draw between measurement
cycles, maximising battery life.

### Normal Operation (per wake cycle)

```
               TPL5110 timer interval
              (set by external resistor,
               e.g. 10 min / 30 min / 60 min)

  Power OFF ──┐                               ┌── Power OFF
              │                               │
              └─> Power ON                    │
                   |                          │
                   |  Full state machine      │
                   |  (WiFi AP, BLE, GSM,     │
                   |   sensors, save, InfluxDB)│
                   |                          │
                   |  STATE_FINISH:           │
                   |  DONE = LOW (50 ms)      │
                   |  DONE = HIGH (2 s hold)  │
                   |                          │
                   └── TPL5110 cuts power ────┘
```

### Timeline

1. **TPL5110 wakes the ESP32** at the configured interval. The ESP32 receives
   full power and begins the boot sequence.
2. **State machine runs.** The entire cycle (WiFi AP, BLE, GSM init, NTP,
   weather read, soil read, save, InfluxDB upload) typically completes in 2 to
   5 minutes depending on GSM signal conditions and BLE activity.
3. **STATE_FINISH pulses the DONE pin:**
   - Pin D2 is driven `LOW` for 50 ms.
   - Pin D2 is then driven `HIGH` and held for at least 2 seconds.
   - The TPL5110 detects the rising edge and disconnects power from the ESP32.
4. **Power is cut.** The ESP32 is fully unpowered. All RAM contents are lost.
   Persistent data survives in LittleFS (flash) and NVS (preferences).

### Fallback Behaviour

If the TPL5110 fails to cut power (hardware fault, wiring issue, or the DONE
signal is not acknowledged):

- The firmware prints a warning: `[WARN] TPL5110 did not cut power`.
- It enters an infinite loop calling `feedWDT()` every 1 second.
- The hardware watchdog (45 s timeout) is kept alive indefinitely, preventing
  a reset.
- This ensures the station does not drain the battery through repeated
  boot-reset cycles.

### Persistent State Across Cycles

Since the ESP32 has no battery-backed RTC and RAM is volatile, the following
mechanisms preserve continuity:

| Data                | Storage        | Key / Path                     |
|---------------------|----------------|--------------------------------|
| Daily CSV           | LittleFS       | `/DD-MM-YYYY.csv`              |
| BLE sensor CSV      | LittleFS       | `/BLE-DD-MM-YYYY.csv`          |
| Last daily CSV path | NVS (`ws-cfg`) | `lastDailyCsv`                 |
| Paired BLE MAC      | NVS (`ws-cfg`) | `bleSavedMac`                  |
| Last OTA timestamp  | NVS (`ws-cfg`) | `lastota`                      |
| All user settings   | NVS (`ws-cfg`) | MQTT, InfluxDB, WiFi, BLE, etc |

On each wake, the NTP state reconstructs the current timestamp from either the
GSM network clock, NTP, or (as a last resort) the most recent timestamp in the
daily CSV plus a 10-minute increment.
