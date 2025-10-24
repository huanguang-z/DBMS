#include "dbms/storage/space/free_space_manager.h"

#include <algorithm>

namespace dbms {
namespace storage {

FreeSpaceManager::FreeSpaceManager(uint32_t page_size, std::vector<uint32_t> thresholds)
    : page_size_(page_size), thresholds_(std::move(thresholds)) {
  // 阈值规范化：升序 + 去重
  std::sort(thresholds_.begin(), thresholds_.end());
  thresholds_.erase(std::unique(thresholds_.begin(), thresholds_.end()), thresholds_.end());
  // 桶数 = 阈值数 + 1
  bins_.resize(thresholds_.size() + 1);
}

size_t FreeSpaceManager::BinIndex(uint32_t free_bytes) const {
  auto it = std::lower_bound(thresholds_.begin(), thresholds_.end(), free_bytes);
  return static_cast<size_t>(std::distance(thresholds_.begin(), it));
}

page_id_t FreeSpaceManager::Find(uint16_t need_bytes) const {
  std::lock_guard<std::mutex> g(mu_);
  const size_t start = BinIndex(need_bytes);
  for (size_t i = start; i < bins_.size(); ++i) {
    for (const auto pid : bins_[i]) {
      auto it = pid2free_.find(pid);
      if (it != pid2free_.end() && it->second >= need_bytes) {
        return pid;
      }
    }
  }
  return kInvalidPageId;
}

void FreeSpaceManager::Update(page_id_t pid, uint16_t free_bytes) {
  const size_t new_bin = BinIndex(free_bytes);
  std::lock_guard<std::mutex> g(mu_);

  auto it_bin = pid2bin_.find(pid);
  if (it_bin != pid2bin_.end()) {
    const size_t old_bin = it_bin->second;
    if (old_bin != new_bin) {
      bins_[old_bin].erase(pid);
      bins_[new_bin].insert(pid);
      it_bin->second = new_bin;
    }
    pid2free_[pid] = free_bytes;
    return;
  }

  bins_[new_bin].insert(pid);
  pid2bin_.emplace(pid, new_bin);
  pid2free_.emplace(pid, free_bytes);
}

void FreeSpaceManager::Remove(page_id_t pid) {
  std::lock_guard<std::mutex> g(mu_);
  auto it_bin = pid2bin_.find(pid);
  if (it_bin == pid2bin_.end()) return;

  const size_t bin = it_bin->second;
  bins_[bin].erase(pid);
  pid2bin_.erase(it_bin);
  pid2free_.erase(pid);
}

void FreeSpaceManager::RegisterSegmentProbe(FreeProbeFn free_probe, PageCountFn page_count) {
  std::lock_guard<std::mutex> g(mu_);
  probe_free_  = std::move(free_probe);
  probe_count_ = std::move(page_count);
}

Status FreeSpaceManager::RebuildFromSegment(seg_id_t seg) {
  std::lock_guard<std::mutex> g(mu_);
  if (!probe_free_ || !probe_count_) return Status::Unavailable("FSM: no probe registered");

  for (auto& s : bins_) s.clear();
  pid2bin_.clear();
  pid2free_.clear();

  const uint64_t pages = probe_count_(seg);
  for (uint64_t i = 0; i < pages; ++i) {
    const page_id_t pid = static_cast<page_id_t>(i);
    const uint16_t  free = probe_free_(seg, pid);
    const size_t b = BinIndex(free);
    bins_[b].insert(pid);
    pid2bin_.emplace(pid, b);
    pid2free_.emplace(pid, free);
  }
  return Status::OK();
}

std::vector<size_t> FreeSpaceManager::BinSizes() const {
  std::lock_guard<std::mutex> g(mu_);
  std::vector<size_t> v;
  v.reserve(bins_.size());
  for (const auto& s : bins_) v.push_back(s.size());
  return v;
}

std::vector<uint32_t> FreeSpaceManager::BinThresholds() const {
  std::lock_guard<std::mutex> g(mu_);
  return thresholds_;
}

size_t FreeSpaceManager::TotalTrackedPages() const {
  std::lock_guard<std::mutex> g(mu_);
  return pid2bin_.size();
}

}  // namespace storage
}  // namespace dbms
