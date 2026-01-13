#include "RS485Sensor.h"



uint16_t dataProcess::getMedian(uint16_t val1, uint16_t val2, uint16_t val3) {
	uint16_t middle ;
	if ((val1 <= val2) && (val1 <= val3)) {
    middle = (val2 <= val3) ? val2 : val3;
  } else if ((val2 <= val1) && (val2 <= val3)) {
    middle = (val1 <= val3) ? val1 : val3;
  } else {
    middle = (val1 <= val2) ? val1 : val2;
  }
  return middle;
}

void sensor::begin(Stream* serialPort) {
  _serial = serialPort; 
}

bool sensor::read (uint8_t sensorType ,uint16_t address, uint16_t length, Stream* serialPort) {
	uint8_t startAttempt = 0 ; /* Read Attempt Flags */
	uint8_t retry_FLAGS = 0 ;	/* Retry Flags*/
	uint8_t result;
	uint16_t rawBuffer[readAttempt][12];

	modbus.begin(address, *serialPort) ;

	while (startAttempt < readAttempt && retry_FLAGS < maxRetry) {
		result = modbus.readHoldingRegisters(address, length) ;
		if (result == modbus.ku8MBSuccess) {
			for (int cnt = 0; cnt < length; cnt++) {
				if (cnt < 12) { /* Protect Buffer overflow by the max array */
					rawBuffer[startAttempt][cnt] = modbus.getResponseBuffer(cnt);
				}
			}
			startAttempt ++ ;
		}
		else {
			retry_FLAGS ++ ;
			delay(50) ;
		}
		delay(100) ;
	}
	if (sensorType == SOIL) {
		memset(&currentSoil, 0, sizeof(soilData)); /* Set the all of struct to zero First.*/
		currentSoil.moisture_content = postProcessing.getMedian(rawBuffer[0][0], rawBuffer[1][0], rawBuffer[2][0]);
		currentSoil.soil_Temp        = postProcessing.getMedian(rawBuffer[0][1], rawBuffer[1][1], rawBuffer[2][1]);
		currentSoil.EC               = postProcessing.getMedian(rawBuffer[0][2], rawBuffer[1][2], rawBuffer[2][2]);
		currentSoil.PH               = postProcessing.getMedian(rawBuffer[0][3], rawBuffer[1][3], rawBuffer[2][3]);
		currentSoil.N                = postProcessing.getMedian(rawBuffer[0][4], rawBuffer[1][4], rawBuffer[2][4]);
		currentSoil.P                = postProcessing.getMedian(rawBuffer[0][5], rawBuffer[1][5], rawBuffer[2][5]);
		currentSoil.K                = postProcessing.getMedian(rawBuffer[0][6], rawBuffer[1][6], rawBuffer[2][6]);
		return true;
	}
	else if (sensorType == WEATHER) {
		memset(&currentWeather, 0, sizeof(weatherData)); // เคลียร์ค่าเก่า
		currentWeather.windSpeed         = postProcessing.getMedian(rawBuffer[0][0], rawBuffer[1][0], rawBuffer[2][0]);
		currentWeather.windStrength      = postProcessing.getMedian(rawBuffer[0][1], rawBuffer[1][1], rawBuffer[2][1]);
		currentWeather.WindDirection_Num = postProcessing.getMedian(rawBuffer[0][2], rawBuffer[1][2], rawBuffer[2][2]);
		currentWeather.windDirection_Deg = postProcessing.getMedian(rawBuffer[0][3], rawBuffer[1][3], rawBuffer[2][3]);
		currentWeather.humidity          = postProcessing.getMedian(rawBuffer[0][4], rawBuffer[1][4], rawBuffer[2][4]);
		currentWeather.temperature       = postProcessing.getMedian(rawBuffer[0][5], rawBuffer[1][5], rawBuffer[2][5]);
		currentWeather.noise             = postProcessing.getMedian(rawBuffer[0][6], rawBuffer[1][6], rawBuffer[2][6]);
		currentWeather.PM_2_5            = postProcessing.getMedian(rawBuffer[0][7], rawBuffer[1][7], rawBuffer[2][7]);
		currentWeather.PM_10             = postProcessing.getMedian(rawBuffer[0][8], rawBuffer[1][8], rawBuffer[2][8]);
		currentWeather.pressure          = postProcessing.getMedian(rawBuffer[0][9], rawBuffer[1][9], rawBuffer[2][9]);
		currentWeather.illuminace_High   = postProcessing.getMedian(rawBuffer[0][10], rawBuffer[1][10], rawBuffer[2][10]);
		currentWeather.illuminace_Low    = postProcessing.getMedian(rawBuffer[0][11], rawBuffer[1][11], rawBuffer[2][11]);
		return true ;
	}
	return false ;
}
