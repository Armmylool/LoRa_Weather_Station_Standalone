/* ROVER with SENSOR, GSM SIM800L, WiFi AP, and MQTT */
/* ====================================================== */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_task_wdt.h>
#include "sensor_v2.h"
#include "Memory.h"
#include "GsmHandler.h"
#include "WifiApServer.h"

/* ===== PUBLISH MODE ===== */
#define REAL    /* Uncomment for REAL: 6 records in 1 MQTT payload */
                /* Comment out for TEST: 1 record per MQTT payload (default) */

/* ===== STATE MACHINE ===== */
typedef enum {
    STATE_GSM_INIT,
    STATE_NTP,
    STATE_WIFI_AP,
    STATE_SOIL,
    STATE_WEATHER,
    STATE_SAVE,
    STATE_RECONNECT,
    STATE_PUBLISH,
    STATE_FINISH,
} systemState;

/* ===== GLOBAL OBJECTS ===== */
RS485sensor modbusSensor;
Memory internalMemory;
GsmHandler gsmHandler;
WifiApServer wifiApServer;
HardwareSerial RS485Serial(1);
HardwareSerial GSM_SERIAL(0);
systemState currentState = STATE_GSM_INIT;

/* ===== GLOBAL VARIABLES ===== */
timeStruct currentTime = {0, 0, 0, 0, 0, 0, "00/00/0000", "00:00:00"};
static unsigned long soilStateStart = 0;
static unsigned long weatherStateStart = 0;
static bool gsmAvailable = false;
static bool timeSynced = false;
static bool wifiApStarted = false;
static uint16_t batteryVoltage = 0;
static unsigned long stateEntryTime = 0;
static systemState prevState = STATE_GSM_INIT;

/* ===== FORWARD DECLARATIONS ===== */
void checkFile(const char* fileName);
bool readLastTimeFromBackup(timeStruct* outTime);
void incrementTime(timeStruct* t, uint8_t addMinutes);
void feedWDT();
void clearWeatherData();
bool publishBatchData();
bool formatRecordJson(const DataRecord& record, char* buf, size_t bufLen);
bool publishHeartbeat();
#ifdef REAL
bool publishRealData();
bool buildBatchJSON(char* buffer, size_t bufferSize);
#endif
String getLastDataLine(const char* fileName);

/* ===== SETUP ===== */

void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    delay(500);

    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    Serial.println(F("[WDT] Watchdog initialized"));

    pinMode(BATT_PIN, INPUT);
    pinMode(PIN_DONE, OUTPUT);
    digitalWrite(PIN_DONE, LOW);

    RS485Serial.begin(SERIAL_RS485, SERIAL_8N1, RS_485_RX_PIN, RS_485_TX_PIN);
    modbusSensor.begin(&RS485Serial);
    Serial.println(F("[SERIAL] RS485 HardwareSerial UART1 started"));

    GSM_SERIAL.begin(SERIAL_GSM, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    Serial.println(F("[SERIAL] GSM HardwareSerial UART0 started"));

    feedWDT();
    if (!LittleFS.begin(true)) {
        Serial.println(F("Error: Failed to mount LittleFS."));
    }
    Serial.println(F("LittleFS mounted successfully."));
    feedWDT();
    checkFile(backupdata);
    delay(100);
    checkFile(temporarydata);
    delay(100);

    Serial.println(F("===== SETUP COMPLETE ====="));
}

/* ===== MAIN LOOP (STATE MACHINE) ===== */

void loop() {
    feedWDT();

    /* Track state entry time for software timeout */
    if (currentState != prevState) {
        stateEntryTime = millis();
        prevState = currentState;
    }

    /* Per-state software timeout safety net */
    unsigned long stateElapsed = millis() - stateEntryTime;
    systemState fallbackState = currentState;
    unsigned long maxDuration = 0;
    bool timeoutForce = false;

    switch (currentState) {
        case STATE_GSM_INIT:   maxDuration = GSM_INIT_TIMEOUT_MS;   fallbackState = STATE_NTP;       break;
        case STATE_NTP:        maxDuration = TIME_WAIT_TIMEOUT;      fallbackState = STATE_WIFI_AP;    break;
        case STATE_WIFI_AP:    maxDuration = WIFI_AP_TIMEOUT;        fallbackState = STATE_SOIL;       break;
        case STATE_SOIL:       maxDuration = SOIL_TIMEOUT;           fallbackState = STATE_WEATHER;    break;
        case STATE_WEATHER:    maxDuration = WEATHER_TIMEOUT;        fallbackState = STATE_SAVE;       break;
        case STATE_SAVE:       maxDuration = 10000;                  fallbackState = STATE_RECONNECT;  break;
        case STATE_RECONNECT:  maxDuration = RECONNECT_TIMEOUT;      fallbackState = STATE_FINISH;     break;
        case STATE_PUBLISH:    maxDuration = MQTT_PUBLISH_TIMEOUT;   fallbackState = STATE_FINISH;     break;
        case STATE_FINISH:     maxDuration = 0; break;
        default:               maxDuration = 0; break;
    }

    if (maxDuration > 0 && stateElapsed >= maxDuration) {
        Serial.printf("[WDT] State %d TIMEOUT after %lu ms (max %lu ms). Forcing -> State %d\n",
                      currentState, stateElapsed, maxDuration, fallbackState);
        currentState = fallbackState;
        stateEntryTime = millis();
        prevState = currentState;
        timeoutForce = true;
    }

    if (timeoutForce) return;

    switch (currentState) {

        /* ===== GSM INIT + GPRS CONNECT ===== */
        case STATE_GSM_INIT: {
            if (!gsmHandler.init(GSM_SERIAL)) {
                Serial.println(F("[GSM] Init failed! Will use fallback time."));
                gsmAvailable = false;
                currentState = STATE_NTP;
                break;
            }

            if (!gsmHandler.connectNetwork()) {
                Serial.println(F("[GSM] Network connect failed!"));
                gsmAvailable = false;
                currentState = STATE_NTP;
                break;
            }

            gsmAvailable = true;
            Serial.println(F("[GSM] Ready."));

            /* Publish heartbeat immediately now that internet is up */
            publishHeartbeat();

            currentState = STATE_NTP;
            break;
        }

        /* ===== NTP TIME SYNC ===== */
        case STATE_NTP: {
            timeSynced = false;

            if (gsmAvailable) {
                if (gsmHandler.getNetworkTime(&currentTime)) {
                    timeSynced = true;
                    Serial.printf("[NTP] Synced: %s %s\n", currentTime.dateStr, currentTime.timeStr);
                } else {
                    Serial.println(F("[NTP] Failed. Using fallback."));
                }
            }

            if (!timeSynced) {
                if (!readLastTimeFromBackup(&currentTime)) {
                    Serial.println(F("[NTP] No backup data. Using zero time."));
                    memset(&currentTime, 0, sizeof(timeStruct));
                    snprintf(currentTime.dateStr, sizeof(currentTime.dateStr), "00/00/0000");
                    snprintf(currentTime.timeStr, sizeof(currentTime.timeStr), "00:00:00");
                }
                incrementTime(&currentTime, TIME_INCREMENT_MINUTES);
                Serial.printf("[NTP] Fallback: %s %s\n", currentTime.dateStr, currentTime.timeStr);
            }

            currentState = STATE_WIFI_AP;

            batteryVoltage = batteryRead();
            Serial.printf("[BATT] Battery: %u mV\n", batteryVoltage);
            break;
        }

        /* ===== WIFI AP FOR DATA DOWNLOAD (60s TIMEOUT) ===== */
        case STATE_WIFI_AP: {
            if (!wifiApStarted) {
                if (gsmAvailable) {
                    gsmHandler.mqttDisconnect();
                }
                wifiApStarted = true;
                wifiApServer.begin();
            }

            wifiApServer.handleClient();

            if (wifiApServer.isTimedOut()) {
                wifiApServer.stop();
                wifiApStarted = false;
                Serial.println(F("[WiFi] AP timeout. Proceeding to sensor read."));
                currentState = STATE_SOIL;
            }
            break;
        }

        /* ===== READ SOIL SENSOR ===== */
        case STATE_SOIL: {
            if (soilStateStart == 0) {
                soilStateStart = millis();
            }

            unsigned long elapsed = millis() - soilStateStart;
            if (elapsed < SOIL_SETTLE_DELAY) {
                feedWDT();
                delay(10);
                break;
            }
            if (modbusSensor.read(SOIL, SOIL_SLAVE_ID, SOIL_REGISTER_ADDR,
                                  SOIL_REGISTER_LEN, &RS485Serial)) {
                if (DEBUG) {
                    Serial.println(F("Soil Success!"));
                    Serial.printf("  Moisture: %.1f%%\n", modbusSensor.currentSensor.soil_humi / 10.0);
                    Serial.printf("  Temp: %.1f C\n", modbusSensor.currentSensor.soil_temp / 10.0);
                    Serial.printf("  EC: %u uS/cm\n", modbusSensor.currentSensor.soil_ec);
                    Serial.printf("  PH: %.1f\n", modbusSensor.currentSensor.soil_ph / 10.0);
                    Serial.printf("  N: %u mg/kg\n", modbusSensor.currentSensor.soil_N);
                    Serial.printf("  P: %u mg/kg\n", modbusSensor.currentSensor.soil_P);
                    Serial.printf("  K: %u mg/kg\n", modbusSensor.currentSensor.soil_K);
                }
                soilStateStart = 0;
                currentState = STATE_WEATHER;
                break;
            }

            if (millis() - soilStateStart >= SOIL_TIMEOUT) {
                Serial.println(F("[WARN] Soil sensor timeout."));
                soilStateStart = 0;
                currentState = STATE_WEATHER;
            }
            break;
        }

        /* ===== READ WEATHER SENSOR ===== */
        case STATE_WEATHER: {
            if (weatherStateStart == 0) {
                weatherStateStart = millis();
                Serial.println(F("[WEATHER] Settling delay..."));
            }

            unsigned long elapsed = millis() - weatherStateStart;
            if (elapsed < WEATHER_SETTLE_DELAY) {
                feedWDT();
                delay(10);
                break;
            }

            if (modbusSensor.read(WEATHER, WEATHER_SLAVE_ID, WEATHER_REGISTER_ADDR,
                                  WEATHER_REGISTER_LEN, &RS485Serial)) {
                if (DEBUG) {
                    Serial.println(F("Weather Success!"));
                    Serial.printf("  Wind Speed: %.1f m/s\n", modbusSensor.currentSensor.windSpeed / 10.0);
                    Serial.printf("  Wind Dir: %u Deg\n", modbusSensor.currentSensor.windDir_Deg);
                    Serial.printf("  Air Humidity: %.1f%%\n", modbusSensor.currentSensor.air_humidity / 10.0);
                    Serial.printf("  Air Temp: %.1f C\n", modbusSensor.currentSensor.air_temperature / 10.0);
                    Serial.printf("  CO2: %u ppm\n", modbusSensor.currentSensor.CO2);
                    Serial.printf("  Pressure: %.1f kPa\n", modbusSensor.currentSensor.pressure / 10.0);
                    Serial.printf("  Illuminance: %lu lux\n", modbusSensor.currentSensor.illuminance);
                    Serial.printf("  Rainfall: %.1f mm\n", modbusSensor.currentSensor.rainfall / 10.0);
                    Serial.printf("  Solar: %u W/m2\n", modbusSensor.currentSensor.solar);
                }
                weatherStateStart = 0;
                currentState = STATE_SAVE;
                break;
            }

            if (millis() - weatherStateStart >= WEATHER_TIMEOUT) {
                Serial.println(F("[WARN] Weather sensor timeout."));
                clearWeatherData();
                weatherStateStart = 0;
                currentState = STATE_SAVE;
            }
            break;
        }

        /* ===== SAVE TO LITTLEFS (smart rollover keeps last entry) ===== */
        case STATE_SAVE: {
            if (internalMemory.isUsageOverThreshold(STORAGE_ROLLOVER_PERCENT)) {
                String lastLine = getLastDataLine(backupdata);
                LittleFS.remove(backupdata);
                checkFile(backupdata);
                if (lastLine.length() > 0) {
                    String entry = lastLine + "\r\n";
                    internalMemory.append(LittleFS, backupdata, entry.c_str());
                }
                Serial.println(F("[SAVE] Rollover: backup reset, last entry preserved."));
            }

            if (internalMemory.saveData(backupdata, &currentTime, &modbusSensor.currentSensor)) {
                Serial.println(F("[SAVE] Saved to backup."));
            } else {
                Serial.println(F("[SAVE] Backup save FAILED."));
            }

            if (internalMemory.saveData(temporarydata, &currentTime, &modbusSensor.currentSensor)) {
                Serial.println(F("[SAVE] Saved to temp."));
            } else {
                Serial.println(F("[SAVE] Temp save FAILED."));
            }

            currentState = STATE_RECONNECT;
            break;
        }

        /* ===== RECONNECT GSM BEFORE PUBLISH ===== */
        case STATE_RECONNECT: {
            int recordCount = internalMemory.countDataLines(temporarydata);
            if (recordCount < PUBLISH_BATCH_SIZE) {
                Serial.printf("[RECONNECT] Only %d records (< %d). Skip publish.\n",
                              recordCount, PUBLISH_BATCH_SIZE);
                currentState = STATE_FINISH;
                break;
            }

            if (gsmAvailable && gsmHandler.isNetworkConnected()) {
                Serial.println(F("[RECONNECT] GSM still connected."));
                currentState = STATE_PUBLISH;
                break;
            }

            Serial.println(F("[RECONNECT] GSM down. Attempting software reset + reconnect..."));
            unsigned long reconnectStart = millis();

            gsmHandler.restart();

            while (millis() - reconnectStart < RECONNECT_TIMEOUT) {
                feedWDT();

                if (!gsmHandler.init(GSM_SERIAL)) {
                    Serial.println(F("[RECONNECT] Init failed, retrying in 5s..."));
                    delay(5000);
                    continue;
                }

                if (!gsmHandler.connectNetwork()) {
                    Serial.println(F("[RECONNECT] Network failed, retrying in 5s..."));
                    gsmHandler.restart();
                    delay(5000);
                    continue;
                }

                gsmAvailable = true;
                Serial.println(F("[RECONNECT] GSM reconnected!"));
                currentState = STATE_PUBLISH;
                break;
            }

            if (currentState != STATE_PUBLISH) {
                Serial.println(F("[RECONNECT] Failed. Temp data kept. Going to sleep."));
                gsmAvailable = false;
                currentState = STATE_FINISH;
            }
            break;
        }

        /* ===== PUBLISH VIA MQTT ===== */
        case STATE_PUBLISH: {
            int recordCount = internalMemory.countDataLines(temporarydata);
            Serial.printf("[PUBLISH] Temp records: %d\n", recordCount);

            if (recordCount < PUBLISH_BATCH_SIZE) {
                Serial.println(F("[PUBLISH] Not enough records yet."));
                currentState = STATE_FINISH;
                break;
            }

#ifdef REAL
            if (publishRealData()) {
                Serial.println(F("[PUBLISH] REAL: Batch published (all in 1 message)."));
            } else {
                Serial.println(F("[PUBLISH] REAL: Publish failed. Temp data kept for retry."));
            }
#else
            if (publishBatchData()) {
                Serial.println(F("[PUBLISH] TEST: All records published individually."));
            } else {
                Serial.println(F("[PUBLISH] TEST: Publish failed. Temp data kept for retry."));
            }
#endif

            currentState = STATE_FINISH;
            break;
        }

        /* ===== FINISH: SIGNAL TPL5110 DONE ===== */
        case STATE_FINISH: {
            Serial.println(F("[DONE] Sending DONE to TPL5110"));
            Serial.flush();
            pinMode(PIN_DONE, OUTPUT);
            digitalWrite(PIN_DONE, LOW);
            delay(50);
            digitalWrite(PIN_DONE, HIGH);
            delay(2000);
            Serial.println(F("[WARN] TPL5110 did not cut power"));
            Serial.flush();
            while (1) { feedWDT(); delay(1000); }
            break;
        }

        default:
            Serial.printf("[ERR] Invalid state=%d\n", currentState);
            currentState = STATE_FINISH;
            break;
    }
}

/* ===== MQTT BATCH PUBLISH HELPER ===== */

bool publishBatchData() {
    if (!gsmHandler.mqttConnect()) {
        Serial.println(F("[MQTT] MQTT connect failed"));
        return false;
    }

    int recordCount = internalMemory.countDataLines(temporarydata);
    int toPublish = (recordCount > PUBLISH_MAX_RECORDS) ? PUBLISH_MAX_RECORDS : recordCount;
    int totalPublished = 0;

    while (totalPublished < toPublish) {
        int batchCount = (toPublish - totalPublished > PUBLISH_BATCH_SIZE)
                         ? PUBLISH_BATCH_SIZE : (toPublish - totalPublished);

        DataRecord records[PUBLISH_BATCH_SIZE];
        memset(records, 0, sizeof(records));

        if (!internalMemory.readDataRecords(temporarydata, records, batchCount)) {
            Serial.println(F("[MQTT] Failed to read temp records"));
            break;
        }

        bool batchOk = true;
        for (int i = 0; i < batchCount; i++) {
            if (records[i].valid != 1) continue;

            static char jsonBuf[400];
            if (!formatRecordJson(records[i], jsonBuf, sizeof(jsonBuf))) {
                Serial.printf("[MQTT] JSON format failed for record %d\n", i);
                continue;
            }

            gsmHandler.mqttLoop();
            if (!gsmHandler.mqttPublish(MQTT_TOPIC, jsonBuf)) {
                Serial.printf("[MQTT] Publish failed at record %d\n", i);
                batchOk = false;
                break;
            }
            delay(100);
        }

        if (batchOk) {
            internalMemory.removeFirstDataLines(temporarydata, batchCount);
            totalPublished += batchCount;
            Serial.printf("[MQTT] Batch done: %d/%d published\n", totalPublished, toPublish);
        } else {
            break;
        }
    }

    gsmHandler.mqttDisconnect();
    Serial.printf("[MQTT] Total published: %d/%d\n", totalPublished, toPublish);
    return (totalPublished == toPublish);
}

bool formatRecordJson(const DataRecord& record, char* buf, size_t bufLen) {
    char dateBuf[12];
    char timeBuf[9];
    snprintf(dateBuf, sizeof(dateBuf), "%02u/%02u/%04u",
             record.date, record.month, 2000 + record.year);
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:00",
             record.hour, record.minute);

    int written = snprintf(buf, bufLen,
        "{\"date\":\"%s\",\"time\":\"%s\",\"battery_mv\":%u,"
        "\"soil_humidity\":%.1f,\"soil_temperature\":%.1f,\"ec\":%u,\"ph\":%.1f,"
        "\"n\":%u,\"p\":%u,\"k\":%u,"
        "\"wind_speed\":%.1f,\"wind_direction\":%u,\"air_humidity\":%.1f,"
        "\"air_temperature\":%.1f,\"co2\":%u,\"pressure\":%.1f,"
        "\"illuminance\":%lu,\"rainfall\":%.1f,\"solar\":%u}",
        dateBuf, timeBuf, batteryVoltage,
        record.data.soil_humi / 10.0,
        record.data.soil_temp / 10.0,
        record.data.soil_ec,
        record.data.soil_ph / 10.0,
        record.data.soil_N,
        record.data.soil_P,
        record.data.soil_K,
        record.data.windSpeed / 10.0,
        record.data.windDir_Deg,
        record.data.air_humidity / 10.0,
        record.data.air_temperature / 10.0,
        record.data.CO2,
        record.data.pressure / 10.0,
        (unsigned long)record.data.illuminance,
        record.data.rainfall / 10.0,
        record.data.solar
    );

    return (written > 0 && (size_t)written < bufLen);
}

/* ===== REAL MODE: Batch JSON (6 records in 1 MQTT payload) ===== */
#ifdef REAL
#include <stdarg.h>
static uint16_t publishSequence = 0;

static bool appendToBuffer(char* buffer, size_t bufferSize, int& len, const char* format, ...) {
    if (bufferSize == 0 || len < 0 || (size_t)len >= bufferSize) return false;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + len, bufferSize - len, format, args);
    va_end(args);
    if (written < 0) return false;
    if ((size_t)written >= bufferSize - len) {
        len = (int)bufferSize - 1;
        buffer[len] = '\0';
        return false;
    }
    len += written;
    return true;
}

bool buildBatchJSON(char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return false;
    buffer[0] = '\0';

    int recordCount = internalMemory.countDataLines(temporarydata);
    int toPublish = (recordCount > PUBLISH_MAX_RECORDS) ? PUBLISH_MAX_RECORDS : recordCount;
    if (toPublish <= 0) return false;

    DataRecord records[PUBLISH_MAX_RECORDS];
    memset(records, 0, sizeof(records));
    if (!internalMemory.readDataRecords(temporarydata, records, toPublish)) return false;

    int validCount = 0;
    for (int i = 0; i < toPublish; i++) {
        if (records[i].valid == 1) validCount++;
    }
    if (validCount == 0) return false;

    int len = 0;
    if (!appendToBuffer(buffer, bufferSize, len,
        "{\"n\":0,\"seq\":%u,\"vt\":%u,\"d\":\"%02u%02u%02u\",\"r\":[",
        publishSequence++, batteryVoltage,
        records[0].date, records[0].month, records[0].year % 100)) {
        return false;
    }

    int idx = 0;
    for (int i = 0; i < toPublish; i++) {
        if (records[i].valid != 1) continue;
        if (idx > 0 && !appendToBuffer(buffer, bufferSize, len, ",")) return false;

        const SensorData& d = records[i].data;
        if (!appendToBuffer(buffer, bufferSize, len,
            "{\"d\":\"%02u%02u%02u\",\"t\":\"%02u%02u\","
            "\"sh\":%u,\"st\":%d,\"se\":%u,\"ph\":%u,"
            "\"sn\":%u,\"sp\":%u,\"sk\":%u,"
            "\"ws\":%u,\"wd\":%u,\"ah\":%u,\"at\":%d,"
            "\"co2\":%u,\"pr\":%u,\"il\":%lu,\"rf\":%u,\"so\":%u}",
            records[i].date, records[i].month, records[i].year % 100,
            records[i].hour, records[i].minute,
            d.soil_humi, d.soil_temp, d.soil_ec, d.soil_ph,
            d.soil_N, d.soil_P, d.soil_K,
            d.windSpeed, d.windDir_Deg, d.air_humidity, d.air_temperature,
            d.CO2, d.pressure, (unsigned long)d.illuminance, d.rainfall, d.solar)) {
            Serial.printf("[MQTT] Buffer full at record %d, sending partial\n", idx);
            break;
        }
        idx++;
    }

    return appendToBuffer(buffer, bufferSize, len, "]}");
}

bool publishRealData() {
    if (!gsmHandler.mqttConnect()) {
        Serial.println(F("[MQTT] MQTT connect failed"));
        return false;
    }

    int recordCount = internalMemory.countDataLines(temporarydata);
    Serial.printf("[PUBLISH] REAL mode: %d records -> 1 payload\n", recordCount);

    if (recordCount < PUBLISH_BATCH_SIZE) {
        Serial.println(F("[PUBLISH] Not enough records yet."));
        gsmHandler.mqttDisconnect();
        return false;
    }

    static char jsonBuf[2048];
    if (!buildBatchJSON(jsonBuf, sizeof(jsonBuf))) {
        Serial.println(F("[MQTT] Failed to build batch JSON"));
        gsmHandler.mqttDisconnect();
        return false;
    }

    size_t payloadLen = strlen(jsonBuf);
    Serial.printf("[MQTT] Batch payload: %u bytes\n", payloadLen);

    /* SIM800L safe limit: split if payload > 1024 bytes */
    if (payloadLen > 1024) {
        Serial.printf("[MQTT] Payload too large (%u > 1024). Falling back to individual publish.\n", payloadLen);
        gsmHandler.mqttDisconnect();
        return publishBatchData();
    }

    bool ok = gsmHandler.mqttPublish(MQTT_TOPIC, jsonBuf);
    if (ok) {
        int published = (recordCount > PUBLISH_MAX_RECORDS) ? PUBLISH_MAX_RECORDS : recordCount;
        internalMemory.removeFirstDataLines(temporarydata, published);
        Serial.printf("[MQTT] Published %d records in 1 message\n", published);
    }

    gsmHandler.mqttDisconnect();
    return ok;
}
#endif /* REAL */

/* ===== HEARTBEAT ===== */

bool publishHeartbeat() {
    if (!gsmAvailable) return false;
    if (!gsmHandler.mqttConnect()) {
        Serial.println(F("[HB] MQTT connect failed"));
        return false;
    }

    char payload[128];
    int gsmRssi = gsmHandler.getSignalQuality();
    snprintf(payload, sizeof(payload),
        "{\"n\":0,\"alive\":1,\"vt\":%u,\"heap\":%u,\"uptime\":%lu,\"state\":%d,\"gsm_rssi\":%d}",
        batteryVoltage, ESP.getFreeHeap(), millis() / 1000, currentState, gsmRssi);

    bool ok = gsmHandler.mqttPublish(MQTT_PONG_TOPIC, payload);
    if (ok) {
        Serial.printf("[HB] Sent: %s\n", payload);
    } else {
        Serial.println(F("[HB] Publish failed"));
    }

    gsmHandler.mqttDisconnect();
    return ok;
}

/* ===== HELPER FUNCTIONS ===== */

void feedWDT() {
    esp_task_wdt_reset();
}

void clearWeatherData() {
    modbusSensor.currentSensor.windSpeed = 0;
    modbusSensor.currentSensor.windDir_Deg = 0;
    modbusSensor.currentSensor.air_humidity = 0;
    modbusSensor.currentSensor.air_temperature = 0;
    modbusSensor.currentSensor.CO2 = 0;
    modbusSensor.currentSensor.pressure = 0;
    modbusSensor.currentSensor.illuminance = 0;
    modbusSensor.currentSensor.rainfall = 0;
    modbusSensor.currentSensor.solar = 0;
}

void incrementTime(timeStruct* t, uint8_t addMinutes) {
    if (t->date == 0 || t->month == 0 || t->month > 12 || t->year == 0) {
        t->date = 1;
        t->month = 1;
        t->year = 2024;
        t->hour = 0;
        t->minute = 0;
        t->second = 0;
    }

    int totalMinutes = t->hour * 60 + t->minute + addMinutes;
    int carryDays = 0;

    while (totalMinutes >= 24 * 60) {
        totalMinutes -= 24 * 60;
        carryDays++;
    }

    t->hour = (uint8_t)(totalMinutes / 60);
    t->minute = (uint8_t)(totalMinutes % 60);
    t->second = 0;

    static const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    t->date += carryDays;

    uint8_t monthIdx = (t->month >= 1 && t->month <= 12) ? (t->month - 1) : 0;
    while (t->date > daysInMonth[monthIdx]) {
        t->date -= daysInMonth[monthIdx];
        t->month++;
        monthIdx = (t->month >= 1 && t->month <= 12) ? (t->month - 1) : 0;
        if (t->month > 12) {
            t->month = 1;
            t->year++;
            monthIdx = 0;
        }
    }

    snprintf(t->dateStr, sizeof(t->dateStr), "%02u/%02u/%04u", t->date, t->month, t->year);
    snprintf(t->timeStr, sizeof(t->timeStr), "%02u:%02u:%02u", t->hour, t->minute, t->second);
}

bool readLastTimeFromBackup(timeStruct* outTime) {
    if (!LittleFS.exists(backupdata)) return false;
    File file = LittleFS.open(backupdata, "r");
    if (!file) return false;

    file.readStringUntil('\n');

    String lastLine = "";
    uint16_t linesScanned = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) lastLine = line;
        linesScanned++;
        if ((linesScanned % 16) == 0) { feedWDT(); delay(0); }
    }
    file.close();

    if (lastLine.length() == 0) return false;

    int firstComma = lastLine.indexOf(',');
    if (firstComma < 0) return false;
    int secondComma = lastLine.indexOf(',', firstComma + 1);
    if (secondComma < 0) return false;

    String dateStr = lastLine.substring(0, firstComma);
    String timeStr = lastLine.substring(firstComma + 1, secondComma);

    int d1 = dateStr.indexOf('/');
    int d2 = dateStr.lastIndexOf('/');
    if (d1 < 0 || d2 < 0 || d1 == d2) return false;

    outTime->date = (uint8_t)dateStr.substring(0, d1).toInt();
    outTime->month = (uint8_t)dateStr.substring(d1 + 1, d2).toInt();
    outTime->year = (uint16_t)dateStr.substring(d2 + 1).toInt();

    int t1 = timeStr.indexOf(':');
    int t2 = timeStr.lastIndexOf(':');
    if (t1 < 0 || t2 < 0 || t1 == t2) return false;

    outTime->hour = (uint8_t)timeStr.substring(0, t1).toInt();
    outTime->minute = (uint8_t)timeStr.substring(t1 + 1, t2).toInt();
    outTime->second = (uint8_t)timeStr.substring(t2 + 1).toInt();

    snprintf(outTime->dateStr, sizeof(outTime->dateStr), "%02u/%02u/%04u",
             outTime->date, outTime->month, outTime->year);
    snprintf(outTime->timeStr, sizeof(outTime->timeStr), "%02u:%02u:%02u",
             outTime->hour, outTime->minute, outTime->second);

    return true;
}

String getLastDataLine(const char* fileName) {
    if (!LittleFS.exists(fileName)) return "";
    File file = LittleFS.open(fileName, "r");
    if (!file) return "";

    file.readStringUntil('\n');

    String lastLine = "";
    uint16_t linesScanned = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        linesScanned++;
        if ((linesScanned % 16) == 0) { feedWDT(); delay(0); }
        line.trim();
        if (line.length() > 0) lastLine = line;
    }
    file.close();
    return lastLine;
}

void checkFile(const char* fileName) {
    if (!LittleFS.exists(fileName)) {
        Serial.println(F("File doesn't exist. Creating new file"));
        internalMemory.write(LittleFS, fileName,
            "Date,Time,"
            "Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,"
            "WindSpeed,WindDirection,Air_Humidity,Air_Temperature,CO2,Pressure,Illuminance,Rainfall,Solar\r\n");
    } else {
        Serial.println(F("File exists. Ready to append."));
    }
}
