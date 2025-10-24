#ifndef DBMS_STORAGE_BUFFER_PAGE_GUARD_H_
#define DBMS_STORAGE_BUFFER_PAGE_GUARD_H_

/**
 * @file page_guard.h
 * @brief RAII 封装：获取页数据并在析构时自动 Unpin。
 *
 * 用法建议：
 *  - 由 BufferPoolManager 暴露的“RAII 版 Fetch”接口创建 PageGuard 最为安全；
 *    当前版本保留 PageGuard 类型与友元关系，便于后续无缝扩展。
 */

#include <cstdint>
#include <utility>

#include "dbms/storage/storage_types.h"

namespace dbms {
namespace storage {

class BufferPoolManager;

class PageGuard {
public:
  PageGuard() = default;
  ~PageGuard();

  PageGuard(const PageGuard&) = delete;
  PageGuard& operator=(const PageGuard&) = delete;

  PageGuard(PageGuard&& other) noexcept { MoveFrom(std::move(other)); }
  PageGuard& operator=(PageGuard&& other) noexcept {
    if (this != &other) { Release(); MoveFrom(std::move(other)); }
    return *this;
  }

  bool           Valid()  const noexcept { return bpm_ != nullptr; }
  std::uint8_t*  Data()   const noexcept { return data_; }
  page_id_t      PageId() const noexcept { return pid_; }

  /// 标记为脏；析构/Release 时将把 dirty 标志传给 BPM
  void MarkDirty() noexcept { dirty_ = true; }

  /// 及早释放（否则在析构时释放）；释放后本 Guard 变为无效
  void Release();

private:
  friend class BufferPoolManager;
  PageGuard(BufferPoolManager* bpm, page_id_t pid, int fid, std::uint8_t* data)
      : bpm_(bpm), pid_(pid), fid_(fid), data_(data) {}

  void MoveFrom(PageGuard&& o) noexcept {
    bpm_ = o.bpm_;  pid_ = o.pid_;  fid_ = o.fid_;  data_ = o.data_;  dirty_ = o.dirty_;
    o.bpm_ = nullptr; o.pid_ = kInvalidPageId; o.fid_ = -1; o.data_ = nullptr; o.dirty_ = false;
  }

private:
  BufferPoolManager* bpm_{nullptr};
  page_id_t          pid_{kInvalidPageId};
  int                fid_{-1};
  std::uint8_t*      data_{nullptr};
  bool               dirty_{false};
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_BUFFER_PAGE_GUARD_H_
