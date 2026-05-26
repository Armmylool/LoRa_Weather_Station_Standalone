#ifndef GSM_HANDLER_H_
#define GSM_HANDLER_H_

#include "utilities.h"
#include "sensor_v2.h"

#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>
#include <PubSubClient.h>

class GsmHandler {
public:
    GsmHandler();
    ~GsmHandler();

    bool init(Stream& serial);
    bool connectNetwork();
    bool getNetworkTime(timeStruct* outTime);
    bool mqttConnect();
    bool mqttPublish(const char* topic, const char* payload);
    void mqttLoop();
    void mqttDisconnect();
    bool isNetworkConnected();
    bool isGprsConnected();     /* checks active PDP/data session, not just cell reg */
    void powerOff();
    void restart();
    void sendAT(const char* cmd);
    int  getSignalQuality();
    bool sendEmail(const char* smtpServer, uint16_t port,
                   const char* user, const char* pass,
                   const char* to,   const char* subject,
                   const char* body);
    TinyGsm* getModem() { return _modem; }

    /* dynamic MQTT config — call before mqttConnect() */
    void setMqttConfig(const char* broker, uint16_t port,
                       const char* user,   const char* pass,
                       const char* clientId);

private:
    TinyGsm*       _modem;
    TinyGsmClient* _client;
    PubSubClient*  _mqtt;
    bool           _initialized;
    bool           _networkConnected;

    /* runtime MQTT config (defaults from utilities.h) */
    char     _mqttBroker[64];
    uint16_t _mqttPort;
    char     _mqttUser[32];
    char     _mqttPass[32];
    char     _mqttClientId[32];

    bool parseGsmDateTime(const String& dt, timeStruct* outTime);
    bool syncNtp();
};

#endif
