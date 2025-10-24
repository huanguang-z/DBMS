#ifndef DBMS_STORAGE_STORAGE_OPTIONS_H_
#define DBMS_STORAGE_STORAGE_OPTIONS_H_

/**
 * @file storage_options.h
 * @brief Storage 模块的运行时配置（由上层解析配置文件后填充）。
 *
 * 说明：
 *  - 不在此引入 JSON/TOML 解析，保持 Storage 纯净；
 *  - 所有字段具备合理默认值，便于零配置启动；
 *  - Validate() 只做“明显异常”的快速检查。
 */

#include <cstdint>
#include <string>
#include <vector>

#include "dbms/storage/storage_types.h"

namespace dbms {
namespace storage {

struct StorageOptions {
  // ---- Page & Buffer 基本设置 ----
  uint32_t page_size          = kDefaultPageSize;  // 页大小（字节）
  uint32_t buffer_pool_frames = 256;               // 缓冲帧数量

  // ---- 替换策略（可插拔，文本约定）----
  // 示例："clock" / "lruk:k=2"
  std::string replacer = "clock";

  // ---- 空闲空间管理（FSM 分桶阈值，单位：字节）----
  std::vector<uint32_t> fsm_bins = {128, 512, 1024, 2048, 4096, 8192};

  // ---- I/O 行为与校验（预留）----
  bool io_direct       = false;  // 直接 I/O（需要对齐约束）
  bool enable_checksum = true;   // 是否启用页校验（实现可后置）

  // ---- 合法性检查（轻量）----
  bool Validate() const {
    if (page_size < 1024) return false;              // 粗略下限（避免过小）
    if (buffer_pool_frames == 0) return false;
    if (fsm_bins.empty()) return false;
    return true;
  }
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_STORAGE_OPTIONS_H_
