#include "SDCard.h"
#include "RS485Sensor.h"
#include "utilities.h"

/* Declare Function */
SDCard Card ;

void setup() {
  Serial.begin(115200);
  if (!Card.init(CS_PIN)) {
    while (1);
  }
  Serial.println("SD Card initialized.");
  if (!SD.exists("/data.csv")) {
    Serial.println("File doesn't exist. Creating new file...");
    Card.write(SD, "/data.csv", "Time,Value\r\n"); 
  } else {
    Serial.println("File already exists. Ready to append.");
  }

  String dataMessage = String(millis()) + "," + String(analogRead(34)) + "\r\n";
  Card.append(SD, "/data.csv", dataMessage.c_str());
  delay(1000); 
}

void loop() {

}

