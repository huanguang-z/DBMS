#include "dbms/storage/record/schema.h"

#include <cassert>
#include <cmath>
#include <stdexcept>

namespace dbms {
namespace storage {

// ---- 静态工具 ----

bool Schema::IsFixedType(Type t) {
  switch (t) {
    case Type::INT32:
    case Type::INT64:
    case Type::FLOAT:
    case Type::DOUBLE:
    case Type::CHAR:
    case Type::DATE:
      return true;
    case Type::VARCHAR:
      return false;
  }
  return true;
}

uint32_t Schema::FixedSizeOf(Type t, uint32_t char_or_varchar_len) {
  switch (t) {
    case Type::INT32:  return 4;
    case Type::INT64:  return 8;
    case Type::FLOAT:  return 4;
    case Type::DOUBLE: return 8;
    case Type::DATE:   return 4;
    case Type::CHAR:   return char_or_varchar_len;
    case Type::VARCHAR:return 4;  // (uint16_t offset, uint16_t len)
  }
  return 0;
}

// ---- Schema 主体 ----

Schema::Schema(std::vector<Column> cols, bool use_null_bitmap)
    : columns_(std::move(cols)), use_null_bitmap_(use_null_bitmap) {
  BuildLayout();
}

bool Schema::IsFixed(size_t idx) const {
  return IsFixedType(columns_.at(idx).type) && columns_[idx].type != Type::VARCHAR;
}

size_t Schema::FixedSizeOf(size_t idx) const {
  const auto& c = columns_.at(idx);
  return FixedSizeOf(c.type, c.len);
}

size_t Schema::FixedOffsetOf(size_t idx) const {
  return static_cast<size_t>(fixed_offsets_.at(idx));
}

uint32_t Schema::VarCharMaxLen(size_t idx) const {
  const auto& c = columns_.at(idx);
  if (c.type != Type::VARCHAR) return 0;
  return c.len;
}

void Schema::BuildLayout() {
  const size_t n = columns_.size();
  fixed_offsets_.resize(n);
  fixed_sizes_.resize(n);

  null_bytes_ = use_null_bitmap_ ? static_cast<size_t>((n + 7) / 8) : 0;

  size_t off = null_bytes_;
  for (size_t i = 0; i < n; ++i) {
    const auto& col = columns_[i];
    const size_t sz = FixedSizeOf(col.type, col.len);
    fixed_offsets_[i] = static_cast<uint32_t>(off);
    fixed_sizes_[i]   = static_cast<uint32_t>(sz);
    off += sz;
  }
  fixed_area_size_ = off;
}

}  // namespace storage
}  // namespace dbms
