#ifndef DBMS_STORAGE_STORAGE_TYPES_H_
#define DBMS_STORAGE_STORAGE_TYPES_H_

/**
 * @file storage_types.h
 * @brief 基础别名、常量与轻量 Status（Storage 模块的稳定公共头）。
 *
 * 设计要点：
 *  - 仅依赖 C++17 标准库；核心路径不抛异常，统一用 Status 报错。
 *  - 该文件要尽量“稳定、精简”，便于被其他并列模块安全包含。
 */

#include <cstdint>
#include <string>
#include <utility>

namespace dbms {
namespace storage {

// ------------------------- 基本 ID / 句柄类型 -------------------------

/// 页号（逻辑位于某段内；0-based）
using page_id_t = uint32_t;

/// 段号（一个表/索引即为一个段）
using seg_id_t  = uint32_t;

/// 记录标识：页号 + 槽位号（堆页中定位一条记录）
struct RID {
  page_id_t page_id{0};
  uint16_t  slot{0};

  bool operator==(const RID& rhs) const noexcept { return page_id == rhs.page_id && slot == rhs.slot; }
  bool operator!=(const RID& rhs) const noexcept { return !(*this == rhs); }
};

// ------------------------------ 常量与版本 ------------------------------

/// 默认页大小：8 KiB（可通过配置覆盖）
constexpr uint32_t kDefaultPageSize = 8192;

/// 无效占位（用于未初始化/错误返回）
constexpr page_id_t kInvalidPageId = static_cast<page_id_t>(-1);
constexpr seg_id_t  kInvalidSegId  = static_cast<seg_id_t>(-1);

/// 页格式版本号（用于向后兼容检查）
constexpr uint32_t kPageFormatVersion = 1;

// ------------------------------ 错误模型 ------------------------------

/**
 * @brief 轻量级状态码：核心路径不抛异常，返回 Status。
 *
 * 常见场景：
 *  - InvalidArgument：参数非法（空指针/越界等）
 *  - NotFound       ：资源不存在（页越界/槽位墓碑等）
 *  - OutOfRange     ：空间不足、范围错误
 *  - IOError        ：系统调用/磁盘 I/O 失败
 *  - Corruption     ：数据损坏（短读/校验失败等）
 *  - Unavailable    ：资源暂不可用（无空闲帧等）
 */
enum class StatusCode : uint8_t {
  kOk = 0,
  kInvalidArgument,
  kNotFound,
  kOutOfRange,
  kIOError,
  kCorruption,
  kUnavailable,
  kUnknown
};

class Status {
public:
  /// OK 状态
  Status() : code_(StatusCode::kOk) {}
  explicit Status(StatusCode c, std::string msg = {}) : code_(c), msg_(std::move(msg)) {}

  static Status OK() { return Status(); }

  bool ok() const noexcept { return code_ == StatusCode::kOk; }
  StatusCode code() const noexcept { return code_; }
  const std::string& message() const noexcept { return msg_; }

  // 便捷构造
  static Status InvalidArgument(std::string m) { return Status(StatusCode::kInvalidArgument, std::move(m)); }
  static Status NotFound(std::string m)        { return Status(StatusCode::kNotFound, std::move(m)); }
  static Status OutOfRange(std::string m)      { return Status(StatusCode::kOutOfRange, std::move(m)); }
  static Status IOError(std::string m)         { return Status(StatusCode::kIOError, std::move(m)); }
  static Status Corruption(std::string m)      { return Status(StatusCode::kCorruption, std::move(m)); }
  static Status Unavailable(std::string m)     { return Status(StatusCode::kUnavailable, std::move(m)); }
  static Status Unknown(std::string m)         { return Status(StatusCode::kUnknown, std::move(m)); }

private:
  StatusCode  code_{StatusCode::kOk};
  std::string msg_;
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_STORAGE_TYPES_H_
