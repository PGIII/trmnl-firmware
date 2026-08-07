#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "hardware_types.h"
#include "special_function.h"
#include "trmnl_log.h"

enum class ApiSetupOutcome { Ok, DeserializationError, StatusError };

struct ApiSetupResponse {
  ApiSetupOutcome outcome;
  uint16_t status;
  String api_key;
  String friendly_id;
  String image_url;
  String message;
};

struct ApiSetupInputs {
  String baseUrl;
  String macAddress;
  String firmwareVersion;
  String model;
};

enum class ApiDisplayOutcome {
  Ok,
  DeserializationError,
};

// Maximum number of extra images accepted from a `prefetch_batch` array in a
// single /api/display response. ESP32 RAM can't hold an unbounded recipe's
// worth of image buffers at once, and no real recipe needs more than a
// handful of frames per wake -- entries beyond this are ignored rather than
// overflowing a fixed-size array.
#define PREFETCH_BATCH_MAX_ENTRIES 16

struct PrefetchBatchEntry {
  String filename;
  String url;
};

struct ApiDisplayResponse {
  ApiDisplayOutcome outcome;
  String error_detail;
  uint64_t status;
  String image_url;
  uint32_t image_url_timeout;
  String filename;
  bool update_firmware;
  bool maximum_compatibility;
  String firmware_url;
  uint64_t refresh_rate;
  uint32_t temp_profile;
  bool reset_firmware;
  SPECIAL_FUNCTION special_function;
  String action;
  String touchbar_mode;
  // Opt-in: when true, the device should download and cache the image (so it
  // takes its place in the local browse order) without showing it on screen.
  // Absent on older servers, in which case behaviour is unchanged.
  bool prefetch;
  // Opt-in: additional images to download and cache (without displaying) in
  // this same wake, alongside the primary image/prefetch above -- lets a
  // multi-step recipe warm its entire local browse order in a single wake
  // instead of one wake per frame. Absent, null, or an empty array means
  // nothing extra to fetch, which is the safe default for older servers.
  // Capped at PREFETCH_BATCH_MAX_ENTRIES; additional entries are ignored.
  PrefetchBatchEntry prefetch_batch[PREFETCH_BATCH_MAX_ENTRIES];
  uint8_t prefetch_batch_count;
};

struct ApiDisplayInputs {
  String baseUrl;
  String apiKey;
  String friendlyId;
  String updateSource;
  uint32_t refreshRate;
  String macAddress;
  float batteryVoltage;
  ChargingStatus chargingStatus;
#ifdef BOARD_TRMNL_X
  int batteryCount;
  int batteryCurrent;
  int currentBatteryCapacity;
  int maxBatteryCapacity;
  float batteryTemperature;
  int stateOfCharge;
  int stateOfHealth;
#endif
  String firmwareVersion;
  String firmwareCommit;
  String model;
  int rssi;
  String wifiBand;
  int displayWidth;
  int displayHeight;
  SPECIAL_FUNCTION specialFunction;
  UsbStatus usbStatus;
  bool imageCached;
  int prevWakeTime;
};

struct ApiLogInputs {
  String macAddress;
  String apiKey;
};

typedef struct {
  char current_image[100];
  char current_error_message[100];
} ScreenStatus;

typedef struct DeviceStatusStamp {
  int8_t wifi_rssi_level;
  char wifi_status[30];
  uint32_t refresh_rate;
  uint32_t time_since_last_sleep;
  char current_fw_version[10];
  char special_function[100];
  float battery_voltage;
  char wakeup_reason[30];
  uint32_t free_heap_size;
  uint32_t max_alloc_size;

  ScreenStatus screen_status;

} DeviceStatusStamp;

struct LogWithDetails {
  DeviceStatusStamp deviceStatusStamp;
  time_t timestamp;
  int codeline;
  const char *sourceFile;
  const char *logMessage;
  uint32_t logId;
  String filenameCurrent;
  String filenameNew;
  bool logRetry;
  int retryAttempt;
  LogLevel level;
};
