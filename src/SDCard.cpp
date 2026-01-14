#include "SDCard.h"

void SDCard::write(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\n", path);
  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void SDCard::append(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Appending to file: %s\n", path);
  File file = fs.open(path, FILE_APPEND); 
  if(!file){
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)){
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close(); 
}

bool SDCard::init(short CS_PINOUT) {
  if (!SD.begin(CS_PINOUT)){
      Serial.println("Card Mount Failed");
      return false ;
  }
  else {
      return true ;
  }
}

bool SDCard::saveDataTOSD(const char* fileName, timeStruct* time_val, soilData* soil_val, weatherData* weather_val) {
  if (time_val == nullptr || soil_val == nullptr || weather_val == nullptr) {
    Serial.println("[SD Error] Data pointers are NULL. Skipping save.");
    return false;
  }

  char dataBuffer[1024] ;

  snprintf(dataBuffer, sizeof(dataBuffer), 
    "%s,%s,"                            // Date, Time
    "%u,%.1f,%u,%.1f,%u,%u,%u,"         // Soil
    "%.1f,%u,%u,%u,%.1f,%.1f,%.1f,%u,%u,%.1f,%lu,%.1f,%u\r\n", // Weather

    time_val->dateStr, time_val->timeStr,

    soil_val->moisture_content, soil_val->soil_Temp / 10.0, soil_val->EC, soil_val->PH / 10.0, soil_val->N, soil_val->P, soil_val->K,

    weather_val->windSpeed / 10.0, weather_val->windStrength, weather_val->WindDirection_Num, weather_val->windDirection_Deg,
    weather_val->humidity / 10.0, weather_val->temperature / 10.0, weather_val->noise / 10.0, weather_val->PM_2_5, weather_val->PM_10,
    weather_val->pressure / 10.0, weather_val->illuminance, weather_val->rainfall / 10.0, weather_val->solar_irradiance
  );
  append(SD, fileName, dataBuffer) ;
  return true ;
}