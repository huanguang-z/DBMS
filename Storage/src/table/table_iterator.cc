#include "dbms/storage/table/table_iterator.h"

#include "dbms/storage/table/table_heap.h"
#include "dbms/storage/page/page.h"
#include "internal/page/slotted_page_layout.h"

namespace dbms {
namespace storage {

TableIterator::TableIterator(const TableHeap* table)
    : table_(table), pid_(0), slot_(0), end_(false), current_{} {
  SeekFirst();
}

void TableIterator::SeekFirst() {
  if (!table_) { end_ = true; return; }

  const uint64_t pages = table_->sm_->PageCount(table_->seg_id_);
  if (pages == 0) { end_ = true; return; }

  for (page_id_t p = 0; p < pages; ++p) {
    std::uint8_t* data = nullptr;
    if (!table_->bpm_->FetchPage(p, &data).ok()) continue;

    const auto* hdr = reinterpret_cast<const PageHeader*>(data);
    const uint16_t max_slot = hdr->slot_count;

    SlottedPage sp(data, table_->page_size_);
    for (uint16_t s = 0; s < max_slot; ++s) {
      const std::uint8_t* rec = nullptr;
      uint16_t len = 0;
      if (sp.Get(s, &rec, &len).ok()) {
        current_.rid = RID{p, s};
        current_.tuple = Tuple::Deserialize(rec, len);
        table_->bpm_->UnpinPage(p, /*dirty=*/false);
        pid_ = p; slot_ = s; end_ = false;
        return;
      }
    }
    table_->bpm_->UnpinPage(p, /*dirty=*/false);
  }
  end_ = true;
}

bool TableIterator::LoadAt(page_id_t pid, uint16_t slot) {
  std::uint8_t* data = nullptr;
  if (!table_->bpm_->FetchPage(pid, &data).ok()) return false;

  const auto* hdr = reinterpret_cast<const PageHeader*>(data);
  if (slot >= hdr->slot_count) {
    table_->bpm_->UnpinPage(pid, /*dirty=*/false);
    return false;
  }

  SlottedPage sp(data, table_->page_size_);
  const std::uint8_t* rec = nullptr;
  uint16_t len = 0;
  bool ok = sp.Get(slot, &rec, &len).ok();
  if (ok) {
    current_.rid = RID{pid, slot};
    current_.tuple = Tuple::Deserialize(rec, len);
  }
  table_->bpm_->UnpinPage(pid, /*dirty=*/false);
  return ok;
}

bool TableIterator::AdvanceOne() {
  const uint64_t pages = table_->sm_->PageCount(table_->seg_id_);
  page_id_t p = pid_;
  uint16_t  s = static_cast<uint16_t>(slot_ + 1);

  while (p < pages) {
    std::uint8_t* data = nullptr;
    if (!table_->bpm_->FetchPage(p, &data).ok()) { ++p; s = 0; continue; }
    const auto* hdr = reinterpret_cast<const PageHeader*>(data);
    const uint16_t max_slot = hdr->slot_count;
    table_->bpm_->UnpinPage(p, /*dirty=*/false);

    for (; s < max_slot; ++s) {
      if (LoadAt(p, s)) { pid_ = p; slot_ = s; return true; }
    }
    ++p; s = 0;
  }
  return false;
}

TableIterator& TableIterator::operator++() {
  if (end_ || !table_) return *this;
  if (!AdvanceOne()) { end_ = true; }
  return *this;
}

}  // namespace storage
}  // namespace dbms
