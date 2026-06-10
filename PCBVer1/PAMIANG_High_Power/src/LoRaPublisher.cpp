/* LoRa data publishing — extracted from main.cpp */
#include "LoRaPublisher.h"
#include "utilities.h"
#include "TxUtilities.h"
#include <esp_task_wdt.h>
#include <LittleFS.h>

LoRaPublisher::LoRaPublisher()
    : _ctx(nullptr), _sequence(0) {}

void LoRaPublisher::begin(LoRaPublisherCtx* ctx) {
    _ctx = ctx;
}

/* ── Send ACK packet via LoRa ── */
bool LoRaPublisher::sendAck(uint16_t seq) {
    LoRaAckPacket ack;
    ack.magic = LORA_MAGIC;
    ack.type  = LORA_PKT_ACK;
    ack.seq   = seq;

    bool ok = _ctx->loraHandler->send((const uint8_t*)&ack, sizeof(ack));
    if (ok) {
        Serial.printf("[LoRa-ACK] Sent ACK for seq=%u\n", seq);
    } else {
        Serial.printf("[LoRa-ACK] Failed to send ACK for seq=%u\n", seq);
    }
    return ok;
}

/* ── Publish data records via LoRa with ACK ── */
bool LoRaPublisher::publishData() {
    int recordCount = _ctx->memory->countDataLines(_ctx->tempDataPath);
    Serial.printf("[LoRa] %d records in temp file\n", recordCount);

    if (recordCount <= 0) {
        Serial.println(F("[LoRa] No records to publish."));
        return false;
    }

    if (!*_ctx->loraAvailable) {
        Serial.println(F("[LoRa] Re-initializing..."));
        if (_ctx->loraHandler->init(*_ctx->loraSerial)) {
            *_ctx->loraAvailable = true;
        } else {
            Serial.println(F("[LoRa] Init failed — cannot publish"));
            return false;
        }
    }

    int maxRecords = PUBLISH_MAX_RECORDS;
    if (_ctx->publishBatchSize && *_ctx->publishBatchSize > 0) {
        maxRecords = min(maxRecords, (int)*_ctx->publishBatchSize);
    }
    int toPublish = (recordCount > maxRecords) ? maxRecords : recordCount;

    DataRecord records[PUBLISH_MAX_RECORDS];
    memset(records, 0, sizeof(records));
    if (!_ctx->memory->readDataRecords(_ctx->tempDataPath, records, toPublish)) {
        Serial.println(F("[LoRa] Failed to read temp records"));
        return false;
    }

    int published = 0;
    for (int i = 0; i < toPublish; i++) {
        if (records[i].valid != 1) continue;

        LoRaDataPacket pkt;
        memset(&pkt, 0, sizeof(pkt));

        pkt.hdr.magic = LORA_MAGIC;
        pkt.hdr.type  = LORA_PKT_DATA;
        pkt.hdr.seq   = _sequence;
        pkt.hdr.vt    = *_ctx->batteryVoltage;

        pkt.date   = records[i].date;
        pkt.month  = records[i].month;
        pkt.year   = records[i].year;
        pkt.hour   = records[i].hour;
        pkt.minute = records[i].minute;

        pkt.sensor = records[i].data;

        pkt.ble_valid = records[i].ble_valid;
        if (pkt.ble_valid)
            pkt.ble = records[i].ble;

        bool acked = false;
        for (uint8_t retry = 0; retry <= LORA_ACK_MAX_RETRIES; retry++) {
            if (retry > 0) {
                Serial.printf("[LoRa] Retry %u/%u for record %d\n",
                              retry, LORA_ACK_MAX_RETRIES, i + 1);
            }

            Serial.printf("[LoRa] Sending record %d/%d seq=%u (%u bytes)\n",
                          i + 1, toPublish, pkt.hdr.seq, sizeof(pkt));

            bool ok = _ctx->loraHandler->send((const uint8_t*)&pkt, sizeof(pkt));
            if (!ok) {
                Serial.printf("[LoRa] Send failed at record %d.\n", i + 1);
                delay(LORA_ACK_TX_SETTLE);
                continue;
            }

            delay(LORA_ACK_TX_SETTLE);
            _ctx->loraHandler->flushInput();

            LoRaAckPacket ack;
            acked = _ctx->loraHandler->waitAck(ack, pkt.hdr.seq, LORA_ACK_TIMEOUT_MS);
            if (acked) break;

            Serial.printf("[LoRa] No ACK for seq=%u (attempt %u/%u)\n",
                          pkt.hdr.seq, retry + 1, LORA_ACK_MAX_RETRIES + 1);
            esp_task_wdt_reset();
        }

        if (!acked) {
            Serial.printf("[LoRa] Gave up on record %d after %u attempts. Stopping.\n",
                          i + 1, LORA_ACK_MAX_RETRIES + 1);
            break;
        }

        _sequence++;
        published++;
        esp_task_wdt_reset();
        delay(500);
    }

    if (published > 0) {
        Serial.printf("[LoRa] Removing %d lines from CSV...\n", published);
        Serial.flush();
        _ctx->memory->removeFirstDataLines(_ctx->tempDataPath, published);
        Serial.printf("[LoRa] Published %d/%d record(s)\n", published, toPublish);
        Serial.flush();
    }

    return (published > 0);
}

/* ── Publish heartbeat via LoRa ── */
bool LoRaPublisher::publishHeartbeat() {
    if (!*_ctx->loraAvailable) return false;

    LoRaHeartbeat pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.hdr.magic  = LORA_MAGIC;
    pkt.hdr.type   = LORA_PKT_HEARTBEAT;
    pkt.hdr.seq    = _sequence++;
    pkt.hdr.vt     = *_ctx->batteryVoltage;
    pkt.uptime     = millis() / 1000;
    pkt.heap       = (uint16_t)(ESP.getFreeHeap() & 0xFFFF);

    Serial.printf("[LoRa] Heartbeat (%u bytes)\n", sizeof(pkt));
    bool ok = _ctx->loraHandler->send((const uint8_t*)&pkt, sizeof(pkt));
    if (ok) Serial.println(F("[LoRa] Heartbeat sent"));
    else    Serial.println(F("[LoRa] Heartbeat failed"));
    return ok;
}
