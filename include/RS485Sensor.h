#ifndef RS485SENSOR_H_
#define RS485SENSOR_H_

#include "utilities.h"
#include "ModbusMaster.h"
#include "RTClib.h"

typedef enum {
  SOIL = 1,
  WEATHER = 2
} sensorList;

typedef struct {
  uint8_t date ;
  uint8_t month ;
  uint16_t year ;
  uint8_t hour ;
  uint8_t minute ;
  uint8_t second ;

  char dateStr[12] ;
  char timeStr[9] ;
} timeStruct ;

typedef struct {
  uint16_t moisture_content = 0 ;
  int16_t soil_Temp = 0 ;
  uint16_t EC = 0 ; 
  uint16_t PH = 0 ;
  uint16_t N = 0 ;
  uint16_t P = 0 ;
  uint16_t K = 0 ;
} soilData ;
  
typedef struct {
  uint16_t windSpeed = 0 ;
  uint16_t windStrength = 0 ;
  uint16_t WindDirection_Num = 0 ;
  uint16_t windDirection_Deg = 0 ;
  uint16_t humidity = 0 ;
  uint16_t temperature = 0 ;
  uint16_t noise = 0 ;
  uint16_t PM_2_5 = 0 ;
  uint16_t PM_10 = 0 ;
  uint16_t pressure = 0 ;
  uint16_t illuminance = 0 ;
  uint16_t rainfall = 0 ;
  uint16_t solar_irradiance = 0 ;
} weatherData ;

class dataProcess {
  public :
    /** @brief Calculate a Median of three values. */
    uint16_t getMedian(uint16_t val1, uint16_t val2, uint16_t val3) ;
    /** @brief Calculate a Median of three values (32-bit version). */
    uint32_t getMedian32(uint32_t val1, uint32_t val2, uint32_t val3) ;
    /** @brief Change from 2 bytes to 1 byte */
    uint16_t twobytes_to_onebyte(uint16_t highval, uint16_t lowval) ;
} ;

class RS485sensor {
  public : 
    soilData currentSoil;
    weatherData currentWeather ;
    /** @brief Send the Serial2 in this class. */
    void begin(Stream* serialPort);
    /** @brief Read a data with Modbus RS485 Protocol */
    bool read(uint8_t sensorType ,uint8_t slaveID, uint16_t address, uint16_t length, Stream* serialPort) ;
    /** @brief Write the register in order to setting the RS485 Sensor. */
    bool write (uint8_t sensorType ,uint8_t slaveID, uint16_t address, uint16_t value, Stream* serialPort) ;
  private :
    ModbusMaster modbus;
    dataProcess postProcessing;
    Stream* _serial = nullptr;
} ;

class timeSensor {
  public :
    timeStruct time ;
    /** @brief Start the RTC module and config time. */
    bool init_timeSet () ;
    /** @brief Get time and save in Time struct */
    bool getTime(timeStruct* val) ;
    
  private :
    RTC_DS3231 rtc;
} ;



#endif 