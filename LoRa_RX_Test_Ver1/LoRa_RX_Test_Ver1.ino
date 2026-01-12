#include "LoRaWan_APP.h"
#include "Arduino.h"

// --- Config ต้องตรงกับฝั่งส่งเป๊ะๆ ---
#define RF_FREQUENCY 923000000    // Hz
#define TX_OUTPUT_POWER 14        // dBm
#define LORA_BANDWIDTH 0          // [0: 125 kHz]
#define LORA_SPREADING_FACTOR 10  // [SF10]
#define LORA_CODINGRATE 1         // [1: 4/5]
#define LORA_PREAMBLE_LENGTH 8
#define LORA_SYMBOL_TIMEOUT 0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false

// --- Struct ข้อมูล (ต้องเหมือนฝั่งส่ง) ---
typedef struct {
  uint16_t moisture_content;
  uint16_t temp;
  uint16_t EC;
  uint16_t N;
  uint16_t P;
  uint16_t K;
  uint16_t salinity;
  uint16_t TDS;
} soilSensor;

soilSensor incomingData;
static RadioEvents_t RadioEvents;
int16_t rssi, rxSize;
bool lora_idle = true;

// ฟังก์ชัน Callback เมื่อรับข้อมูลเสร็จ
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    // 1. เช็คขนาดข้อมูลว่าตรงกับ Struct เราไหม (16 Bytes)
    if (size == sizeof(soilSensor)) {
        
        // 2. ก๊อปปี้ข้อมูลดิบ (payload) เข้าตัวแปร Struct
        memcpy(&incomingData, payload, size);

        // 3. แสดงผล
        Serial.println("\n--------------------------------");
        Serial.printf("Received Packet (RSSI: %d dBm)\n", rssi);
        Serial.printf("Moisture : %d\n", incomingData.moisture_content);
        Serial.printf("Temp     : %d\n", incomingData.temp);
        Serial.printf("EC       : %d\n", incomingData.EC);
        Serial.printf("N-P-K    : %d - %d - %d\n", incomingData.N, incomingData.P, incomingData.K);
        Serial.printf("Salinity : %d\n", incomingData.salinity);
        Serial.printf("TDS      : %d\n", incomingData.TDS);
        Serial.println("--------------------------------");
        
    } else {
        Serial.printf("\nError: Size mismatch! Expected %d, Got %d\n", sizeof(soilSensor), size);
    }
    
    // ตั้งสถานะให้พร้อมรับรอบต่อไป
    Radio.Sleep( );
    lora_idle = true;
}

void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    
    // ตั้งค่า LoRa Callback
    RadioEvents.RxDone = OnRxDone;
    Radio.Init( &RadioEvents );
    Radio.SetChannel( RF_FREQUENCY );
    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                       LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                       LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                       0, true, 0, 0, LORA_IQ_INVERSION_ON, true );
                       
    Serial.println("LoRa Receiver Ready...");
}

void loop()
{
  if(lora_idle)
  {
    lora_idle = false;
    Serial.println("Wait for LoRa Packet...");
    Radio.Rx(0); // เข้าโหมดรอรับ (Continuous RX)
  }
  Radio.IrqProcess( ); // ประมวลผล Interrupt (สำคัญมาก ห้ามลบ)
}