#include "dbms/storage/record/tuple.h"

#include <cassert>
#include <cstring>

namespace dbms {
namespace storage {

// ================= Tuple =================

void Tuple::Serialize(std::uint8_t* out) const {
  std::memcpy(out, data_.data(), data_.size());
}

Tuple Tuple::Deserialize(const std::uint8_t* src, size_t len) {
  Tuple t;
  t.data_.assign(src, src + len);
  return t;
}

bool Tuple::IsNull(const Schema& s, size_t i) const {
  if (!s.UseNullBitmap()) return false;
  const size_t byte = i / 8;
  const size_t bit  = i % 8;
  if (s.NullBitmapSize() == 0 || data_.size() < s.NullBitmapSize()) return false;
  return (data_[byte] >> bit) & 0x1;
}

static inline const void* FixedPtr(const Tuple& t, const Schema& s, size_t i) {
  return t.Bytes().data() + s.FixedOffsetOf(i);
}

Status Tuple::GetInt32(const Schema& s, size_t i, int32_t* out) const {
  if (!out) return Status::InvalidArgument("GetInt32: out=null");
  if (IsNull(s, i)) return Status::NotFound("GetInt32: NULL");
  if (s.GetColumn(i).type != Type::INT32) return Status::InvalidArgument("type mismatch");
  std::memcpy(out, FixedPtr(*this, s, i), 4);
  return Status::OK();
}

Status Tuple::GetInt64(const Schema& s, size_t i, int64_t* out) const {
  if (!out) return Status::InvalidArgument("GetInt64: out=null");
  if (IsNull(s, i)) return Status::NotFound("GetInt64: NULL");
  if (s.GetColumn(i).type != Type::INT64) return Status::InvalidArgument("type mismatch");
  std::memcpy(out, FixedPtr(*this, s, i), 8);
  return Status::OK();
}

Status Tuple::GetFloat(const Schema& s, size_t i, float* out) const {
  if (!out) return Status::InvalidArgument("GetFloat: out=null");
  if (IsNull(s, i)) return Status::NotFound("GetFloat: NULL");
  if (s.GetColumn(i).type != Type::FLOAT) return Status::InvalidArgument("type mismatch");
  std::memcpy(out, FixedPtr(*this, s, i), 4);
  return Status::OK();
}

Status Tuple::GetDouble(const Schema& s, size_t i, double* out) const {
  if (!out) return Status::InvalidArgument("GetDouble: out=null");
  if (IsNull(s, i)) return Status::NotFound("GetDouble: NULL");
  if (s.GetColumn(i).type != Type::DOUBLE) return Status::InvalidArgument("type mismatch");
  std::memcpy(out, FixedPtr(*this, s, i), 8);
  return Status::OK();
}

Status Tuple::GetDate(const Schema& s, size_t i, int32_t* out_days) const {
  if (!out_days) return Status::InvalidArgument("GetDate: out=null");
  if (IsNull(s, i)) return Status::NotFound("GetDate: NULL");
  if (s.GetColumn(i).type != Type::DATE) return Status::InvalidArgument("type mismatch");
  std::memcpy(out_days, FixedPtr(*this, s, i), 4);
  return Status::OK();
}

Status Tuple::GetChar(const Schema& s, size_t i, std::string* out) const {
  if (!out) return Status::InvalidArgument("GetChar: out=null");
  if (IsNull(s, i)) return Status::NotFound("GetChar: NULL");
  if (s.GetColumn(i).type != Type::CHAR) return Status::InvalidArgument("type mismatch");
  const size_t n = s.FixedSizeOf(i);
  const char* p  = reinterpret_cast<const char*>(FixedPtr(*this, s, i));
  size_t real = n;
  while (real > 0 && p[real - 1] == '\0') --real;
  out->assign(p, p + real);
  return Status::OK();
}

Status Tuple::GetVarChar(const Schema& s, size_t i, std::string* out) const {
  if (!out) return Status::InvalidArgument("GetVarChar: out=null");
  if (IsNull(s, i)) return Status::NotFound("GetVarChar: NULL");
  if (s.GetColumn(i).type != Type::VARCHAR) return Status::InvalidArgument("type mismatch");
  const std::uint8_t* meta = reinterpret_cast<const std::uint8_t*>(FixedPtr(*this, s, i));
  uint16_t off = 0, len = 0;
  std::memcpy(&off, meta + 0, sizeof(uint16_t));
  std::memcpy(&len, meta + 2, sizeof(uint16_t));
  if (off + len > data_.size()) return Status::Corruption("varchar offset/len out of range");
  out->assign(reinterpret_cast<const char*>(data_.data() + off), len);
  return Status::OK();
}

// ================= TupleBuilder =================

TupleBuilder::TupleBuilder(const Schema& s) : s_(s) {
  row_.assign(s_.FixedAreaSize(), 0);
  set_.assign(s_.ColumnCount(), false);
}

void TupleBuilder::SetNullBit(size_t i) {
  if (!s_.UseNullBitmap()) return;
  const size_t byte = i / 8, bit = i % 8;
  row_[byte] |= static_cast<std::uint8_t>(1u << bit);
}

bool TupleBuilder::GetNullBit(size_t i) const {
  if (!s_.UseNullBitmap()) return false;
  const size_t byte = i / 8, bit = i % 8;
  return (row_[byte] >> bit) & 0x1;
}

void TupleBuilder::WriteFixed(size_t off, const void* src, size_t n) {
  std::memcpy(row_.data() + off, src, n);
}

void TupleBuilder::WriteVarMeta(size_t fixed_off, uint16_t off, uint16_t len) {
  std::memcpy(row_.data() + fixed_off + 0, &off, sizeof(uint16_t));
  std::memcpy(row_.data() + fixed_off + 2, &len, sizeof(uint16_t));
}

Status TupleBuilder::SetNull(size_t i) {
  if (i >= s_.ColumnCount()) return Status::OutOfRange("SetNull: index OOR");
  if (!s_.GetColumn(i).nullable && s_.UseNullBitmap())
    return Status::InvalidArgument("SetNull: column not nullable");
  if (s_.UseNullBitmap()) {
    SetNullBit(i);
    set_[i] = true;
  } else {
    return Status::InvalidArgument("SetNull: null-bitmap disabled");
  }
  return Status::OK();
}

Status TupleBuilder::SetInt32(size_t i, int32_t v) {
  if (i >= s_.ColumnCount()) return Status::OutOfRange("SetInt32: index OOR");
  if (s_.GetColumn(i).type != Type::INT32) return Status::InvalidArgument("type mismatch");
  WriteFixed(s_.FixedOffsetOf(i), &v, sizeof(v));
  set_[i] = true; return Status::OK();
}

Status TupleBuilder::SetInt64(size_t i, int64_t v) {
  if (i >= s_.ColumnCount()) return Status::OutOfRange("SetInt64: index OOR");
  if (s_.GetColumn(i).type != Type::INT64) return Status::InvalidArgument("type mismatch");
  WriteFixed(s_.FixedOffsetOf(i), &v, sizeof(v));
  set_[i] = true; return Status::OK();
}

Status TupleBuilder::SetFloat(size_t i, float v) {
  if (i >= s_.ColumnCount()) return Status::OutOfRange("SetFloat: index OOR");
  if (s_.GetColumn(i).type != Type::FLOAT) return Status::InvalidArgument("type mismatch");
  WriteFixed(s_.FixedOffsetOf(i), &v, sizeof(v));
  set_[i] = true; return Status::OK();
}

Status TupleBuilder::SetDouble(size_t i, double v) {
  if (i >= s_.ColumnCount()) return Status::OutOfRange("SetDouble: index OOR");
  if (s_.GetColumn(i).type != Type::DOUBLE) return Status::InvalidArgument("type mismatch");
  WriteFixed(s_.FixedOffsetOf(i), &v, sizeof(v));
  set_[i] = true; return Status::OK();
}

Status TupleBuilder::SetDate(size_t i, int32_t days) {
  if (i >= s_.ColumnCount()) return Status::OutOfRange("SetDate: index OOR");
  if (s_.GetColumn(i).type != Type::DATE) return Status::InvalidArgument("type mismatch");
  WriteFixed(s_.FixedOffsetOf(i), &days, sizeof(days));
  set_[i] = true; return Status::OK();
}

Status TupleBuilder::SetChar(size_t i, std::string_view v) {
  if (i >= s_.ColumnCount()) return Status::OutOfRange("SetChar: index OOR");
  const auto& c = s_.GetColumn(i);
  if (c.type != Type::CHAR) return Status::InvalidArgument("type mismatch");
  const size_t N = s_.FixedSizeOf(i);
  std::vector<char> buf(N, '\0');
  const size_t copy = std::min(N, v.size());
  std::memcpy(buf.data(), v.data(), copy);
  WriteFixed(s_.FixedOffsetOf(i), buf.data(), N);
  set_[i] = true; return Status::OK();
}

Status TupleBuilder::SetVarChar(size_t i, std::string_view v) {
  if (i >= s_.ColumnCount()) return Status::OutOfRange("SetVarChar: index OOR");
  const auto& c = s_.GetColumn(i);
  if (c.type != Type::VARCHAR) return Status::InvalidArgument("type mismatch");
  if (v.size() > c.len) return Status::OutOfRange("varchar exceeds max length");

  const uint16_t off = static_cast<uint16_t>(s_.FixedAreaSize() + var_.size());
  const uint16_t len = static_cast<uint16_t>(v.size());
  WriteVarMeta(s_.FixedOffsetOf(i), off, len);
  var_.insert(var_.end(), v.begin(), v.end());

  set_[i] = true; return Status::OK();
}

Status TupleBuilder::Build(Tuple* out) {
  if (!out) return Status::InvalidArgument("Build: out=null");
  for (size_t i = 0; i < s_.ColumnCount(); ++i) {
    if (!set_[i]) return Status::InvalidArgument("column not set: idx=" + std::to_string(i));
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(s_.FixedAreaSize() + var_.size());
  bytes.insert(bytes.end(), row_.begin(), row_.end());
  bytes.insert(bytes.end(), var_.begin(), var_.end());
  *out = Tuple(std::move(bytes));
  return Status::OK();
}

}  // namespace storage
}  // namespace dbms
