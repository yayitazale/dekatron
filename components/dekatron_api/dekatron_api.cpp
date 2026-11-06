// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dekatron_api.h"

#include <esp_random.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <optional>

#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#endif
#include "esphome/components/json/json_util.h"
#include "esphome/components/sha256/sha256.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "web_index.h"

namespace esphome {
namespace dekatron_api {

static const char *const TAG = "dekatron_api";

static const char *const API_PREFIX = "/api/";
static const char *const CSRF_HEADER = "X-Dekatron";
static constexpr size_t MAX_BODY_BYTES = 1024;
static constexpr size_t MAX_PASSWORD_BYTES = 64;
static constexpr size_t SALT_BYTES = 8;
static constexpr uint32_t WRONG_PASSWORD_DELAY_MS = 500;

using elekstube_ips::EntryKind;
using elekstube_ips::Store;

namespace {

void append_json_string(std::string &out, const char *text) {
  out += '"';
  for (const char *p = text; *p != '\0'; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char escaped[8];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      out += escaped;
    } else {
      out += static_cast<char>(c);
    }
  }
  out += '"';
}

void append_number(std::string &out, long value) { out += std::to_string(value); }

void hash_password(const uint8_t *salt, const char *password, uint8_t *hash) {
  sha256::SHA256 hasher;
  hasher.init();
  hasher.add(salt, SALT_BYTES);
  hasher.add(reinterpret_cast<const uint8_t *>(password), strlen(password));
  hasher.calculate();
  hasher.get_bytes(hash);
}

// Takes as long whatever the difference is, so that how long it takes says nothing about the password.
bool same_bytes(const uint8_t *first, const uint8_t *second, size_t length) {
  uint8_t difference = 0;
  for (size_t index = 0; index < length; index++) {
    difference |= first[index] ^ second[index];
  }
  return difference == 0;
}

const char *layout_name(uint8_t images_per_frame) {
  switch (images_per_frame) {
    case elekstube_ips::SCENE_IMAGES:
      return "scene";
    case 1:
      return "common";
    default:
      return "per-tube";
  }
}

}  // namespace

void DekatronApi::setup() {
  this->base_->init();
  this->base_->add_handler_without_auth(this);
}

void DekatronApi::dump_config() {
  ESP_LOGCONFIG(TAG, "Dekatron API: password %s", this->tubes_->settings().data().password_set ? "set" : "not set");
}

DekatronApi::Route DekatronApi::route_(AsyncWebServerRequest *request) const {
  request->url_to(this->url_);
  const bool get = request->method() == HTTP_GET;
  const bool post = request->method() == HTTP_POST;
  if (get && (strcmp(this->url_, "/") == 0 || strcmp(this->url_, "/index.html") == 0)) {
    return Route::WEB;
  }
  if (strncmp(this->url_, API_PREFIX, strlen(API_PREFIX)) != 0 || (!get && !post)) {
    return Route::NONE;
  }
  const char *path = this->url_ + strlen(API_PREFIX);
  if (get) {
    if (strcmp(path, "info") == 0) return Route::INFO;
    if (strcmp(path, "state") == 0) return Route::STATE;
    if (strcmp(path, "store") == 0) return Route::STORE_GET;
  } else {
    if (strcmp(path, "store") == 0) return Route::STORE_POST;
    if (strcmp(path, "play") == 0) return Route::PLAY;
    if (strcmp(path, "stop") == 0) return Route::STOP;
    if (strcmp(path, "settings") == 0) return Route::SETTINGS;
    if (strcmp(path, "password") == 0) return Route::PASSWORD;
    if (strcmp(path, "controls") == 0) return Route::CONTROLS;
    if (strcmp(path, "firmware") == 0) return Route::FIRMWARE;
  }
  return Route::NONE;
}

bool DekatronApi::canHandle(AsyncWebServerRequest *request) const {
  const bool method = request->method() == HTTP_GET || request->method() == HTTP_POST;
  request->url_to(this->url_);
  if (method && request->method() == HTTP_GET && this->route_(request) == Route::WEB) {
    // While the clock offers its own network to set up the Wi-Fi, that page is the one to show.
#ifdef USE_CAPTIVE_PORTAL
    return captive_portal::global_captive_portal == nullptr || !captive_portal::global_captive_portal->is_active();
#else
    return true;
#endif
  }
  return method && strncmp(this->url_, API_PREFIX, strlen(API_PREFIX)) == 0;
}

bool DekatronApi::authorized_(AsyncWebServerRequest *request) {
  const auto &settings = this->tubes_->settings().data();
  if (!settings.password_set) {
    return true;
  }
  const auto header = request->get_header("Authorization");
  static const char BASIC[] = "Basic ";
  if (!header.has_value() || header->size() <= strlen(BASIC) || strncasecmp(header->c_str(), BASIC, strlen(BASIC)) != 0) {
    return false;
  }
  uint8_t decoded[MAX_PASSWORD_BYTES + 40];
  const size_t length = base64_decode(reinterpret_cast<const uint8_t *>(header->c_str()) + strlen(BASIC),
                                      header->size() - strlen(BASIC), decoded, sizeof(decoded) - 1);
  decoded[length] = '\0';
  const char *colon = strchr(reinterpret_cast<const char *>(decoded), ':');
  const char *password = colon != nullptr ? colon + 1 : "";
  uint8_t hash[sizeof(settings.password_hash)];
  hash_password(settings.password_salt, password, hash);
  if (same_bytes(hash, settings.password_hash, sizeof(hash))) {
    return true;
  }
  // Slows down anyone who tries passwords one after another.
  delay(WRONG_PASSWORD_DELAY_MS);
  return false;
}

int DekatronApi::precheck_(AsyncWebServerRequest *request, Route route) {
  if (route == Route::NONE) {
    return 404;
  }
  if (request->method() == HTTP_POST && !request->hasHeader(CSRF_HEADER)) {
    return 400;
  }
  if (route != Route::INFO && route != Route::WEB && !this->authorized_(request)) {
    return 401;
  }
  return 0;
}

void DekatronApi::reply_(AsyncWebServerRequest *request, int code, const std::string &json) {
  request->send(code, "application/json", json.c_str());
}

void DekatronApi::error_(AsyncWebServerRequest *request, int code, const char *message) {
  std::string json = "{\"error\":";
  append_json_string(json, message);
  json += '}';
  this->reply_(request, code, json);
}

void DekatronApi::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  const Route route = this->route_(request);
  auto &store = this->tubes_->store();
  if (index == 0) {
    this->body_.clear();
    this->uploading_ = false;
    this->body_handled_ = true;
    this->refusal_ = nullptr;
    if (this->firmware_) {  // an earlier upload that never finished
      this->firmware_->abort();
      this->firmware_.reset();
    }
    this->denied_ = this->precheck_(request, route);
    if (this->denied_ == 0 && route == Route::FIRMWARE) {
      // A program from anybody on the network is the one thing that must never be open.
      if (!this->tubes_->settings().data().password_set) {
        this->denied_ = 422;
        this->refusal_ = "set a password before sending firmware";
      } else if (total < 100000 || total > 1441792) {
        this->denied_ = 422;
        this->refusal_ = "that is not a firmware file for this clock";
      } else {
        this->firmware_ = ota::make_ota_backend();
        if (!this->firmware_ || this->firmware_->begin(total) != ota::OTA_RESPONSE_OK) {
          this->firmware_.reset();
          this->denied_ = 422;
          this->refusal_ = "the clock cannot take a firmware of this size";
        }
      }
    } else if (this->denied_ == 0 && route == Route::STORE_POST) {
      if (total > store.capacity()) {
        this->denied_ = 422;
      } else if (!store.begin_write()) {
        this->denied_ = 409;
      } else {
        this->uploading_ = true;
        this->upload_started_ms_ = millis();
        this->upload_started_frames_ = this->tubes_->frames_total();
      }
    } else if (this->denied_ == 0 && total > MAX_BODY_BYTES) {
      this->denied_ = 422;
    }
  }
  if (this->denied_ != 0) {
    return;  // the rest of the body is read and thrown away
  }
  if (route == Route::FIRMWARE) {
    if (this->firmware_->write(data, len) != ota::OTA_RESPONSE_OK) {
      this->firmware_->abort();
      this->firmware_.reset();
      this->denied_ = 422;
      this->refusal_ = "the clock could not write the firmware";
      return;
    }
    if (index + len >= total) {
      // The last piece: checks the image and makes it the one to start next.
      if (this->firmware_->end() != ota::OTA_RESPONSE_OK) {
        this->denied_ = 422;
        this->refusal_ = "that file is not a valid firmware for this clock";
      } else {
        this->uploading_ = true;  // ready to start
      }
      if (this->denied_ != 0) this->firmware_->abort();
      this->firmware_.reset();
    }
    return;
  }
  if (route == Route::STORE_POST) {
    if (store.write(data, len) != Store::Status::WRITING) {
      this->uploading_ = false;
      this->denied_ = 422;
    }
    return;
  }
  this->body_.append(reinterpret_cast<const char *>(data), len);
}

void DekatronApi::handleRequest(AsyncWebServerRequest *request) {
  const Route route = this->route_(request);
  // A body only goes through handleBody when it is not a form, so a request that skipped it is checked here.
  const bool checked = this->body_handled_;
  this->body_handled_ = false;
  int denied = checked ? this->denied_ : this->precheck_(request, route);
  if (denied == 0 && (route == Route::STORE_POST || route == Route::FIRMWARE) && !this->uploading_) {
    denied = 400;
  }
  const bool uploading = this->uploading_;
  this->uploading_ = false;
  if (denied != 0) {
    this->body_.clear();
    this->denied_ = 0;
    const char *message = denied == 401   ? "wrong or missing password"
                          : denied == 404 ? "no such call"
                          : denied == 409 ? "another store is being written"
                          : denied == 422 ? "too large or not a valid store"
                                          : "the header X-Dekatron is missing, or the request is empty";
    this->error_(request, denied, this->refusal_ != nullptr ? this->refusal_
                                  : denied == 422 && route == Route::STORE_POST && !uploading
                                      ? this->tubes_->store().state().c_str()
                                      : message);
    this->refusal_ = nullptr;
    return;
  }

  switch (route) {
    case Route::WEB:
      this->send_web_(request);
      break;
    case Route::INFO:
      this->send_info_(request);
      break;
    case Route::STATE:
      this->send_state_(request);
      break;
    case Route::STORE_GET:
      this->send_store_(request);
      break;
    case Route::STORE_POST:
      this->finish_store_(request);
      break;
    case Route::PLAY:
      this->play_(request);
      break;
    case Route::STOP:
      this->tubes_->stop_animation();
      this->reply_(request, 200, "{\"ok\":true}");
      break;
    case Route::SETTINGS:
      this->change_settings_(request);
      break;
    case Route::PASSWORD:
      this->change_password_(request);
      break;
    case Route::CONTROLS:
      this->change_controls_(request);
      break;
    case Route::FIRMWARE:
      this->finish_firmware_(request);
      break;
    case Route::NONE:
      this->error_(request, 404, "no such call");
      break;
  }
  this->body_.clear();
}

// The page is public, as it holds nothing but the program that asks for the password when it needs it.
void DekatronApi::send_web_(AsyncWebServerRequest *request) {
  auto *response = request->beginResponse(200, "text/html; charset=utf-8", WEB_INDEX_GZ, sizeof(WEB_INDEX_GZ));
  response->addHeader("Content-Encoding", "gzip");
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}

void DekatronApi::send_info_(AsyncWebServerRequest *request) {
  std::string json = "{\"product\":\"dekatron\",\"api\":1,\"name\":";
  append_json_string(json, App.get_name().c_str());
  json += ",\"auth\":";
  json += this->tubes_->settings().data().password_set ? "true" : "false";
  json += '}';
  this->reply_(request, 200, json);
}

void DekatronApi::send_state_(AsyncWebServerRequest *request) {
  auto &store = this->tubes_->store();
  const auto &settings = this->tubes_->settings().data();
  char name[Store::NAME_BYTES + 1];

  std::string json = "{\"name\":";
  append_json_string(json, App.get_name().c_str());
  json += ",\"version\":";
  append_json_string(json, ESPHOME_PROJECT_VERSION);
  json += ",\"esphome\":";
  append_json_string(json, ESPHOME_VERSION);
  json += ",\"build\":";
  {
    char built[Application::BUILD_TIME_STR_SIZE];
    App.get_build_time_string(built);
    append_json_string(json, built);
  }
  json += ",\"uptime_s\":";
  append_number(json, static_cast<long>(millis() / 1000));
  json += ",\"free_heap\":";
  append_number(json, static_cast<long>(this->tubes_->free_heap()));
  json += ",\"largest_block\":";
  append_number(json, static_cast<long>(this->tubes_->largest_heap_block()));
  json += ",\"animation\":";
  append_json_string(json, this->tubes_->animation_state().c_str());

  json += ",\"settings\":{\"timezone\":";
  append_json_string(json, settings.timezone);
  json += ",\"boot_animation\":";
  json += settings.boot_animation ? "true" : "false";
  json += ",\"password\":";
  json += settings.password_set ? "true" : "false";
  json += ",\"face\":";
  append_json_string(json, (this->tubes_->face_name(this->tubes_->current_face(), name), name));
  json += "},\"store\":{\"status\":";
  append_json_string(json, store.state().c_str());
  json += ",\"ready\":";
  json += store.ready() ? "true" : "false";
  json += ",\"size\":";
  append_number(json, static_cast<long>(store.size()));
  json += ",\"capacity\":";
  append_number(json, static_cast<long>(store.capacity()));

  char theme[Store::NAME_BYTES + 1];
  store.theme_name(theme);
  json += ",\"theme\":";
  append_json_string(json, theme);
  json += "},\"faces\":[";
  for (uint8_t face = 0; face < this->tubes_->face_count(); face++) {
    this->tubes_->face_name(face, name);
    json += face == 0 ? "" : ",";
    append_json_string(json, name);
  }
  json += "],\"clips\":[";
  bool first = true;
  uint8_t entry;
  for (uint8_t nth = 0; store.nth(EntryKind::CLIP, nth, &entry); nth++) {
    const auto info = store.info(entry);
    store.name(entry, name);
    json += first ? "" : ",";
    first = false;
    json += "{\"name\":";
    append_json_string(json, name);
    json += ",\"frames\":";
    append_number(json, static_cast<long>(info.frames));
    json += ",\"fps\":";
    append_number(json, static_cast<long>(info.fps));
    json += ",\"layout\":\"";
    json += layout_name(info.images_per_frame);
    json += "\",\"leds\":";
    json += info.leds ? "true" : "false";
    json += "}";
  }
  json += "]";
  this->append_controls_(json);
  json += "}";
  this->reply_(request, 200, json);
}

void DekatronApi::send_store_(AsyncWebServerRequest *request) {
  auto &store = this->tubes_->store();
  if (!store.ready()) {
    this->error_(request, 404, "there is no store");
    return;
  }
  // The store is sent straight from flash, without a copy.
  auto *response = request->beginResponse(200, "application/octet-stream", store.data(), store.size());
  response->addHeader("Content-Disposition", "attachment; filename=\"store.bin\"");
  request->send(response);
}

void DekatronApi::finish_firmware_(AsyncWebServerRequest *request) {
  ESP_LOGI(TAG, "New firmware written, restarting");
  this->reply_(request, 200, "{\"ok\":true,\"restart\":true}");
  // Gives the answer time to leave before the clock goes down.
  this->defer([this]() { this->set_timeout(1500, []() { App.safe_reboot(); }); });
}

void DekatronApi::finish_store_(AsyncWebServerRequest *request) {
  auto &store = this->tubes_->store();
  const Store::Status status = store.finish_write();
  const uint32_t took_ms = millis() - this->upload_started_ms_;
  const uint32_t frames = this->tubes_->frames_total() - this->upload_started_frames_;
  this->tubes_->refresh();
  ESP_LOGI(TAG, "Store received in %u ms while %u frames were drawn", static_cast<unsigned>(took_ms),
           static_cast<unsigned>(frames));
  if (status == Store::Status::IDLE) {
    std::string json = "{\"ok\":true,\"store\":";
    append_json_string(json, store.state().c_str());
    json += ",\"ms\":";
    append_number(json, static_cast<long>(took_ms));
    json += ",\"frames\":";
    append_number(json, static_cast<long>(frames));
    json += '}';
    this->reply_(request, 200, json);
  } else {
    this->error_(request, 422, store.state().c_str());
  }
}

void DekatronApi::play_(AsyncWebServerRequest *request) {
  bool played = false;
  const bool valid = json::parse_json(this->body_, [&](JsonObject root) {
    played = this->tubes_->play_request(root["type"] | "effect", root["name"] | "", root["tubes"] | "all",
                                        root["duration"] | 10, root["priority"] | 2, root["color"] | "",
                                        root["loop"] | 0);
    return true;
  });
  if (!valid) {
    this->error_(request, 400, "the body is not JSON");
  } else if (!played) {
    this->error_(request, 422, "the clock did not accept it, see its log");
  } else {
    this->reply_(request, 200, "{\"ok\":true}");
  }
}

void DekatronApi::change_settings_(AsyncWebServerRequest *request) {
  auto &settings = this->tubes_->settings();
  auto data = settings.data();  // changed as a copy, so a rejected request leaves nothing half done
  std::string problem;
  int face_to_select = -1;
  const bool valid = json::parse_json(this->body_, [&](JsonObject root) {
    JsonVariant timezone = root["timezone"];
    if (timezone.is<const char *>()) {
      const char *text = timezone.as<const char *>();
      time::ParsedTimezone parsed;
      if (strlen(text) >= elekstube_ips::TIMEZONE_BYTES) {
        problem = "the timezone is too long";
      } else if (text[0] != '\0' && !elekstube_ips::parse_posix_timezone(text, &parsed)) {
        problem = "the timezone is not a POSIX one, such as CET-1CEST,M3.5.0,M10.5.0/3";
      } else {
        snprintf(data.timezone, sizeof(data.timezone), "%s", text);
      }
    }
    JsonVariant boot = root["boot_animation"];
    if (boot.is<bool>()) {
      data.boot_animation = boot.as<bool>() ? 1 : 0;
    }
    JsonVariant face = root["face"];
    if (face.is<const char *>()) {
      char name[Store::NAME_BYTES + 1];
      bool found = false;
      for (uint8_t candidate = 0; candidate < this->tubes_->face_count() && !found; candidate++) {
        this->tubes_->face_name(candidate, name);
        if (strcmp(name, face.as<const char *>()) == 0) {
          face_to_select = candidate;
          found = true;
        }
      }
      if (!found) {
        problem = "there is no face with that name";
      }
    }
    return true;
  });
  if (!valid) {
    this->error_(request, 400, "the body is not JSON");
    return;
  }
  if (!problem.empty()) {
    this->error_(request, 422, problem.c_str());
    return;
  }
  settings.data() = data;
  if (face_to_select >= 0) {
    this->tubes_->select_face(static_cast<uint8_t>(face_to_select));
  }
  settings.apply_timezone();
  this->tubes_->set_boot_animation(data.boot_animation != 0);
  const bool saved = settings.save();

  // What was understood comes back, so a client can check it against its own reading.
  std::string json = "{\"ok\":";
  json += saved ? "true" : "false";
  json += ",\"timezone\":";
  append_json_string(json, data.timezone);
  time::ParsedTimezone parsed;
  if (data.timezone[0] != '\0' && elekstube_ips::parse_posix_timezone(data.timezone, &parsed)) {
    json += ",\"parsed\":{\"std\":";
    append_number(json, parsed.std_offset_seconds);
    json += ",\"dst\":";
    append_number(json, parsed.dst_offset_seconds);
    for (const auto *rule : {&parsed.dst_start, &parsed.dst_end}) {
      json += rule == &parsed.dst_start ? ",\"start\":[" : ",\"end\":[";
      append_number(json, static_cast<long>(rule->type));
      for (const long field : {static_cast<long>(rule->month), static_cast<long>(rule->week),
                               static_cast<long>(rule->day_of_week), static_cast<long>(rule->day),
                               static_cast<long>(rule->time_seconds)}) {
        json += ',';
        append_number(json, field);
      }
      json += ']';
    }
    json += '}';
  }
  json += ",\"boot_animation\":";
  json += data.boot_animation ? "true" : "false";
  json += '}';
  this->reply_(request, saved ? 200 : 500, json);
}

namespace {

void append_hex_color(std::string &out, float red, float green, float blue) {
  char text[10];
  snprintf(text, sizeof(text), "\"#%02x%02x%02x\"", static_cast<int>(lroundf(red * 255)), static_cast<int>(lroundf(green * 255)),
           static_cast<int>(lroundf(blue * 255)));
  out += text;
}

const char *json_bool(bool value) { return value ? "true" : "false"; }

}  // namespace

// What the page can change, as the entities of the clock have it now. A part that is not set up is left out.
void DekatronApi::append_controls_(std::string &json) {
  json += ",\"controls\":{";
  bool first = true;
  const auto key = [&](const char *name) {
    json += first ? "" : ",";
    first = false;
    json += '"';
    json += name;
    json += "\":";
  };
  if (this->displays_ != nullptr) {
    key("displays");
    json += json_bool(this->displays_->state);
  }
  if (this->brightness_ != nullptr) {
    key("brightness");
    append_number(json, lroundf(this->brightness_->state));
  }
  if (this->night_ != nullptr || this->night_start_ != nullptr) {
    key("night");
    json += '{';
    bool inner = true;
    const auto field = [&](const char *name) {
      json += inner ? "" : ",";
      inner = false;
      json += '"';
      json += name;
      json += "\":";
    };
    if (this->night_ != nullptr) {
      field("on");
      json += json_bool(this->night_->state);
    }
    if (this->night_start_ != nullptr) {
      field("start");
      append_number(json, lroundf(this->night_start_->state));
    }
    if (this->night_end_ != nullptr) {
      field("end");
      append_number(json, lroundf(this->night_end_->state));
    }
    if (this->night_brightness_ != nullptr) {
      field("brightness");
      append_number(json, lroundf(this->night_brightness_->state));
    }
    json += '}';
  }
  if (this->hour24_ != nullptr) {
    key("hour24");
    json += json_bool(this->hour24_->state);
  }
  if (this->leading_zero_ != nullptr) {
    key("leading_zero");
    json += json_bool(this->leading_zero_->state);
  }
  if (this->transition_ != nullptr) {
    key("transition");
    append_json_string(json, this->transition_->current_option().c_str());
  }
  if (this->leds_ != nullptr) {
    const auto &values = this->leds_->current_values;
    key("leds");
    json += "{\"on\":";
    json += json_bool(values.is_on());
    json += ",\"color\":";
    append_hex_color(json, values.get_red(), values.get_green(), values.get_blue());
    json += ",\"brightness\":";
    append_number(json, lroundf(values.get_brightness() * 100));
    json += ",\"effect\":";
    append_json_string(json, this->leds_->get_effect_name().c_str());
    json += ",\"effects\":[\"None\"";
    for (auto *effect : this->leds_->get_effects()) {
      json += ',';
      append_json_string(json, effect->get_name().c_str());
    }
    json += "]}";
  }
  json += '}';
}

namespace {

// A change that the page asked for, kept until the main loop can apply it: the entities of ESPHome
// belong to that loop, and this runs on the thread of the web server.
struct ControlChange {
  std::optional<bool> displays, night_on, hour24, leading_zero, leds_on;
  std::optional<int> brightness, night_start, night_end, night_brightness, leds_brightness;
  std::optional<std::string> transition, leds_effect;
  std::optional<elekstube_ips::Rgb> leds_color;
};

}  // namespace

void DekatronApi::change_controls_(AsyncWebServerRequest *request) {
  auto change = std::make_shared<ControlChange>();
  std::string problem;
  const auto number = [&](JsonVariant value, int low, int high, const char *what, std::optional<int> *out) {
    if (value.isNull()) return;
    if (!value.is<int>() || value.as<int>() < low || value.as<int>() > high) {
      problem = std::string(what) + " must be a number from " + std::to_string(low) + " to " + std::to_string(high);
      return;
    }
    *out = value.as<int>();
  };
  const auto flag = [&](JsonVariant value, const char *what, std::optional<bool> *out) {
    if (value.isNull()) return;
    if (!value.is<bool>()) {
      problem = std::string(what) + " must be true or false";
      return;
    }
    *out = value.as<bool>();
  };
  const bool valid = json::parse_json(this->body_, [&](JsonObject root) {
    flag(root["displays"], "displays", &change->displays);
    number(root["brightness"], 5, 100, "brightness", &change->brightness);
    flag(root["hour24"], "hour24", &change->hour24);
    flag(root["leading_zero"], "leading_zero", &change->leading_zero);
    JsonObject night = root["night"];
    if (!night.isNull()) {
      flag(night["on"], "night.on", &change->night_on);
      number(night["start"], 0, 23, "night.start", &change->night_start);
      number(night["end"], 0, 23, "night.end", &change->night_end);
      number(night["brightness"], 0, 100, "night.brightness", &change->night_brightness);
    }
    JsonVariant transition = root["transition"];
    if (transition.is<const char *>()) {
      const std::string name = transition.as<const char *>();
      if (name != "Off" && name != "Fade") problem = "transition must be Off or Fade";
      change->transition = name;
    }
    JsonObject leds = root["leds"];
    if (!leds.isNull()) {
      flag(leds["on"], "leds.on", &change->leds_on);
      number(leds["brightness"], 1, 100, "leds.brightness", &change->leds_brightness);
      JsonVariant color = leds["color"];
      if (color.is<const char *>()) {
        elekstube_ips::Rgb rgb;
        if (elekstube_ips::parse_color(color.as<const char *>(), &rgb)) {
          change->leds_color = rgb;
        } else {
          problem = "leds.color must be like #ffb020";
        }
      }
      JsonVariant effect = leds["effect"];
      if (effect.is<const char *>()) {
        std::string name = effect.as<const char *>();
        bool known = name == "None";
        if (this->leds_ != nullptr) {
          for (auto *candidate : this->leds_->get_effects()) known = known || name == candidate->get_name();
        }
        if (known) {
          change->leds_effect = name;
        } else {
          problem = "leds.effect is not one of the effects in the state";
        }
      }
    }
    return true;
  });
  if (!valid) {
    this->error_(request, 400, "the body is not JSON");
    return;
  }
  if (!problem.empty()) {
    this->error_(request, 422, problem.c_str());
    return;
  }
  this->defer([this, change]() {
    const auto set_number = [](number::Number *entity, std::optional<int> value) {
      if (entity != nullptr && value) {
        auto call = entity->make_call();
        call.set_value(static_cast<float>(*value));
        call.perform();
      }
    };
    const auto set_switch = [](switch_::Switch *entity, std::optional<bool> value) {
      if (entity != nullptr && value) {
        if (*value) {
          entity->turn_on();
        } else {
          entity->turn_off();
        }
      }
    };
    set_switch(this->displays_, change->displays);
    set_number(this->brightness_, change->brightness);
    set_switch(this->night_, change->night_on);
    set_number(this->night_start_, change->night_start);
    set_number(this->night_end_, change->night_end);
    set_number(this->night_brightness_, change->night_brightness);
    set_switch(this->hour24_, change->hour24);
    set_switch(this->leading_zero_, change->leading_zero);
    if (this->transition_ != nullptr && change->transition) {
      auto call = this->transition_->make_call();
      call.set_option(*change->transition);
      call.perform();
    }
    if (this->leds_ != nullptr && (change->leds_on || change->leds_brightness || change->leds_color || change->leds_effect)) {
      auto call = this->leds_->make_call();
      if (change->leds_on && !*change->leds_on) {
        call.set_state(false);  // turning off is all it does, as the rest would be lost with it
      } else {
        if (change->leds_on) call.set_state(true);
        if (change->leds_brightness) call.set_brightness(*change->leds_brightness / 100.0f);
        if (change->leds_color) call.set_rgb(change->leds_color->r / 255.0f, change->leds_color->g / 255.0f, change->leds_color->b / 255.0f);
        if (change->leds_effect) call.set_effect(*change->leds_effect);
      }
      call.perform();
    }
  });
  this->reply_(request, 200, "{\"ok\":true}");
}

void DekatronApi::change_password_(AsyncWebServerRequest *request) {
  std::string password;
  bool present = false;
  const bool valid = json::parse_json(this->body_, [&](JsonObject root) {
    JsonVariant value = root["password"];
    if (value.is<const char *>()) {
      password = value.as<const char *>();
      present = true;
    }
    return true;
  });
  if (!valid || !present) {
    this->error_(request, 400, "expected {\"password\": \"...\"}");
    return;
  }
  if (password.size() > MAX_PASSWORD_BYTES) {
    this->error_(request, 422, "the password is too long");
    return;
  }
  auto &settings = this->tubes_->settings();
  auto &data = settings.data();
  if (password.empty()) {
    data.password_set = 0;
    memset(data.password_salt, 0, sizeof(data.password_salt));
    memset(data.password_hash, 0, sizeof(data.password_hash));
  } else {
    esp_fill_random(data.password_salt, sizeof(data.password_salt));
    hash_password(data.password_salt, password.c_str(), data.password_hash);
    data.password_set = 1;
  }
  const bool saved = settings.save();
  this->reply_(request, saved ? 200 : 500, saved ? "{\"ok\":true}" : "{\"ok\":false}");
}

}  // namespace dekatron_api
}  // namespace esphome
