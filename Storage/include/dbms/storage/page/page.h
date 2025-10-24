#ifndef DBMS_STORAGE_PAGE_PAGE_H_
#define DBMS_STORAGE_PAGE_PAGE_H_

/**
 * @file page.h
 * @brief 公共页头定义（稳定对外），隐藏具体页内布局与算法。
 *
 * 注：槽位页（Slotted Page）的内部布局与算法在 internal/page/slotted_page_layout.h。
 */

#include <cstdint>
#include "dbms/storage/storage_types.h"

namespace dbms {
namespace storage {

/**
 * @brief 每个磁盘页开头的通用头部。
 *
 * 字段：
 *  - page_id       : 逻辑页号（位于某段内）
 *  - page_lsn      : WAL/ARIES 恢复用的页 LSN（占位，后续由恢复模块维护）
 *  - slot_count    : 槽位数量（堆页有意义，索引页可自定义含义）
 *  - free_off      : 连续空闲区起始偏移（从页首起算）
 *  - free_size     : 连续空闲区大小（字节）
 *  - checksum      : 可选页校验（0 表示未启用）
 *  - format_version: 页格式版本（兼容性检查）
 */
struct PageHeader {
  page_id_t page_id{ kInvalidPageId };
  uint64_t  page_lsn{ 0 };
  uint16_t  slot_count{ 0 };
  uint16_t  free_off{ static_cast<uint16_t>(sizeof(PageHeader)) };
  uint16_t  free_size{ 0 };
  uint32_t  checksum{ 0 };
  uint32_t  format_version{ kPageFormatVersion };
};

// 尽量保持紧凑（有助于页内布局与缓存友好）
static_assert(sizeof(PageHeader) <= 64, "PageHeader should remain compact (<64 bytes).");

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_PAGE_PAGE_H_
