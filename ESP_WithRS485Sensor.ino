/* This code for 6 in 1 soil Sensor with RS485*/
#include <ModbusMaster.h>

#define RS485_RX_PIN 16
#define RS485_TX_PIN 17
#define DATA_RX_PIN 18 
#define DATA_TX_PIN 19

ModbusMaster sensor;

typedef struct {
  uint16_t moisture_content = 0 ;
  uint16_t temp = 0 ;
  uint16_t EC = 0 ; 
  uint16_t N = 0 ;
  uint16_t P = 0 ;
  uint16_t K = 0 ;
  uint16_t salinity = 0 ;
  uint16_t TDS = 0 ;
} soilSensor ;

typedef enum {
  STATE_IDLE, /* Init the Sensor system */
  STATE_SOILSENSOR, /* Interrupt Read Soil Sensor */
  STATE_AIRSENSOR, /* Read Air Sensor*/
  STATE_TIMESENSOR, /* Read Time sensor*/
  STATE_UARTTRANSMIT, /* Send All Data to another Sensor*/ 
} systemState ;


soilSensor soilData ;
systemState currentState = STATE_IDLE ;

/* Attempt Reading Settings */
short soilReadAttempts = 2 ;

void soilRead() {
  short readAttempts = 0 ;
  memset(&soilData, 0, sizeof(soilData)) ;
  while (readAttempts <= soilReadAttempts) {
    uint8_t result;
    result = sensor.readHoldingRegisters(0, 8);  
    if (result == sensor.ku8MBSuccess) {
      soilData.moisture_content = sensor.getResponseBuffer(0) ;
      soilData.temp = sensor.getResponseBuffer(1) ;
      soilData.EC= sensor.getResponseBuffer(2);
      soilData.N = sensor.getResponseBuffer(3) ;
      soilData.P = sensor.getResponseBuffer(4);
      soilData.K = sensor.getResponseBuffer(5);
      soilData.salinity = sensor.getResponseBuffer(6);
      soilData.TDS = sensor.getResponseBuffer(7);
      Serial.printf("Soil: %d | Temp: %d | EC: %d | NPK: %d-%d-%d | Sal: %d | TDS: %d\n", 
                soilData.moisture_content, 
                soilData.temp, 
                soilData.EC, 
                soilData.N, soilData.P, soilData.K, 
                soilData.salinity, 
                soilData.TDS);
      delay(500) ;
      readAttempts = 0 ;
      break ;
    } else {
      Serial.printf("ReadAttempts : %d\n", readAttempts) ;
      Serial.println("Read error, check your serial configs, wiring and power supply");
      delay(500) ;
      readAttempts ++ ;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, DATA_RX_PIN, DATA_TX_PIN) ;   /* For UART */
  Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN) ;   /* For RS485 Sensor */
  sensor.begin(1, Serial2);                                       /* sensor slave id 1 , read port serial2*/
}

void loop() {
  switch (currentState) {
    case STATE_IDLE : /* Power Management Initialise System */
      Serial.println("State Idle") ;
      delay(500) ;
      currentState = STATE_SOILSENSOR ;
    case STATE_SOILSENSOR :
      soilRead() ;
      // currentState = STATE_AIRSENSOR ;
    // case STATE_AIRSENSOR : 
    //   /* Insert the RS485 AIRSENSOR*/
    // case STATE_UARTTRANSMIT :
    
  }
}