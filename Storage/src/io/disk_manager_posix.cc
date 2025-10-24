/**
 * @file disk_manager_posix.cc
 * @brief DiskManager 的 POSIX 实现（Ubuntu 22.04）。
 */

#include "dbms/storage/io/disk_manager.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace dbms {
namespace storage {

// ======== 内部辅助 ========

static std::string ErrnoMessage(const char* op, const std::string& path) {
  return std::string(op) + "('" + path + "'): " + std::strerror(errno);
}

// ======== File 实现（RAII POSIX 包装） ========

File::~File() { Close(); }

Status File::Open(bool create_if_missing) {
  if (Valid()) return Status::OK();
  int flags = O_RDWR | (create_if_missing ? O_CREAT : 0);
  int fd = ::open(path_.c_str(), flags, 0644);
  if (fd < 0) {
    return Status::IOError(ErrnoMessage("open", path_));
  }
  fd_ = fd;
  return Status::OK();
}

void File::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

uint64_t File::SizeBytes() const {
  if (fd_ < 0) return 0;
  struct stat st;
  if (::fstat(fd_, &st) != 0) return 0;
  return static_cast<uint64_t>(st.st_size);
}

Status File::Resize(uint64_t new_size) {
  if (fd_ < 0) return Status::IOError(ErrnoMessage("open", path_));
  if (::ftruncate(fd_, static_cast<off_t>(new_size)) != 0) {
    return Status::IOError(ErrnoMessage("ftruncate", path_));
  }
  return Status::OK();
}

Status File::WriteAt(const void* buf, size_t n, uint64_t offset) {
  if (fd_ < 0) return Status::IOError(ErrnoMessage("open", path_));
  if (!buf && n > 0) return Status::InvalidArgument("WriteAt: buf=null");
  const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(buf);
  size_t   remain = n;
  off_t    off = static_cast<off_t>(offset);
  while (remain > 0) {
    ssize_t w = ::pwrite(fd_, p, remain, off);
    if (w < 0) {
      if (errno == EINTR) continue;
      return Status::IOError(ErrnoMessage("pwrite", path_));
    }
    p      += w;
    remain -= static_cast<size_t>(w);
    off    += w;
  }
  return Status::OK();
}

Status File::ReadAt(void* buf, size_t n, uint64_t offset) const {
  if (fd_ < 0) return Status::IOError(ErrnoMessage("open", path_));
  if (!buf && n > 0) return Status::InvalidArgument("ReadAt: buf=null");

  // 边界检查：若请求越过 EOF，直接返回 NotFound
  const uint64_t size = SizeBytes();
  if (offset + n > size) {
    return Status::NotFound("ReadAt: range beyond EOF");
  }

  std::uint8_t* p = reinterpret_cast<std::uint8_t*>(buf);
  size_t   remain = n;
  off_t    off = static_cast<off_t>(offset);
  while (remain > 0) {
    ssize_t r = ::pread(fd_, p, remain, off);
    if (r == 0) {
      return Status::Corruption("ReadAt: unexpected EOF");
    }
    if (r < 0) {
      if (errno == EINTR) continue;
      return Status::IOError(ErrnoMessage("pread", path_));
    }
    p      += r;
    remain -= static_cast<size_t>(r);
    off    += r;
  }
  return Status::OK();
}

Status File::Sync() const {
  if (fd_ < 0) return Status::IOError(ErrnoMessage("open", path_));
  if (::fdatasync(fd_) != 0) {
    return Status::IOError(ErrnoMessage("fdatasync", path_));
  }
  return Status::OK();
}

// ======== DiskManager 实现 ========

DiskManager::DiskManager(std::string file_path, uint32_t page_size)
    : file_(std::move(file_path)),
      page_size_(page_size < sizeof(PageHeader) ? kDefaultPageSize : page_size) {
  (void)file_.Open(/*create_if_missing=*/true);
}

DiskManager::~DiskManager() = default;

Status DiskManager::EnsureCapacityFor(page_id_t pid) {
  const uint64_t need = (static_cast<uint64_t>(pid) + 1) * page_size_;
  const uint64_t cur  = file_.SizeBytes();
  if (cur >= need) return Status::OK();
  return file_.Resize(need);
}

Status DiskManager::ReadPage(page_id_t pid, void* out_buf) const {
  if (!out_buf) return Status::InvalidArgument("ReadPage: out_buf=null");
  const uint64_t off = static_cast<uint64_t>(pid) * page_size_;
  return file_.ReadAt(out_buf, page_size_, off);
}

Status DiskManager::WritePage(page_id_t pid, const void* in_buf) {
  if (!in_buf) return Status::InvalidArgument("WritePage: in_buf=null");
  if (Status s = EnsureCapacityFor(pid); !s.ok()) return s;
  const uint64_t off = static_cast<uint64_t>(pid) * page_size_;
  return file_.WriteAt(in_buf, page_size_, off);
}

Status DiskManager::Sync() const { return file_.Sync(); }

uint64_t DiskManager::PageCount() const {
  const uint64_t bytes = file_.SizeBytes();
  return bytes / page_size_;
}

Status DiskManager::ResizeToPages(uint64_t new_page_count) {
  return file_.Resize(new_page_count * static_cast<uint64_t>(page_size_));
}

}  // namespace storage
}  // namespace dbms
