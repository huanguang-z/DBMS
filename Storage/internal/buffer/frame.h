#ifndef DBMS_STORAGE_INTERNAL_BUFFER_FRAME_H_
#define DBMS_STORAGE_INTERNAL_BUFFER_FRAME_H_

/**
 * @file frame.h
 * @brief 缓冲帧元数据：pin_count / dirty / page_id / 页级读写锁。
 *
 * 数据区由 BufferPoolManager 管理为一整块连续内存，每个 frame 拿一个切片。
 */

#include <cstdint>
#include <shared_mutex>

#include "dbms/storage/storage_types.h"

namespace dbms {
namespace storage {

struct Frame {
  page_id_t         page_id{ kInvalidPageId };
  int               pin_count{ 0 };
  bool              dirty{ false };

  // 每帧的页级读写锁（物理保护，非事务锁）
  mutable std::shared_mutex latch;

  // 指向该 frame 的页内存起始（长度 = page_size）
  std::uint8_t*     data{ nullptr };

  void Reset(std::uint8_t* p) {
    page_id   = kInvalidPageId;
    pin_count = 0;
    dirty     = false;
    data      = p;
  }
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_INTERNAL_BUFFER_FRAME_H_
