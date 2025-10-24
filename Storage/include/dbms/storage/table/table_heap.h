#ifndef DBMS_STORAGE_TABLE_TABLE_HEAP_H_
#define DBMS_STORAGE_TABLE_TABLE_HEAP_H_

/**
 * @file table_heap.h
 * @brief 行存“堆表”接口：插入、更新、删除、读取、顺序扫描。
 *
 * 约定：
 *  - 一个表对应一个段（seg_id_t），由 SegmentManager 分配/回收页；
 *  - 记录以 SlottedPage 写入页内，RID=(page_id, slot)；
 *  - 每次页内变更后，用 FSM.Update(pid, free_size) 维护空闲空间信息。
 */

#include <cstdint>
#include <memory>

#include "dbms/storage/storage_types.h"
#include "dbms/storage/record/tuple.h"
#include "dbms/storage/buffer/buffer_pool_manager.h"
#include "dbms/storage/space/free_space_manager.h"
#include "dbms/storage/segment/segment_manager.h"

namespace dbms {
namespace storage {

class TableIterator;  // 前置

class TableHeap {
public:
  TableHeap(seg_id_t seg_id,
            uint32_t page_size,
            BufferPoolManager* bpm,
            FreeSpaceManager* fsm,
            SegmentManager* sm)
      : seg_id_(seg_id), page_size_(page_size), bpm_(bpm), fsm_(fsm), sm_(sm) {}

  TableHeap(const TableHeap&) = delete;
  TableHeap& operator=(const TableHeap&) = delete;

  // ---- DML ----
  Status Insert(const Tuple& t, RID* out);
  Status Update(const RID& rid, const Tuple& t);
  Status Erase (const RID& rid);
  Status Get   (const RID& rid, Tuple* out) const;

  // ---- 扫描 ----
  TableIterator Begin() const;
  TableIterator End()   const;

  // ---- 访问器 ----
  seg_id_t  segment_id() const noexcept { return seg_id_; }
  uint32_t  page_size()  const noexcept { return page_size_; }

private:
  void UpdateFsmForPage(page_id_t pid, std::uint8_t* page);

private:
  seg_id_t            seg_id_{kInvalidSegId};
  uint32_t            page_size_{0};
  BufferPoolManager*  bpm_{nullptr};
  FreeSpaceManager*   fsm_{nullptr};
  SegmentManager*     sm_{nullptr};

  friend class TableIterator;  // 迭代器访问 bpm_/sm_/page_size_/seg_id_
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_TABLE_TABLE_HEAP_H_
