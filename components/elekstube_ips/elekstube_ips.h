#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <string>

#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>

#include "animation.h"
#include "clip_library.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "faces_builtin.h"

namespace esphome {
namespace elekstube_ips {

enum DisplayMode : uint8_t {
  MODE_CLOCK = 0,
  MODE_IDENTIFY = 1,
};

// What the clock shows after it starts, until it reaches the time.
enum class StartupStage : uint8_t { BOOT, CONNECTING, ONLINE, SETUP, DONE };

class ElekstubeIps : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_x_offset(uint16_t offset) { this->x_offset_ = offset; }
  void set_y_offset(uint16_t offset) { this->y_offset_ = offset; }
  void set_madctl(uint8_t madctl) { this->madctl_ = madctl; }
  void set_time(time::RealTimeClock *clock) { this->clock_ = clock; }
  void set_leds(light::LightState *leds) { this->leds_ = leds; }

  // Returns false when the request is invalid or a more urgent animation is running.
  bool play_request(const char *type, const char *name, const char *tubes, int duration_s, int priority,
                    const char *color, const char *text, bool scroll, int loops);
  bool download_clips(const char *url);
  std::string clip_library_state() const { return this->clips_.state(); }
  void play_effect(size_t index, float duration_s);
  void stop_animation() { this->stop_requested_.store(true); }
  std::string animation_state() const;
  void set_transition(size_t transition) { this->transition_.store(static_cast<uint8_t>(transition)); }
  // Starts a countdown, or cancels the one that is running.
  void toggle_countdown(int duration_s);
  // A white pulse on every tube, over any other animation, to confirm something that is about to happen.
  void flash_displays();
  // Returns true if a button press was used to skip the startup, and has no other meaning then.
  bool skip_startup();

  void set_mode(size_t mode);
  void set_face(size_t face);
  void set_hour_24(bool hour_24);
  void set_leading_zero(bool leading_zero);
  void set_displays_on(bool on) { this->displays_on_.store(on); }
  void set_brightness(float percent) { this->brightness_.store(static_cast<uint8_t>(percent)); }
  void set_night_enabled(bool enabled) { this->night_enabled_.store(enabled); }
  void set_night_start(float hour) { this->night_start_.store(static_cast<uint8_t>(hour)); }
  void set_night_end(float hour) { this->night_end_.store(static_cast<uint8_t>(hour)); }
  void set_night_brightness(float percent) { this->night_brightness_.store(static_cast<uint8_t>(percent)); }

  float fps() const { return this->fps_.load(); }
  float free_heap() const;
  float largest_heap_block() const;

 protected:
  static void task_entry_(void *arg);
  void task_loop_();

  bool init_bus_();
  void load_face_pack_();
  void init_panels_();
  void select_(uint8_t tube_mask);
  void transmit_(const void *buffer, size_t bytes);
  void send_command_(uint8_t command, const uint8_t *data = nullptr, size_t length = 0);
  void broadcast_command_(uint8_t command, const uint8_t *data = nullptr, size_t length = 0);
  void send_pixels_(const uint16_t *pixels, size_t count);
  void set_window_(uint16_t first_column = 0, uint16_t last_column = 134, uint16_t first_row = 0,
                   uint16_t last_row = 239);
  bool draw_sweep_tube_(uint8_t tube, uint32_t elapsed_ms);
  void draw_text_tube_(uint8_t tube, const uint8_t *glyphs, size_t count, int32_t text_left);
  bool draw_text_frame_(uint32_t elapsed_ms);
  bool draw_clip_frame_();
  bool draw_scene_frame_(const ClipInfo &info);
  void draw_scene_patch_(const ScenePatch &patch, const uint8_t *data, uint32_t size, const uint16_t *palette);
  void draw_identify_(uint8_t tube);
  void draw_blank_(uint8_t tube_mask);
  void draw_jpeg_(uint8_t tube_mask, const uint8_t *jpeg, size_t size, uint8_t level_percent = 100);
  void draw_digit_(uint8_t tube, uint8_t digit, bool fade);
  void draw_effect_(uint8_t tube_mask, uint8_t tube, uint32_t elapsed_ms);
  bool draw_animation_frame_(uint32_t elapsed_ms);  // true when something reached the panels
  void update_animation_(int64_t now_ms);
  void begin_animation_(int64_t now_ms);
  bool start_clip_(const char *name, uint32_t duration_ms, int64_t now_ms);
  void enter_startup_stage_(StartupStage stage, int64_t now_ms);
  void update_startup_(int64_t now_ms, bool time_valid);
  void draw_setup_screen_();
  void update_setup_client_();
  void draw_setup_qr_(uint8_t tube, const char *text);
  void draw_setup_icon_(uint8_t tube, const uint8_t *icon, size_t size);
  uint16_t current_dim_() const;
  bool submit_(const AnimationRequest &request);
  void apply_led_event_();
  void update_led_pulse_();
  void render_identify_strip_(uint8_t tube, uint16_t first_row, uint16_t rows);
  static unsigned int jpeg_input_(void *decoder, uint8_t *buffer, unsigned int bytes);
  static unsigned int jpeg_output_(void *decoder, void *bitmap, void *rect);

  uint8_t target_brightness_(const ESPTime &now) const;
  void apply_brightness_(uint8_t brightness);
  void draw_clock_(const ESPTime &now, uint8_t skip_mask);
  FaceImage face_image_(uint8_t face, uint8_t digit) const;

  time::RealTimeClock *clock_{nullptr};
  spi_device_handle_t device_{nullptr};
  uint16_t *strip_{nullptr};
  uint8_t *jpeg_pool_{nullptr};
  const uint8_t *face_pack_{nullptr};
  uint32_t face_pack_sets_{0};
  ClipLibrary clips_;
  uint16_t x_offset_{52};
  uint16_t y_offset_{40};
  uint8_t madctl_{0x08};

  // Written by the main loop and read by the drawing task.
  std::atomic<uint8_t> mode_{MODE_CLOCK};
  std::atomic<uint8_t> face_{0};
  std::atomic<bool> hour_24_{true};
  std::atomic<bool> leading_zero_{true};
  std::atomic<bool> displays_on_{true};
  std::atomic<uint8_t> brightness_{100};
  std::atomic<bool> night_enabled_{false};
  std::atomic<uint8_t> night_start_{23};
  std::atomic<uint8_t> night_end_{7};
  std::atomic<uint8_t> night_brightness_{20};
  std::atomic<uint8_t> transition_{0};
  std::atomic<bool> redraw_{true};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> skip_startup_requested_{false};
  std::atomic<bool> wifi_connected_{false};
  std::atomic<bool> wifi_configured_{false};
  std::atomic<bool> setup_client_joined_{false};
  std::string setup_url_;  // written before setup_client_joined_ becomes true
  uint32_t last_setup_client_check_ms_{0};
  std::atomic<float> fps_{0.0f};
  portMUX_TYPE request_lock_ = portMUX_INITIALIZER_UNLOCKED;
  AnimationRequest pending_request_;  // guarded by request_lock_
  bool has_pending_request_{false};   // guarded by request_lock_

  // Written by the drawing task and read by the main loop.
  std::atomic<uint8_t> active_priority_{0};  // 0 when no animation is running
  std::atomic<uint8_t> active_effect_{0};
  std::atomic<uint32_t> active_elapsed_s_{0};
  std::atomic<uint32_t> active_duration_s_{0};
  std::atomic<uint8_t> led_event_{0};
  std::atomic<uint32_t> led_color_{0};
  std::atomic<uint32_t> animation_start_ms32_{0};
  std::atomic<StartupStage> startup_stage_{StartupStage::BOOT};

  // Owned by the drawing task.
  int64_t startup_stage_start_ms_{0};
  bool finish_at_loop_end_{false};
  bool setup_drawn_{false};
  bool setup_qr_shows_url_{false};
  AnimationRequest active_request_;
  bool animation_active_{false};
  int64_t animation_start_ms_{0};
  uint8_t shown_digits_[6]{};
  uint16_t *sweep_lines_{nullptr};
  bool sweep_lines_valid_[6]{};
  bool text_drawn_{false};
  bool text_tube_blank_[6]{};
  uint32_t clip_shown_frames_[6]{};
  uint32_t scene_frames_drawn_{0};
  uint32_t scene_slowest_us_{0};
  uint8_t applied_brightness_{100};
  bool panels_powered_{true};

  // Owned by the main loop.
  light::LightState *leds_{nullptr};
  uint32_t last_led_pulse_ms_{0};
  bool leds_saved_{false};
  bool saved_leds_on_{false};
  float saved_leds_brightness_{1.0f};
  float saved_leds_rgb_[3]{1.0f, 1.0f, 1.0f};
  std::string saved_leds_effect_{"None"};
};

}  // namespace elekstube_ips
}  // namespace esphome
