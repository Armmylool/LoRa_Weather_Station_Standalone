/* All-in-One Weather Station v2.0.0 */
#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_task_wdt.h>
#include <NimBLEDevice.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include "sensor_v2.h"
#include "Memory.h"
#include "GsmHandler.h"
#include "WifiApServer.h"

/* ===== STATE MACHINE ===== */
typedef enum {
    STATE_GSM_INIT,
    STATE_NTP,
    STATE_SOIL,
    STATE_WEATHER,
    STATE_SAVE,
    STATE_WIFI_AP,
    STATE_RECONNECT,
    STATE_PUBLISH,
    STATE_FINISH,
} systemState;

/* ===== GLOBAL OBJECTS ===== */
RS485sensor    modbusSensor;
Memory         internalMemory;
GsmHandler     gsmHandler;
WifiApServer   wifiApServer;
Preferences    nvs;
HardwareSerial RS485Serial(1);
HardwareSerial GSM_SERIAL(0);
systemState    currentState = STATE_WIFI_AP;  /* AP first, sensors after */

/* ===== GLOBAL VARIABLES ===== */
timeStruct currentTime    = {0, 0, 0, 0, 0, 0, "00/00/0000", "00:00:00"};
static unsigned long soilStateStart    = 0;
static unsigned long weatherStateStart = 0;
static bool     gsmAvailable  = false;
static bool     timeSynced    = false;
static bool     wifiApStarted = false;
static bool     bleActive     = false;
static uint16_t batteryVoltage = 0;
static int      gsmRssi        = 0;
static unsigned long stateEntryTime = 0;
static systemState   prevState      = STATE_GSM_INIT;

/* date-named daily CSV: /DD-MM-YYYY.csv */
static char dailyCsv[24] = "/DATA.csv";

/* SystemStatus passed to web portal */
static SystemStatus sysStatus;

/* ===== FORWARD DECLARATIONS ===== */
void checkFile(const char* fileName);
bool readLastTimeFromBackup(timeStruct* outTime);
void incrementTime(timeStruct* t, uint8_t addMinutes);
void feedWDT();
void clearWeatherData();
bool publishHeartbeat();
String getLastDataLine(const char* fileName);
bool buildCompactJSON(DataRecord* records, int count, uint16_t battVoltage, int gsmRssi,
                      char* buffer, size_t bufferSize);
void updateDailyCsv();
void initBLE();
void doBleScan();
void refreshBleData();

/* ===== MQTT HELPERS ===== */
#include <stdarg.h>
static uint16_t publishSequence = 0;

static bool appendToBuffer(char* buffer, size_t bufferSize, int& len, const char* format, ...) {
    if (bufferSize == 0 || len < 0 || (size_t)len >= bufferSize) return false;
    va_list args; va_start(args, format);
    int written = vsnprintf(buffer + len, bufferSize - len, format, args);
    va_end(args);
    if (written < 0) return false;
    if ((size_t)written >= bufferSize - len) {
        len = (int)bufferSize - 1; buffer[len] = '\0'; return false;
    }
    len += written;
    return true;
}

/* ===== BLE SCAN + CONNECT STORAGE ===== */
static BLEFoundDevice    bleDevices[BLE_MAX_DEVICES];
static uint8_t           bleDeviceCount   = 0;
static unsigned long     lastBleScan      = 0;
static BLEConnectedDevice bleConnDev      = {};
static char              bleTargetMac[18] = "";
static char              bleSavedMac[18]  = "";   /* NVS-persisted, auto-reconnect */
static bool              bleConnPending   = false;
static bool              bleScanRequest   = false; /* manual scan from web UI */
static NimBLEClient*               blePersistClient = nullptr;
static NimBLERemoteCharacteristic* bleCharPtrs[BLE_CHAR_MAX] = {};
static unsigned long            lastBleRefresh   = 0;

/* NUS (Nordic UART Service) — Sniffer Portal */
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
static bool                     bleIsNus           = false;
/* fixed char[] buffers — safe to write from BLE task context */
#define NUS_BUF 400
static char          bleNusAccum[NUS_BUF]     = {0};  /* partial accumulator */
static int           bleNusAccumLen           = 0;
static char          bleNusJsonReady[NUS_BUF] = {0};  /* last complete JSON */
static volatile bool bleNusNotified           = false;
static NimBLERemoteCharacteristic* bleNusRxChar     = nullptr;
static unsigned long            bleNusLastUpdateMs = 0;
static bool                     bleNusRefreshReq   = false;

class BLEScanCB : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (bleDeviceCount >= BLE_MAX_DEVICES) return;
        BLEFoundDevice& d = bleDevices[bleDeviceCount++];
        const char* nm = dev->getName().length() ? dev->getName().c_str() : "(unknown)";
        strncpy(d.name, nm, sizeof(d.name) - 1); d.name[sizeof(d.name)-1] = '\0';
        strncpy(d.mac, dev->getAddress().toString().c_str(), sizeof(d.mac) - 1); d.mac[sizeof(d.mac)-1] = '\0';
        d.rssi = dev->getRSSI();
    }
};
static BLEScanCB bleScanCB;

/* ===== NUS NOTIFICATION CALLBACK (char[] only — BLE task safe) ===== */
static void nusNotifyCallback(NimBLERemoteCharacteristic*, uint8_t* pData, size_t length, bool) {
    /* accumulate into fixed buffer */
    for (size_t i = 0; i < length; i++) {
        if (bleNusAccumLen < NUS_BUF - 1)
            bleNusAccum[bleNusAccumLen++] = (char)pData[i];
    }
    bleNusAccum[bleNusAccumLen] = '\0';

    /* find last complete JSON object */
    char* close = strrchr(bleNusAccum, '}');
    if (close) {
        char* open = bleNusAccum;
        for (char* p = bleNusAccum; p <= close; p++)
            if (*p == '{') open = p;
        if (open < close) {
            int jsonLen = (int)(close - open) + 1;
            if (jsonLen < NUS_BUF) {
                memcpy(bleNusJsonReady, open, jsonLen);
                bleNusJsonReady[jsonLen] = '\0';
                bleNusNotified = true;
            }
        }
        /* keep only bytes after last '}' */
        int rem = bleNusAccumLen - (int)(close - bleNusAccum) - 1;
        if (rem > 0) memmove(bleNusAccum, close + 1, rem);
        bleNusAccumLen = (rem > 0) ? rem : 0;
        bleNusAccum[bleNusAccumLen] = '\0';
    }
    if (bleNusAccumLen >= NUS_BUF - 1) bleNusAccumLen = 0; /* overflow guard */
}

/* extract value for "key" from JSON c-string */
static void jsonField(const char* j, const char* key, char* out, size_t outLen) {
    char needle[40]; snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char* p = strstr(j, needle);
    if (!p) { strncpy(out, "--", outLen - 1); return; }
    p += strlen(needle);
    if (*p == '"') {
        p++;
        const char* e = strchr(p, '"');
        if (!e) { strncpy(out, "--", outLen - 1); return; }
        size_t n = min((size_t)(e - p), outLen - 1);
        memcpy(out, p, n); out[n] = '\0';
    } else {
        const char* e1 = strchr(p, ',');
        const char* e2 = strchr(p, '}');
        const char* e  = (!e1 || (e2 && e2 < e1)) ? e2 : e1;
        if (!e) { strncpy(out, "--", outLen - 1); return; }
        size_t n = min((size_t)(e - p), outLen - 1);
        memcpy(out, p, n); out[n] = '\0';
    }
}

/* last BLE timestamp written to CSV — used for de-duplication */
static char bleLastSavedTs[20] = "";

/* Save BLE NUS data to /BLE-DD-MM-YYYY.csv.
   Skips write if the timestamp matches the last saved record. */
static void saveBleDataToCsv(const char* json) {
    char ts[20] = "";
    jsonField(json, "ts", ts, sizeof(ts));
    if (!ts[0] || ts[0] == '-') return;   /* no valid timestamp */

    if (strcmp(ts, bleLastSavedTs) == 0) {
        Serial.println(F("[BLE-CSV] Duplicate timestamp — skip"));
        return;
    }

    char temp[12]="", hum[12]="", tmp117[12]="", delta[12]="";
    char rain[12]="", leaf[12]="", par[12]="", soil[12]="";
    jsonField(json, "temp",   temp,   sizeof(temp));
    jsonField(json, "hum",    hum,    sizeof(hum));
    jsonField(json, "tmp117", tmp117, sizeof(tmp117));
    jsonField(json, "delta",  delta,  sizeof(delta));
    jsonField(json, "rain",   rain,   sizeof(rain));
    jsonField(json, "leaf",   leaf,   sizeof(leaf));
    jsonField(json, "par",    par,    sizeof(par));
    jsonField(json, "soil",   soil,   sizeof(soil));

    /* parse "YYYY-MM-DD HH:MM:SS" → DD/MM/YYYY and HH:MM:SS */
    char dateStr[12] = "", timeStr[10] = "";
    int yr = 0, mo = 0, dy = 0;
    if (strlen(ts) >= 19 &&
        sscanf(ts, "%4d-%2d-%2d", &yr, &mo, &dy) == 3 && yr >= 2024) {
        snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", dy, mo, yr);
        strncpy(timeStr, ts + 11, 8); timeStr[8] = '\0';
    } else if (currentTime.year > 0) {
        strncpy(dateStr, currentTime.dateStr, sizeof(dateStr));
        strncpy(timeStr, currentTime.timeStr, sizeof(timeStr));
    }

    /* BLE CSV filename — one file per day based on BLE timestamp date */
    char bleCsv[32];
    if (yr >= 2024)
        snprintf(bleCsv, sizeof(bleCsv), "/BLE-%02d-%02d-%04d.csv", dy, mo, yr);
    else if (currentTime.year > 0)
        snprintf(bleCsv, sizeof(bleCsv), "/BLE-%02u-%02u-%04u.csv",
                 currentTime.date, currentTime.month, currentTime.year);
    else
        strncpy(bleCsv, "/BLE-data.csv", sizeof(bleCsv));

    /* create file with header if it does not exist yet */
    if (!LittleFS.exists(bleCsv))
        internalMemory.write(LittleFS, bleCsv,
            "Date,Time,Temperature(C),Humidity(%),TMP117(C),DeltaT(C),"
            "Rainfall,LeafWetness,PAR,SoilMoisture\r\n");

    /* append data row */
    char row[180];
    snprintf(row, sizeof(row), "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\r\n",
             dateStr, timeStr, temp, hum, tmp117, delta, rain, leaf, par, soil);
    internalMemory.append(LittleFS, bleCsv, row);

    strncpy(bleLastSavedTs, ts, sizeof(bleLastSavedTs) - 1);
    Serial.printf("[BLE-CSV] Saved to %s: %s", bleCsv, row);
}

static void parseNusJson(const char* json) {
    if (strstr(json, "\"error\"") != nullptr) {
        Serial.printf("[BLE] NUS error response ignored: %.80s\n", json);
        return;
    }
    struct { const char* k; const char* l; } fields[] = {
        {"ts",     "Timestamp"},
        {"temp",   "Temperature (C)"},
        {"hum",    "Humidity (%)"},
        {"tmp117", "TMP117 (C)"},
        {"delta",  "Delta T (C)"},
        {"rain",   "Rainfall"},
        {"leaf",   "Leaf Wetness"},
        {"par",    "PAR Light"},
        {"soil",   "Soil Moisture"},
    };
    bleConnDev.charCount = 0;
    char val[32];
    for (auto& f : fields) {
        if (bleConnDev.charCount >= BLE_CHAR_MAX) break;
        BLECharData& cd = bleConnDev.chars[bleConnDev.charCount++];
        strncpy(cd.uuid, f.l, sizeof(cd.uuid) - 1);
        jsonField(json, f.k, val, sizeof(val));
        strncpy(cd.ascii, val, sizeof(cd.ascii) - 1);
        strncpy(cd.value, val, sizeof(cd.value) - 1);
    }
    Serial.printf("[BLE] NUS parsed %u fields from: %.80s\n",
                  bleConnDev.charCount, json);

    /* save to CSV — skip if timestamp unchanged */
    saveBleDataToCsv(json);
}

/* ===== BLE INIT ===== */
void initBLE() {
    nvs.begin("ws-cfg", true);
    bool bleEn = nvs.getBool("bleEnable", false);
    nvs.end();

    if (!bleEn) { Serial.println(F("[BLE] Client disabled")); return; }

    NimBLEDevice::init("WeatherStation-GW");
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    bleActive = true;
    strncpy(sysStatus.bleMac,
            NimBLEDevice::getAddress().toString().c_str(),
            sizeof(sysStatus.bleMac) - 1);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&bleScanCB, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    Serial.println(F("[BLE] NimBLE client ready (scanner)"));
}

/* ===== BLE CONNECT & READ (GATT or NUS) ===== */
void connectAndReadBle(const char* mac) {
    if (!bleActive || strlen(mac) < 17) return;
    Serial.printf("[BLE] Connecting to %s\n", mac);

    bleConnDev.connected = false;
    bleConnDev.charCount = 0;
    strncpy(bleConnDev.mac, mac, sizeof(bleConnDev.mac) - 1);
    bleConnDev.name[0] = '\0';

    /* find name from last scan */
    for (uint8_t i = 0; i < bleDeviceCount; i++) {
        if (strcmp(bleDevices[i].mac, mac) == 0) {
            strncpy(bleConnDev.name, bleDevices[i].name, sizeof(bleConnDev.name) - 1);
            break;
        }
    }

    if (!blePersistClient) blePersistClient = NimBLEDevice::createClient();
    if (blePersistClient->isConnected()) {
        blePersistClient->disconnect();
        delay(300);
    }

    esp_task_wdt_reset();
    if (!blePersistClient->connect(NimBLEAddress(mac))) {
        Serial.println(F("[BLE] Connect FAILED"));
        return;
    }
    Serial.println(F("[BLE] Connected. Discovering services..."));
    esp_task_wdt_reset();

    memset(bleCharPtrs, 0, sizeof(bleCharPtrs));
    bleIsNus        = false;
    bleNusRxChar    = nullptr;
    bleNusAccumLen  = 0;
    bleNusAccum[0]  = '\0';
    bleNusNotified  = false;

    /* ── NUS detection (NimBLE — same library as Sniffer Portal) ── */
    NimBLERemoteService* nusSvc = blePersistClient->getService(NUS_SERVICE_UUID);
    Serial.printf("[BLE] NUS service: %s\n", nusSvc ? "FOUND" : "not found");

    if (nusSvc) {
        NimBLERemoteCharacteristic* nusTx = nusSvc->getCharacteristic(NUS_TX_UUID);
        NimBLERemoteCharacteristic* nusRx = nusSvc->getCharacteristic(NUS_RX_UUID);
        Serial.printf("[BLE] NUS TX: %s  RX: %s\n",
            nusTx ? "found" : "missing", nusRx ? "found" : "missing");
        if (nusTx && nusTx->canNotify()) {
            nusTx->subscribe(true, nusNotifyCallback);
            Serial.println(F("[BLE] NUS TX subscribed OK"));
        }
        bleNusRxChar = nusRx;
        bleIsNus     = true;
        bleConnDev.connected = true;
        lastBleRefresh = millis();

        /* request immediate data push */
        if (bleNusRxChar && bleNusRxChar->canWrite()) {
            bleNusRxChar->writeValue((uint8_t*)"live", 4, false);
            Serial.println(F("[BLE] NUS: sent initial live request"));
        }

        strncpy(bleSavedMac, mac, sizeof(bleSavedMac) - 1);
        nvs.begin("ws-cfg", false);
        nvs.putString("bleSavedMac", mac);
        nvs.end();
        Serial.println(F("[BLE] NUS connected, waiting for notifications"));
        return;
    }

    /* ── Regular GATT enumerate (NimBLE vector iteration) ── */
    Serial.println(F("[BLE] GATT mode — reading characteristics"));
    auto* services = blePersistClient->getServices(true);
    if (services) {
        for (auto* svc : *services) {
            if (bleConnDev.charCount >= BLE_CHAR_MAX) break;
            auto* chars = svc->getCharacteristics(true);
            if (!chars) continue;
            for (auto* ch : *chars) {
                if (bleConnDev.charCount >= BLE_CHAR_MAX) break;
                if (!ch->canRead()) continue;
                esp_task_wdt_reset();
                uint8_t idx = bleConnDev.charCount;
                BLECharData& cd = bleConnDev.chars[idx];
                strncpy(cd.uuid, ch->getUUID().toString().c_str(), sizeof(cd.uuid) - 1);
                std::string raw = ch->readValue();
                char hex[64] = ""; int hl = 0;
                for (size_t b = 0; b < raw.size() && hl < 60; b++)
                    hl += snprintf(hex + hl, sizeof(hex) - hl, "%02X ", (uint8_t)raw[b]);
                strncpy(cd.value, hex, sizeof(cd.value) - 1);
                char asc[32] = ""; int al = 0;
                for (size_t b = 0; b < raw.size() && al < 30; b++)
                    asc[al++] = (raw[b] >= 0x20 && raw[b] < 0x7F) ? raw[b] : '.';
                asc[al] = '\0';
                strncpy(cd.ascii, asc, sizeof(cd.ascii) - 1);
                bleCharPtrs[idx] = ch;
                bleConnDev.charCount++;
            }
        }
    }

    bleConnDev.connected = true;
    lastBleRefresh = millis();
    Serial.printf("[BLE] GATT connected, %u readable chars\n", bleConnDev.charCount);

    strncpy(bleSavedMac, mac, sizeof(bleSavedMac) - 1);
    nvs.begin("ws-cfg", false);
    nvs.putString("bleSavedMac", mac);
    nvs.end();
}

/* ===== BLE LIVE REFRESH ===== */
void refreshBleData() {
    if (!bleConnDev.connected) return;
    if (!blePersistClient || !blePersistClient->isConnected()) {
        Serial.println(F("[BLE] Connection lost"));
        bleConnDev.connected = false;
        bleIsNus = false;
        return;
    }
    if (bleIsNus) {
        /* NUS: request fresh data; actual update arrives via nusNotifyCallback */
        if (bleNusRxChar && bleNusRxChar->canWrite()) {
            bleNusRxChar->writeValue((uint8_t*)"live", 4, false);
        }
        return;
    }
    /* GATT: re-read all readable characteristics */
    for (uint8_t i = 0; i < bleConnDev.charCount; i++) {
        NimBLERemoteCharacteristic* ch = bleCharPtrs[i];
        if (!ch) continue;
        esp_task_wdt_reset();
        std::string raw = ch->readValue();
        BLECharData& cd = bleConnDev.chars[i];
        char hex[64] = ""; int hl = 0;
        for (size_t b = 0; b < raw.size() && hl < 60; b++)
            hl += snprintf(hex + hl, sizeof(hex) - hl, "%02X ", (uint8_t)raw[b]);
        strncpy(cd.value, hex, sizeof(cd.value) - 1);
        char asc[32] = ""; int al = 0;
        for (size_t b = 0; b < raw.size() && al < 30; b++)
            asc[al++] = (raw[b] >= 0x20 && raw[b] < 0x7F) ? raw[b] : '.';
        asc[al] = '\0';
        strncpy(cd.ascii, asc, sizeof(cd.ascii) - 1);
    }
}

/* ===== BLE SCAN ===== */
void doBleScan() {
    if (!bleActive) return;
    nvs.begin("ws-cfg", true);
    bool centralEn = nvs.getBool("bleEnable", false);
    nvs.end();
    if (!centralEn) return;

    Serial.println(F("[BLE] Scanning 3s..."));
    bleDeviceCount = 0;
    NimBLEScan* scan = NimBLEDevice::getScan();
    esp_task_wdt_reset();
    scan->start(3, false);
    scan->clearResults();
    esp_task_wdt_reset();
    Serial.printf("[BLE] Found %u device(s)\n", bleDeviceCount);
}

/* ===== INFLUXDB v2 SEND ===== */
void sendToInfluxDB(timeStruct* t, SensorData* s) {
    nvs.begin("ws-cfg", true);
    bool   en  = nvs.getBool  ("influxEn",     false);
    String host = nvs.getString("influxHost",   "");
    uint32_t port = nvs.getUInt("influxPort",   8086);
    String tok  = nvs.getString("influxToken",  "");
    String org  = nvs.getString("influxOrg",    "");
    String bkt  = nvs.getString("influxBucket", "");
    nvs.end();

    if (!en || host.length() == 0 || tok.length() == 0) return;

    char url[160];
    snprintf(url, sizeof(url), "http://%s:%u/api/v2/write?org=%s&bucket=%s&precision=s",
             host.c_str(), port, org.c_str(), bkt.c_str());

    char line[768];
    snprintf(line, sizeof(line),
        "weather_station,station=Station_1 "
        "soil_humi=%.1f,soil_temp=%.1f,soil_ec=%u,soil_ph=%.1f,"
        "soil_N=%u,soil_P=%u,soil_K=%u,"
        "wind_speed=%.1f,wind_dir=%u,air_humi=%.1f,air_temp=%.1f,"
        "co2=%u,pressure=%.1f,illuminance=%lu,rainfall=%.1f,solar=%u,"
        "battery=%u",
        s->soil_humi/10.0, s->soil_temp/10.0, s->soil_ec, s->soil_ph/10.0,
        s->soil_N, s->soil_P, s->soil_K,
        s->windSpeed/10.0, s->windDir_Deg, s->air_humidity/10.0, s->air_temperature/10.0,
        s->CO2, s->pressure/10.0, (unsigned long)s->illuminance, s->rainfall/10.0, s->solar,
        batteryVoltage);

    /* Append Sniffer WatchDog measurement when NUS connected and has data */
    if (bleIsNus && bleConnDev.connected && bleConnDev.charCount >= 9) {
        char suf[256];
        snprintf(suf, sizeof(suf),
            "\nsniffer_watchdog,station=Station_1,mac=%s "
            "temp=%s,hum=%s,tmp117=%s,delta=%s,"
            "rain=%s,leaf=%s,par=%s,soil=%s",
            bleConnDev.mac,
            bleConnDev.chars[1].ascii,  /* Temperature (C)  */
            bleConnDev.chars[2].ascii,  /* Humidity (%)     */
            bleConnDev.chars[3].ascii,  /* TMP117 (C)       */
            bleConnDev.chars[4].ascii,  /* Delta T (C)      */
            bleConnDev.chars[5].ascii,  /* Rainfall         */
            bleConnDev.chars[6].ascii,  /* Leaf Wetness     */
            bleConnDev.chars[7].ascii,  /* PAR Light        */
            bleConnDev.chars[8].ascii); /* Soil Moisture    */
        strncat(line, suf, sizeof(line) - strlen(line) - 1);
        Serial.println(F("[INFLUX] Appending sniffer_watchdog measurement"));
    }

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", String("Token ") + tok);
    http.addHeader("Content-Type",  "text/plain; charset=utf-8");
    esp_task_wdt_reset();
    int code = http.POST(line);
    Serial.printf("[INFLUX] POST %d\n", code);
    http.end();
}

/* ===== REMOTE OTA CHECK (via GSM) ===== */
static unsigned long lastOtaCheckMs = 0;
static bool          otaCheckNow    = false;

static void parseOtaUrl(const String& url, String& host, uint16_t& port) {
    String s = url;
    if      (s.startsWith("http://"))  s = s.substring(7);
    else if (s.startsWith("https://")) s = s.substring(8);
    int slash = s.indexOf('/');
    String hp = (slash >= 0) ? s.substring(0, slash) : s;
    int colon = hp.lastIndexOf(':');
    if (colon >= 0) { host = hp.substring(0, colon); port = (uint16_t)hp.substring(colon+1).toInt(); }
    else            { host = hp; port = 80; }
}

/* Raw HTTP/1.1 GET over TinyGsmClient.
   Parses status code, Content-Length, x-MD5 headers.
   Returns HTTP status code, or negative on connect/parse failure.
   Leaves client connected so caller can read the body. */
static int gsmHttpGet(TinyGsmClient& client,
                      const char* host, uint16_t port, const char* path,
                      const char* ver, const char* dev, const char* prj, const char* pw,
                      int& contentLen, char* md5out) {
    contentLen = -1; md5out[0] = '\0';
    /* retry TCP connect up to 3 times */
    bool tcpOk = false;
    for (int att = 1; att <= 3 && !tcpOk; att++) {
        feedWDT();
        tcpOk = client.connect(host, port);
        if (!tcpOk && att < 3) { Serial.printf("[OTA] TCP connect attempt %d failed, retry...\n", att); delay(3000); feedWDT(); }
    }
    if (!tcpOk) {
        Serial.printf("[OTA] TCP connect failed %s:%u after 3 attempts\n", host, port); return -1;
    }
    String req = String("GET ") + path + " HTTP/1.1\r\n"
               + "Host: " + host + "\r\n"
               + "Connection: close\r\n"
               + "x-ESP32-version: " + ver + "\r\n"
               + "x-ESP32-device: "  + dev + "\r\n"
               + "x-ESP32-project: " + prj + "\r\n";
    if (pw && pw[0]) req += String("x-ESP32-password: ") + pw + "\r\n";
    req += "\r\n";
    client.print(req);
    /* wait for data */
    uint32_t t0 = millis();
    while (!client.available() && millis() - t0 < 15000) { feedWDT(); delay(200); }
    if (!client.available()) { client.stop(); return -2; }
    /* status line: "HTTP/1.1 NNN ..." */
    String sl = client.readStringUntil('\n');
    int sp = sl.indexOf(' ');
    int code = (sp >= 0) ? sl.substring(sp+1, sp+4).toInt() : -3;
    /* response headers */
    while (client.connected() || client.available()) {
        feedWDT();
        String line = client.readStringUntil('\n'); line.trim();
        if (line.isEmpty()) break;
        String low = line; low.toLowerCase();
        if      (low.startsWith("content-length:")) { String v = line.substring(15); v.trim(); contentLen = v.toInt(); }
        else if (low.startsWith("x-md5:"))          { String v = line.substring(6);  v.trim(); strncpy(md5out, v.c_str(), 32); md5out[32] = '\0'; }
    }
    return code;
}

void checkRemoteOTA() {
    /* read all NVS settings in one open/close */
    nvs.begin("ws-cfg", true);
    String   srv       = nvs.getString("otaserver",   "");
    uint32_t ivH       = nvs.getUInt  ("otainterval", OTA_DEFAULT_INTERVAL_H);
    bool     bootCheck = nvs.getBool  ("otaboot",     false);
    String   prj       = nvs.getString("otaproject",  OTA_DEFAULT_PROJECT);
    String   dev       = nvs.getString("otadevice",   OTA_DEFAULT_DEVICE);
    String   dlpw      = nvs.getString("otadlpass",   OTA_DEFAULT_DLPASS);
    nvs.end();

    if (srv.length() == 0) return;
    if (!bootCheck && ivH == 0) return;
    if (!bootCheck && !otaCheckNow && lastOtaCheckMs != 0 &&
        (millis() - lastOtaCheckMs) < (unsigned long)ivH * 3600000UL) return;
    otaCheckNow    = false;
    lastOtaCheckMs = millis();

    if (!gsmAvailable || !gsmHandler.getModem()) {
        Serial.println(F("[OTA] GSM unavailable, skip")); return;
    }

    String host; uint16_t port;
    parseOtaUrl(srv, host, port);
    Serial.printf("[OTA] Checking %s:%u via GSM\n", host.c_str(), port);

    /* Use mux 1 (GsmHandler MQTT uses mux 0 — avoid conflict) */
    /* Force-close mux 1 before connecting in case it was left open */
    gsmHandler.getModem()->sendAT("+CIPCLOSE=1");
    gsmHandler.getModem()->waitResponse(3000);
    feedWDT();

    /* /update returns 304 (up to date), 200+binary (new fw), or 401 (auth fail) */
    char md5[33]; int contentLen;
    TinyGsmClient dlClient(*gsmHandler.getModem(), 1);
    int code = gsmHttpGet(dlClient, host.c_str(), port, "/update",
                          FIRMWARE_VERSION,
                          dev.length() ? dev.c_str() : OTA_DEFAULT_DEVICE,
                          prj.c_str(), dlpw.c_str(), contentLen, md5);
    Serial.printf("[OTA] /update HTTP %d len=%d md5=%s\n", code, contentLen, md5);
    if (code == 401) { Serial.println(F("[OTA] Auth failed — check download password")); dlClient.stop(); return; }

    if (code == 200 &&
        Update.begin(contentLen > 0 ? (size_t)contentLen : UPDATE_SIZE_UNKNOWN)) {
        if (md5[0]) Update.setMD5(md5);
        uint8_t chunk[512]; size_t written = 0;
        uint32_t t0 = millis();
        bool writeOk = true;
        while ((dlClient.connected() || dlClient.available()) &&
               (contentLen < 0 || written < (size_t)contentLen) &&
               millis() - t0 < 300000UL) {         /* 5-min no-data timeout */
            feedWDT();
            int avail = dlClient.available();
            if (avail > 0) {
                size_t r = dlClient.readBytes(chunk,
                    min((size_t)avail, sizeof(chunk)));
                if (r > 0) {
                    if (Update.write(chunk, r) != r) { writeOk = false; break; }
                    written += r;
                    t0 = millis();
                    Serial.printf("[OTA] Written %u / %d bytes\n",
                                  (unsigned)written, contentLen);
                }
            } else { delay(20); }
        }
        dlClient.stop();
        if (!writeOk) { Update.abort(); Serial.println(F("[OTA] Write failed, aborted")); return; }
        if (Update.end(true)) {
            Serial.printf("[OTA] Done %u bytes — rebooting\n", (unsigned)written);
            nvs.begin("ws-cfg", false);
            nvs.putString("lastota", currentTime.year > 0
                ? String(currentTime.dateStr) + " " + currentTime.timeStr : "auto");
            nvs.end();
            delay(500); ESP.restart();
        } else {
            Serial.printf("[OTA] Update.end error %u\n", (unsigned)Update.getError());
        }
    } else if (code == 304) {
        Serial.println(F("[OTA] Already up to date"));
    }
    dlClient.stop();
}

/* ===== SETUP ===== */
void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    delay(500);

    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    Serial.println(F("[WDT] Watchdog initialized"));
    Serial.printf("[FW] All-in-One Weather Station v%s\n", FIRMWARE_VERSION);

    pinMode(BATT_PIN, INPUT);
    pinMode(PIN_DONE, OUTPUT);
    digitalWrite(PIN_DONE, LOW);

    RS485Serial.begin(SERIAL_RS485, SERIAL_8N1, RS_485_RX_PIN, RS_485_TX_PIN);
    modbusSensor.begin(&RS485Serial);
    Serial.println(F("[SERIAL] RS485 UART1 started"));

    GSM_SERIAL.begin(SERIAL_GSM, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    Serial.println(F("[SERIAL] GSM UART0 started"));

    feedWDT();
    if (!LittleFS.begin(true)) Serial.println(F("[FS] LittleFS FAILED"));
    else Serial.println(F("[FS] LittleFS mounted"));

    feedWDT();
    initBLE();
    feedWDT();

    /* Load saved BLE MAC → auto-reconnect on AP start */
    nvs.begin("ws-cfg", true);
    String savedMac = nvs.getString("bleSavedMac", "");
    nvs.end();
    if (savedMac.length() == 17) {
        strncpy(bleSavedMac, savedMac.c_str(), sizeof(bleSavedMac) - 1);
        strncpy(bleTargetMac, bleSavedMac, sizeof(bleTargetMac) - 1);
        bleConnPending = true;
        Serial.printf("[BLE] Auto-reconnect scheduled: %s\n", bleSavedMac);
    }

    /* Load last OTA string from Preferences */
    nvs.begin("ws-cfg", true);
    String lastOta = nvs.getString("lastota", "Never");
    nvs.end();

    /* Populate SystemStatus */
    sysStatus.sensor    = &modbusSensor.currentSensor;
    sysStatus.time      = &currentTime;
    sysStatus.battMv    = &batteryVoltage;
    sysStatus.gsmAvail  = &gsmAvailable;
    sysStatus.gsmRssi   = &gsmRssi;
    sysStatus.bleActive = &bleActive;
    sysStatus.memory    = &internalMemory;
    sysStatus.gsm            = &gsmHandler;
    sysStatus.bleDevices     = bleDevices;
    sysStatus.bleDeviceCount = &bleDeviceCount;
    sysStatus.bleConnDev     = &bleConnDev;
    sysStatus.bleTargetMac   = bleTargetMac;
    sysStatus.bleConnPending = &bleConnPending;
    sysStatus.bleSavedMac    = bleSavedMac;
    sysStatus.bleScanRequest = &bleScanRequest;
    sysStatus.bleIsNus          = &bleIsNus;
    sysStatus.bleNusLastMs      = &bleNusLastUpdateMs;
    sysStatus.bleNusRefreshReq  = &bleNusRefreshReq;
    sysStatus.otaCheckNow       = &otaCheckNow;
    strncpy(sysStatus.bleMac, "N/A", sizeof(sysStatus.bleMac));
    strncpy(sysStatus.lastOtaStr, lastOta.c_str(), sizeof(sysStatus.lastOtaStr) - 1);
    sysStatus.lastOtaStr[sizeof(sysStatus.lastOtaStr) - 1] = '\0';

    batteryVoltage = batteryRead();
    Serial.printf("[BATT] %u mV\n", batteryVoltage);
    Serial.println(F("===== SETUP COMPLETE ====="));
}

/* ===== DAILY CSV HELPER ===== */
void updateDailyCsv() {
    if (currentTime.year == 0) return;
    snprintf(dailyCsv, sizeof(dailyCsv), "/%02u-%02u-%04u.csv",
             currentTime.date, currentTime.month, currentTime.year);
    if (!LittleFS.exists(dailyCsv)) {
        internalMemory.write(LittleFS, dailyCsv,
            "Date,Time,"
            "Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,"
            "WindSpeed,WindDirection,Air_Humidity,Air_Temperature,"
            "CO2,Pressure,Illuminance,Rainfall,Solar\r\n");
    }
}

/* ===== MAIN LOOP ===== */
void loop() {
    feedWDT();

    if (currentState != prevState) {
        stateEntryTime = millis(); prevState = currentState;
    }

    unsigned long stateElapsed = millis() - stateEntryTime;
    systemState   fallbackState = currentState;
    unsigned long maxDuration   = 0;
    bool timeoutForce = false;

    switch (currentState) {
        case STATE_GSM_INIT:  maxDuration = GSM_INIT_TIMEOUT_MS;  fallbackState = STATE_NTP;       break;
        case STATE_NTP:       maxDuration = TIME_WAIT_TIMEOUT;     fallbackState = STATE_SOIL;      break;
        case STATE_SOIL:      maxDuration = SOIL_TIMEOUT;          fallbackState = STATE_WEATHER;   break;
        case STATE_WEATHER:   maxDuration = WEATHER_TIMEOUT;       fallbackState = STATE_SAVE;      break;
        case STATE_SAVE:      maxDuration = 10000;                 fallbackState = STATE_WIFI_AP;   break;
        case STATE_WIFI_AP:   maxDuration = WIFI_AP_TIMEOUT;       fallbackState = STATE_GSM_INIT;  break;
        case STATE_RECONNECT: maxDuration = RECONNECT_TIMEOUT;     fallbackState = STATE_FINISH;    break;
        case STATE_PUBLISH:   maxDuration = MQTT_PUBLISH_TIMEOUT;  fallbackState = STATE_FINISH;    break;
        case STATE_FINISH:    maxDuration = 0; break;
        default:              maxDuration = 0; break;
    }

    if (maxDuration > 0 && stateElapsed >= maxDuration) {
        Serial.printf("[TMO] State %d timeout %lu ms -> %d\n",
                      currentState, stateElapsed, fallbackState);
        currentState = fallbackState;
        stateEntryTime = millis(); prevState = currentState;
        timeoutForce = true;
    }
    if (timeoutForce) return;

    switch (currentState) {

        /* ── GSM INIT ── */
        case STATE_GSM_INIT: {
            delay(2000);

            if (!gsmHandler.init(GSM_SERIAL)) {
                gsmAvailable = false; currentState = STATE_NTP; break;
            }
            if (!gsmHandler.connectNetwork()) {
                gsmAvailable = false; currentState = STATE_NTP; break;
            }
            gsmAvailable = true;
            gsmRssi = gsmHandler.getSignalQuality();
            publishHeartbeat();
            currentState = STATE_NTP;
            break;
        }

        /* ── NTP ── */
        case STATE_NTP: {
            timeSynced = false;
            if (gsmAvailable && gsmHandler.getNetworkTime(&currentTime)) {
                timeSynced = true;
                Serial.printf("[NTP] %s %s\n", currentTime.dateStr, currentTime.timeStr);
            }
            if (!timeSynced) {
                if (!readLastTimeFromBackup(&currentTime)) {
                    memset(&currentTime, 0, sizeof(timeStruct));
                    snprintf(currentTime.dateStr, sizeof(currentTime.dateStr), "00/00/0000");
                    snprintf(currentTime.timeStr, sizeof(currentTime.timeStr), "00:00:00");
                }
                incrementTime(&currentTime, TIME_INCREMENT_MINUTES);
                Serial.printf("[NTP] Fallback: %s %s\n", currentTime.dateStr, currentTime.timeStr);
            }
            updateDailyCsv();
            if (gsmAvailable) checkRemoteOTA();
            currentState = STATE_SOIL;
            break;
        }

        /* ── SOIL ── */
        case STATE_SOIL: {
            if (gsmAvailable) gsmHandler.mqttLoop();
            if (soilStateStart == 0) soilStateStart = millis();
            if (millis() - soilStateStart < SOIL_SETTLE_DELAY) { feedWDT(); delay(10); break; }
            if (modbusSensor.read(SOIL, SOIL_SLAVE_ID, SOIL_REGISTER_ADDR,
                                  SOIL_REGISTER_LEN, &RS485Serial)) {
                soilStateStart = 0; currentState = STATE_WEATHER; break;
            }
            if (millis() - soilStateStart >= SOIL_TIMEOUT) {
                soilStateStart = 0; currentState = STATE_WEATHER;
            }
            break;
        }

        /* ── WEATHER ── */
        case STATE_WEATHER: {
            if (gsmAvailable) gsmHandler.mqttLoop();
            if (weatherStateStart == 0) weatherStateStart = millis();
            if (millis() - weatherStateStart < WEATHER_SETTLE_DELAY) { feedWDT(); delay(10); break; }
            if (modbusSensor.read(WEATHER, WEATHER_SLAVE_ID, WEATHER_REGISTER_ADDR,
                                  WEATHER_REGISTER_LEN, &RS485Serial)) {
                weatherStateStart = 0; currentState = STATE_SAVE; break;
            }
            if (millis() - weatherStateStart >= WEATHER_TIMEOUT) {
                clearWeatherData(); weatherStateStart = 0; currentState = STATE_SAVE;
            }
            break;
        }

        /* ── SAVE ── */
        case STATE_SAVE: {
            if (gsmAvailable) gsmHandler.mqttLoop();

            nvs.begin("ws-cfg", true);
            uint8_t rollPct = (uint8_t)nvs.getUInt("memRollover", STORAGE_ROLLOVER_PERCENT);
            bool rollEn = nvs.getBool("memRolloverEn", true);
            nvs.end();
            if (rollEn && internalMemory.isUsageOverThreshold(rollPct)) {
                String last = getLastDataLine(dailyCsv);
                LittleFS.remove(dailyCsv);
                updateDailyCsv();
                if (last.length()) {
                    String e = last + "\r\n";
                    internalMemory.append(LittleFS, dailyCsv, e.c_str());
                }
            }

            if (internalMemory.saveData(dailyCsv, &currentTime, &modbusSensor.currentSensor))
                Serial.println(F("[SAVE] Daily CSV OK"));

            /* InfluxDB v2 push */
            if (gsmAvailable) sendToInfluxDB(&currentTime, &modbusSensor.currentSensor);

            currentState = STATE_RECONNECT;  /* AP already ran at boot */
            break;
        }

        /* ── WIFI AP ── */
        case STATE_WIFI_AP: {
            if (!wifiApStarted) {
                wifiApStarted = true;
                lastBleScan   = 0;
                wifiApServer.begin(&sysStatus);
            }
            wifiApServer.handleClient();

            /* Manual scan request from web UI */
            if (bleScanRequest) {
                bleScanRequest = false;
                doBleScan();
            }

            /* BLE connect / auto-reconnect request */
            if (bleConnPending) {
                bleConnPending = false;
                connectAndReadBle(bleTargetMac);
            }

            /* NUS: process incoming notification from Sniffer Portal */
            if (bleIsNus && bleNusNotified) {
                bleNusNotified = false;
                parseNusJson(bleNusJsonReady);
                bleNusLastUpdateMs = millis();
            }

            /* NUS: web UI requested an immediate data push */
            if (bleIsNus && bleNusRefreshReq) {
                bleNusRefreshReq = false;
                if (bleNusRxChar && bleNusRxChar->canWrite())
                    bleNusRxChar->writeValue((uint8_t*)"live", 4, false);
            }

            /* Live BLE data refresh every 10 s (NUS) / 5 s (GATT) */
            unsigned long bleRefreshMs = bleIsNus ? 10000UL : 5000UL;
            if (bleConnDev.connected && millis() - lastBleRefresh >= bleRefreshMs) {
                lastBleRefresh = millis();
                refreshBleData();
            }

            /* BLE scan every 30 s (skip if actively connected) */
            if (bleActive && !bleConnDev.connected &&
                millis() - lastBleScan >= 30000UL) {
                lastBleScan = millis();
                doBleScan();
            }

            /* OTA update requested from web UI — exit AP immediately */
            if (otaCheckNow) {
                wifiApServer.stop();
                wifiApStarted = false;
                currentState  = STATE_GSM_INIT;
                break;
            }

            if (wifiApServer.isTimedOut()) {
                wifiApServer.stop();
                wifiApStarted = false;
                currentState  = STATE_GSM_INIT;
            }
            delay(2);
            break;
        }

        /* ── RECONNECT ── */
        case STATE_RECONNECT: {
            currentState = STATE_FINISH;
            break;
        }

        /* ── PUBLISH ── */
        case STATE_PUBLISH: {
            currentState = STATE_FINISH;
            break;
        }

        /* ── FINISH ── */
        case STATE_FINISH: {
            Serial.println(F("[DONE] Pulsing TPL5110"));
            delay(3000);
            Serial.flush();
            digitalWrite(PIN_DONE, LOW); delay(50);
            digitalWrite(PIN_DONE, HIGH); delay(2000);
            Serial.println(F("[WARN] TPL5110 did not cut power"));
            Serial.flush();
            while (1) { feedWDT(); delay(1000); }
            break;
        }

        default: currentState = STATE_FINISH; break;
    }
}

/* ===== HEARTBEAT ===== */
bool publishHeartbeat() {
    if (!gsmAvailable || !gsmHandler.mqttConnect()) return false;
    char payload[160];
    snprintf(payload, sizeof(payload),
        "{\"n\":0,\"alive\":1,\"vt\":%u,\"heap\":%u,\"uptime\":%lu,\"gsm_rssi\":%d,\"fw\":\"%s\"}",
        batteryVoltage, ESP.getFreeHeap(), millis() / 1000, gsmRssi, FIRMWARE_VERSION);
    return gsmHandler.mqttPublish(MQTT_PONG_TOPIC, payload);
}

/* ===== BUILD COMPACT JSON ===== */
bool buildCompactJSON(DataRecord* records, int count, uint16_t battVoltage, int gsmRssiVal,
                      char* buffer, size_t bufferSize) {
    if (bufferSize == 0 || count <= 0) return false;
    buffer[0] = '\0';
    int valid = 0;
    for (int i = 0; i < count; i++) if (records[i].valid == 1) valid++;
    if (valid == 0) return false;

    int len = 0;
    if (!appendToBuffer(buffer, bufferSize, len,
        "{\"n\":1,\"seq\":%u,\"vt\":%u,\"srs\":%d,\"d\":\"%02u%02u%02u\",\"r\":[",
        publishSequence++, battVoltage, gsmRssiVal,
        records[0].date, records[0].month, records[0].year % 100)) return false;

    int idx = 0;
    for (int i = 0; i < count; i++) {
        if (records[i].valid != 1) continue;
        const SensorData& d = records[i].data;
        if (idx > 0 && !appendToBuffer(buffer, bufferSize, len, ",")) return false;
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
            d.CO2, d.pressure, (unsigned long)d.illuminance, d.rainfall, d.solar)) return false;
        idx++;
    }
    if (!appendToBuffer(buffer, bufferSize, len, "]")) return false;

    /* Append BLE / Sniffer Portal data when available */
    if (bleConnDev.connected && bleConnDev.charCount > 0) {
        if (bleIsNus) {
            /* NUS (Sniffer Portal) — compact named fields */
            static const char* nusKeys[] = {
                "ts","temp","hum","tmp117","delta","rain","leaf","par","soil"
            };
            if (!appendToBuffer(buffer, bufferSize, len,
                ",\"sniffer\":{\"mac\":\"%s\",\"name\":\"%s\"",
                bleConnDev.mac, bleConnDev.name)) return false;
            for (uint8_t i = 0; i < bleConnDev.charCount && i < 9; i++) {
                if (!appendToBuffer(buffer, bufferSize, len,
                    ",\"%s\":\"%s\"", nusKeys[i],
                    bleConnDev.chars[i].ascii)) return false;
            }
            if (!appendToBuffer(buffer, bufferSize, len, "}")) return false;
        } else {
            /* GATT — generic chars array */
            if (!appendToBuffer(buffer, bufferSize, len,
                ",\"ble\":{\"mac\":\"%s\",\"name\":\"%s\",\"chars\":[",
                bleConnDev.mac, bleConnDev.name)) return false;
            for (uint8_t i = 0; i < bleConnDev.charCount; i++) {
                if (i > 0 && !appendToBuffer(buffer, bufferSize, len, ",")) return false;
                if (!appendToBuffer(buffer, bufferSize, len,
                    "{\"u\":\"%s\",\"v\":\"%s\"}",
                    bleConnDev.chars[i].uuid,
                    bleConnDev.chars[i].ascii)) return false;
            }
            if (!appendToBuffer(buffer, bufferSize, len, "]}")) return false;
        }
    }

    return appendToBuffer(buffer, bufferSize, len, "}");
}

/* ===== UTILITIES ===== */
void feedWDT() { esp_task_wdt_reset(); }

void clearWeatherData() {
    auto& s = modbusSensor.currentSensor;
    s.windSpeed = s.windDir_Deg = s.air_humidity = 0;
    s.air_temperature = s.CO2 = s.pressure = 0;
    s.illuminance = s.rainfall = s.solar = 0;
}

void incrementTime(timeStruct* t, uint8_t addMinutes) {
    if (t->date == 0 || t->month == 0 || t->month > 12 || t->year == 0) {
        t->date = 1; t->month = 1; t->year = 2024; t->hour = 0; t->minute = 0; t->second = 0;
    }
    int total = t->hour * 60 + t->minute + addMinutes;
    int carry = 0;
    while (total >= 1440) { total -= 1440; carry++; }
    t->hour = (uint8_t)(total / 60);
    t->minute = (uint8_t)(total % 60);
    t->second = 0;
    static const uint8_t dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    t->date += carry;
    uint8_t mi = (t->month >= 1 && t->month <= 12) ? (t->month - 1) : 0;
    while (t->date > dim[mi]) {
        t->date -= dim[mi]; t->month++;
        mi = (t->month >= 1 && t->month <= 12) ? (t->month - 1) : 0;
        if (t->month > 12) { t->month = 1; t->year++; mi = 0; }
    }
    snprintf(t->dateStr, sizeof(t->dateStr), "%02u/%02u/%04u", t->date, t->month, t->year);
    snprintf(t->timeStr, sizeof(t->timeStr), "%02u:%02u:%02u", t->hour, t->minute, t->second);
}

bool readLastTimeFromBackup(timeStruct* outTime) {
    if (!LittleFS.exists(dailyCsv)) return false;
    File f = LittleFS.open(dailyCsv, "r"); if (!f) return false;
    f.readStringUntil('\n');
    String last = ""; uint16_t n = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n'); n++;
        if ((n % 16) == 0) { feedWDT(); delay(0); }
        line.trim(); if (line.length()) last = line;
    }
    f.close();
    if (!last.length()) return false;
    int fc = last.indexOf(','); if (fc < 0) return false;
    int sc = last.indexOf(',', fc+1); if (sc < 0) return false;
    String ds = last.substring(0, fc);
    String ts = last.substring(fc+1, sc);
    int d1 = ds.indexOf('/'), d2 = ds.lastIndexOf('/');
    if (d1 < 0 || d2 < 0 || d1 == d2) return false;
    outTime->date  = (uint8_t)ds.substring(0, d1).toInt();
    outTime->month = (uint8_t)ds.substring(d1+1, d2).toInt();
    outTime->year  = (uint16_t)ds.substring(d2+1).toInt();
    int t1 = ts.indexOf(':'), t2 = ts.lastIndexOf(':');
    if (t1 < 0 || t2 < 0 || t1 == t2) return false;
    outTime->hour   = (uint8_t)ts.substring(0, t1).toInt();
    outTime->minute = (uint8_t)ts.substring(t1+1, t2).toInt();
    outTime->second = (uint8_t)ts.substring(t2+1).toInt();
    snprintf(outTime->dateStr, sizeof(outTime->dateStr), "%02u/%02u/%04u",
             outTime->date, outTime->month, outTime->year);
    snprintf(outTime->timeStr, sizeof(outTime->timeStr), "%02u:%02u:%02u",
             outTime->hour, outTime->minute, outTime->second);
    return true;
}

String getLastDataLine(const char* fileName) {
    if (!LittleFS.exists(fileName)) return "";
    File f = LittleFS.open(fileName, "r"); if (!f) return "";
    f.readStringUntil('\n');
    String last = ""; uint16_t n = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n'); n++;
        if ((n % 16) == 0) { feedWDT(); delay(0); }
        line.trim(); if (line.length()) last = line;
    }
    f.close(); return last;
}

void checkFile(const char* fileName) {
    if (!LittleFS.exists(fileName)) {
        Serial.printf("[FS] Creating %s\n", fileName);
        internalMemory.write(LittleFS, fileName,
            "Date,Time,"
            "Soil_Humidity,Soil_Temperature,EC,PH,N,P,K,"
            "WindSpeed,WindDirection,Air_Humidity,Air_Temperature,"
            "CO2,Pressure,Illuminance,Rainfall,Solar\r\n");
    }
}