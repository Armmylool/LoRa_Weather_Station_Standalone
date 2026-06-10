/* PAMIANG RX Board v0.3.0 (LoRa Receiver + BLE Relay)
 *
 * Architecture:
 *   E32-433T30S (LoRa UART) ─[UART1]─> ESP32-C3 (parse LoRaDataPacket)
 *                                              │
 *                                              ├─> LittleFS CSV (local backup)
 *                                              └─> BLE NUS ─> LILYGO SIM800L ─> MQTT
 *
 * The E32-433T30S provides LoRa UART data. After successful
 * LoRa packet reception, data is forwarded to the LILYGO TTGO SIM800L via
 * BLE Nordic UART Service (NUS) using the UARTPacket wire format.
 *
 * State machine:
 *   STATE_LORA_INIT  -> STATE_BLE_INIT -> STATE_LORA_RX
 *   STATE_LORA_RX    (collects packets for a window, up to PUBLISH_MAX_RECORDS)
 *       -> STATE_SAVE -> STATE_LORA_RX  (loop)
 *       -> STATE_FINISH  (TPL5110 timeout or fatal error)
 *
 * BLE runs asynchronously via BleNusClient state machine.
 */
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <stddef.h>

#include "utilities_rx.h"
#include "sensor_v2.h"
#include "Memory.h"
#include "LoRaE32Handler.h"
#include "LoRaPacket.h"
#include "BleNusClient.h"
#include "BlePacketTypes.h"

/* ===== STATE MACHINE ===== */
typedef enum {
    STATE_LORA_INIT,
    STATE_BLE_INIT,
    STATE_LORA_RX,
    STATE_SAVE,
    STATE_FINISH,
} systemState;

/* ===== GLOBAL OBJECTS ===== */
LoRaE32Handler  loraHandler;
Memory          internalMemory;
Preferences     nvs;
HardwareSerial  LORA_SERIAL(1);
BleNusClient    bleClient;
systemState     currentState = STATE_LORA_INIT;

/* ===== GLOBAL VARIABLES ===== */
static bool     loraAvailable  = false;
static uint16_t batteryVoltage = 0;
static unsigned long stateEntryTime = 0;
static systemState   prevState      = STATE_LORA_RX;

static char dailyCsv[24] = "/DATA.csv";
static const char* temporarydata = "/DATA_TEMP.csv";

static DataRecord  rxRecords[PUBLISH_MAX_RECORDS];
static LoRaDataPacket rxPackets[PUBLISH_MAX_RECORDS];
static uint8_t     rxRecordCount = 0;
static unsigned long rxWindowStart = 0;

/* RX window: collect packets for this long before saving/publishing */
static constexpr unsigned long RX_WINDOW_MS        = 10000UL;
static constexpr unsigned long RX_POLL_INTERVAL_MS = 1000UL;

/* ===== SEND ACK ===== */
static void sendLoRaAck(uint16_t seq) {
    LoRaAckPacket ack;
    ack.magic = LORA_MAGIC;
    ack.type  = LORA_PKT_ACK;
    ack.seq   = seq;

    delay(LORA_ACK_RX_SETTLE);
    LORA_SERIAL.write((const uint8_t*)&ack, sizeof(ack));
    LORA_SERIAL.flush();
    Serial.printf("[LoRa-RX] Sent ACK for seq=%u (%u bytes)\n", seq, sizeof(ack));
}

/* ===== BATTERY READ ===== */
static uint16_t batteryReadRx() {
    uint16_t adc = analogRead(BATT_PIN_RX);
    uint32_t mv = (uint32_t)adc * 3300 * (R1_RX + R2_RX) / (4095 * R2_RX);
    return (uint16_t)mv;
}

/* ===== HELPERS ===== */
static void feedWDT() { esp_task_wdt_reset(); }

static void copyText(char* dst, size_t dstSize, const char* src) {
    if (dstSize == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static void printReceivedDataPacket(const LoRaDataPacket& pkt) {
    BleSensorData emptyBle = {0, 0, 0, 0, 0, 0, 0};
    const BleSensorData& ble = pkt.ble_valid ? pkt.ble : emptyBle;

    char dateStr[12];
    char timeStr[9];
    uint16_t fullYear = (uint16_t)pkt.year + 2000;
    snprintf(dateStr, sizeof(dateStr), "%02u/%02u/%04u",
             pkt.date, pkt.month, fullYear);
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u",
             pkt.hour, pkt.minute, 0);

    Serial.println(F("[LoRa-RX] Received data (CSV format):"));
    Serial.println(F("Date,Time,Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,WindSpeed,WindDirection,Air_Humidity,Air_Temperature,CO2,Pressure,Illuminance,Rainfall,Solar,BLE_Temp,BLE_Humi,BLE_TMP117,BLE_Rain,BLE_Leaf,BLE_PAR,BLE_Soil"));
    Serial.printf(
        "%s,%s,"
        "%.1f,%.1f,%u,%.1f,%u,%u,%u,"
        "%.1f,%u,%.1f,%.1f,%u,%.1f,%lu,%.1f,%u,"
        "%.1f,%.1f,%.1f,%u,%u,%u,%u\n",
        dateStr, timeStr,
        (double)pkt.sensor.soil_humi / 10.0,
        (double)pkt.sensor.soil_temp / 10.0,
        pkt.sensor.soil_ec,
        (double)pkt.sensor.soil_ph / 10.0,
        pkt.sensor.soil_N,
        pkt.sensor.soil_P,
        pkt.sensor.soil_K,
        (double)pkt.sensor.windSpeed / 10.0,
        pkt.sensor.windDir_Deg,
        (double)pkt.sensor.air_humidity / 10.0,
        (double)pkt.sensor.air_temperature / 10.0,
        pkt.sensor.CO2,
        (double)pkt.sensor.pressure / 10.0,
        (unsigned long)pkt.sensor.illuminance,
        (double)pkt.sensor.rainfall / 10.0,
        pkt.sensor.solar,
        (double)ble.ble_temp / 10.0,
        (double)ble.ble_humi / 10.0,
        (double)ble.ble_tmp117 / 10.0,
        ble.ble_rain,
        ble.ble_leaf,
        ble.ble_par,
        ble.ble_soil);
}

/* ===== LORA PACKET PARSER =====
 * Reads bytes from LoRa UART, detects packet boundaries by magic byte 0xA5,
 * then reads the full struct based on packet type.
 */
static bool rxPacketAvailable() {
    return LORA_SERIAL.available() >= 1;
}

static bool readLoRaPacket(LoRaDataPacket& out) {
    while (LORA_SERIAL.available()) {
        uint8_t b = (uint8_t)LORA_SERIAL.read();
        if (b == LORA_MAGIC) {
            unsigned long t0 = millis();
            while (!LORA_SERIAL.available() && millis() - t0 < 500) { feedWDT(); delay(1); }
            if (!LORA_SERIAL.available()) return false;

            uint8_t pktType = (uint8_t)LORA_SERIAL.read();

            if (pktType == LORA_PKT_HEARTBEAT) {
                uint8_t buf[sizeof(LoRaHeartbeat) - 2];
                size_t need = sizeof(buf);
                size_t got = 0;
                t0 = millis();
                while (got < need && millis() - t0 < 1000) {
                    if (LORA_SERIAL.available()) {
                        buf[got++] = (uint8_t)LORA_SERIAL.read();
                    } else {
                        feedWDT(); delay(1);
                    }
                }
                if (got < need) {
                    Serial.println(F("[LoRa-RX] Heartbeat incomplete"));
                    return false;
                }

                LoRaHeartbeat hb;
                hb.hdr.magic = LORA_MAGIC;
                hb.hdr.type  = LORA_PKT_HEARTBEAT;
                hb.hdr.seq   = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
                hb.hdr.vt    = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
                hb.uptime    = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8)
                             | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
                hb.heap      = (uint16_t)buf[8] | ((uint16_t)buf[9] << 8);

                Serial.printf("[LoRa-RX] Heartbeat seq=%u vt=%umV uptime=%lus heap=%u\n",
                              hb.hdr.seq, hb.hdr.vt,
                              (unsigned long)hb.uptime, hb.heap);
                return false;

            } else if (pktType == LORA_PKT_DATA) {
                uint8_t buf[sizeof(LoRaDataPacket)];
                buf[0] = LORA_MAGIC;
                buf[1] = LORA_PKT_DATA;

                size_t need = sizeof(LoRaDataPacket) - 2;
                size_t got = 0;
                t0 = millis();
                while (got < need && millis() - t0 < 2000) {
                    if (LORA_SERIAL.available()) {
                        buf[2 + got] = (uint8_t)LORA_SERIAL.read();
                        got++;
                    } else {
                        feedWDT(); delay(1);
                    }
                }
                if (got < need) {
                    Serial.printf("[LoRa-RX] Data packet incomplete (%u/%u)\n",
                                  (unsigned)got, (unsigned)need);
                    return false;
                }

                memcpy(&out, buf, sizeof(out));

                Serial.printf("[LoRa-RX] Data seq=%u vt=%umV "
                              "%02u/%02u/%02u %02u:%02u "
                              "soil[h=%u t=%d] weather[ws=%u ah=%u at=%d] ble=%u\n",
                              out.hdr.seq, out.hdr.vt,
                              out.date, out.month, out.year,
                              out.hour, out.minute,
                              out.sensor.soil_humi, out.sensor.soil_temp,
                              out.sensor.windSpeed, out.sensor.air_humidity,
                              out.sensor.air_temperature,
                              out.ble_valid);
                printReceivedDataPacket(out);
                sendLoRaAck(out.hdr.seq);
                return true;
            } else {
                Serial.printf("[LoRa-RX] Unknown packet type 0x%02X\n", pktType);
                return false;
            }
        }
    }
    return false;
}

/* ===== DATA RECORD STORAGE ===== */
static void dataRecordFromPacket(const LoRaDataPacket& pkt, DataRecord& rec) {
    memset(&rec, 0, sizeof(rec));
    rec.date   = pkt.date;
    rec.month  = pkt.month;
    rec.year   = pkt.year;
    rec.hour   = pkt.hour;
    rec.minute = pkt.minute;
    rec.data   = pkt.sensor;
    rec.ble    = pkt.ble;
    rec.ble_valid = pkt.ble_valid;
    rec.valid = 1;
}

/* ===== BLE PACKET BUILDER =====
 * Converts collected DataRecord array into UARTPacket format
 * expected by the LILYGO SIM800L NUS receiver.
 *
 * Wire format follows BlePacketTypes.h on this board and packet_types.h
 * on the LILYGO SIM800L board.
 */
static void copySensorDataToTransport(const SensorData& src, BleTransport_SensorData& dst) {
    dst.soil_humi       = src.soil_humi;
    dst.soil_temp       = src.soil_temp;
    dst.soil_ec         = src.soil_ec;
    dst.soil_ph         = src.soil_ph;
    dst.soil_N          = src.soil_N;
    dst.soil_P          = src.soil_P;
    dst.soil_K          = src.soil_K;
    dst.windSpeed       = src.windSpeed;
    dst.windDir_Deg     = src.windDir_Deg;
    dst.air_humidity    = src.air_humidity;
    dst.air_temperature = src.air_temperature;
    dst.CO2             = src.CO2;
    dst.pressure        = src.pressure;
    dst.illuminance     = src.illuminance;
    dst.rainfall        = src.rainfall;
    dst.solar           = src.solar;
}

static void copyBleDataToTransport(const BleSensorData& src, BleTransport_BleSensorData& dst) {
    dst.ble_temp   = src.ble_temp;
    dst.ble_humi   = src.ble_humi;
    dst.ble_tmp117 = src.ble_tmp117;
    dst.ble_rain   = src.ble_rain;
    dst.ble_leaf   = src.ble_leaf;
    dst.ble_par    = src.ble_par;
    dst.ble_soil   = src.ble_soil;
}

static uint16_t bleCalculateChecksum(const uint8_t* data, size_t len) {
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

static bool buildBlePacket(const DataRecord* records, uint8_t count,
                            BleTransport_UARTPacket& pkt) {
    if (count == 0 || count > BLE_PKT_MAX_RECORDS) return false;

    memset(&pkt, 0, sizeof(pkt));

    pkt.header1 = BLE_PKT_HEADER1;
    pkt.header2 = BLE_PKT_HEADER2;
    pkt.nodeID  = BLE_NODE_ID;

    BleTransport_LoRaBatchPacket& batch = pkt.packet;
    batch.header1 = BLE_PKT_HEADER1;
    batch.header2 = BLE_PKT_HEADER2;
    batch.nodeID  = BLE_NODE_ID;
    batch.date    = records[0].date;
    batch.month   = records[0].month;
    batch.year    = records[0].year;
    batch.batteryVoltage = batteryVoltage / 10;

    uint8_t validCount = 0;
    for (uint8_t i = 0; i < count && i < BLE_PKT_MAX_RECORDS; i++) {
        const DataRecord& src = records[i];
        BleTransport_DataRecord& dst = batch.records[i];

        dst.hour   = src.hour;
        dst.minute = src.minute;
        dst.valid  = BLE_PKT_RECORD_VALID;
        dst.checksum = 0;

        copySensorDataToTransport(src.data, dst.data);
        copyBleDataToTransport(src.ble, dst.ble);
        dst.ble_valid = src.ble_valid;

        uint8_t recCrc = 0;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&dst);
        for (size_t j = 0; j < offsetof(BleTransport_DataRecord, checksum); j++) {
            recCrc += p[j];
        }
        dst.checksum = recCrc;
        validCount++;
    }

    for (uint8_t i = validCount; i < BLE_PKT_MAX_RECORDS; i++) {
        memset(&batch.records[i], 0, sizeof(BleTransport_DataRecord));
    }

    batch.checksum = bleCalculateChecksum(
        reinterpret_cast<const uint8_t*>(&batch),
        offsetof(BleTransport_LoRaBatchPacket, checksum)
    );

    pkt.dataLen = sizeof(BleTransport_LoRaBatchPacket);
    pkt.checksum = bleCalculateChecksum(
        reinterpret_cast<const uint8_t*>(&pkt),
        offsetof(BleTransport_UARTPacket, checksum)
    );

    return true;
}

static bool forwardToBle(const DataRecord* records, uint8_t count) {
    if (!bleClient.isConnected()) {
        Serial.println(F("[BLE-TX] Not connected, skipping BLE forward"));
        return false;
    }

    BleTransport_UARTPacket pkt;
    if (!buildBlePacket(records, count, pkt)) {
        Serial.println(F("[BLE-TX] Failed to build packet"));
        return false;
    }

    Serial.printf("[BLE-TX] Forwarding %u records via BLE (sizeof=%u)\n",
                  count, sizeof(BleTransport_UARTPacket));
    Serial.printf("[BLE-TX] Header: %02X %02X Node:%u\n",
                  pkt.header1, pkt.header2, pkt.nodeID);

    bool ok = bleClient.sendPacket(pkt);
    if (ok) {
        Serial.printf("[BLE-TX] Sent OK (total=%lu)\n",
                      (unsigned long)bleClient.getTotalSent());
    } else {
        Serial.printf("[BLE-TX] Send FAILED (errors=%lu dropped=%lu)\n",
                      (unsigned long)bleClient.getTotalErrors(),
                      (unsigned long)bleClient.getTotalDropped());
    }
    return ok;
}

static bool forwardLoRaPacketsToBle(const LoRaDataPacket* packets, uint8_t count) {
    if (!bleClient.isConnected()) {
        Serial.println(F("[BLE-TX] Not connected, skipping raw LoRa forward"));
        return false;
    }

    bool allOk = true;
    for (uint8_t i = 0; i < count; i++) {
        const LoRaDataPacket& pkt = packets[i];
        Serial.printf("[BLE-TX] Forwarding raw LoRa packet %u/%u seq=%u (%u bytes) ble=%u\n",
                      i + 1, count, pkt.hdr.seq,
                      (unsigned)sizeof(LoRaDataPacket), pkt.ble_valid);
        bool ok = bleClient.sendBytes(reinterpret_cast<const uint8_t*>(&pkt),
                                      sizeof(LoRaDataPacket));
        if (!ok) allOk = false;
        feedWDT();
        delay(50);
    }
    return allOk;
}

/* ===== SETUP ===== */
void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    delay(500);

    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    Serial.println(F("[WDT] Watchdog initialized"));
    Serial.printf("[FW] PAMIANG RX Board v%s (BLE Relay)\n", FIRMWARE_VERSION);

    pinMode(PIN_DONE, OUTPUT);
    digitalWrite(PIN_DONE, LOW);

    batteryVoltage = batteryReadRx();
    Serial.printf("[BATT] %u mV\n", batteryVoltage);

    feedWDT();
    if (!LittleFS.begin(true)) Serial.println(F("[FS] LittleFS FAILED"));
    else Serial.println(F("[FS] LittleFS mounted"));

    bool recreateTemp = !LittleFS.exists(temporarydata);
    if (!recreateTemp) {
        File tf = LittleFS.open(temporarydata, "r");
        String hdr = tf.readStringUntil('\n');
        tf.close();
        recreateTemp = (hdr.indexOf("BLE_Temp") < 0 || hdr.indexOf("BLE_DeltaT") >= 0);
    }

    if (recreateTemp) {
        LittleFS.remove(temporarydata);
        internalMemory.write(LittleFS, temporarydata,
            "Date,Time,"
            "Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,"
            "WindSpeed,WindDirection,Air_Humidity,Air_Temperature,"
            "CO2,Pressure,Illuminance,Rainfall,Solar,"
            "BLE_Temp,BLE_Humi,BLE_TMP117,BLE_Rain,BLE_Leaf,BLE_PAR,BLE_Soil\r\n");
    }

    Serial.println(F("===== SETUP COMPLETE ====="));
}

/* ===== MAIN LOOP ===== */
void loop() {
    feedWDT();

    bleClient.loop();

    if (currentState != prevState) {
        stateEntryTime = millis(); prevState = currentState;
    }

    switch (currentState) {

        /* ── LoRa INIT ── */
        case STATE_LORA_INIT: {
            LORA_SERIAL.begin(SERIAL_LORA, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
            delay(1000);
            feedWDT();

            if (loraHandler.init(LORA_SERIAL)) {
                loraAvailable = true;
                Serial.println(F("[LoRa] E32-433T30S initialized OK"));
            } else {
                loraAvailable = false;
                Serial.println(F("[LoRa] E32 init FAILED"));
            }

            currentState = STATE_BLE_INIT;
            break;
        }

        /* ── BLE INIT ── */
        case STATE_BLE_INIT: {
            Serial.println(F("[BLE] Initializing BLE NUS client..."));
            feedWDT();

            bool bleOk = bleClient.begin(
                BLE_TARGET_DEVICE_NAME,
                BLE_NUS_SERVICE_UUID,
                BLE_NUS_RX_CHAR_UUID,
                BLE_NUS_TX_CHAR_UUID
            );

            if (bleOk) {
                Serial.println(F("[BLE] NUS client initialized"));
            } else {
                Serial.println(F("[BLE] NUS client init FAILED"));
            }

            currentState = STATE_LORA_RX;
            break;
        }

        /* ── LoRa RECEIVE (collect packets for a window) ── */
        case STATE_LORA_RX: {
            if (rxWindowStart == 0) {
                rxWindowStart = millis();
                rxRecordCount = 0;
                Serial.println(F("[LoRa-RX] Listening for packets..."));
            }

            LoRaDataPacket pkt;
            while (readLoRaPacket(pkt) && rxRecordCount < PUBLISH_MAX_RECORDS) {
                rxPackets[rxRecordCount] = pkt;
                dataRecordFromPacket(pkt, rxRecords[rxRecordCount]);
                rxRecordCount++;
                Serial.printf("[LoRa-RX] Stored record %u/%u\n",
                              rxRecordCount, PUBLISH_MAX_RECORDS);
            }

            unsigned long elapsed = millis() - rxWindowStart;

            if (rxRecordCount >= PUBLISH_MAX_RECORDS ||
                (elapsed >= RX_WINDOW_MS && rxRecordCount > 0)) {
                Serial.printf("[LoRa-RX] Window done: %u records in %lu ms\n",
                              rxRecordCount, elapsed);
                currentState = STATE_SAVE;
                break;
            }

            if (elapsed >= RX_WINDOW_MS && rxRecordCount == 0) {
                rxWindowStart = millis();
            }

            feedWDT();
            delay(10);
            break;
        }

        /* ── SAVE received records to temp CSV + forward via BLE ── */
        case STATE_SAVE: {
            for (uint8_t i = 0; i < rxRecordCount; i++) {
                DataRecord& rec = rxRecords[i];
                timeStruct ts = {};
                ts.date   = rec.date;
                ts.month  = rec.month;
                ts.year   = (uint16_t)rec.year + 2000;
                ts.hour   = rec.hour;
                ts.minute = rec.minute;
                ts.second = 0;
                snprintf(ts.dateStr, sizeof(ts.dateStr), "%02u/%02u/%04u",
                         ts.date, ts.month, ts.year);
                snprintf(ts.timeStr, sizeof(ts.timeStr), "%02u:%02u:%02u",
                         ts.hour, ts.minute, ts.second);

                snprintf(dailyCsv, sizeof(dailyCsv), "/%02u-%02u-%04u.csv",
                         ts.date, ts.month, ts.year);

                internalMemory.saveData(dailyCsv, &ts, &rec.data,
                                        rec.ble_valid ? &rec.ble : nullptr);
                internalMemory.saveData(temporarydata, &ts, &rec.data,
                                        rec.ble_valid ? &rec.ble : nullptr);
            }

            Serial.printf("[SAVE] Saved %u records to CSV\n", rxRecordCount);

            if (rxRecordCount > 0) {
                forwardLoRaPacketsToBle(rxPackets, rxRecordCount);
            }

            rxWindowStart = 0;
            rxRecordCount = 0;

            currentState = STATE_LORA_RX;
            break;
        }

        /* ── FINISH ── */
        case STATE_FINISH: {
            bleClient.disconnect();
            Serial.println(F("[DONE] Pulsing TPL5110"));
            delay(3000);
            Serial.flush();
            for (int i = 0; i < 5; i++) {
                digitalWrite(PIN_DONE, LOW);  delay(100);
                digitalWrite(PIN_DONE, HIGH); delay(3000);
                delay(500);
            }
            Serial.println(F("[WARN] TPL5110 did not cut power"));
            Serial.flush();
            while (1) { feedWDT(); delay(1000); }
            break;
        }

        default: currentState = STATE_FINISH; break;
    }
}
