#include "dbms/storage/buffer/page_guard.h"
#include "dbms/storage/buffer/buffer_pool_manager.h"

namespace dbms {
namespace storage {

PageGuard::~PageGuard() { Release(); }

void PageGuard::Release() {
  if (!bpm_) return;
  // 由 BPM 进行实际的 unpin；guard 只传递“脏”标记与 frame id
  (void)bpm_->UnpinFrame(fid_, dirty_);
  bpm_   = nullptr;
  pid_   = kInvalidPageId;
  fid_   = -1;
  data_  = nullptr;
  dirty_ = false;
}

}  // namespace storage
}  // namespace dbms
