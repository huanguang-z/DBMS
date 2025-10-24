#ifndef DBMS_STORAGE_INTERNAL_BUFFER_PAGE_TABLE_H_
#define DBMS_STORAGE_INTERNAL_BUFFER_PAGE_TABLE_H_

/**
 * @file page_table.h
 * @brief 简单的 page_id → frame_id 映射（hash map）。
 */

#include <unordered_map>

#include "dbms/storage/storage_types.h"
#include "dbms/storage/buffer/replacer.h"  // for frame_id_t

namespace dbms {
namespace storage {

class PageTable {
public:
  bool Lookup(page_id_t pid, frame_id_t* out_fid) const {
    auto it = map_.find(pid);
    if (it == map_.end()) return false;
    *out_fid = it->second;
    return true;
  }

  void   Insert(page_id_t pid, frame_id_t fid) { map_[pid] = fid; }
  void   Erase(page_id_t pid) { map_.erase(pid); }
  void   Clear() { map_.clear(); }
  size_t Size() const { return map_.size(); }

private:
  std::unordered_map<page_id_t, frame_id_t> map_;
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_INTERNAL_BUFFER_PAGE_TABLE_H_
