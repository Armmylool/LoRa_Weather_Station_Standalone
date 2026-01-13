#include "SDCard.h"
#include "RS485Sensor.h"
#include "utilities.h"

/* Declare Function */
SDCard Card ;
sensor RS485Sensor ;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN) ;
  RS485Sensor.begin(&Serial2) ;
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
  if (RS485Sensor.read(SOIL, 0x01, 7, &Serial2)) {
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
  delay(1000) ;
}

