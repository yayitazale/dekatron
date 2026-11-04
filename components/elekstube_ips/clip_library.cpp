#include "clip_library.h"

#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_rom_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <memory>

#include "esphome/core/log.h"

namespace esphome {
namespace elekstube_ips {

static const char *const TAG = "clip_library";

static constexpr uint32_t REGION_OFFSET = FACE_REGION_BYTES;
static constexpr uint32_t REGION_BYTES = 0xE0000;
static const char MAGIC[4] = {'E', 'K', 'C', 'L'};
static constexpr uint32_t VERSION = 1;
static constexpr uint32_t MAX_CLIPS = 255;
static constexpr size_t HEAD_BYTES = 4096;
static constexpr size_t CHUNK_BYTES = 2048;
static constexpr uint32_t DOWNLOAD_STACK = 6144;
static constexpr int HTTP_TIMEOUT_MS = 15000;

struct __attribute__((packed)) LibraryHeader {
  char magic[4];
  uint32_t version;
  uint32_t total_size;
  uint32_t crc;
  uint32_t clip_count;
};

struct __attribute__((packed)) ClipEntry {
  char name[16];
  uint32_t table_offset;
  uint32_t frames;
  uint16_t fps;
  uint8_t images_per_frame;
  uint8_t padding[3];
};

struct __attribute__((packed)) ImageEntry {
  uint32_t offset;
  uint32_t size;
};

static bool header_is_valid(const LibraryHeader &header) {
  return memcmp(header.magic, MAGIC, sizeof(MAGIC)) == 0 && header.version == VERSION &&
         header.total_size > sizeof(LibraryHeader) && header.total_size <= REGION_BYTES && header.clip_count > 0 &&
         header.clip_count <= MAX_CLIPS;
}

void ClipLibrary::load() {
  this->ready_.store(false);
  this->clip_count_.store(0);
  if (this->mapped_ == nullptr) {
    return;
  }
  const uint8_t *base = this->mapped_ + REGION_OFFSET;
  LibraryHeader header;
  memcpy(&header, base, sizeof(header));
  if (!header_is_valid(header)) {
    ESP_LOGI(TAG, "No clip library");
    return;
  }
  const uint32_t crc =
      esp_rom_crc32_le(0, base + sizeof(LibraryHeader), header.total_size - sizeof(LibraryHeader));
  if (crc != header.crc) {
    ESP_LOGW(TAG, "Clip library is damaged");
    return;
  }

  const uint64_t entries_end = sizeof(LibraryHeader) + static_cast<uint64_t>(header.clip_count) * sizeof(ClipEntry);
  if (entries_end > header.total_size) {
    ESP_LOGW(TAG, "Clip library is malformed");
    return;
  }
  for (uint32_t index = 0; index < header.clip_count; index++) {
    ClipEntry entry;
    memcpy(&entry, base + sizeof(LibraryHeader) + index * sizeof(ClipEntry), sizeof(entry));
    const uint64_t images = static_cast<uint64_t>(entry.frames) * entry.images_per_frame;
    const uint64_t table_end = entry.table_offset + images * sizeof(ImageEntry);
    if (entry.fps == 0 || entry.frames == 0 || (entry.images_per_frame != 1 && entry.images_per_frame != 6) ||
        table_end > header.total_size) {
      ESP_LOGW(TAG, "Clip library is malformed");
      return;
    }
    for (uint64_t image = 0; image < images; image++) {
      ImageEntry location;
      memcpy(&location, base + entry.table_offset + image * sizeof(ImageEntry), sizeof(location));
      if (static_cast<uint64_t>(location.offset) + location.size > header.total_size) {
        ESP_LOGW(TAG, "Clip library is malformed");
        return;
      }
    }
  }
  this->base_ = base;
  this->clip_count_.store(header.clip_count);
  this->ready_.store(true);
  ESP_LOGI(TAG, "Clip library loaded with %u clips", static_cast<unsigned>(header.clip_count));
}

bool ClipLibrary::find(const char *name, uint8_t *index) const {
  if (!this->ready()) {
    return false;
  }
  const uint32_t count = this->clip_count_.load();
  for (uint32_t clip = 0; clip < count; clip++) {
    ClipEntry entry;
    memcpy(&entry, this->base_ + sizeof(LibraryHeader) + clip * sizeof(ClipEntry), sizeof(entry));
    if (strncmp(entry.name, name, sizeof(entry.name)) == 0) {
      *index = static_cast<uint8_t>(clip);
      return true;
    }
  }
  return false;
}

ClipInfo ClipLibrary::info(uint8_t index) const {
  ClipEntry entry;
  memcpy(&entry, this->base_ + sizeof(LibraryHeader) + index * sizeof(ClipEntry), sizeof(entry));
  return {entry.frames, entry.fps, entry.images_per_frame};
}

void ClipLibrary::image(uint8_t index, uint32_t frame, uint8_t image, const uint8_t **data, uint32_t *size) const {
  ClipEntry entry;
  memcpy(&entry, this->base_ + sizeof(LibraryHeader) + index * sizeof(ClipEntry), sizeof(entry));
  ImageEntry location;
  const uint32_t position = frame * entry.images_per_frame + image;
  memcpy(&location, this->base_ + entry.table_offset + position * sizeof(ImageEntry), sizeof(location));
  *data = this->base_ + location.offset;
  *size = location.size;
}

std::string ClipLibrary::state() const {
  switch (this->status_.load()) {
    case Status::DOWNLOADING:
      return "downloading " + std::to_string(this->percent_.load()) + "%";
    case Status::FAILED_HTTP:
      return "failed: could not download";
    case Status::FAILED_FORMAT:
      return "failed: not a clip library";
    case Status::FAILED_FLASH:
      return "failed: flash error";
    case Status::FAILED_CRC:
      return "failed: damaged download";
    default:
      break;
  }
  return std::to_string(this->clip_count_.load()) + " clips";
}

bool ClipLibrary::start_download(const char *url) {
  if (this->partition_ == nullptr) {
    return false;
  }
  Status expected = this->status_.load();
  if (expected == Status::DOWNLOADING ||
      !this->status_.compare_exchange_strong(expected, Status::DOWNLOADING)) {
    ESP_LOGW(TAG, "A download is already running");
    return false;
  }
  this->url_ = url;
  this->percent_.store(0);
  if (xTaskCreate(download_entry_, "clip_download", DOWNLOAD_STACK, this, 1, nullptr) != pdPASS) {
    this->status_.store(Status::FAILED_FLASH);
    return false;
  }
  return true;
}

void ClipLibrary::download_entry_(void *arg) {
  static_cast<ClipLibrary *>(arg)->download_();
  vTaskDelete(nullptr);
}

void ClipLibrary::download_() {
  const Status result = this->run_download_();
  this->load();
  this->status_.store(result);
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

ClipLibrary::Status ClipLibrary::run_download_() {
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

  std::unique_ptr<char, decltype(&heap_caps_free)> head(
      static_cast<char *>(heap_caps_malloc(HEAD_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)), heap_caps_free);
  std::unique_ptr<char, decltype(&heap_caps_free)> chunk(
      static_cast<char *>(heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)), heap_caps_free);
  if (head == nullptr || chunk == nullptr) {
    return Status::FAILED_FLASH;
  }

  // Nothing is erased until the file looks like a library, so a wrong URL keeps the current one.
  const int head_received = read_fully(client.get(), head.get(), HEAD_BYTES);
  if (head_received < static_cast<int>(sizeof(LibraryHeader))) {
    return Status::FAILED_HTTP;
  }
  LibraryHeader header;
  memcpy(&header, head.get(), sizeof(header));
  if (!header_is_valid(header)) {
    return Status::FAILED_FORMAT;
  }

  this->ready_.store(false);
  if (esp_partition_erase_range(this->partition_, REGION_OFFSET, REGION_BYTES) != ESP_OK) {
    return Status::FAILED_FLASH;
  }

  const uint32_t head_bytes = static_cast<uint32_t>(head_received) < header.total_size
                                  ? static_cast<uint32_t>(head_received)
                                  : header.total_size;
  uint32_t crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(head.get()) + sizeof(LibraryHeader),
                                  head_bytes - sizeof(LibraryHeader));
  uint32_t written = head_bytes;
  while (written < header.total_size) {
    const uint32_t remaining = header.total_size - written;
    const int wanted = remaining < CHUNK_BYTES ? remaining : CHUNK_BYTES;
    const int received = read_fully(client.get(), chunk.get(), wanted);
    if (received != wanted) {
      return Status::FAILED_HTTP;
    }
    if (esp_partition_write(this->partition_, REGION_OFFSET + written, chunk.get(), received) != ESP_OK) {
      return Status::FAILED_FLASH;
    }
    crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t *>(chunk.get()), received);
    written += received;
    this->percent_.store(static_cast<uint8_t>(static_cast<uint64_t>(written) * 100 / header.total_size));
    vTaskDelay(1);
  }
  if (crc != header.crc) {
    return Status::FAILED_CRC;
  }

  // Last step: the library only becomes valid once its first bytes are in place.
  if (esp_partition_write(this->partition_, REGION_OFFSET, head.get(), head_bytes) != ESP_OK) {
    return Status::FAILED_FLASH;
  }
  return Status::IDLE;
}

}  // namespace elekstube_ips
}  // namespace esphome
