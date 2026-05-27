#ifndef WIFI_AP_SERVER_H_
#define WIFI_AP_SERVER_H_

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "sensor_v2.h"
#include "Memory.h"
#include "GsmHandler.h"

/* ── BLE scan results ── */
#define BLE_MAX_DEVICES 10

struct BLEFoundDevice {
    char   name[32];
    char   mac[18];
    int8_t rssi;
};

/* ── BLE connected device / GATT read results ── */
#define BLE_CHAR_MAX 16

struct BLECharData {
    char uuid[37];   /* "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" */
    char value[64];  /* hex string of raw bytes */
    char ascii[32];  /* printable ASCII representation */
};

struct BLEConnectedDevice {
    char       mac[18];
    char       name[32];
    bool       connected;
    uint8_t    charCount;
    BLECharData chars[BLE_CHAR_MAX];
};

/* ── System-wide status passed to web portal ── */
struct SystemStatus {
    SensorData*          sensor;
    timeStruct*          time;
    uint16_t*            battMv;
    bool*                gsmAvail;
    int*                 gsmRssi;
    bool*                bleActive;
    Memory*              memory;
    GsmHandler*          gsm;
    char                 lastOtaStr[24];
    BLEFoundDevice*      bleDevices;
    uint8_t*             bleDeviceCount;
    char                 bleMac[18];
    BLEConnectedDevice*  bleConnDev;    /* GATT read results */
    char*                bleTargetMac;  /* MAC requested for connect */
    bool*                bleConnPending;
    char*                bleSavedMac;   /* NVS-persisted MAC for auto-reconnect */
    bool*                bleScanRequest;/* trigger immediate scan */
    bool*                bleIsNus;      /* true = connected device is NUS (Sniffer Portal) */
    unsigned long*       bleNusLastMs;  /* millis() when last NUS JSON was parsed */
    bool*                bleNusRefreshReq; /* set true to trigger immediate NUS data request */
    bool*                otaCheckNow;   /* set true to trigger OTA check on next GSM cycle */
};

class WifiApServer {
public:
    WifiApServer();
    ~WifiApServer();

    bool begin(SystemStatus* status);
    void handleClient();
    void stop();
    bool isTimedOut();
    bool isActive();

private:
    WebServer*    _server;
    Preferences   _prefs;
    SystemStatus* _st;
    unsigned long _startTime;
    unsigned long _apTimeoutMs;
    unsigned long _noClientSince;
    bool          _active;

    /* session */
    char          _token[17];
    unsigned long _tokenTime;
    bool          _loggedIn;

    bool   checkAuth();
    void   startSession();
    void   clearSession();

    /* page handlers */
    void hLogin();
    void hLoginPost();
    void hLogout();
    void hRoot();
    void hLive();
    void hApiLive();
    void hBleConnect();
    void hBleDisconnect();
    void hBleScan();
    void hBleForget();
    void hBleRefresh();
    void hApiBle();
    void hFiles();
    void hDownload();
    void hDelete();
    void hDeleteAll();
    void hLog();
    void hLogDownload();
    void hSettings();
    void hSetPassword();
    void hSetAp();
    void hSetBle();
    void hSetSources();
    void hSetSoil();
    void hSetWeather();
    void hSetFile();
    void hSetInflux();
    void hSetNtp();
    void hNtpSyncNow();
    void hSetMem();
    void hSetMqtt();
    void hSetEmail();
    void hSetOta();
    void hTestInflux();

    void hTestMqtt();
    void hTestEmail();
    void hTestOta();
    void hOtaUpdate();
    void hAbout();
    void hReboot();
    void hNotFound();

    /* helpers */
    void   sendPage(const char* title, const char* activeTab,
                    const String& body, bool withNav = true);
    String navBar(const char* active);
    String topBar();
    void   appendEvent(const char* event);
    String eventLogPath();
    String fmtBytes(uint32_t b);
    void   redirectTo(const char* url);
    bool   isDataFile(const String& name);
    void   sendSettingsChunk(const String& html);
    void   startSettingsPage(const String& msg);
    void   endSettingsPage();
};

#endif
