#include "Memory.h"
#include <esp_task_wdt.h>

static constexpr uint16_t FILE_WDT_FEED_INTERVAL_LINES = 16;

static void feedFileWDT() {
  esp_task_wdt_reset();
  delay(0);
}

void Memory::write(fs::FS &fs, const char * path, const char * message){
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

bool Memory::append(fs::FS &fs, const char * path, const char * message){
  File file = fs.open(path, FILE_APPEND); 
  if(!file){
    Serial.println("Failed to open file for appending");
    return false;
  }
  bool success = false;
  if(file.print(message)){
    success = true;
  } else {
    Serial.println("Append failed");
    success = false;
  }
  
  file.flush();
  file.close(); 
  
  return success;
}

bool Memory::saveData(const char* fileName, timeStruct* time_val, SensorData* sensor_val){
  if (time_val == nullptr || sensor_val == nullptr) {
    Serial.println("Null pointer passed to saveData");
    return false;
  }
  
  uint64_t availableSpace = getAvailableSpace();
  if (availableSpace < MIN_FREE_SPACE_BYTES) {
    Serial.printf("LittleFS almost full! Available: %llu bytes\n", availableSpace);
    return false;
  }
  
  if (isFileTooLarge(fileName, MAX_FILE_SIZE_BYTES)) {
    Serial.println("File too large (>500KB), consider rotation");
  }
  
  static char dataBuffer[512];
    
  int written = snprintf(dataBuffer, sizeof(dataBuffer), 
    "%s,%s,"
    "%.1f,%.1f,%u,%.1f,%u,%u,%u,"
    "%.1f,%u,%.1f,%.1f,%u,%.1f,%lu,%.1f,%u\r\n",
    
    time_val->dateStr, time_val->timeStr,
    
    // Soil
    (double)sensor_val->soil_humi / 10.0,
    (double)sensor_val->soil_temp / 10.0,
    sensor_val->soil_ec,
    (double)sensor_val->soil_ph / 10.0,
    sensor_val->soil_N, 
    sensor_val->soil_P, 
    sensor_val->soil_K,
    
    // Weather
    (double)sensor_val->windSpeed / 10.0,
    sensor_val->windDir_Deg,
    (double)sensor_val->air_humidity / 10.0,
    (double)sensor_val->air_temperature / 10.0,
    sensor_val->CO2,
    (double)sensor_val->pressure / 10.0,
    sensor_val->illuminance,
    (double)sensor_val->rainfall / 10.0,
    sensor_val->solar
  );
  
  if (written < 0 || written >= sizeof(dataBuffer)) {
      Serial.println("Buffer overflow in snprintf");
      return false;
  }
  
  bool result = append(LittleFS, fileName, dataBuffer);
  return result;
}

uint64_t Memory::getAvailableSpace() {
  return LittleFS.totalBytes() - LittleFS.usedBytes();
}

bool Memory::isFileTooLarge(const char* path, uint32_t maxSizeBytes) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  uint32_t size = file.size();
  file.close();
  return size > maxSizeBytes;
}

bool Memory::isUsageOverThreshold(uint8_t thresholdPercent) {
  uint64_t totalBytes = LittleFS.totalBytes();
  uint64_t usedBytes = LittleFS.usedBytes();
  if (totalBytes == 0) return false;
  uint8_t usagePercent = (usedBytes * 100) / totalBytes;
  if (usagePercent >= thresholdPercent) {
    Serial.printf("LittleFS usage at %u%% (>= %u%% threshold)\n", usagePercent, thresholdPercent);
    return true;
  }
  return false;
}

int Memory::countDataLines(const char* fileName) {
  if (!LittleFS.exists(fileName)) return 0;
  File file = LittleFS.open(fileName, "r");
  if (!file) return 0;

  file.readStringUntil('\n');

  int count = 0;
  uint32_t linesScanned = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    linesScanned++;
    if ((linesScanned % FILE_WDT_FEED_INTERVAL_LINES) == 0) {
      feedFileWDT();
    }
    line.trim();
    if (line.length() > 0) {
      count++;
    }
  }
  file.close();
  return count;
}

static float parseNextField(String& line, int& pos) {
  int commaIndex = line.indexOf(',', pos);
  String value;
  if (commaIndex < 0) {
    value = line.substring(pos);
    pos = -1;
  } else {
    value = line.substring(pos, commaIndex);
    pos = commaIndex + 1;
  }
  return value.toFloat();
}

static int32_t parseIntNextField(String& line, int& pos) {
  int commaIndex = line.indexOf(',', pos);
  String value;
  if (commaIndex < 0) {
    value = line.substring(pos);
    pos = -1;
  } else {
    value = line.substring(pos, commaIndex);
    pos = commaIndex + 1;
  }
  return value.toInt();
}

bool Memory::readDataRecords(const char* fileName, DataRecord* records, int maxRecords) {
  if (!LittleFS.exists(fileName)) return false;
  File file = LittleFS.open(fileName, "r");
  if (!file) return false;

  file.readStringUntil('\n');

  int index = 0;
  uint32_t linesScanned = 0;
  while (file.available() && index < maxRecords) {
    String line = file.readStringUntil('\n');
    linesScanned++;
    if ((linesScanned % FILE_WDT_FEED_INTERVAL_LINES) == 0) {
      feedFileWDT();
    }
    line.trim();
    if (line.length() == 0) continue;

    int pos = 0;

    int commaIndex = line.indexOf(',', pos);
    if (commaIndex < 0) continue;
    String dateStr = line.substring(pos, commaIndex);
    pos = commaIndex + 1;

    commaIndex = line.indexOf(',', pos);
    if (commaIndex < 0) continue;
    String timeStr = line.substring(pos, commaIndex);
    pos = commaIndex + 1;

    int d1 = dateStr.indexOf('/');
    int d2 = dateStr.lastIndexOf('/');
    if (d1 < 0 || d2 < 0 || d1 == d2) continue;

    records[index].date = (uint8_t)dateStr.substring(0, d1).toInt();
    records[index].month = (uint8_t)dateStr.substring(d1 + 1, d2).toInt();
    records[index].year = (uint8_t)(dateStr.substring(d2 + 1).toInt() % 100);
    if (records[index].date == 0 || records[index].date > 31 ||
        records[index].month == 0 || records[index].month > 12) {
      continue;
    }

    records[index].hour = (uint8_t)timeStr.substring(0, 2).toInt();
    records[index].minute = (uint8_t)timeStr.substring(3, 5).toInt();
    if (records[index].hour > 23 || records[index].minute > 59) {
      continue;
    }

    records[index].data.soil_humi = (uint16_t)(parseNextField(line, pos) * 10.0f);
    records[index].data.soil_temp = (int16_t)(parseNextField(line, pos) * 10.0f);
    records[index].data.soil_ec = (uint16_t)parseIntNextField(line, pos);
    records[index].data.soil_ph = (uint8_t)(parseNextField(line, pos) * 10.0f);
    records[index].data.soil_N = (uint16_t)parseIntNextField(line, pos);
    records[index].data.soil_P = (uint16_t)parseIntNextField(line, pos);
    records[index].data.soil_K = (uint16_t)parseIntNextField(line, pos);

    records[index].data.windSpeed = (uint16_t)(parseNextField(line, pos) * 10.0f);
    records[index].data.windDir_Deg = (uint16_t)parseIntNextField(line, pos);
    records[index].data.air_humidity = (uint16_t)(parseNextField(line, pos) * 10.0f);
    records[index].data.air_temperature = (int16_t)(parseNextField(line, pos) * 10.0f);
    records[index].data.CO2 = (uint16_t)parseIntNextField(line, pos);
    records[index].data.pressure = (uint16_t)(parseNextField(line, pos) * 10.0f);
    records[index].data.illuminance = (uint32_t)parseIntNextField(line, pos);
    records[index].data.rainfall = (uint16_t)(parseNextField(line, pos) * 10.0f);
    records[index].data.solar = (uint16_t)parseIntNextField(line, pos);

    records[index].valid = 1;
    index++;
  }

  file.close();
  return index > 0;
}

bool Memory::removeFirstDataLines(const char* fileName, uint16_t linesToRemove) {
  if (linesToRemove == 0) return true;
  if (!LittleFS.exists(fileName)) return false;

  static const char* swapFileName = "/DATA_TEMP_SWAP.csv";
  File source = LittleFS.open(fileName, "r");
  if (!source) return false;

  LittleFS.remove(swapFileName);
  File swap = LittleFS.open(swapFileName, FILE_WRITE);
  if (!swap) {
    source.close();
    Serial.println("Failed to open swap file for temp rewrite");
    return false;
  }

  String header = source.readStringUntil('\n');
  header.trim();
  if (header.length() > 0) {
    swap.print(header);
    swap.print("\r\n");
  }

  uint16_t removed = 0;
  uint16_t kept = 0;
  uint32_t linesScanned = 0;
  while (source.available()) {
    String line = source.readStringUntil('\n');
    linesScanned++;
    if ((linesScanned % FILE_WDT_FEED_INTERVAL_LINES) == 0) {
      feedFileWDT();
    }
    line.trim();
    if (line.length() == 0) continue;

    if (removed < linesToRemove) {
      removed++;
      continue;
    }

    swap.print(line);
    swap.print("\r\n");
    kept++;
  }

  source.close();
  feedFileWDT();
  swap.flush();
  swap.close();

  if (!LittleFS.remove(fileName)) {
    LittleFS.remove(swapFileName);
    Serial.println("Failed to remove old temp file");
    return false;
  }

  if (!LittleFS.rename(swapFileName, fileName)) {
    Serial.println("Failed to rename rewritten temp file");
    return false;
  }

  Serial.printf("Removed %u delivered rows from %s; %u rows remain.\n", removed, fileName, kept);
  return removed == linesToRemove;
}

bool Memory::clearDataRows(const char* fileName) {
  if (!LittleFS.exists(fileName)) return false;

  File file = LittleFS.open(fileName, "r");
  if (!file) return false;

  String header = file.readStringUntil('\n');
  file.close();

  static const char* swapFileName = "/DATA_CLEAR_SWAP.csv";
  LittleFS.remove(swapFileName);
  File swap = LittleFS.open(swapFileName, FILE_WRITE);
  if (!swap) return false;

  header.trim();
  if (header.length() > 0) {
    swap.print(header);
    swap.print("\r\n");
  }
  swap.flush();
  swap.close();

  if (!LittleFS.remove(fileName)) {
    LittleFS.remove(swapFileName);
    return false;
  }

  if (!LittleFS.rename(swapFileName, fileName)) {
    return false;
  }

  Serial.printf("Cleared all data rows from %s (header preserved).\n", fileName);
  return true;
}
