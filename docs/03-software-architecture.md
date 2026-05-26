# Module 3 -- Software Architecture

**Firmware:** v2.3.5 | **Build:** 18-05-2026 | **Platform:** Seeed XIAO ESP32-C3
**Credits:** IDEA Laboratory @ KMUTT

---

## Table of Contents

1. [File / Module Map](#1-file--module-map)
2. [State Machine Detail](#2-state-machine-detail)
3. [Data Structures](#3-data-structures)
4. [Data Flow Diagram](#4-data-flow-diagram)
5. [Sensor Reading Pipeline](#5-sensor-reading-pipeline)
6. [Time Management](#6-time-management)
7. [Watchdog Management](#7-watchdog-management)
8. [NVS Preferences Keys](#8-nvs-preferences-keys)

---

## 1. File / Module Map

### `include/utilities.h` -- Central Configuration

Compile-time constants, pin assignments, default values, and timeouts. Every other module
includes this header. Key categories: version macros, serial baud rates, RS-485 and GSM
pinouts, Modbus register maps, battery divider resistors, TPL5110 done pin, NTP time
parameters, WiFi AP credentials, web portal auth defaults, OTA defaults, email defaults,
MQTT broker settings, memory limits, WDT timeout, and per-state timeouts.

### `src/main_1.cpp` -- Application Entry Point, State Machine, BLE, InfluxDB, OTA

| Responsibility | Key Functions / Objects |
|---|---|
| State machine (9 states) | `loop()`, `systemState` enum |
| BLE client (NimBLE) | `initBLE()`, `doBleScan()`, `connectAndReadBle()`, `refreshBleData()` |
| NUS (Nordic UART Service) | `nusNotifyCallback()`, `parseNusJson()`, `saveBleDataToCsv()` |
| InfluxDB v2 line-protocol POST | `sendToInfluxDB()` |
| Remote OTA over GSM HTTP | `checkRemoteOTA()`, `gsmHttpGet()`, `parseOtaUrl()` |
| Heartbeat (MQTT pong) | `publishHeartbeat()` |
| Compact JSON builder for MQTT | `buildCompactJSON()` |
| Time fallback logic | `readLastTimeFromBackup()`, `incrementTime()` |
| System status aggregation | `SystemStatus sysStatus` |
| Daily CSV file management | `updateDailyCsv()`, `checkFile()`, `getLastDataLine()` |
| Watchdog feeder | `feedWDT()` |

### `src/sensor_v2.cpp` / `include/sensor_v2.h` -- Modbus RS-485 Sensors

| Responsibility | Key Functions / Classes |
|---|---|
| Modbus RTU master communication | `RS485sensor::read()`, `RS485sensor::write()` |
| Median filtering | `dataProcess::getMedian()`, `dataProcess::getMedian32()` |
| Zero-retry and UART recovery | internal to `RS485sensor::read()` |
| Consecutive-failure zero-out | `clearSensorSection()`, `_consecutiveFailCount` |
| Battery ADC reading | `batteryRead()` |
| Sensor / time data types | `SensorData`, `timeStruct`, `DataRecord` |

### `src/GsmHandler.cpp` / `include/GsmHandler.h` -- GSM / GPRS / MQTT / NTP / Email

| Responsibility | Key Functions / Classes |
|---|---|
| SIM800 modem init | `GsmHandler::init()` |
| GPRS data connection | `GsmHandler::connectNetwork()` |
| Network time (RTC + NTP) | `GsmHandler::getNetworkTime()`, `syncNtp()` |
| MQTT publish | `GsmHandler::mqttConnect()`, `mqttPublish()`, `mqttDisconnect()` |
| Email via SIM800 SMTP AT | `GsmHandler::sendEmail()` |
| Modem control | `GsmHandler::powerOff()`, `restart()` |
| Dynamic MQTT reconfiguration | `GsmHandler::setMqttConfig()` |

### `src/Memory.cpp` / `include/Memory.h` -- LittleFS CSV Storage

| Responsibility | Key Functions / Classes |
|---|---|
| CSV row append | `Memory::append()`, `Memory::saveData()` |
| File creation with header | `Memory::write()` |
| CSV line count | `Memory::countDataLines()` |
| Read records back to structs | `Memory::readDataRecords()` |
| Remove delivered rows (swap) | `Memory::removeFirstDataLines()` |
| Storage monitoring | `Memory::getAvailableSpace()`, `isFileTooLarge()`, `isUsageOverThreshold()` |
| Clear all data rows | `Memory::clearDataRows()` |

### `src/WifiApServer.cpp` / `include/WifiApServer.h` -- WiFi AP Web Portal

| Responsibility | Key Functions / Classes |
|---|---|
| WiFi AP start/stop | `WifiApServer::begin()`, `stop()` |
| HTTP server (30+ routes) | `hLive`, `hFiles`, `hSettings`, `hAbout`, etc. |
| Session-based auth | `checkAuth()`, `startSession()`, `clearSession()` |
| BLE scan/connect/disconnect UI | `hBleScan()`, `hBleConnect()`, `hBleDisconnect()`, `hApiBle()` |
| Live data JSON API | `hApiLive()`, `hApiBle()` |
| File download/delete | `hDownload()`, `hDelete()`, `hDeleteAll()` |
| All settings save handlers | `hSetInflux`, `hSetMqtt`, `hSetOta`, `hSetEmail`, etc. |
| Event logging | `appendEvent()`, `hLog()`, `hLogDownload()` |
| OTA trigger from web UI | `hOtaUpdate()` |
| Test endpoints (InfluxDB, MQTT, Email, OTA) | `hTestInflux()`, `hTestMqtt()`, `hTestEmail()`, `hTestOta()` |
| BLE NTP sync from sniffer | `hNtpSyncNow()` |

---

## 2. State Machine Detail

The firmware runs a single-threaded loop with nine states. The TPL5110 nano-power timer
controls board power; the MCU wakes, runs through the state machine once, and pulses the
DONE pin to cut power in `STATE_FINISH`.

```
                         ┌──────────────┐
                         │ STATE_WIFI_AP │ (min 1 min, max 5 min)
                         └──────┬───────┘
                                │
                         ┌──────▼───────┐
                         │ STATE_GSM_INIT│ (120 s timeout)
                         └──────┬───────┘
                                │
                         ┌──────▼───────┐
                         │   STATE_NTP   │ (30 s timeout)
                         └──────┬───────┘
                                │
                    ┌───────────▼───────────┐
                    │    STATE_WEATHER       │ (15 s settle + 30 s timeout)
                    └───────────┬───────────┘
                                │
                      ┌─────────▼─────────┐
                      │    STATE_SOIL      │ (15 s settle + 30 s timeout)
                      └─────────┬─────────┘
                                │
                       ┌────────▼────────┐
                       │   STATE_SAVE     │ (10 s timeout)
                       └────────┬────────┘
                                │
                    ┌───────────▼───────────┐
                    │  STATE_RECONNECT       │ (120 s timeout, currently passes through)
                    └───────────┬───────────┘
                                │
                    ┌───────────▼───────────┐
                    │  STATE_PUBLISH         │ (60 s timeout, currently passes through)
                    └───────────┬───────────┘
                                │
                       ┌────────▼────────┐
                       │  STATE_FINISH    │ (no timeout -- pulses TPL5110 DONE)
                       └─────────────────┘
```

### State Reference Table

| State | Purpose | Timeout | Entry Condition | Exit Condition | On Timeout / Failure |
|---|---|---|---|---|---|
| `STATE_WIFI_AP` | WiFi AP + BLE portal; user configuration, live data view, BLE connect | 300 s max (5 min) | First state after boot | (1) BLE data received after 1 min, (2) AP idle timeout after 1 min, (3) OTA requested (immediate exit, before 1 min) | Falls through to `STATE_GSM_INIT` |
| `STATE_GSM_INIT` | Initialize SIM800, register network, open GPRS data session | 120 000 ms | After `STATE_WIFI_AP` exits | Modem init + GPRS connect succeed | Sets `gsmAvailable = false`, advances to `STATE_NTP` regardless |
| `STATE_NTP` | Obtain wall-clock time via GSM RTC or NTP fallback | 30 000 ms | After `STATE_GSM_INIT` | `getNetworkTime()` succeeds or all fallbacks exhausted | Uses CSV-timestamp + 10 min increment fallback |
| `STATE_WEATHER` | Read weather station over Modbus (slave 0x01, 16 registers) | 30 000 ms (incl. 15 s settle) | After `STATE_NTP` | `RS485sensor::read(WEATHER, ...)` returns true | `clearWeatherData()` zeroes all weather fields, advances |
| `STATE_SOIL` | Read soil sensor over Modbus (slave 0x03, 7 registers) | 30 000 ms (incl. 15 s settle) | After `STATE_WEATHER` | `RS485sensor::read(SOIL, ...)` returns true | Advances to `STATE_SAVE` with whatever data was collected |
| `STATE_SAVE` | Write sensor data to daily CSV; send to InfluxDB if GSM available | 10 000 ms | After `STATE_SOIL` | CSV append + InfluxDB POST complete | Falls through to `STATE_RECONNECT` |
| `STATE_RECONNECT` | Reserved for GSM reconnection and pending MQTT publish | 120 000 ms | After `STATE_SAVE` | Currently passes through immediately | Falls through to `STATE_FINISH` |
| `STATE_PUBLISH` | Reserved for batch MQTT publish of stored records | 60 000 ms | After `STATE_RECONNECT` | Currently passes through immediately | Falls through to `STATE_FINISH` |
| `STATE_FINISH` | Signal TPL5110 to cut power; infinite loop if power remains | None | After `STATE_PUBLISH` | TPL5110 cuts power (DONE pulse) | Hangs in `while(1)` feeding WDT |

### STATE_WIFI_AP Exit Logic Detail

The WiFi AP state has a nuanced exit strategy:

```
                      Boot
                        │
                ┌───────▼───────┐
                │  AP started   │
                │  BLE scan     │
                │  BLE connect  │
                └───────┬───────┘
                        │
              elapsed < 60 s ?
              ┌─── YES ────┐
              │            │
        OTA requested?   Wait (delay 2 ms)
        ┌─ YES ─┐       loop continues
        │       │
  Close AP     Continue
  -> GSM_INIT  waiting
              │
              │  elapsed >= 60 s
              ▼
        BLE data received?
        ┌─ YES ──────── NO ─────┐
        │                        │
  Close AP               AP idle timeout?
  -> GSM_INIT            ┌─ YES ──┐
                         │         │
                    Close AP   Keep waiting
                    -> GSM_INIT  (loop)
```

---

## 3. Data Structures

### 3.1 SensorData (33 bytes, packed)

Defined in `include/sensor_v2.h`.

```
Offset  Field               Type        Size  Unit / Scaling
------  -----               ----        ----  --------------
 0      soil_humi           uint16_t     2    0.1% (raw / 10.0)
 2      soil_temp           int16_t      2    0.1 C (raw / 10.0)
 4      soil_ec             uint16_t     2    uS/cm (raw value)
 6      soil_ph             uint8_t      1    0.1 (raw / 10.0)
 7      (pad)                            1    padding for alignment
 8      soil_N              uint16_t     2    mg/kg (raw value)
10      soil_P              uint16_t     2    mg/kg (raw value)
12      soil_K              uint16_t     2    mg/kg (raw value)
------ Soil subtotal: 13 bytes ------------------------------
14      windSpeed           uint16_t     2    0.1 m/s (raw / 10.0)
16      windDir_Deg         uint16_t     2    degrees (raw value)
18      air_humidity        uint16_t     2    0.1% (raw / 10.0)
20      air_temperature     int16_t      2    0.1 C (raw / 10.0)
22      CO2                 uint16_t     2    ppm (raw value)
24      pressure            uint16_t     2    0.1 kPa (raw / 10.0)
26      illuminance         uint32_t     4    lux (raw value)
30      rainfall            uint16_t     2    0.1 mm (raw / 10.0)
32      solar               uint16_t     2    W/m2 (raw value)
------ Weather subtotal: 20 bytes ---------------------------
      TOTAL: 33 bytes (packed)
```

Note: Due to `__attribute__((packed))`, the `uint8_t soil_ph` at offset 6 is followed by
one implicit padding byte before the `uint16_t soil_N` at offset 8, bringing the actual
struct size to 33 bytes.

### 3.2 timeStruct (37 bytes)

Defined in `include/sensor_v2.h`.

```
Field       Type        Size  Description
-----       ----        ----  -----------
date        uint8_t      1    Day of month (1-31)
month       uint8_t      1    Month (1-12)
year        uint16_t     2    Full year (e.g. 2026)
hour        uint8_t      1    Hours (0-23)
minute      uint8_t      1    Minutes (0-59)
second      uint8_t      1    Seconds (0-59)
dateStr     char[12]    12    "DD/MM/YYYY" (null-terminated)
timeStr     char[9]      9    "HH:MM:SS" (null-terminated)
```

### 3.3 DataRecord (packet for CSV round-trip, packed)

Defined in `include/sensor_v2.h`.

```
Field       Type        Size  Description
-----       ----        ----  -----------
date        uint8_t      1    Day
month       uint8_t      1    Month
year        uint8_t      1    Year (last 2 digits when read from CSV)
hour        uint8_t      1    Hour
minute      uint8_t      1    Minute
data        SensorData  33    Full sensor reading
valid       uint8_t      1    1 = valid record, 0 = skip
```

Typedef aliases: `DataRecord` = `Packet`; `soilData` = `weatherData` = `SensorData`.

### 3.4 SystemStatus (shared state pointer struct)

Defined in `include/WifiApServer.h`. Aggregates pointers to all runtime state so the web
portal can read live data without global coupling.

```
Field               Type                    Purpose
-----               ----                    -------
sensor              SensorData*             Pointer to RS485sensor.currentSensor
time                timeStruct*             Pointer to currentTime
battMv              uint16_t*               Pointer to batteryVoltage (mV)
gsmAvail            bool*                   Pointer to gsmAvailable flag
gsmRssi             int*                    Pointer to gsmRssi (dBm)
bleActive           bool*                   Pointer to BLE client active flag
memory              Memory*                 Pointer to internalMemory
gsm                 GsmHandler*             Pointer to gsmHandler
lastOtaStr          char[24]                Last OTA timestamp string
bleDevices          BLEFoundDevice*         BLE scan results array
bleDeviceCount      uint8_t*                Number of discovered BLE devices
bleMac              char[18]                This device's BLE MAC address
bleConnDev          BLEConnectedDevice*     Currently connected BLE device data
bleTargetMac        char*                   MAC address targeted for connection
bleConnPending      bool*                   Connection request pending flag
bleSavedMac         char*                   NVS-persisted MAC for auto-reconnect
bleScanRequest      bool*                   Trigger immediate BLE scan
bleIsNus            bool*                   True if connected device is NUS type
bleNusLastMs        unsigned long*          millis() of last NUS data update
bleNusRefreshReq    bool*                   Trigger immediate NUS data request
otaCheckNow         bool*                   Trigger OTA check on next GSM cycle
```

### 3.5 BLEFoundDevice

Defined in `include/WifiApServer.h`. Maximum 10 devices (`BLE_MAX_DEVICES`).

```
Field       Type        Size  Description
-----       ----        ----  -----------
name        char[32]    32    Device name (or "(unknown)")
mac         char[18]    18    MAC address string "XX:XX:XX:XX:XX:XX"
rssi        int8_t       1    Signal strength (dBm)
```

### 3.6 BLECharData

Defined in `include/WifiApServer.h`. Maximum 16 per device (`BLE_CHAR_MAX`).

```
Field       Type        Size  Description
-----       ----        ----  -----------
uuid        char[37]    37    Characteristic UUID string (or field label for NUS)
value       char[64]    64    Hex string of raw bytes (GATT) or raw value string (NUS)
ascii       char[32]    32    Printable ASCII representation
```

### 3.7 BLEConnectedDevice

Defined in `include/WifiApServer.h`.

```
Field       Type            Size  Description
-----       ----            ----  -----------
mac         char[18]        18    Connected device MAC
name        char[32]        32    Connected device name
connected   bool             1    Connection state
charCount   uint8_t          1    Number of discovered characteristics
chars       BLECharData[16] varies Up to 16 characteristic data entries
```

---

## 4. Data Flow Diagram

### 4.1 Main Sensor Data Flow (RS-485)

```
 Modbus         RS485sensor       dataProcess         Memory             GsmHandler
 Sensors        .read()           .getMedian()        .saveData()        / HTTPClient
 ─────────    ───────────────    ────────────────    ────────────────    ────────────────

 [Weather  ──► 3x read attempts ──► Median of 3 ──────► CSV append ──────► InfluxDB v2
  Station     (slave 0x01,       per field          LittleFS daily     line-protocol
  16 regs]    16 regs)                              file DD-MM-YYYY    POST over GPRS
                                                     .csv]             TCP
 [Soil     ──► 3x read attempts ──► Median of 3 ──┘
  Sensor      (slave 0x03,
  7 regs]     7 regs)

                      │                                 │                  │
                      │                                 │                  ▼
                      │                                 │            MQTT publish
                      │                                 │            (heartbeat
                      │                                 │             + data)
                      │                                 │
                      ▼                                 ▼
               RS485sensor.                      LittleFS flash
               currentSensor                     (persistent CSV
               (SensorData)                       buffer)
```

### 4.2 BLE NUS Sniffer Data Flow

```
 BLE Sniffer       NimBLE            nusNotify       parseNusJson     Memory         InfluxDB
 Portal            Client            Callback()      ()               .append()      (append to
 ──────────    ───────────────    ─────────────    ─────────────    ─────────────    same POST)
                                                                               ────────────────

 [Nordic   ──► NUS TX char  ──► Accumulate   ──► Extract JSON ──► BLE-DD-MM-YYYY ──► sniffer_watchdog
  UART       notifications     into char[]       fields via         .csv              measurement
  Service]                     buffer; find       jsonField()      (separate file
                               last {...})                          from main
                                                                    sensor CSV)
```

### 4.3 CSV Format

**Main sensor CSV** (`/DD-MM-YYYY.csv`):

```
Date,Time,Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,WindSpeed,WindDirection,
Air_Humidity,Air_Temperature,CO2,Pressure,Illuminance,Rainfall,Solar
DD/MM/YYYY,HH:MM:SS,<soil_humi/10>,<soil_temp/10>,<soil_ec>,<soil_ph/10>,
<soil_N>,<soil_P>,<soil_K>,<windSpeed/10>,<windDir_Deg>,<air_humidity/10>,
<air_temp/10>,<CO2>,<pressure/10>,<illuminance>,<rainfall/10>,<solar>
```

**BLE sniffer CSV** (`/BLE-DD-MM-YYYY.csv`):

```
Date,Time,Temperature(C),Humidity(%),TMP117(C),DeltaT(C),Rainfall,LeafWetness,PAR,SoilMoisture
```

### 4.4 Data Scaling Reference

All scaled fields store raw integer values and are divided at output time:

| Field | Raw Type | Scaling | Display Unit |
|---|---|---|---|
| `soil_humi` | `uint16_t` | / 10.0 | % |
| `soil_temp` | `int16_t` | / 10.0 | C |
| `soil_ec` | `uint16_t` | raw | uS/cm |
| `soil_ph` | `uint8_t` | / 10.0 | pH |
| `soil_N` | `uint16_t` | raw | mg/kg |
| `soil_P` | `uint16_t` | raw | mg/kg |
| `soil_K` | `uint16_t` | raw | mg/kg |
| `windSpeed` | `uint16_t` | / 10.0 | m/s |
| `windDir_Deg` | `uint16_t` | raw | degrees |
| `air_humidity` | `uint16_t` | / 10.0 | % |
| `air_temperature` | `int16_t` | / 10.0 | C |
| `CO2` | `uint16_t` | raw | ppm |
| `pressure` | `uint16_t` | / 10.0 | kPa |
| `illuminance` | `uint32_t` | raw | lux |
| `rainfall` | `uint16_t` | / 10.0 | mm |
| `solar` | `uint16_t` | raw | W/m2 |

---

## 5. Sensor Reading Pipeline

The sensor read pipeline implements a 5-stage noise rejection strategy in
`RS485sensor::read()`.

### Stage Overview

```
 Stage 1: Median of 3         Stage 2: Retry 5x           Stage 3: Zero-retry
 ──────────────────           ──────────────              ─────────────────
 3 successful Modbus          If a Modbus read fails,     After median calculation,
 reads per field;             retry up to 5 times         if critical fields are
 median selected to           (maxRetry = 5).             zero, re-read the sensor
 reject outliers.             Total attempts = 15 max.    up to 5 more times.

 Stage 4: 0xE0 UART          Stage 5: Consecutive
          Recovery                   Failure Zero-out
 ──────────────────           ─────────────────────────
 If Modbus returns            If 3 consecutive read
 error code 0xE0              sessions produce zero
 (InvalidSlaveID),            successful reads, the
 perform full UART            sensor section (SOIL or
 recovery: end(),             WEATHER) is zeroed out
 delay, begin() again.        to indicate persistent
                              failure.
```

### Stage 1: Median of 3 Reads

The function collects up to `readAttempt` (3) successful Modbus reads into
`rawBuffer[3][16]`. Each register position is independently median-filtered via
`dataProcess::getMedian()` (or `getMedian32()` for the 32-bit illuminance field).

```
  rawBuffer[0][0..15]  ──┐
  rawBuffer[1][0..15]  ──┼──► getMedian(buf, 3) ──► SensorData.field
  rawBuffer[2][0..15]  ──┘
```

### Stage 2: Retry 5x

If a single Modbus `readHoldingRegisters()` call fails (returns non-zero error code),
`retry_FLAGS` increments. Up to `maxRetry` (5) failures are tolerated before the read
loop exits. The WDT is fed on each attempt.

```
  while (startAttempt < 3 && retry_FLAGS < 5):
    if Modbus OK  -> store in rawBuffer[startAttempt], startAttempt++
    if Modbus ERR -> retry_FLAGS++
                     if error == 0xE0 -> full UART recovery
                     else             -> delay 1000 ms
```

### Stage 3: Zero-Retry

After median calculation, if critical fields remain zero, the function re-enters a
secondary read loop (up to `maxRetry` = 5 additional attempts):

- **Soil sensor:** retries if `soil_humi == 0 || soil_temp == 0 || soil_ec == 0`
- **Weather sensor:** retries if `air_humidity == 0 || air_temperature == 0 || CO2 == 0`

Each retry iteration performs 3 more reads with 5 retries, producing a new median
calculation. The WDT is fed throughout.

### Stage 4: 0xE0 UART Recovery

When the Modbus library returns error code `0xE0` (InvalidSlaveID), this indicates a
UART protocol desync. Recovery procedure:

1. `HardwareSerial::end()` -- shut down UART
2. `delay(100)`
3. `HardwareSerial::begin(9600, SERIAL_8N1, RX, TX)` -- reinitialize
4. `delay(500)`
5. Flush any stale bytes from RX buffer
6. `delay(200)`
7. Re-initialize ModbusMaster pre/post transmission callbacks

The `_uartReady` flag controls whether recovery or a simpler flush+settle is performed
on subsequent calls.

### Stage 5: Consecutive Failure Zero-Out

If `startAttempt == 0` (no successful reads at all), `_consecutiveFailCount` increments.
When it reaches `_maxConsecutiveFail` (3), the corresponding sensor section in
`currentSensor` is zeroed via `clearSensorSection()`, and the counter resets. This
prevents stale data from persisting across multiple wake cycles.

```
  _consecutiveFailCount:  0 ──► 1 ──► 2 ──► 3 ──► zero section, reset to 0
                                       (normal      (persistent failure:
                                        success      zero out data)
                                        resets to 0)
```

---

## 6. Time Management

### 6.1 Time Source Priority

```
  Priority   Source             Condition
  ────────   ──────             ─────────
  1 (best)   GSM RTC            gsmAvailable == true AND
             (getGSMDateTime)   getGSMDateTime() returns valid date/time

  2          NTP over GSM       GSM RTC invalid AND
             (pool.ntp.org)     syncNtp() succeeds, then re-read GSM RTC

  3          CSV timestamp      gsmAvailable == false AND
             + 10 min           lastDailyCsv has at least one data row
             (fallback)         readLastTimeFromBackup() parses last row

  4 (worst)   Reset to          All above fail: currentTime zeroed,
             epoch 01/01/2024   then incrementTime() corrects to
                                01/01/2024 00:00 + 10 min
```

### 6.2 GSM RTC Parsing

`parseGsmDateTime()` in `GsmHandler.cpp` parses the SIM800 date-time string format:
`"YY/MM/DD,HH:MM:SS"` (with optional seconds). Year is adjusted by +2000. Validation
rejects years before 2024, months outside 1-12, dates outside 1-31, etc.

### 6.3 NTP Sync

`syncNtp()` in `GsmHandler.cpp` sends SIM800-specific AT commands:

1. `AT+CNTP="pool.ntp.org",0` -- configure NTP server (UTC+0)
2. `AT+CNTP` -- trigger sync
3. Poll `+CNTP: 1` response for up to 60 s (`NTP_TIMEOUT_MS`), feeding WDT every 2 s

### 6.4 CSV Timestamp Fallback

`readLastTimeFromBackup()` in `main_1.cpp`:

1. Opens the last daily CSV file (path stored in NVS key `lastDailyCsv`)
2. Scans all data rows, keeping the last non-empty line
3. Parses date (`DD/MM/YYYY`) and time (`HH:MM:SS`) from the first two CSV fields
4. Returns the parsed `timeStruct` for `incrementTime()` to advance

### 6.5 Time Increment

`incrementTime()` advances a `timeStruct` by `TIME_INCREMENT_MINUTES` (10 minutes):

1. Adds minutes to `hour * 60 + minute`
2. Handles day rollover if total >= 1440
3. Handles month rollover using a days-in-month lookup table
4. Handles year rollover if month exceeds 12
5. Updates `dateStr` and `timeStr` formatted strings
6. Resets seconds to 0

### 6.6 BLE NTP Sync (Optional)

The web portal provides an "NTP Sync Now" button (`/ntpsync`) when a BLE NUS sniffer is
connected. It reads the timestamp from the first NUS characteristic (`ts` field format:
`YYYY-MM-DD HH:MM:SS`) and writes it directly into `currentTime`.

---

## 7. Watchdog Management

### 7.1 Configuration

- **Timeout:** 45 seconds (`WDT_TIMEOUT_SEC` in `utilities.h`)
- **Mode:** Trigger system reset on timeout (`esp_task_wdt_init(WDT_TIMEOUT_SEC, true)`)
- **Subscribed task:** Main loop task (`esp_task_wdt_add(NULL)` in `setup()`)

### 7.2 WDT Feed Points

The watchdog is fed at the following locations throughout the firmware:

| Location | File | Context |
|---|---|---|
| `feedWDT()` (wrapper) | `main_1.cpp` | Called at top of every `loop()` iteration |
| `feedWDT()` | `main_1.cpp` | In `setup()` after LittleFS init, after BLE init |
| `esp_task_wdt_reset()` | `main_1.cpp` | Inside `STATE_WIFI_AP` -- implicit via `wifiApServer.handleClient()` |
| `esp_task_wdt_reset()` | `sensor_v2.cpp` | Inside `RS485sensor::read()` -- every Modbus read attempt |
| `esp_task_wdt_reset()` | `sensor_v2.cpp` | Inside zero-retry loops for both soil and weather |
| `esp_task_wdt_reset()` | `sensor_v2.cpp` | In `batteryRead()` -- before and after 1 s delay |
| `esp_task_wdt_reset()` | `GsmHandler.cpp` | In `connectNetwork()` -- every 2 s during network wait |
| `esp_task_wdt_reset()` | `GsmHandler.cpp` | In `mqttConnect()` -- during MQTT connection loop |
| `esp_task_wdt_reset()` | `GsmHandler.cpp` | In `mqttPublish()` -- post-publish drain loop (10 x 200 ms) |
| `esp_task_wdt_reset()` | `GsmHandler.cpp` | In `syncNtp()` -- polling loop |
| `feedFileWDT()` | `Memory.cpp` | Every 16 lines during `countDataLines()`, `readDataRecords()`, `removeFirstDataLines()` |
| `esp_task_wdt_reset()` | `main_1.cpp` | In `checkRemoteOTA()` -- during HTTP GET and firmware download |
| `esp_task_wdt_reset()` | `main_1.cpp` | In `sendToInfluxDB()` -- during TCP connect and response wait |
| `esp_task_wdt_reset()` | `main_1.cpp` | In `connectAndReadBle()` -- during BLE connect retries and GATT discovery |
| `esp_task_wdt_reset()` | `main_1.cpp` | In `refreshBleData()` -- per characteristic read |
| `esp_task_wdt_reset()` | `WifiApServer.cpp` | In `handleClient()` -- every HTTP request cycle |
| `esp_task_wdt_reset()` | `WifiApServer.cpp` | In `appendEvent()` -- every event log write |
| `esp_task_wdt_reset()` | `WifiApServer.cpp` | In file download handlers -- before streaming |
| `esp_task_wdt_reset()` | `WifiApServer.cpp` | In test handlers (OTA, etc.) -- during GSM operations |
| `feedWDT()` | `main_1.cpp` | In `readLastTimeFromBackup()` -- every 16 CSV lines |
| `feedWDT()` | `main_1.cpp` | In `STATE_FINISH` infinite loop -- `while(1)` with 1 s delay |

### 7.3 Long-Operation Coverage

The following operations exceed the 45 s WDT threshold if not explicitly fed:

| Operation | Max Duration | Feed Strategy |
|---|---|---|
| GSM network registration | 120 s | Fed every 2 s in `connectNetwork()` |
| NTP sync wait | 60 s | Fed every 2 s in `syncNtp()` |
| MQTT connect | 60 s | Fed in `mqttConnect()` retry loop |
| OTA firmware download | 300 s | Fed in `checkRemoteOTA()` chunk loop |
| Large CSV file scan | Variable | Fed every 16 lines via `feedFileWDT()` |
| InfluxDB TCP wait | 10 s | Fed in `sendToInfluxDB()` poll loop |
| BLE GATT service discovery | Variable | Fed per characteristic read |
| `STATE_FINISH` hang | Infinite | Fed every 1 s in `while(1)` loop |

---

## 8. NVS Preferences Keys

All keys are stored in the NVS namespace `"ws-cfg"` using the Arduino `Preferences`
library. The namespace is opened as read-only (`true`) for reads and read-write
(`false`) for writes.

### 8.1 Complete Key Reference

| Key | NVS Type | Default | Purpose |
|---|---|---|---|
| `bleEnable` | `bool` | `false` | Enable BLE client (scan + connect) |
| `bleSavedMac` | `String` | `""` | MAC address of last connected BLE device (auto-reconnect) |
| `lastDailyCsv` | `String` | `"/DATA.csv"` | Path to the most recent daily CSV file |
| `lastota` | `String` | `"Never"` | Timestamp of last successful OTA update |
| `otaserver` | `String` | `""` | OTA server base URL (e.g. `http://host:8889`) |
| `otainterval` | `UInt` | `24` | Auto-check interval in hours (0 = off) |
| `otaboot` | `bool` | `false` | Check for OTA on every boot |
| `otaproject` | `String` | `""` | OTA project identifier (sent as `x-ESP32-project` header) |
| `otadevice` | `String` | `"All-in-One"` | OTA device type (sent as `x-ESP32-device` header) |
| `otadlpass` | `String` | `""` | OTA download password (sent as `x-ESP32-password` header) |
| `otapass` | `String` | `"admin"` | ArduinoOTA local WiFi password (used in AP mode) |
| `influxEn` | `bool` | `false` | Enable InfluxDB v2 data upload |
| `influxHost` | `String` | `"119.59.103.220"` | InfluxDB server host / IP |
| `influxPort` | `UInt` | `8086` | InfluxDB server port |
| `influxToken` | `String` | (long token) | InfluxDB v2 API authorization token |
| `influxOrg` | `String` | `"Pamiang"` | InfluxDB organization |
| `influxBucket` | `String` | `"Srisaket_Station_I"` | InfluxDB bucket name |
| `influxLastSync` | `String` | `"--"` | Timestamp of last successful InfluxDB sync (set by web UI test) |
| `memRollover` | `UInt` | `80` | LittleFS usage percentage threshold for CSV rollover (10-90) |
| `memRolloverEn` | `bool` | `true` | Enable automatic CSV rollover when storage threshold reached |
| `apTimeout` | `UInt` | `5` | WiFi AP idle timeout in minutes (0 = never close) |
| `fileInterval` | `UInt` | `10` | New data file rotation interval in minutes |
| `webpass` | `String` | `"admin"` | Web portal login password |
| `srcModbus` | `bool` | `true` | Enable Modbus RS-485 sensor data source |
| `srcBle` | `bool` | `false` | Enable BLE sensor data source |
| `ntpEnable` | `bool` | `true` | Enable NTP time sync when GSM connects |
| `mqttEnable` | `bool` | `true` | Enable MQTT heartbeat publishing |
| `mqttHost` | `String` | `"119.59.103.220"` | MQTT broker host / IP |
| `mqttPort` | `UInt` | `1883` | MQTT broker port |
| `mqttUser` | `String` | `"kmutt"` | MQTT broker username |
| `mqttPass` | `String` | `"kmutt@kmutt"` | MQTT broker password |
| `emailEnable` | `bool` | `false` | Enable email notifications |
| `emailsmtp` | `String` | `"smtp.gmail.com"` | SMTP server (auto-set when Gmail password entered) |
| `emailport` | `UInt` | `587` | SMTP port (auto-set when Gmail password entered) |
| `emailuser` | `String` | `""` | Email sender address (Gmail) |
| `emailpass` | `String` | `""` | Email app password (Gmail) |
| `emailto` | `String` | `""` | Primary email recipient |
| `emaillightto` | `String` | `""` | Lightning report email recipient |
| `emailFreqH` | `UInt` | `24` | Email notification frequency in hours |
| `emailalarm` | `bool` | `false` | Send email alert on failed web login attempt |

### 8.2 Key Access Patterns

**Read-only access (no write):**

```
nvs.begin("ws-cfg", true);        // read-only
String val = nvs.getString("key", defaultVal);
nvs.end();
```

**Read-write access:**

```
nvs.begin("ws-cfg", false);       // read-write
nvs.putBool("key", value);
nvs.putString("key", value);
nvs.putUInt("key", value);
nvs.end();
```

### 8.3 Key Grouping by Module

```
ws-cfg
 ├── BLE Client
 │    ├── bleEnable        (bool)
 │    └── bleSavedMac      (String)
 │
 ├── OTA
 │    ├── otaserver        (String)
 │    ├── otainterval      (UInt)
 │    ├── otaboot          (bool)
 │    ├── otaproject       (String)
 │    ├── otadevice        (String)
 │    ├── otadlpass        (String)
 │    ├── otapass          (String)
 │    └── lastota          (String)
 │
 ├── InfluxDB
 │    ├── influxEn         (bool)
 │    ├── influxHost       (String)
 │    ├── influxPort       (UInt)
 │    ├── influxToken      (String)
 │    ├── influxOrg        (String)
 │    ├── influxBucket     (String)
 │    └── influxLastSync   (String)
 │
 ├── Memory
 │    ├── memRollover      (UInt)
 │    ├── memRolloverEn    (bool)
 │    ├── lastDailyCsv     (String)
 │    └── fileInterval     (UInt)
 │
 ├── WiFi AP
 │    ├── apTimeout        (UInt)
 │    └── webpass          (String)
 │
 ├── Data Sources
 │    ├── srcModbus         (bool)
 │    └── srcBle            (bool)
 │
 ├── NTP
 │    └── ntpEnable        (bool)
 │
 ├── MQTT
 │    ├── mqttEnable       (bool)
 │    ├── mqttHost         (String)
 │    ├── mqttPort         (UInt)
 │    ├── mqttUser         (String)
 │    └── mqttPass         (String)
 │
 └── Email
      ├── emailEnable      (bool)
      ├── emailsmtp        (String)
      ├── emailport        (UInt)
      ├── emailuser        (String)
      ├── emailpass        (String)
      ├── emailto          (String)
      ├── emaillightto     (String)
      ├── emailFreqH       (UInt)
      └── emailalarm       (bool)
```

---

*End of Module 3 -- Software Architecture*
