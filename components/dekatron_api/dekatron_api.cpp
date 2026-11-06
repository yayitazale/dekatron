#include "dekatron_api.h"

#include <esp_random.h>

#include <cstring>

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
    this->denied_ = this->precheck_(request, route);
    if (this->denied_ == 0 && route == Route::STORE_POST) {
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
  if (denied == 0 && route == Route::STORE_POST && !this->uploading_) {
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
    this->error_(request, denied, denied == 422 && route == Route::STORE_POST && !uploading
                                      ? this->tubes_->store().state().c_str()
                                      : message);
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
  json += ",\"esphome\":";
  append_json_string(json, ESPHOME_VERSION);
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
    json += "\"}";
  }
  json += "]}";
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
                                        root["text"] | "", root["scroll"] | false, root["loop"] | 0);
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
