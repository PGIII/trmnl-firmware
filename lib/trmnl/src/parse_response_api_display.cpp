
#include <ArduinoJson.h>
#include <special_function.h>
#include <trmnl_log.h>

#include "api_response_parsing.h"

// Sane upper bound on an individual prefetch_batch entry's filename length;
// entries longer than this are treated as malformed and skipped (the actual
// on-flash name is further shortened by filesystem_fix_filename).
#define PREFETCH_BATCH_FILENAME_MAX_LEN 128

ApiDisplayResponse parseResponse_apiDisplay(String &payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Log_error("JSON deserialization error.");
    return ApiDisplayResponse{
        .outcome = ApiDisplayOutcome::DeserializationError,
        .error_detail = error.c_str(),
        .status = 0,
        .image_url = "",
        .image_url_timeout = 0,
        .filename = "",
        .update_firmware = false,
        .maximum_compatibility = false,
        .firmware_url = "",
        .refresh_rate = 0,
        .temp_profile = 0,
        .reset_firmware = false,
        .special_function = SF_NONE,
        .action = "",
        .touchbar_mode = "",
        .prefetch = false,
        .prefetch_batch_count = 0};
  }
  String special_function_str = doc["special_function"];
  // Convert the temperature profile ("default", "a", "b", "c")
  // into an integer value (0,1,2,3)
  String tp = doc["temperature_profile"];
  uint32_t u32TP = 0; // default
  if (tp == "a")
    u32TP = 1;
  else if (tp == "b")
    u32TP = 2;
//     else if (tp == "c") u32TP = 3;

  ApiDisplayResponse response = {
      .outcome = ApiDisplayOutcome::Ok,
      .error_detail = "",
      .status = doc["status"],
      .image_url = doc["image_url"] | "",
      .image_url_timeout = doc["image_url_timeout"],
      .filename = doc["filename"] | "",
      .update_firmware = doc["update_firmware"],
      // server doesn't return this flag if device.firmware_version <= 1.6.2
      .maximum_compatibility = doc["maximum_compatibility"] | false,
      .firmware_url = doc["firmware_url"] | "",
      .refresh_rate = doc["refresh_rate"],
      .temp_profile = u32TP,
      .reset_firmware = doc["reset_firmware"],
      .special_function = parseSpecialFunction(special_function_str),
      .action = doc["action"] | "",
      .touchbar_mode = doc["touchbar_mode"] | "",
      // server doesn't return this flag unless the caller opted into prefetching
      .prefetch = doc["prefetch"] | false,
      .prefetch_batch_count = 0};

  // Opt-in batch prefetch: a list of extra images to cache alongside the
  // primary one so a multi-step recipe can warm its whole local browse order
  // in a single wake. Absent/null/empty is the default (nothing to add),
  // which keeps older servers that don't send this field at all safe.
  JsonArray batch = doc["prefetch_batch"].as<JsonArray>();
  for (JsonVariant entry : batch) {
    if (response.prefetch_batch_count >= PREFETCH_BATCH_MAX_ENTRIES) {
      Log_error("prefetch_batch: ignoring extra entries beyond the %d accepted", PREFETCH_BATCH_MAX_ENTRIES);
      break;
    }
    String entry_filename = entry["filename"] | "";
    String entry_url = entry["url"] | "";
    if (entry_filename.isEmpty() || entry_url.isEmpty() ||
        entry_filename.length() > PREFETCH_BATCH_FILENAME_MAX_LEN) {
      Log_error("prefetch_batch: skipping malformed entry (missing filename/url or filename too long)");
      continue;
    }
    response.prefetch_batch[response.prefetch_batch_count].filename = entry_filename;
    response.prefetch_batch[response.prefetch_batch_count].url = entry_url;
    response.prefetch_batch_count++;
  }

  return response;
}
