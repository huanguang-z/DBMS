#include "internal/buffer/clock_replacer.h"

namespace dbms {
namespace storage {

ClockReplacer::ClockReplacer(int capacity)
    : present_(capacity, 0), ref_(capacity, 0), cap_(capacity) {}

void ClockReplacer::Pin(frame_id_t fid) {
  if (fid < 0 || fid >= cap_) return;
  present_[fid] = 0;
  ref_[fid] = 0;
}

void ClockReplacer::Unpin(frame_id_t fid) {
  if (fid < 0 || fid >= cap_) return;
  present_[fid] = 1;
  ref_[fid] = 1;  // 新近释放，给一次“保留”
}

bool ClockReplacer::Victim(frame_id_t* out) {
  if (!out) return false;
  if (cap_ == 0) return false;

  // 防止极端死循环：扫描上限 2 * cap_
  int scanned = 0;
  const int limit = cap_ * 2;
  while (scanned < limit) {
    if (present_[hand_]) {
      if (ref_[hand_] == 0) {
        *out = hand_;
        present_[hand_] = 0;
        ref_[hand_] = 0;
        hand_ = (hand_ + 1) % cap_;
        return true;
      } else {
        ref_[hand_] = 0;
      }
    }
    hand_ = (hand_ + 1) % cap_;
    ++scanned;
  }
  return false;
}

int ClockReplacer::Size() const {
  int s = 0;
  for (int i = 0; i < cap_; ++i) s += present_[i] ? 1 : 0;
  return s;
}

}  // namespace storage
}  // namespace dbms
