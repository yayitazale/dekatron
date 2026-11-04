#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_partition.h>

namespace esphome {
namespace elekstube_ips {

// The partition starts with the face pack; the clip library takes the rest.
static constexpr uint32_t FACE_REGION_BYTES = 0x40000;

struct ClipInfo {
  uint32_t frames;
  uint16_t fps;
  uint8_t images_per_frame;
};

// Clips stored in flash in the format written by tools/make_clips.py. The header goes last when
// downloading, so a library that is cut short or damaged never validates.
class ClipLibrary {
 public:
  void attach(const esp_partition_t *partition, const uint8_t *mapped) {
    this->partition_ = partition;
    this->mapped_ = mapped;
  }
  void load();

  bool ready() const { return this->ready_.load(); }
  bool find(const char *name, uint8_t *index) const;
  ClipInfo info(uint8_t index) const;
  void image(uint8_t index, uint32_t frame, uint8_t image, const uint8_t **data, uint32_t *size) const;

  // Replaces the library in the background. Returns false if a download is already running.
  bool start_download(const char *url);
  std::string state() const;

 protected:
  enum class Status : uint8_t { IDLE, DOWNLOADING, FAILED_HTTP, FAILED_FORMAT, FAILED_FLASH, FAILED_CRC };

  static void download_entry_(void *arg);
  void download_();
  Status run_download_();

  const esp_partition_t *partition_{nullptr};
  const uint8_t *mapped_{nullptr};
  const uint8_t *base_{nullptr};
  std::atomic<bool> ready_{false};
  std::atomic<uint32_t> clip_count_{0};
  std::atomic<Status> status_{Status::IDLE};
  std::atomic<uint8_t> percent_{0};
  std::string url_;
};

}  // namespace elekstube_ips
}  // namespace esphome
