#include "dbms/storage/segment/segment_manager.h"
#include <cassert>
#include <cstring>
#include <utility>
#include <vector>

namespace dbms {
namespace storage {

SegmentManager::SegmentManager(uint32_t page_size, std::string base_dir)
    : page_size_(page_size), base_dir_(std::move(base_dir)) {}

SegmentManager::~SegmentManager() = default;

std::string SegmentManager::MakePath(seg_id_t seg) const {
  return base_dir_ + "/seg_" + std::to_string(seg) + ".dbseg";
}

std::string SegmentManager::SegmentPath(seg_id_t seg) const { return MakePath(seg); }

Status SegmentManager::EnsureSegment(seg_id_t seg) {
  std::lock_guard<std::mutex> g(mu_);
  auto it = segs_.find(seg);
  if (it != segs_.end()) return Status::OK();

  std::string path = MakePath(seg);
  auto dm = std::make_unique<DiskManager>(path, page_size_);

  Segment s;
  s.disk = std::move(dm);
  s.free_list.clear();
  segs_.emplace(seg, std::move(s));
  return Status::OK();
}

page_id_t SegmentManager::AllocatePage(seg_id_t seg) {
  std::lock_guard<std::mutex> g(mu_);
  auto it = segs_.find(seg);
  if (it == segs_.end()) {
    if (!EnsureSegment(seg).ok()) return kInvalidPageId;
    it = segs_.find(seg);
    if (it == segs_.end()) return kInvalidPageId;
  }

  Segment& S = it->second;

  // 1) 复用空闲页
  if (!S.free_list.empty()) {
    page_id_t pid = S.free_list.back();
    S.free_list.pop_back();
    return pid;
  }

  // 2) 追加新页
  DiskManager* dm = S.disk.get();
  if (!dm) return kInvalidPageId;

  const uint64_t count = dm->PageCount();
  const page_id_t pid  = static_cast<page_id_t>(count);
  Status s = dm->ResizeToPages(count + 1);
  if (!s.ok()) return kInvalidPageId;
  return pid;
}

void SegmentManager::FreePage(seg_id_t seg, page_id_t pid) {
  std::lock_guard<std::mutex> g(mu_);
  auto it = segs_.find(seg);
  if (it == segs_.end()) return;
  it->second.free_list.push_back(pid);
}

uint64_t SegmentManager::PageCount(seg_id_t seg) const {
  std::lock_guard<std::mutex> g(mu_);
  auto it = segs_.find(seg);
  if (it == segs_.end() || !it->second.disk) return 0;
  return it->second.disk->PageCount();
}

uint16_t SegmentManager::ProbePageFree(seg_id_t seg, page_id_t pid) const {
  std::lock_guard<std::mutex> g(mu_);
  auto it = segs_.find(seg);
  if (it == segs_.end() || !it->second.disk) return 0;

  DiskManager* dm = it->second.disk.get();
  std::vector<std::uint8_t> buf(page_size_, 0);
  Status s = dm->ReadPage(pid, buf.data());
  if (!s.ok()) return 0;

  const auto* hdr = reinterpret_cast<const PageHeader*>(buf.data());
  if (hdr->format_version != kPageFormatVersion) return 0;
  return hdr->free_size;
}

DiskManager* SegmentManager::GetDisk(seg_id_t seg) {
  std::lock_guard<std::mutex> g(mu_);
  auto it = segs_.find(seg);
  if (it == segs_.end()) return nullptr;
  return it->second.disk.get();
}

}  // namespace storage
}  // namespace dbms
