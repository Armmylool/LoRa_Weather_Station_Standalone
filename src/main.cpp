#include "SDCard.h"
#include "RS485Sensor.h"

typedef enum {
  STATE_IDLE ,
  STATE_TIME ,
  STATE_SOIL ,
  STATE_WEATHER , 
  STATE_SAVEMEMORY ,
  STATE_UARTTRANSMIT ,
  STATE_UARTRESPOND , 
} systemState ;

/* Declare Function */
SDCard Card ; /* SDCard utilized */
RS485sensor modbusSensor ;   /* RS485 Sensor utilized */
timeSensor RTC ;  /* RTC Module utilized */
systemState currentState = STATE_IDLE ;

/* Declare Function */
void checkFile(const char* fileName) ;

void setup() {
  Serial.begin(115200) ;
  Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN) ;
  Wire.begin() ;

  modbusSensor.begin(&Serial2) ;   /* Begin Sensor */
  if (Card.init(CS_PIN)){   /* Init Sensor */
    Serial.println("SD Card Init") ; 
    checkFile(fileSavingName) ;
  }
  else {
    Serial.println("SD Card Fail") ;
    while(1) ; /* This is version of testing. The real product this line will delete. */
  }
  if (RTC.init_timeSet()) {   /* Init RTC Module */
    Serial.println("Init RTC") ;
  } 
  else {
    Serial.println("RTC Init Failed") ;
  }

  Serial.print("System Ready...") ; 
  delay(100) ;
}

void loop() {
  switch (currentState) {
    case STATE_IDLE : 
      /* Now it is nothing here. */
      delay(10000) ;
      currentState = STATE_TIME ;
      break ;
    case STATE_TIME :
      if (RTC.getTime(&RTC.time)) {
        currentState = STATE_SOIL ;
      }
      else {
        Serial.println("RTC Read Failed");
        /* Add this condition to do smth. */
      }
      break ;
      /* Check เวลาว่า ขึ้นเดือนใหม่หรือยังถ้าขึ้นแล้วให้ Reset ค่า Rainfall */
    case STATE_SOIL :
      if(modbusSensor.read(SOIL, 0x01, 0x0000, 7, &Serial2)) {
        if (DEBUG) {
          Serial.println("Success!");
          Serial.print("  - Moisture: "); 
          Serial.println(modbusSensor.currentSoil.moisture_content);
          Serial.print("  - Temp: ");
          Serial.println(modbusSensor.currentSoil.soil_Temp); 
          Serial.print("  - EC: ");
          Serial.println(modbusSensor.currentSoil.EC);
          Serial.print("  - PH: "); 
          Serial.println(modbusSensor.currentSoil.PH);
          Serial.print("  - N: ");
          Serial.println(modbusSensor.currentSoil.N); 
          Serial.print("  - P: ");
          Serial.println(modbusSensor.currentSoil.P);
          Serial.print("  - K: ");
          Serial.println(modbusSensor.currentSoil.K);
        }
        delay(100) ;
        currentState = STATE_WEATHER ;
      }
      else {
        Serial.println("Soil Read Failed") ;
        /* Add this condition to do smth. */
      }
      break ;
    case STATE_WEATHER :
      if (modbusSensor.read(WEATHER, 0x02, 0x01F4, 16, &Serial2)) {
        if (DEBUG) {
          Serial.println("Success!");
          Serial.print("  - windSpeed: "); 
          Serial.println(modbusSensor.currentWeather.windSpeed);
          Serial.print("  - windStrength: ");
          Serial.println(modbusSensor.currentWeather.windStrength); 
          Serial.print("  - WindDirection_Num: ");
          Serial.println(modbusSensor.currentWeather.WindDirection_Num);
          Serial.print("  - windDirection_Deg: "); 
          Serial.println(modbusSensor.currentWeather.windDirection_Deg);
          Serial.print("  - humidity: ");
          Serial.println(modbusSensor.currentWeather.humidity); 
          Serial.print("  - temperature: "); 
          Serial.println(modbusSensor.currentWeather.temperature);
          Serial.print("  - noise: ");
          Serial.println(modbusSensor.currentWeather.noise); 
          Serial.print("  - PM_2_5: "); 
          Serial.println(modbusSensor.currentWeather.PM_2_5);
          Serial.print("  - PM_10: ");
          Serial.println(modbusSensor.currentWeather.PM_10); 
          Serial.print("  - pressure: "); 
          Serial.println(modbusSensor.currentWeather.pressure);
          Serial.print("  - illuminace: ");
          Serial.println(modbusSensor.currentWeather.illuminance) ; 
        }
        delay(100) ;
        currentState = STATE_SAVEMEMORY ;
      }
      else {
        Serial.println("Weather Read Failed") ;
        /* Add this condition to do smth. */
      }
      break ;
    case STATE_SAVEMEMORY :
      if (Card.saveDataTOSD(fileSavingName, &RTC.time, &modbusSensor.currentSoil, &modbusSensor.currentWeather)) {
        Serial.println("Save Successfully") ;
        currentState = STATE_IDLE ;
      }
      else {
        Serial.print("Save Failed") ;
      }
      currentState = STATE_IDLE; 
      break;
  }
}

void checkFile(const char* fileName) {
  if(!SD.exists(fileName)) {
    Serial.println("File doesn't exist. Creating new file") ;
    Card.write(SD, fileName, "Date,Time,Moisture,soil_temperature,Electrical conductivity,PH,N,P,K,"
      "windSpeed,windStrength,WindDirection_Num,windDirection_Deg,humidity,temperature,noise,PM_2_5,PM_10,pressure,Illuminance,Rainfall,solar_Irradiance\r\n") ;
  }
  else {
    Serial.println("File already exists. Ready to append.");
  }
}