#ifndef DBMS_STORAGE_RECORD_TUPLE_H_
#define DBMS_STORAGE_RECORD_TUPLE_H_

/**
 * @file tuple.h
 * @brief 记录对象：持有一行字节序列，提供构造（TupleBuilder）与读取接口。
 *
 * 布局见 schema.h： [NullBitmap?][Fixed Area][Var Area]
 * - VARCHAR 的固定区为 (uint16_t offset, uint16_t len)，offset 从行起始算起。
 * - CHAR(N) 固定 N 字节（右侧 '\0' 填充；超长则截断）。
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dbms/storage/storage_types.h"
#include "dbms/storage/record/schema.h"

namespace dbms {
namespace storage {

class Tuple {
public:
  Tuple() = default;
  explicit Tuple(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

  const std::vector<std::uint8_t>& Bytes() const noexcept { return data_; }
  size_t Size()  const noexcept { return data_.size(); }
  bool   Empty() const noexcept { return data_.empty(); }

  // 序列化/反序列化（拷贝）
  void   Serialize(std::uint8_t* out) const;
  static Tuple Deserialize(const std::uint8_t* src, size_t len);

  // NULL 位图
  bool   IsNull(const Schema& schema, size_t col_idx) const;

  // 读取接口（列为 NULL 返回 NotFound；类型不匹配返回 InvalidArgument）
  Status GetInt32 (const Schema& s, size_t i, int32_t*  out) const;
  Status GetInt64 (const Schema& s, size_t i, int64_t*  out) const;
  Status GetFloat (const Schema& s, size_t i, float*    out) const;
  Status GetDouble(const Schema& s, size_t i, double*   out) const;
  Status GetDate  (const Schema& s, size_t i, int32_t*  out_days) const;
  Status GetChar  (const Schema& s, size_t i, std::string* out) const;
  Status GetVarChar(const Schema& s, size_t i, std::string* out) const;

private:
  friend class TupleBuilder;
  const std::uint8_t* PtrAt(size_t off) const { return data_.data() + off; }
  std::uint8_t*       PtrAt(size_t off)       { return data_.data() + off; }

private:
  std::vector<std::uint8_t> data_;
};

/**
 * @brief 行构造器：按列设置值，调用 Build() 生成 Tuple。
 *
 * 用法：
 *   TupleBuilder tb(schema);
 *   tb.SetInt32(0, 42);
 *   tb.SetVarChar(1, "hello");
 *   Tuple t; tb.Build(&t);
 */
class TupleBuilder {
public:
  explicit TupleBuilder(const Schema& s);

  // 设置 NULL 与各类型值（下标 i 为列序）
  Status SetNull(size_t i);
  Status SetInt32 (size_t i, int32_t  v);
  Status SetInt64 (size_t i, int64_t  v);
  Status SetFloat (size_t i, float    v);
  Status SetDouble(size_t i, double   v);
  Status SetDate  (size_t i, int32_t  days);
  Status SetChar  (size_t i, std::string_view v);   // 固定 N 字节
  Status SetVarChar(size_t i, std::string_view v);  // 限定最大长度

  // 生成最终 Tuple
  Status Build(Tuple* out);

private:
  const Schema& s_;

  std::vector<std::uint8_t> row_;  // Fixed + NullBitmap
  std::vector<std::uint8_t> var_;  // Var 区缓冲
  std::vector<bool>         set_;  // 每列是否已设置/标 NULL

  void SetNullBit(size_t i);
  bool GetNullBit(size_t i) const;

  void WriteFixed(size_t off, const void* src, size_t n);

  struct VarMeta { uint16_t off; uint16_t len; };  // 仅作说明
  void WriteVarMeta(size_t fixed_off, uint16_t off, uint16_t len);
};

}  // namespace storage
}  // namespace dbms

#endif  // DBMS_STORAGE_RECORD_TUPLE_H_
