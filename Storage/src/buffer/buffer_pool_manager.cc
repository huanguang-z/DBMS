#include "dbms/storage/buffer/buffer_pool_manager.h"

#include <cassert>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "dbms/storage/page/page.h"
#include "internal/buffer/frame.h"
#include "internal/buffer/page_table.h"
#include "internal/buffer/clock_replacer.h"
#include "internal/buffer/lruk_replacer.h"

namespace dbms {
namespace storage {

// -------------------- 内部实现（Pimpl） --------------------

struct BufferPoolManager::Impl {
  explicit Impl(int num_frames, uint32_t page_size, DiskManager* disk, std::unique_ptr<IReplacer> r)
      : frames(num_frames), num_frames(num_frames), page_size(page_size), disk(disk), replacer(std::move(r)) {
    // 预分配一整块连续内存作为“页池”
    arena.resize(static_cast<size_t>(num_frames) * page_size, 0);
    for (int i = 0; i < num_frames; ++i) {
      frames[i].Reset(arena.data() + static_cast<size_t>(i) * page_size);
      free_list.push_back(i);
    }
  }

  // 元数据
  std::vector<std::uint8_t> arena;   // 页内存
  std::vector<Frame>        frames;  // 帧元信息
  PageTable                 table;   // page_id -> frame_id
  std::deque<int>           free_list;
  int                       num_frames{0};
  uint32_t                  page_size{0};

  // 组件
  DiskManager*              disk{nullptr};
  std::unique_ptr<IReplacer> replacer;

  // 统计与锁
  BufferStats               stats;
  mutable std::mutex        mu;

  // 刷盘回调（用于 WAL 对齐）：在写盘前调用
  std::function<void(page_id_t, uint64_t)> flush_cb;
};

// -------------------- BPM 外部接口 --------------------

BufferPoolManager::BufferPoolManager(int num_frames,
                                     uint32_t page_size,
                                     DiskManager* disk,
                                     std::unique_ptr<IReplacer> replacer)
    : p_(new Impl(num_frames, page_size, disk, std::move(replacer))),
      num_frames_(num_frames),
      page_size_(page_size) {}

BufferPoolManager::~BufferPoolManager() = default;

// 查找 frame
frame_id_t BufferPoolManager::LookupFrame(page_id_t pid) const {
  frame_id_t fid = -1;
  if (p_->table.Lookup(pid, &fid)) return fid;
  return -1;
}

// 选择用于装载 pid 的 frame；若需要淘汰，返回被逐出的旧 pid
frame_id_t BufferPoolManager::AcquireFrameFor(page_id_t pid, page_id_t* evicted_pid) {
   (void)pid; 
  *evicted_pid = kInvalidPageId;

  // 优先用 free_list
  if (!p_->free_list.empty()) {
    int fid = p_->free_list.front();
    p_->free_list.pop_front();
    return fid;
  }

  // 使用替换器找受害者
  int victim = -1;
  if (!p_->replacer->Victim(&victim)) return -1;

  Frame& f = p_->frames[victim];
  // 如果该帧持有旧页，先刷盘（如果脏）
  if (f.page_id != kInvalidPageId) {
    if (FlushFrame(victim)) {
      p_->stats.flushes++;
    }
    // 从 page table 移除旧映射
    p_->table.Erase(f.page_id);
    *evicted_pid = f.page_id;
    p_->stats.evictions++;
  }
  return victim;
}

bool BufferPoolManager::FlushFrame(frame_id_t fid) {
  Frame& f = p_->frames[fid];
  if (!f.dirty || f.page_id == kInvalidPageId) return false;

  // 刷盘前回调（WAL 对齐），从 PageHeader 读取 page_lsn
  auto* hdr = reinterpret_cast<PageHeader*>(f.data);
  if (p_->flush_cb) {
    p_->flush_cb(f.page_id, hdr->page_lsn);
  }

  if (!p_->disk) return false;
  Status s = p_->disk->WritePage(f.page_id, f.data);
  if (!s.ok()) return false;

  f.dirty = false;
  return true;
}

Status BufferPoolManager::FetchPage(page_id_t pid, std::uint8_t** out_data) {
  if (!out_data) return Status::InvalidArgument("FetchPage: out_data=null");

  std::lock_guard<std::mutex> g(p_->mu);

  // 命中：直接返回
  if (int fid = LookupFrame(pid); fid >= 0) {
    Frame& f = p_->frames[fid];
    f.pin_count++;
    p_->replacer->Pin(fid);
    p_->stats.hits++;
    *out_data = f.data;
    return Status::OK();
  }

  // 未命中：需要装入
  page_id_t evicted = kInvalidPageId;
  int fid = AcquireFrameFor(pid, &evicted);
  if (fid < 0) {
    return Status::Unavailable("FetchPage: no frame available");
  }

  Frame& f = p_->frames[fid];

  // 从磁盘读取 pid（如果 pid 超出文件范围，返回 NotFound）
  if (Status s = p_->disk->ReadPage(pid, f.data); !s.ok()) {
    // 回收该 frame
    f.page_id = kInvalidPageId;
    f.pin_count = 0;
    f.dirty = false;
    p_->free_list.push_front(fid);
    return s;
  }

  f.page_id = pid;
  f.pin_count = 1;
  f.dirty = false;
  p_->table.Insert(pid, fid);
  p_->replacer->Pin(fid);  // 新加载的页默认被固定，不可淘汰
  p_->stats.misses++;
  *out_data = f.data;
  return Status::OK();
}

Status BufferPoolManager::NewPage(page_id_t* out_pid, std::uint8_t** out_data) {
  if (!out_pid || !out_data) return Status::InvalidArgument("NewPage: null out param");
  std::lock_guard<std::mutex> g(p_->mu);

  page_id_t evicted = kInvalidPageId;
  int fid = AcquireFrameFor(/*pid*/0, &evicted);
  if (fid < 0) return Status::Unavailable("NewPage: no frame available");

  // 分配新的 page_id：基于文件长度
  const uint64_t count = p_->disk->PageCount();
  const page_id_t pid  = static_cast<page_id_t>(count);

  std::memset(p_->frames[fid].data, 0, p_->page_size);
  p_->frames[fid].page_id = pid;
  p_->frames[fid].pin_count = 1;
  p_->frames[fid].dirty = false;
  p_->table.Insert(pid, fid);
  p_->replacer->Pin(fid);

  // 写一个空页到磁盘尾部，确保文件增长
  (void)p_->disk->WritePage(pid, p_->frames[fid].data);

  *out_pid  = pid;
  *out_data = p_->frames[fid].data;
  return Status::OK();
}

Status BufferPoolManager::UnpinPage(page_id_t pid, bool is_dirty) {
  std::lock_guard<std::mutex> g(p_->mu);
  int fid = LookupFrame(pid);
  if (fid < 0) return Status::NotFound("UnpinPage: pid not in buffer");
  return UnpinFrame(fid, is_dirty);
}

Status BufferPoolManager::UnpinFrame(frame_id_t fid, bool is_dirty) {
  Frame& f = p_->frames[fid];
  if (f.pin_count <= 0) return Status::InvalidArgument("UnpinFrame: pin_count <= 0");

  f.pin_count--;
  f.dirty = f.dirty || is_dirty;
  if (f.pin_count == 0) {
    p_->replacer->Unpin(fid);  // 可被替换
  }
  return Status::OK();
}

Status BufferPoolManager::FlushPage(page_id_t pid) {
  std::lock_guard<std::mutex> g(p_->mu);
  int fid = LookupFrame(pid);
  if (fid < 0) return Status::NotFound("FlushPage: pid not in buffer");
  if (FlushFrame(fid)) p_->stats.flushes++;
  return Status::OK();
}

void BufferPoolManager::FlushAll() {
  std::lock_guard<std::mutex> g(p_->mu);
  for (int fid = 0; fid < p_->num_frames; ++fid) {
    (void)FlushFrame(fid);
  }
}

BufferStats BufferPoolManager::GetStats() const {
  std::lock_guard<std::mutex> g(p_->mu);
  return p_->stats;
}

void BufferPoolManager::RegisterFlushCallback(std::function<void(page_id_t, uint64_t)> cb) {
  std::lock_guard<std::mutex> g(p_->mu);
  p_->flush_cb = std::move(cb);
}

}  // namespace storage
}  // namespace dbms
