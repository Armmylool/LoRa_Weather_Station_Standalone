# Hardware Reference

> Module 2 of 6 -- Srisaket Weather Station Embedded Technical Manual
> IDEA Laboratory @ KMUTT

This document provides the complete hardware specification for the Srisaket
Weather Station firmware, covering the MCU, pin assignments, peripheral wiring,
sensor interfaces, and power architecture. All values are sourced from the
project configuration files (`include/utilities.h`, `platformio.ini`) and
verified against the runtime code.

---

## 1. MCU Specifications

| Parameter     | Value                                |
|---------------|--------------------------------------|
| MCU           | ESP32-C3 (RISC-V, single-core)      |
| Clock         | 160 MHz                              |
| Flash         | 4 MB (OTA partition scheme)          |
| Framework     | Arduino (PlatformIO)                 |
| Board         | Seeed XIAO ESP32-C3                  |
| Partition     | `partitions_ota_4mb.csv` (OTA-enabled) |
| Debug         | USB CDC, 115200 baud                 |
| WDT           | 45-second hardware watchdog          |

**Source references:**

- `platformio.ini`: `board = seeed_xiao_esp32c3`, `board_build.f_cpu = 160000000L`
- `include/utilities.h`: `SERIAL_BAUDRATE 115200`, `WDT_TIMEOUT_SEC = 45`

---

## 2. Pin Assignment Table

| Pin | Function        | Direction | Notes                              |
|-----|-----------------|-----------|--------------------------------------|
| D4  | RS-485 RX       | Input     | UART1, 9600 baud, 8N1              |
| D10 | RS-485 TX       | Output    | UART1, 9600 baud, 8N1              |
| D7  | GSM RX          | Input     | UART0, 9600 baud, 8N1              |
| D6  | GSM TX          | Output    | UART0, 9600 baud, 8N1              |
| A0  | Battery ADC     | Input     | Voltage divider midpoint            |
| D2  | TPL5110 DONE    | Output    | LOW-to-HIGH pulse to cut power      |

**Source references:**

- RS-485: `RS_485_RX_PIN D4`, `RS_485_TX_PIN D10`
- GSM: `GSM_RX_PIN D7`, `GSM_TX_PIN D6`
- Battery: `BATT_PIN A0`
- TPL5110: `PIN_DONE D2`

---

## 3. Battery Voltage Divider

The battery voltage is measured through a resistor divider connected to the
ADC input pin A0.

### Circuit Description

```
Vbat ──── R1 (100 kohm) ────┬──── R2 (100 kohm) ──── GND
                              |
                             A0 (ADC input)
```

### Component Values

| Component | Value       | Source constant |
|-----------|-------------|-----------------|
| R1        | 100 kOhm    | `R1 = 100000`   |
| R2        | 100 kOhm    | `R2 = 100000`   |

### Calculation

```
V_battery = V_pin x (R1 + R2) / R2 = V_pin x 2
```

With equal 100 kOhm resistors, the divider ratio is exactly 2.0, so the
measured voltage at A0 is half the actual battery voltage.

### Software Behavior

- The ADC reads 64 samples, each via `analogReadMilliVolts(A0)`, with a
  2 ms delay between samples.
- Samples are summed and averaged to reduce noise.
- The result is converted to millivolts and returned as `uint16_t`.

```c
// From sensor_v2.cpp -- batteryRead()
uint32_t sumMilliVolts = 0;
const int numSamples = 64;
for (int i = 0; i < numSamples; i++) {
    sumMilliVolts += analogReadMilliVolts(A0);
    delay(2);
}
float pinVoltage = (sumMilliVolts / (float)numSamples) / 1000.0;
float dividerRatio = (float)(R1 + R2) / (float)R2;
float batteryVoltage = pinVoltage * dividerRatio;
uint16_t batteryMilliVolts = (uint16_t)(batteryVoltage * 1000.0);
```

---

## 4. RS-485 Bus Wiring

The RS-485 bus provides a half-duplex Modbus RTU interface to the soil and
weather sensors.

### Electrical Connection

```
ESP32-C3                              RS-485 Transceiver
D10 (TX) ──────────────────────────►  DI (Data In)
D4  (RX) ◄──────────────────────────  RO (Data Out)
                                          │
                                     DE/RE (tied together)
                                          │
                                      A ────── Bus A ──┬── Sensor 1 (0x03)
                                      B ────── Bus B ──┤
                                                        └── Sensor 2 (0x01)
```

### Configuration

| Parameter   | Value                    |
|-------------|--------------------------|
| UART        | UART1 (`HardwareSerial(1)`) |
| Baud rate   | 9600                     |
| Data bits   | 8                        |
| Parity      | None                     |
| Stop bits   | 1                        |
| Protocol    | Modbus RTU               |
| Max devices | 2 (soil + weather)       |

### Software Notes

- The `ModbusMaster` library handles Driver Enable (DE) and Receiver Enable
  (RE) via pre/post transmission callbacks.
- A 500 microsecond post-transmission delay is applied after each frame to
  allow the bus to settle.
- On communication errors (especially `0xE0` -- Invalid Slave ID), the UART
  is fully reinitialized (end, begin, flush) as a recovery mechanism.
- The bus reads 3 samples per sensor (median filtering) with up to 5 retries
  on failure.

### Recommended Practice

- Install a 120 Ohm termination resistor at each end of the bus to minimize
  signal reflections.

---

## 5. GSM Modem Wiring

The GSM modem provides cellular connectivity for MQTT data publishing, NTP
time synchronization, and remote OTA firmware updates.

### Connection

```
ESP32-C3                     SIM800 Modem
D6 (TX) ──────────────────►  RX
D7 (RX) ◄──────────────────  TX
```

### Configuration

| Parameter   | Value                    |
|-------------|--------------------------|
| UART        | UART0 (`HardwareSerial(0)`) |
| Baud rate   | 9600                     |
| Data bits   | 8                        |
| Parity      | None                     |
| Stop bits   | 1                        |
| Library     | TinyGSM (SIM800 variant) |
| APN         | `internet`               |

### Power Requirements

The SIM800 modem requires a stable power supply capable of delivering peak
currents up to 2 A during transmission bursts. Insufficient power will cause
the modem to reset or fail during network operations.

### Software Notes

- The GSM serial port is assigned to `GSM_SERIAL` (`HardwareSerial(0)`) and
  shared with the modem object.
- GSM initialization has a 120-second timeout (`GSM_INIT_TIMEOUT_MS`).
- The AT command interface is managed by the TinyGSM library, which abstracts
  modem operations (network registration, GPRS connect, TCP/IP, HTTP, NTP).

---

## 6. Soil Sensor (7-in-1)

A combined soil sensor connected via Modbus RTU over the RS-485 bus. Measures
seven soil parameters in a single read operation.

### Modbus Configuration

| Parameter        | Value          |
|------------------|----------------|
| Interface        | Modbus RTU, RS-485 |
| Slave ID         | `0x03`         |
| Register start   | `0x0000`       |
| Register count   | 7              |
| Function code    | 0x03 (Read Holding Registers) |

### Register Map

| Register Offset | Measurement        | Unit    | Encoding          | C type    |
|-----------------|--------------------|---------|-------------------|-----------|
| 0               | Soil Moisture      | %       | Value x 10        | `uint16_t` |
| 1               | Soil Temperature   | deg C   | Value x 10        | `int16_t`  |
| 2               | EC (Conductivity)  | uS/cm   | Direct integer    | `uint16_t` |
| 3               | pH                 | -       | Value x 10        | `uint8_t`  |
| 4               | Nitrogen (N)       | mg/kg   | Direct integer    | `uint16_t` |
| 5               | Phosphorus (P)     | mg/kg   | Direct integer    | `uint16_t` |
| 6               | Potassium (K)      | mg/kg   | Direct integer    | `uint16_t` |

### Data Structure

```c
// From sensor_v2.h -- SensorData struct (soil fields)
uint16_t soil_humi;      // Moisture (% x 10)
int16_t  soil_temp;      // Temperature (deg C x 10)
uint16_t soil_ec;        // EC (uS/cm, direct)
uint8_t  soil_ph;        // pH (x 10, clamped to 255)
uint16_t soil_N;         // Nitrogen (mg/kg, direct)
uint16_t soil_P;         // Phosphorus (mg/kg, direct)
uint16_t soil_K;         // Potassium (mg/kg, direct)
```

### Software Notes

- Each read cycle takes 3 successful samples and applies median filtering.
- If key values (moisture, temperature, EC) read as zero, the firmware
  retries up to `maxRetry` (5) additional times before accepting the reading.
- pH values are clamped to a maximum of 255 before being cast to `uint8_t`.
- After 3 consecutive total read failures, the sensor section is zeroed to
  prevent stale data from persisting.

---

## 7. Weather Station (9-in-1)

A combined weather sensor connected via Modbus RTU over the RS-485 bus.
Measures nine atmospheric parameters.

### Modbus Configuration

| Parameter        | Value            |
|------------------|------------------|
| Interface        | Modbus RTU, RS-485 |
| Slave ID         | `0x01`           |
| Register start   | `0x01F4` (500)   |
| Register count   | 16               |
| Function code    | 0x03 (Read Holding Registers) |

### Register Map

| Register Offset | Measurement       | Unit   | Encoding          | C type     |
|-----------------|-------------------|--------|-------------------|------------|
| 0               | Wind Speed        | m/s    | Value x 10        | `uint16_t`  |
| 3               | Wind Direction    | deg    | Direct integer    | `uint16_t`  |
| 4               | Air Humidity      | %      | Value x 10        | `uint16_t`  |
| 5               | Air Temperature   | deg C  | Value x 10        | `int16_t`   |
| 7               | CO2               | ppm    | Direct integer    | `uint16_t`  |
| 9               | Pressure          | kPa    | Value x 10        | `uint16_t`  |
| 10-11           | Illuminance       | lux    | 32-bit (2 regs)   | `uint32_t`  |
| 13              | Rainfall          | mm     | Value x 10        | `uint16_t`  |
| 15              | Solar Radiation   | W/m2   | Direct integer    | `uint16_t`  |

> **Note:** Illuminance spans two 16-bit registers (offsets 10 and 11),
> combined as `(reg[10] << 16) | reg[11]` to form a 32-bit value. This is
> the only field requiring a 32-bit median filter.

### Data Structure

```c
// From sensor_v2.h -- SensorData struct (weather fields)
uint16_t windSpeed;       // Wind Speed (m/s x 10)
uint16_t windDir_Deg;     // Wind Direction (deg, direct)
uint16_t air_humidity;    // Air Humidity (% x 10)
int16_t  air_temperature; // Air Temperature (deg C x 10)
uint16_t CO2;             // CO2 (ppm, direct)
uint16_t pressure;        // Pressure (kPa x 10)
uint32_t illuminance;     // Illuminance (lux, 32-bit direct)
uint16_t rainfall;        // Rainfall (mm x 10)
uint16_t solar;           // Solar Radiation (W/m2, direct)
```

### Software Notes

- The same median-of-3 read strategy is used as with the soil sensor.
- If key values (humidity, temperature, CO2) read as zero, the firmware
  retries up to 5 additional times.
- Illuminance uses a dedicated 32-bit median function (`getMedian32`) due to
  its two-register encoding.
- Not all 16 registers are mapped to sensor fields; some register offsets
  (1, 2, 6, 8, 12, 14) are skipped or reserved by the sensor manufacturer.

---

## 8. TPL5110 Power Timer

The TPL5110 is a nano-power system timer that controls power to the entire
system. The ESP32-C3 signals task completion via the DONE pin, and the TPL5110
cuts power until the next wake interval.

### Pin Configuration

| Parameter  | Value     |
|------------|-----------|
| DONE pin   | D2 (GPIO) |
| Direction  | Output    |
| Idle state | LOW       |

### DONE Pulse Sequence

The firmware sends the DONE signal in `STATE_FINISH` at the end of each
measurement cycle:

```
1. Set D2 LOW   -- ensure clean start
2. Wait 50 ms   -- minimum pulse width
3. Set D2 HIGH  -- signal "work complete" to TPL5110
4. Hold 2 s     -- wait for TPL5110 to cut power
```

```c
// From main_1.cpp -- STATE_FINISH
digitalWrite(PIN_DONE, LOW);   delay(50);
digitalWrite(PIN_DONE, HIGH);  delay(2000);
// If execution reaches here, TPL5110 did not cut power
```

### Wake Interval

The TPL5110 wake interval is configured by an external resistor on the TPL5110
board and is **not** software-configurable. The firmware has no control over
the duty cycle; it must complete all work and pulse DONE within the watchdog
timeout (45 seconds).

### Fallback Behavior

If the TPL5110 fails to cut power (hardware fault), the firmware enters an
infinite loop that continuously feeds the watchdog to prevent a reset while
waiting for the TPL5110 to act:

```c
Serial.println("[WARN] TPL5110 did not cut power");
while (1) { feedWDT(); delay(1000); }
```

---

## 9. Power Supply Architecture

```
Battery ──► Voltage Regulator ──► 3.3V Rail
                                      +-- ESP32-C3
                                      +-- RS-485 Transceiver
                                      +-- SIM800 Modem (via dedicated regulator)
                                      +-- TPL5110 (controls power to entire system)

TPL5110 DRV pin ──► Enable regulator output
TPL5110 DONE  <── ESP32-C3 D2 (signals "work complete")
```

### Power Flow

1. The TPL5110 drives the voltage regulator enable (DRV pin) at the start of
   each wake interval.
2. The 3.3 V rail powers the ESP32-C3, RS-485 transceiver, and associated
   circuitry.
3. The SIM800 GSM modem is powered through a separate high-current regulator
   (peak 2 A during TX).
4. When the firmware has completed all tasks, it pulses the DONE pin (D2).
5. The TPL5110 de-asserts DRV, disabling the voltage regulator and cutting
   power to the entire system.
6. The TPL5110 remains in an ultra-low-power state until the next wake
   interval, which is determined by its external timing resistor.

### Key Constraints

- The entire measurement cycle (GSM init, NTP sync, sensor reads, data
  publish, OTA check) must complete within the 45-second hardware watchdog
  timeout.
- Battery voltage is measured at startup to monitor power supply health.

---

## 10. Debug Port

The debug console uses the USB CDC (Communications Device Class) serial
interface, which is enabled by the `ARDUINO_USB_MODE=1` build flag.

### Configuration

| Parameter     | Value        |
|---------------|--------------|
| Interface     | USB CDC      |
| Baud rate     | 115200       |
| Data bits     | 8            |
| Parity        | None         |
| Stop bits     | 1            |

### Build Flags

```ini
; From platformio.ini
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DARDUINO_USB_MODE=1
```

- `CORE_DEBUG_LEVEL=0`: Disables the ESP-IDF verbose debug output so that
  only application-level messages appear on the serial console.
- `ARDUINO_USB_MODE=1`: Configures the USB peripheral in CDC mode for serial
  communication.
- `DEBUG=1`: Application-level compile flag (defined in `utilities.h`) that
  enables `Serial.printf` debug output throughout the firmware. Set to `0` to
  suppress debug messages and reduce serial output overhead.

### Usage

Connect the Seeed XIAO ESP32-C3 to a computer via USB. Open a serial terminal
(e.g., PlatformIO Serial Monitor, `minicom`, or PuTTY) at 115200 baud. Debug
messages use the `Serial.printf` format with function name prefixes for
traceability:

```
[FW] All-in-One Weather Station v2.3.5
[WDT] Watchdog initialized
[SERIAL] RS485 UART1 started
[SERIAL] GSM UART0 started
[FS] LittleFS mounted
[BATT] 4125 mV
===== SETUP COMPLETE =====
```
