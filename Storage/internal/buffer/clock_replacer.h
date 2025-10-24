#ifndef DBMS_STORAGE_INTERNAL_BUFFER_CLOCK_REPLACER_H_
#define DBMS_STORAGE_INTERNAL_BUFFER_CLOCK_REPLACER_H_

/**
 * @file clock_replacer.h
 * @brief CLOCK 策略：维护一圈“可替换帧”，用引用位决定是否跳过。
 *
 * 约定：
 *  - Pin(fid)：从候选集中移出，并清除引用位；
 *  - Unpin(fid)：加入候选集，设置引用位=1；
 *  - Victim(out)：顺时针扫描，遇到引用位=0 的候选帧即选择之；扫描时将 1→0。
 */

#include <vector>

#include "dbms/storage/buffer/replacer.h"

namespace dbms {
namespace storage {

class ClockReplacer final : public IReplacer {
public:
  explicit ClockReplacer(int capacity);

  void Pin(frame_id_t fid) override;
  void Unpin(frame_id_t fid) override;
  bool Victim(frame_id_t* out) override;
  int  Size() const override;

private:
  std::vector<char> present_;   // 是否在候选集
  std::vector<char> ref_;       // 引用位
  int               hand_{0};   // 时钟指针
  int               cap_{0};
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_INTERNAL_BUFFER_CLOCK_REPLACER_H_
