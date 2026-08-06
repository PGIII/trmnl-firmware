#pragma once

#include <persistence_interface.h>
#include <stdint.h>

#ifndef OTA_RETRY_INTERVAL_SECONDS_OVERRIDE
#define OTA_RETRY_INTERVAL_SECONDS_OVERRIDE (24UL * 60 * 60)
#endif
constexpr uint32_t OTA_RETRY_INTERVAL_SECONDS = OTA_RETRY_INTERVAL_SECONDS_OVERRIDE;
#define OTA_LAST_ATTEMPT_KEY "last_ota"

bool otaAttemptDue(uint32_t now, uint32_t lastOta);
uint32_t otaLastAttempt(Persistence &persistence);
void otaRecordAttempt(Persistence &persistence, uint32_t now);
