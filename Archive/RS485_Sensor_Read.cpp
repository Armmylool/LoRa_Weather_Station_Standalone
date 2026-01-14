#include "SDCard.h"
#include "RS485Sensor.h"
#include "utilities.h"

/* Declare Function */
SDCard Card ;
RS485sensor RS485Sensor ;

void setup() {
  Serial.begin(115200);
  RS485Sensor.begin(&Serial2) ;
  Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN) ;
  // if (!Card.init(CS_PIN)) {
  //   while (1);
  // }
  // Serial.println("SD Card initialized.");
  // if (!SD.exists("/data.csv")) {
  //   Serial.println("File doesn't exist. Creating new file...");
  //   Card.write(SD, "/data.csv", "Time,Value\r\n"); 
  // } else {
  //   Serial.println("File already exists. Ready to append.");
  // }

  // String dataMessage = String(millis()) + "," + String(analogRead(34)) + "\r\n";
  // Card.append(SD, "/data.csv", dataMessage.c_str());
  // delay(1000);
  Serial.print("INIT") ; 
  delay(100) ;
}

void loop() {
  if (RS485Sensor.read(SOIL, 0x01, 0x00, 7, &Serial2)) {
    Serial.println("Success!");
    Serial.print("  - Moisture: "); 
    Serial.println(RS485Sensor.currentSoil.moisture_content);
    Serial.print("  - Temp: ");
    Serial.println(RS485Sensor.currentSoil.soil_Temp); 
    Serial.print("  - EC: ");
    Serial.println(RS485Sensor.currentSoil.EC);
    Serial.print("  - PH: "); 
    Serial.println(RS485Sensor.currentSoil.PH);
    Serial.print("  - N: ");
    Serial.println(RS485Sensor.currentSoil.N); 
    Serial.print("  - P: ");
    Serial.println(RS485Sensor.currentSoil.P);
    Serial.print("  - K: ");
    Serial.println(RS485Sensor.currentSoil.K);
  }
  delay(100) ;
  if (RS485Sensor.read(WEATHER, 0x02, 0x01F4, 16, &Serial2)){
    Serial.println("Success!");
    Serial.print("  - windSpeed: "); 
    Serial.println(RS485Sensor.currentWeather.windSpeed);
    Serial.print("  - windStrength: ");
    Serial.println(RS485Sensor.currentWeather.windStrength); 
    Serial.print("  - WindDirection_Num: ");
    Serial.println(RS485Sensor.currentWeather.WindDirection_Num);
    Serial.print("  - windDirection_Deg: "); 
    Serial.println(RS485Sensor.currentWeather.windDirection_Deg);
    Serial.print("  - humidity: ");
    Serial.println(RS485Sensor.currentWeather.humidity); 
    Serial.print("  - temperature: "); 
    Serial.println(RS485Sensor.currentWeather.temperature);
    Serial.print("  - noise: ");
    Serial.println(RS485Sensor.currentWeather.noise); 
    Serial.print("  - PM_2_5: "); 
    Serial.println(RS485Sensor.currentWeather.PM_2_5);
    Serial.print("  - PM_10: ");
    Serial.println(RS485Sensor.currentWeather.PM_10); 
    Serial.print("  - pressure: "); 
    Serial.println(RS485Sensor.currentWeather.pressure);
    Serial.print("  - illuminace_High: ");
    Serial.println(RS485Sensor.currentWeather.illuminace_High); 
    Serial.print("  - illuminace_Low: ");
    Serial.println(RS485Sensor.currentWeather.illuminace_Low); 
  }
  delay(1000) ;
  if (RS485Sensor.write(WEATHER, 0x02, 0x6002, 0x005A, &Serial2)) {
    Serial.println("IN Write loop") ;
  }
  //Test
}

