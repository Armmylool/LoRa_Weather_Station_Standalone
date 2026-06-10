/* Config Parameters — PAMIANG High Power (LoRa E32-433T30S) */
#ifndef UTILITIES_H_
#define UTILITIES_H_

#include <Arduino.h>
#include <HardwareSerial.h>

/* ===== VERSION ===== */
#define FIRMWARE_VERSION    "3.0.0-lora"
#define BUILD_DATE          "03-06-2026"

/* ===== SERIAL BAUDRATES ===== */
#define SERIAL_BAUDRATE 115200
#define SERIAL_RS485    9600
#define SERIAL_LORA     9600
#define DEBUG           1

/* ===== RS485 PINOUT ===== */
#define RS_485_RX_PIN D7
#define RS_485_TX_PIN D6

/* ===== LORA E32-433T30S PINOUT ===== */
#define LORA_RX_PIN  D5
#define LORA_TX_PIN  D4
extern HardwareSerial LORA_SERIAL;

/* ===== LORA E32 CONFIG ===== */
#define LORA_ADDR_H         0x00
#define LORA_ADDR_L         0x01
#define LORA_CHANNEL        0x17    /* 433MHz + 23 = Channel 23 (default) */
static constexpr unsigned long LORA_INIT_TIMEOUT_MS = 10000;
static constexpr unsigned long LORA_TX_TIMEOUT_MS   = 15000;
static constexpr size_t         LORA_MAX_PAYLOAD     = 512;

/* ===== LORA TX REDUNDANCY ===== */
static constexpr uint8_t        LORA_TX_REDUNDANCY   = 3;
static constexpr unsigned long  LORA_TX_REDUND_DELAY = 500;

/* ===== LORA ACK PROTOCOL ===== */
static constexpr unsigned long LORA_ACK_TIMEOUT_MS  = 2000;
static constexpr uint8_t       LORA_ACK_MAX_RETRIES = 3;
static constexpr unsigned long LORA_ACK_TX_SETTLE   = 100;
static constexpr unsigned long LORA_ACK_RX_SETTLE   = 50;

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
const uint8_t  BATT_PIN = A0 ;
const uint32_t R1       = 100000;
const uint32_t R2       = 100000;

/* ===== DONE PIN (TPL5110) ===== */
#define PIN_DONE D8

/* ===== TIME ===== */
const uint8_t TIME_INCREMENT_MINUTES = 10;

/* ===== WIFI AP CONFIG ===== */
#define WIFI_AP_SSID     "WeatherStation_AP_Pamiang"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_CHANNEL  1

/* WiFi AP timing */
static constexpr unsigned long WIFI_AP_TIMEOUT               = 60000UL;
static constexpr unsigned long WIFI_AP_DEFAULT_IDLE_MIN      = 2;
static constexpr unsigned long BLE_RETRY_FAILED_SCAN_MS      = 60000UL;
static constexpr unsigned long BLE_RETRY_SCAN_INTERVAL_MS    = 10000UL;
static constexpr uint8_t       BLE_CONNECT_TIMEOUT_SEC       = 5;

/* ===== WEB PORTAL AUTH ===== */
#define WEB_DEFAULT_USER "admin"
#define WEB_DEFAULT_PASS "admin"
static constexpr unsigned long WEB_SESSION_TIMEOUT_MS = 600000UL;

/* ===== OTA DEFAULTS (ArduinoOTA over WiFi AP) ===== */
#define OTA_DEFAULT_PASSWORD    "admin"

/* ===== PUBLISH ===== */
const uint8_t PUBLISH_BATCH_SIZE  = 1;
const uint8_t PUBLISH_MAX_RECORDS = 6;

/* ===== MEMORY ===== */
const uint32_t     MIN_FREE_SPACE_BYTES   = 10000;
const uint32_t     MAX_FILE_SIZE_BYTES    = 500 * 1024;
const uint8_t      STORAGE_ROLLOVER_PERCENT = 80;

/* ===== WDT ===== */
static constexpr int WDT_TIMEOUT_SEC = 45;

/* ===== STATE TIMEOUTS ===== */
static constexpr unsigned long SOIL_TIMEOUT         = 30000;
static constexpr unsigned long SOIL_SETTLE_DELAY    = 5000;
static constexpr unsigned long WEATHER_SETTLE_DELAY = 15000;
static constexpr unsigned long WEATHER_TIMEOUT      = 30000;
static constexpr unsigned long RECONNECT_TIMEOUT    = 120000;

#endif