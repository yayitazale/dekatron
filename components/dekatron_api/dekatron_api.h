#pragma once

#include <string>

#include "esphome/components/elekstube_ips/elekstube_ips.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace dekatron_api {

// Everything lives under /api/. With a password set, every call but /api/info needs it, as HTTP
// basic authentication whose user name is ignored. Calls that change something are POSTs that
// also need the header X-Dekatron, which a web page of another site cannot add without a
// permission that this API never gives.
//
//   GET  /          the web page of the clock, which uses everything below
//   GET  /api/info      who this is, without any detail
//   GET  /api/state     the state of the clock, its settings, and the faces and clips it has
//   GET  /api/store     the store as it is in flash
//   POST /api/store     replaces the store with the body, which is a file made by make_clips.py
//   POST /api/play      starts an animation, with the JSON of the MQTT play topic
//   POST /api/stop      stops it
//   POST /api/settings  {"timezone": "CET-1CEST,M3.5.0,M10.5.0/3", "boot_animation": true}
//   POST /api/password  {"password": "..."}, and an empty one removes it
class DekatronApi : public Component, public AsyncWebHandler {
 public:
  void set_web_server_base(web_server_base::WebServerBase *base) { this->base_ = base; }
  void set_tubes(elekstube_ips::ElekstubeIps *tubes) { this->tubes_ = tubes; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
  bool isRequestHandlerTrivial() const override { return false; }

 protected:
  enum class Route : uint8_t { NONE, WEB, INFO, STATE, STORE_GET, STORE_POST, PLAY, STOP, SETTINGS, PASSWORD };

  Route route_(AsyncWebServerRequest *request) const;
  int precheck_(AsyncWebServerRequest *request, Route route);
  bool authorized_(AsyncWebServerRequest *request);
  void reply_(AsyncWebServerRequest *request, int code, const std::string &json);
  void error_(AsyncWebServerRequest *request, int code, const char *message);

  void send_web_(AsyncWebServerRequest *request);
  void send_info_(AsyncWebServerRequest *request);
  void send_state_(AsyncWebServerRequest *request);
  void send_store_(AsyncWebServerRequest *request);
  void finish_store_(AsyncWebServerRequest *request);
  void play_(AsyncWebServerRequest *request);
  void change_settings_(AsyncWebServerRequest *request);
  void change_password_(AsyncWebServerRequest *request);

  web_server_base::WebServerBase *base_{nullptr};
  elekstube_ips::ElekstubeIps *tubes_{nullptr};

  // Requests are served one at a time, so one request's state can live here.
  mutable char url_[AsyncWebServerRequest::URL_BUF_SIZE];
  std::string body_;
  int denied_{0};          // what handleBody decided, which only counts if handleBody ran
  bool body_handled_{false};
  bool uploading_{false};
  uint32_t upload_started_ms_{0};
  uint32_t upload_started_frames_{0};
};

}  // namespace dekatron_api
}  // namespace esphome
