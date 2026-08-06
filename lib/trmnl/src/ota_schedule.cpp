#include <ota_schedule.h>

bool otaAttemptDue(uint32_t now, uint32_t lastOta) {
  if (lastOta == 0) return true;
  if (now == 0) return false;
    // now and lastOta are uint32_t, so if the clock moved backward (e.g. NTP resync,
    // RTC glitch) then now - lastOta would underflow to ~4.29 billion and pass the
    // interval check, causing the device to retry OTA on every wake forever. A backwards
    // clock means the stored timestamp is untrustworthy, so we prefer attempting the OTA
    // (and re-recording a fresh timestamp) over silently wedging retries forever.
  if (now < lastOta) return true;
  return (now - lastOta) >= OTA_RETRY_INTERVAL_SECONDS;
}

uint32_t otaLastAttempt(Persistence &persistence) { return persistence.readUint(OTA_LAST_ATTEMPT_KEY, 0); }

void otaRecordAttempt(Persistence &persistence, uint32_t now) {
  if (now != 0) persistence.writeUint(OTA_LAST_ATTEMPT_KEY, now);
}
