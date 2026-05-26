# Module 4 -- Communication Protocols

**Firmware:** v2.3.5 | **Platform:** Seeed XIAO ESP32-C3 | **Credits:** IDEA Laboratory @ KMUTT

This module documents every wired and wireless protocol used by the Srisaket Weather Station. All register offsets, AT command sequences, JSON field names, and HTTP headers are drawn directly from the source code.

---

## Table of Contents

1. [Modbus RTU (RS-485)](#1-modbus-rtu-rs-485)
2. [MQTT](#2-mqtt)
3. [InfluxDB v2 HTTP](#3-influxdb-v2-http)
4. [BLE Nordic UART Service (NUS)](#4-ble-nordic-uart-service-nus)
5. [BLE GATT Generic](#5-ble-gatt-generic)
6. [SMTP Email](#6-smtp-email)
7. [NTP Time Synchronization](#7-ntp-time-synchronization)
8. [OTA Firmware Update (HTTP)](#8-ota-firmware-update-http)
9. [WiFi AP HTTP API](#9-wifi-ap-http-api)

---

## 1. Modbus RTU (RS-485)

### 1.1 Physical Layer

| Parameter       | Value                             |
|-----------------|-----------------------------------|
| Baud rate       | 9600                              |
| Data bits       | 8                                 |
| Parity          | None                              |
| Stop bits       | 1                                 |
| RX pin          | D4 (`RS_485_RX_PIN`)             |
| TX pin          | D10 (`RS_485_TX_PIN`)            |
| UART instance   | `HardwareSerial(1)`               |
| Library         | `ModbusMaster` (Doc Walker)       |

### 1.2 Function Code

The firmware uses **Function Code 0x03** (Read Holding Registers) exclusively for sensor reads, and **Function Code 0x06** (Write Single Register) for configuration writes.

```
Request frame:
  [Slave Address] [Function 0x03] [Reg Addr Hi] [Reg Addr Lo] [Count Hi] [Count Lo] [CRC Lo] [CRC Hi]

Response frame:
  [Slave Address] [Function 0x03] [Byte Count] [Data...] [CRC Lo] [CRC Hi]
```

### 1.3 Register Maps

#### Soil Sensor -- Slave 0x03

| Parameter        | Register | rawBuffer Index | Data Type  | Scaling          | Unit   |
|------------------|----------|-----------------|------------|------------------|--------|
| Soil Moisture    | 0x0000   | [0]             | uint16     | value / 10.0     | %      |
| Soil Temperature | 0x0001   | [1]             | int16      | value / 10.0     | C      |
| Soil EC          | 0x0002   | [2]             | uint16     | raw              | uS/cm  |
| Soil pH          | 0x0003   | [3]             | uint16     | clamped to 255, / 10.0 | -- |
| Soil Nitrogen    | 0x0004   | [4]             | uint16     | raw              | mg/kg  |
| Soil Phosphorus  | 0x0005   | [5]             | uint16     | raw              | mg/kg  |
| Soil Potassium   | 0x0006   | [6]             | uint16     | raw              | mg/kg  |

- Starting address: `0x0000`
- Register count: 7

#### Weather Sensor -- Slave 0x01

| Parameter       | Register | rawBuffer Index | Data Type       | Scaling          | Unit   |
|-----------------|----------|-----------------|-----------------|------------------|--------|
| Wind Speed      | 0x01F4   | [0]             | uint16          | value / 10.0     | m/s    |
| (reserved)      | 0x01F5   | [1]             | --              | --               | --     |
| (reserved)      | 0x01F6   | [2]             | --              | --               | --     |
| Wind Direction  | 0x01F7   | [3]             | uint16          | raw              | deg    |
| Air Humidity    | 0x01F8   | [4]             | uint16          | value / 10.0     | %      |
| Air Temperature | 0x01F9   | [5]             | int16           | value / 10.0     | C      |
| (reserved)      | 0x01FA   | [6]             | --              | --               | --     |
| CO2             | 0x01FB   | [7]             | uint16          | raw              | ppm    |
| (reserved)      | 0x01FC   | [8]             | --              | --               | --     |
| Pressure        | 0x01FD   | [9]             | uint16          | value / 10.0     | kPa    |
| Illuminance Hi  | 0x01FE   | [10]            | uint32 (combined) | (Hi<<16) \| Lo | lux    |
| Illuminance Lo  | 0x01FF   | [11]            | uint32 (combined) | --              | lux    |
| (reserved)      | 0x0200   | [12]            | --              | --               | --     |
| Rainfall        | 0x0201   | [13]            | uint16          | value / 10.0     | mm     |
| (reserved)      | 0x0202   | [14]            | --              | --               | --     |
| Solar Radiation | 0x0203   | [15]            | uint16          | raw              | W/m2   |

- Starting address: `0x01F4` (500 decimal)
- Register count: 16
- Illuminance spans two registers (indices [10] and [11]), combined as `(raw[10] << 16) | raw[11]` into a 32-bit value

### 1.4 Read Strategy -- Median of Multiple Samples

For each sensor type, the firmware performs `readAttempt` (3) successful reads into a 2D buffer `rawBuffer[readAttempt][16]`. A median filter (`dataProcess::getMedian`) is then applied per parameter across the 3 samples to reject outliers. The median filter for 32-bit illuminance values uses `getMedian32`.

If fewer than 3 successful reads are obtained within `maxRetry` (5) individual Modbus transaction failures, the partial median is computed from whatever samples succeeded. If zero reads succeed, the sensor section data is zeroed.

### 1.5 Error Handling and Recovery

| Error Code | Meaning               | Recovery Action                                  |
|------------|-----------------------|--------------------------------------------------|
| 0x00       | `ku8MBSuccess`        | Normal path -- data captured                     |
| 0xE0       | Invalid Slave ID      | Full UART recovery: `hwSerial->end()`, 100 ms delay, re-init at 9600/8N1, 500 ms settle, drain RX buffer |
| Other      | Timeout/CRC/Bus error | 1000 ms delay, re-initialize ModbusMaster with slave ID, increment retry counter |

**Retry logic:**

1. Inner loop: up to `readAttempt` (3) successful reads, with up to `maxRetry` (5) total failures allowed.
2. If all `maxRetry` failures are exhausted with zero successful reads, `_consecutiveFailCount` increments. After `_maxConsecutiveFail` (3) consecutive total-failure events, the sensor data section is zeroed and the function returns `true` to avoid blocking the state machine.
3. On success, `_consecutiveFailCount` resets to 0.

**Zero-retry (post-read validation):**

- Soil: if `soil_humi == 0 || soil_temp == 0 || soil_ec == 0` after the first successful read, the firmware re-enters the full read loop up to `maxRetry` more times.
- Weather: if `air_humidity == 0 || air_temperature == 0 || CO2 == 0`, the same re-read logic applies.

### 1.6 UART Recovery Procedure

Called when `result == 0xE0` or on the first invocation (`_uartReady == false`):

```cpp
hwSerial->end();               // Tear down UART
delay(100);
hwSerial->begin(9600, SERIAL_8N1, D4, D10);  // Re-init
delay(500);
drain RX buffer;               // Discard stale bytes
hwSerial->flush();
delay(200);
modbus.preTransmission([]() {});
modbus.postTransmission([]() { delayMicroseconds(1000); });
```

---

## 2. MQTT

### 2.1 Broker Configuration

| Parameter     | Default Value                | Source               |
|---------------|------------------------------|----------------------|
| Broker        | `119.59.103.220`             | `MQTT_BROKER`        |
| Port          | 1883                         | `MQTT_PORT`          |
| Username      | `kmutt`                      | `MQTT_USER`          |
| Password      | `kmutt@kmutt`                | `MQTT_PASSWORD`      |
| Client ID     | `PCB_TEST_1`                 | `MQTT_CLIENT_ID`     |
| Buffer size   | 2048 bytes                   | `setBufferSize(2048)`|
| Keep-alive    | 90 seconds                   | `setKeepAlive(90)`   |
| Socket timeout| 45 seconds                   | `setSocketTimeout(45)`|
| QoS           | 0 (fire-and-forget)          | `publish(topic, payload, false)` |

All broker settings (host, port, user, password) are overridable at runtime via the WiFi AP Settings page and stored in NVS keys `mqttHost`, `mqttPort`, `mqttUser`, `mqttPass`.

### 2.2 Topics

| Topic                                   | Direction       | Purpose                                    |
|-----------------------------------------|-----------------|--------------------------------------------|
| `weather/Srisaket/Station_1`            | Device -> Broker| Compact sensor data JSON                   |
| `weather/Srisaket/Station_1/ping`       | Broker -> Device| (Reserved for ping)                        |
| `weather/Srisaket/Station_1/pong`       | Device -> Broker| Heartbeat / alive response                 |

### 2.3 Heartbeat JSON (Pong)

Published to `MQTT_PONG_TOPIC` immediately after GSM network registration succeeds:

```json
{
  "n": 0,
  "alive": 1,
  "vt": 3650,
  "heap": 54200,
  "uptime": 142,
  "gsm_rssi": 15,
  "fw": "2.3.5"
}
```

| Field      | Type   | Description                       |
|------------|--------|-----------------------------------|
| `n`        | int    | Message type: 0 = heartbeat       |
| `alive`    | int    | Always 1                          |
| `vt`       | int    | Battery voltage in mV             |
| `heap`     | int    | Free heap in bytes                |
| `uptime`   | int    | Milliseconds since boot / 1000    |
| `gsm_rssi` | int    | GSM signal quality (dBm)          |
| `fw`       | string | Firmware version string           |

### 2.4 Compact Sensor Data JSON

Published to `MQTT_TOPIC`. Built by `buildCompactJSON()` in `main_1.cpp`:

```json
{
  "n": 1,
  "seq": 42,
  "vt": 3650,
  "srs": 15,
  "d": "260526",
  "r": [
    {
      "d": "260526",
      "t": "0830",
      "sh": 452,
      "st": 268,
      "se": 320,
      "ph": 68,
      "sn": 42,
      "sp": 15,
      "sk": 38,
      "ws": 25,
      "wd": 180,
      "ah": 785,
      "at": 312,
      "co2": 410,
      "pr": 1013,
      "il": 32000,
      "rf": 0,
      "so": 340
    }
  ],
  "sniffer": {
    "mac": "AA:BB:CC:DD:EE:FF",
    "name": "SnifferDevice",
    "ts": "2026-05-26 08:30:00",
    "temp": "26.5",
    "hum": "78.2",
    "tmp117": "25.8",
    "delta": "0.7",
    "rain": "0",
    "leaf": "234",
    "par": "450",
    "soil": "35.2"
  }
}
```

#### Field Mapping -- Header

| Field  | Description                                      |
|--------|--------------------------------------------------|
| `n`    | Message type: 1 = data record                    |
| `seq`  | Monotonically increasing sequence number (uint16)|
| `vt`   | Battery voltage (mV)                             |
| `srs`  | GSM signal quality (dBm)                         |
| `d`    | Date stamp as `DDMMYY` string                    |

#### Field Mapping -- Per-Record (inside `r[]`)

| Field  | Source                | Raw Type | Scaling     | Unit    |
|--------|-----------------------|----------|-------------|---------|
| `d`    | Record date           | string   | `DDMMYY`    | --      |
| `t`    | Record time           | string   | `HHMM`      | --      |
| `sh`   | soil_humi             | uint16   | / 10.0      | %       |
| `st`   | soil_temp             | int16    | / 10.0      | C       |
| `se`   | soil_ec               | uint16   | raw         | uS/cm   |
| `ph`   | soil_ph               | uint8    | / 10.0      | --      |
| `sn`   | soil_N                | uint16   | raw         | mg/kg   |
| `sp`   | soil_P                | uint16   | raw         | mg/kg   |
| `sk`   | soil_K                | uint16   | raw         | mg/kg   |
| `ws`   | windSpeed             | uint16   | / 10.0      | m/s     |
| `wd`   | windDir_Deg           | uint16   | raw         | deg     |
| `ah`   | air_humidity          | uint16   | / 10.0      | %       |
| `at`   | air_temperature       | int16    | / 10.0      | C       |
| `co2`  | CO2                   | uint16   | raw         | ppm     |
| `pr`   | pressure              | uint16   | / 10.0      | kPa     |
| `il`   | illuminance           | uint32   | raw         | lux     |
| `rf`   | rainfall              | uint16   | / 10.0      | mm      |
| `so`   | solar                 | uint16   | raw         | W/m2    |

### 2.5 Sniffer Object (BLE NUS Data)

When a BLE NUS connection is active with at least one characteristic read, the `"sniffer"` object is appended to the compact JSON payload. This object carries data received from the BLE Sniffer Portal device.

| Field    | Type   | Description                                |
|----------|--------|--------------------------------------------|
| `mac`    | string | MAC address of connected BLE device        |
| `name`   | string | BLE advertisement name                     |
| `ts`     | string | Timestamp from sniffer (`YYYY-MM-DD HH:MM:SS`) |
| `temp`   | string | Temperature reading                        |
| `hum`    | string | Humidity reading                           |
| `tmp117` | string | TMP117 temperature sensor                  |
| `delta`  | string | Delta T value                              |
| `rain`   | string | Rainfall reading                           |
| `leaf`   | string | Leaf wetness reading                       |
| `par`    | string | PAR (photosynthetically active radiation)  |
| `soil`   | string | Soil moisture reading                      |

When the connected BLE device is in GATT mode (not NUS), a `"ble"` object is emitted instead, containing a `"chars"` array of `{"u": "<UUID>", "v": "<ASCII value>"}` entries.

### 2.6 Publish Sequence

1. GSM network registration (`connectNetwork()`)
2. `publishHeartbeat()` -- heartbeat to pong topic
3. After sensor reads and data save, `buildCompactJSON()` builds payload
4. `mqttConnect()` with retry loop (2-second intervals, up to `MQTT_PUBLISH_TIMEOUT` = 60 s)
5. `mqttPublish()` -- publishes payload
6. Post-publish: 10 iterations of `mqtt->loop()` with 200 ms delays (total 2 s) to ensure delivery
7. `_client->stop()` to free the TCP connection

---

## 3. InfluxDB v2 HTTP

### 3.1 Connection Parameters

| Parameter  | Default Value                                              | NVS Key        |
|------------|------------------------------------------------------------|----------------|
| Host       | `119.59.103.220`                                           | `influxHost`   |
| Port       | 8086                                                       | `influxPort`   |
| Token      | `NykyVkwU_wrgf3Sfw3A5DcY33gT6X-ZOW-gsd3Qk0E0AoIlUfrJPEK5adTyzineh99EzyyPy9qyCY6vTRqzjRA==` | `influxToken` |
| Org        | `Pamiang`                                                  | `influxOrg`    |
| Bucket     | `Srisaket_Station_I`                                      | `influxBucket` |
| Enabled    | false (must be enabled via Settings)                       | `influxEn`     |

### 3.2 Line Protocol -- weather_station Measurement

The first measurement written to InfluxDB:

```
weather_station,station=Srisaket soil_humi=45.2,soil_temp=26.8,soil_ec=320,soil_ph=6.8,soil_N=42,soil_P=15,soil_K=38,wind_speed=2.5,wind_dir=180,air_humi=78.5,air_temp=31.2,co2=410,pressure=101.3,illuminance=32000,rainfall=0.0,solar=340,battery=3.650,gsm_rssi=15
```

| Tag/Field        | Type   | Scaling      | Unit    |
|------------------|--------|--------------|---------|
| **Tag:** station | string | "Srisaket"   | --      |
| soil_humi        | float  | / 10.0       | %       |
| soil_temp        | float  | / 10.0       | C       |
| soil_ec          | int    | raw          | uS/cm   |
| soil_ph          | float  | / 10.0       | --      |
| soil_N           | int    | raw          | mg/kg   |
| soil_P           | int    | raw          | mg/kg   |
| soil_K           | int    | raw          | mg/kg   |
| wind_speed       | float  | / 10.0       | m/s     |
| wind_dir         | int    | raw          | deg     |
| air_humi         | float  | / 10.0       | %       |
| air_temp         | float  | / 10.0       | C       |
| co2              | int    | raw          | ppm     |
| pressure         | float  | / 10.0       | kPa     |
| illuminance      | int    | raw          | lux     |
| rainfall         | float  | / 10.0       | mm      |
| solar            | int    | raw          | W/m2    |
| battery          | float  | / 1000.0     | V       |
| gsm_rssi         | int    | raw          | dBm     |

### 3.3 Line Protocol -- sniffer_watchdog Measurement

Appended when BLE NUS connection is active with at least 9 parsed characteristics:

```
sniffer_watchdog,station=Srisaket,mac=AA:BB:CC:DD:EE:FF temp=26.5,hum=78.2,tmp117=25.8,delta=0.7,rain=0,leaf=234,par=450,soil=35.2
```

| Tag/Field   | Description                      |
|-------------|----------------------------------|
| **Tag:** station | Always "Srisaket"           |
| **Tag:** mac     | MAC of connected BLE device |
| temp        | Temperature                     |
| hum         | Humidity                        |
| tmp117      | TMP117 temperature              |
| delta       | Delta T                         |
| rain        | Rainfall                        |
| leaf        | Leaf wetness                    |
| par         | PAR light                       |
| soil        | Soil moisture                   |

Fields with value `"--"` or empty strings are omitted.

### 3.4 HTTP POST Format

```
POST /api/v2/write?org=Pamiang&bucket=Srisaket_Station_I&precision=ms HTTP/1.1
Host: 119.59.103.220
Authorization: Token <influxToken>
Content-Type: text/plain; charset=utf-8
Content-Length: <N>
Connection: close

<line protocol body>
```

### 3.5 TCP Mux Usage

The firmware uses SIM800 mux channel **1** (`TinyGsmClient(*modem, 1)`) for InfluxDB connections. Before opening, it closes any existing connection on channel 1 with `AT+CIPCLOSE=1`. TCP connect is attempted up to 3 times with 2-second delays between attempts.

Response parsing reads the HTTP status line to extract the status code. Response body (error text) is logged if non-empty.

---

## 4. BLE Nordic UART Service (NUS)

### 4.1 Service and Characteristic UUIDs

| UUID                                   | Role         | Description                     |
|-----------------------------------------|--------------|---------------------------------|
| `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | Service      | Nordic UART Service             |
| `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | TX (notify)  | Sniffer -> Gateway (notify)     |
| `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | RX (write)   | Gateway -> Sniffer (write cmd)  |

### 4.2 Connection Flow

1. `NimBLEDevice::init("WeatherStation-GW")` -- device name
2. Set TX power to `ESP_PWR_LVL_P3` (+9 dBm)
3. Scan for BLE devices (active scan, 100 ms interval, 99 ms window, 3 s duration)
4. User selects device via WiFi AP web UI; `connectAndReadBle(mac)` is called
5. Up to 3 connection attempts with 1-second delays
6. Service discovery: check for NUS service UUID
7. If NUS found: subscribe to TX characteristic for notifications
8. Write `"live"` (4 bytes) to RX characteristic to request immediate data push
9. MAC address persisted to NVS key `bleSavedMac` for auto-reconnect on next boot

### 4.3 NUS Notification JSON Payload

The sniffer device sends JSON objects via NUS TX notifications. Example:

```json
{
  "ts": "2026-05-26 08:30:00",
  "temp": "26.5",
  "hum": "78.2",
  "tmp117": "25.8",
  "delta": "0.7",
  "rain": "0",
  "leaf": "234",
  "par": "450",
  "soil": "35.2"
}
```

| Field   | Description                             |
|---------|-----------------------------------------|
| `ts`    | Timestamp in `YYYY-MM-DD HH:MM:SS`     |
| `temp`  | Temperature (C)                         |
| `hum`   | Humidity (%)                            |
| `tmp117`| TMP117 die temperature (C)             |
| `delta` | Delta T (C)                             |
| `rain`  | Rainfall reading                        |
| `leaf`  | Leaf wetness reading                    |
| `par`   | PAR (photosynthetically active radiation) |
| `soil`  | Soil moisture                           |

### 4.4 Notification Accumulator

Incoming NUS bytes are accumulated in a 400-byte ring buffer (`bleNusAccum`). The parser scans for the last `{` and the last `}` in the buffer. When a complete JSON object is detected, it is extracted into `bleNusJsonReady` and the `bleNusNotified` flag is set. The main loop then calls `parseNusJson()` which:

1. Rejects payloads containing `"error"` key
2. Extracts all 9 fields (`ts`, `temp`, `hum`, `tmp117`, `delta`, `rain`, `leaf`, `par`, `soil`)
3. Populates `bleConnDev.chars[]` with human-readable labels as UUID and string values
4. Calls `saveBleDataToCsv()` to write to `/BLE-DD-MM-YYYY.csv`
5. Sets `bleDataReceived = true` to advance the state machine

### 4.5 Live Data Request

At any time, the gateway can request fresh data from the sniffer by writing the ASCII string `"live"` (4 bytes, no null terminator) to the NUS RX characteristic. This happens:

- Once on initial connection
- Periodically every 10 seconds (NUS refresh interval) via `refreshBleData()`
- On-demand when the web UI sends a `/ble/refresh` request

### 4.6 Auto-Reconnect

On boot, if NVS key `bleSavedMac` contains a valid 17-character MAC address, the firmware sets `bleConnPending = true` and automatically connects to the previously paired device during the `STATE_WIFI_AP` phase.

---

## 5. BLE GATT Generic

### 5.1 Service Discovery Flow

When a connected BLE device does not expose the NUS service UUID, the firmware falls back to generic GATT discovery:

1. Call `blePersistClient->getServices(true)` to discover all services
2. Iterate over each service's characteristics
3. For each characteristic where `canRead()` returns true:
   - Read raw value via `ch->readValue()`
   - Convert to hex string (space-separated uppercase hex bytes, max 60 chars)
   - Convert to ASCII string (printable characters only, `.` for non-printable, max 30 chars)
   - Store in `bleConnDev.chars[]` at the next available index
4. Stop after discovering `BLE_CHAR_MAX` (16) readable characteristics

### 5.2 Data Display

| Field     | Format                                           |
|-----------|--------------------------------------------------|
| UUID      | Standard 36-char BLE UUID string                 |
| Hex Value | Space-separated uppercase hex (e.g. `1A 02 FF`)  |
| ASCII     | Printable characters with `.` for non-printable   |

### 5.3 Live Refresh

For GATT-mode connections, all readable characteristics are re-read every **5 seconds** via `refreshBleData()`. Each characteristic is read individually with WDT resets between reads.

---

## 6. SMTP Email

### 6.1 SIM800 AT Command Sequence

The `GsmHandler::sendEmail()` method executes the following AT command chain:

```
AT+EMAILCID=1                          // Use bearer profile 1
AT+EMAILTO=30                          // Email timeout 30 seconds
AT+SMTPSRV="<smtpServer>",<port>       // Set SMTP server and port
AT+SMTPAUTH=1,"<user>","<password>"    // Enable SMTP authentication
AT+SMTPFROM="<user>","WeatherStation"  // Set sender
AT+SMTPRCPT=0,0,"<to>","Recipient"     // Set recipient
AT+SMTPSUBJECT="<subject>"             // Set subject line
AT+SMTPBODY=<bodyLength>               // Initiate body, wait for ">SMTPBODY:" prompt
<stream body bytes>                    // Write email body directly to modem stream
AT+SMTPSEND                            // Trigger send
// Wait up to 30 seconds for "+SMTPSEND: 1" response
```

### 6.2 Default Configuration

| Parameter   | Default          | NVS Key       |
|-------------|------------------|---------------|
| SMTP Server | `smtp.gmail.com` | `emailsmtp`   |
| SMTP Port   | 587              | `emailport`   |
| Sender      | (user-set)       | `emailuser`   |
| Password    | (user-set)       | `emailpass`   |
| Recipient   | (user-set)       | `emailto`     |

### 6.3 Email Alarm (Failed Login)

When the `emailalarm` NVS setting is enabled, a failed web login attempt triggers an email to the primary recipient with subject `"WeatherStation Login Alarm"` and body containing:

```
FAILED login attempt
User: <attempted username>
IP: <client IP>
Time: DD-MM-YYYY HH:MM
```

---

## 7. NTP Time Synchronization

### 7.1 Time Acquisition Strategy

The firmware attempts time acquisition in the following priority order:

1. **GSM RTC**: `_modem->getGSMDateTime(DATE_FULL)` -- uses the cellular network's built-in clock
2. **NTP via SIM800**: `syncNtp()` -- queries `pool.ntp.org` via AT commands
3. **Last CSV backup**: reads the last timestamp from the most recent daily CSV file on LittleFS
4. **Fallback**: increments the last known time by `TIME_INCREMENT_MINUTES` (10 minutes)

### 7.2 AT+CNTP Command Flow

```
AT+CNTP="pool.ntp.org",0              // Configure NTP server, timezone offset 0
// Wait for OK response (3 s timeout)

AT+CNTP                               // Trigger NTP synchronization
// Poll for "+CNTP: 1" response (up to NTP_TIMEOUT_MS = 60 s)
// Feed WDT every 2 seconds during wait
```

Success is indicated by the URC `+CNTP: 1`. After successful NTP sync, the GSM RTC is updated and `getGSMDateTime()` returns the corrected time.

### 7.3 GSM DateTime Format

The GSM modem returns date/time in the format `"YY/MM/DD,HH:MM:SS"` (at least 14 characters). Parsing extracts year (+2000), month, date, hour, minute, second. Validation rejects dates before year 2024 or out-of-range values.

### 7.4 NTP Sync from BLE (Web UI)

The `/ntpsync` endpoint allows syncing the station clock from the connected BLE NUS device's `ts` field, which has format `YYYY-MM-DD HH:MM:SS`. This is useful when GSM is unavailable.

---

## 8. OTA Firmware Update (HTTP)

### 8.1 Request Format

The OTA update uses an HTTP GET request to the configured OTA server:

```
GET /update HTTP/1.1
Host: <ota_server>
Connection: close
x-ESP32-version: 2.3.5
x-ESP32-device: All-in-One
x-ESP32-project: <project_name>
x-ESP32-password: <download_password>

```

### 8.2 Custom Request Headers

| Header                  | Description                                   | Source                      |
|-------------------------|-----------------------------------------------|-----------------------------|
| `x-ESP32-version`       | Current firmware version string               | `FIRMWARE_VERSION`          |
| `x-ESP32-device`        | Device type identifier                        | NVS `otadevice`             |
| `x-ESP32-project`       | Project name                                  | NVS `otaproject`            |
| `x-ESP32-password`      | Download authentication password (optional)   | NVS `otadlpass`             |

### 8.3 Response Codes

| HTTP Code | Meaning                        | Action Taken                                  |
|-----------|--------------------------------|-----------------------------------------------|
| 200       | New firmware available         | Read Content-Length and x-MD5 headers, begin download |
| 304       | Already up to date             | No action, log message                        |
| 401       | Authentication failed          | Log warning, abort                            |
| Other     | Unexpected                     | No action                                     |

### 8.4 Response Headers Parsed

| Header           | Description                                      |
|------------------|--------------------------------------------------|
| `Content-Length` | Firmware binary size in bytes                    |
| `x-MD5`          | 32-character hex MD5 checksum of the firmware    |

### 8.5 Download and Flash Process

1. **TCP connect**: Uses SIM800 mux channel 1 (`TinyGsmClient(*modem, 1)`). Closes any existing connection first with `AT+CIPCLOSE=1`. Up to 3 TCP connect attempts with 3-second delays.
2. **HTTP GET**: Sends the request with custom headers.
3. **Parse response**: Extracts HTTP status code, `Content-Length`, and `x-MD5` from headers.
4. **Begin update**: `Update.begin(contentLen)` -- allocates flash partition.
5. **Set MD5**: If `x-MD5` header was present, `Update.setMD5(md5)` enables integrity verification.
6. **Chunk download**: Reads in 512-byte chunks with a 300-second overall timeout. WDT is reset on each chunk. Progress is logged as `"Written N / Total bytes"`.
7. **Finalize**: `Update.end(true)` writes to flash. On success, stores timestamp in NVS key `lastota`, delays 500 ms, then `ESP.restart()`.

### 8.6 OTA Configuration

| Parameter       | Default          | NVS Key       | Description                       |
|-----------------|------------------|---------------|-----------------------------------|
| Server URL      | (empty)          | `otaserver`   | Base URL (e.g. `http://host:8889`)|
| Project         | (empty)          | `otaproject`  | Project identifier                |
| Device          | `All-in-One`     | `otadevice`   | Device type string                |
| Download pass   | (empty)          | `otadlpass`   | Password for OTA server auth      |
| ArduinoOTA pass | `admin`          | `otapass`     | Password for local ArduinoOTA     |
| Check interval  | 24 hours         | `otainterval` | Auto-check period (0 = disabled)  |
| Boot check      | false            | `otaboot`     | Check on every boot               |

### 8.7 Trigger Conditions

OTA check runs when:
- Boot check is enabled (`otaboot = true`)
- Interval-based check is due (elapsed time >= `otainterval` hours)
- User triggers from web UI (`/otaupdate` sets `otaCheckNow = true`)

---

## 9. WiFi AP HTTP API

### 9.1 Access Point Configuration

| Parameter    | Value                |
|--------------|----------------------|
| SSID         | `WeatherStation_AP`  |
| Password     | `12345678`           |
| Channel      | 1                    |
| IP           | Default AP IP (192.168.4.1) |
| Port         | 80                   |
| Max duration | 5 minutes (configurable: 1, 5, 10, 15, 30, 60 min, or Never) |

The AP timeout counts only when zero clients are connected. Connecting a client resets the timer.

### 9.2 Session Authentication

| Property         | Value                                   |
|------------------|-----------------------------------------|
| Default username | `admin` (`WEB_DEFAULT_USER`)            |
| Default password | `admin` (`WEB_DEFAULT_PASS`)            |
| Token format     | 16-character uppercase hex string       |
| Token generation | XOR-shift PRNG seeded with `millis()` ^ `esp_random()` |
| Session timeout  | 10 minutes (`WEB_SESSION_TIMEOUT_MS`)   |
| Cookie name      | `sid`                                   |
| Cookie path      | `/`                                     |

Authentication flow:

1. Client submits `POST /login` with form fields `u` (username) and `p` (password)
2. Credentials validated against NVS-stored values (`webpass` key) and compiled defaults
3. On success: a 16-char token is generated and set as `Set-Cookie: sid=<token>; Path=/`
4. Subsequent requests: `checkAuth()` validates that the `Cookie` header contains the token and the session has not expired
5. Session timeout refreshes (resets the 10-minute timer) on every authenticated request
6. Password can be changed via `POST /setpwd` (minimum 4 characters, must match confirmation)

### 9.3 Complete Endpoint Reference

#### Authentication

| Method | Endpoint     | Auth | Purpose                                  |
|--------|-------------|------|------------------------------------------|
| GET    | `/`          | No   | Redirect to `/live` or `/login`          |
| GET    | `/login`     | No   | Login page (HTML form)                   |
| POST   | `/login`     | No   | Submit credentials; set session cookie   |
| GET    | `/logout`    | No   | Clear session; redirect to `/login`      |

#### Live Data

| Method | Endpoint     | Auth | Purpose                                  |
|--------|-------------|------|------------------------------------------|
| GET    | `/live`      | Yes  | Live dashboard with sensor data, BLE, battery, memory |
| GET    | `/api/live`  | No*  | JSON API: current sensor readings        |

`/api/live` returns sensor data without checking session authentication. Response format:

```json
{
  "gsm_rssi": 15,
  "batt": "3650 mV",
  "sh": "45.2",
  "st": "26.8",
  "se": "320",
  "sp": "6.8",
  "sn": "42",
  "spv": "15",
  "sk": "38",
  "ws": "2.5",
  "wd": "180",
  "ah": "78.5",
  "at": "31.2",
  "co2": "410",
  "pr": "101.3",
  "il": "32000",
  "rf": "0.0",
  "so": "340"
}
```

#### BLE Management

| Method | Endpoint          | Auth | Purpose                                  |
|--------|-------------------|------|------------------------------------------|
| GET    | `/ble/scan`       | Yes  | Trigger BLE scan (3 s); redirect to settings |
| GET    | `/ble/connect`    | Yes  | Connect to BLE device; param `mac=AA:BB:CC:DD:EE:FF` |
| GET    | `/ble/disconnect` | Yes  | Disconnect BLE device; redirect to `/live`  |
| GET    | `/ble/forget`     | Yes  | Remove saved BLE MAC from NVS; redirect to `/live` |
| GET    | `/ble/refresh`    | Yes  | Request immediate data push from NUS sniffer |
| GET    | `/api/ble`        | No*  | JSON API: BLE connection status and data  |

`/api/ble` response format:

```json
{
  "connected": true,
  "nus": true,
  "hasData": true,
  "age": 5,
  "mac": "AA:BB:CC:DD:EE:FF",
  "name": "SnifferDevice",
  "chars": [
    {"uuid": "Timestamp", "hex": "", "ascii": "2026-05-26 08:30:00"},
    {"uuid": "Temperature (C)", "hex": "", "ascii": "26.5"}
  ]
}
```

For GATT mode, `nus` is `false` and `chars` contains raw hex/ASCII data.

#### File Management

| Method | Endpoint     | Auth | Purpose                                  |
|--------|-------------|------|------------------------------------------|
| GET    | `/files`     | Yes  | List data CSV files with size and actions |
| GET    | `/download`  | Yes  | Download file; param `f=<filename>`      |
| POST   | `/delete`    | Yes  | Delete single file; param `f=<filename>` |
| POST   | `/deleteall` | Yes  | Delete all data CSV files                |

Data files are identified by pattern `DD-MM-YYYY.csv` or the literal name `DATA.csv`.

#### Event Log

| Method | Endpoint    | Auth | Purpose                                  |
|--------|------------|------|------------------------------------------|
| GET    | `/log`      | Yes  | View event log files and last 20 entries |
| GET    | `/logdl`    | Yes  | Download event log file; param `f=<filename>` |

Event logs are stored as `/Event-DD-MM-YYYY.csv` with format `DateTime,Event\r\n`.

#### Settings

| Method | Endpoint       | Auth | Purpose                                  |
|--------|----------------|------|------------------------------------------|
| GET    | `/settings`    | Yes  | Settings page (OTA, WiFi AP, BLE, data sources, InfluxDB, NTP, memory, MQTT, email, password) |
| POST   | `/setpwd`      | Yes  | Change web portal password; params `p1`, `p2` |
| POST   | `/setap`       | Yes  | Set AP idle timeout; param `apt` (minutes)   |
| POST   | `/setble`      | Yes  | Enable/disable BLE client; param `en` (checkbox) |
| POST   | `/setsrc`      | Yes  | Toggle data sources; params `mod`, `ble` (checkboxes) |
| POST   | `/setfile`     | Yes  | Set file rotation interval; param `di` (minutes) |
| POST   | `/setinflux`   | Yes  | Save InfluxDB config; params `en`, `host`, `port`, `tok`, `org`, `bkt` |
| POST   | `/setntp`      | Yes  | Enable/disable NTP sync; param `en`       |
| POST   | `/setmem`      | Yes  | Memory rollover settings; params `pct`, `en` |
| POST   | `/setmqtt`     | Yes  | Save MQTT config; params `en`, `host`, `port`, `user`, `pass` |
| POST   | `/setemail`    | Yes  | Save email config; params `en`, `gmail`, `gpass`, `eto`, `freq`, `lightto`, `alarm` |
| POST   | `/setota`      | Yes  | Save OTA config; params `srv`, `prj`, `dev`, `dpw`, `pw`, `iv`, `otaboot` |

#### Test / Diagnostic

| Method | Endpoint       | Auth | Purpose                                  |
|--------|----------------|------|------------------------------------------|
| GET    | `/testinflux`  | Yes  | Test InfluxDB TCP connectivity           |
| GET    | `/ntpsync`     | Yes  | Sync time from BLE NUS device            |
| GET    | `/testmqtt`    | Yes  | Test MQTT broker TCP connectivity        |
| GET    | `/testemail`   | Yes  | Send test email via GSM                  |
| GET    | `/testota`     | Yes  | Test OTA server connectivity             |
| GET    | `/otaupdate`   | Yes  | Trigger OTA firmware update (exits AP)   |

Test endpoints return JSON: `{"ok": true/false, "msg": "description"}`.

#### System

| Method | Endpoint    | Auth | Purpose                                  |
|--------|------------|------|------------------------------------------|
| GET    | `/about`    | Yes  | About page (credits, firmware version)   |
| GET    | `/reboot`   | Yes  | Reboot device after confirmation          |
| ANY    | (not found) | No   | Redirect to `/login`                      |

### 9.4 Auto-Refresh Intervals

The `/live` page uses JavaScript auto-polling:

| Data Source      | Interval | Endpoint      |
|------------------|----------|---------------|
| Sensor readings  | 30 s     | `/api/live`   |
| BLE status       | 5 s      | `/api/ble`    |

### 9.5 Event Logging

Every significant action (login, logout, setting change, file deletion, BLE connect/disconnect, OTA request, reboot) is appended to a daily event log file `/Event-DD-MM-YYYY.csv` with the format:

```csv
DateTime,Event
26/05/2026 08:30:15,Login SUCCESS user=admin ip=192.168.4.2
26/05/2026 08:30:45,BLE connect request: AA:BB:CC:DD:EE:FF
26/05/2026 08:35:10,Password changed
```

---

*End of Module 4 -- Communication Protocols*
