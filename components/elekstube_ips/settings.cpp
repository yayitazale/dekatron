#include "settings.h"

#include <cctype>

#include "esphome/core/log.h"

namespace esphome {
namespace elekstube_ips {

static const char *const TAG = "settings";

static constexpr uint32_t SETTINGS_VERSION = 1;
static constexpr uint32_t SETTINGS_KEY = 0x4F545331;  // "OTS1"

void Settings::load() {
  this->preference_ = global_preferences->make_preference<SettingsData>(SETTINGS_KEY);
  SettingsData data{};
  this->loaded_ = this->preference_.load(&data) && data.version == SETTINGS_VERSION;
  if (this->loaded_) {
    data.timezone[TIMEZONE_BYTES - 1] = '\0';
    this->data_ = data;
    return;
  }
  this->data_ = SettingsData{};
  this->data_.version = SETTINGS_VERSION;
  this->data_.boot_animation = 1;
}

bool Settings::save() {
  const bool saved = this->preference_.save(&this->data_);
  global_preferences->sync();
  return saved;
}

#ifdef USE_TIME_TIMEZONE

namespace {

using time::DSTRule;
using time::DSTRuleType;

// A cursor over the text, so that each piece of the grammar reads like the grammar does.
struct Reader {
  const char *p;

  bool done() const { return *this->p == '\0'; }
  bool accept(char c) {
    if (*this->p != c) {
      return false;
    }
    this->p++;
    return true;
  }

  bool number(int32_t *value) {
    if (!isdigit(static_cast<unsigned char>(*this->p))) {
      return false;
    }
    int32_t result = 0;
    while (isdigit(static_cast<unsigned char>(*this->p))) {
      result = result * 10 + (*this->p++ - '0');
      if (result > 100000) {
        return false;
      }
    }
    *value = result;
    return true;
  }

  // The name of a zone: three or more letters, or anything between angle brackets.
  bool name() {
    if (this->accept('<')) {
      while (*this->p != '\0' && *this->p != '>') {
        this->p++;
      }
      return this->accept('>');
    }
    int letters = 0;
    while (isalpha(static_cast<unsigned char>(*this->p))) {
      this->p++;
      letters++;
    }
    return letters >= 3;
  }

  // [+-]hh[:mm[:ss]], in seconds.
  bool time(int32_t *seconds) {
    int32_t sign = 1;
    if (this->accept('-')) {
      sign = -1;
    } else {
      this->accept('+');
    }
    int32_t hours, minutes = 0, secs = 0;
    if (!this->number(&hours) || hours > 167) {
      return false;
    }
    if (this->accept(':')) {
      if (!this->number(&minutes) || minutes > 59) {
        return false;
      }
      if (this->accept(':') && (!this->number(&secs) || secs > 59)) {
        return false;
      }
    }
    *seconds = sign * (hours * 3600 + minutes * 60 + secs);
    return true;
  }

  // Mm.w.d, Jn or n, followed by an optional /time. A transition happens at 02:00 unless it says so.
  bool rule(DSTRule *rule) {
    *rule = DSTRule{};
    rule->time_seconds = 7200;
    int32_t value;
    if (this->accept('M')) {
      rule->type = DSTRuleType::MONTH_WEEK_DAY;
      int32_t month, week, day;
      if (!this->number(&month) || !this->accept('.') || !this->number(&week) || !this->accept('.') ||
          !this->number(&day) || month < 1 || month > 12 || week < 1 || week > 5 || day > 6) {
        return false;
      }
      rule->month = static_cast<uint8_t>(month);
      rule->week = static_cast<uint8_t>(week);
      rule->day_of_week = static_cast<uint8_t>(day);
    } else if (this->accept('J')) {
      rule->type = DSTRuleType::JULIAN_NO_LEAP;
      if (!this->number(&value) || value < 1 || value > 365) {
        return false;
      }
      rule->day = static_cast<uint16_t>(value);
    } else {
      rule->type = DSTRuleType::DAY_OF_YEAR;
      if (!this->number(&value) || value > 365) {
        return false;
      }
      rule->day = static_cast<uint16_t>(value);
    }
    if (this->accept('/')) {
      return this->time(&rule->time_seconds);
    }
    return true;
  }
};

}  // namespace

bool parse_posix_timezone(const char *text, time::ParsedTimezone *timezone) {
  Reader reader{text};
  *timezone = time::ParsedTimezone{};
  int32_t offset;
  if (!reader.name() || !reader.time(&offset)) {
    return false;
  }
  timezone->std_offset_seconds = offset;
  if (reader.done()) {
    return true;
  }
  // The zone has summer time, which is one hour ahead unless it says otherwise.
  if (!reader.name()) {
    return false;
  }
  timezone->dst_offset_seconds = offset - 3600;
  if (*reader.p != ',' && !reader.time(&timezone->dst_offset_seconds)) {
    return false;
  }
  if (!reader.accept(',') || !reader.rule(&timezone->dst_start) || !reader.accept(',') ||
      !reader.rule(&timezone->dst_end)) {
    return false;
  }
  return reader.done();
}

bool Settings::apply_timezone() const {
  time::ParsedTimezone timezone;
  if (this->data_.timezone[0] == '\0' || !parse_posix_timezone(this->data_.timezone, &timezone)) {
    return false;
  }
  time::set_global_tz(timezone);
  ESP_LOGI(TAG, "Timezone %s: offset %d s, summer time %s", this->data_.timezone,
           static_cast<int>(timezone.std_offset_seconds), timezone.has_dst() ? "yes" : "no");
  return true;
}

#else

bool Settings::apply_timezone() const { return false; }

#endif  // USE_TIME_TIMEZONE

}  // namespace elekstube_ips
}  // namespace esphome
