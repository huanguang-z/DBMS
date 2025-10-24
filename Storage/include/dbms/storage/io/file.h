#ifndef DBMS_STORAGE_IO_FILE_H_
#define DBMS_STORAGE_IO_FILE_H_

/**
 * @file file.h
 * @brief 轻量 POSIX 文件封装（RAII）：最小读写/扩容/同步能力。
 *
 * 统一错误返回 Status，不抛异常；支持 64 位文件偏移（_FILE_OFFSET_BITS=64）。
 */

#include <cstdint>
#include <string>

#include "dbms/storage/storage_types.h"

namespace dbms {
namespace storage {

class File {
public:
  File() = default;
  explicit File(std::string path) : path_(std::move(path)) {}
  ~File();

  File(const File&) = delete;
  File& operator=(const File&) = delete;

  File(File&& other) noexcept { MoveFrom(std::move(other)); }
  File& operator=(File&& other) noexcept {
    if (this != &other) { Close(); MoveFrom(std::move(other)); }
    return *this;
  }

  /// 以读写模式打开；不存在时可选择创建
  Status Open(bool create_if_missing = true);

  /// 关闭（幂等）
  void   Close();

  /// 是否有效打开
  bool   Valid() const noexcept { return fd_ >= 0; }

  /// 文件大小（字节）
  uint64_t SizeBytes() const;

  /// 调整文件大小（字节）（扩展/截断）
  Status Resize(uint64_t new_size);

  /// 写入 n 字节到 offset（保证写满或报错）
  Status WriteAt(const void* buf, size_t n, uint64_t offset);

  /// 从 offset 读取 n 字节（保证读满；越界返回 NotFound/Corruption）
  Status ReadAt(void* buf, size_t n, uint64_t offset) const;

  /// 刷新到稳定存储
  Status Sync() const;

  const std::string& path() const noexcept { return path_; }
  int  fd() const noexcept { return fd_; }

private:
  void MoveFrom(File&& o) noexcept {
    fd_ = o.fd_; path_ = std::move(o.path_);
    o.fd_ = -1;  o.path_.clear();
  }

private:
  int         fd_{-1};
  std::string path_;
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_IO_FILE_H_
