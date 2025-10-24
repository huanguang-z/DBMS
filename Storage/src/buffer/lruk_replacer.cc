#include "internal/buffer/lruk_replacer.h"

#include <limits>

namespace dbms {
namespace storage {

LruKReplacer::LruKReplacer(int capacity, int k) : entries_(capacity), cap_(capacity), k_(k) {
  for (auto& e : entries_) e.k = k_;
}

void LruKReplacer::Pin(frame_id_t fid) {
  if (fid < 0 || fid >= cap_) return;
  entries_[fid].present = false;
}

void LruKReplacer::Unpin(frame_id_t fid) {
  if (fid < 0 || fid >= cap_) return;
  entries_[fid].present = true;
  Touch(fid);
}

bool LruKReplacer::Victim(frame_id_t* out) {
  if (!out) return false;

  auto worst = Clock::now();
  bool found = false;
  frame_id_t fid_sel = -1;

  for (frame_id_t i = 0; i < cap_; ++i) {
    auto& e = entries_[i];
    if (!e.present) continue;

    // 取 K=2 的“倒数第二次访问时间”为比较依据；若没有，则退化为 last1。
    auto key = (e.last2.time_since_epoch().count() != 0) ? e.last2 : e.last1;
    if (!found || key < worst) {
      found = true;
      worst = key;
      fid_sel = i;
    }
  }

  if (!found) return false;
  entries_[fid_sel].present = false;
  *out = fid_sel;
  return true;
}

int LruKReplacer::Size() const {
  int s = 0;
  for (const auto& e : entries_) s += e.present ? 1 : 0;
  return s;
}

void LruKReplacer::Touch(frame_id_t fid) {
  auto now = Clock::now();
  auto& e = entries_[fid];
  e.last2 = e.last1;
  e.last1 = now;
}

}  // namespace storage
}  // namespace dbms
