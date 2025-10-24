#ifndef DBMS_STORAGE_INTERNAL_BUFFER_LRUK_REPLACER_H_
#define DBMS_STORAGE_INTERNAL_BUFFER_LRUK_REPLACER_H_

/**
 * @file lruk_replacer.h
 * @brief 简化版 LRU-K (K=2 默认)：记录最近两次访问时间戳，选择“最不常用”的未固定帧。
 *
 * 注意：教学用途的轻量实现；追求可读性与可比性而非极致性能。
 */

#include <chrono>
#include <vector>

#include "dbms/storage/buffer/replacer.h"

namespace dbms {
namespace storage {

class LruKReplacer final : public IReplacer {
public:
  explicit LruKReplacer(int capacity, int k = 2);

  void Pin(frame_id_t fid) override;
  void Unpin(frame_id_t fid) override;
  bool Victim(frame_id_t* out) override;
  int  Size() const override;

private:
  using Clock = std::chrono::steady_clock;
  using Time  = Clock::time_point;

  struct Entry {
    bool   present{false};   // 是否在候选集
    int    k{2};             // K 值（目前只用到 2）
    Time   last1{};          // 最近一次访问
    Time   last2{};          // 倒数第二次访问（未满时为 epoch）
  };

  void Touch(frame_id_t fid);

private:
  std::vector<Entry> entries_;
  int                cap_{0};
  int                k_{2};
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_INTERNAL_BUFFER_LRUK_REPLACER_H_
