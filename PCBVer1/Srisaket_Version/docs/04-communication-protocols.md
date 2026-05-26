# Module 4: Communication Protocols

**Project:** All-in-One Weather Station (Srisaket Version)
**Firmware:** v2.3.5
**Credits:** IDEA Laboratory @ KMUTT

---

This module documents every communication protocol used by the weather station. The device talks to sensors over Modbus RTU, publishes data to the cloud via MQTT and InfluxDB, receives BLE data from Sniffer Portal devices, and exposes a local WiFi AP web interface for configuration and monitoring.

---

## Table of Contents

1. [Modbus RTU (RS-485)](#1-modbus-rtu-rs-485)
2. [MQTT](#2-mqtt)
3. [InfluxDB v2 HTTP](#3-influxdb-v2-http)
4. [BLE NUS (Nordic UART Service)](#4-ble-nus-nordic-uart-service)
5. [BLE GATT](#5-ble-gatt)
6. [SMTP Email (via SIM800 AT)](#6-smtp-email-via-sim800-at)
7. [NTP (via SIM800 AT)](#7-ntp-via-sim800-at)
8. [OTA HTTP (via GSM)](#8-ota-http-via-gsm)
9. [WiFi AP HTTP API](#9-wifi-ap-http-api)

---

## 1. Modbus RTU (RS-485)

The weather station communicates with two Modbus RTU slave devices over a shared RS-485 bus. The bus operates at **9600 baud, 8N1** (8 data bits, no parity, 1 stop bit) in half-duplex mode.

**Hardware pins:**
- TX: D10
- RX: D4
- UART: Hardware Serial 1

**Bus configuration (defined in `include/utilities.h`):**

| Parameter       | Value    |
|-----------------|----------|
| Baud rate       | 9600     |
| Data bits       | 8        |
| Parity          | None     |
| Stop bits       | 1        |
| Mode            | Half-duplex |
| Post-TX delay   | 500 us (initial), 1000 us (after recovery) |

### 1.1 Soil Sensor Register Map (Slave 0x03)

**Read command:** Function code `0x03` (Read Holding Registers), Start address `0x0000`, Quantity `7`.

| Register | Offset | Length | Field       | Unit    | Scale          |
|----------|--------|--------|-------------|---------|----------------|
| 0x0000   | 0      | 1      | Moisture    | %       | x10            |
| 0x0001   | 1      | 1      | Temperature | deg C   | x10 (signed)   |
| 0x0002   | 2      | 1      | EC          | uS/cm   | direct         |
| 0x0003   | 3      | 1      | pH          | --      | x10 (clamped 0--25.5) |
| 0x0004   | 4      | 1      | Nitrogen    | mg/kg   | direct         |
| 0x0005   | 5      | 1      | Phosphorus  | mg/kg   | direct         |
| 0x0006   | 6      | 1      | Potassium   | mg/kg   | direct         |

**pH clamping:** The raw pH register value is clamped to a maximum of 255 (representing pH 25.5) before being stored as a `uint8_t`. The actual pH is obtained by dividing by 10 (e.g., raw 68 = pH 6.8).

**Code mapping (`sensor_v2.cpp`, SOIL branch):**

| rawBuffer index | SensorData field  | Notes                        |
|-----------------|-------------------|------------------------------|
| `[0]`           | `soil_humi`       | Moisture % x10               |
| `[1]`           | `soil_temp`       | Temperature x10 (int16_t)    |
| `[2]`           | `soil_ec`         | EC, direct value             |
| `[3]`           | `soil_ph`         | pH x10, clamped to 255       |
| `[4]`           | `soil_N`          | Nitrogen, direct             |
| `[5]`           | `soil_P`          | Phosphorus, direct           |
| `[6]`           | `soil_K`          | Potassium, direct            |

### 1.2 Weather Station Register Map (Slave 0x01)

**Read command:** Function code `0x03` (Read Holding Registers), Start address `0x01F4`, Quantity `16`.

| Register       | Offset | Length | Field          | Unit    | Scale            |
|----------------|--------|--------|----------------|---------|------------------|
| 0x01F4         | 0      | 1      | Wind Speed     | m/s     | x10              |
| 0x01F5--0x01F6 | 1--2   | 2      | (reserved)     | --      | --               |
| 0x01F7         | 3      | 1      | Wind Direction | degrees | direct           |
| 0x01F8         | 4      | 1      | Air Humidity   | %       | x10              |
| 0x01F9         | 5      | 1      | Air Temperature| deg C   | x10 (signed)     |
| 0x01FA--0x01FB | 6--7   | 1+1    | CO2 / (reserved)| ppm    | CO2 direct       |
| 0x01FC         | 8      | --     | (offset)       | --      | --               |
| 0x01FD         | 9      | 1      | Pressure       | kPa     | x10              |
| 0x01FE--0x01FF | 10--11 | 2      | Illuminance    | lux     | 32-bit direct    |
| 0x0200--0x0201 | 12--13 | 1+1    | (reserved) / Rainfall | mm | x10           |
| 0x0202--0x0203 | 14--15 | 1+1    | (reserved) / Solar | W/m2 | direct        |

**Code mapping (`sensor_v2.cpp`, WEATHER branch):**

| rawBuffer index | SensorData field    | Notes                              |
|-----------------|---------------------|------------------------------------|
| `[0]`           | `windSpeed`         | Wind speed x10                     |
| `[3]`           | `windDir_Deg`       | Wind direction, direct             |
| `[4]`           | `air_humidity`      | Humidity % x10                     |
| `[5]`           | `air_temperature`   | Temperature x10 (int16_t)          |
| `[7]`           | `CO2`               | CO2 ppm, direct                    |
| `[9]`           | `pressure`          | Pressure x10                       |
| `[10:11]`       | `illuminance`       | 32-bit: `(buf[10] << 16) \| buf[11]` |
| `[13]`          | `rainfall`          | Rainfall x10                       |
| `[15]`          | `solar`             | Solar irradiance, direct           |

### 1.3 Read Strategy and Error Handling

The Modbus read function implements a robust multi-sample strategy with error recovery:

**Sampling parameters (defined in `include/utilities.h`):**

| Parameter       | Value | Description                              |
|-----------------|-------|------------------------------------------|
| `readAttempt`   | 3     | Number of successful samples per read    |
| `maxRetry`      | 5     | Maximum consecutive failure retries      |

**Read flow:**

1. If the UART has not been initialized since boot, perform a full UART recovery first.
2. Otherwise, flush and settle the serial port (drain RX buffer, flush TX, wait 200 ms).
3. Initialize the ModbusMaster library for the target slave ID.
4. Attempt to read holding registers. On success, store the response buffer into `rawBuffer[attempt][]` and increment the attempt counter.
5. On failure, increment the retry counter. If the error code is `0xE0` (Invalid Slave ID), perform a full UART recovery:
   - Call `hwSerial->end()` to shut down the UART.
   - Wait 100 ms, then re-initialize at 9600 baud 8N1.
   - Wait 500 ms, then drain the RX buffer and flush TX.
   - Wait 200 ms, then re-initialize ModbusMaster.
6. For other errors, wait 1000 ms and retry.
7. After collecting samples, compute the **median** of the successful samples for each register (16-bit median for most fields, 32-bit median for illuminance).

**Zero-retry logic:** After a successful read, if key fields are all zero (soil: humi/temp/EC; weather: humidity/temp/CO2), the code re-reads the sensor up to `maxRetry` additional times to ensure valid data.

**Consecutive failure tracking:** If all retries are exhausted with zero successful samples, a consecutive failure counter is incremented. After 3 consecutive total failures, the sensor section is zeroed out and the read returns `true` (to prevent indefinite blocking).

### 1.4 UART Recovery Procedure

When a critical Modbus error is detected (error code `0xE0`), the following recovery is executed:

```
1. hwSerial->end()          // Shut down UART
2. delay(100 ms)
3. hwSerial->begin(9600, SERIAL_8N1, RX_PIN, TX_PIN)  // Re-initialize
4. delay(500 ms)
5. Drain RX buffer          // Read and discard all pending bytes
6. Flush TX buffer
7. delay(200 ms)
8. Re-initialize ModbusMaster pre/post transmission callbacks
```

### 1.5 Write Operation

Writing a single register uses function code `0x06` (Write Single Register):

```
modbus.writeSingleRegister(address, value)
```

Up to 5 retries with 50 ms delay between attempts. Returns `true` on success.

---

## 2. MQTT

The weather station publishes sensor data and heartbeat messages to an MQTT broker via the GSM modem connection. The MQTT implementation uses the PubSubClient library over a TinyGsmClient TCP transport.

### 2.1 Broker Configuration

| Parameter     | Value                    | Source                |
|---------------|--------------------------|-----------------------|
| Broker host   | `119.59.103.220`         | `utilities.h` default, overridable via NVS |
| Broker port   | `1883`                   | `utilities.h` default, overridable via NVS |
| Username      | `kmutt`                  | Default, overridable via NVS |
| Password      | `kmutt@kmutt`            | Default, overridable via NVS |
| Client ID     | `PCB_TEST_1`             | Hardcoded in `GsmHandler.cpp` |
| Keep alive    | 90 seconds               | Set in `GsmHandler::init()` |
| Socket timeout| 45 seconds               | Set in `GsmHandler::init()` |
| Buffer size   | 2048 bytes               | Set in `GsmHandler::init()` |
| Publish timeout| 60000 ms                | `MQTT_PUBLISH_TIMEOUT` |

### 2.2 Topics

| Topic                                    | Direction  | Purpose                              |
|------------------------------------------|------------|--------------------------------------|
| `weather/Srisaket/Station_1/ping`        | Subscribe  | Incoming ping (triggers heartbeat)   |
| `weather/Srisaket/Station_1/pong`        | Publish    | Heartbeat response                   |
| `weather/Srisaket/Station_1`             | Publish    | Sensor data (compact JSON)           |

### 2.3 Heartbeat JSON

Published to the pong topic immediately after GSM initialization succeeds. This is the first outbound message each cycle, confirming the device is alive.

```json
{"n":0,"alive":1,"vt":3200,"heap":45000,"uptime":65,"gsm_rssi":15,"fw":"2.3.5"}
```

**Field definitions:**

| Field      | Type   | Description                                     |
|------------|--------|-------------------------------------------------|
| `n`        | int    | Message type marker: `0` = heartbeat            |
| `alive`    | int    | Always `1` (device alive confirmation)          |
| `vt`       | int    | Battery voltage in millivolts                   |
| `heap`     | int    | Free heap memory in bytes (`ESP.getFreeHeap()`) |
| `uptime`   | int    | Seconds since boot (`millis() / 1000`)          |
| `gsm_rssi` | int    | GSM signal quality from `getSignalQuality()`    |
| `fw`       | string | Firmware version string (e.g. `"2.3.5"`)       |

### 2.4 Sensor Data JSON

Published to the main data topic using the `buildCompactJSON` function. All numeric sensor values are raw integers (divide by 10 where applicable to obtain actual units).

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
  "sniffer":{
    "mac":"AA:BB:CC:DD:EE:FF","name":"Sniffer1",
    "ts":"2026-05-26T14:30:00","temp":"25.3","hum":"65.2",
    "tmp117":"24.8","delta":"0.5","rain":"0","leaf":"2.1",
    "par":"450","soil":"35.6"
  }
}
```

**Top-level fields:**

| Field  | Type   | Description                                     |
|--------|--------|-------------------------------------------------|
| `n`    | int    | Message type marker: `1` = sensor data          |
| `seq`  | int    | Monotonically increasing publish sequence number |
| `vt`   | int    | Battery voltage (mV)                            |
| `srs`  | int    | GSM signal quality (RSSI)                       |
| `d`    | string | Date stamp, `DDMMYY` format                     |
| `r`    | array  | Array of sensor reading records                 |

**Reading record fields:**

| Field  | Maps to            | Unit    | Notes                     |
|--------|--------------------|---------|---------------------------|
| `d`    | --                 | --      | Date stamp `DDMMYY`       |
| `t`    | --                 | --      | Time stamp `HHMM`         |
| `sh`   | soil_humi          | %       | x10 (divide by 10)        |
| `st`   | soil_temp          | deg C   | x10, signed               |
| `se`   | soil_ec            | uS/cm   | direct                    |
| `ph`   | soil_ph            | --      | x10 (divide by 10)        |
| `sn`   | soil_N             | mg/kg   | direct                    |
| `sp`   | soil_P             | mg/kg   | direct                    |
| `sk`   | soil_K             | mg/kg   | direct                    |
| `ws`   | windSpeed          | m/s     | x10 (divide by 10)        |
| `wd`   | windDir_Deg        | degrees | direct                    |
| `ah`   | air_humidity       | %       | x10 (divide by 10)        |
| `at`   | air_temperature    | deg C   | x10, signed               |
| `co2`  | CO2                | ppm     | direct                    |
| `pr`   | pressure           | kPa     | x10 (divide by 10)        |
| `il`   | illuminance        | lux     | 32-bit direct             |
| `rf`   | rainfall           | mm      | x10 (divide by 10)        |
| `so`   | solar              | W/m2    | direct                    |

**Sniffer object (conditional):** When a BLE NUS Sniffer Portal device is connected and has data, a `sniffer` object is appended with the following fields:

| Field     | Type   | Description                        |
|-----------|--------|------------------------------------|
| `mac`     | string | BLE device MAC address             |
| `name`    | string | BLE device name                    |
| `ts`      | string | ISO 8601 timestamp                 |
| `temp`    | string | Temperature (deg C)                |
| `hum`     | string | Humidity (%)                       |
| `tmp117`  | string | TMP117 temperature (deg C)         |
| `delta`   | string | Delta T (deg C)                    |
| `rain`    | string | Rainfall status                    |
| `leaf`    | string | Leaf wetness                       |
| `par`     | string | PAR light                          |
| `soil`    | string | Soil moisture                      |

When the connected BLE device is a generic GATT device (not NUS), a `ble` object is included instead, containing raw characteristic UUIDs and hex values.

### 2.5 MQTT Connection Flow

1. After GSM network registration and GPRS connection succeed, `publishHeartbeat()` is called.
2. `mqttConnect()` opens a TCP connection to the broker and authenticates with username/password.
3. The heartbeat payload is published to the pong topic.
4. After each `mqttPublish()` call, the client loops for 2 seconds (10 x 200 ms) to ensure delivery, then stops the underlying TCP client.
5. MQTT settings (host, port, user, password) can be changed at runtime through the WiFi AP settings page and are persisted to NVS.

---

## 3. InfluxDB v2 HTTP

Sensor data is written to an InfluxDB v2 instance using the line protocol over HTTP. The connection uses SIM800 mux ID **1** (mux 0 is reserved for the MQTT client).

### 3.1 Configuration

| Parameter  | Default Value                          | NVS Key        |
|------------|----------------------------------------|----------------|
| Host       | `119.59.103.220`                       | `influxHost`   |
| Port       | `8086`                                 | `influxPort`   |
| Token      | (long base64 token)                    | `influxToken`  |
| Org        | `Pamiang`                              | `influxOrg`    |
| Bucket     | `Srisaket_Station_I`                   | `influxBucket` |
| Enabled    | `false` (must be enabled in settings)  | `influxEn`     |

All parameters are stored in NVS under the `ws-cfg` namespace and configurable via the WiFi AP settings page.

### 3.2 Line Protocol Format

**Primary measurement (weather data):**

```
weather_station,station=Srisaket soil_humi=45.5,soil_temp=-3.0,soil_ec=350,soil_ph=6.8,soil_N=25,soil_P=30,soil_K=40,wind_speed=1.2,wind_dir=180,air_humi=75.0,air_temp=32.5,co2=410,pressure=1013.0,illuminance=25000,rainfall=0.0,solar=350,battery=3.200,gsm_rssi=15
```

Values are formatted with appropriate precision:
- `soil_humi`, `soil_temp`, `soil_ph`, `wind_speed`, `air_humi`, `air_temp`, `pressure`, `rainfall`, `battery` -- formatted with `%.1f` or `%.2f` (floating point, pre-divided by 10 or 1000 as needed).
- `soil_ec`, `soil_N`, `soil_P`, `soil_K`, `wind_dir`, `co2`, `solar` -- integer values, direct.
- `illuminance` -- unsigned long integer, direct.

**Sniffer watchdog measurement (conditional):** When a BLE NUS device is connected and has at least 9 characteristics parsed, a second line is appended:

```
sniffer_watchdog,station=Srisaket,mac=AA:BB:CC:DD:EE:FF temp=25.3,hum=65.2,tmp117=24.8,delta=0.5,rain=0,leaf=2.1,par=450,soil=35.6
```

Fields with `--` or empty values are omitted from the line.

### 3.3 HTTP Request

```
POST /api/v2/write?org=<org>&bucket=<bucket>&precision=ms HTTP/1.1
Host: <host>
Authorization: Token <token>
Content-Type: text/plain; charset=utf-8
Content-Length: <length>
Connection: close

<line protocol body>
```

**Connection parameters:**

| Parameter           | Value                          |
|---------------------|--------------------------------|
| TCP mux ID          | 1 (mux 0 reserved for MQTT)   |
| Connect retries     | 3                              |
| Retry delay         | 2000 ms                        |
| Response timeout    | 10000 ms                       |
| Pre-connect cleanup | `AT+CIPCLOSE=1` sent before connect |

The response status code is parsed from the HTTP status line. A successful write returns HTTP `204 No Content`.

---

## 4. BLE NUS (Nordic UART Service)

The weather station acts as a BLE Central device that connects to Sniffer Portal peripherals using the Nordic UART Service (NUS). NUS provides a transparent UART-like data channel over BLE notifications and writes.

### 4.1 Service and Characteristic UUIDs

| Item                 | UUID                                   |
|----------------------|----------------------------------------|
| Service              | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| TX Characteristic (notify) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| RX Characteristic (write)  | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |

- **TX (notify):** The sniffer sends data to the weather station via notifications on this characteristic. The station subscribes to notifications upon connection.
- **RX (write):** The weather station sends commands to the sniffer by writing to this characteristic.

### 4.2 NUS JSON Notification Payload

The sniffer device sends sensor data as a JSON object via NUS notifications. The payload may arrive in multiple notification packets and is accumulated until a complete JSON object (matched `{` to `}`) is detected.

```json
{"ts":"2026-05-26T14:30:00","temp":"25.3","hum":"65.2","tmp117":"24.8","delta":"0.5","rain":"0","leaf":"2.1","par":"450","soil":"35.6"}
```

**Field definitions:**

| Field     | Type   | Description                        |
|-----------|--------|------------------------------------|
| `ts`      | string | ISO 8601 timestamp (`YYYY-MM-DDTHH:MM:SS`) |
| `temp`    | string | Temperature (deg C)                |
| `hum`     | string | Humidity (%)                       |
| `tmp117`  | string | TMP117 precision temperature (deg C) |
| `delta`   | string | Delta T, temperature difference (deg C) |
| `rain`    | string | Rainfall status                    |
| `leaf`    | string | Leaf wetness value                 |
| `par`     | string | PAR (Photosynthetically Active Radiation) light value |
| `soil`    | string | Soil moisture value                |

All values are transmitted as strings.

### 4.3 NUS Command Protocol

The weather station sends commands to the sniffer via the RX characteristic:

| Command | Value  | Purpose                             |
|---------|--------|-------------------------------------|
| `live`  | `"live"` (4 bytes) | Request immediate data push from sniffer |

The `live` command is sent:
- Immediately after NUS connection is established.
- When the web UI requests a data refresh (`/ble/refresh` endpoint).
- During periodic refresh cycles (every 10 seconds for NUS).

### 4.4 Notification Accumulator

NUS notifications may arrive in fragments. The accumulator handles reassembly:

1. Received bytes are appended to `bleNusAccum[]` (400-byte buffer).
2. The accumulator searches backward from the end for the last `}` closing brace.
3. It then searches forward from the beginning for the matching `{` opening brace.
4. The text between `{` and `}` (inclusive) is copied to `bleNusJsonReady[]` as a complete JSON payload.
5. Any remaining bytes after the `}` are shifted to the front of the accumulator for the next notification.
6. If the accumulator fills to capacity without finding a complete JSON object, it is reset.

### 4.5 Auto-Reconnect

When a NUS device is successfully connected:
1. The MAC address is saved to NVS key `bleSavedMac` in the `ws-cfg` namespace.
2. On subsequent boots, if `bleSavedMac` is present in NVS, the device automatically schedules a connection attempt (`bleConnPending = true`).
3. The auto-reconnect uses up to 3 connection attempts with 1000 ms delay between retries.
4. The saved MAC can be cleared via the WiFi AP web interface (`/ble/forget` endpoint).

### 4.6 NUS Data CSV Logging

Received NUS data is saved to a daily BLE CSV file (`/BLE-DD-MM-YYYY.csv`) with the following format:

```
Date,Time,Temperature(C),Humidity(%),TMP117(C),DeltaT(C),Rainfall,LeafWetness,PAR,SoilMoisture
26/05/2026,14:30:00,25.3,65.2,24.8,0.5,0,2.1,450,35.6
```

Duplicate entries are prevented by comparing the timestamp field against the last saved timestamp.

---

## 5. BLE GATT

For BLE devices that are not NUS Sniffer Portals, the weather station falls back to generic GATT service discovery and characteristic reading.

### 5.1 Discovery Procedure

After establishing a BLE connection, the station checks for the NUS service UUID. If not found, it enters GATT mode:

1. Discovers all services on the connected device (`getServices(true)`).
2. For each service, iterates through all characteristics.
3. For each readable characteristic (`canRead() == true`):
   - Reads the raw byte value.
   - Converts to a hex string representation (e.g., `"48 65 6C 6C 6F "`).
   - Converts to printable ASCII, replacing non-printable bytes with `.`.
4. Stores up to **16 characteristics** maximum (`BLE_CHAR_MAX`).

### 5.2 GATT Data Format

| Field    | Description                                    |
|----------|------------------------------------------------|
| `uuid`   | Characteristic UUID string                     |
| `value`  | Hex string of raw bytes (e.g. `"1A 2B 3C "`)  |
| `ascii`  | Printable ASCII representation                 |

### 5.3 BLE Scan Parameters

| Parameter       | Value    |
|-----------------|----------|
| Scan type       | Active scan |
| Scan interval   | 100 (unit: 0.625 ms) |
| Scan window     | 99 (unit: 0.625 ms) |
| Scan duration   | 3 seconds |
| Max devices     | 10 (`BLE_MAX_DEVICES`) |

The scan results store device name, MAC address, and RSSI for each discovered device.

### 5.4 BLE Refresh Intervals

| Mode      | Refresh interval |
|-----------|------------------|
| NUS       | 10 seconds       |
| GATT      | 5 seconds        |

Periodic BLE scans (when not connected) run every **30 seconds**.

---

## 6. SMTP Email (via SIM800 AT)

The weather station can send email alerts using the SIM800 modem's built-in SMTP client. This is used for login failure notifications and periodic status reports.

### 6.1 AT Command Sequence

The `GsmHandler::sendEmail()` function executes the following AT commands:

```
AT+EMAILCID=1                          // Bind email to GPRS context
AT+EMAILTO=30                          // Set email timeout to 30 seconds
AT+SMTPSRV="<server>",<port>           // Configure SMTP server
AT+SMTPAUTH=1,"<user>","<pass>"        // Enable SMTP authentication
AT+SMTPFROM="<user>","WeatherStation"  // Set sender address and name
AT+SMTPRCPT=0,0,"<to>","Recipient"     // Add recipient
AT+SMTPSUBJECT="<subject>"             // Set email subject
AT+SMTPBODY=<length>                   // Initiate body transfer
<body content>                         // Write body bytes to stream
AT+SMTPSEND                            // Send the email
```

**Default configuration (from `utilities.h`):**

| Parameter   | Default  | NVS Key      |
|-------------|----------|--------------|
| SMTP server | (none)   | `emailsmtp`  |
| Port        | `587`    | `emailport`  |
| Username    | (none)   | `emailuser`  |
| Password    | (none)   | `emailpass`  |
| Recipient   | (none)   | `emailto`    |

When the user saves Gmail credentials via the settings page, the SMTP server is automatically set to `smtp.gmail.com` and the port to `587`.

### 6.2 Login Failure Alert

When a failed login attempt is detected on the WiFi AP web portal and the email alarm is enabled (`emailalarm` NVS key), the system sends an alert email containing:

- The attempted username
- The client IP address
- The timestamp of the attempt

---

## 7. NTP (via SIM800 AT)

The weather station synchronizes its clock using the SIM800 modem's NTP functionality. This is critical because the ESP32 does not have a battery-backed RTC.

### 7.1 AT Command Sequence

The `GsmHandler::syncNtp()` function executes:

```
AT+CNTP="pool.ntp.org",0              // Configure NTP server (UTC+0)
AT+CNTP                                // Trigger NTP sync
```

After sending `AT+CNTP`, the code waits up to **60 seconds** (`NTP_TIMEOUT_MS`) for the response `+CNTP: 1`, which indicates a successful sync.

### 7.2 Time Retrieval Flow

Time synchronization follows a priority chain:

1. **GSM network time:** First, `getGSMDateTime(DATE_FULL)` is called to read the modem's RTC. If the returned datetime string is valid (length >= 14 characters, sensible date/time ranges), it is parsed directly.
2. **NTP sync:** If the GSM RTC returns an invalid datetime, `syncNtp()` is called to synchronize via NTP. After a 1-second delay, the GSM RTC is read again.
3. **Fallback (in `main_1.cpp`):** If neither GSM time nor NTP succeeds, the system reads the last timestamp from the daily CSV backup file and increments it by `TIME_INCREMENT_MINUTES` (10 minutes). If no backup exists, the time is zeroed.

**GSM datetime format:** The modem returns a string in the format `YY/MM/DD,HH:MM:SS` (e.g., `"26/05/26,14:30:00"`). Year is offset by 2000.

---

## 8. OTA HTTP (via GSM)

The weather station supports over-the-air firmware updates via HTTP over the GSM connection. The update server is configurable through the WiFi AP settings page.

### 8.1 HTTP Request

The OTA check sends an HTTP GET request to the update server:

```
GET /update HTTP/1.1
Host: <server>
Connection: close
x-ESP32-version: 2.3.5
x-ESP32-device: All-in-One
x-ESP32-project: <project>
x-ESP32-password: <password>

```

**Custom headers:**

| Header               | Description                          |
|----------------------|--------------------------------------|
| `x-ESP32-version`    | Current firmware version string      |
| `x-ESP32-device`     | Device type (default: `"All-in-One"`) |
| `x-ESP32-project`    | Project identifier (configurable)    |
| `x-ESP32-password`   | Download authentication password     |

### 8.2 Response Codes

| Code | Meaning               | Action                                          |
|------|-----------------------|--------------------------------------------------|
| 200  | New firmware available| Download, flash, verify MD5, reboot              |
| 304  | Already up to date    | No action                                        |
| 401  | Authentication failed | Log error, skip                                  |
| Other| Error                 | Log error, skip                                  |

### 8.3 Firmware Download and Flashing

When HTTP 200 is received:

1. Parse response headers for `Content-Length` and `x-md5`.
2. Initialize `Update` library with the content length (or `UPDATE_SIZE_UNKNOWN` if not provided).
3. If MD5 is available, set it via `Update.setMD5()`.
4. Download firmware in **512-byte chunks**.
5. Write each chunk to flash via `Update.write()`.
6. Total download timeout: **5 minutes** (300,000 ms). The timeout resets on each successful chunk.
7. After all bytes are written, call `Update.end(true)` to finalize.
8. Save the OTA timestamp to NVS (`lastota` key).
9. Reboot the device via `ESP.restart()`.

**Error handling:**
- If `Update.write()` returns a different size than requested, the update is aborted.
- If `Update.end()` fails, the error code is logged.
- TCP connection uses up to 3 connect retries with 3-second delays between attempts.
- Mux ID 1 is used (mux 0 reserved for MQTT).

### 8.4 OTA Configuration

| Parameter      | Default       | NVS Key       | Description                        |
|----------------|---------------|---------------|------------------------------------|
| Server URL     | (none)        | `otaserver`   | Base URL (e.g. `http://host:8889`) |
| Project name   | (none)        | `otaproject`  | Project identifier                 |
| Device type    | `All-in-One`  | `otadevice`   | Device type string                 |
| Download pass  | (none)        | `otadlpass`   | Password for OTA server auth       |
| Local OTA pass | `admin`       | `otapass`     | ArduinoOTA password                |
| Check interval | 24 hours      | `otainterval` | Auto-check interval (hours, 0=off) |
| Boot check     | false         | `otaboot`     | Check on every boot                |

### 8.5 OTA Triggers

OTA checks can be triggered by:
- **Boot check:** If `otaboot` is enabled and the interval has elapsed since the last check.
- **Web UI trigger:** The `/otaupdate` endpoint sets `otaCheckNow = true`, causing the main loop to close WiFi AP, initialize GSM, and run `checkRemoteOTA()`.
- **Interval check:** If the configured interval (in hours) has elapsed since `lastOtaCheckMs`.

---

## 9. WiFi AP HTTP API

The weather station exposes a full-featured web interface over a WiFi access point. The AP starts at boot and runs simultaneously with BLE operations.

### 9.1 Access Point Configuration

| Parameter     | Value                    |
|---------------|--------------------------|
| SSID          | `WeatherStation_AP`      |
| Password      | `12345678`               |
| Channel       | 1                        |
| Max duration  | 5 minutes (configurable) |
| Min duration  | 1 minute                 |

The AP closes when either:
- A minimum of 1 minute has elapsed AND BLE data has been received.
- A minimum of 1 minute has elapsed AND no clients are connected for the idle timeout period.
- OTA is requested (closes AP immediately).

### 9.2 Session Authentication

Authentication uses a cookie-based session token:

| Parameter         | Value              |
|-------------------|--------------------|
| Token format      | 16-character hex string (`%08lX%08lX`) |
| Session timeout   | 10 minutes (`WEB_SESSION_TIMEOUT_MS = 600000`) |
| Default username  | `admin`            |
| Default password  | `admin`            |
| Cookie name       | `sid`              |

The session token is refreshed on every authenticated request. The token is generated using a simple XOR-shift PRNG seeded with `millis()` XOR `esp_random()`.

### 9.3 API Endpoint Reference

| Path               | Method   | Auth  | Purpose                              |
|--------------------|----------|-------|--------------------------------------|
| `/`                | GET      | No    | Redirect to `/live` or `/login`      |
| `/login`           | GET      | No    | Login page                           |
| `/login`           | POST     | No    | Login form submission                |
| `/logout`          | GET      | No    | Clear session, redirect to login     |
| `/live`            | GET      | Yes   | Live sensor dashboard                |
| `/api/live`        | GET      | Yes   | JSON sensor data (polled every 30s)  |
| `/api/ble`         | GET      | Yes   | JSON BLE data (polled every 5s)      |
| `/ble/scan`        | GET      | Yes   | Trigger BLE scan                     |
| `/ble/connect?mac=...` | GET | Yes   | Connect to BLE device by MAC address |
| `/ble/disconnect`  | GET      | Yes   | Disconnect current BLE device        |
| `/ble/forget`      | GET      | Yes   | Forget saved BLE device from NVS     |
| `/ble/refresh`     | GET      | Yes   | Request NUS data push                |
| `/files`           | GET      | Yes   | List data files                      |
| `/download?f=...`  | GET      | Yes   | Download a CSV file                  |
| `/delete`          | POST     | Yes   | Delete a single data file            |
| `/deleteall`       | POST     | Yes   | Delete all data files                |
| `/log`             | GET      | Yes   | Event log viewer                     |
| `/logdl?f=...`     | GET      | Yes   | Download event log file              |
| `/settings`        | GET      | Yes   | Configuration page                   |
| `/setpwd`          | POST     | Yes   | Change web password                  |
| `/setap`           | POST     | Yes   | Save AP timeout settings             |
| `/setble`          | POST     | Yes   | Save BLE client settings             |
| `/setsrc`          | POST     | Yes   | Save data source settings            |
| `/setfile`         | POST     | Yes   | Save file interval settings          |
| `/setinflux`       | POST     | Yes   | Save InfluxDB settings               |
| `/setntp`          | POST     | Yes   | Save NTP settings                    |
| `/ntpsync`         | GET      | Yes   | Sync time from BLE NUS device        |
| `/setmem`          | POST     | Yes   | Save memory rollover settings        |
| `/setmqtt`         | POST     | Yes   | Save MQTT settings                   |
| `/setemail`        | POST     | Yes   | Save email settings                  |
| `/setota`          | POST     | Yes   | Save OTA settings                    |
| `/reboot`          | GET      | Yes   | Reboot device                        |
| `/otaupdate`       | GET      | Yes   | Trigger OTA update                   |
| `/testinflux`      | GET      | Yes   | Test InfluxDB TCP connection         |
| `/testmqtt`        | GET      | Yes   | Test MQTT TCP connection             |
| `/testemail`       | GET      | Yes   | Send test email via GSM              |
| `/testota`         | GET      | Yes   | Test OTA server connectivity         |
| `/about`           | GET      | Yes   | About page                           |

### 9.4 JSON API Responses

**`/api/live` response:**

```json
{
  "gsm_rssi": 15,
  "batt": "3200 mV",
  "sh": "45.5",
  "st": "-3.0",
  "se": "350",
  "sp": "6.8",
  "sn": "25",
  "spv": "30",
  "sk": "40",
  "ws": "1.2",
  "wd": "180",
  "ah": "75.0",
  "at": "32.5",
  "co2": "410",
  "pr": "1013.0",
  "il": "25000",
  "rf": "0.0",
  "so": "350"
}
```

All sensor values are formatted as strings with appropriate decimal precision (values already divided by 10 where applicable). Field names match the compact MQTT JSON abbreviations.

**`/api/ble` response:**

```json
{
  "connected": true,
  "nus": true,
  "hasData": true,
  "age": 5,
  "mac": "AA:BB:CC:DD:EE:FF",
  "name": "Sniffer1",
  "chars": [
    {"uuid": "Timestamp", "hex": "", "ascii": "2026-05-26T14:30:00"},
    {"uuid": "Temperature (C)", "hex": "", "ascii": "25.3"}
  ]
}
```

| Field       | Type    | Description                              |
|-------------|---------|------------------------------------------|
| `connected` | boolean | Whether a BLE device is connected        |
| `nus`       | boolean | Whether the device is a NUS Sniffer      |
| `hasData`   | boolean | Whether NUS data has been received       |
| `age`       | int     | Seconds since last NUS data update       |
| `mac`       | string  | Connected device MAC address             |
| `name`      | string  | Connected device name                    |
| `chars`     | array   | Array of characteristic data objects     |

For NUS devices, `uuid` contains the human-readable field label (e.g., `"Temperature (C)"`), and `ascii` contains the parsed value. For GATT devices, `uuid` is the actual BLE characteristic UUID, and `hex` contains the raw byte hex dump.

**Test endpoint responses (all return the same format):**

```json
{"ok": true, "msg": "TCP connected OK"}
```

```json
{"ok": false, "msg": "Connection failed"}
```

### 9.5 Event Logging

The WiFi AP server maintains a daily event log in CSV format (`/Event-DD-MM-YYYY.csv`):

```
DateTime,Event
26/05/2026 14:30:00,Login SUCCESS user=admin ip=192.168.4.2
26/05/2026 14:30:05,BLE connect request: AA:BB:CC:DD:EE:FF
26/05/2026 14:35:00,Login FAILED user=guest ip=192.168.4.3
```

Events are logged for: login success/failure, BLE operations, settings changes, file deletions, OTA requests, manual reboots, and email alarm triggers.

---

*End of Module 4 -- Communication Protocols*
