#include "RS485Sensor.h"

timeSensor RTC ;

void setup () {
  Serial.begin(115200) ;
  RTC.init_timeSet() ; 
}

void loop () {
  RTC.getTime(&RTC.time) ;
  delay(1000) ;
}