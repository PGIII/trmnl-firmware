#include <ota_schedule.h>

bool otaAttemptDue(uint32_t now, uint32_t lastOta) {
  if (lastOta == 0) return true;
  if (now == 0) return false;
  // now and lastOta are uint32_t, so if the clock moved backward (e.g. NTP resync,
  // RTC glitch) then now - lastOta would underflow to ~4.29 billion and pass the
  // interval check, causing the device to retry OTA on every wake forever. A stored
  // timestamp in the future means the clock moved backward and the stored value is
  // untrustworthy, so we return false to prevent that unbounded retry-every-wake loop.
  // The caller is responsible for repairing the stored timestamp so this self-heals
  // instead of wedging forever.
  if (now < lastOta) return false;
  return (now - lastOta) >= OTA_RETRY_INTERVAL_SECONDS;
}

uint32_t otaLastAttempt(Persistence &persistence) { return persistence.readUint(OTA_LAST_ATTEMPT_KEY, 0); }

void otaRecordAttempt(Persistence &persistence, uint32_t now) {
  if (now != 0) persistence.writeUint(OTA_LAST_ATTEMPT_KEY, now);
}
