#ifndef UTILITIES_H
#define UTILITIES_H

#include <Arduino.h>

#define soilSensor 1   /* Same as slave ID */
#define weatherSensor 2    /* Same as slave ID */
#define RS485Log Serial2   /* Use as RS485 */
#define Comms Serial1  /* Use as communication talking between two ESP32 */
#define Log Serial   /* USe as Debugging */
#define DEBUG true

const uint8_t CS_PIN = GPIO_NUM_5 ; /* Chip Select Pin */
const uint8_t RS485_RX_PIN  = 16 ;  /* RS485 RX Pinout */
const uint8_t RS485_TX_PIN = 17 ;   /* RS485 TX Pinout */
// const int DATA_RX_PIN = ค่าอื่น เช่น 13 ;    /* Data RX Pinout */
// const int DATA_TX_PIN = ค่าอื่น เช่น 14 ;    /* DATA TX Pinout */
const uint8_t readAttempt = 3 ; /* Reading Attempt 0, 1, 2 readAttempt is 3. */
const uint8_t maxRetry = 5 ; /* Maximum Retry when reading all sensor error.*/
static const char* fileSavingName = "/Data_TEST.csv" ;   /* Name of File SD Card. */

#endif