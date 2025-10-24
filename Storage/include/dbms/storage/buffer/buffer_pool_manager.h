#ifndef DBMS_STORAGE_BUFFER_BUFFER_POOL_MANAGER_H_
#define DBMS_STORAGE_BUFFER_BUFFER_POOL_MANAGER_H_

/**
 * @file buffer_pool_manager.h
 * @brief 缓冲池管理器：页的装入/固定/解固定/刷盘；对接替换器。
 *
 * 线程模型：
 *  - 内部用一把互斥锁保护 page table / free list / 统计；
 *  - 页级并发（shared_mutex）在 frame 中（实现细节），推荐由上层按需配合使用；
 *  - 提供与 Recovery 对接的“刷盘前回调”接口。
 */

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "dbms/storage/storage_types.h"
#include "dbms/storage/page/page.h"
#include "dbms/storage/io/disk_manager.h"
#include "dbms/storage/buffer/replacer.h"

namespace dbms {
namespace storage {

class PageGuard;  // 前置声明

struct BufferStats {
  uint64_t hits{0};
  uint64_t misses{0};
  uint64_t evictions{0};
  uint64_t flushes{0};
};

class BufferPoolManager {
public:
  BufferPoolManager(int num_frames,
                    uint32_t page_size,
                    DiskManager* disk,
                    std::unique_ptr<IReplacer> replacer);

  ~BufferPoolManager();

  BufferPoolManager(const BufferPoolManager&) = delete;
  BufferPoolManager& operator=(const BufferPoolManager&) = delete;

  // ----------------- 数据页操作 -----------------

  /**
   * @brief 将页装入内存并固定，返回页内字节缓冲区指针（长度=page_size）。
   *        调用方应在使用完成后尽快调用 UnpinPage()。
   */
  Status FetchPage(page_id_t pid, std::uint8_t** out_data);

  /**
   * @brief 分配一个新页并返回其缓冲区（已置零）。page_id 由文件长度推导。
   *        （当接入 SegmentManager 后，上层更推荐用 Segment->AllocatePage()）
   */
  Status NewPage(page_id_t* out_pid, std::uint8_t** out_data);

  /**
   * @brief 解固定页；当 pin_count 归 0，替换器可将其作为受害者。
   * @param is_dirty 若为 true，标记该页脏（Flush 时会写回）
   */
  Status UnpinPage(page_id_t pid, bool is_dirty);

  /// 立即将页写回磁盘（若在缓冲池中且为脏）
  Status FlushPage(page_id_t pid);

  /// 将所有脏页刷盘
  void   FlushAll();

  // ----------------- 统计与配置 -----------------

  BufferStats GetStats() const;
  uint32_t    page_size() const noexcept { return page_size_; }
  int         num_frames() const noexcept { return num_frames_; }

  /**
   * @brief 注册刷盘回调：在真正写盘前调用（用于 WAL 对齐）。
   *        回调参数：page_id, page_lsn（来源于 PageHeader）。
   */
  void RegisterFlushCallback(std::function<void(page_id_t, uint64_t)> cb);

private:
  friend class PageGuard;

  // 内部辅助
  struct Impl;
  std::unique_ptr<Impl> p_;  // Pimpl：隐藏具体数据结构以稳定头文件

  // 非接口的内部方法（给 PageGuard 使用）
  frame_id_t LookupFrame(page_id_t pid) const;
  frame_id_t AcquireFrameFor(page_id_t pid, page_id_t* evicted_pid);
  bool       FlushFrame(frame_id_t fid);
  Status     UnpinFrame(frame_id_t fid, bool is_dirty);

private:
  const int      num_frames_;
  const uint32_t page_size_;
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_BUFFER_BUFFER_POOL_MANAGER_H_
