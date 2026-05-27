/* Config Parameters */
#ifndef UTILITIES_H_
#define UTILITIES_H_

#include <Arduino.h>
#include <HardwareSerial.h>

/* ===== VERSION ===== */
#define FIRMWARE_VERSION    "2.3.6"
#define BUILD_DATE          "26-05-2026"

/* ===== SERIAL BAUDRATES ===== */
#define SERIAL_BAUDRATE 115200
#define SERIAL_RS485    9600
#define SERIAL_GSM      9600
#define DEBUG           1

/* ===== RS485 PINOUT ===== */
#define RS_485_RX_PIN D4
#define RS_485_TX_PIN D10

/* ===== GSM PINOUT ===== */
#define GSM_RX_PIN D7
#define GSM_TX_PIN D6
extern HardwareSerial GSM_SERIAL;

/* ===== GSM CONFIG ===== */
#define GSM_APN             "internet"
#define GSM_INIT_TIMEOUT_MS  120000
#define NTP_TIMEOUT_MS        60000

/* ===== RS485 SENSOR READ ===== */
const uint8_t readAttempt = 3;
const uint8_t maxRetry    = 5;

/* ===== SENSOR MODBUS CONFIG ===== */
const uint8_t  SOIL_SLAVE_ID         = 0x01;
const uint16_t SOIL_REGISTER_ADDR    = 0x0000;
const uint16_t SOIL_REGISTER_LEN     = 7;
const uint8_t  WEATHER_SLAVE_ID      = 0x02;
const uint16_t WEATHER_REGISTER_ADDR = 0x01F4;
const uint16_t WEATHER_REGISTER_LEN  = 16;

/* ===== BATTERY READ ===== */
const uint8_t  BATT_PIN = A0;
const uint32_t R1       = 100000;
const uint32_t R2       = 100000;

/* ===== DONE PIN (TPL5110) ===== */
#define PIN_DONE D2

/* ===== NTP / TIME ===== */
static constexpr unsigned long TIME_WAIT_TIMEOUT      = 30000;
const uint8_t                  TIME_INCREMENT_MINUTES = 10;

/* ===== WIFI AP CONFIG ===== */
#define WIFI_AP_SSID     "WeatherStation_AP"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_CHANNEL  1
static constexpr unsigned long WIFI_AP_TIMEOUT = 1800000UL; /* 5 min */
static constexpr unsigned long BLE_WAIT_TIMEOUT_MS = 60000UL; /* 1 min - BLE data wait timeout (configurable) */

/* ===== WEB PORTAL AUTH ===== */
#define WEB_DEFAULT_USER "admin"
#define WEB_DEFAULT_PASS "admin"
static constexpr unsigned long WEB_SESSION_TIMEOUT_MS = 600000UL; /* 10 min */

/* ===== OTA DEFAULTS (stored in Preferences) ===== */
#define OTA_DEFAULT_SERVER      ""
#define OTA_DEFAULT_PASSWORD    "admin"
#define OTA_DEFAULT_INTERVAL_H  24
#define OTA_DEFAULT_PROJECT     ""
#define OTA_DEFAULT_DEVICE      "All-in-One"
#define OTA_DEFAULT_DLPASS      ""

/* ===== EMAIL DEFAULTS (stored in Preferences) ===== */
#define EMAIL_DEFAULT_SMTP  ""
#define EMAIL_DEFAULT_PORT  587
#define EMAIL_DEFAULT_USER  ""
#define EMAIL_DEFAULT_PASS  ""
#define EMAIL_DEFAULT_TO    ""

/* ===== MQTT CONFIG ===== */
#define MQTT_BROKER     "119.59.103.220"
#define MQTT_PORT       1883
#define MQTT_TOPIC      "weather/Srisaket/Station_1"
#define MQTT_PING_TOPIC "weather/Srisaket/Station_1/ping"
#define MQTT_PONG_TOPIC "weather/Srisaket/Station_1/pong"
#define MQTT_USER       "kmutt"
#define MQTT_PASSWORD   "kmutt@kmutt"
#define MQTT_CLIENT_ID  "PCB_TEST_1"
static constexpr unsigned long MQTT_PUBLISH_TIMEOUT = 60000;
const uint8_t PUBLISH_BATCH_SIZE  = 6;   /* Min records before batch MQTT publish */
const uint8_t PUBLISH_MAX_RECORDS = 12;  /* Max records per publish cycle */

/* ===== MEMORY ===== */
const uint32_t     MIN_FREE_SPACE_BYTES   = 10000;
const uint32_t     MAX_FILE_SIZE_BYTES    = 500 * 1024;
const uint8_t      STORAGE_ROLLOVER_PERCENT = 80;

/* ===== WDT ===== */
static constexpr int WDT_TIMEOUT_SEC = 45;

/* ===== STATE TIMEOUTS ===== */
static constexpr unsigned long SOIL_TIMEOUT         = 30000;
static constexpr unsigned long SOIL_SETTLE_DELAY    = 15000;
static constexpr unsigned long WEATHER_SETTLE_DELAY = 15000;
static constexpr unsigned long WEATHER_TIMEOUT      = 30000;
static constexpr unsigned long RECONNECT_TIMEOUT    = 120000;

#endif
