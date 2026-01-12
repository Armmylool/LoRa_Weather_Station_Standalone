#include "LoRaWan_APP.h"
#include "Arduino.h"

// --- Config LoRa (Forest Mode) ---
#define RF_FREQUENCY 923000000    // Hz
#define TX_OUTPUT_POWER 22        // dBm (แรงสุดเพื่อทะลุป่า)
#define LORA_BANDWIDTH 0          // [0: 125 kHz]
#define LORA_SPREADING_FACTOR 10  // [SF10] ส่งไกล ทะลุสิ่งกีดขวาง
#define LORA_CODINGRATE 1         // [1: 4/5]
#define LORA_PREAMBLE_LENGTH 8    
#define LORA_SYMBOL_TIMEOUT 0     
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false

// --- Config Sleep ---
#define SLEEP_DURATION 10 // ระยะเวลาหลับ (วินาที) เช่น 600 = 10 นาที
#define BUFFER_SIZE 50 

char txpacket[BUFFER_SIZE];
volatile bool cadDone = false;
volatile bool channelBusy = false;
volatile bool txDone = false;

// เก็บค่าตัวนับไว้ใน RTC Memory เพื่อไม่ให้หายตอน Deep Sleep
RTC_DATA_ATTR double txNumber = 0; 

static RadioEvents_t RadioEvents;

// --- Callback Functions ---

void OnTxDone(void) {
  Serial.println(" -> TX Done!");
  txDone = true; // ยกธงว่าส่งเสร็จแล้ว เตรียมหลับได้
}

void OnTxTimeout(void) {
  Serial.println(" -> TX Timeout!");
  txDone = true; // หมดเวลาก็ถือว่าจบงาน หลับได้
}

void onCadDone(bool activityResult) {
  if (activityResult) {
    // Serial.println("[Callback] Channel is Busy"); 
    channelBusy = true;
  } else {
    // Serial.println("[Callback] Channel is Free");
    channelBusy = false;
  }
  cadDone = true;
}

// --- Setup (ทำงานรอบเดียวแล้วจบเลย) ---

void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  // เชื่อมฟังก์ชัน Callback
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.CadDone = onCadDone; 

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  
  // ตั้งค่า Tx Config ก่อนเริ่มเสมอ
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
  
  Serial.println("Wake up & Ready...");

  // --- เริ่มกระบวนการส่งข้อมูล ---
  processTransmission();

  // --- เตรียมตัวนอน (Deep Sleep) ---
  Serial.printf("Going to sleep for %d seconds...\n", SLEEP_DURATION);
  Serial.flush(); // รอให้ print ให้หมดก่อน
  
  Radio.Sleep(); // สั่งชิป LoRa หลับด้วย
  
  // ตั้งเวลาตื่น
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {
  // ไม่ได้ใช้ loop แล้ว เพราะทำงานใน setup จบแล้วหลับเลย
}

// --- ฟังก์ชันทำงานหลัก ---
void processTransmission() {
  
  // 1. เตรียมข้อมูล
  txNumber += 1;
  sprintf(txpacket, "Hello Forest %.2f", txNumber);
  Serial.printf("[Prepare] Packet: \"%s\" (Len: %d)\n", txpacket, strlen(txpacket));

  // 2. วนลูป CAD (Listen Before Talk)
  int retryCount = 0;
  const int maxRetries = 5; 
  bool sentSuccess = false;

  while (retryCount < maxRetries) {
    Serial.printf("Checking Channel (Attempt %d)...\n", retryCount + 1);
    
    // สั่ง CAD
    cadDone = false;
    Radio.Standby();
    Radio.StartCad(4); // 4 Symbols

    // รอผล CAD
    unsigned long startCheck = millis();
    while (!cadDone) {
      Radio.IrqProcess(); 
      if (millis() - startCheck > 500) break; // Timeout
    }

    // เช็คผล
    if (cadDone && !channelBusy) {
      // --- ว่าง -> ส่งเลย ---
      Serial.println(">> Channel Free! Sending...");
      
      txDone = false; // รีเซ็ตธงส่ง
      Radio.Send((uint8_t *)txpacket, strlen(txpacket));
      
      // รอจนกว่าจะส่งเสร็จ (TxDone)
      unsigned long startTx = millis();
      while (!txDone) {
        Radio.IrqProcess();
        if (millis() - startTx > 3000) { // Timeout การส่ง 3 วิ
           Serial.println("Error: TX Timeout waiting");
           break;
        }
      }
      
      sentSuccess = true;
      break; // จบงาน ออกจากลูปไปนอน
      
    } else {
      // --- ไม่ว่าง -> รอแล้วลองใหม่ ---
      Serial.println(">> BUSY! Backing off...");
      delay(random(200, 1000)); // สุ่มรอ
      retryCount++;
    }
  }

  if (!sentSuccess) {
    Serial.println("Failed to send: Channel too busy.");
  }
}