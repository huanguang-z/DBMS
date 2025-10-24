#ifndef DBMS_STORAGE_RECORD_SCHEMA_H_
#define DBMS_STORAGE_RECORD_SCHEMA_H_

/**
 * @file schema.h
 * @brief 行记录的模式定义：列类型、长度、NULL 性、固定区偏移等。
 *
 * 行布局：
 *   [ NullBitmap? ][ Fixed Area ][ Var Area ]
 *  - NullBitmap：可选（use_null_bitmap=true），1bit/列；
 *  - Fixed Area：定长类型直接存放；VARCHAR 存放 (uint16_t offset, uint16_t len)；
 *  - Var Area  ：实际变长字节序列（按照出现顺序附加）。
 */

#include <cstdint>
#include <string>
#include <vector>

namespace dbms {
namespace storage {

enum class Type : uint8_t {
  INT32,
  INT64,
  FLOAT,
  DOUBLE,
  CHAR,     // 需结合 Column.len 指定 N 字节
  VARCHAR,  // 需结合 Column.len 指定最大长度
  DATE      // 以“自 1970-01-01 的天数（int32）”编码
};

struct Column {
  std::string name;
  Type        type{Type::INT32};
  uint32_t    len{0};       ///< 对 CHAR/VARCHAR 有意义（字节）
  bool        nullable{false};
};

class Schema {
public:
  Schema() = default;
  Schema(std::vector<Column> cols, bool use_null_bitmap = false);

  // 基本属性
  size_t        ColumnCount() const noexcept { return columns_.size(); }
  const Column& GetColumn(size_t idx) const { return columns_.at(idx); }
  bool          UseNullBitmap() const noexcept { return use_null_bitmap_; }

  // 固定区与位图
  size_t NullBitmapSize() const noexcept { return null_bytes_; }
  size_t FixedAreaSize()  const noexcept { return fixed_area_size_; }

  // 工具：判定是否定长；定长字节数；固定区偏移
  bool   IsFixed(size_t idx) const;
  size_t FixedSizeOf(size_t idx) const;
  size_t FixedOffsetOf(size_t idx) const;

  // VARCHAR 最大长度（对非 VARCHAR 返回 0）
  uint32_t VarCharMaxLen(size_t idx) const;

  // ---- 静态工具 ----
  static bool     IsFixedType(Type t);
  static uint32_t FixedSizeOf(Type t, uint32_t char_or_varchar_len);

private:
  void BuildLayout();  // 预计算固定区偏移与大小

private:
  std::vector<Column>  columns_;
  bool                 use_null_bitmap_{false};

  size_t               null_bytes_{0};               // NullBitmap 字节数
  size_t               fixed_area_size_{0};          // NullBitmap + 各列定长部分
  std::vector<uint32_t> fixed_offsets_;              // 含 NullBitmap 的整体偏移
  std::vector<uint32_t> fixed_sizes_;                // 每列在固定区的大小
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_RECORD_SCHEMA_H_
