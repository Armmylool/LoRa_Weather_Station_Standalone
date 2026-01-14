#ifndef SDCARD_H
#define SDCARD_H

#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "RS485Sensor.h"

class SDCard {
  public :
    /** @brief Write File If no file is detected it call it. It will generate File.
    */
    void write(fs::FS &fs, const char * path, const char * message) ;

    /** @brief Insert the data to the path file. */
    void append(fs::FS &fs, const char * path, const char * message) ;

    /** @brief Call this for Begin the SD Card write. */
    bool init(short CS_PIN) ;
    
    bool saveDataTOSD(const char* fileName, timeStruct* time_val, soilData* soil_val, weatherData* weather_val) ;
};

#endif