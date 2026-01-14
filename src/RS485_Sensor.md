# RS485 Example Command

**Read all of register of SOIL Sensor**
RS485Sensor.read(SOIL, 0x01, 0x00, 7, &Serial2)

**Read all of register of Weather Sensor**
RS485Sensor.read(WEATHER, 0x02, 0x01F4, 16, &Serial2)

**Write to reset the rainfall value of  Weather Sensor**
RS485Sensor.write(WEATHER, 0x02, 0x6002, 0x005A, &Serial2)
