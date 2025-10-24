#ifndef DBMS_STORAGE_SEGMENT_SEGMENT_MANAGER_H_
#define DBMS_STORAGE_SEGMENT_SEGMENT_MANAGER_H_

/**
 * @file segment_manager.h
 * @brief 段管理：一个段对应一个文件；负责页的分配/回收与基本元数据。
 *
 * 职责：
 *  - seg_id → 文件（DiskManager）映射与生命周期；
 *  - AllocatePage/FreePage：段内页的编号管理（优先复用空闲栈，否则扩容）；
 *  - PageCount(seg)、ProbePageFree(seg,pid)：便于 FSM 重建；
 *
 * 线程安全：对外方法内部加锁（std::mutex）。
 */

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "dbms/storage/storage_types.h"
#include "dbms/storage/page/page.h"
#include "dbms/storage/io/disk_manager.h"

namespace dbms {
namespace storage {

class SegmentManager {
public:
  /**
   * @param page_size  页大小（字节）
   * @param base_dir   段文件所在目录（需已存在且可写）
   */
  SegmentManager(uint32_t page_size, std::string base_dir);

  SegmentManager(const SegmentManager&) = delete;
  SegmentManager& operator=(const SegmentManager&) = delete;

  ~SegmentManager();

  /// 确保段已存在（若文件不存在则创建）
  Status      EnsureSegment(seg_id_t seg);

  /// 段文件路径（展示/调试）
  std::string SegmentPath(seg_id_t seg) const;

  // ---- 页分配 / 回收 ----
  page_id_t   AllocatePage(seg_id_t seg);         ///< 失败时返回 kInvalidPageId
  void        FreePage(seg_id_t seg, page_id_t);  ///< 简单放回空闲栈，不收缩文件

  // ---- 查询 / 探测 ----
  uint64_t    PageCount(seg_id_t seg) const;      ///< 文件可寻址页数
  uint16_t    ProbePageFree(seg_id_t seg, page_id_t pid) const;  ///< 读取 PageHeader.free_size

  // ---- 访问器 ----
  DiskManager* GetDisk(seg_id_t seg);
  uint32_t     page_size() const noexcept { return page_size_; }
  const std::string& base_dir() const noexcept { return base_dir_; }

private:
  struct Segment {
    std::unique_ptr<DiskManager> disk;     // 段文件
    std::vector<page_id_t>       free_list; // 空闲页栈（后进先出）
  };

  std::string MakePath(seg_id_t seg) const;

private:
  uint32_t    page_size_{0};
  std::string base_dir_;

  mutable std::mutex                 mu_;
  std::unordered_map<seg_id_t, Segment> segs_;
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_SEGMENT_SEGMENT_MANAGER_H_
