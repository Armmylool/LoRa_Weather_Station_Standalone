---
title: Embedded Technical Manual — Design Specification
date: 2026-05-26
project: LoRa Weather Base — Srisaket Version
firmware: 2.3.5
---

# Design Specification: Embedded Technical Manual

## Purpose

Create a modular technical manual for embedded engineers working on the Srisaket Weather Station firmware. The documentation covers the full system from hardware pins to cloud protocols.

## Target Audience

Embedded engineers joining the project or maintaining deployed units. Assumes familiarity with ESP32, UART, and embedded C++ but no prior knowledge of this specific codebase.

## Approach

Role-based modular documents organized by what an engineer needs to do. Each module is self-contained but cross-references others where needed.

## Document Set

All files live in `docs/` at the project root.

### Module 1: System Overview (`01-system-overview.md`)

- Project introduction and purpose
- Firmware version (2.3.5) and build date
- Architecture block diagram (ASCII art)
- State machine flow with transitions and timeouts
- Module dependency graph
- Boot sequence step-by-step
- Duty cycle / TPL5110 power lifecycle

Estimated length: ~2 pages.

### Module 2: Hardware Reference (`02-hardware-reference.md`)

- MCU specs (ESP32-C3, RISC-V, 160 MHz, 4 MB Flash, OTA partition)
- Complete pin assignment table
- Battery voltage divider circuit (R1=100k, R2=100k, formula)
- RS-485 bus wiring (half-duplex, termination, sensor connection)
- GSM modem wiring (SIM800 UART, power)
- Soil sensor 7-in-1 (Slave 0x03) hardware specs
- Weather station 9-in-1 (Slave 0x01) hardware specs
- TPL5110 power timer DONE pin behavior
- Power supply architecture
- Debug port (USB CDC, 115200 baud)

Estimated length: ~3 pages.

### Module 3: Software Architecture (`03-software-architecture.md`)

- File/module map with responsibilities
- State machine detail (each state: purpose, timeout, entry/exit, fallback)
- Data structures (SensorData, timeStruct, DataRecord, SystemStatus, BLEConnectedDevice)
- Data flow diagram (sensor read → CSV → MQTT/InfluxDB)
- Sensor reading pipeline (median filtering, retry logic, zero-value recovery, 0xE0 recovery)
- Time management (GSM RTC/NTP primary, CSV fallback, BLE NTP)
- Watchdog management (45s WDT feeding across operations)
- NVS Preferences keys table

Estimated length: ~4 pages.

### Module 4: Communication Protocols (`04-communication-protocols.md`)

- Modbus RTU: frame format, register maps (soil Slave 0x03 addr 0x0000 len 7; weather Slave 0x01 addr 0x01F4 len 16)
- MQTT: broker config, topics, JSON payload format (sensor data + heartbeat)
- InfluxDB v2 HTTP: line protocol format, measurements, tags, fields, auth
- BLE NUS: UUIDs, JSON notification payload fields
- BLE GATT: service discovery and characteristic read flow
- SMTP: SIM800 AT command sequence
- NTP: +CNTP AT command flow
- OTA HTTP: request headers, response codes (200/304/401), MD5 verification
- WiFi AP HTTP API: endpoint table with methods, params, responses

Estimated length: ~5 pages.

### Module 5: Configuration & Deployment (`05-configuration-deployment.md`)

- First-time provisioning steps
- NVS configuration keys table (all ws-cfg keys, types, defaults)
- Web portal configuration walkthrough
- OTA update process and server requirements
- MQTT broker setup requirements
- InfluxDB v2 bucket/org/token requirements
- Email SMTP server requirements
- LittleFS file system structure
- CSV file naming conventions and formats

Estimated length: ~3 pages.

### Module 6: Troubleshooting & Maintenance (`06-troubleshooting-maintenance.md`)

- Common issues and solutions table
- Debug serial console usage and CORE_DEBUG_LEVEL
- Event log format and reading event CSV files
- Storage management (free space check, cleanup, rollover threshold)
- Web portal monitoring (/live dashboard, /log viewer)
- Watchdog reset analysis
- Factory reset procedure
- Known limitations and edge cases

Estimated length: ~2 pages.

## Content Sources

All content derived from actual source code analysis:
- `platformio.ini` — build configuration
- `include/utilities.h` — pins, constants, defaults
- `src/main_1.cpp` — state machine, BLE, InfluxDB, OTA
- `src/sensor_v2.cpp` / `include/sensor_v2.h` — Modbus sensor interface
- `src/GsmHandler.cpp` / `include/GsmHandler.h` — GSM modem handler
- `src/Memory.cpp` / `include/Memory.h` — LittleFS CSV storage
- `src/WifiApServer.cpp` / `include/WifiApServer.h` — WiFi AP web portal

## Out of Scope

- PCB schematic or layout documentation (hardware team responsibility)
- Server-side infrastructure documentation (backend team responsibility)
- Mobile app documentation
- End-user operation manual
