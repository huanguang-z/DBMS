#ifndef DBMS_STORAGE_SPACE_FREE_SPACE_MANAGER_H_
#define DBMS_STORAGE_SPACE_FREE_SPACE_MANAGER_H_

/**
 * @file free_space_manager.h
 * @brief 空闲空间管理器（FSM）：按“剩余空间”分桶管理页，快速定位可插入页。
 *
 * 桶定义：
 *  thresholds_ = {t0, t1, ..., tN-1}（严格递增）
 *   Bin0: [0, t0)
 *   Bin1: [t0, t1)
 *   ...
 *   BinN: [tN-1, +∞)  （实现中以 page_size 截断）
 *
 * 线程安全：内部用互斥锁保护所有状态；Find/Update/Remove/重建均为线程安全。
 */

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dbms/storage/storage_types.h"

namespace dbms {
namespace storage {

class FreeSpaceManager {
public:
  /**
   * @param page_size   页大小（字节）
   * @param thresholds  桶阈值（严格递增），例如 {128,512,1024,2048,4096,8192}
   */
  FreeSpaceManager(uint32_t page_size, std::vector<uint32_t> thresholds);

  FreeSpaceManager(const FreeSpaceManager&) = delete;
  FreeSpaceManager& operator=(const FreeSpaceManager&) = delete;

  /**
   * @brief 查找一个可容纳 need_bytes 的页（任意一个即可）。
   * @return 命中返回 pid；否则返回 kInvalidPageId。
   */
  page_id_t Find(uint16_t need_bytes) const;

  /**
   * @brief 插入/更新某页的空闲空间（自动在桶间迁移）。
   * @param pid         页号
   * @param free_bytes  该页连续空闲空间大小（字节）
   */
  void Update(page_id_t pid, uint16_t free_bytes);

  /// 从 FSM 中移除某页（例如页被释放）
  void Remove(page_id_t pid);

  // ---------- 与段管理的低耦合重建支持 ----------
  using FreeProbeFn  = std::function<uint16_t(seg_id_t /*seg*/, page_id_t /*pid*/)>;
  using PageCountFn  = std::function<uint64_t(seg_id_t /*seg*/)>;

  /// Integration/Segment 层在启动后注入
  void RegisterSegmentProbe(FreeProbeFn free_probe, PageCountFn page_count);

  /// 全量扫描某段并重建 FSM；未注册回调返回 Unavailable
  Status RebuildFromSegment(seg_id_t seg);

  // ---------- 观测 ----------
  std::vector<size_t>   BinSizes() const;        ///< 每个桶的 pid 数量
  std::vector<uint32_t> BinThresholds() const;   ///< 桶阈值快照
  size_t                TotalTrackedPages() const;

private:
  size_t BinIndex(uint32_t free_bytes) const;

private:
  uint32_t page_size_{0};
  std::vector<uint32_t> thresholds_;                       // 桶阈值
  std::vector<std::unordered_set<page_id_t>> bins_;        // 每桶的 pid 集合

  std::unordered_map<page_id_t, size_t>   pid2bin_;        // pid -> bin
  std::unordered_map<page_id_t, uint16_t> pid2free_;       // pid -> free

  FreeProbeFn  probe_free_;
  PageCountFn  probe_count_;

  mutable std::mutex mu_;
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_SPACE_FREE_SPACE_MANAGER_H_
