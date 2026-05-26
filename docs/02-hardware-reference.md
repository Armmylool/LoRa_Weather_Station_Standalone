# Module 2 -- Hardware Reference

> **Firmware:** v2.3.5 (2026-05-18)
> **Board:** Seeed XIAO ESP32-C3
> **Framework:** Arduino (PlatformIO, Espressif32 platform)
> **Project:** Srisaket Weather Station -- IDEA Laboratory @ KMUTT

---

## 1. MCU Specifications

| Parameter             | Value                                      |
|-----------------------|--------------------------------------------|
| Microcontroller       | ESP32-C3 (RISC-V single-core, 32-bit)     |
| Max Clock Frequency   | 160 MHz                                    |
| Flash Size            | 4 MB (external, quad-SPI)                  |
| SRAM                  | 400 KB                                     |
| Bluetooth             | BLE 5 (NimBLE stack enabled at build)      |
| Wi-Fi                 | 2.4 GHz 802.11 b/g/n                      |
| USB Interface         | USB-C (CDC serial + download)              |
| Board Variant         | `seeed_xiao_esp32c3`                       |
| Build System          | `board_build.f_cpu = 160000000L`           |
| Partition Scheme      | Dual-OTA with LittleFS (see table below)  |
| Watchdog Timer        | Hardware WDT, 45 s timeout, auto-reset enabled |
| Debug Level           | `CORE_DEBUG_LEVEL=0` (none)                |

### Partition Layout (`partitions_ota_4mb.csv`)

| Partition  | Type   | SubType | Offset     | Size       | Notes                        |
|------------|--------|---------|------------|------------|------------------------------|
| nvs        | data   | nvs     | `0x9000`   | `0x5000`   | 20 KB -- NVS key-value store |
| otadata   | data   | ota     | `0xE000`   | `0x2000`   | 8 KB -- OTA boot selector    |
| app0       | app    | ota_0   | `0x10000`  | `0x1A0000` | 1.625 MB -- OTA slot A       |
| app1       | app    | ota_1   | `0x1B0000` | `0x1A0000` | 1.625 MB -- OTA slot B       |
| spiffs     | data   | spiffs  | `0x350000` | `0xB0000`  | 720 KB -- LittleFS (CSV/log) |

Current firmware image is approximately 1.27 MB, occupying ~78% of each OTA slot.

### Watchdog Configuration

- Timeout: **45 seconds** (`WDT_TIMEOUT_SEC`)
- Initialized in `setup()` via `esp_task_wdt_init(WDT_TIMEOUT_SEC, true)`
- Main task subscribed with `esp_task_wdt_add(NULL)`
- All blocking loops call `esp_task_wdt_reset()` periodically
- Upon timeout, the ESP32-C3 performs a hardware reset

---

## 2. Pin Assignment Table

| Pin Name | GPIO | Function          | Direction | Peripheral / Detail                                  |
|----------|------|-------------------|-----------|------------------------------------------------------|
| D4       | 4    | RS-485 RX         | Input     | UART1 RX, 9600 baud, 8N1                            |
| D10      | 10   | RS-485 TX         | Output    | UART1 TX, 9600 baud, 8N1                            |
| D7       | 7    | GSM Modem RX      | Input     | UART0 RX (alternate pins), 9600 baud, 8N1           |
| D6       | 6    | GSM Modem TX      | Output    | UART0 TX (alternate pins), 9600 baud, 8N1           |
| A0       | 0    | Battery ADC       | Input     | ADC channel 0, voltage divider input                |
| D2       | 2    | TPL5110 DONE      | Output    | Active-HIGH pulse to cut system power               |

### Pin Initialization Sequence (from `setup()`)

```
pinMode(BATT_PIN, INPUT);           // A0 - analog input
pinMode(PIN_DONE, OUTPUT);         // D2 - driven LOW initially
digitalWrite(PIN_DONE, LOW);       // ensure TPL5110 is not triggered

RS485Serial.begin(9600, SERIAL_8N1, D4, D10);   // UART1 for RS-485
GSM_SERIAL.begin(9600, SERIAL_8N1, D7, D6);     // UART0 for GSM modem
```

The ESP32-C3 USB CDC serial (used for debug output) is configured separately and does not consume a GPIO pin. The build flag `ARDUINO_USB_MODE=1` selects USB CDC mode.

---

## 3. Battery Voltage Divider

### Circuit

```
          R1 (100 kohm)
Vbat ----/\/\/\/\----+---- Vpin (A0)
                      |
          R2 (100 kohm)|
GND ----/\/\/\/\----+---- GND
```

### Design Parameters

| Parameter             | Value                    |
|-----------------------|--------------------------|
| R1                    | 100 kohm                 |
| R2                    | 100 kohm                 |
| Divider Ratio         | (R1 + R2) / R2 = 2.0     |
| ADC Pin               | A0 (GPIO 0)              |
| ADC Reference         | Internal, ~2500 mV range |
| Calibration Factor    | 1.000 (no correction)    |

### Measurement Formula

```
Vpin  = analogReadMilliVolts(A0) / 1000.0
Vbat  = Vpin * (R1 + R2) / R2 * calibrationFactor
```

Equivalently: `Vbat = Vpin * 2.0` (for the nominal 1.000 calibration factor).

The function `batteryRead()` in `sensor_v2.cpp` returns battery voltage as an integer in millivolts (`uint16_t`).

### Sampling Method

1. Delay 1000 ms to allow ADC to stabilize.
2. Collect **64 samples** from `analogReadMilliVolts(A0)` with 2 ms inter-sample delay.
3. Compute the arithmetic mean of all samples.
4. Apply the divider ratio and calibration factor.
5. Return the result in millivolts.

With R1 = R2 = 100 kohm, the maximum measurable battery voltage is approximately 5.0 V (ADC saturates at ~2.5 V on the pin). A single-cell Li-ion/Li-Po (3.0--4.2 V) is within range.

---

## 4. RS-485 Bus Wiring

### Bus Configuration

| Parameter         | Value              |
|-------------------|--------------------|
| Physical Layer    | RS-485 half-duplex |
| Baud Rate         | 9600               |
| Data Format       | 8N1 (8 data, no parity, 1 stop) |
| UART Peripheral   | UART1 (`HardwareSerial(1)`)     |
| RX Pin            | D4 (GPIO 4)        |
| TX Pin            | D10 (GPIO 10)      |
| Protocol          | Modbus RTU         |
| Library           | 4-20ma/ModbusMaster v2.0.1 |
| Post-Transmission Delay | 500 us (initial), 1000 us (after recovery) |

### Wiring Diagram

```
  XIAO ESP32-C3                    RS-485 Transceiver               Sensor Bus
  +-----------+                     +--------------+               +---------+
  |        D4 |--- RX ------------>| RO           |               |         |
  |           |                     |           A  |--- A --------| A       |
  |           |                     |    MAX485    |               | Sensor |
  |           |                     |           B  |--- B --------| B       |
  |       D10 |--- TX ------------>| DI           |               |         |
  |           |                     |              |               |         |
  |        5V |--- VCC ----------->| VCC          |               |         |
  |       GND |--- GND ----------->| GND          |               |         |
  +-----------+                     +--------------+               +---------+

  Note: DE/RE tied together and driven HIGH for transmit, LOW for receive.
        A 120 ohm termination resistor is placed across A-B at the bus ends.
```

### Bus Devices

| Device               | Slave Address | Register Start | Register Count |
|----------------------|---------------|----------------|----------------|
| Soil Sensor (7-in-1) | 0x03          | 0x0000         | 7              |
| Weather Station (9-in-1) | 0x01      | 0x01F4 (500)   | 16             |

### Read Strategy

- **readAttempt = 3**: Three successful reads per sensor per cycle.
- **maxRetry = 5**: Maximum failed attempts before giving up on the current cycle.
- **Median filter**: The three readings for each register are sorted and the median value is selected to reject noise.
- **Zero-retry**: If key readings (soil moisture/temperature/EC or air humidity/temperature/CO2) are zero after the first pass, the firmware re-reads up to `maxRetry` additional times.
- **UART recovery**: On error code 0xE0 (invalid slave ID), the UART is fully restarted (`end()` then `begin()` with 500 ms settle delay).

---

## 5. GSM Modem Wiring

### Hardware Interface

| Parameter           | Value                                  |
|---------------------|----------------------------------------|
| Modem               | SIM800 series (via TinyGSM library)    |
| UART Peripheral     | UART0 (`HardwareSerial(0)`)            |
| RX Pin (ESP32)      | D7 (GPIO 7) -- receives from modem TX  |
| TX Pin (ESP32)      | D6 (GPIO 6) -- transmits to modem RX   |
| Baud Rate           | 9600                                   |
| Data Format         | 8N1                                    |
| APN                 | `internet`                             |
| Library             | vshymanskyy/TinyGsm v0.12.0            |

### Wiring Diagram

```
  XIAO ESP32-C3                      SIM800 Module
  +-----------+                      +---------------+
  |        D7 |<--- RX (ESP RX) ---->| TXD           |
  |        D6 |---> TX (ESP TX) ---->| RXD           |
  |        5V |---> VCC (if req'd) ->| VCC           |
  |       GND |--- GND ------------->| GND           |
  |           |                      | PWRKEY (ctrl) |
  +-----------+                      +---------------+
```

### Power Requirements

- SIM800 modules require up to **2 A burst current** during transmit bursts.
- The station power supply must be sized accordingly or the modem must have its own bulk capacitor (typically 1000 uF or more near the module VCC).
- Firmware powers off the modem at end of cycle via `gsmHandler.powerOff()` (issues AT command `AT+CPOWD=1`).

### AT Command Interface

| Function           | AT Command / Method                     |
|--------------------|-----------------------------------------|
| Initialize modem   | `_modem->init()`                        |
| Full functionality | `AT+CFUN=1`                             |
| Network check      | `_modem->isNetworkConnected()`          |
| GPRS connect       | `_modem->gprsConnect("internet", "", "")` |
| Signal quality     | `_modem->getSignalQuality()` (returns CSQ) |
| Network time       | `_modem->getGSMDateTime(DATE_FULL)`     |
| NTP sync           | `AT+CNTP="pool.ntp.org",0`             |
| Software reset     | `AT+CFUN=1,1`                           |
| Power off          | `_modem->poweroff()`                    |
| Email send         | `AT+SMTPSRV`, `AT+SMTPAUTH`, `AT+SMTPSEND` |

### Network Initialization Timeout

| Parameter                  | Value      |
|----------------------------|------------|
| GSM init / network timeout | 120000 ms  |
| NTP sync timeout           | 60000 ms   |
| WDT feed during wait       | Every 2 s  |

---

## 6. Soil Sensor (7-in-1)

### Modbus Configuration

| Parameter       | Value     |
|-----------------|-----------|
| Slave Address   | 0x03      |
| Function Code   | 0x03 (Read Holding Registers) |
| Register Start  | 0x0000    |
| Register Count  | 7         |

### Register Map

| Register | Offset | Field          | Data Type | Scaling          | Unit    |
|----------|--------|----------------|-----------|------------------|---------|
| 0x0000   | [0]    | soil_humi      | uint16_t  | value / 10.0     | %       |
| 0x0001   | [1]    | soil_temp      | int16_t   | value / 10.0     | deg C   |
| 0x0002   | [2]    | soil_ec        | uint16_t  | value (raw)      | uS/cm   |
| 0x0003   | [3]    | soil_ph        | uint16_t  | value / 10.0     | pH      |
| 0x0004   | [4]    | soil_N         | uint16_t  | value (raw)      | mg/kg   |
| 0x0005   | [5]    | soil_P         | uint16_t  | value (raw)      | mg/kg   |
| 0x0006   | [6]    | soil_K         | uint16_t  | value (raw)      | mg/kg   |

### Data Processing

- Three consecutive reads are taken (`readAttempt = 3`).
- For each register, the median of the three values is computed via bubble sort.
- pH is truncated to `uint8_t` (max 255) after median calculation.
- If soil_humi, soil_temp, or soil_ec are zero after the first pass, the firmware retries up to `maxRetry` (5) additional times with 1500 ms delay between retries.

### Struct Storage

Fields stored in `SensorData` struct (packed):

```c
uint16_t soil_humi;    // moisture (% * 10)
int16_t  soil_temp;    // temperature (C * 10)
uint16_t soil_ec;      // electrical conductivity (uS/cm)
uint8_t  soil_ph;      // pH (truncated from median)
uint16_t soil_N;       // nitrogen (mg/kg)
uint16_t soil_P;       // phosphorus (mg/kg)
uint16_t soil_K;       // potassium (mg/kg)
```

### Settle and Timeout

| Parameter              | Value     |
|------------------------|-----------|
| Settle delay (power-on)| 15000 ms  |
| Read timeout           | 30000 ms  |

---

## 7. Weather Station (9-in-1)

### Modbus Configuration

| Parameter       | Value           |
|-----------------|-----------------|
| Slave Address   | 0x01            |
| Function Code   | 0x03 (Read Holding Registers) |
| Register Start  | 0x01F4 (500)    |
| Register Count  | 16              |

### Register Map

| Register | Offset | Field            | Data Type | Scaling          | Unit   |
|----------|--------|------------------|-----------|------------------|--------|
| 0x01F4   | [0]    | windSpeed        | uint16_t  | value / 10.0     | m/s    |
| 0x01F5   | [1]    | (reserved)       | uint16_t  | --               | --     |
| 0x01F6   | [2]    | (reserved)       | uint16_t  | --               | --     |
| 0x01F7   | [3]    | windDir_Deg      | uint16_t  | value (raw)      | degrees|
| 0x01F8   | [4]    | air_humidity     | uint16_t  | value / 10.0     | %      |
| 0x01F9   | [5]    | air_temperature  | int16_t   | value / 10.0     | deg C  |
| 0x01FA   | [6]    | (reserved)       | uint16_t  | --               | --     |
| 0x01FB   | [7]    | CO2              | uint16_t  | value (raw)      | ppm    |
| 0x01FC   | [8]    | (reserved)       | uint16_t  | --               | --     |
| 0x01FD   | [9]    | pressure         | uint16_t  | value / 10.0     | kPa    |
| 0x01FE   | [10]   | illuminance (hi) | uint16_t  | hi << 16 \| lo   | lux    |
| 0x01FF   | [11]   | illuminance (lo) | uint16_t  | (32-bit value)   | lux    |
| 0x0200   | [12]   | (reserved)       | uint16_t  | --               | --     |
| 0x0201   | [13]   | rainfall         | uint16_t  | value / 10.0     | mm     |
| 0x0202   | [14]   | (reserved)       | uint16_t  | --               | --     |
| 0x0203   | [15]   | solar            | uint16_t  | value (raw)      | W/m2   |

### Data Processing

- Three consecutive reads are taken (`readAttempt = 3`).
- For each register, the median of the three values is computed.
- **Illuminance** is a 32-bit value composed from two 16-bit registers: `(raw[10] << 16) | raw[11]`. A dedicated `getMedian32()` function handles the 32-bit median filter.
- If air_humidity, air_temperature, or CO2 are zero after the first pass, the firmware retries up to `maxRetry` (5) additional times with 1500 ms delay.

### Struct Storage

Fields stored in `SensorData` struct (packed):

```c
uint16_t windSpeed;        // wind speed (m/s * 10)
uint16_t windDir_Deg;      // wind direction (degrees, raw)
uint16_t air_humidity;     // humidity (% * 10)
int16_t  air_temperature;  // temperature (C * 10)
uint16_t CO2;              // CO2 concentration (ppm, raw)
uint16_t pressure;         // atmospheric pressure (kPa * 10)
uint32_t illuminance;      // light intensity (lux, raw)
uint16_t rainfall;         // rainfall (mm * 10)
uint16_t solar;            // solar radiation (W/m2, raw)
```

### Settle and Timeout

| Parameter              | Value     |
|------------------------|-----------|
| Settle delay (power-on)| 15000 ms  |
| Read timeout           | 30000 ms  |

---

## 8. TPL5110 Power Timer

### Overview

The TPL5110 is a nano-power timer that periodically enables the system power supply. It replaces a continuous-power design with a duty-cycled approach, significantly reducing average current consumption. The firmware signals completion by driving the DONE pin, causing the TPL5110 to cut power.

### Pin Connection

| Signal   | ESP32 Pin | Direction | Active State |
|----------|-----------|-----------|--------------|
| DONE     | D2        | Output    | HIGH pulse   |

### DONE Pin Sequence

The following sequence is executed in the `STATE_FINISH` state:

```
1. Print "[DONE] Pulsing TPL5110" to debug serial
2. Wait 3000 ms (flush pending serial output)
3. Serial.flush()
4. Drive PIN_DONE LOW  -- ensure known starting state
5. Wait 50 ms
6. Drive PIN_DONE HIGH -- signal TPL5110 to cut power
7. Wait 2000 ms
8. If still running: print "[WARN] TPL5110 did not cut power"
9. Enter infinite loop (feed WDT, delay 1 s) -- safety fallback
```

### Timing Diagram

```
                     DONE Pulse Sequence
                     _______________________
PIN_DONE (D2)  _____|                       |_______
                t0   50ms   t1   2000ms   t2
                     |<--- HIGH --->|
                     ^              ^
                  DONE assert    TPL5110
                                 cuts VCC
```

### TPL5110 Behavior

- The TPL5110 wake interval is set by an external resistor on its DELAY pin (not configurable in firmware).
- On each wake, the TPL5110 drives its DRV pin HIGH, enabling the system voltage regulator.
- When the ESP32-C3 asserts DONE (HIGH), the TPL5110 responds by:
  1. Driving DRV LOW (disables the regulator).
  2. Entering its low-power timing state until the next interval.
- If the DONE signal is not received within the TPL5110's internal timeout (approximately 2 hours default), the TPL5110 forces a power cycle.

---

## 9. Power Supply Architecture

### Power Path

```
                         Power Supply Architecture

  Battery                TPL5110                  Voltage              Load
  (Li-Ion/Li-Po)         Timer                    Regulator

  +---------+            +-----------+            +----------+
  |         |            |           |            |          |---> ESP32-C3 (3.3V logic)
  |  3.0-   |--- Vbat ---| DRV  OUT  |--- EN --->| LDO or   |---> RS-485 Transceiver
  |  4.2V   |    |       |           |            | Buck     |      (VCC = 3.3V or 5V)
  |         |    |       |           |            | 3.3V     |---> SIM800 (3.4-4.4V, *
  |         |    |       |           |            |          |      separate reg)
  +---------+    |       +-----------+            +----------+
                 |            ^
                 |            | DONE
                 |            | (D2)
                 |       +----+----+
                 |       |  ESP32  |
                 |       |  -C3    |
                 |       +---------+
                 |
                 +---> Voltage Divider (R1=100k, R2=100k) ---> A0 (ADC)
```

### Rail Description

| Rail            | Source                        | Voltage Range    | Consumers                   |
|-----------------|-------------------------------|------------------|-----------------------------|
| Vbat            | Battery direct                | 3.0 -- 4.2 V     | ADC divider, TPL5110 VCC    |
| Vsys (regulated)| LDO/Buck, enabled by TPL5110 | 3.3 V            | ESP32-C3, RS-485 transceiver|
| Vmodem          | Separate regulator or Vbat    | 3.4 -- 4.4 V     | SIM800 module               |

### Current Budget (Typical)

| Component            | Active Current | Sleep/Idle Current |
|----------------------|----------------|--------------------|
| ESP32-C3 (160 MHz)   | ~20--50 mA     | N/A (power-cycled) |
| RS-485 Transceiver   | ~20--40 mA     | N/A (power-cycled) |
| SIM800 (transmit)    | ~200--2000 mA  | N/A (power-cycled) |
| TPL5110              | N/A (always on)| ~35 nA             |
| Voltage Divider (A0) | ~15--21 uA     | ~15--21 uA (always)|

The TPL5110 ensures the system is fully powered down between measurement cycles. The only continuous current draw is the TPL5110 quiescent current and the voltage divider leakage through R1+R2 (~15--21 uA at 3.0--4.2 V).

---

## 10. Debug Port

### Configuration

| Parameter          | Value                     |
|--------------------|---------------------------|
| Physical Interface | USB-C (CDC ACM virtual serial) |
| Baud Rate          | 115200                    |
| Data Format        | 8N1 (default)             |
| Build Flag         | `ARDUINO_USB_MODE=1`      |
| Debug Level        | `CORE_DEBUG_LEVEL=0` (none -- no ESP-IDF log output) |
| Application Debug  | `DEBUG = 1` (firmware-level `Serial.printf` statements) |

### Build Flags (complete list from `platformio.ini`)

| Flag                        | Purpose                                     |
|-----------------------------|---------------------------------------------|
| `-DCORE_DEBUG_LEVEL=0`      | Suppress ESP-IDF framework debug output     |
| `-DARDUINO_USB_MODE=1`      | Select USB CDC serial (not hardware UART0)  |
| `-DCONFIG_BT_NIMBLE_ENABLED=1` | Enable NimBLE Bluetooth LE stack        |

### Serial Usage Summary

| Peripheral    | UART     | Pins       | Baud   | Purpose                      |
|---------------|----------|------------|--------|------------------------------|
| USB CDC       | USB      | (USB-C)    | 115200 | Debug output, firmware upload|
| RS-485 Bus    | UART1    | D4, D10    | 9600   | Modbus RTU sensor comms      |
| GSM Modem     | UART0    | D7, D6     | 9600   | SIM800 AT command interface  |

Note: UART0 is remapped to pins D7/D6 for the GSM modem. The USB CDC serial is a separate endpoint and does not conflict with UART0's GPIO remap.

### Debug Output Format

All debug messages are prefixed with a tag identifying the subsystem:

| Prefix       | Subsystem                    |
|--------------|------------------------------|
| `[FW]`       | Firmware version / boot      |
| `[WDT]`      | Watchdog timer               |
| `[SERIAL]`   | UART initialization          |
| `[BATT]`     | Battery voltage reading      |
| `[MODBUS]`   | Modbus communication errors  |
| `[ERROR]`    | Critical failures            |
| `[WARN]`     | Non-critical warnings        |
| `[GSM]`      | GSM modem / GPRS             |
| `[NTP]`      | Time synchronization         |
| `[MQTT]`     | MQTT publish / connect       |
| `[INFLUX]`   | InfluxDB HTTP writes         |
| `[OTA]`      | Over-the-air updates         |
| `[BLE]`      | Bluetooth LE scan/connect    |
| `[BLE-CSV]`  | BLE data CSV storage         |
| `[SAVE]`     | Data persistence             |
| `[FS]`       | LittleFS operations          |
| `[DONE]`     | TPL5110 power-off sequence   |
| `[TMO]`      | State machine timeout        |
| `[WiFi]`     | WiFi AP operations           |

---

*Document generated from firmware source code analysis. All register addresses, pin assignments, and constants verified against `utilities.h`, `sensor_v2.cpp`, `sensor_v2.h`, `main_1.cpp`, `GsmHandler.cpp`, `platformio.ini`, and `partitions_ota_4mb.csv`.*
