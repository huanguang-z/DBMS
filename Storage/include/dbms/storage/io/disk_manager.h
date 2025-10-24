#ifndef DBMS_STORAGE_IO_DISK_MANAGER_H_
#define DBMS_STORAGE_IO_DISK_MANAGER_H_

/**
 * @file disk_manager.h
 * @brief 页级 I/O 管理：以固定页大小对文件进行读/写/扩容/同步。
 *
 * 语义：
 *  - ReadPage(pid) ：读取第 pid 页到 out_buf（page_size 字节）；
 *  - WritePage(pid)：将缓冲区写入第 pid 页；必要时扩容文件；
 *  - PageCount()   ：(file_size / page_size) 的向下取整。
 */

#include <cstdint>
#include <string>
#include <memory>
#include "dbms/storage/storage_types.h"
#include "dbms/storage/page/page.h"
#include "dbms/storage/io/file.h"

namespace dbms {
namespace storage {

class DiskManager {
public:
  DiskManager(std::string file_path, uint32_t page_size = kDefaultPageSize);

  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  ~DiskManager();

  // ---- 页级 ----
  Status ReadPage(page_id_t pid, void* out_buf) const;
  Status WritePage(page_id_t pid, const void* in_buf);

  // ---- 文件级 ----
  Status  Sync() const;
  uint64_t PageCount() const;
  Status  ResizeToPages(uint64_t new_page_count);

  // ---- 访问器 ----
  uint32_t page_size() const noexcept { return page_size_; }
  const std::string& file_path() const noexcept { return file_.path(); }

private:
  Status EnsureCapacityFor(page_id_t pid);  // 确保能写第 pid 页

private:
  File      file_;
  uint32_t  page_size_{kDefaultPageSize};
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_IO_DISK_MANAGER_H_
