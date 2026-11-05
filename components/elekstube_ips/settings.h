#pragma once

#include <cstdint>
#include <cstring>

#include "esphome/core/defines.h"
#include "esphome/components/time/posix_tz.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace elekstube_ips {

// What the user can change from outside, kept in flash across restarts.
struct SettingsData {
  uint32_t version;
  char timezone[48];  // a POSIX timezone, empty to keep the one compiled in
  uint8_t boot_animation;
  uint8_t password_set;
  uint8_t password_salt[8];
  uint8_t password_hash[32];
};

static constexpr size_t TIMEZONE_BYTES = sizeof(SettingsData::timezone);

#ifdef USE_TIME_TIMEZONE
// Reads a POSIX timezone such as "CET-1CEST,M3.5.0,M10.5.0/3". The firmware has no parser of its own
// at runtime, as ESPHome only parses the one in the YAML when it compiles.
bool parse_posix_timezone(const char *text, time::ParsedTimezone *timezone);
#endif

class Settings {
 public:
  void load();
  // Writes the settings to flash, which takes a moment: do not call it from the drawing task.
  bool save();

  SettingsData &data() { return this->data_; }
  const SettingsData &data() const { return this->data_; }
  // Applies the saved timezone. Returns false when there is none or it cannot be read.
  bool apply_timezone() const;

 protected:
  SettingsData data_{};
  ESPPreferenceObject preference_;
  bool loaded_{false};
};

}  // namespace elekstube_ips
}  // namespace esphome
