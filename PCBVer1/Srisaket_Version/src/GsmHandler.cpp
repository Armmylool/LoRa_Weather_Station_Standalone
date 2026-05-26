#include "GsmHandler.h"
#include <esp_task_wdt.h>

GsmHandler::GsmHandler()
    : _modem(nullptr), _client(nullptr), _mqtt(nullptr),
      _initialized(false), _networkConnected(false),
      _mqttPort(MQTT_PORT) {
    strncpy(_mqttBroker,   MQTT_BROKER,    sizeof(_mqttBroker)   - 1);
    strncpy(_mqttUser,     MQTT_USER,      sizeof(_mqttUser)     - 1);
    strncpy(_mqttPass,     MQTT_PASSWORD,  sizeof(_mqttPass)     - 1);
    strncpy(_mqttClientId, MQTT_CLIENT_ID, sizeof(_mqttClientId) - 1);
}

GsmHandler::~GsmHandler() {
    if (_mqtt) { delete _mqtt; _mqtt = nullptr; }
    if (_client) { delete _client; _client = nullptr; }
    if (_modem) { delete _modem; _modem = nullptr; }
}

bool GsmHandler::init(Stream& serial) {
    if (_initialized) return true;

    _modem = new TinyGsm(serial);
    _client = new TinyGsmClient(*_modem);
    _mqtt = new PubSubClient(*_client);

    _mqtt->setServer(_mqttBroker, _mqttPort);
    _mqtt->setBufferSize(2048);
    _mqtt->setKeepAlive(90);
    _mqtt->setSocketTimeout(45);

    Serial.println(F("[GSM] Initializing modem..."));
    if (!_modem->init()) {
        Serial.println(F("[GSM] Modem init FAILED"));
        return false;
    }

    String info = _modem->getModemInfo();
    Serial.printf("[GSM] Modem: %s\n", info.c_str());

    if (_modem->getSimStatus() != 1) {
        Serial.println(F("[GSM] SIM card not detected!"));
        return false;
    }
    Serial.println(F("[GSM] SIM card OK"));

    _initialized = true;
    return true;
}

bool GsmHandler::connectNetwork() {
    if (!_initialized) return false;

    Serial.printf("[GSM] Connecting to GPRS (APN: %s)...\n", GSM_APN);

    _modem->sendAT("+CFUN=1");
    _modem->waitResponse(5000);

    /* Manual network wait — feeds WDT every 2s to prevent 30s WDT reset */
    Serial.println(F("[GSM] Waiting for network..."));
    unsigned long netStart = millis();
    while (millis() - netStart < GSM_INIT_TIMEOUT_MS) {
        esp_task_wdt_reset();
        if (_modem->isNetworkConnected()) break;
        delay(2000);
    }

    if (!_modem->isNetworkConnected()) {
        Serial.println(F("[GSM] Network registration FAILED"));
        return false;
    }
    Serial.println(F("[GSM] Network registered"));

    if (!_modem->gprsConnect(GSM_APN, "", "")) {
        Serial.println(F("[GSM] GPRS connect FAILED"));
        return false;
    }

    Serial.printf("[GSM] Signal: %d\n", _modem->getSignalQuality());
    String ip = _modem->getLocalIP();
    Serial.printf("[GSM] Local IP: %s\n", ip.c_str());

    _networkConnected = true;
    return true;
}

bool GsmHandler::getNetworkTime(timeStruct* outTime) {
    if (!_initialized || !_networkConnected) return false;

    String dt = _modem->getGSMDateTime(DATE_FULL);
    if (dt.length() >= 14 && parseGsmDateTime(dt, outTime)) {
        Serial.printf("[NTP] Network time: %s %s\n", outTime->dateStr, outTime->timeStr);
        return true;
    }

    Serial.println(F("[NTP] RTC time invalid, syncing via NTP..."));
    if (!syncNtp()) {
        Serial.println(F("[NTP] NTP sync FAILED"));
        return false;
    }

    delay(1000);
    dt = _modem->getGSMDateTime(DATE_FULL);
    if (dt.length() >= 14 && parseGsmDateTime(dt, outTime)) {
        Serial.printf("[NTP] NTP time: %s %s\n", outTime->dateStr, outTime->timeStr);
        return true;
    }

    Serial.println(F("[NTP] Time read FAILED after sync"));
    return false;
}

bool GsmHandler::mqttConnect() {
    if (!_initialized || !_networkConnected || !_mqtt) return false;

    if (_mqtt->connected()) return true;

    Serial.printf("[MQTT] Connecting to %s:%u...\n", _mqttBroker, _mqttPort);

    unsigned long start = millis();
    while (!_mqtt->connected() && (millis() - start < MQTT_PUBLISH_TIMEOUT)) {
        esp_task_wdt_reset();
        if (_mqtt->connect(_mqttClientId, _mqttUser, _mqttPass)) {
            Serial.println(F("[MQTT] Connected!"));
            return true;
        }
        Serial.printf("[MQTT] Connect failed, rc=%d. Retrying...\n", _mqtt->state());
        delay(2000);
    }

    Serial.println(F("[MQTT] Connection FAILED (timeout)"));
    return false;
}

bool GsmHandler::mqttPublish(const char* topic, const char* payload) {
    if (!_mqtt || !_mqtt->connected()) {
        Serial.printf("[MQTT] Not connected (state=%d)\n", _mqtt ? _mqtt->state() : -99);
        return false;
    }

    _mqtt->loop();

    bool ok = _mqtt->publish(topic, payload, false);
    if (ok) {
        Serial.printf("[MQTT] Published %u bytes to %s\n", strlen(payload), topic);
        for (int i = 0; i < 10; i++) {
            delay(200);
            _mqtt->loop();
            esp_task_wdt_reset();
        }
        _client->stop();
        delay(500);
    } else {
        Serial.printf("[MQTT] Publish FAILED (state=%d, len=%u)\n", _mqtt->state(), strlen(payload));
    }
    return ok;
}

void GsmHandler::mqttLoop() {
    if (_mqtt) _mqtt->loop();
}

void GsmHandler::mqttDisconnect() {
    if (_mqtt && _mqtt->connected()) {
        _mqtt->disconnect();
        Serial.println(F("[MQTT] Disconnected"));
    }
}

bool GsmHandler::isNetworkConnected() {
    if (!_initialized) return false;
    _networkConnected = _modem->isNetworkConnected();
    return _networkConnected;
}

bool GsmHandler::isGprsConnected() {
    if (!_initialized || !_modem) return false;
    return _modem->isGprsConnected();
}

int GsmHandler::getSignalQuality() {
    if (!_initialized || !_modem) return 0;
    return _modem->getSignalQuality();
}

void GsmHandler::powerOff() {
    if (_modem) {
        _modem->poweroff();
        Serial.println(F("[GSM] Modem powered off"));
    }
    _networkConnected = false;
}

void GsmHandler::restart() {
    Serial.println(F("[GSM] Software reset via AT+CFUN=1,1..."));
    if (_modem) {
        _modem->sendAT(GF("+CFUN=1,1"));
        _modem->waitResponse(5000);
    }
    if (_mqtt) { delete _mqtt; _mqtt = nullptr; }
    if (_client) { delete _client; _client = nullptr; }
    if (_modem) { delete _modem; _modem = nullptr; }
    _initialized = false;
    _networkConnected = false;
    delay(3000);
    Serial.println(F("[GSM] Modem reset. Ready for re-init."));
}

void GsmHandler::sendAT(const char* cmd) {
    if (_modem) _modem->sendAT(cmd);
}

bool GsmHandler::parseGsmDateTime(const String& dt, timeStruct* outTime) {
    if (dt.length() < 14) return false;

    int year  = dt.substring(0, 2).toInt() + 2000;
    int month = dt.substring(3, 5).toInt();
    int date  = dt.substring(6, 8).toInt();
    int hour  = dt.substring(9, 11).toInt();
    int minute = dt.substring(12, 14).toInt();
    int second = (dt.length() >= 17) ? dt.substring(15, 17).toInt() : 0;

    if (year < 2024 || month < 1 || month > 12 ||
        date < 1 || date > 31 || hour > 23 || minute > 59 || second > 59) {
        return false;
    }

    outTime->year   = (uint16_t)year;
    outTime->month  = (uint8_t)month;
    outTime->date   = (uint8_t)date;
    outTime->hour   = (uint8_t)hour;
    outTime->minute = (uint8_t)minute;
    outTime->second = (uint8_t)second;

    snprintf(outTime->dateStr, sizeof(outTime->dateStr),
             "%02u/%02u/%04u", outTime->date, outTime->month, outTime->year);
    snprintf(outTime->timeStr, sizeof(outTime->timeStr),
             "%02u:%02u:%02u", outTime->hour, outTime->minute, outTime->second);

    return true;
}

void GsmHandler::setMqttConfig(const char* broker, uint16_t port,
                               const char* user, const char* pass,
                               const char* clientId) {
    strncpy(_mqttBroker,   broker,   sizeof(_mqttBroker)   - 1);
    strncpy(_mqttUser,     user,     sizeof(_mqttUser)     - 1);
    strncpy(_mqttPass,     pass,     sizeof(_mqttPass)     - 1);
    strncpy(_mqttClientId, clientId, sizeof(_mqttClientId) - 1);
    _mqttPort = port;
    if (_mqtt) _mqtt->setServer(_mqttBroker, _mqttPort);
}

bool GsmHandler::sendEmail(const char* smtpServer, uint16_t port,
                           const char* user, const char* pass,
                           const char* to,   const char* subject,
                           const char* body) {
    if (!_initialized || !_networkConnected || !_modem) return false;
    Serial.printf("[EMAIL] Connecting %s:%u\n", smtpServer, port);

    char cmd[160];
    snprintf(cmd, sizeof(cmd), "+EMAILCID=1");
    _modem->sendAT(cmd); _modem->waitResponse(3000);

    snprintf(cmd, sizeof(cmd), "+EMAILTO=30");
    _modem->sendAT(cmd); _modem->waitResponse(3000);

    snprintf(cmd, sizeof(cmd), "+SMTPSRV=\"%s\",%u", smtpServer, port);
    _modem->sendAT(cmd);
    if (_modem->waitResponse(5000) != 1) { Serial.println(F("[EMAIL] SMTPSRV failed")); return false; }

    snprintf(cmd, sizeof(cmd), "+SMTPAUTH=1,\"%s\",\"%s\"", user, pass);
    _modem->sendAT(cmd);
    if (_modem->waitResponse(5000) != 1) { Serial.println(F("[EMAIL] AUTH failed")); return false; }

    snprintf(cmd, sizeof(cmd), "+SMTPFROM=\"%s\",\"WeatherStation\"", user);
    _modem->sendAT(cmd); _modem->waitResponse(3000);

    snprintf(cmd, sizeof(cmd), "+SMTPRCPT=0,0,\"%s\",\"Recipient\"", to);
    _modem->sendAT(cmd); _modem->waitResponse(3000);

    snprintf(cmd, sizeof(cmd), "+SMTPSUBJECT=\"%s\"", subject);
    _modem->sendAT(cmd); _modem->waitResponse(3000);

    snprintf(cmd, sizeof(cmd), "+SMTPBODY=%u", (unsigned)strlen(body));
    _modem->sendAT(cmd);
    if (_modem->waitResponse(3000, GF("+SMTPBODY:")) != 1) { Serial.println(F("[EMAIL] BODY failed")); return false; }
    _modem->stream.print(body);
    _modem->waitResponse(3000);

    _modem->sendAT("+SMTPSEND");
    bool ok = (_modem->waitResponse(30000, GF("+SMTPSEND: 1")) == 1);
    Serial.printf("[EMAIL] %s\n", ok ? "Sent OK" : "Send FAILED");
    return ok;
}

bool GsmHandler::syncNtp() {
    _modem->sendAT("+CNTP=\"pool.ntp.org\",0");
    if (_modem->waitResponse(3000) != 1) {
        Serial.println(F("[NTP] Config NTP server FAILED"));
        return false;
    }

    _modem->sendAT("+CNTP");

    unsigned long start = millis();
    while (millis() - start < NTP_TIMEOUT_MS) {
        esp_task_wdt_reset();
        int8_t resp = _modem->waitResponse(2000, GF("+CNTP: 1"));
        if (resp == 1) {
            Serial.println(F("[NTP] Sync SUCCESS"));
            _modem->waitResponse(1000);
            return true;
        }
    }

    Serial.println(F("[NTP] Sync TIMEOUT"));
    return false;
}
