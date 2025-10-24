#include "internal/page/slotted_page_layout.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

namespace dbms {
namespace storage {

// 槽目录项（页尾向前生长）
struct Slot {
  uint16_t off;  // 记录起始偏移（从页首）
  uint16_t len;  // 记录长度；len==0 表示空槽/已删除
};

static inline Slot* SlotAt(std::uint8_t* base, std::uint32_t page_size, uint16_t slot_id) {
  return reinterpret_cast<Slot*>(base + page_size - (static_cast<size_t>(slot_id) + 1) * sizeof(Slot));
}
static inline const Slot* SlotAtConst(const std::uint8_t* base, std::uint32_t page_size, uint16_t slot_id) {
  return reinterpret_cast<const Slot*>(base + page_size - (static_cast<size_t>(slot_id) + 1) * sizeof(Slot));
}

void SlottedPage::InitNew(std::uint8_t* page, page_id_t pid, std::uint32_t page_size) {
  std::memset(page, 0, page_size);
  auto* hdr = reinterpret_cast<PageHeader*>(page);
  hdr->page_id        = pid;
  hdr->page_lsn       = 0;
  hdr->slot_count     = 0;
  hdr->free_off       = static_cast<uint16_t>(sizeof(PageHeader));
  hdr->free_size      = static_cast<uint16_t>(page_size - sizeof(PageHeader));
  hdr->checksum       = 0;
  hdr->format_version = kPageFormatVersion;
}

Status SlottedPage::Insert(const std::uint8_t* rec, std::uint16_t len, std::uint16_t* out_slot) {
  if (!rec || !out_slot) return Status::InvalidArgument("Insert: null arg");
  if (len == 0)          return Status::InvalidArgument("Insert: empty record");
  auto* hdr = reinterpret_cast<PageHeader*>(page_);

  // 1) 先尝试是否需要新槽位，计算最小需求
  bool reuse_slot = false;
  uint16_t free_slot = 0;
  for (uint16_t i = 0; i < hdr->slot_count; ++i) {
    const Slot* s = SlotAtConst(page_, page_size_, i);
    if (s->len == 0) { reuse_slot = true; free_slot = i; break; }
  }
  const uint16_t extra_slot_bytes = reuse_slot ? 0 : static_cast<uint16_t>(sizeof(Slot));

  // 2) 判断是否有足够连续空闲；不足则压缩一次再判断
  auto need = static_cast<uint32_t>(len) + extra_slot_bytes;
  if (hdr->free_size < need) {
    Compact();
    hdr = reinterpret_cast<PageHeader*>(page_);
    if (hdr->free_size < need) {
      return Status::OutOfRange("Insert: no space");
    }
  }

  // 3) 拷贝记录到 free_off
  std::memmove(page_ + hdr->free_off, rec, len);
  const uint16_t rec_off = hdr->free_off;
  hdr->free_off  = static_cast<uint16_t>(hdr->free_off + len);
  hdr->free_size = static_cast<uint16_t>(hdr->free_size - len);

  // 4) 分配槽位（可能复用）
  uint16_t slot_id = free_slot;
  if (!reuse_slot) {
    slot_id = hdr->slot_count;
    hdr->slot_count = static_cast<uint16_t>(hdr->slot_count + 1);
    hdr->free_size  = static_cast<uint16_t>(hdr->free_size - sizeof(Slot));
  }

  Slot* s = SlotAt(page_, page_size_, slot_id);
  s->off = rec_off;
  s->len = len;

  *out_slot = slot_id;
  return Status::OK();
}

Status SlottedPage::Get(std::uint16_t slot, const std::uint8_t** out_ptr, std::uint16_t* out_len) const {
  if (!out_ptr || !out_len) return Status::InvalidArgument("Get: null out");
  const auto* hdr = reinterpret_cast<const PageHeader*>(page_);
  if (slot >= hdr->slot_count) return Status::NotFound("Get: slot OOR");

  const Slot* s = SlotAtConst(page_, page_size_, slot);
  if (s->len == 0) return Status::NotFound("Get: tombstone");
  if (s->off < sizeof(PageHeader) || static_cast<uint32_t>(s->off) + s->len > page_size_) {
    return Status::Corruption("Get: slot range invalid");
  }
  *out_ptr = page_ + s->off;
  *out_len = s->len;
  return Status::OK();
}

Status SlottedPage::Update(std::uint16_t slot, const std::uint8_t* rec, std::uint16_t len) {
  if (!rec) return Status::InvalidArgument("Update: rec=null");
  auto* hdr = reinterpret_cast<PageHeader*>(page_);
  if (slot >= hdr->slot_count) return Status::NotFound("Update: slot OOR");

  Slot* s = SlotAt(page_, page_size_, slot);
  if (s->len == 0) return Status::NotFound("Update: tombstone");

  // 1) 若新数据不大于旧长度，原地覆盖（保留可能的内碎片，不调整 free_off/free_size）
  if (len <= s->len) {
    std::memmove(page_ + s->off, rec, len);
    s->len = len;
    return Status::OK();
  }

  // 2) 需要更大空间：若当前连续空闲不足，尝试压缩一次；仍不足则返回 OutOfRange（交由上层迁移）
  if (hdr->free_size < len) {
    Compact();
    hdr = reinterpret_cast<PageHeader*>(page_);
    if (hdr->free_size < len) {
      return Status::OutOfRange("Update: no space");
    }
  }

  // 3) 将新副本写到 free_off，更新槽指针；旧区域形成内碎片（下次压缩回收）
  std::memmove(page_ + hdr->free_off, rec, len);
  s->off = hdr->free_off;
  s->len = len;
  hdr->free_off  = static_cast<uint16_t>(hdr->free_off + len);
  hdr->free_size = static_cast<uint16_t>(hdr->free_size - len);
  return Status::OK();
}

Status SlottedPage::Erase(std::uint16_t slot) {
  auto* hdr = reinterpret_cast<PageHeader*>(page_);
  if (slot >= hdr->slot_count) return Status::NotFound("Erase: slot OOR");
  Slot* s = SlotAt(page_, page_size_, slot);
  if (s->len == 0) return Status::NotFound("Erase: already tombstone");
  s->len = 0;  // 标记墓碑（不立即回收空洞；由 Compact 回收）
  return Status::OK();
}

void SlottedPage::Compact() {
  auto* hdr = reinterpret_cast<PageHeader*>(page_);
  const uint16_t n = hdr->slot_count;

  // 收集存活条目（(old_off, slot_id, len)），按 old_off 升序以降低重叠移动风险
  struct Live { uint16_t off, slot, len; };
  std::vector<Live> lives;
  lives.reserve(n);
  for (uint16_t i = 0; i < n; ++i) {
    const Slot* s = SlotAtConst(page_, page_size_, i);
    if (s->len != 0) lives.push_back({s->off, i, s->len});
  }
  std::sort(lives.begin(), lives.end(), [](const Live& a, const Live& b){ return a.off < b.off; });

  // 新的写入位置从页头之后开始
  uint16_t cur = static_cast<uint16_t>(sizeof(PageHeader));
  for (const auto& e : lives) {
    // 防御性检查
    if (e.off < sizeof(PageHeader) || static_cast<uint32_t>(e.off) + e.len > page_size_) continue;
    // 向前紧凑（memmove 支持重叠）
    std::memmove(page_ + cur, page_ + e.off, e.len);
    // 更新槽目录
    Slot* s = SlotAt(page_, page_size_, e.slot);
    s->off = cur;
    // s->len 不变
    cur = static_cast<uint16_t>(cur + e.len);
  }

  // 重算连续空闲区
  hdr->free_off  = cur;
  // 槽目录占用：n * sizeof(Slot)（即使含 tombstone 也保留）
  const uint32_t dir_bytes = static_cast<uint32_t>(n) * sizeof(Slot);
  hdr->free_size = static_cast<uint16_t>(page_size_ - cur - dir_bytes);
}

}  // namespace storage
}  // namespace dbms
