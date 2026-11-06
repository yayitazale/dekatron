#include "elekstube_ips.h"

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp32/rom/tjpgd.h>
#include <cmath>
#include <cstring>

#include "effects.h"
#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/light_call.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <esp_wifi.h>
#include "qrcodegen.h"
#include "setup_icons.h"

namespace esphome {
namespace elekstube_ips {

static const char *const TAG = "elekstube_ips";

static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_23;
static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_18;
static constexpr gpio_num_t PIN_DC = GPIO_NUM_25;
static constexpr gpio_num_t PIN_RST = GPIO_NUM_26;
static constexpr gpio_num_t PIN_ENABLE = GPIO_NUM_27;
static constexpr gpio_num_t PIN_CS_DATA = GPIO_NUM_14;
static constexpr gpio_num_t PIN_CS_CLOCK = GPIO_NUM_9;
static constexpr gpio_num_t PIN_CS_LATCH = GPIO_NUM_10;

static constexpr uint16_t WIDTH = 135;
static constexpr uint16_t HEIGHT = 240;
static constexpr uint16_t STRIP_ROWS = 16;
static constexpr size_t STRIP_PIXELS = WIDTH * STRIP_ROWS;
static constexpr uint8_t TUBE_COUNT = 6;
static constexpr uint8_t ALL_TUBES = 0x3F;
static constexpr int SPI_HZ = 40000000;
static constexpr size_t JPEG_POOL_BYTES = 4096;
static constexpr float LED_GAMMA = 2.8f;  // the gamma_correct default of the light
static constexpr float LED_PULSE_MIN = 0.01f;
static constexpr uint32_t LED_PULSE_INTERVAL_MS = 33;

static constexpr uint8_t DIGIT_COUNT = 10;
static constexpr uint8_t DIGIT_BLANK = DIGIT_COUNT;
static constexpr uint8_t DIGIT_UNKNOWN = 0xFF;

// Tube 5 is the leftmost one.
static constexpr uint8_t CENTRE_LEFT_TUBE = 3;
static constexpr uint8_t CENTRE_RIGHT_TUBE = 2;
static constexpr uint32_t COUNTDOWN_FINAL_SECONDS = 10;

// A theme replaces what the clock shows at each of these moments by providing a clip with its name.
static const char *const CLIP_BOOT = "boot";
static const char *const CLIP_CONNECTING = "connecting";
static const char *const CLIP_ONLINE = "online";
static const char *const CLIP_COUNTDOWN_END = "countdown_end";
static constexpr uint32_t ONLINE_FALLBACK_MS = 2000;
static constexpr uint32_t FLASH_MS = 1600;  // one period of the pulse: dark, white, dark
static constexpr int64_t CONNECTING_TIMEOUT_MS = 30000;

static constexpr int SETUP_QR_MAX_VERSION = 5;
static constexpr int SETUP_QR_QUIET_MODULES = 2;
static constexpr uint32_t SETUP_CLIENT_CHECK_MS = 500;
static constexpr TickType_t CLOCK_POLL = pdMS_TO_TICKS(50);
static constexpr uint8_t TRANSITION_FADE = 1;
static constexpr uint8_t LED_EVENT_NONE = 0;
static constexpr uint8_t LED_EVENT_START = 1;
static constexpr uint8_t LED_EVENT_END = 2;

// The data partition holds the store, see tools/make_clips.py.
static constexpr esp_partition_subtype_t STORE_PARTITION_SUBTYPE = static_cast<esp_partition_subtype_t>(0x40);

static constexpr uint8_t CMD_NOP = 0x00;
static constexpr uint8_t CMD_SWRESET = 0x01;
static constexpr uint8_t CMD_SLPIN = 0x10;
static constexpr uint8_t CMD_SLPOUT = 0x11;
static constexpr uint8_t CMD_DISPOFF = 0x28;
static constexpr uint8_t CMD_NORON = 0x13;
static constexpr uint8_t CMD_INVON = 0x21;
static constexpr uint8_t CMD_DISPON = 0x29;
static constexpr uint8_t CMD_CASET = 0x2A;
static constexpr uint8_t CMD_RASET = 0x2B;
static constexpr uint8_t CMD_RAMWR = 0x2C;
static constexpr uint8_t CMD_MADCTL = 0x36;
static constexpr uint8_t CMD_COLMOD = 0x3A;

struct PanelCommand {
  uint8_t command;
  uint8_t length;
  uint8_t data[14];
};

// Voltage, frame rate and gamma values for 135x240 ST7789 panels, as set by TFT_eSPI.
static const PanelCommand PANEL_SETTINGS[] = {
    {CMD_NORON, 0, {}},
    {0xB6, 2, {0x0A, 0x82}},
    {0xB0, 2, {0x00, 0xE0}},
    {CMD_COLMOD, 1, {0x55}},
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}},
    {0xB7, 1, {0x35}},
    {0xBB, 1, {0x28}},
    {0xC0, 1, {0x0C}},
    {0xC2, 2, {0x01, 0xFF}},
    {0xC3, 1, {0x10}},
    {0xC4, 1, {0x20}},
    {0xC6, 1, {0x01}},  // 111 Hz instead of TFT_eSPI's 60 Hz: tearing lasts half as long
    {0xD0, 2, {0xA4, 0xA1}},
    {0xE0, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17}},
    {0xE1, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E}},
    {CMD_INVON, 0, {}},
};

static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (static_cast<uint16_t>(r & 0x1F) << 11) | (static_cast<uint16_t>(g & 0x3F) << 5) | (b & 0x1F);
}

// Panels read RGB565 most significant byte first.
static constexpr uint16_t to_wire(uint16_t color) { return static_cast<uint16_t>((color >> 8) | (color << 8)); }

// Seven-segment layout for the identification pattern, as bits a to g.
static constexpr uint8_t SEG_A = 1 << 0, SEG_B = 1 << 1, SEG_C = 1 << 2, SEG_D = 1 << 3, SEG_E = 1 << 4,
                         SEG_F = 1 << 5, SEG_G = 1 << 6;
static constexpr uint8_t TUBE_DIGITS[TUBE_COUNT] = {
    SEG_B | SEG_C,
    SEG_A | SEG_B | SEG_G | SEG_E | SEG_D,
    SEG_A | SEG_B | SEG_G | SEG_C | SEG_D,
    SEG_F | SEG_G | SEG_B | SEG_C,
    SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
    SEG_A | SEG_F | SEG_G | SEG_E | SEG_C | SEG_D,
};

static bool digit_pixel_lit(uint8_t segments, uint16_t x, uint16_t y) {
  static constexpr uint16_t LEFT = 20, RIGHT = 115, TOP = 20, BOTTOM = 220, MIDDLE = 120, THICK = 20;
  const bool in_x = x >= LEFT && x < RIGHT;
  const bool left_bar = x >= LEFT && x < LEFT + THICK;
  const bool right_bar = x >= RIGHT - THICK && x < RIGHT;
  const bool upper = y >= TOP && y < MIDDLE + THICK / 2;
  const bool lower = y >= MIDDLE - THICK / 2 && y < BOTTOM;
  return ((segments & SEG_A) && in_x && y >= TOP && y < TOP + THICK) || ((segments & SEG_B) && right_bar && upper) ||
         ((segments & SEG_C) && right_bar && lower) || ((segments & SEG_D) && in_x && y >= BOTTOM - THICK && y < BOTTOM) ||
         ((segments & SEG_E) && left_bar && lower) || ((segments & SEG_F) && left_bar && upper) ||
         ((segments & SEG_G) && in_x && y >= MIDDLE - THICK / 2 && y < MIDDLE + THICK / 2);
}

static bool hour_in_night(uint8_t hour, uint8_t start, uint8_t end) {
  if (start == end) {
    return false;
  }
  return start < end ? (hour >= start && hour < end) : (hour >= start || hour < end);
}

static void output_pin(gpio_num_t pin, int level) {
  gpio_reset_pin(pin);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  gpio_set_level(pin, level);
}

float ElekstubeIps::free_heap() const { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }

float ElekstubeIps::largest_heap_block() const { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }

void ElekstubeIps::set_mode(size_t mode) {
  this->mode_.store(static_cast<uint8_t>(mode));
  this->redraw_.store(true);
}

void ElekstubeIps::set_face(size_t face) {
  this->face_.store(static_cast<uint8_t>(face));
  this->redraw_.store(true);
}

void ElekstubeIps::set_hour_24(bool hour_24) { this->hour_24_.store(hour_24); }

void ElekstubeIps::set_leading_zero(bool leading_zero) { this->leading_zero_.store(leading_zero); }

void ElekstubeIps::setup() {
  this->settings_.load();
  this->boot_animation_.store(this->settings_.data().boot_animation != 0);
  if (!this->init_bus_()) {
    this->mark_failed();
    return;
  }
  this->load_store_();
  // Runs on the main loop's core: WiFi stays on the other one.
  if (xTaskCreatePinnedToCore(task_entry_, "tubes", 8192, this, 1, nullptr, 1) != pdPASS) {
    ESP_LOGE(TAG, "Could not start the drawing task");
    this->mark_failed();
  }
}

void ElekstubeIps::loop() {
  if (!this->timezone_applied_) {
    // The time component sets its own timezone while starting, so this has to come after it.
    this->timezone_applied_ = true;
    this->settings_.apply_timezone();
    this->refresh_faces_();
  }
  this->wifi_connected_.store(wifi::global_wifi_component->is_connected());
  this->wifi_configured_.store(wifi::global_wifi_component->has_sta());
  this->update_setup_client_();
  this->apply_led_event_();
  this->update_led_track_();
  this->update_led_pulse_();
}

// The scene draws on the other core and leaves its colors here. The LEDs are written from the main loop.
void ElekstubeIps::update_led_track_() {
  if (this->leds_ == nullptr || !this->led_track_active_.load() || !this->led_dirty_.exchange(false)) {
    return;
  }
  auto *strip = static_cast<light::AddressableLight *>(this->leds_->get_output());
  for (int led = 0; led < TUBE_COUNT; led++) {
    const uint32_t color = this->led_values_[led].load();
    (*strip)[led] = Color((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
  }
  strip->schedule_show();
}

void ElekstubeIps::update_led_pulse_() {
  if (this->leds_ == nullptr || static_cast<Effect>(this->active_effect_.load()) != Effect::PULSE) {
    return;
  }
  const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  if (now_ms - this->last_led_pulse_ms_ < LED_PULSE_INTERVAL_MS) {
    return;
  }
  this->last_led_pulse_ms_ = now_ms;
  const float light = pulse_brightness(now_ms - this->animation_start_ms32_.load());
  auto call = this->leds_->make_call();
  call.set_save(false);
  call.set_publish(false);
  call.set_transition_length(0);
  call.set_brightness(powf(light < LED_PULSE_MIN ? LED_PULSE_MIN : light, 1.0f / LED_GAMMA));
  call.perform();
}

void ElekstubeIps::update_setup_client_() {
  const uint32_t now_ms = millis();
  if (this->startup_stage_.load() != StartupStage::SETUP ||
      now_ms - this->last_setup_client_check_ms_ < SETUP_CLIENT_CHECK_MS) {
    return;
  }
  this->last_setup_client_check_ms_ = now_ms;
#ifdef USE_WIFI_AP
  wifi_sta_list_t clients{};
  const bool joined = esp_wifi_ap_get_sta_list(&clients) == ESP_OK && clients.num > 0;
  if (joined && this->setup_url_.empty()) {
    char address[network::IP_ADDRESS_BUFFER_SIZE];
    this->setup_url_ = std::string("http://") + wifi::global_wifi_component->wifi_soft_ap_ip().str_to(address);
  }
  this->setup_client_joined_.store(joined);
#endif
}

void ElekstubeIps::toggle_countdown(int duration_s) {
  if (static_cast<Effect>(this->active_effect_.load()) == Effect::COUNTDOWN) {
    this->stop_animation();
    return;
  }
  this->play_request("effect", "countdown", "all", duration_s, DEFAULT_PRIORITY, "", 0);
}

void ElekstubeIps::flash_displays() {
  AnimationRequest request;
  request.effect = Effect::PULSE;
  request.color = {255, 255, 255};
  request.control_leds = false;
  request.priority = 1;
  request.duration_ms = FLASH_MS;
  this->submit_(request);
}

bool ElekstubeIps::skip_startup() {
  if (this->startup_stage_.load() == StartupStage::DONE) {
    return false;
  }
  this->skip_startup_requested_.store(true);
  return true;
}

bool ElekstubeIps::download_store(const char *url) {
  if (url[0] == '\0') {
    ESP_LOGW(TAG, "Store download without a URL");
    return false;
  }
  return this->store_.start_download(url);
}

bool ElekstubeIps::play_request(const char *type, const char *name, const char *tubes, int duration_s, int priority,
                                const char *color, int loops) {
  AnimationRequest request;
  bool clip_lights = false;
  if (strcmp(type, "effect") == 0) {
    request.effect = effect_from_name(name);
    if (request.effect == Effect::NONE || request.effect == Effect::CLIP) {
      ESP_LOGW(TAG, "Unknown effect: %s", name);
      return false;
    }
  } else if (strcmp(type, "clip") == 0) {
    request.effect = Effect::CLIP;
    if (!this->store_.find(name, EntryKind::CLIP, &request.clip_index)) {
      ESP_LOGW(TAG, "Unknown clip: %s", name);
      return false;
    }
    // A scene with lights drives them itself, from black, unless a color is asked for.
    const bool lights = this->store_.info(request.clip_index).leds;
    request.control_leds = color[0] != '\0' || lights;
    clip_lights = lights && color[0] == '\0';
  } else {
    ESP_LOGW(TAG, "Unsupported animation type: %s", type);
    return false;
  }
  if (!parse_tubes(tubes, &request.tube_mask)) {
    ESP_LOGW(TAG, "Invalid tubes: %s", tubes);
    return false;
  }
  request.color = clip_lights ? Rgb{0, 0, 0} : effect_default_color(request.effect);
  if (color[0] != '\0' && !parse_color(color, &request.color)) {
    ESP_LOGW(TAG, "Invalid colour: %s", color);
    return false;
  }
  if (duration_s < 1 || static_cast<uint32_t>(duration_s) > MAX_DURATION_MS / 1000) {
    ESP_LOGW(TAG, "Invalid duration: %d", duration_s);
    return false;
  }
  if (priority < 1 || priority > LOWEST_PRIORITY) {
    ESP_LOGW(TAG, "Invalid priority: %d", priority);
    return false;
  }
  request.duration_ms = static_cast<uint32_t>(duration_s) * 1000;
  request.priority = static_cast<uint8_t>(priority);
  if (request.effect == Effect::CLIP && loops > 0) {
    const ClipInfo info = this->store_.info(request.clip_index);
    const uint64_t total_ms = static_cast<uint64_t>(info.frames) * 1000 * loops / info.fps;
    if (total_ms > MAX_DURATION_MS) {
      ESP_LOGW(TAG, "Clip loops exceed the maximum duration: %d", loops);
      return false;
    }
    request.duration_ms = total_ms > 0 ? static_cast<uint32_t>(total_ms) : 1;
  }
  return this->submit_(request);
}

void ElekstubeIps::play_effect(size_t index, float duration_s) {
  AnimationRequest request;
  request.effect = effect_from_index(index);
  if (request.effect == Effect::NONE) {
    return;
  }
  request.color = effect_default_color(request.effect);
  request.duration_ms = static_cast<uint32_t>(duration_s) * 1000;
  this->submit_(request);
}

bool ElekstubeIps::submit_(const AnimationRequest &request) {
  if (this->mode_.load() != MODE_CLOCK) {
    ESP_LOGW(TAG, "Animation ignored: the clock is in a test mode");
    return false;
  }
  const uint8_t active = this->active_priority_.load();
  if (active != 0 && !priority_allows(request.priority, active)) {
    ESP_LOGI(TAG, "Animation %s rejected: priority %u is running", effect_name(request.effect), active);
    return false;
  }
  portENTER_CRITICAL(&this->request_lock_);
  this->pending_request_ = request;
  this->has_pending_request_ = true;
  portEXIT_CRITICAL(&this->request_lock_);
  return true;
}

std::string ElekstubeIps::animation_state() const {
  const auto effect = static_cast<Effect>(this->active_effect_.load());
  if (effect == Effect::NONE) {
    return this->mode_.load() == MODE_CLOCK ? "clock" : "test";
  }
  return std::string(effect_name(effect)) + " " + std::to_string(this->active_elapsed_s_.load()) + "/" +
         std::to_string(this->active_duration_s_.load()) + " s";
}

void ElekstubeIps::apply_led_event_() {
  const uint8_t event = this->led_event_.exchange(LED_EVENT_NONE);
  if (event == LED_EVENT_NONE || this->leds_ == nullptr) {
    return;
  }
  auto call = this->leds_->make_call();
  call.set_save(false);
  call.set_transition_length(0);
  if (event == LED_EVENT_END) {
    if (!this->leds_saved_) {
      return;
    }
    call.set_state(this->saved_leds_on_);
    call.set_brightness(this->saved_leds_brightness_);
    call.set_rgb(this->saved_leds_rgb_[0], this->saved_leds_rgb_[1], this->saved_leds_rgb_[2]);
    call.set_effect(this->saved_leds_effect_);
    this->leds_saved_ = false;
    call.perform();
    return;
  }
  if (!this->leds_saved_) {
    const auto &values = this->leds_->current_values;
    this->saved_leds_on_ = values.is_on();
    this->saved_leds_brightness_ = values.get_brightness();
    this->saved_leds_rgb_[0] = values.get_red();
    this->saved_leds_rgb_[1] = values.get_green();
    this->saved_leds_rgb_[2] = values.get_blue();
    this->saved_leds_effect_ = std::string(this->leds_->get_effect_name().c_str());
    this->leds_saved_ = true;
  }
  const uint32_t color = this->led_color_.load();
  call.set_state(true);
  call.set_brightness(1.0f);
  call.set_rgb(((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f);
  call.set_effect("None");
  call.perform();
}

void ElekstubeIps::dump_config() {
  ESP_LOGCONFIG(TAG, "EleksTube IPS: %ux%u, offset %u,%u, MADCTL 0x%02X, store faces: %u", WIDTH, HEIGHT,
                this->x_offset_, this->y_offset_, this->madctl_,
                static_cast<unsigned>(this->store_.count(EntryKind::FACE)));
  if (this->is_failed()) {
    ESP_LOGE(TAG, "Setup failed");
  }
}

bool ElekstubeIps::init_bus_() {
  output_pin(PIN_DC, 1);
  output_pin(PIN_RST, 1);
  output_pin(PIN_ENABLE, 1);
  output_pin(PIN_CS_DATA, 1);
  output_pin(PIN_CS_CLOCK, 0);
  output_pin(PIN_CS_LATCH, 0);
  this->select_(0);

  this->strip_ = static_cast<uint16_t *>(heap_caps_malloc(STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA));
  if (this->strip_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate the strip buffer");
    return false;
  }
  this->jpeg_pool_ = static_cast<uint8_t *>(heap_caps_malloc(JPEG_POOL_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (this->jpeg_pool_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate the JPEG pool");
    return false;
  }
  this->sweep_lines_ = static_cast<uint16_t *>(
      heap_caps_malloc(TUBE_COUNT * WIDTH * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (this->sweep_lines_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate the sweep lines");
    return false;
  }

  spi_bus_config_t bus{};
  bus.mosi_io_num = PIN_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = PIN_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = STRIP_PIXELS * sizeof(uint16_t);
  esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI bus: %s", esp_err_to_name(err));
    return false;
  }

  spi_device_interface_config_t device{};
  device.clock_speed_hz = SPI_HZ;
  device.mode = 3;
  device.spics_io_num = -1;
  device.queue_size = 1;
  err = spi_bus_add_device(SPI3_HOST, &device, &this->device_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI device: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

// A missing or invalid store is normal: the built-in faces are used instead.
void ElekstubeIps::load_store_() {
  const esp_partition_t *partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, STORE_PARTITION_SUBTYPE, nullptr);
  if (partition == nullptr) {
    ESP_LOGW(TAG, "Store partition not found");
    return;
  }
  const void *mapped = nullptr;
  esp_partition_mmap_handle_t handle;
  const esp_err_t err = esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &mapped, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Could not map the store partition: %s", esp_err_to_name(err));
    return;
  }
  // The mapping stays for the life of the program.
  this->store_.attach(partition, static_cast<const uint8_t *>(mapped));
  this->store_.load();
}

static const char *const BUILTIN_FACE_NAMES[BUILTIN_FACE_COUNT] = {"Nixie"};

void ElekstubeIps::refresh() {
  this->redraw_.store(true);
  this->defer([this]() { this->refresh_faces_(); });
}

bool ElekstubeIps::select_face(uint8_t face) {
  if (face >= this->face_count()) {
    return false;
  }
  this->defer([this, face]() {
    if (this->face_select_ == nullptr) {
      this->set_face(face);
      return;
    }
    auto call = this->face_select_->make_call();
    call.set_index(face);
    call.perform();
  });
  return true;
}

// The selector offers the faces that the clock has, as the store changes. Home Assistant reads the
// options when it connects, so it may show the old ones until then.
void ElekstubeIps::refresh_faces_() {
  if (this->face_select_ == nullptr) {
    return;
  }
  const uint8_t count = this->face_count();
  this->face_names_.clear();
  this->face_names_.reserve(count);
  char name[Store::NAME_BYTES + 1];
  for (uint8_t face = 0; face < count; face++) {
    this->face_name(face, name);
    this->face_names_.emplace_back(name);
  }
  FixedVector<const char *> options;
  options.init(count);
  for (const auto &face_name : this->face_names_) {
    options.push_back(face_name.c_str());
  }
  this->face_select_->traits.set_options(options);
  const uint8_t current = this->face_.load() < count ? this->face_.load() : 0;
  this->face_select_->publish_state(static_cast<size_t>(current));
}

uint8_t ElekstubeIps::face_count() const { return BUILTIN_FACE_COUNT + this->store_.count(EntryKind::FACE); }

void ElekstubeIps::face_name(uint8_t face, char *buffer) const {
  uint8_t entry;
  if (face < BUILTIN_FACE_COUNT) {
    snprintf(buffer, Store::NAME_BYTES + 1, "%s", BUILTIN_FACE_NAMES[face]);
  } else if (this->store_.nth(EntryKind::FACE, face - BUILTIN_FACE_COUNT, &entry)) {
    this->store_.name(entry, buffer);
  } else {
    buffer[0] = '\0';
  }
}

FaceImage ElekstubeIps::face_image_(uint8_t face, uint8_t digit) const {
  if (face < BUILTIN_FACE_COUNT) {
    return BUILTIN_FACES[face][digit];
  }
  // The store can be replaced while this runs, so a face that has gone falls back to the first one.
  uint8_t entry;
  if (!this->store_.nth(EntryKind::FACE, face - BUILTIN_FACE_COUNT, &entry)) {
    return BUILTIN_FACES[0][digit];
  }
  FaceImage image;
  this->store_.table_entry(entry, digit, &image.data, &image.size);
  return image;
}

void ElekstubeIps::init_panels_() {
  // SPI mode 3 idles high but the pin starts low. Move it while no panel is
  // selected, or the panels would take that edge as a bit and misread every command.
  this->select_(0);
  this->send_command_(CMD_NOP);

  gpio_set_level(PIN_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(PIN_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(120));

  this->broadcast_command_(CMD_SWRESET);
  vTaskDelay(pdMS_TO_TICKS(150));
  this->broadcast_command_(CMD_SLPOUT);
  vTaskDelay(pdMS_TO_TICKS(120));
  this->broadcast_command_(CMD_MADCTL, &this->madctl_, 1);
  for (const PanelCommand &setting : PANEL_SETTINGS) {
    this->broadcast_command_(setting.command, setting.data, setting.length);
  }
  this->broadcast_command_(CMD_DISPON);
  vTaskDelay(pdMS_TO_TICKS(120));
  ESP_LOGI(TAG, "Panels initialised");
}

void ElekstubeIps::broadcast_command_(uint8_t command, const uint8_t *data, size_t length) {
  this->select_(ALL_TUBES);
  this->send_command_(command, data, length);
  this->select_(0);
}

void ElekstubeIps::transmit_(const void *buffer, size_t bytes) {
  spi_transaction_t transaction{};
  transaction.length = bytes * 8;
  transaction.tx_buffer = buffer;
  const esp_err_t err = spi_device_transmit(this->device_, &transaction);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI transmit of %u bytes: %s", static_cast<unsigned>(bytes), esp_err_to_name(err));
  }
}

// The 74HC595 outputs are active low. Q5 is tube 0 and Q0 is tube 5, so the first
// two bits shifted in are unused and end up on Q7 and Q6.
void ElekstubeIps::select_(uint8_t tube_mask) {
  const uint8_t outputs = static_cast<uint8_t>(~(tube_mask << 2));
  for (uint8_t bit = 0; bit < 8; bit++) {
    gpio_set_level(PIN_CS_DATA, (outputs >> bit) & 1);
    gpio_set_level(PIN_CS_CLOCK, 1);
    gpio_set_level(PIN_CS_CLOCK, 0);
  }
  gpio_set_level(PIN_CS_LATCH, 1);
  gpio_set_level(PIN_CS_LATCH, 0);
}

void ElekstubeIps::send_command_(uint8_t command, const uint8_t *data, size_t length) {
  gpio_set_level(PIN_DC, 0);
  this->transmit_(&command, 1);
  gpio_set_level(PIN_DC, 1);
  if (length > 0) {
    this->transmit_(data, length);
  }
}

void ElekstubeIps::send_pixels_(const uint16_t *pixels, size_t count) {
  this->transmit_(pixels, count * sizeof(uint16_t));
}

// Tube N shows the digit N + 1, which also reveals a rotated or mirrored panel. The border sits
// on the outermost pixels and the grid fills the rest, so any framing error shows.
void ElekstubeIps::render_identify_strip_(uint8_t tube, uint16_t first_row, uint16_t rows) {
  static constexpr uint16_t GRID_STEP = 15;
  static constexpr uint16_t ONE_CENTRE_SHIFT = 38;
  uint16_t *pixel = this->strip_;
  const uint16_t grid = to_wire(rgb565(6, 12, 6));
  const uint16_t x_shift = tube == 0 ? ONE_CENTRE_SHIFT : 0;
  for (uint16_t y = first_row; y < first_row + rows; y++) {
    for (uint16_t x = 0; x < WIDTH; x++) {
      const bool border = x < 2 || x >= WIDTH - 2 || y < 2 || y >= HEIGHT - 2;
      if (border) {
        *pixel++ = to_wire(rgb565(31, 0, 0));
      } else if (digit_pixel_lit(TUBE_DIGITS[tube], x + x_shift, y)) {
        *pixel++ = 0xFFFF;
      } else {
        *pixel++ = (x % GRID_STEP == 0 || y % GRID_STEP == 0) ? grid : 0x0000;
      }
    }
  }
}

void ElekstubeIps::set_window_(uint16_t first_column, uint16_t last_column, uint16_t first_row, uint16_t last_row) {
  const uint16_t x0 = this->x_offset_ + first_column;
  const uint16_t x1 = this->x_offset_ + last_column;
  const uint16_t y0 = this->y_offset_ + first_row;
  const uint16_t y1 = this->y_offset_ + last_row;
  const uint8_t columns[] = {static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0), static_cast<uint8_t>(x1 >> 8),
                             static_cast<uint8_t>(x1)};
  const uint8_t rows[] = {static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0), static_cast<uint8_t>(y1 >> 8),
                          static_cast<uint8_t>(y1)};

  this->send_command_(CMD_CASET, columns, sizeof(columns));
  this->send_command_(CMD_RASET, rows, sizeof(rows));
  this->send_command_(CMD_RAMWR);
}

struct JpegSession {
  ElekstubeIps *owner;
  const uint8_t *data;
  size_t size;
  size_t position;
  uint16_t *strip;
  uint16_t dim;
};

unsigned int ElekstubeIps::jpeg_input_(void *decoder, uint8_t *buffer, unsigned int bytes) {
  auto *session = static_cast<JpegSession *>(static_cast<JDEC *>(decoder)->device);
  const size_t available = session->size - session->position;
  const size_t count = bytes < available ? bytes : available;
  if (buffer != nullptr) {
    memcpy(buffer, session->data + session->position, count);
  }
  session->position += count;
  return count;
}

// The decoder delivers blocks left to right, so a row of blocks is complete at the right edge.
unsigned int ElekstubeIps::jpeg_output_(void *decoder, void *bitmap, void *rect_ptr) {
  auto *session = static_cast<JpegSession *>(static_cast<JDEC *>(decoder)->device);
  const auto *rect = static_cast<JRECT *>(rect_ptr);
  const auto *source = static_cast<const uint8_t *>(bitmap);
  const uint16_t dim = session->dim;
  for (uint16_t y = rect->top; y <= rect->bottom; y++) {
    uint16_t *target = session->strip + (y - rect->top) * WIDTH + rect->left;
    for (uint16_t x = rect->left; x <= rect->right; x++) {
      const uint8_t r = (source[0] * dim) >> 8;
      const uint8_t g = (source[1] * dim) >> 8;
      const uint8_t b = (source[2] * dim) >> 8;
      *target++ = to_wire(rgb565(r >> 3, g >> 2, b >> 3));
      source += 3;
    }
  }
  if (rect->right == WIDTH - 1) {
    session->owner->send_pixels_(session->strip, WIDTH * (rect->bottom - rect->top + 1));
  }
  return 1;
}

uint16_t ElekstubeIps::current_dim_() const {
  // Pixel values are gamma encoded: the exponent makes the percentage match the light emitted.
  return static_cast<uint16_t>(256.0f * powf(this->applied_brightness_ / 100.0f, 1.0f / DISPLAY_GAMMA));
}

void ElekstubeIps::draw_jpeg_(uint8_t tube_mask, const uint8_t *jpeg, size_t size, uint8_t level_percent) {
  const uint16_t dim = this->current_dim_() * level_percent / 100;
  JpegSession session{this, jpeg, size, 0, this->strip_, dim};
  JDEC decoder{};
  using InputFunc = UINT (*)(JDEC *, BYTE *, UINT);
  using OutputFunc = UINT (*)(JDEC *, void *, JRECT *);

  this->select_(tube_mask);
  this->set_window_();
  JRESULT result = jd_prepare(&decoder, reinterpret_cast<InputFunc>(&ElekstubeIps::jpeg_input_), this->jpeg_pool_,
                              JPEG_POOL_BYTES, &session);
  if (result == JDR_OK) {
    result = jd_decomp(&decoder, reinterpret_cast<OutputFunc>(&ElekstubeIps::jpeg_output_), 0);
  }
  this->select_(0);
  if (result != JDR_OK) {
    ESP_LOGE(TAG, "JPEG decode failed: %d", result);
  }
}

void ElekstubeIps::draw_blank_(uint8_t tube_mask) {
  memset(this->strip_, 0, STRIP_PIXELS * sizeof(uint16_t));
  this->select_(tube_mask);
  this->set_window_();
  for (uint16_t row = 0; row < HEIGHT; row += STRIP_ROWS) {
    const uint16_t count = HEIGHT - row < STRIP_ROWS ? HEIGHT - row : STRIP_ROWS;
    this->send_pixels_(this->strip_, WIDTH * count);
  }
  this->select_(0);
}

void ElekstubeIps::draw_identify_(uint8_t tube) {
  this->select_(1 << tube);
  this->set_window_();
  for (uint16_t row = 0; row < HEIGHT; row += STRIP_ROWS) {
    const uint16_t count = HEIGHT - row < STRIP_ROWS ? HEIGHT - row : STRIP_ROWS;
    this->render_identify_strip_(tube, row, count);
    this->send_pixels_(this->strip_, WIDTH * count);
  }
  this->select_(0);
}

uint8_t ElekstubeIps::target_brightness_(const ESPTime &now) const {
  if (!this->displays_on_.load()) {
    return 0;
  }
  if (this->night_enabled_.load() && now.is_valid() &&
      hour_in_night(now.hour, this->night_start_.load(), this->night_end_.load())) {
    return this->night_brightness_.load();
  }
  return this->brightness_.load();
}

// Driving GPIO 27 low does not turn the panels off on this clock, so they sleep by command instead.
void ElekstubeIps::apply_brightness_(uint8_t brightness) {
  if (brightness == 0) {
    if (this->panels_powered_) {
      this->draw_blank_(ALL_TUBES);
      this->broadcast_command_(CMD_DISPOFF);
      this->broadcast_command_(CMD_SLPIN);
      this->panels_powered_ = false;
    }
    return;
  }
  if (!this->panels_powered_) {
    this->panels_powered_ = true;
    this->broadcast_command_(CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
    this->broadcast_command_(CMD_DISPON);
    this->redraw_.store(true);
  }
  if (brightness != this->applied_brightness_) {
    this->applied_brightness_ = brightness;
    this->redraw_.store(true);
  }
}

void ElekstubeIps::draw_digit_(uint8_t tube, uint8_t digit, bool fade) {
  static constexpr uint8_t FADE_LEVELS[] = {60, 25};
  const uint8_t face = this->face_.load();
  const uint8_t mask = 1 << tube;
  const uint8_t previous = this->shown_digits_[tube];
  const bool can_fade = fade && previous < DIGIT_COUNT && digit < DIGIT_COUNT;
  if (can_fade) {
    const FaceImage image = this->face_image_(face, previous);
    for (const uint8_t level : FADE_LEVELS) {
      this->draw_jpeg_(mask, image.data, image.size, level);
    }
  }
  if (digit == DIGIT_BLANK) {
    this->draw_blank_(mask);
  } else {
    const FaceImage image = this->face_image_(face, digit);
    if (can_fade) {
      this->draw_jpeg_(mask, image.data, image.size, FADE_LEVELS[1]);
      this->draw_jpeg_(mask, image.data, image.size, FADE_LEVELS[0]);
    }
    this->draw_jpeg_(mask, image.data, image.size);
  }
  this->shown_digits_[tube] = digit;
}

void ElekstubeIps::draw_effect_(uint8_t tube_mask, uint8_t tube, uint32_t elapsed_ms) {
  const EffectFrame frame{this->active_request_.effect, tube, elapsed_ms, this->active_request_.color,
                          this->current_dim_()};
  this->select_(tube_mask);
  this->set_window_();
  for (uint16_t row = 0; row < HEIGHT; row += STRIP_ROWS) {
    const uint16_t count = HEIGHT - row < STRIP_ROWS ? HEIGHT - row : STRIP_ROWS;
    render_effect_strip(frame, this->strip_, row, count);
    this->send_pixels_(this->strip_, WIDTH * count);
  }
  this->select_(0);
}

bool ElekstubeIps::draw_sweep_tube_(uint8_t tube, uint32_t elapsed_ms) {
  const EffectFrame frame{Effect::SWEEP, tube, elapsed_ms, this->active_request_.color, this->current_dim_()};
  uint16_t line[WIDTH];
  render_sweep_line(frame, line);

  uint16_t *shown = this->sweep_lines_ + tube * WIDTH;
  uint16_t first = 0;
  uint16_t last = WIDTH - 1;
  if (this->sweep_lines_valid_[tube]) {
    while (first < WIDTH && line[first] == shown[first]) {
      first++;
    }
    if (first == WIDTH) {
      return false;
    }
    while (line[last] == shown[last]) {
      last--;
    }
  }
  const uint16_t columns = last - first + 1;
  for (uint16_t row = 0; row < STRIP_ROWS; row++) {
    memcpy(this->strip_ + row * columns, line + first, columns * sizeof(uint16_t));
  }
  this->select_(1 << tube);
  this->set_window_(first, last);
  for (uint16_t row = 0; row < HEIGHT; row += STRIP_ROWS) {
    const uint16_t count = HEIGHT - row < STRIP_ROWS ? HEIGHT - row : STRIP_ROWS;
    this->send_pixels_(this->strip_, columns * count);
  }
  this->select_(0);
  memcpy(shown, line, sizeof(line));
  this->sweep_lines_valid_[tube] = true;
  return true;
}

bool ElekstubeIps::draw_clip_frame_() {
  const AnimationRequest &request = this->active_request_;
  if (!this->store_.ready()) {
    // A download is replacing the library.
    this->stop_requested_.store(true);
    vTaskDelay(1);
    return false;
  }
  const ClipInfo info = this->store_.info(request.clip_index);
  if (info.images_per_frame == SCENE_IMAGES) {
    return this->draw_scene_frame_(info);
  }
  const auto current_frame = [&]() {
    const uint64_t elapsed = esp_timer_get_time() / 1000 - this->animation_start_ms_;
    return static_cast<uint32_t>(elapsed * info.fps / 1000 % info.frames);
  };

  bool drew = false;
  if (info.images_per_frame == 1) {
    const uint32_t frame = current_frame();
    if (frame != this->clip_shown_frames_[0]) {
      const uint8_t *data;
      uint32_t size;
      this->store_.image(request.clip_index, frame, 0, &data, &size);
      this->draw_jpeg_(request.tube_mask, data, size);
      this->clip_shown_frames_[0] = frame;
      drew = true;
    }
  } else {
    for (uint8_t tube = 0; tube < TUBE_COUNT; tube++) {
      if ((request.tube_mask & (1 << tube)) == 0) {
        continue;
      }
      const uint32_t frame = current_frame();
      if (frame != this->clip_shown_frames_[tube]) {
        const uint8_t *data;
        uint32_t size;
        // Images are stored left to right, and tube 5 is the leftmost one.
        this->store_.image(request.clip_index, frame, TUBE_COUNT - 1 - tube, &data, &size);
        this->draw_jpeg_(1 << tube, data, size);
        this->clip_shown_frames_[tube] = frame;
        drew = true;
        vTaskDelay(1);
      }
    }
  }
  vTaskDelay(1);
  return drew;
}

// A patch needs every patch before it, so a scene that falls behind catches up instead of skipping.
bool ElekstubeIps::draw_scene_frame_(const ClipInfo &info) {
  const AnimationRequest &request = this->active_request_;
  const uint64_t elapsed_ms = esp_timer_get_time() / 1000 - this->animation_start_ms_;
  if (this->scene_frames_drawn_ > elapsed_ms * info.fps / 1000) {
    vTaskDelay(1);
    return false;
  }
  const int64_t started_us = esp_timer_get_time();
  const uint32_t frame = this->scene_frames_drawn_ % info.frames;
  if (frame == 0) {
    if (this->scene_frames_drawn_ > 0 && this->finish_at_loop_end_) {
      this->stop_requested_.store(true);
      return false;
    }
    if (this->scene_frames_drawn_ > 0) {
      ESP_LOGI(TAG, "Scene loop: %u frames in %u ms, slowest frame %u us", static_cast<unsigned>(info.frames),
               static_cast<unsigned>(elapsed_ms), static_cast<unsigned>(this->scene_slowest_us_));
    }
    this->draw_blank_(request.tube_mask);
  }

  const uint8_t *palette_data;
  uint32_t palette_size;
  this->store_.table_entry(request.clip_index, 0, &palette_data, &palette_size);
  uint16_t palette[SCENE_PALETTE_BYTES / sizeof(uint16_t)];
  const uint16_t dim = this->current_dim_();
  for (size_t index = 0; index < SCENE_PALETTE_BYTES / sizeof(uint16_t); index++) {
    uint16_t color;
    memcpy(&color, palette_data + index * sizeof(color), sizeof(color));
    if (dim >= 256) {
      palette[index] = to_wire(color);
    } else {
      const uint8_t r = (((color >> 11) & 0x1F) * dim) >> 8;
      const uint8_t g = (((color >> 5) & 0x3F) * dim) >> 8;
      const uint8_t b = ((color & 0x1F) * dim) >> 8;
      palette[index] = to_wire(rgb565(r, g, b));
    }
  }

  const uint8_t *record;
  uint32_t record_size;
  this->store_.table_entry(request.clip_index, 1 + frame, &record, &record_size);
  uint16_t count;
  memcpy(&count, record, sizeof(count));
  for (uint16_t index = 0; index < count; index++) {
    ScenePatch patch;
    memcpy(&patch, record + sizeof(count) + index * sizeof(patch), sizeof(patch));
    if ((request.tube_mask & (1 << patch.tube)) == 0) {
      continue;
    }
    const uint8_t *blob;
    uint32_t blob_size;
    this->store_.table_entry(request.clip_index, 1 + info.frames + patch.blob, &blob, &blob_size);
    this->draw_scene_patch_(patch, blob, blob_size, palette);
  }

  const uint32_t took_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
  if (took_us > this->scene_slowest_us_) {
    this->scene_slowest_us_ = took_us;
  }
  if (info.leds) {
    const uint8_t *track;
    uint32_t track_size;
    this->store_.table_entry(request.clip_index, 1 + info.frames + info.scene_blobs, &track, &track_size);
    if (track_size == info.frames * LED_FRAME_BYTES) {
      const uint8_t *colors = track + frame * LED_FRAME_BYTES;
      for (int led = 0; led < TUBE_COUNT; led++) {
        this->led_values_[led].store((colors[led * 3] << 16) | (colors[led * 3 + 1] << 8) | colors[led * 3 + 2]);
      }
      this->led_dirty_.store(true);
      this->led_track_active_.store(true);
    }
  }
  this->scene_frames_drawn_++;
  vTaskDelay(1);
  return true;
}

// The pixels are palette indices compressed with RLE: a token below 128 repeats the next byte
// token + 1 times, and any other token is followed by token - 127 literal bytes.
void ElekstubeIps::draw_scene_patch_(const ScenePatch &patch, const uint8_t *data, uint32_t size,
                                     const uint16_t *palette) {
  this->select_(1 << patch.tube);
  this->set_window_(patch.x, patch.x + patch.width - 1, patch.y, patch.y + patch.height - 1);
  uint32_t remaining = static_cast<uint32_t>(patch.width) * patch.height;
  uint32_t position = 0;
  size_t filled = 0;
  while (remaining > 0 && position < size) {
    const uint8_t token = data[position++];
    const bool repeat = token < 128;
    uint32_t count = repeat ? token + 1u : token - 127u;
    if (count > remaining) {
      count = remaining;
    }
    if (position + (repeat ? 1 : count) > size) {
      break;
    }
    remaining -= count;
    for (uint32_t index = 0; index < count; index++) {
      this->strip_[filled++] = palette[repeat ? data[position] : data[position + index]];
      if (filled == STRIP_PIXELS) {
        this->send_pixels_(this->strip_, filled);
        filled = 0;
      }
    }
    position += repeat ? 1 : count;
  }
  if (filled > 0) {
    this->send_pixels_(this->strip_, filled);
  }
  this->select_(0);
}

bool ElekstubeIps::draw_animation_frame_(uint32_t elapsed_ms) {
  const AnimationRequest &request = this->active_request_;
  if (request.effect == Effect::CLIP) {
    return this->draw_clip_frame_();
  }
  if (request.effect == Effect::COUNTDOWN) {
    const uint32_t remaining = (request.duration_ms - elapsed_ms + 999) / 1000;
    const uint32_t hours = remaining / 3600;
    const uint32_t minutes = remaining / 60 % 60;
    const uint32_t seconds = remaining % 60;
    uint8_t digits[TUBE_COUNT] = {
        static_cast<uint8_t>(seconds % 10), static_cast<uint8_t>(seconds / 10), static_cast<uint8_t>(minutes % 10),
        static_cast<uint8_t>(minutes / 10), static_cast<uint8_t>(hours % 10),   DIGIT_BLANK,
    };
    if (remaining <= COUNTDOWN_FINAL_SECONDS && request.tube_mask == ALL_TUBES) {
      // The last seconds move to the two centre tubes, where the launch will happen.
      memset(digits, DIGIT_BLANK, sizeof(digits));
      digits[CENTRE_RIGHT_TUBE] = static_cast<uint8_t>(seconds % 10);
      digits[CENTRE_LEFT_TUBE] = static_cast<uint8_t>(seconds / 10);
    }
    for (uint8_t tube = 0; tube < TUBE_COUNT; tube++) {
      if ((request.tube_mask & (1 << tube)) != 0 && digits[tube] != this->shown_digits_[tube]) {
        this->draw_digit_(tube, digits[tube], false);
      }
    }
    vTaskDelay(CLOCK_POLL);
    return true;
  }
  if (request.effect == Effect::PULSE) {
    // Every tube shows the same picture, so they can all be written at once.
    this->draw_effect_(request.tube_mask, 0, elapsed_ms);
    vTaskDelay(1);
    return true;
  }
  bool drew = false;
  for (uint8_t tube = 0; tube < TUBE_COUNT; tube++) {
    if ((request.tube_mask & (1 << tube)) == 0) {
      continue;
    }
    // A frame takes long enough to draw that each tube needs its own time, or moving shapes break between tubes.
    const uint32_t tube_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000 - this->animation_start_ms_);
    if (request.effect == Effect::SWEEP) {
      if (!this->draw_sweep_tube_(tube, tube_ms)) {
        continue;
      }
    } else {
      this->draw_effect_(1 << tube, tube, tube_ms);
    }
    drew = true;
    vTaskDelay(1);
  }
  if (!drew) {
    vTaskDelay(1);
  }
  return drew;
}

void ElekstubeIps::update_animation_(int64_t now_ms) {
  bool started = false;
  portENTER_CRITICAL(&this->request_lock_);
  if (this->has_pending_request_) {
    this->active_request_ = this->pending_request_;
    this->has_pending_request_ = false;
    started = true;
  }
  portEXIT_CRITICAL(&this->request_lock_);

  const bool stop = this->stop_requested_.exchange(false);
  if (started) {
    // A request from outside ends the startup, as it means the clock is already in use.
    this->startup_stage_.store(StartupStage::DONE);
    this->begin_animation_(now_ms);
    return;
  }
  if (!this->animation_active_) {
    return;
  }
  const int64_t elapsed = now_ms - this->animation_start_ms_;
  this->active_elapsed_s_.store(static_cast<uint32_t>(elapsed / 1000));
  if (stop || elapsed >= this->active_request_.duration_ms) {
    if (!stop && this->active_request_.effect == Effect::COUNTDOWN && this->active_request_.tube_mask == ALL_TUBES &&
        this->start_clip_(CLIP_COUNTDOWN_END, 0, now_ms)) {
      return;
    }
    this->animation_active_ = false;
    this->led_track_active_.store(false);
    this->active_priority_.store(0);
    this->active_effect_.store(static_cast<uint8_t>(Effect::NONE));
    this->led_event_.store(LED_EVENT_END);
    this->redraw_.store(true);
    ESP_LOGI(TAG, "Animation finished");
  }
}

void ElekstubeIps::begin_animation_(int64_t now_ms) {
  const AnimationRequest &request = this->active_request_;
  this->animation_active_ = true;
  this->animation_start_ms_ = now_ms;
  this->active_priority_.store(request.priority);
  this->active_effect_.store(static_cast<uint8_t>(request.effect));
  this->active_duration_s_.store(request.duration_ms / 1000);
  this->active_elapsed_s_.store(0);
  this->animation_start_ms32_.store(static_cast<uint32_t>(now_ms));
  memset(this->shown_digits_, DIGIT_UNKNOWN, sizeof(this->shown_digits_));
  memset(this->sweep_lines_valid_, 0, sizeof(this->sweep_lines_valid_));
  memset(this->clip_shown_frames_, 0xFF, sizeof(this->clip_shown_frames_));
  this->scene_frames_drawn_ = 0;
  this->scene_slowest_us_ = 0;
  this->finish_at_loop_end_ = false;
  this->led_track_active_.store(false);
  this->led_color_.store((request.color.r << 16) | (request.color.g << 8) | request.color.b);
  // Replacing an animation that changed the LEDs with one that does not must still give them back.
  this->led_event_.store(request.control_leds ? LED_EVENT_START : LED_EVENT_END);
  ESP_LOGI(TAG, "Animation started: %s", effect_name(request.effect));
}

// A duration of 0 plays the clip once. Returns false if the clip is not in the library.
bool ElekstubeIps::start_clip_(const char *name, uint32_t duration_ms, int64_t now_ms) {
  AnimationRequest request;
  request.effect = Effect::CLIP;
  if (!this->store_.find(name, EntryKind::CLIP, &request.clip_index)) {
    return false;
  }
  const ClipInfo info = this->store_.info(request.clip_index);
  request.control_leds = info.leds;
  request.color = {0, 0, 0};
  request.duration_ms = duration_ms != 0 ? duration_ms : info.frames * 1000 / info.fps;
  this->active_request_ = request;
  this->begin_animation_(now_ms);
  return true;
}

void ElekstubeIps::enter_startup_stage_(StartupStage stage, int64_t now_ms) {
  this->startup_stage_.store(stage);
  this->startup_stage_start_ms_ = now_ms;
  AnimationRequest fallback;
  switch (stage) {
    case StartupStage::BOOT:
      if (this->boot_animation_.load()) {
        this->start_clip_(CLIP_BOOT, 0, now_ms);
      }
      break;
    case StartupStage::CONNECTING:
      if (!this->start_clip_(CLIP_CONNECTING, MAX_DURATION_MS, now_ms)) {
        fallback.effect = Effect::SWEEP;
        fallback.color = effect_default_color(Effect::SWEEP);
        fallback.control_leds = false;
        fallback.duration_ms = MAX_DURATION_MS;
        this->active_request_ = fallback;
        this->begin_animation_(now_ms);
      }
      break;
    case StartupStage::ONLINE:
      if (!this->start_clip_(CLIP_ONLINE, 0, now_ms)) {
        fallback.effect = Effect::PULSE;
        fallback.color = {0, 200, 80};
        fallback.control_leds = false;
        fallback.duration_ms = ONLINE_FALLBACK_MS;
        this->active_request_ = fallback;
        this->begin_animation_(now_ms);
      }
      break;
    case StartupStage::SETUP:
      this->setup_drawn_ = false;
      break;
    case StartupStage::DONE:
      this->redraw_.store(true);
      break;
  }
}

void ElekstubeIps::update_startup_(int64_t now_ms, bool time_valid) {
  const StartupStage stage = this->startup_stage_.load();
  if (stage == StartupStage::DONE) {
    return;
  }
  if (this->skip_startup_requested_.exchange(false)) {
    this->stop_requested_.store(this->animation_active_);
    this->enter_startup_stage_(StartupStage::DONE, now_ms);
    return;
  }
  const bool connected = this->wifi_connected_.load();
  const bool configured = this->wifi_configured_.load();
  switch (stage) {
    case StartupStage::BOOT:
      if (!this->animation_active_) {
        const StartupStage next = connected    ? StartupStage::ONLINE
                                  : configured ? StartupStage::CONNECTING
                                               : StartupStage::SETUP;
        this->enter_startup_stage_(next, now_ms);
      }
      break;
    case StartupStage::CONNECTING:
      if (!this->animation_active_) {
        this->enter_startup_stage_(connected ? StartupStage::ONLINE : StartupStage::CONNECTING, now_ms);
      } else if (connected) {
        // A scene ends when the lander is out of sight, so the landing can follow without a jump.
        if (this->active_request_.effect == Effect::CLIP) {
          this->finish_at_loop_end_ = true;
        } else {
          this->stop_requested_.store(true);
        }
      } else if (time_valid && now_ms - this->startup_stage_start_ms_ >= CONNECTING_TIMEOUT_MS) {
        // The time matters more: Wi-Fi keeps trying in the background.
        this->stop_requested_.store(true);
        this->enter_startup_stage_(StartupStage::DONE, now_ms);
      }
      break;
    case StartupStage::ONLINE:
      if (!this->animation_active_) {
        this->enter_startup_stage_(StartupStage::DONE, now_ms);
      }
      break;
    case StartupStage::SETUP:
      if (connected) {
        this->enter_startup_stage_(StartupStage::ONLINE, now_ms);
      }
      break;
    case StartupStage::DONE:
      break;
  }
}

void ElekstubeIps::draw_setup_screen_() {
  // The code first joins the network of the clock. Not every phone then opens the setup page by
  // itself, so once one has joined, the code opens that page.
  const bool show_url = this->setup_client_joined_.load();
  if (!this->setup_drawn_ || show_url != this->setup_qr_shows_url_) {
    if (!this->setup_drawn_) {
      this->draw_blank_(ALL_TUBES);
    } else {
      this->draw_blank_(1 << CENTRE_RIGHT_TUBE);  // the two codes can differ in size
    }
    if (show_url) {
      this->draw_setup_icon_(CENTRE_LEFT_TUBE, SETUP_ICON_GLOBE, sizeof(SETUP_ICON_GLOBE));
    } else {
      this->draw_setup_icon_(CENTRE_LEFT_TUBE, SETUP_ICON_WIFI, sizeof(SETUP_ICON_WIFI));
    }
    char text[96];
    if (show_url) {
      snprintf(text, sizeof(text), "%s", this->setup_url_.c_str());
    } else {
      snprintf(text, sizeof(text), "WIFI:T:nopass;S:%s;;", App.get_name().c_str());
    }
    this->draw_setup_qr_(CENTRE_RIGHT_TUBE, text);
    this->setup_drawn_ = true;
    this->setup_qr_shows_url_ = show_url;
  }
}

void ElekstubeIps::draw_setup_qr_(uint8_t tube, const char *text) {
  uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(SETUP_QR_MAX_VERSION)];
  uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(SETUP_QR_MAX_VERSION)];
  if (!qrcodegen_encodeText(text, scratch, code, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, SETUP_QR_MAX_VERSION,
                            qrcodegen_Mask_AUTO, true)) {
    ESP_LOGE(TAG, "Could not build the setup QR code");
    return;
  }
  // Dark modules on a light panel, with the quiet zone that scanners need around them.
  const int modules = qrcodegen_getSize(code);
  const int total = modules + 2 * SETUP_QR_QUIET_MODULES;
  const int scale = WIDTH / total;
  const int side = total * scale;
  const uint16_t first_column = (WIDTH - side) / 2;
  const uint16_t first_row = (HEIGHT - side) / 2;
  const uint16_t dim = this->current_dim_();
  const uint16_t light = to_wire(rgb565((28 * dim) >> 8, (56 * dim) >> 8, (26 * dim) >> 8));

  this->select_(1 << tube);
  this->set_window_(first_column, first_column + side - 1, first_row, first_row + side - 1);
  size_t filled = 0;
  for (int row = 0; row < side; row++) {
    for (int column = 0; column < side; column++) {
      const bool dark = qrcodegen_getModule(code, column / scale - SETUP_QR_QUIET_MODULES,
                                            row / scale - SETUP_QR_QUIET_MODULES);
      this->strip_[filled++] = dark ? 0x0000 : light;
    }
    if (filled + side > STRIP_PIXELS) {
      this->send_pixels_(this->strip_, filled);
      filled = 0;
    }
  }
  if (filled > 0) {
    this->send_pixels_(this->strip_, filled);
  }
  this->select_(0);
}

// The icons store levels of light, so they share the code that draws the patches of a scene.
void ElekstubeIps::draw_setup_icon_(uint8_t tube, const uint8_t *icon, size_t size) {
  uint16_t palette[256]{};
  const uint32_t dim = this->current_dim_();
  for (uint32_t level = 0; level < SETUP_ICON_LEVELS; level++) {
    const uint32_t shade = level * dim / (SETUP_ICON_LEVELS - 1);
    palette[level] = to_wire(rgb565((31 * shade) >> 8, (63 * shade) >> 8, (31 * shade) >> 8));
  }
  ScenePatch patch{};
  patch.tube = tube;
  patch.x = (WIDTH - SETUP_ICON_SIZE) / 2;
  patch.y = (HEIGHT - SETUP_ICON_SIZE) / 2;
  patch.width = SETUP_ICON_SIZE;
  patch.height = SETUP_ICON_SIZE;
  this->draw_scene_patch_(patch, icon, size, palette);
}

void ElekstubeIps::draw_clock_(const ESPTime &now, uint8_t skip_mask) {
  uint8_t digits[TUBE_COUNT];
  if (now.is_valid()) {
    uint8_t hour = now.hour;
    if (!this->hour_24_.load()) {
      hour = hour % 12 == 0 ? 12 : hour % 12;
    }
    digits[0] = now.second % 10;
    digits[1] = now.second / 10;
    digits[2] = now.minute % 10;
    digits[3] = now.minute / 10;
    digits[4] = hour % 10;
    digits[5] = hour / 10;
    if (digits[5] == 0 && !this->leading_zero_.load()) {
      digits[5] = DIGIT_BLANK;
    }
  } else {
    memset(digits, DIGIT_BLANK, sizeof(digits));
  }

  const bool fade = this->transition_.load() == TRANSITION_FADE && skip_mask == 0;
  for (uint8_t tube = 0; tube < TUBE_COUNT; tube++) {
    if ((skip_mask & (1 << tube)) == 0 && digits[tube] != this->shown_digits_[tube]) {
      this->draw_digit_(tube, digits[tube], fade);
    }
  }
}

void ElekstubeIps::task_entry_(void *arg) { static_cast<ElekstubeIps *>(arg)->task_loop_(); }

void ElekstubeIps::task_loop_() {
  this->init_panels_();
  this->enter_startup_stage_(StartupStage::BOOT, esp_timer_get_time() / 1000);

  uint32_t frames_in_window = 0;
  int64_t window_start = esp_timer_get_time();
  const auto count_frame = [&]() {
    frames_in_window++;
    this->frames_total_.fetch_add(1);
    const int64_t current = esp_timer_get_time();
    if (current - window_start >= 1000000) {
      this->fps_.store(frames_in_window * 1000000.0f / (current - window_start));
      frames_in_window = 0;
      window_start = current;
    }
  };

  while (true) {
    const ESPTime now = this->clock_->now();
    const uint8_t mode = this->mode_.load();
    const int64_t now_ms = esp_timer_get_time() / 1000;
    this->update_animation_(now_ms);
    this->update_startup_(now_ms, now.is_valid());
    this->apply_brightness_(this->target_brightness_(now));
    if (!this->panels_powered_) {
      vTaskDelay(CLOCK_POLL);
      continue;
    }

    const bool redraw = this->redraw_.exchange(false);
    if (mode == MODE_CLOCK && this->startup_stage_.load() == StartupStage::SETUP) {
      this->draw_setup_screen_();
      vTaskDelay(CLOCK_POLL);
      continue;
    }
    if (mode == MODE_CLOCK) {
      if (redraw) {
        memset(this->shown_digits_, DIGIT_UNKNOWN, sizeof(this->shown_digits_));
        memset(this->sweep_lines_valid_, 0, sizeof(this->sweep_lines_valid_));
        memset(this->clip_shown_frames_, 0xFF, sizeof(this->clip_shown_frames_));
      }
      uint8_t animated_tubes = 0;
      if (this->animation_active_) {
        const int64_t started = this->animation_start_ms_;
        if (this->draw_animation_frame_(static_cast<uint32_t>(now_ms - started))) {
          count_frame();
        }
        animated_tubes = this->active_request_.tube_mask;
      }
      this->draw_clock_(now, animated_tubes);
      if (!this->animation_active_) {
        this->fps_.store(0.0f);
        frames_in_window = 0;
        window_start = esp_timer_get_time();
        vTaskDelay(CLOCK_POLL);
      }
      continue;
    }

    for (uint8_t tube = 0; tube < TUBE_COUNT; tube++) {
      this->draw_identify_(tube);
      vTaskDelay(1);
    }
    count_frame();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

}  // namespace elekstube_ips
}  // namespace esphome
