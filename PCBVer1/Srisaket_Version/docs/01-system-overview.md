# Module 1 -- System Overview

**All-in-One Weather Station -- Srisaket Version**
Firmware v2.3.5 | Build 18-05-2026
IDEA Laboratory @ KMUTT

---

## 1. Project Introduction

The Srisaket Weather Station is a battery-powered, duty-cycled environmental
monitoring device built around the Seeed XIAO ESP32-C3 module.  It is designed
for unattended field deployment: the device wakes on a hardware timer, reads
local sensors, uploads data over GSM/GPRS, and powers itself back down -- all
within a single duty cycle typically lasting 3--5 minutes.

### 1.1 Primary Functions

| Function | Description |
|---|---|
| **Soil sensing** | 7-in-1 RS-485 soil sensor (humidity, temperature, EC, pH, N, P, K) |
| **Weather sensing** | 9-in-1 RS-485 weather station (wind speed/direction, air temp/humidity, CO2, pressure, illuminance, rainfall, solar irradiance) |
| **BLE gateway** | Nordic UART Service (NUS) client for Sniffer Portal BLE sensor devices |
| **Data upload** | MQTT publish and InfluxDB v2 line-protocol write over GSM/GPRS (SIM800) |
| **Local storage** | LittleFS CSV files with automatic rollover at configurable thresholds |
| **Configuration portal** | WiFi AP web server with authentication, live data view, BLE scan/connect, and OTA controls |
| **Remote firmware update** | OTA over GSM via custom HTTP server; also supports ArduinoOTA over WiFi AP |
| **Email alerts** | SMTP email for login-failure alarms and periodic status reports (via SIM800 AT commands) |

### 1.2 Key Specifications

| Parameter | Value | Source |
|---|---|---|
| MCU | ESP32-C3 (RISC-V, single-core, 160 MHz) | `platformio.ini` |
| Flash | 4 MB, partitioned: 2 x 1.625 MB OTA + 720 KB LittleFS | `partitions_ota_4mb.csv` |
| Firmware version | 2.3.5 | `utilities.h` |
| Build date | 18-05-2026 | `utilities.h` |
| Framework | Arduino (PlatformIO) | `platformio.ini` |
| BLE stack | NimBLE (`CONFIG_BT_NIMBLE_ENABLED=1`) | `platformio.ini` |
| Debug level | 0 (none) | `platformio.ini` `CORE_DEBUG_LEVEL=0` |
| Watchdog timeout | 45 seconds | `WDT_TIMEOUT_SEC` in `utilities.h` |
| Serial debug baud | 115200 | `SERIAL_BAUDRATE` in `utilities.h` |

### 1.3 External Libraries

| Library | Version | Purpose |
|---|---|---|
| `4-20ma/ModbusMaster` | ^2.0.1 | Modbus RTU master for RS-485 sensor reads |
| `knolleary/PubSubClient` | ^2.8 | MQTT client (used over TinyGsmClient) |
| `vshymanskyy/TinyGSM` | ^0.12.0 | SIM800 GSM modem abstraction |
| `h2zero/NimBLE-Arduino` | ^1.4.2 | BLE 5.0 client (scan, GATT, NUS) |

---

## 2. Architecture Block Diagram

```
                        +-----------------------------------------+
                        |          ESP32-C3 (XIAO)                |
                        |         160 MHz / 4 MB Flash            |
                        |                                         |
  Battery (3.7 V) ---->| A0 (ADC)    Battery voltage divider     |
                        |   R1 = 100k, R2 = 100k                 |
                        |                                         |
                        | D2 (GPIO) --------------------------+  |
                        |            ^  TPL5110 DONE pin       |  |
                        |            |                         |  |
  TPL5110 Timer ------>| EN         |   Power-on trigger      |  |
  (duty cycle)         |            |                         |  |
                        |            |                         |  |
                        +--- UART ---+--- SPI ----+--- BLE ----+  |
                        |            |            |            |  |
              UART1     v            v            v            v  |
    +-----------------------+  +----------+  +---------+  +------+|
    |  RS-485 Transceiver   |  | LittleFS |  |  NVS    |  | NimBLE|
    |  RX: D4  TX: D10      |  | (720 KB) |  |(Prefs)  |  | Stack |
    +-----------+-----------+  +----+-----+  +----+----+  +--+---+
                |                   |              |          |
        +-------+-------+     CSV data       Config       +---+
        |               |     files         key-value     |
   +----+-----+  +------+-----+          store           |
   | 7-in-1   |  | 9-in-1     |                         |
   | SOIL     |  | WEATHER    |                    +-----+------+
   | Slave 03 |  | Slave 01   |                    | BLE Sensor |
   | Modbus   |  | Modbus     |                    | (NUS /     |
   | 9600 baud |  | 9600 baud  |                    |  GATT)     |
   +----------+  +------------+                    +------------+

          UART0 (shared with USB)
    +-------------------------------------------+
    |              SIM800 GSM Modem              |
    |  RX: D7    TX: D6    9600 baud            |
    |                                            |
    |  GSM/GPRS ----> MQTT Broker (TCP)          |
    |             ----> InfluxDB v2 (HTTP)        |
    |             ----> OTA Server (HTTP)         |
    |             ----> SMTP Email                |
    +-------------------------------------------+

          WiFi (AP mode only, no STA)
    +-------------------------------------------+
    |          WiFi AP Web Portal               |
    |  SSID: WeatherStation_AP                  |
    |  Pass: 12345678    Channel: 1             |
    |  Port: 80 (HTTP)                          |
    |  Also: ArduinoOTA on same interface       |
    +-------------------------------------------+
```

### 2.1 Pin Assignment Summary

| Pin | GPIO/ADC | Function | Direction |
|---|---|---|---|
| `D4` | GPIO | RS-485 UART1 RX | Input |
| `D10` | GPIO | RS-485 UART1 TX | Output |
| `D7` | GPIO | GSM UART0 RX | Input |
| `D6` | GPIO | GSM UART0 TX | Output |
| `D2` | GPIO | TPL5110 DONE signal | Output |
| `A0` | ADC | Battery voltage sense (divider input) | Input |

### 2.2 UART Allocation

| UART | Peripheral | Baud Rate | Pins |
|---|---|---|---|
| UART0 (`HardwareSerial(0)`) | SIM800 GSM | 9600 | D7 (RX), D6 (TX) |
| UART1 (`HardwareSerial(1)`) | RS-485 Modbus | 9600 | D4 (RX), D10 (TX) |
| USB CDC | Debug serial | 115200 | USB |

Note: UART0 is shared between the GSM modem and the USB serial debug
port.  The GSM modem takes ownership of UART0 at init time via
`GsmHandler::init()`.

---

## 3. State Machine Flow

The firmware operates as a single-threaded state machine in `loop()`.  Each
state has a maximum duration (timeout); on expiry the machine advances to the
fallback state.  This prevents the device from hanging indefinitely in any
single state.

### 3.1 State Table

| # | State | Timeout | Fallback | Description |
|---|---|---|---|---|
| 0 | `STATE_WIFI_AP` | 300,000 ms (5 min) | `STATE_GSM_INIT` | WiFi AP + BLE scan/connect (minimum 60 s hold) |
| 1 | `STATE_GSM_INIT` | 120,000 ms (2 min) | `STATE_NTP` | Initialize SIM800, register network, open GPRS |
| 2 | `STATE_NTP` | 30,000 ms | `STATE_WEATHER` | Get network time (GSM RTC or NTP); fallback to last-known time + 10 min increment |
| 3 | `STATE_WEATHER` | 30,000 ms | `STATE_SOIL` | 15 s settle delay, then read 9-in-1 weather sensor via Modbus |
| 4 | `STATE_SOIL` | 30,000 ms | `STATE_SAVE` | 15 s settle delay, then read 7-in-1 soil sensor via Modbus |
| 5 | `STATE_SAVE` | 10,000 ms | `STATE_RECONNECT` | Save data to LittleFS CSV; optionally rollover; send to InfluxDB v2 |
| 6 | `STATE_RECONNECT` | 120,000 ms | `STATE_FINISH` | Legacy placeholder; immediately advances to `STATE_FINISH` |
| 7 | `STATE_PUBLISH` | 60,000 ms | `STATE_FINISH` | Legacy placeholder; immediately advances to `STATE_FINISH` |
| 8 | `STATE_FINISH` | -- (none) | -- | Pulse TPL5110 DONE pin; enter infinite WDT-fed loop |

### 3.2 State Transition Diagram

```
                      Power On (TPL5110)
                            |
                            v
                    +------------------+
                    | STATE_WIFI_AP    |
                    | Max: 5 min       |
                    | Min hold: 1 min  |
                    |                  |
                    | - Start WiFi AP  |
                    | - BLE scan/conn  |
                    | - Web portal     |
                    +--------+---------+
                             |
               +-------------+--------------+
               |                            |
         BLE data received           Timeout / AP idle
         (after 1 min min)           (after 1 min min)
               |                            |
               +-------------+--------------+
                             |
                             v
                    +------------------+
                    | STATE_GSM_INIT   |
                    | Timeout: 120 s   |
                    |                  |
                    | - Init SIM800    |
                    | - GPRS connect   |
                    | - Publish MQTT   |
                    |   heartbeat      |
                    +--------+---------+
                             |
                        Success / Fail
                             |
                             v
                    +------------------+
                    | STATE_NTP        |
                    | Timeout: 30 s    |
                    |                  |
                    | - GSM RTC time   |
                    | - or NTP sync    |
                    | - or fallback    |
                    |   (last + 10min) |
                    | - OTA check      |
                    +--------+---------+
                             |
                             v
                    +------------------+
                    | STATE_WEATHER    |
                    | Timeout: 30 s    |
                    | Settle: 15 s     |
                    |                  |
                    | - Modbus read    |
                    |   Slave 0x01     |
                    |   Reg 0x01F4,    |
                    |   Len 16         |
                    +--------+---------+
                             |
                        Read OK / Timeout
                             |
                             v
                    +------------------+
                    | STATE_SOIL       |
                    | Timeout: 30 s    |
                    | Settle: 15 s     |
                    |                  |
                    | - Modbus read    |
                    |   Slave 0x03     |
                    |   Reg 0x0000,    |
                    |   Len 7          |
                    +--------+---------+
                             |
                        Read OK / Timeout
                             |
                             v
                    +------------------+
                    | STATE_SAVE       |
                    | Timeout: 10 s    |
                    |                  |
                    | - Rollover check |
                    | - CSV append     |
                    | - InfluxDB write |
                    +--------+---------+
                             |
                             v
                    +------------------+
                    | STATE_RECONNECT  |
                    | (immediate pass) |
                    +--------+---------+
                             |
                             v
                    +------------------+
                    | STATE_FINISH     |
                    |                  |
                    | - Pulse DONE pin |
                    | - Infinite loop  |
                    |   with WDT feed  |
                    +------------------+
```

### 3.3 STATE_WIFI_AP Exit Conditions

The WiFi AP state has the most complex exit logic because it must balance
multiple concerns:

| Condition | Min elapsed | Action |
|---|---|---|
| OTA requested from web UI | 0 s (anytime) | Close AP, jump to `STATE_GSM_INIT` |
| BLE data received (GATT or NUS notification parsed) | >= 60 s | Close AP, advance to `STATE_GSM_INIT` |
| AP idle timeout (no connected clients) | >= 60 s | Close AP, advance to `STATE_GSM_INIT` |
| Hard timeout (5 min elapsed) | >= 300 s | Force advance to `STATE_GSM_INIT` |

Before the 60-second minimum window expires, none of the normal exit
conditions are checked.  The only early exit is an OTA update request from
the web portal.

### 3.4 NTP Fallback Chain

The time-synchronization logic in `STATE_NTP` follows this priority order:

1. **GSM RTC** -- Read via `TinyGsm::getGSMDateTime(DATE_FULL)`.  If the
   returned date/time passes validation (year >= 2024, valid month/day/hour/min),
   it is used directly.
2. **NTP sync** -- If GSM RTC is invalid, issue AT+CNTP to
   `pool.ntp.org` with a 60-second timeout, then re-read GSM RTC.
3. **Last backup + increment** -- If all network time sources fail, read the
   last timestamp from the most recent daily CSV file via
   `readLastTimeFromBackup()` and add `TIME_INCREMENT_MINUTES` (10 minutes).

---

## 4. Module Dependency Graph

The codebase is organized into five source modules plus a central
configuration header.  The diagram below shows compile-time `#include`
dependencies (solid arrows) and runtime data-flow dependencies (dashed
arrows).

```
                           utilities.h
                          (pins, constants,
                           defaults, config)
                               ^
                               |  included by all
          +--------------------+-------------------+
          |                    |                   |
     sensor_v2.h          GsmHandler.h        WifiApServer.h
     sensor_v2.cpp        GsmHandler.cpp      WifiApServer.cpp
          ^                    ^                    ^
          |                    |                    |
          +--- Memory.h -------+                    |
          |   Memory.cpp       |                    |
          |                    |                    |
          +--------------------+--------------------+
          |                                         |
          v                                         v
     main_1.cpp  <------ uses all modules ----------+
     (state machine,
      BLE, InfluxDB,
      OTA, heartbeat)
```

### 4.1 Include Dependency Details

| Source File | Includes |
|---|---|
| `utilities.h` | `Arduino.h`, `HardwareSerial.h` |
| `sensor_v2.h` | `utilities.h`, `ModbusMaster.h` |
| `sensor_v2.cpp` | `sensor_v2.h`, `esp_task_wdt.h` |
| `GsmHandler.h` | `utilities.h`, `sensor_v2.h`, `TinyGsmClient.h`, `PubSubClient.h` |
| `GsmHandler.cpp` | `GsmHandler.h`, `esp_task_wdt.h` |
| `Memory.h` | `FS.h`, `LittleFS.h`, `sensor_v2.h` |
| `Memory.cpp` | `Memory.h`, `esp_task_wdt.h` |
| `WifiApServer.h` | `Arduino.h`, `WiFi.h`, `WebServer.h`, `Preferences.h`, `sensor_v2.h`, `Memory.h`, `GsmHandler.h` |
| `WifiApServer.cpp` | `WifiApServer.h`, `utilities.h`, `LittleFS.h`, `esp_task_wdt.h`, `ArduinoOTA.h`, `HTTPClient.h`, `Update.h` |
| `main_1.cpp` | `Arduino.h`, `HardwareSerial.h`, `esp_task_wdt.h`, `NimBLEDevice.h`, `HTTPClient.h`, `Update.h`, `Preferences.h`, `sensor_v2.h`, `Memory.h`, `GsmHandler.h`, `WifiApServer.h` |

### 4.2 Module Responsibility Summary

| Module | Lines (approx) | Responsibility |
|---|---|---|
| `utilities.h` | 109 | Central configuration: pin definitions, timeouts, default credentials, sensor Modbus addresses |
| `sensor_v2.cpp/.h` | 382 / 97 | RS-485 Modbus RTU reads for soil and weather sensors; median filtering; battery ADC read |
| `GsmHandler.cpp/.h` | 320 / 59 | SIM800 modem init, GPRS connect, MQTT connect/publish, NTP sync, SMTP email, dynamic MQTT config |
| `Memory.cpp/.h` | 348 / 42 | LittleFS file operations: CSV write/append, data serialization, rollover, line counting, record parsing |
| `WifiApServer.cpp/.h` | 1643 / 150 | WiFi AP web portal: authentication, live data, file management, settings forms, BLE scan/connect UI, OTA trigger |
| `main_1.cpp` | 1252 | State machine, BLE client (NimBLE scan/connect/NUS), InfluxDB v2 upload, remote OTA, heartbeat, time management |

---

## 5. Boot Sequence

The following steps execute in `setup()` on every power-on:

```
 Step  Action                                    Detail
 ----  ------                                    ------
  1    Serial.begin(115200)                      Debug console
       delay(500 ms)

  2    esp_task_wdt_init(45, true)               Initialize watchdog timer
       esp_task_wdt_add(NULL)                    Subscribe main task to WDT

  3    Print firmware banner                     "[FW] All-in-One Weather Station v2.3.5"

  4    pinMode(A0, INPUT)                        Battery voltage sense pin
       pinMode(D2, OUTPUT)
       digitalWrite(D2, LOW)                     TPL5110 DONE held LOW initially

  5    RS485Serial.begin(9600, 8N1, D4, D10)     Start UART1 for RS-485
       modbusSensor.begin(&RS485Serial)          Pass serial port to Modbus master

  6    GSM_SERIAL.begin(9600, 8N1, D7, D6)       Start UART0 for SIM800

  7    LittleFS.begin(true)                      Mount filesystem (format on fail)
                                                   Partition: 720 KB at 0x350000

  8    initBLE()                                 Initialize NimBLE if enabled in NVS
       - Read "bleEnable" from Preferences
       - NimBLEDevice::init("WeatherStation-GW")
       - Set power to ESP_PWR_LVL_P3
       - Configure active scan (interval=100, window=99)

  9    Load BLE saved MAC from NVS               "bleSavedMac" in "ws-cfg" namespace
       If valid (17 chars):
         - Set bleTargetMac and bleConnPending=true
         - Auto-reconnect will occur in STATE_WIFI_AP

 10    Load last OTA timestamp from NVS          "lastota" key for web portal display

 11    Populate SystemStatus struct               Wire global pointers for web portal:
       sysStatus.sensor     -> modbusSensor.currentSensor
       sysStatus.time       -> currentTime
       sysStatus.battMv     -> batteryVoltage
       sysStatus.gsmAvail   -> gsmAvailable
       sysStatus.gsmRssi    -> gsmRssi
       sysStatus.bleActive  -> bleActive
       sysStatus.memory     -> internalMemory
       sysStatus.gsm        -> gsmHandler
       sysStatus.bleDevices -> bleDevices[]
       ... (13 more fields for BLE, OTA, NUS state)

 12    batteryRead()                             64-sample ADC average on A0
       - Voltage divider: R1=100k, R2=100k (ratio=2.0)
       - Calibration factor: 1.000
       - Result in millivolts stored in batteryVoltage

 13    Print "===== SETUP COMPLETE ====="         Enter loop() -> STATE_WIFI_AP
```

### 5.1 NVS (Preferences) Namespace

All persistent configuration is stored in the NVS namespace `"ws-cfg"`,
managed through the Arduino `Preferences` API.

| Key | Type | Default | Purpose |
|---|---|---|---|
| `webpass` | String | `"admin"` | Web portal login password |
| `apTimeout` | UInt | 5 | AP idle timeout in minutes (0=never) |
| `bleEnable` | Bool | false | Enable BLE client scanning |
| `bleSavedMac` | String | `""` | Last connected BLE device MAC |
| `srcModbus` | Bool | true | Enable Modbus RS-485 sensors |
| `srcBle` | Bool | false | Enable BLE sensor data source |
| `fileInterval` | UInt | 10 | New data file every N minutes |
| `influxEn` | Bool | false | Enable InfluxDB v2 upload |
| `influxHost` | String | `""` | InfluxDB server host/IP |
| `influxPort` | UInt | 8086 | InfluxDB port |
| `influxToken` | String | `""` | InfluxDB API token |
| `influxOrg` | String | `""` | InfluxDB organization |
| `influxBucket` | String | `""` | InfluxDB bucket |
| `mqttEnable` | Bool | true | Enable MQTT publish |
| `mqttHost` | String | (compile-time) | MQTT broker host |
| `mqttPort` | UInt | 1883 | MQTT broker port |
| `mqttUser` | String | (compile-time) | MQTT username |
| `mqttPass` | String | (compile-time) | MQTT password |
| `emailEnable` | Bool | false | Enable email notifications |
| `emailuser` | String | `""` | Gmail sender address |
| `emailpass` | String | `""` | Gmail app password |
| `emailto` | String | `""` | Primary recipient |
| `emaillightto` | String | `""` | Lightning report recipient |
| `emailFreqH` | UInt | 24 | Email notification frequency (hours) |
| `emailalarm` | Bool | false | Login failure alarm enabled |
| `otaserver` | String | `""` | OTA server base URL |
| `otaproject` | String | `""` | OTA project name |
| `otadevice` | String | `"All-in-One"` | OTA device type |
| `otadlpass` | String | `""` | OTA download password |
| `otapass` | String | `"admin"` | ArduinoOTA password |
| `otainterval` | UInt | 24 | Auto-check interval (hours) |
| `otaboot` | Bool | false | Check OTA on every boot |
| `ntpEnable` | Bool | true | Enable NTP sync on GSM connect |
| `memRollover` | UInt | 80 | LittleFS rollover threshold (%) |
| `memRolloverEn` | Bool | true | Enable rollover |
| `lastota` | String | `"Never"` | Last successful OTA timestamp |
| `lastDailyCsv` | String | `"/DATA.csv"` | Path to current daily CSV |

---

## 6. Duty Cycle / Power Lifecycle

The Srisaket Weather Station is a single-shot device controlled by the
TPL5110 nanopower timer.  Each duty cycle follows this sequence:

### 6.1 Power-On

```
    TPL5110 Timer
    (externally configured,
     typically 5--10 min interval)
         |
         |  Connects VCC to circuit
         v
    ESP32-C3 boots
    setup() runs (see Section 5)
    loop() enters STATE_WIFI_AP
```

The TPL5110 timer interval is set by an external resistor on the TPL5110
board.  It is not configurable in firmware.  Typical deployment intervals
are 5 or 10 minutes.

### 6.2 DONE Pin Signaling

When the state machine reaches `STATE_FINISH`, the firmware pulses the
TPL5110 DONE pin (D2) to signal that work is complete:

```
    STATE_FINISH entry
         |
         |  delay(3000 ms)              -- Final serial output flush
         |  Serial.flush()
         |
         |  digitalWrite(D2, LOW)       -- Ensure LOW baseline
         |  delay(50 ms)
         |
         |  digitalWrite(D2, HIGH)      -- Pulse DONE
         |  delay(2000 ms)              -- Hold HIGH for 2 seconds
         |
         |  Serial.println("[WARN] TPL5110 did not cut power")
         |
         |  while (1) {                 -- Infinite fallback loop
         |      esp_task_wdt_reset();       Feed WDT to stay alive
         |      delay(1000);
         |  }
```

The TPL5110 requires the DONE pin to be driven HIGH.  Once it detects the
rising edge (held for a sufficient duration), the TPL5110 disconnects power
from the load.  The 2-second hold ensures the TPL5110 reliably detects the
signal.

### 6.3 Fallback Behavior

If the TPL5110 does not cut power after the DONE pulse (hardware fault,
wiring issue, or timer misconfiguration), the firmware enters an infinite
loop that continuously feeds the hardware watchdog.  This prevents the WDT
from triggering a system reset, which would restart the state machine and
cause an unintended second duty cycle.

The serial message `"[WARN] TPL5110 did not cut power"` is printed once
before entering the fallback loop, providing a diagnostic indicator if the
debug console is monitored.

### 6.4 Typical Duty Cycle Timeline

The following table shows a typical duty cycle with approximate durations.
Actual times vary based on sensor response, GSM network conditions, and
whether the WiFi AP is accessed.

| Phase | Duration | Cumulative |
|---|---|---|
| Boot + setup() | ~2 s | 2 s |
| STATE_WIFI_AP (min hold) | 60 s | 62 s |
| STATE_GSM_INIT | 15--120 s | 77--182 s |
| STATE_NTP | 2--30 s | 79--212 s |
| STATE_WEATHER (15 s settle + read) | 18--30 s | 97--242 s |
| STATE_SOIL (15 s settle + read) | 18--30 s | 115--272 s |
| STATE_SAVE | 1--10 s | 116--282 s |
| STATE_RECONNECT (pass-through) | <1 s | 116--282 s |
| STATE_FINISH (DONE pulse) | 5 s | 121--287 s |
| **Total (typical)** | **~2--5 min** | -- |

### 6.5 Power Architecture Notes

- The ESP32-C3 has no deep-sleep in this design.  Power is physically
  removed by the TPL5110 between duty cycles.  All RAM state is lost.
- Persistent data survives across cycles in LittleFS (CSV files) and NVS
  (Preferences / configuration).
- The SIM800 modem is not explicitly powered down before the DONE pulse.
  The TPL5110 removes power to the entire board, including the GSM modem.
- The BLE saved MAC in NVS enables automatic reconnection to the last
  known BLE sensor device on the next boot, without requiring the web portal.

---

*End of Module 1 -- System Overview.  Refer to subsequent modules for
detailed documentation of each subsystem.*
