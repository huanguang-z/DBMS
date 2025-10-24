#ifndef DBMS_STORAGE_TABLE_TABLE_ITERATOR_H_
#define DBMS_STORAGE_TABLE_TABLE_ITERATOR_H_

/**
 * @file table_iterator.h
 * @brief 堆表顺序扫描迭代器：跨页/跨槽位，自动跳过 tombstone。
 *
 * 资源策略：
 *  - operator* 返回“值类型快照”（Tuple 拷贝），不依赖页 pin 的生命周期。
 */

#include <cstdint>

#include "dbms/storage/storage_types.h"
#include "dbms/storage/record/tuple.h"

namespace dbms {
namespace storage {

class TableHeap;

class TableIterator {
public:
  struct Row {
    RID   rid;
    Tuple tuple;
  };

  TableIterator() : table_(nullptr), pid_(0), slot_(0), end_(true) {}
  explicit TableIterator(const TableHeap* table);

  bool IsEnd() const noexcept { return end_; }

  const Row& operator*()  const { return current_; }
  const Row* operator->() const { return &current_; }

  TableIterator& operator++();

  bool operator==(const TableIterator& rhs) const {
    if (end_ && rhs.end_) return true;
    return table_ == rhs.table_ && pid_ == rhs.pid_ && slot_ == rhs.slot_ && end_ == rhs.end_;
  }
  bool operator!=(const TableIterator& rhs) const { return !(*this == rhs); }

private:
  void SeekFirst();                       // 定位到第一个有效记录
  bool LoadAt(page_id_t pid, uint16_t slot);  // 读取 (pid,slot)，填充 current_
  bool AdvanceOne();                      // 向下一个有效记录推进

private:
  const TableHeap* table_{nullptr};
  page_id_t  pid_{0};
  uint16_t   slot_{0};
  bool       end_{true};
  Row        current_{};
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_TABLE_TABLE_ITERATOR_H_
