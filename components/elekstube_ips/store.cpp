#include "store.h"

#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_timer.h>
#include <esp_rom_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace elekstube_ips {

static const char *const TAG = "store";

static const char MAGIC[4] = {'E', 'K', 'S', 'T'};
static constexpr uint32_t VERSION = 2;
static constexpr uint32_t MAX_ENTRIES = 255;
static constexpr size_t HEAD_BYTES = 4096;  // also the size of a flash sector
static constexpr size_t CHUNK_BYTES = 2048;
static constexpr size_t ERASE_BLOCK_BYTES = 64 * 1024;
static constexpr uint32_t DOWNLOAD_STACK = 6144;
static constexpr int HTTP_TIMEOUT_MS = 15000;
static constexpr uint32_t ABANDONED_WRITE_MS = 10000;  // a connection that went quiet has gone

struct __attribute__((packed)) Header {
  char magic[4];
  uint32_t version;
  uint32_t total_size;
  uint32_t crc;
  uint32_t entry_count;
};

struct __attribute__((packed)) Entry {
  char name[16];
  uint32_t table_offset;
  uint32_t frames;
  uint16_t fps;
  uint8_t images_per_frame;
  uint8_t kind;
  uint16_t scene_blobs;
  uint16_t reserved;
};
static_assert(sizeof(Entry) == 32, "the entries of the store are 32 bytes");

struct __attribute__((packed)) ImageEntry {
  uint32_t offset;
  uint32_t size;
};

static uint64_t table_length(const Entry &entry) {
  if (entry.images_per_frame == SCENE_IMAGES) {
    return 1 + static_cast<uint64_t>(entry.frames) + entry.scene_blobs;
  }
  return static_cast<uint64_t>(entry.frames) * entry.images_per_frame;
}

static bool scene_is_valid(const uint8_t *base, const Entry &entry) {
  ImageEntry location;
  memcpy(&location, base + entry.table_offset, sizeof(location));
  if (location.size != SCENE_PALETTE_BYTES) {
    return false;
  }
  for (uint32_t frame = 0; frame < entry.frames; frame++) {
    memcpy(&location, base + entry.table_offset + (1 + frame) * sizeof(ImageEntry), sizeof(location));
    uint16_t count;
    if (location.size < sizeof(count)) {
      return false;
    }
    memcpy(&count, base + location.offset, sizeof(count));
    if (location.size != sizeof(count) + count * sizeof(ScenePatch)) {
      return false;
    }
    for (uint16_t index = 0; index < count; index++) {
      ScenePatch patch;
      memcpy(&patch, base + location.offset + sizeof(count) + index * sizeof(ScenePatch), sizeof(patch));
      if (patch.tube >= 6 || patch.width == 0 || patch.height == 0 || patch.x + patch.width > 135 ||
          patch.y + patch.height > 240 || patch.blob >= entry.scene_blobs) {
        return false;
      }
    }
  }
  return true;
}

static bool entry_is_valid(const uint8_t *base, uint32_t total, const Entry &entry) {
  const bool face = entry.kind == static_cast<uint8_t>(EntryKind::FACE);
  const bool theme = entry.kind == static_cast<uint8_t>(EntryKind::THEME);
  if (entry.kind > static_cast<uint8_t>(EntryKind::THEME) || entry.frames == 0 || entry.name[0] == '\0') {
    return false;
  }
  if (face ? (entry.images_per_frame != FACE_IMAGES || entry.frames != 1)
      : theme ? (entry.images_per_frame != 1 || entry.frames != 1)
              : (entry.fps == 0 || (entry.images_per_frame != 1 && entry.images_per_frame != 6 &&
                                    entry.images_per_frame != SCENE_IMAGES))) {
    return false;
  }
  const uint64_t images = table_length(entry);
  if (entry.table_offset + images * sizeof(ImageEntry) > total) {
    return false;
  }
  for (uint64_t image = 0; image < images; image++) {
    ImageEntry location;
    memcpy(&location, base + entry.table_offset + image * sizeof(ImageEntry), sizeof(location));
    if (static_cast<uint64_t>(location.offset) + location.size > total) {
      return false;
    }
  }
  return entry.images_per_frame != SCENE_IMAGES || scene_is_valid(base, entry);
}

void Store::load() {
  this->ready_.store(false);
  this->entry_count_.store(0);
  this->size_ = 0;
  if (this->mapped_ == nullptr || this->partition_ == nullptr) {
    return;
  }
  const uint8_t *base = this->mapped_;
  Header header;
  memcpy(&header, base, sizeof(header));
  if (memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0 || header.version != VERSION ||
      header.total_size <= sizeof(Header) || header.total_size > this->partition_->size || header.entry_count == 0 ||
      header.entry_count > MAX_ENTRIES) {
    ESP_LOGI(TAG, "No store in flash");
    return;
  }
  if (esp_rom_crc32_le(0, base + sizeof(Header), header.total_size - sizeof(Header)) != header.crc) {
    ESP_LOGW(TAG, "The store is damaged");
    return;
  }
  const uint64_t entries_end = sizeof(Header) + static_cast<uint64_t>(header.entry_count) * sizeof(Entry);
  if (entries_end > header.total_size) {
    ESP_LOGW(TAG, "The store is malformed");
    return;
  }
  uint32_t faces = 0;
  uint32_t themes = 0;
  for (uint32_t index = 0; index < header.entry_count; index++) {
    Entry entry;
    memcpy(&entry, base + sizeof(Header) + index * sizeof(Entry), sizeof(entry));
    if (!entry_is_valid(base, header.total_size, entry)) {
      ESP_LOGW(TAG, "The store is malformed");
      return;
    }
    faces += entry.kind == static_cast<uint8_t>(EntryKind::FACE);
    themes += entry.kind == static_cast<uint8_t>(EntryKind::THEME);
  }
  if (faces > MAX_STORE_FACES || themes > 1) {
    ESP_LOGW(TAG, "The store has too many faces or themes");
    return;
  }
  this->base_ = base;
  this->size_ = header.total_size;
  this->entry_count_.store(header.entry_count);
  this->ready_.store(true);
  ESP_LOGI(TAG, "Store loaded: %u faces, %u clips, %u bytes", static_cast<unsigned>(this->count(EntryKind::FACE)),
           static_cast<unsigned>(this->count(EntryKind::CLIP)), static_cast<unsigned>(header.total_size));
}

static Entry read_entry(const uint8_t *base, uint8_t index) {
  Entry entry;
  memcpy(&entry, base + sizeof(Header) + index * sizeof(Entry), sizeof(entry));
  return entry;
}

EntryKind Store::kind(uint8_t entry) const {
  return static_cast<EntryKind>(read_entry(this->base_, entry).kind);
}

void Store::name(uint8_t entry, char *buffer) const {
  const Entry data = read_entry(this->base_, entry);
  memcpy(buffer, data.name, NAME_BYTES);
  buffer[NAME_BYTES] = '\0';
}

bool Store::find(const char *name, EntryKind kind, uint8_t *entry) const {
  if (!this->ready()) {
    return false;
  }
  const uint32_t count = this->entry_count_.load();
  for (uint32_t index = 0; index < count; index++) {
    const Entry data = read_entry(this->base_, index);
    if (data.kind == static_cast<uint8_t>(kind) && strncmp(data.name, name, sizeof(data.name)) == 0) {
      *entry = static_cast<uint8_t>(index);
      return true;
    }
  }
  return false;
}

bool Store::nth(EntryKind kind, uint8_t nth, uint8_t *entry) const {
  if (!this->ready()) {
    return false;
  }
  const uint32_t count = this->entry_count_.load();
  for (uint32_t index = 0; index < count; index++) {
    if (read_entry(this->base_, index).kind == static_cast<uint8_t>(kind) && nth-- == 0) {
      *entry = static_cast<uint8_t>(index);
      return true;
    }
  }
  return false;
}

uint8_t Store::count(EntryKind kind) const {
  if (!this->ready()) {
    return 0;
  }
  uint8_t found = 0;
  const uint32_t count = this->entry_count_.load();
  for (uint32_t index = 0; index < count; index++) {
    found += read_entry(this->base_, index).kind == static_cast<uint8_t>(kind);
  }
  return found;
}

bool Store::theme_name(char *buffer) const {
  uint8_t entry;
  if (!this->nth(EntryKind::THEME, 0, &entry)) {
    buffer[0] = '\0';
    return false;
  }
  this->name(entry, buffer);
  return true;
}

ClipInfo Store::info(uint8_t entry) const {
  const Entry data = read_entry(this->base_, entry);
  return {data.frames, data.fps, data.images_per_frame, data.scene_blobs};
}

void Store::image(uint8_t entry, uint32_t frame, uint8_t image, const uint8_t **data, uint32_t *size) const {
  const ClipInfo clip = this->info(entry);
  this->table_entry(entry, frame * clip.images_per_frame + image, data, size);
}

void Store::table_entry(uint8_t entry, uint32_t position, const uint8_t **data, uint32_t *size) const {
  const Entry info = read_entry(this->base_, entry);
  ImageEntry location;
  memcpy(&location, this->base_ + info.table_offset + position * sizeof(ImageEntry), sizeof(location));
  *data = this->base_ + location.offset;
  *size = location.size;
}

std::string Store::state() const {
  switch (this->status_.load()) {
    case Status::DOWNLOADING:
      return "downloading " + std::to_string(this->percent_.load()) + "%";
    case Status::WRITING:
      return "receiving " + std::to_string(this->percent_.load()) + "%";
    case Status::FAILED_HTTP:
      return "failed: could not download";
    case Status::FAILED_FORMAT:
      return "failed: not a store";
    case Status::FAILED_FLASH:
      return "failed: flash error";
    case Status::FAILED_CRC:
      return "failed: damaged data";
    case Status::FAILED_BUSY:
      return "failed: busy";
    default:
      break;
  }
  return std::to_string(this->count(EntryKind::FACE)) + " faces, " + std::to_string(this->count(EntryKind::CLIP)) +
         " clips";
}

bool Store::begin_write() {
  if (this->partition_ == nullptr) {
    return false;
  }
  Status expected = this->status_.load();
  if (expected == Status::WRITING && millis() - this->last_write_ms_.load() > ABANDONED_WRITE_MS) {
    // The client of an earlier write left without finishing it.
    this->writer_.reset();
    this->status_.store(Status::FAILED_BUSY);
    expected = Status::FAILED_BUSY;
  }
  if (expected == Status::DOWNLOADING || expected == Status::WRITING ||
      !this->status_.compare_exchange_strong(expected, Status::WRITING)) {
    ESP_LOGW(TAG, "A store is already being written");
    return false;
  }
  this->last_write_ms_.store(millis());
  this->writer_ = std::make_unique<Writer>();
  this->writer_->head.reset(new (std::nothrow) uint8_t[HEAD_BYTES]);
  if (this->writer_->head == nullptr) {
    this->writer_.reset();
    this->status_.store(Status::FAILED_FLASH);
    return false;
  }
  this->percent_.store(0);
  return true;
}

void Store::abort_write(Status reason) {
  this->writer_.reset();
  this->status_.store(reason);
}

// The first bytes wait in memory until everything else is in flash and checked.
Store::Status Store::write(const uint8_t *data, size_t length) {
  if (this->writer_ == nullptr) {
    return Status::FAILED_BUSY;
  }
  Writer &writer = *this->writer_;
  this->last_write_ms_.store(millis());
  while (length > 0) {
    size_t take = length;
    // The header is checked as soon as it arrives, before anything is erased.
    if (writer.received < sizeof(Header)) {
      take = std::min<size_t>(take, sizeof(Header) - writer.received);
    }
    if (writer.total != 0 && writer.received + take > writer.total) {
      this->abort_write(Status::FAILED_FORMAT);
      return Status::FAILED_FORMAT;
    }
    if (writer.received < HEAD_BYTES) {
      memcpy(writer.head.get() + writer.received, data, std::min<size_t>(take, HEAD_BYTES - writer.received));
    }
    if (writer.received + take > HEAD_BYTES) {
      const size_t skip = writer.received < HEAD_BYTES ? HEAD_BYTES - writer.received : 0;
      const int64_t write_started = esp_timer_get_time();
      if (!writer.erased ||
          esp_partition_write(this->partition_, writer.received + skip, data + skip, take - skip) != ESP_OK) {
        this->abort_write(Status::FAILED_FLASH);
        return Status::FAILED_FLASH;
      }
      writer.write_us += static_cast<uint32_t>(esp_timer_get_time() - write_started);
      vTaskDelay(1);
    }
    if (writer.received >= sizeof(Header)) {
      writer.crc = esp_rom_crc32_le(writer.crc, data, take);
    }
    writer.received += take;
    data += take;
    length -= take;

    if (writer.total == 0 && writer.received == sizeof(Header)) {
      Header header;
      memcpy(&header, writer.head.get(), sizeof(header));
      if (memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0 || header.version != VERSION ||
          header.total_size <= sizeof(Header) || header.total_size > this->partition_->size ||
          header.entry_count == 0 || header.entry_count > MAX_ENTRIES) {
        this->abort_write(Status::FAILED_FORMAT);
        return Status::FAILED_FORMAT;
      }
      writer.total = header.total_size;
      writer.expected_crc = header.crc;
      // From here on the current store is gone.
      this->ready_.store(false);
      // Erased in blocks, with a pause between them, so that the clock keeps drawing in between.
      const size_t erase = (header.total_size + HEAD_BYTES - 1) / HEAD_BYTES * HEAD_BYTES;
      const int64_t erase_started = esp_timer_get_time();
      for (size_t erased = 0; erased < erase; erased += ERASE_BLOCK_BYTES) {
        if (esp_partition_erase_range(this->partition_, erased, std::min<size_t>(ERASE_BLOCK_BYTES, erase - erased)) !=
            ESP_OK) {
          this->abort_write(Status::FAILED_FLASH);
          return Status::FAILED_FLASH;
        }
        vTaskDelay(1);
      }
      writer.erase_ms = static_cast<uint32_t>((esp_timer_get_time() - erase_started) / 1000);
      writer.erased = true;
    }
    if (writer.total != 0) {
      this->percent_.store(static_cast<uint8_t>(static_cast<uint64_t>(writer.received) * 100 / writer.total));
    }
  }
  return Status::WRITING;
}

Store::Status Store::finish_write() {
  if (this->writer_ == nullptr) {
    return Status::FAILED_BUSY;
  }
  Writer &writer = *this->writer_;
  if (writer.total == 0 || writer.received != writer.total) {
    this->abort_write(Status::FAILED_FORMAT);
    return Status::FAILED_FORMAT;
  }
  if (writer.crc != writer.expected_crc) {
    this->abort_write(Status::FAILED_CRC);
    return Status::FAILED_CRC;
  }
  ESP_LOGI(TAG, "Flash was busy %u ms erasing and %u ms writing", static_cast<unsigned>(writer.erase_ms),
           static_cast<unsigned>(writer.write_us / 1000));
  // Last step: the store only becomes valid once its first bytes are in place.
  if (esp_partition_write(this->partition_, 0, writer.head.get(), std::min<size_t>(writer.total, HEAD_BYTES)) !=
      ESP_OK) {
    this->abort_write(Status::FAILED_FLASH);
    return Status::FAILED_FLASH;
  }
  this->writer_.reset();
  this->load();
  const Status result = this->ready() ? Status::IDLE : Status::FAILED_FORMAT;
  this->status_.store(result);
  return result;
}

bool Store::start_download(const char *url) {
  if (this->partition_ == nullptr) {
    return false;
  }
  Status expected = this->status_.load();
  if (expected == Status::DOWNLOADING || expected == Status::WRITING ||
      !this->status_.compare_exchange_strong(expected, Status::DOWNLOADING)) {
    ESP_LOGW(TAG, "A download is already running");
    return false;
  }
  this->url_ = url;
  this->percent_.store(0);
  if (xTaskCreate(download_entry_, "store_download", DOWNLOAD_STACK, this, 1, nullptr) != pdPASS) {
    this->status_.store(Status::FAILED_FLASH);
    return false;
  }
  return true;
}

void Store::download_entry_(void *arg) {
  static_cast<Store *>(arg)->download_();
  vTaskDelete(nullptr);
}

void Store::download_() {
  const Status result = this->run_download_();
  if (result != Status::IDLE) {
    this->writer_.reset();
    this->status_.store(result);
  }
  ESP_LOGI(TAG, "Download finished: %s", this->state().c_str());
}

static int read_fully(esp_http_client_handle_t client, char *buffer, int length) {
  int received = 0;
  while (received < length) {
    const int count = esp_http_client_read(client, buffer + received, length - received);
    if (count < 0) {
      return -1;
    }
    if (count == 0) {
      break;
    }
    received += count;
  }
  return received;
}

Store::Status Store::run_download_() {
  struct ClientCloser {
    void operator()(esp_http_client *client) const {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
    }
  };

  esp_http_client_config_t config{};
  config.url = this->url_.c_str();
  config.timeout_ms = HTTP_TIMEOUT_MS;
  std::unique_ptr<esp_http_client, ClientCloser> client(esp_http_client_init(&config));
  if (client == nullptr || esp_http_client_open(client.get(), 0) != ESP_OK ||
      esp_http_client_fetch_headers(client.get()) < 0 || esp_http_client_get_status_code(client.get()) != 200) {
    return Status::FAILED_HTTP;
  }

  this->writer_ = std::make_unique<Writer>();
  this->writer_->head.reset(new (std::nothrow) uint8_t[HEAD_BYTES]);
  std::unique_ptr<char, decltype(&heap_caps_free)> chunk(
      static_cast<char *>(heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)), heap_caps_free);
  if (this->writer_->head == nullptr || chunk == nullptr) {
    return Status::FAILED_FLASH;
  }

  while (true) {
    const int received = read_fully(client.get(), chunk.get(), CHUNK_BYTES);
    if (received < 0) {
      return Status::FAILED_HTTP;
    }
    if (received > 0) {
      const Status written = this->write(reinterpret_cast<const uint8_t *>(chunk.get()), received);
      if (written != Status::WRITING) {
        return written;
      }
    }
    if (this->writer_->total != 0 && this->writer_->received == this->writer_->total) {
      break;
    }
    if (received < static_cast<int>(CHUNK_BYTES)) {
      return Status::FAILED_HTTP;  // the file ended before its own total size
    }
    vTaskDelay(1);
  }
  return this->finish_write();
}

}  // namespace elekstube_ips
}  // namespace esphome
