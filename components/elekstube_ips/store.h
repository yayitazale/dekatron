#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <esp_partition.h>

namespace esphome {
namespace elekstube_ips {

// The store is one file in the data partition, in the format written by tools/opentube_store.py. It
// holds the clock faces and the clips. The header goes last when writing, so a store that is cut
// short or damaged never validates.

// A scene stores what changes between frames instead of whole images, see tools/make_scenes.py.
// Its table holds the palette, then one record per frame, then the pixel blobs.
static constexpr uint8_t SCENE_IMAGES = 0;
static constexpr uint32_t SCENE_PALETTE_BYTES = 512;
// A clock face is ten JPEG images, one per digit.
static constexpr uint8_t FACE_IMAGES = 10;

enum class EntryKind : uint8_t { CLIP = 0, FACE = 1 };

struct ClipInfo {
  uint32_t frames;
  uint16_t fps;
  uint8_t images_per_frame;  // SCENE_IMAGES for a scene
  uint16_t scene_blobs;
};

struct __attribute__((packed)) ScenePatch {
  uint8_t tube;
  uint8_t x;
  uint8_t y;
  uint8_t width;
  uint8_t height;
  uint8_t padding;
  uint16_t blob;
};

class Store {
 public:
  enum class Status : uint8_t {
    IDLE,
    DOWNLOADING,
    WRITING,
    FAILED_HTTP,
    FAILED_FORMAT,
    FAILED_FLASH,
    FAILED_CRC,
    FAILED_BUSY,
  };

  void attach(const esp_partition_t *partition, const uint8_t *mapped) {
    this->partition_ = partition;
    this->mapped_ = mapped;
  }
  void load();

  bool ready() const { return this->ready_.load(); }
  uint32_t entry_count() const { return this->entry_count_.load(); }
  EntryKind kind(uint8_t entry) const;
  void name(uint8_t entry, char *buffer) const;  // buffer holds at least NAME_BYTES + 1 characters
  bool find(const char *name, EntryKind kind, uint8_t *entry) const;
  // The nth entry of a kind, counting from 0.
  bool nth(EntryKind kind, uint8_t nth, uint8_t *entry) const;
  uint8_t count(EntryKind kind) const;
  ClipInfo info(uint8_t entry) const;
  void image(uint8_t entry, uint32_t frame, uint8_t image, const uint8_t **data, uint32_t *size) const;
  void table_entry(uint8_t entry, uint32_t position, const uint8_t **data, uint32_t *size) const;

  // The valid store as it is in flash, or nullptr and 0 when there is none.
  const uint8_t *data() const { return this->ready() ? this->base_ : nullptr; }
  uint32_t size() const { return this->ready() ? this->size_ : 0; }
  uint32_t capacity() const { return this->partition_ != nullptr ? this->partition_->size : 0; }

  // Writes a new store, which replaces the current one. The bytes arrive in order, in any chunks.
  // Nothing is erased until the first bytes look like a store, so a wrong file keeps the current one.
  bool begin_write();
  Status write(const uint8_t *data, size_t length);
  Status finish_write();
  void abort_write(Status reason);

  // Replaces the store in the background with a file from an HTTP address.
  bool start_download(const char *url);
  Status status() const { return this->status_.load(); }
  uint8_t percent() const { return this->percent_.load(); }
  std::string state() const;

  static constexpr size_t NAME_BYTES = 16;

 protected:
  static void download_entry_(void *arg);
  void download_();
  Status run_download_();

  const esp_partition_t *partition_{nullptr};
  const uint8_t *mapped_{nullptr};
  const uint8_t *base_{nullptr};
  uint32_t size_{0};
  std::atomic<bool> ready_{false};
  std::atomic<uint32_t> entry_count_{0};
  std::atomic<Status> status_{Status::IDLE};
  std::atomic<uint8_t> percent_{0};
  std::atomic<uint32_t> last_write_ms_{0};
  std::string url_;

  // The state of a write in progress, owned by whoever started it.
  struct Writer {
    std::unique_ptr<uint8_t[]> head;  // the first bytes, written last
    uint32_t received{0};
    uint32_t total{0};
    uint32_t crc{0};
    uint32_t expected_crc{0};
    bool erased{false};
    uint32_t erase_ms{0};    // how long the flash was busy, kept to see where an upload spends its time
    uint32_t write_us{0};
  };
  std::unique_ptr<Writer> writer_;
};

}  // namespace elekstube_ips
}  // namespace esphome
