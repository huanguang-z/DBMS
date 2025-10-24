#include "dbms/storage/table/table_heap.h"
#include "dbms/storage/table/table_iterator.h"
#include <cstring>

#include "internal/page/slotted_page_layout.h"
#include "dbms/storage/page/page.h"

namespace dbms {
namespace storage {

void TableHeap::UpdateFsmForPage(page_id_t pid, std::uint8_t* page) {
  auto* hdr = reinterpret_cast<PageHeader*>(page);
  fsm_->Update(pid, hdr->free_size);
}

Status TableHeap::Insert(const Tuple& t, RID* out) {
  if (!out) return Status::InvalidArgument("Insert: out=null");
  if (t.Empty()) return Status::InvalidArgument("Insert: empty tuple");

  const uint16_t need = static_cast<uint16_t>(t.Size());

  // 1) 从 FSM 找页
  page_id_t pid = fsm_->Find(need);

  // 2) 若没有可用页，分配新页并初始化
  if (pid == kInvalidPageId) {
    pid = sm_->AllocatePage(seg_id_);
    if (pid == kInvalidPageId) return Status::Unavailable("Insert: allocate page failed");

    std::uint8_t* data = nullptr;
    Status s = bpm_->FetchPage(pid, &data);
    if (!s.ok()) return s;
    SlottedPage::InitNew(data, pid, page_size_);
    bpm_->UnpinPage(pid, /*dirty=*/true);

    // 初始空闲上报 FSM
    if (bpm_->FetchPage(pid, &data).ok()) {
      UpdateFsmForPage(pid, data);
      bpm_->UnpinPage(pid, /*dirty=*/false);
    }
  }

  // 3) 在候选页尝试插入；若失败则分配新页再尝试
  {
    std::uint8_t* data = nullptr;
    Status s = bpm_->FetchPage(pid, &data);
    if (!s.ok()) return s;

    SlottedPage sp(data, page_size_);
    uint16_t slot = 0;
    Status ins = sp.Insert(t.Bytes().data(), static_cast<uint16_t>(t.Size()), &slot);
    if (!ins.ok()) {
      bpm_->UnpinPage(pid, /*dirty=*/false);

      page_id_t npid = sm_->AllocatePage(seg_id_);
      if (npid == kInvalidPageId) return ins;

      std::uint8_t* ndata = nullptr;
      if (!bpm_->FetchPage(npid, &ndata).ok()) return Status::Unavailable("Fetch new page failed");
      SlottedPage::InitNew(ndata, npid, page_size_);
      SlottedPage sp2(ndata, page_size_);
      Status ins2 = sp2.Insert(t.Bytes().data(), static_cast<uint16_t>(t.Size()), &slot);
      if (!ins2.ok()) {
        bpm_->UnpinPage(npid, /*dirty=*/false);
        return ins2;
      }
      UpdateFsmForPage(npid, ndata);
      bpm_->UnpinPage(npid, /*dirty=*/true);
      *out = RID{npid, slot};
      return Status::OK();
    }
    UpdateFsmForPage(pid, data);
    bpm_->UnpinPage(pid, /*dirty=*/true);
    *out = RID{pid, slot};
    return Status::OK();
  }
}

Status TableHeap::Update(const RID& rid, const Tuple& t) {
  std::uint8_t* data = nullptr;
  Status s = bpm_->FetchPage(rid.page_id, &data);
  if (!s.ok()) return s;

  SlottedPage sp(data, page_size_);
  Status up = sp.Update(rid.slot, t.Bytes().data(), static_cast<uint16_t>(t.Size()));
  if (up.ok()) {
    UpdateFsmForPage(rid.page_id, data);
    bpm_->UnpinPage(rid.page_id, /*dirty=*/true);
    return Status::OK();
  }

  if (up.code() == StatusCode::kOutOfRange) {
    bpm_->UnpinPage(rid.page_id, /*dirty=*/false);

    RID new_rid;
    Status ins = Insert(t, &new_rid);
    if (!ins.ok()) return ins;

    std::uint8_t* data_old = nullptr;
    if (!bpm_->FetchPage(rid.page_id, &data_old).ok()) return Status::Unavailable("Re-fetch old page failed");
    SlottedPage sp_old(data_old, page_size_);
    (void)sp_old.Erase(rid.slot);
    UpdateFsmForPage(rid.page_id, data_old);
    bpm_->UnpinPage(rid.page_id, /*dirty=*/true);
    return Status::OK();
  }

  bpm_->UnpinPage(rid.page_id, /*dirty=*/false);
  return up;
}

Status TableHeap::Erase(const RID& rid) {
  std::uint8_t* data = nullptr;
  Status s = bpm_->FetchPage(rid.page_id, &data);
  if (!s.ok()) return s;

  SlottedPage sp(data, page_size_);
  Status del = sp.Erase(rid.slot);
  if (del.ok()) {
    UpdateFsmForPage(rid.page_id, data);
    bpm_->UnpinPage(rid.page_id, /*dirty=*/true);
  } else {
    bpm_->UnpinPage(rid.page_id, /*dirty=*/false);
  }
  return del;
}

Status TableHeap::Get(const RID& rid, Tuple* out) const {
  if (!out) return Status::InvalidArgument("Get: out=null");

  std::uint8_t* data = nullptr;
  Status s = bpm_->FetchPage(rid.page_id, &data);
  if (!s.ok()) return s;

  SlottedPage sp(data, page_size_);
  const std::uint8_t* p = nullptr;
  uint16_t len = 0;
  Status g = sp.Get(rid.slot, &p, &len);
  if (g.ok()) {
    *out = Tuple::Deserialize(p, len);
    bpm_->UnpinPage(rid.page_id, /*dirty=*/false);
    return Status::OK();
  }
  bpm_->UnpinPage(rid.page_id, /*dirty=*/false);
  return g;
}

// 迭代器接口（实现见 table_iterator.cc）
TableIterator TableHeap::Begin() const { return TableIterator(this); }
TableIterator TableHeap::End()   const { return TableIterator(); }

}  // namespace storage
}  // namespace dbms
