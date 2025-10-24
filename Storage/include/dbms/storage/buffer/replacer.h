#ifndef DBMS_STORAGE_BUFFER_REPLACER_H_
#define DBMS_STORAGE_BUFFER_REPLACER_H_

/**
 * @file replacer.h
 * @brief 替换策略公共接口（允许 CLOCK / LRU-K 等可插拔实现）。
 *
 * 约定：
 *  - Pin(fid)   ：从候选集移出（不可被淘汰）；
 *  - Unpin(fid) ：加入候选集（pin_count==0）；
 *  - Victim(out)：从候选集中选择一个 frame（策略自定），成功返回 true。
 */

#include <cstdint>

namespace dbms {
namespace storage {

using frame_id_t = int;

struct IReplacer {
  virtual ~IReplacer() = default;

  virtual void Pin(frame_id_t fid)   = 0;
  virtual void Unpin(frame_id_t fid) = 0;

  /// 选择受害者 frame（成功返回 true，并在 *out 写入 frame_id）
  virtual bool Victim(frame_id_t* out) = 0;

  /// 候选集大小（调试/统计用）
  virtual int  Size() const = 0;
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_BUFFER_REPLACER_H_
