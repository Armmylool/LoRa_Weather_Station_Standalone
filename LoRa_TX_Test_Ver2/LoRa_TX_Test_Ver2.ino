#include <Arduino.h>

const int RX_PIN = 18;    /* RX_Pin for UART */
const int TX_PIN = 17;    /* TX_Pin for UART */
const int PACKET_SIZE = 18;   /* Packet_size for receive a message from ESP32*/

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

typedef enum {
  STATE_RECEIVED,
  STATE_RESPOND,
  STATE_LoRaSEND,
} systemState ;

uint8_t respondPacket[2] = {0x01, 0xFF} ;   /* If the Received completed This will send this message back. */
soilSensor incomingData;
systemState currentState = STATE_RECEIVED ;
uint8_t rawBuffer[PACKET_SIZE]; 

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.println("Receiver Ready: Smart Peek Mode");
}

void loop() {
  // Serial.println(currentState) ;
  switch (currentState) {
    case STATE_RECEIVED :
      if (Serial2.available() >= PACKET_SIZE) {
        if (Serial2.peek() != 0xAA) {       /* Message Header Synchronize */
          Serial2.read(); 
          return; 
        }
        Serial2.readBytes(rawBuffer, PACKET_SIZE);
        if (rawBuffer[1] == 0xBB) {
          memcpy(&incomingData, &rawBuffer[2], sizeof(soilSensor));
          Serial.println("--------------------") ;
          Serial.printf("Moisture: %d\n", incomingData.moisture_content);
          Serial.printf("Temp:     %d\n", incomingData.temp);
          Serial.printf("EC:       %d\n", incomingData.EC);
          Serial.printf("N:       %d\n", incomingData.N);
          Serial.printf("P:       %d\n", incomingData.P);
          Serial.printf("K:       %d\n", incomingData.K);
          Serial.printf("Salinity:       %d\n", incomingData.salinity);
          Serial.printf("TDS:       %d\n", incomingData.TDS);
          Serial.println("--------------------");
        } 
      }
      currentState = STATE_RESPOND ;
    case STATE_RESPOND :
      Serial2.write(respondPacket, sizeof(respondPacket)) ;
      currentState = STATE_RECEIVED ;
    }
}