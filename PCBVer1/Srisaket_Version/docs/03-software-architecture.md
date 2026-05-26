# Module 3: Software Architecture

> All-in-One Weather Station -- Srisaket Version
> Firmware v2.3.5 | IDEA Laboratory @ KMUTT

---

## 3.1 File / Module Map

| File | Responsibility |
|---|---|
| `src/main_1.cpp` | Top-level state machine (8 states), BLE client (scan/connect/NUS/GATT), InfluxDB v2 HTTP upload, OTA update via GSM, heartbeat MQTT publish, time management (NTP/RTC/fallback), daily CSV rotation, watchdog feeding |
| `src/sensor_v2.cpp` / `include/sensor_v2.h` | Modbus RS-485 sensor reads with multi-stage noise rejection (median of 3, retry up to 5x, zero-retry, UART recovery), `SensorData` struct, `DataRecord` serialization, battery ADC read (64-sample averaging) |
| `src/GsmHandler.cpp` / `include/GsmHandler.h` | SIM800 modem initialization, GPRS data connection, MQTT publish/subscribe, GSM RTC time query (`+CCLK`), NTP sync (`+CNTP` to `pool.ntp.org`), SMTP email delivery, AT command passthrough, signal quality reporting |
| `src/Memory.cpp` / `include/Memory.h` | LittleFS CSV file create/append/read, `DataRecord` serialization to CSV rows, storage usage monitoring, file rotation when usage exceeds threshold, line-level read and removal for batch publish |
| `src/WifiApServer.cpp` / `include/WifiApServer.h` | WiFi Access Point (AP), HTTP web server with session authentication, live sensor dashboard, BLE scan/connect/disconnect UI, file browser/download/delete, settings management (all NVS keys), OTA trigger, event log, system reboot |
| `include/utilities.h` | Central configuration: firmware version, serial baud rates, RS-485 and GSM pin assignments, Modbus slave IDs and register maps, battery voltage divider constants, WDT timeout, state timeouts, WiFi AP credentials, MQTT defaults, InfluxDB defaults, OTA defaults, email defaults, memory thresholds |

---

## 3.2 State Machine Detail

The firmware operates as a single-threaded state machine in `loop()`. Each cycle wakes from deep sleep (TPL5110 timer), runs through the states sequentially, and pulses the TPL5110 DONE pin to cut power in `STATE_FINISH`.

### State Transition Diagram

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
STATE_WEATHER (30 s timeout, 15 s settle)
    |
    v
STATE_SOIL (30 s timeout, 15 s settle)
    |
    v
STATE_SAVE (10 s timeout)
    |
    v
STATE_RECONNECT (120 s timeout)
    |
    v
STATE_FINISH (no timeout -- infinite loop with WDT feed)
```

`STATE_PUBLISH` exists as a defined state but is currently a passthrough to `STATE_FINISH`.

### State-by-State Reference

#### STATE_WIFI_AP

| Attribute | Value |
|---|---|
| **Purpose** | Start WiFi AP web portal and BLE client simultaneously; allow user configuration, BLE scan/connect, and OTA trigger |
| **Timeout (max)** | 300 000 ms (5 minutes), defined by `WIFI_AP_MAX_DURATION_MS` |
| **Timeout (min)** | 60 000 ms (1 minute), defined by `WIFI_AP_MIN_DURATION_MS` |
| **Entry conditions** | First state entered at boot; `wifiApStarted` is false |
| **Exit conditions (success)** | (1) BLE data received after the 1-minute minimum window -- advances immediately; (2) AP idle timeout (no connected clients) after 1-minute minimum; (3) OTA requested from web UI -- exits before the 1-minute minimum |
| **Exit conditions (timeout)** | After 5 minutes elapsed, forced transition to `STATE_GSM_INIT` |
| **Fallback behavior** | On any exit, the WiFi AP server is stopped and BLE operations cease. Transition always goes to `STATE_GSM_INIT` |

#### STATE_GSM_INIT

| Attribute | Value |
|---|---|
| **Purpose** | Initialize SIM800 modem, register on cellular network, establish GPRS data session |
| **Timeout** | 120 000 ms, defined by `GSM_INIT_TIMEOUT_MS` |
| **Entry conditions** | Entered from `STATE_WIFI_AP` |
| **Exit conditions (success)** | `GsmHandler::init()` returns true and `GsmHandler::connectNetwork()` returns true; `gsmAvailable` set to true; heartbeat published via MQTT |
| **Exit conditions (failure)** | Either `init()` or `connectNetwork()` fails; `gsmAvailable` set to false |
| **Fallback behavior** | On failure, `gsmAvailable = false` and the system continues without GSM (no InfluxDB, no MQTT, time falls back to RTC/file) |

#### STATE_NTP

| Attribute | Value |
|---|---|
| **Purpose** | Synchronize system clock via GSM RTC or NTP; fall back to last CSV timestamp + increment |
| **Timeout** | 30 000 ms, defined by `TIME_WAIT_TIMEOUT` |
| **Entry conditions** | Entered from `STATE_GSM_INIT` |
| **Exit conditions (success)** | `GsmHandler::getNetworkTime()` returns a valid `timeStruct`; daily CSV path is updated |
| **Exit conditions (failure)** | GSM time unavailable; system reads last timestamp from the most recent CSV file and increments by `TIME_INCREMENT_MINUTES` (10 min). If no CSV exists, time is zeroed |
| **Fallback behavior** | If NTP fails but GSM is available, the GSM RTC (`+CCLK`) is tried first. If both fail, the CSV-based fallback is used. The system always proceeds to `STATE_WEATHER` |

#### STATE_WEATHER

| Attribute | Value |
|---|---|
| **Purpose** | Read weather station sensor (slave 0x01) via Modbus RS-485 |
| **Timeout** | 30 000 ms, defined by `WEATHER_TIMEOUT` |
| **Settle delay** | 15 000 ms, defined by `WEATHER_SETTLE_DELAY` |
| **Entry conditions** | Entered from `STATE_NTP`; WiFi AP is stopped if still running |
| **Exit conditions (success)** | `RS485sensor::read(WEATHER, ...)` returns true |
| **Exit conditions (timeout)** | After 30 s total (including the 15 s settle), weather fields are zeroed and state advances |
| **Fallback behavior** | `clearWeatherData()` zeros all weather fields in `SensorData`. The system proceeds to `STATE_SOIL` regardless |

#### STATE_SOIL

| Attribute | Value |
|---|---|
| **Purpose** | Read soil sensor (slave 0x03) via Modbus RS-485 |
| **Timeout** | 30 000 ms, defined by `SOIL_TIMEOUT` |
| **Settle delay** | 15 000 ms, defined by `SOIL_SETTLE_DELAY` |
| **Entry conditions** | Entered from `STATE_WEATHER` |
| **Exit conditions (success)** | `RS485sensor::read(SOIL, ...)` returns true |
| **Exit conditions (timeout)** | After 30 s total (including the 15 s settle), state advances to `STATE_SAVE` |
| **Fallback behavior** | On 3 consecutive total failures, the soil section of `SensorData` is zeroed (`clearSensorSection()`). The system proceeds to `STATE_SAVE` regardless |

#### STATE_SAVE

| Attribute | Value |
|---|---|
| **Purpose** | Persist sensor data to daily CSV on LittleFS, upload to InfluxDB if GSM is available, perform storage rollover if needed |
| **Timeout** | 10 000 ms |
| **Entry conditions** | Entered from `STATE_SOIL` |
| **Exit conditions (success)** | `Memory::saveData()` writes CSV row; `sendToInfluxDB()` sends HTTP POST (if enabled and GSM available) |
| **Exit conditions (failure)** | Timeout forces advance; InfluxDB send failure is non-blocking |
| **Fallback behavior** | If storage rollover threshold is exceeded and enabled, the current CSV is deleted and recreated with only the last row preserved. State always advances to `STATE_RECONNECT` |

#### STATE_RECONNECT

| Attribute | Value |
|---|---|
| **Purpose** | Placeholder for MQTT batch publish of stored CSV records (future feature) |
| **Timeout** | 120 000 ms, defined by `RECONNECT_TIMEOUT` |
| **Entry conditions** | Entered from `STATE_SAVE` |
| **Exit conditions** | Currently an immediate passthrough to `STATE_FINISH` |
| **Fallback behavior** | Timeout would force `STATE_FINISH` |

#### STATE_PUBLISH

| Attribute | Value |
|---|---|
| **Purpose** | Placeholder for MQTT batch publish |
| **Timeout** | 60 000 ms, defined by `MQTT_PUBLISH_TIMEOUT` |
| **Entry conditions** | Not currently entered in normal flow |
| **Exit conditions** | Currently an immediate passthrough to `STATE_FINISH` |

#### STATE_FINISH

| Attribute | Value |
|---|---|
| **Purpose** | Pulse the TPL5110 DONE pin to cut power; enter infinite loop as safety fallback |
| **Timeout** | None (infinite loop) |
| **Entry conditions** | Entered from `STATE_RECONNECT` or `STATE_PUBLISH` |
| **Exit conditions** | None -- the TPL5110 should cut power after the DONE pulse |
| **Fallback behavior** | If the TPL5110 fails to cut power, the firmware enters a `while(1)` loop feeding the WDT every 1 s and prints a warning. The device must be manually reset in this case |

---

## 3.3 Data Structures

### 3.3.1 SensorData (33 bytes, packed)

Defined in `include/sensor_v2.h`. Stores all sensor readings from both the soil and weather Modbus slaves.

| Field | Type | Size (bytes) | Unit | Scale | Register |
|---|---|---|---|---|---|
| `soil_humi` | `uint16_t` | 2 | % | x10 | Reg 0 |
| `soil_temp` | `int16_t` | 2 | deg C | x10 | Reg 1 |
| `soil_ec` | `uint16_t` | 2 | uS/cm | direct | Reg 2 |
| `soil_ph` | `uint8_t` | 1 | pH | x10 (0--25.5) | Reg 3 |
| `soil_N` | `uint16_t` | 2 | mg/kg | direct | Reg 4 |
| `soil_P` | `uint16_t` | 2 | mg/kg | direct | Reg 5 |
| `soil_K` | `uint16_t` | 2 | mg/kg | direct | Reg 6 |
| `windSpeed` | `uint16_t` | 2 | m/s | x10 | Reg 0 |
| `windDir_Deg` | `uint16_t` | 2 | degrees | direct | Reg 3 |
| `air_humidity` | `uint16_t` | 2 | % | x10 | Reg 4 |
| `air_temperature` | `int16_t` | 2 | deg C | x10 | Reg 5 |
| `CO2` | `uint16_t` | 2 | ppm | direct | Reg 7 |
| `pressure` | `uint16_t` | 2 | kPa | x10 | Reg 9 |
| `illuminance` | `uint32_t` | 4 | lux | direct | Reg 10--11 (32-bit) |
| `rainfall` | `uint16_t` | 2 | mm | x10 | Reg 13 |
| `solar` | `uint16_t` | 2 | W/m^2 | direct | Reg 15 |

**Total: 13 bytes (soil) + 20 bytes (weather) = 33 bytes.**

Soil registers are read from slave 0x03 at address 0x0000, length 7.
Weather registers are read from slave 0x01 at address 0x01F4, length 16.

### 3.3.2 timeStruct

Defined in `include/sensor_v2.h`. Stores date and time with formatted string representations.

| Field | Type | Description |
|---|---|---|
| `date` | `uint8_t` | Day of month (1--31) |
| `month` | `uint8_t` | Month (1--12) |
| `year` | `uint16_t` | Full year (e.g. 2026) |
| `hour` | `uint8_t` | Hour (0--23) |
| `minute` | `uint8_t` | Minute (0--59) |
| `second` | `uint8_t` | Second (0--59) |
| `dateStr[12]` | `char[12]` | Formatted date: `"DD/MM/YYYY"` |
| `timeStr[9]` | `char[9]` | Formatted time: `"HH:MM:SS"` |

### 3.3.3 DataRecord (packed)

Defined in `include/sensor_v2.h`. Couples a timestamp with sensor data and a validity flag for CSV serialization and batch publish.

| Field | Type | Description |
|---|---|---|
| `date` | `uint8_t` | Day of month |
| `month` | `uint8_t` | Month |
| `year` | `uint8_t` | Year (last 2 digits in compact JSON) |
| `hour` | `uint8_t` | Hour |
| `minute` | `uint8_t` | Minute |
| `data` | `SensorData` | Full sensor payload (33 bytes) |
| `valid` | `uint8_t` | Validity flag: 1 = valid, 0 = invalid/placeholder |

Typedef aliases: `Packet = DataRecord`, `soilData = SensorData`, `weatherData = SensorData`.

### 3.3.4 SystemStatus

Defined in `include/WifiApServer.h`. Pointer-based struct that links the web server to live sensor data, time, battery, GSM status, BLE state, and subsystem handlers. All fields are pointers to globals in `main_1.cpp`, allowing the web server to read current state without copying.

| Field | Type | Description |
|---|---|---|
| `sensor` | `SensorData*` | Pointer to live sensor readings |
| `time` | `timeStruct*` | Pointer to current time |
| `battMv` | `uint16_t*` | Pointer to battery voltage (mV) |
| `gsmAvail` | `bool*` | Pointer to GSM availability flag |
| `gsmRssi` | `int*` | Pointer to GSM signal quality |
| `bleActive` | `bool*` | Pointer to BLE client active flag |
| `memory` | `Memory*` | Pointer to memory handler instance |
| `gsm` | `GsmHandler*` | Pointer to GSM handler instance |
| `lastOtaStr[24]` | `char[24]` | Last OTA update timestamp string |
| `bleDevices` | `BLEFoundDevice*` | BLE scan results array |
| `bleDeviceCount` | `uint8_t*` | Number of BLE devices found |
| `bleMac[18]` | `char[18]` | Local BLE MAC address |
| `bleConnDev` | `BLEConnectedDevice*` | Currently connected BLE device data |
| `bleTargetMac` | `char*` | MAC address requested for connection |
| `bleConnPending` | `bool*` | Connection request pending flag |
| `bleSavedMac` | `char*` | NVS-persisted MAC for auto-reconnect |
| `bleScanRequest` | `bool*` | Trigger flag for immediate BLE scan |
| `bleIsNus` | `bool*` | True if connected device is Nordic UART Service |
| `bleNusLastMs` | `unsigned long*` | `millis()` of last NUS JSON parse |
| `bleNusRefreshReq` | `bool*` | Trigger flag for immediate NUS data request |
| `otaCheckNow` | `bool*` | Trigger flag for OTA check on next GSM cycle |

### 3.3.5 BLEConnectedDevice

Defined in `include/WifiApServer.h`. Represents a BLE peripheral that has been connected, with its GATT characteristic data.

| Field | Type | Description |
|---|---|---|
| `mac[18]` | `char[18]` | MAC address string `"XX:XX:XX:XX:XX:XX"` |
| `name[32]` | `char[32]` | Device name from advertisement |
| `connected` | `bool` | Connection status |
| `charCount` | `uint8_t` | Number of discovered characteristics (max 16) |
| `chars[16]` | `BLECharData[16]` | Array of characteristic data |

### 3.3.6 BLECharData

Defined in `include/WifiApServer.h`. Stores a single BLE GATT characteristic's UUID, raw hex value, and ASCII representation.

| Field | Type | Description |
|---|---|---|
| `uuid[37]` | `char[37]` | UUID string `"xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"` |
| `value[64]` | `char[64]` | Hex string of raw bytes (e.g. `"1A 2B 3C "`) |
| `ascii[32]` | `char[32]` | Printable ASCII representation |

### 3.3.7 BLEFoundDevice

Defined in `include/WifiApServer.h`. Stores BLE scan results (up to 10 devices, `BLE_MAX_DEVICES`).

| Field | Type | Description |
|---|---|---|
| `name[32]` | `char[32]` | Advertisement name |
| `mac[18]` | `char[18]` | MAC address |
| `rssi` | `int8_t` | Signal strength (dBm) |

---

## 3.4 Data Flow Diagram

```
Modbus Sensors (RS-485, UART1 @ 9600 baud)
    |
    |  Slave 0x03 (Soil): 7 registers @ 0x0000
    |  Slave 0x01 (Weather): 16 registers @ 0x01F4
    v
RS485sensor::read()
    |   3 samples -> bubble sort -> median of 3
    |   Retry up to 5x on Modbus error
    |   UART recovery on error 0xE0
    |   Zero-retry if key values are 0
    v
SensorData struct (33 bytes, packed)
    |
    +---> Memory::saveData()
    |         |   Serialize to CSV row: "DD/MM/YYYY,HH:MM:SS,<fields...>"
    |         |   Append to daily CSV file on LittleFS
    |         |   Rollover: delete + recreate if storage > threshold %
    |         v
    |     LittleFS (SPIFFS-compatible flash)
    |         |
    |         +---> /DD-MM-YYYY.csv (weather data)
    |         +---> /BLE-DD-MM-YYYY.csv (BLE sniffer data)
    |
    +---> sendToInfluxDB()
    |         |   InfluxDB v2 line protocol via HTTP POST
    |         |   TCP connection over GSM GPRS (TinyGsmClient)
    |         |   Includes sniffer_watchdog measurement if BLE NUS active
    |         v
    |     InfluxDB Server (default: 119.59.103.220:8086)
    |
    +---> (Future: MQTT batch publish from stored CSV records)

BLE NUS/GATT Sensor (NimBLE client)
    |
    |  Nordic UART Service (NUS):
    |    Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
    |    TX notify: 6e400003-... (sensor -> ESP32)
    |    RX write:  6e400002-... (ESP32 -> sensor)
    |
    |  Or standard GATT: read all readable characteristics
    v
parseNusJson() / GATT read
    |
    +---> saveBleDataToCsv()
    |         |   Parse JSON fields: temp, hum, tmp117, delta, rain,
    |         |                     leaf, par, soil, ts
    |         |   Append to /BLE-DD-MM-YYYY.csv on LittleFS
    |         v
    |     LittleFS BLE CSV
    |
    +---> BLEConnectedDevice struct (displayed on web portal)
    |
    +---> Appended to InfluxDB sniffer_watchdog measurement
```

---

## 3.5 Sensor Reading Pipeline

The RS-485 sensor read uses a multi-stage noise rejection and error recovery pipeline implemented in `RS485sensor::read()`.

### Stage 1: Median Filtering (3 Samples)

- Take 3 successful Modbus reads (`readAttempt = 3`)
- Each register value is stored in `rawBuffer[attempt][register]`
- After 3 samples, bubble-sort the values and select the median
- For 32-bit values (illuminance), use `getMedian32()`
- pH is clamped to `uint8_t` (max 255, representing pH 25.5)

### Stage 2: Modbus Retry (up to 5x)

- On Modbus failure (`ku8MBSuccess` not returned), increment `retry_FLAGS`
- Total attempts capped at `maxRetry = 5` across all retries
- Each retry reinitializes the Modbus master with `modbus.begin(slaveID, *serialPort)`
- 1-second delay between retries (non-0xE0 errors)

### Stage 3: Zero-Value Re-Read

- After successful median computation, check if key values are zero
- Soil: re-read if `soil_humi == 0`, `soil_temp == 0`, or `soil_ec == 0`
- Weather: re-read if `air_humidity == 0`, `air_temperature == 0`, or `CO2 == 0`
- Zero-retry runs up to `maxRetry` (5) additional times with 1.5-second delays
- Each zero-retry performs a full 3-sample median read

### Stage 4: UART Recovery (Error 0xE0)

- On Modbus error code `0xE0` (invalid slave ID), perform full UART recovery:
  1. Call `HardwareSerial::end()` on the RS-485 serial port
  2. Wait 100 ms
  3. Reinitialize with `HardwareSerial::begin(9600, SERIAL_8N1, RX, TX)`
  4. Wait 500 ms
  5. Flush any stale data from RX buffer
  6. Wait 200 ms
  7. Reinitialize Modbus master with extended post-transmission delay (1000 us)
- On first call to `read()`, UART recovery is always performed (`_uartReady` flag)

### Stage 5: Consecutive Failure Zero-Out

- Track `_consecutiveFailCount` (max 3, `_maxConsecutiveFail`)
- If all reads fail (0 successful samples), increment counter
- After 3 consecutive total failures, call `clearSensorSection()` to zero the relevant portion of `currentSensor` and reset the counter
- This prevents stale data from persisting indefinitely

### Pipeline Flow Diagram

```
RS485sensor::read(sensorType, slaveID, address, length, serial)
    |
    v
[UART Recovery if first call] --> [Flush + Settle]
    |
    v
+---> Modbus readHoldingRegisters()
|    |
|    +-- Success --> Store in rawBuffer[] --> startAttempt++
|    |
|    +-- Error 0xE0 --> UART Recovery --> re-init Modbus
|    |                     |
|    +-- Other Error --> delay(1000) --> re-init Modbus
|    |
|    retry_FLAGS < 5 AND startAttempt < 3 ?
|    |
+-- yes --> repeat
     |
     no
     |
     v
startAttempt == 0 ?
    |
    +-- yes --> consecutiveFailCount++
    |           count >= 3 ? --> clearSensorSection() --> return true
    |           return false
    |
    +-- no --> Bubble sort + Median for each register
               |
               v
          Key values == 0 ?
               |
               +-- yes --> Zero-retry loop (up to 5x)
               |            Full 3-sample read each time
               |
               +-- no --> return true (success)
```

---

## 3.6 Time Management

The system uses a three-tier time source strategy, implemented across `STATE_NTP` and `GsmHandler`.

### Tier 1: GSM RTC / NTP (Primary)

1. **GSM RTC**: `GsmHandler::getNetworkTime()` queries the modem's internal clock via AT command `+CCLK`. If the response contains a valid date/time (14+ characters), it is parsed into `timeStruct`.

2. **NTP via SIM800**: If the GSM RTC time is unavailable or invalid, `GsmHandler::syncNtp()` sends:
   - `+CNTP="pool.ntp.org",0` -- configure NTP server
   - `+CNTP` -- trigger sync
   - Waits for `+CNTP: 1` response (success)
   - Then re-reads the GSM RTC which now has NTP-synchronized time

### Tier 2: Last CSV Timestamp + Increment (Fallback)

When GSM is unavailable or time sync fails:

1. `readLastTimeFromBackup()` reads the most recent CSV file path from NVS key `lastDailyCsv`
2. Opens the file, skips the header, reads all data lines (feeding WDT every 16 lines)
3. Parses the last line's date (`DD/MM/YYYY`) and time (`HH:MM:SS`) fields
4. `incrementTime()` adds `TIME_INCREMENT_MINUTES` (10 min) to the parsed time
5. Handles day/month/year rollover correctly

### Tier 3: BLE NTP (Optional)

- The web portal provides an "NTP Sync Now" button (`hNtpSyncNow()`)
- If BLE is connected to a NUS device (Sniffer Portal), the device may provide timestamps in its JSON payload
- The `ts` field from BLE NUS data is used when saving BLE data to CSV

### Time Formatting

All time is stored in `timeStruct` with two formatted strings:
- `dateStr[12]`: `"DD/MM/YYYY"` (11 chars + null)
- `timeStr[9]`: `"HH:MM:SS"` (8 chars + null)

These are updated by `snprintf()` whenever the time struct is modified.

---

## 3.7 Watchdog Management

The ESP32 task watchdog is configured with a 45-second timeout (`WDT_TIMEOUT_SEC = 45`). The watchdog is initialized in `setup()` and fed throughout the firmware via `feedWDT()` (which calls `esp_task_wdt_reset()`).

### WDT Feed Points

| Location | Feed Trigger | Purpose |
|---|---|---|
| `sensor_v2.cpp` | Each Modbus read attempt | Prevent timeout during multi-sample reads |
| `sensor_v2.cpp` | Each zero-retry iteration | Long re-read loops |
| `main_1.cpp` STATE_WEATHER | During 15 s settle delay loop | Wait for sensor stabilization |
| `main_1.cpp` STATE_SOIL | During 15 s settle delay loop | Wait for sensor stabilization |
| `main_1.cpp` STATE_WIFI_AP | Every loop iteration (`delay(2)`) | Keep AP responsive |
| `main_1.cpp` BLE connect | Before and after connection attempts | BLE operations can block |
| `main_1.cpp` BLE GATT read | Each characteristic read | Prevent timeout during service discovery |
| `main_1.cpp` InfluxDB HTTP wait | During 10 s TCP response wait | GSM TCP is slow |
| `main_1.cpp` OTA download | Each 512-byte chunk | Download can take minutes |
| `main_1.cpp` OTA HTTP GET | During 15 s header wait | Waiting for server response |
| `main_1.cpp` readLastTimeFromBackup | Every 16 CSV lines | File I/O can be slow |
| `main_1.cpp` STATE_FINISH | Every 1 s in infinite loop | Prevent WDT reset if TPL5110 fails |
| `main_1.cpp` batteryRead() | Before and after 64-sample ADC read | ADC sampling takes ~128 ms |
| `sensor_v2.cpp` batteryRead() | During 64-sample ADC loop | Safety margin |

---

## 3.8 NVS Preferences Keys

All settings are stored in the `ws-cfg` NVS namespace. The `Preferences` library is used with both read-only (`true`) and read-write (`false`) modes.

### BLE Configuration

| Key | Type | Default | Purpose |
|---|---|---|---|
| `bleEnable` | `bool` | `false` | Enable BLE client (scan + connect) |
| `bleSavedMac` | `string` | `""` | Last connected BLE MAC address, used for auto-reconnect on boot |

### Data Source Selection

| Key | Type | Default | Purpose |
|---|---|---|---|
| `srcModbus` | `bool` | `true` | Enable Modbus RS-485 sensor reads |
| `srcBle` | `bool` | `false` | Enable BLE as data source |
| `fileInterval` | `uint` | `10` | Data logging interval in minutes |

### Storage

| Key | Type | Default | Purpose |
|---|---|---|---|
| `lastDailyCsv` | `string` | `"/DATA.csv"` | Path to the current daily CSV file |
| `memRollover` | `uint` | `80` | Storage usage percentage threshold for rollover |
| `memRolloverEn` | `bool` | `true` | Enable automatic storage rollover |

### WiFi AP

| Key | Type | Default | Purpose |
|---|---|---|---|
| `webpass` | `string` | `"admin"` | Web portal login password |
| `apTimeout` | `uint` | `5` | AP idle timeout in minutes |

### InfluxDB

| Key | Type | Default | Purpose |
|---|---|---|---|
| `influxEn` | `bool` | `false` | Enable InfluxDB upload |
| `influxHost` | `string` | `"119.59.103.220"` | InfluxDB server IP address |
| `influxPort` | `uint` | `8086` | InfluxDB server port |
| `influxToken` | `string` | (long auth token) | InfluxDB v2 API authentication token |
| `influxOrg` | `string` | `"Pamiang"` | InfluxDB organization |
| `influxBucket` | `string` | `"Srisaket_Station_I"` | InfluxDB bucket name |
| `influxLastSync` | `string` | `"--"` | Timestamp of last successful sync |

### MQTT

| Key | Type | Default | Purpose |
|---|---|---|---|
| `mqttEnable` | `bool` | `true` | Enable MQTT connectivity |
| `mqttHost` | `string` | `"119.59.103.220"` | MQTT broker address |
| `mqttPort` | `uint` | `1883` | MQTT broker port |
| `mqttUser` | `string` | `"kmutt"` | MQTT username |
| `mqttPass` | `string` | `"kmutt@kmutt"` | MQTT password |

### Email (SMTP)

| Key | Type | Default | Purpose |
|---|---|---|---|
| `emailEnable` | `bool` | `false` | Enable email alerts |
| `emailuser` | `string` | `""` | SMTP login (Gmail address) |
| `emailpass` | `string` | `""` | SMTP password (app password) |
| `emailsmtp` | `string` | `"smtp.gmail.com"` | SMTP server |
| `emailport` | `uint` | `587` | SMTP port |
| `emailto` | `string` | `""` | Alert recipient email |
| `emaillightto` | `string` | `""` | Light alert recipient email |
| `emailFreqH` | `uint` | `24` | Email frequency in hours |
| `emailalarm` | `bool` | `false` | Enable alarm-triggered emails |

### OTA (Over-The-Air Updates)

| Key | Type | Default | Purpose |
|---|---|---|---|
| `lastota` | `string` | `"Never"` | Timestamp of last successful OTA update |
| `otaserver` | `string` | `""` | OTA server URL (e.g. `http://192.168.1.100:8080`) |
| `otainterval` | `uint` | `24` | OTA check interval in hours |
| `otaboot` | `bool` | `false` | Check for OTA update on every boot |
| `otaproject` | `string` | `""` | OTA project name identifier |
| `otadevice` | `string` | `"All-in-One"` | OTA device name identifier |
| `otadlpass` | `string` | `""` | OTA download password |
| `otapass` | `string` | `"admin"` | ArduinoOTA update password |

### NTP

| Key | Type | Default | Purpose |
|---|---|---|---|
| `ntpEnable` | `bool` | `true` | Enable NTP time synchronization |

---

*End of Module 3. See Module 4 for communication protocols and Module 5 for deployment procedures.*
