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

#include "bench_jpegs.h"
#include "effects.h"
#include "esphome/components/light/light_call.h"
#include "esphome/core/log.h"

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
static constexpr TickType_t CLOCK_POLL = pdMS_TO_TICKS(50);
static constexpr uint8_t TRANSITION_FADE = 1;
static constexpr uint8_t LED_EVENT_NONE = 0;
static constexpr uint8_t LED_EVENT_START = 1;
static constexpr uint8_t LED_EVENT_END = 2;

// Partition layout written by tools/convert.py.
static constexpr esp_partition_subtype_t FACE_PARTITION_SUBTYPE = static_cast<esp_partition_subtype_t>(0x40);
static const char FACE_PACK_MAGIC[4] = {'E', 'K', 'F', 'P'};
static constexpr uint32_t FACE_PACK_MAX_SETS = 8;

struct FacePackHeader {
  char magic[4];
  uint32_t set_count;
};

struct FacePackSet {
  char name[16];
  uint32_t offsets[DIGIT_COUNT];
  uint32_t sizes[DIGIT_COUNT];
};

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
  if (!this->init_bus_()) {
    this->mark_failed();
    return;
  }
  this->load_face_pack_();
  // Runs on the main loop's core: WiFi stays on the other one.
  if (xTaskCreatePinnedToCore(task_entry_, "tubes", 8192, this, 1, nullptr, 1) != pdPASS) {
    ESP_LOGE(TAG, "Could not start the drawing task");
    this->mark_failed();
  }
}

void ElekstubeIps::loop() {
  this->apply_led_event_();
  this->update_led_pulse_();
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

bool ElekstubeIps::play_request(const char *type, const char *name, const char *tubes, int duration_s, int priority,
                                const char *color) {
  if (strcmp(type, "effect") != 0) {
    ESP_LOGW(TAG, "Unsupported animation type: %s", type);
    return false;
  }
  AnimationRequest request;
  request.effect = effect_from_name(name);
  if (request.effect == Effect::NONE) {
    ESP_LOGW(TAG, "Unknown effect: %s", name);
    return false;
  }
  if (!parse_tubes(tubes, &request.tube_mask)) {
    ESP_LOGW(TAG, "Invalid tubes: %s", tubes);
    return false;
  }
  request.color = effect_default_color(request.effect);
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
  ESP_LOGCONFIG(TAG, "EleksTube IPS: %ux%u, offset %u,%u, MADCTL 0x%02X, face pack sets: %u", WIDTH, HEIGHT,
                this->x_offset_, this->y_offset_, this->madctl_, static_cast<unsigned>(this->face_pack_sets_));
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

// A missing or invalid pack is normal: the built-in faces are used instead.
void ElekstubeIps::load_face_pack_() {
  const esp_partition_t *partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, FACE_PARTITION_SUBTYPE, nullptr);
  if (partition == nullptr) {
    ESP_LOGW(TAG, "Face partition not found");
    return;
  }
  const void *mapped = nullptr;
  esp_partition_mmap_handle_t handle;
  const esp_err_t err = esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &mapped, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Could not map the face partition: %s", esp_err_to_name(err));
    return;
  }

  const auto *base = static_cast<const uint8_t *>(mapped);
  FacePackHeader header;
  memcpy(&header, base, sizeof(header));
  if (memcmp(header.magic, FACE_PACK_MAGIC, sizeof(header.magic)) != 0 || header.set_count == 0 ||
      header.set_count > FACE_PACK_MAX_SETS) {
    ESP_LOGI(TAG, "No face pack in the partition");
    esp_partition_munmap(handle);
    return;
  }

  for (uint32_t index = 0; index < header.set_count; index++) {
    FacePackSet set;
    memcpy(&set, base + sizeof(FacePackHeader) + index * sizeof(FacePackSet), sizeof(set));
    for (uint8_t digit = 0; digit < DIGIT_COUNT; digit++) {
      if (set.offsets[digit] > partition->size || set.sizes[digit] > partition->size - set.offsets[digit]) {
        ESP_LOGW(TAG, "Face pack is damaged, ignoring it");
        esp_partition_munmap(handle);
        return;
      }
    }
  }
  this->face_pack_ = base;
  this->face_pack_sets_ = header.set_count;
  ESP_LOGI(TAG, "Face pack loaded with %u sets", static_cast<unsigned>(header.set_count));
}

FaceImage ElekstubeIps::face_image_(uint8_t face, uint8_t digit) const {
  if (face < BUILTIN_FACE_COUNT) {
    return BUILTIN_FACES[face][digit];
  }
  const uint32_t set_index = face - BUILTIN_FACE_COUNT;
  if (this->face_pack_ == nullptr || set_index >= this->face_pack_sets_) {
    return BUILTIN_FACES[0][digit];
  }
  FacePackSet set;
  memcpy(&set, this->face_pack_ + sizeof(FacePackHeader) + set_index * sizeof(FacePackSet), sizeof(set));
  return {this->face_pack_ + set.offsets[digit], set.sizes[digit]};
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

void ElekstubeIps::render_strip_(uint8_t tube, uint8_t mode, uint32_t frame, uint16_t first_row, uint16_t rows) {
  uint16_t *pixel = this->strip_;
  if (mode == MODE_IDENTIFY) {
    // Tube N shows the digit N + 1, which also reveals a rotated or mirrored panel. The
    // border sits on the outermost pixels and the grid fills the rest, so any framing error shows.
    static constexpr uint16_t GRID_STEP = 15;
    static constexpr uint16_t ONE_CENTRE_SHIFT = 38;
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
    return;
  }
  for (uint16_t y = first_row; y < first_row + rows; y++) {
    for (uint16_t x = 0; x < WIDTH; x++) {
      *pixel++ = to_wire(rgb565((x + frame * 2) >> 2, y + frame, tube * 5));
    }
  }
}

void ElekstubeIps::set_window_(uint16_t first_column, uint16_t last_column) {
  const uint16_t x0 = this->x_offset_ + first_column;
  const uint16_t x1 = this->x_offset_ + last_column;
  const uint16_t y0 = this->y_offset_;
  const uint16_t y1 = y0 + HEIGHT - 1;
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

void ElekstubeIps::draw_(uint8_t tube_mask, uint8_t pattern_tube, uint8_t mode, uint32_t frame) {
  this->select_(tube_mask);
  this->set_window_();
  for (uint16_t row = 0; row < HEIGHT; row += STRIP_ROWS) {
    const uint16_t count = HEIGHT - row < STRIP_ROWS ? HEIGHT - row : STRIP_ROWS;
    this->render_strip_(pattern_tube, mode, frame, row, count);
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

bool ElekstubeIps::draw_animation_frame_(uint32_t elapsed_ms) {
  const AnimationRequest &request = this->active_request_;
  if (request.effect == Effect::COUNTDOWN) {
    const uint32_t remaining = (request.duration_ms - elapsed_ms + 999) / 1000;
    const uint32_t hours = remaining / 3600;
    const uint32_t minutes = remaining / 60 % 60;
    const uint32_t seconds = remaining % 60;
    const uint8_t digits[TUBE_COUNT] = {
        static_cast<uint8_t>(seconds % 10), static_cast<uint8_t>(seconds / 10), static_cast<uint8_t>(minutes % 10),
        static_cast<uint8_t>(minutes / 10), static_cast<uint8_t>(hours % 10),   DIGIT_BLANK,
    };
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
    this->led_color_.store((request.color.r << 16) | (request.color.g << 8) | request.color.b);
    this->led_event_.store(LED_EVENT_START);
    ESP_LOGI(TAG, "Animation started: %s", effect_name(request.effect));
    return;
  }
  if (!this->animation_active_) {
    return;
  }
  const int64_t elapsed = now_ms - this->animation_start_ms_;
  this->active_elapsed_s_.store(static_cast<uint32_t>(elapsed / 1000));
  if (stop || elapsed >= this->active_request_.duration_ms) {
    this->animation_active_ = false;
    this->active_priority_.store(0);
    this->active_effect_.store(static_cast<uint8_t>(Effect::NONE));
    this->led_event_.store(LED_EVENT_END);
    this->redraw_.store(true);
    ESP_LOGI(TAG, "Animation finished");
  }
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

  uint32_t frame = 0;
  uint32_t frames_in_window = 0;
  int64_t window_start = esp_timer_get_time();
  const auto count_frame = [&]() {
    frames_in_window++;
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
    this->apply_brightness_(this->target_brightness_(now));
    if (!this->panels_powered_) {
      vTaskDelay(CLOCK_POLL);
      continue;
    }

    const bool redraw = this->redraw_.exchange(false);
    if (mode == MODE_CLOCK) {
      if (redraw) {
        memset(this->shown_digits_, DIGIT_UNKNOWN, sizeof(this->shown_digits_));
        memset(this->sweep_lines_valid_, 0, sizeof(this->sweep_lines_valid_));
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

    if (mode == MODE_BENCH_COMMON) {
      this->draw_(ALL_TUBES, 0, mode, frame);
    } else {
      for (uint8_t tube = 0; tube < TUBE_COUNT; tube++) {
        if (mode == MODE_BENCH_JPEG_PHOTO) {
          this->draw_jpeg_(1 << tube, BENCH_JPEG_PHOTO, sizeof(BENCH_JPEG_PHOTO));
        } else if (mode == MODE_BENCH_JPEG_GRAPHIC) {
          this->draw_jpeg_(1 << tube, BENCH_JPEG_GRAPHIC, sizeof(BENCH_JPEG_GRAPHIC));
        } else {
          this->draw_(1 << tube, tube, mode, frame);
        }
        vTaskDelay(1);
      }
    }
    frame++;
    count_frame();
    vTaskDelay(mode == MODE_IDENTIFY ? pdMS_TO_TICKS(1000) : 1);
  }
}

}  // namespace elekstube_ips
}  // namespace esphome
