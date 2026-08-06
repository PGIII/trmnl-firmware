#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <cinttypes>
#include <http_client.h>
#include <ota_schedule.h>
#include <services/firmware_update.h>
#include <trmnl_log.h>
#include <wifi_network.h>

#include "esp_ota_ops.h"

#ifdef BOARD_TRMNL_X
#include <WifiCaptive.h>
#include <globals.h>
#include <modem.h>
#endif

FirmwareUpdateService::FirmwareUpdateService(Persistence &persistence, Clock &clock,
                                             int32_t wifiConnectionRssiThreshold)
    : _persistence(persistence), _clock(clock), _wifiConnectionRssiThreshold(wifiConnectionRssiThreshold),
      _firmwareUrl{0} {}

bool FirmwareUpdateService::isUpdateDue(bool update_firmware, const String &firmware_url) {
  Log_info("%s [%d]: update_firmware: %d\r\n", __FILE__, __LINE__, update_firmware);
  if (!update_firmware) return false;

  Log_info("%s [%d]: update firmware. Check URL\r\n", __FILE__, __LINE__);
  if (firmware_url.length() == 0) {
    Log_error("%s [%d]: Empty URL\r\n", __FILE__, __LINE__);
    return false;
  }

  firmware_url.toCharArray(_firmwareUrl, sizeof(_firmwareUrl));
  Log_info("%s [%d]: firmware_url: %s\r\n", __FILE__, __LINE__, _firmwareUrl);

  uint32_t now = _clock.getTime();
  uint32_t lastOta = otaLastAttempt(_persistence);
  if (!otaAttemptDue(now, lastOta)) {
        // DIAGNOSTIC: otaAttemptDue() returns false both when the clock is unavailable and
        // when the cooldown is genuinely still active, and info-level logs never leave the
        // device. Disambiguate here with submitted (ERROR-level) logs so we can tell, from
        // the server side, which case is actually happening.
    if (now == 0 && lastOta != 0) {
      Log_error_submit("%s [%d]: OTA check: clock unavailable (now=0), cannot evaluate cooldown; lastOta=%" PRIu32
                        "\r\n",
                        __FILE__, __LINE__, lastOta);
    } else {
      uint32_t remaining =
        (lastOta + OTA_RETRY_INTERVAL_SECONDS > now) ? (lastOta + OTA_RETRY_INTERVAL_SECONDS - now) : 0;
      Log_error_submit(
        "%s [%d]: OTA check: cooldown active, now=%" PRIu32 ", lastOta=%" PRIu32 ", remaining=%" PRIu32 "s\r\n",
        __FILE__, __LINE__, now, lastOta, remaining);
    }
    return false;
  }

    // DIAGNOSTIC: submitted so we can confirm the OTA is actually being attempted.
  Log_error_submit("%s [%d]: OTA check: cooldown satisfied, proceeding with download (now=%" PRIu32
                    ", lastOta=%" PRIu32 ")\r\n",
                    __FILE__, __LINE__, now, lastOta);
  return true;
}

bool FirmwareUpdateService::performFirmwareUpdate() {
#ifdef BOARD_TRMNL_X
  if (g_modem && WifiCaptivePortal.getLastCredentials().is5GHz) {
    Log_info("%s [%d]: Starting modem OTA download...\r\n", __FILE__, __LINE__);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
      Log_fatal("%s [%d]: No OTA partition available\r\n", __FILE__, __LINE__);
      _failureMessage = FW_UPDATE_FAILED;
      return false;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
      Log_fatal("%s [%d]: esp_ota_begin failed: %s\r\n", __FILE__, __LINE__, esp_err_to_name(err));
      _failureMessage = FW_UPDATE_FAILED;
      return false;
    }

    bool write_ok = true;
    auto result = g_modem->httpGet(
      String(_firmwareUrl),
      [&](const uint8_t *data, size_t len) -> bool {
        esp_err_t e = esp_ota_write(ota_handle, data, len);
        if (e != ESP_OK) {
          Log_fatal("%s [%d]: esp_ota_write failed: %s\r\n", __FILE__, __LINE__, esp_err_to_name(e));
          write_ok = false;
          return false;
        }
        return true;
      },
      0, "", 120000UL);

    if (!result.ok || !write_ok) {
      esp_ota_abort(ota_handle);
      Log_fatal("%s [%d]: Modem OTA download failed\r\n", __FILE__, __LINE__);
      _failureMessage = FW_UPDATE_FAILED;
      return false;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
      Log_fatal("%s [%d]: esp_ota_end failed: %s\r\n", __FILE__, __LINE__, esp_err_to_name(err));
      _failureMessage = FW_UPDATE_FAILED;
      return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
      Log_fatal("%s [%d]: esp_ota_set_boot_partition failed: %s\r\n", __FILE__, __LINE__, esp_err_to_name(err));
      _failureMessage = FW_UPDATE_FAILED;
      return false;
    }

    Log_info("%s [%d]: Modem OTA successful. Rebooting...\r\n", __FILE__, __LINE__);
    return true;
  }
#endif

  if (!ensureWifiConnected()) {
    Log_fatal("%s [%d]: Unable to reconnect WiFi for firmware update\r\n", __FILE__, __LINE__);
    _failureMessage = API_FIRMWARE_UPDATE_ERROR;
    return false;
  }

  bool ota_ok = false;
  withHttp(_firmwareUrl, [&](HTTPClient *https, HttpError errorCode) -> bool {
    if (errorCode != HttpError::HTTPCLIENT_SUCCESS || !https) {
      Log_fatal("%s [%d]: Unable to connect for firmware update\r\n", __FILE__, __LINE__);
      _failureMessage = WiFi.RSSI() > _wifiConnectionRssiThreshold ? API_FIRMWARE_UPDATE_ERROR : WIFI_WEAK;
      return false;
    }

    int httpCode = https->GET();
    if (httpCode == HTTP_CODE_OK) {
      Log_info("%s [%d]: Downloading .bin file...\r\n", __FILE__, __LINE__);

      size_t contentLength = https->getSize();
      if (Update.begin(contentLength)) {
        Log_info("%s [%d]: Firmware update start\r\n", __FILE__, __LINE__);

        if (Update.writeStream(https->getStream())) {
          if (Update.end(true)) {
            Log_info("%s [%d]: Firmware update successful. Rebooting...\r\n", __FILE__, __LINE__);
            ota_ok = true;
          } else {
            Log_fatal("%s [%d]: Firmware update failed!\r\n", __FILE__, __LINE__);
            _failureMessage = FW_UPDATE_FAILED;
          }
        } else {
          Log_fatal("%s [%d]: Write to firmware update stream failed!\r\n", __FILE__, __LINE__);
          _failureMessage = FW_UPDATE_FAILED;
        }
      } else {
        Log_fatal("%s [%d]: Begin firmware update failed!\r\n", __FILE__, __LINE__);
        _failureMessage = FW_UPDATE_FAILED;
      }
    } else {
      Log_fatal("%s [%d]: Firmware GET failed, code: %d\r\n", __FILE__, __LINE__, httpCode);
      _failureMessage = API_FIRMWARE_UPDATE_ERROR;
    }
    return false;
  });
  return ota_ok;
}

FirmwareUpdateResult FirmwareUpdateService::performUpdate() {
  _failureMessage = NONE;
  FirmwareUpdateResult result;

  uint32_t now = _clock.getTime();
  if (!performFirmwareUpdate()) {
    Log_info("%s [%d]: OTA update failed, storing the timestamp to prevent boot looping.\r\n", __FILE__, __LINE__);
        // DIAGNOSTIC: submitted so the outcome of an OTA attempt is visible to the server.
    Log_error_submit("%s [%d]: OTA outcome: FAILED, failureMessage=%d\r\n", __FILE__, __LINE__, _failureMessage);
    otaRecordAttempt(_persistence, now);
    result.failureMessage = _failureMessage;
    return result;
  }

    // DIAGNOSTIC: submitted so the outcome of an OTA attempt is visible to the server.
  Log_error_submit("%s [%d]: OTA outcome: SUCCESS\r\n", __FILE__, __LINE__);
  result.updated = true;
  return result;
}
