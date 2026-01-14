#ifndef RS485SENSOR_H_
#define RS485SENSOR_H_

#include "utilities.h"
#include "ModbusMaster.h"

typedef enum  {
  SOIL = 1,
  WEATHER = 2
} sensorList;

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
  uint16_t illuminace_High = 0 ;
  uint16_t illuminace_Low = 0 ;
  uint16_t rainfall = 0 ;
  uint16_t solar_irradiance = 0 ;
} weatherData ;

class dataProcess {
    public :
      /** @brief Calculate a Median of threee values. */
      uint16_t getMedian(uint16_t val1, uint16_t val2, uint16_t val3) ;
} ;

class sensor {
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



#endif 