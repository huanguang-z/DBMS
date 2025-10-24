#ifndef DBMS_STORAGE_INTERNAL_PAGE_SLOTTED_PAGE_LAYOUT_H_
#define DBMS_STORAGE_INTERNAL_PAGE_SLOTTED_PAGE_LAYOUT_H_

/**
 * @file slotted_page_layout.h
 * @brief 槽位页（Slotted Page）布局与页内算法接口。
 *
 * 物理布局（自低地址到高地址）：
 *  [ PageHeader | ...record data growing up... | ...slot directory growing down... ]
 *
 * 关键约定：
 *  - 槽目录位于页尾，按 slot_id 递增排列；每个槽记录一个记录的 (offset,len)。
 *  - 删除：将槽标记为空（len==0），并在需要时进行紧凑化（可延后）。
 *  - Update：尝试原地覆盖；若空间不足则触发页内压缩；仍不足则返回 OutOfRange。
 *
 * 对外只暴露 SlottedPage 的方法；具体槽结构细节不泄漏到其他模块。
 */

#include <cstdint>
#include <cstring>

#include "dbms/storage/storage_types.h"
#include "dbms/storage/page/page.h"

namespace dbms {
namespace storage {

class SlottedPage {
public:
  /**
   * @brief 构造一个页适配器（不会初始化页头）。
   * @param page       指向页的首地址（长度不少于 page_size）
   * @param page_size  页大小（与磁盘/缓冲池一致）
   */
  SlottedPage(std::uint8_t* page, std::uint32_t page_size)
      : page_(page), page_size_(page_size) {}

  /**
   * @brief 初始化一张“全新”页（置 PageHeader 与空闲区）。
   */
  static void InitNew(std::uint8_t* page, page_id_t pid, std::uint32_t page_size);

  // ----------------- 基础操作 -----------------

  /**
   * @brief 插入一条记录，返回分配的 slot id。
   * @note  若空间不足将尝试一次页内压缩；仍不足则返回 OutOfRange。
   */
  Status Insert(const std::uint8_t* rec, std::uint16_t len, std::uint16_t* out_slot);

  /**
   * @brief 读取某槽位的记录内容指针与长度（零拷贝视图）。
   * @return 若槽空/被删/越界返回 NotFound。
   */
  Status Get(std::uint16_t slot, const std::uint8_t** out_ptr, std::uint16_t* out_len) const;

  /**
   * @brief 更新某槽位记录；尽量原地覆盖，不足则压缩一次；仍不足返回 OutOfRange。
   */
  Status Update(std::uint16_t slot, const std::uint8_t* rec, std::uint16_t len);

  /**
   * @brief 删除某槽位（标记为空槽）；必要时可触发轻量整理。
   */
  Status Erase(std::uint16_t slot);

  // ----------------- 观测与工具 -----------------

  /// 页头快照（只读）
  const PageHeader& Header() const {
    return *reinterpret_cast<const PageHeader*>(page_);
  }

  /// 返回当前连续空闲大小（等价于 PageHeader.free_size）
  std::uint16_t FreeSize() const {
    return reinterpret_cast<const PageHeader*>(page_)->free_size;
  }

  /// 槽位总数（包含 tombstone/空槽）
  std::uint16_t SlotCount() const {
    return reinterpret_cast<const PageHeader*>(page_)->slot_count;
  }

private:
  // 内部工具：触发一次页内压缩（将存活记录紧凑到前部，重建槽目录）
  // 压缩后保证：free_off 指向首个空闲字节；free_size 为连续空闲大小。
  void Compact();

private:
  std::uint8_t*  page_{nullptr};
  std::uint32_t  page_size_{0};
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_INTERNAL_PAGE_SLOTTED_PAGE_LAYOUT_H_
