//===-- Scalar.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"

#include <cinttypes>
#include <cstdio>

using namespace lldb;
using namespace lldb_private;

using llvm::APFloat;
using llvm::APInt;
using llvm::APSInt;

Scalar::PromotionKey Scalar::GetPromoKey() const {
  switch (m_type) {
  case e_void:
    return PromotionKey{e_void, 0, false};
  case e_int:
    return PromotionKey{e_int, m_integer.getBitWidth(), m_integer.isUnsigned()};
  case e_float:
    return GetFloatPromoKey(m_float.getSemantics());
  }
  llvm_unreachable("Unhandled category!");
}

Scalar::PromotionKey Scalar::GetFloatPromoKey(const llvm::fltSemantics &sem) {
  static const llvm::fltSemantics *const order[] = {
      &APFloat::IEEEsingle(), &APFloat::IEEEdouble(),
      &APFloat::x87DoubleExtended()};
  for (const auto &entry : llvm::enumerate(order)) {
    if (entry.value() == &sem)
      return PromotionKey{e_float, entry.index(), false};
  }
  llvm_unreachable("Unsupported semantics!");
}

// Promote to max type currently follows the ANSI C rule for type promotion in
// expressions.
Scalar::Type Scalar::PromoteToMaxType(Scalar &lhs, Scalar &rhs) {
  const auto &Promote = [](Scalar &a, const Scalar &b) {
    switch (b.GetType()) {
    case e_void:
      break;
    case e_int:
      a.IntegralPromote(b.m_integer.getBitWidth(), b.m_integer.isSigned());
      break;
    case e_float:
      a.FloatPromote(b.m_float.getSemantics());
    }
  };

  PromotionKey lhs_key = lhs.GetPromoKey();
  PromotionKey rhs_key = rhs.GetPromoKey();

  if (lhs_key > rhs_key)
    Promote(rhs, lhs);
  else if (rhs_key > lhs_key)
    Promote(lhs, rhs);

  // Make sure our type promotion worked as expected
  if (lhs.GetPromoKey() == rhs.GetPromoKey())
    return lhs.GetType(); // Return the resulting type

  // Return the void type (zero) if we fail to promote either of the values.
  return Scalar::e_void;
}

bool Scalar::GetData(DataExtractor &data, size_t limit_byte_size) const {
  size_t byte_size = GetByteSize();
  if (byte_size == 0) {
    data.Clear();
    return false;
  }
  auto buffer_up = std::make_unique<DataBufferHeap>(byte_size, 0);
  GetBytes(buffer_up->GetData());
  lldb::offset_t offset = 0;

  if (limit_byte_size < byte_size) {
    if (endian::InlHostByteOrder() == eByteOrderLittle) {
      // On little endian systems if we want fewer bytes from the current
      // type we just specify fewer bytes since the LSByte is first...
      byte_size = limit_byte_size;
    } else if (endian::InlHostByteOrder() == eByteOrderBig) {
      // On big endian systems if we want fewer bytes from the current type
      // have to advance our initial byte pointer and trim down the number of
      // bytes since the MSByte is first
      offset = byte_size - limit_byte_size;
      byte_size = limit_byte_size;
    }
  }

  data.SetData(std::move(buffer_up), offset, byte_size);
  data.SetByteOrder(endian::InlHostByteOrder());
  return true;
}

void Scalar::GetBytes(llvm::MutableArrayRef<uint8_t> storage) const {
  assert(storage.size() >= GetByteSize());

  const auto &store = [&](const llvm::APInt &val) {
    StoreIntToMemory(val, storage.data(), (val.getBitWidth() + 7) / 8);
  };
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    store(m_integer);
    break;
  case e_float:
    store(m_float.bitcastToAPInt());
    break;
  }
}

size_t Scalar::GetByteSize() const {
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    return (m_integer.getBitWidth() + 7) / 8;
  case e_float:
    return (m_float.bitcastToAPInt().getBitWidth() + 7) / 8;
  }
  return 0;
}

bool Scalar::IsZero() const {
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    return m_integer.isZero();
  case e_float:
    return m_float.isZero();
  }
  return false;
}

void Scalar::GetValue(Stream &s, bool show_type) const {
  if (show_type)
    s.Printf("(%s) ", GetTypeAsCString());

  switch (m_type) {
  case e_void:
    break;
  case e_int:
    s.PutCString(llvm::toString(m_integer, 10));
    break;
  case e_float:
    llvm::SmallString<24> string;
    m_float.toString(string);
    s.PutCString(string);
    break;
  }
}

void Scalar::TruncOrExtendTo(uint16_t bits, bool sign) {
  m_integer.setIsSigned(sign);
  m_integer = m_integer.extOrTrunc(bits);
}

bool Scalar::IntegralPromote(uint16_t bits, bool sign) {
  switch (m_type) {
  case e_void:
  case e_float:
    break;
  case e_int:
    if (GetPromoKey() > PromotionKey(e_int, bits, !sign))
      break;
    m_integer = m_integer.extOrTrunc(bits);
    m_integer.setIsSigned(sign);
    return true;
  }
  return false;
}

bool Scalar::FloatPromote(const llvm::fltSemantics &semantics) {
  bool success = false;
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    m_float = llvm::APFloat(semantics);
    m_float.convertFromAPInt(m_integer, m_integer.isSigned(),
                             llvm::APFloat::rmNearestTiesToEven);
    success = true;
    break;
  case e_float:
    if (GetFloatPromoKey(semantics) < GetFloatPromoKey(m_float.getSemantics()))
      break;
    bool ignore;
    success = true;
    m_float.convert(semantics, llvm::APFloat::rmNearestTiesToEven, &ignore);
  }

  if (success)
    m_type = e_float;
  return success;
}

const char *Scalar::GetValueTypeAsCString(Scalar::Type type) {
  switch (type) {
  case e_void:
    return "void";
  case e_int:
    return "int";
  case e_float:
    return "float";
  }
  return "???";
}

bool Scalar::IsSigned() const {
  switch (m_type) {
  case e_void:
    return false;
  case e_int:
    return m_integer.isSigned();
  case e_float:
    return true;
  }
  llvm_unreachable("Unrecognized type!");
}

bool Scalar::MakeSigned() {
  bool success = false;

  switch (m_type) {
  case e_void:
    break;
  case e_int:
    m_integer.setIsSigned(true);
    success = true;
    break;
  case e_float:
    success = true;
    break;
  }

  return success;
}

bool Scalar::MakeUnsigned() {
  bool success = false;

  switch (m_type) {
  case e_void:
    break;
  case e_int:
    m_integer.setIsUnsigned(true);
    success = true;
    break;
  case e_float:
    success = true;
    break;
  }

  return success;
}

static llvm::APInt ToAPInt(const llvm::APFloat &f, unsigned bits,
                           bool is_unsigned) {
  llvm::APSInt result(bits, is_unsigned);
  bool isExact;
  f.convertToInteger(result, llvm::APFloat::rmTowardZero, &isExact);
  return std::move(result);
}

template <typename T> T Scalar::GetAs(T fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_int: {
    APSInt ext = m_integer.extOrTrunc(sizeof(T) * 8);
    if (ext.isSigned())
      return ext.getSExtValue();
    return ext.getZExtValue();
  }
  case e_float:
    return ToAPInt(m_float, sizeof(T) * 8, std::is_unsigned<T>::value)
        .getSExtValue();
  }
  return fail_value;
}

signed char Scalar::SChar(signed char fail_value) const {
  return GetAs<signed char>(fail_value);
}

unsigned char Scalar::UChar(unsigned char fail_value) const {
  return GetAs<unsigned char>(fail_value);
}

short Scalar::SShort(short fail_value) const {
  return GetAs<short>(fail_value);
}

unsigned short Scalar::UShort(unsigned short fail_value) const {
  return GetAs<unsigned short>(fail_value);
}

int Scalar::SInt(int fail_value) const { return GetAs<int>(fail_value); }

unsigned int Scalar::UInt(unsigned int fail_value) const {
  return GetAs<unsigned int>(fail_value);
}

long Scalar::SLong(long fail_value) const { return GetAs<long>(fail_value); }

unsigned long Scalar::ULong(unsigned long fail_value) const {
  return GetAs<unsigned long>(fail_value);
}

long long Scalar::SLongLong(long long fail_value) const {
  return GetAs<long long>(fail_value);
}

unsigned long long Scalar::ULongLong(unsigned long long fail_value) const {
  return GetAs<unsigned long long>(fail_value);
}

llvm::APInt Scalar::SInt128(const llvm::APInt &fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    return m_integer;
  case e_float:
    return ToAPInt(m_float, 128, /*is_unsigned=*/false);
  }
  return fail_value;
}

llvm::APInt Scalar::UInt128(const llvm::APInt &fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    return m_integer;
  case e_float:
    return ToAPInt(m_float, 128, /*is_unsigned=*/true);
  }
  return fail_value;
}

float Scalar::Float(float fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    if (m_integer.isSigned())
      return llvm::APIntOps::RoundSignedAPIntToFloat(m_integer);
    return llvm::APIntOps::RoundAPIntToFloat(m_integer);

  case e_float: {
    APFloat result = m_float;
    bool losesInfo;
    result.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven,
                   &losesInfo);
    return result.convertToFloat();
  }
  }
  return fail_value;
}

double Scalar::Double(double fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    if (m_integer.isSigned())
      return llvm::APIntOps::RoundSignedAPIntToDouble(m_integer);
    return llvm::APIntOps::RoundAPIntToDouble(m_integer);

  case e_float: {
    APFloat result = m_float;
    bool losesInfo;
    result.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven,
                   &losesInfo);
    return result.convertToDouble();
  }
  }
  return fail_value;
}

long double Scalar::LongDouble(long double fail_value) const {
  /// No way to get more precision at the moment.
  return static_cast<long double>(Double(fail_value));
}

Scalar &Scalar::operator+=(Scalar rhs) {
  Scalar copy = *this;
  if ((m_type = PromoteToMaxType(copy, rhs)) != Scalar::e_void) {
    switch (m_type) {
    case e_void:
      break;
    case e_int:
      m_integer = copy.m_integer + rhs.m_integer;
      break;

    case e_float:
      m_float = copy.m_float + rhs.m_float;
      break;
    }
  }
  return *this;
}

Scalar &Scalar::operator<<=(const Scalar &rhs) {
  if (m_type == e_int && rhs.m_type == e_int)
    static_cast<APInt &>(m_integer) <<= rhs.m_integer;
  else
    m_type = e_void;
  return *this;
}

bool Scalar::ShiftRightLogical(const Scalar &rhs) {
  if (m_type == e_int && rhs.m_type == e_int) {
    m_integer = m_integer.lshr(rhs.m_integer);
    return true;
  }
  m_type = e_void;
  return false;
}

Scalar &Scalar::operator>>=(const Scalar &rhs) {
  switch (m_type) {
  case e_void:
  case e_float:
    m_type = e_void;
    break;

  case e_int:
    switch (rhs.m_type) {
    case e_void:
    case e_float:
      m_type = e_void;
      break;
    case e_int:
      m_integer = m_integer.ashr(rhs.m_integer);
      break;
    }
    break;
  }
  return *this;
}

Scalar &Scalar::operator&=(const Scalar &rhs) {
  if (m_type == e_int && rhs.m_type == e_int)
    m_integer &= rhs.m_integer;
  else
    m_type = e_void;
  return *this;
}

bool Scalar::AbsoluteValue() {
  switch (m_type) {
  case e_void:
    break;

  case e_int:
    if (m_integer.isNegative())
      m_integer = -m_integer;
    return true;

  case e_float:
    m_float.clearSign();
    return true;
  }
  return false;
}

bool Scalar::UnaryNegate() {
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    m_integer = -m_integer;
    return true;
  case e_float:
    m_float.changeSign();
    return true;
  }
  return false;
}

bool Scalar::OnesComplement() {
  if (m_type == e_int) {
    m_integer = ~m_integer;
    return true;
  }

  return false;
}

const Scalar lldb_private::operator+(const Scalar &lhs, const Scalar &rhs) {
  Scalar result = lhs;
  result += rhs;
  return result;
}

const Scalar lldb_private::operator-(Scalar lhs, Scalar rhs) {
  Scalar result;
  if ((result.m_type = Scalar::PromoteToMaxType(lhs, rhs)) != Scalar::e_void) {
    switch (result.m_type) {
    case Scalar::e_void:
      break;
    case Scalar::e_int:
      result.m_integer = lhs.m_integer - rhs.m_integer;
      break;
    case Scalar::e_float:
      result.m_float = lhs.m_float - rhs.m_float;
      break;
    }
  }
  return result;
}

const Scalar lldb_private::operator/(Scalar lhs, Scalar rhs) {
  Scalar result;
  if ((result.m_type = Scalar::PromoteToMaxType(lhs, rhs)) != Scalar::e_void &&
      !rhs.IsZero()) {
    switch (result.m_type) {
    case Scalar::e_void:
      break;
    case Scalar::e_int:
      result.m_integer = lhs.m_integer / rhs.m_integer;
      return result;
    case Scalar::e_float:
      result.m_float = lhs.m_float / rhs.m_float;
      return result;
    }
  }
  // For division only, the only way it should make it here is if a promotion
  // failed, or if we are trying to do a divide by zero.
  result.m_type = Scalar::e_void;
  return result;
}

const Scalar lldb_private::operator*(Scalar lhs, Scalar rhs) {
  Scalar result;
  if ((result.m_type = Scalar::PromoteToMaxType(lhs, rhs)) != Scalar::e_void) {
    switch (result.m_type) {
    case Scalar::e_void:
      break;
    case Scalar::e_int:
      result.m_integer = lhs.m_integer * rhs.m_integer;
      break;
    case Scalar::e_float:
      result.m_float = lhs.m_float * rhs.m_float;
      break;
    }
  }
  return result;
}

const Scalar lldb_private::operator&(Scalar lhs, Scalar rhs) {
  Scalar result;
  if ((result.m_type = Scalar::PromoteToMaxType(lhs, rhs)) != Scalar::e_void) {
    if (result.m_type == Scalar::e_int)
      result.m_integer = lhs.m_integer & rhs.m_integer;
    else
      result.m_type = Scalar::e_void;
  }
  return result;
}

const Scalar lldb_private::operator|(Scalar lhs, Scalar rhs) {
  Scalar result;
  if ((result.m_type = Scalar::PromoteToMaxType(lhs, rhs)) != Scalar::e_void) {
    if (result.m_type == Scalar::e_int)
      result.m_integer = lhs.m_integer | rhs.m_integer;
    else
      result.m_type = Scalar::e_void;
  }
  return result;
}

const Scalar lldb_private::operator%(Scalar lhs, Scalar rhs) {
  Scalar result;
  if ((result.m_type = Scalar::PromoteToMaxType(lhs, rhs)) != Scalar::e_void) {
    if (!rhs.IsZero() && result.m_type == Scalar::e_int) {
      result.m_integer = lhs.m_integer % rhs.m_integer;
      return result;
    }
  }
  result.m_type = Scalar::e_void;
  return result;
}

const Scalar lldb_private::operator^(Scalar lhs, Scalar rhs) {
  Scalar result;
  if ((result.m_type = Scalar::PromoteToMaxType(lhs, rhs)) != Scalar::e_void) {
    if (result.m_type == Scalar::e_int)
      result.m_integer = lhs.m_integer ^ rhs.m_integer;
    else
      result.m_type = Scalar::e_void;
  }
  return result;
}

const Scalar lldb_private::operator<<(const Scalar &lhs, const Scalar &rhs) {
  Scalar result = lhs;
  result <<= rhs;
  return result;
}

const Scalar lldb_private::operator>>(const Scalar &lhs, const Scalar &rhs) {
  Scalar result = lhs;
  result >>= rhs;
  return result;
}

Status Scalar::SetValueFromCString(const char *value_str, Encoding encoding,
                                   size_t byte_size) {
  Status error;
  if (value_str == nullptr || value_str[0] == '\0') {
    error.SetErrorString("Invalid c-string value string.");
    return error;
  }
  switch (encoding) {
  case eEncodingInvalid:
    error.SetErrorString("Invalid encoding.");
    break;

  case eEncodingSint:
  case eEncodingUint: {
    llvm::StringRef str = value_str;
    bool is_signed = encoding == eEncodingSint;
    bool is_negative = is_signed && str.consume_front("-");
    APInt integer;
    if (str.getAsInteger(0, integer)) {
      error.SetErrorStringWithFormatv(
          "'{0}' is not a valid integer string value", value_str);
      break;
    }
    bool fits;
    if (is_signed) {
      integer = integer.zext(integer.getBitWidth() + 1);
      if (is_negative)
        integer.negate();
      fits = integer.isSignedIntN(byte_size * 8);
    } else
      fits = integer.isIntN(byte_size * 8);
    if (!fits) {
      error.SetErrorStringWithFormatv(
          "value {0} is too large to fit in a {1} byte integer value",
          value_str, byte_size);
      break;
    }
    m_type = e_int;
    m_integer =
        APSInt(std::move(integer), !is_signed).extOrTrunc(8 * byte_size);
    break;
  }

  case eEncodingIEEE754: {
    // FIXME: It's not possible to unambiguously map a byte size to a floating
    // point type. This function should be refactored to take an explicit
    // semantics argument.
    const llvm::fltSemantics &sem =
        byte_size <= 4 ? APFloat::IEEEsingle()
                       : byte_size <= 8 ? APFloat::IEEEdouble()
                                        : APFloat::x87DoubleExtended();
    APFloat f(sem);
    if (llvm::Expected<APFloat::opStatus> op =
            f.convertFromString(value_str, APFloat::rmNearestTiesToEven)) {
      m_type = e_float;
      m_float = std::move(f);
    } else
      error = op.takeError();
    break;
  }

  case eEncodingVector:
    error.SetErrorString("vector encoding unsupported.");
    break;
  }
  if (error.Fail())
    m_type = e_void;

  return error;
}

Status Scalar::SetValueFromData(const DataExtractor &data,
                                lldb::Encoding encoding, size_t byte_size) {
  Status error;
  switch (encoding) {
  case lldb::eEncodingInvalid:
    error.SetErrorString("invalid encoding");
    break;
  case lldb::eEncodingVector:
    error.SetErrorString("vector encoding unsupported");
    break;
  case lldb::eEncodingUint:
  case lldb::eEncodingSint: {
    if (data.GetByteSize() < byte_size)
      return Status("insufficient data");
    m_type = e_int;
    m_integer =
        APSInt(APInt::getZero(8 * byte_size), encoding == eEncodingUint);
    if (data.GetByteOrder() == endian::InlHostByteOrder()) {
      llvm::LoadIntFromMemory(m_integer, data.GetDataStart(), byte_size);
    } else {
      std::vector<uint8_t> buffer(byte_size);
      std::copy_n(data.GetDataStart(), byte_size, buffer.rbegin());
      llvm::LoadIntFromMemory(m_integer, buffer.data(), byte_size);
    }
    break;
  }
  case lldb::eEncodingIEEE754: {
    lldb::offset_t offset = 0;

    if (byte_size == sizeof(float))
      operator=(data.GetFloat(&offset));
    else if (byte_size == sizeof(double))
      operator=(data.GetDouble(&offset));
    else if (byte_size == sizeof(long double))
      operator=(data.GetLongDouble(&offset));
    else
      error.SetErrorStringWithFormat("unsupported float byte size: %" PRIu64 "",
                                     static_cast<uint64_t>(byte_size));
  } break;
  }

  return error;
}

bool Scalar::SignExtend(uint32_t sign_bit_pos) {
  const uint32_t max_bit_pos = GetByteSize() * 8;

  if (sign_bit_pos < max_bit_pos) {
    switch (m_type) {
    case Scalar::e_void:
    case Scalar::e_float:
      return false;

    case Scalar::e_int:
      if (sign_bit_pos < (max_bit_pos - 1)) {
        llvm::APInt sign_bit = llvm::APInt::getSignMask(sign_bit_pos + 1);
        llvm::APInt bitwize_and = m_integer & sign_bit;
        if (bitwize_and.getBoolValue()) {
          llvm::APInt mask =
              ~(sign_bit) + llvm::APInt(m_integer.getBitWidth(), 1);
          m_integer |= APSInt(std::move(mask), m_integer.isUnsigned());
        }
        return true;
      }
      break;
    }
  }
  return false;
}

size_t Scalar::GetAsMemoryData(void *dst, size_t dst_len,
                               lldb::ByteOrder dst_byte_order,
                               Status &error) const {
  // Get a data extractor that points to the native scalar data
  DataExtractor data;
  if (!GetData(data)) {
    error.SetErrorString("invalid scalar value");
    return 0;
  }

  const size_t src_len = data.GetByteSize();

  // Prepare a memory buffer that contains some or all of the register value
  const size_t bytes_copied =
      data.CopyByteOrderedData(0,               // src offset
                               src_len,         // src length
                               dst,             // dst buffer
                               dst_len,         // dst length
                               dst_byte_order); // dst byte order
  if (bytes_copied == 0)
    error.SetErrorString("failed to copy data");

  return bytes_copied;
}

bool Scalar::ExtractBitfield(uint32_t bit_size, uint32_t bit_offset) {
  if (bit_size == 0)
    return true;

  switch (m_type) {
  case Scalar::e_void:
  case Scalar::e_float:
    break;

  case Scalar::e_int:
    m_integer >>= bit_offset;
    m_integer = m_integer.extOrTrunc(bit_size).extOrTrunc(8 * GetByteSize());
    return true;
  }
  return false;
}

llvm::APFloat Scalar::CreateAPFloatFromAPSInt(lldb::BasicType basic_type) {
  switch (basic_type) {
  case lldb::eBasicTypeFloat:
    return llvm::APFloat(
        m_integer.isSigned()
            ? llvm::APIntOps::RoundSignedAPIntToFloat(m_integer)
            : llvm::APIntOps::RoundAPIntToFloat(m_integer));
  case lldb::eBasicTypeDouble:
    // No way to get more precision at the moment.
  case lldb::eBasicTypeLongDouble:
    return llvm::APFloat(
        m_integer.isSigned()
            ? llvm::APIntOps::RoundSignedAPIntToDouble(m_integer)
            : llvm::APIntOps::RoundAPIntToDouble(m_integer));
  default:
    const llvm::fltSemantics &sem = APFloat::IEEEsingle();
    return llvm::APFloat::getNaN(sem);
  }
}

llvm::APFloat Scalar::CreateAPFloatFromAPFloat(lldb::BasicType basic_type) {
  switch (basic_type) {
  case lldb::eBasicTypeFloat: {
    bool loses_info;
    m_float.convert(llvm::APFloat::IEEEsingle(),
                    llvm::APFloat::rmNearestTiesToEven, &loses_info);
    return m_float;
  }
  case lldb::eBasicTypeDouble:
    // No way to get more precision at the moment.
  case lldb::eBasicTypeLongDouble: {
    bool loses_info;
    m_float.convert(llvm::APFloat::IEEEdouble(),
                    llvm::APFloat::rmNearestTiesToEven, &loses_info);
    return m_float;
  }
  default:
    const llvm::fltSemantics &sem = APFloat::IEEEsingle();
    return llvm::APFloat::getNaN(sem);
  }
}

bool lldb_private::operator==(Scalar lhs, Scalar rhs) {
  // If either entry is void then we can just compare the types
  if (lhs.m_type == Scalar::e_void || rhs.m_type == Scalar::e_void)
    return lhs.m_type == rhs.m_type;

  llvm::APFloat::cmpResult result;
  switch (Scalar::PromoteToMaxType(lhs, rhs)) {
  case Scalar::e_void:
    break;
  case Scalar::e_int:
    return lhs.m_integer == rhs.m_integer;
  case Scalar::e_float:
    result = lhs.m_float.compare(rhs.m_float);
    if (result == llvm::APFloat::cmpEqual)
      return true;
  }
  return false;
}

bool lldb_private::operator!=(const Scalar &lhs, const Scalar &rhs) {
  return !(lhs == rhs);
}

bool lldb_private::operator<(Scalar lhs, Scalar rhs) {
  if (lhs.m_type == Scalar::e_void || rhs.m_type == Scalar::e_void)
    return false;

  llvm::APFloat::cmpResult result;
  switch (Scalar::PromoteToMaxType(lhs, rhs)) {
  case Scalar::e_void:
    break;
  case Scalar::e_int:
    return lhs.m_integer < rhs.m_integer;
  case Scalar::e_float:
    result = lhs.m_float.compare(rhs.m_float);
    if (result == llvm::APFloat::cmpLessThan)
      return true;
  }
  return false;
}

bool lldb_private::operator<=(const Scalar &lhs, const Scalar &rhs) {
  return !(rhs < lhs);
}

bool lldb_private::operator>(const Scalar &lhs, const Scalar &rhs) {
  return rhs < lhs;
}

bool lldb_private::operator>=(const Scalar &lhs, const Scalar &rhs) {
  return !(lhs < rhs);
}

bool Scalar::ClearBit(uint32_t bit) {
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    m_integer.clearBit(bit);
    return true;
  case e_float:
    break;
  }
  return false;
}

bool Scalar::SetBit(uint32_t bit) {
  switch (m_type) {
  case e_void:
    break;
  case e_int:
    m_integer.setBit(bit);
    return true;
  case e_float:
    break;
  }
  return false;
}

llvm::raw_ostream &lldb_private::operator<<(llvm::raw_ostream &os, const Scalar &scalar) {
  StreamString s;
  scalar.GetValue(s, /*show_type*/ true);
  return os << s.GetString();
}
