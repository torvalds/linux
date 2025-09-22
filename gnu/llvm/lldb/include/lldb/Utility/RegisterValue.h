//===-- RegisterValue.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_REGISTERVALUE_H
#define LLDB_UTILITY_REGISTERVALUE_H

#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <cstring>
#include <utility>

namespace lldb_private {
class DataExtractor;
class Stream;
struct RegisterInfo;

class RegisterValue {
public:
  enum {
    // What we can reasonably put on the stack, big enough to support up to 256
    // byte AArch64 SVE.
    kTypicalRegisterByteSize = 256u,
    // Anything else we'll heap allocate storage for it.
    // 256x256 to support 256 byte AArch64 SME's array storage (ZA) register.
    // Which is a square of vector length x vector length.
    kMaxRegisterByteSize = 256u * 256u,
  };

  typedef llvm::SmallVector<uint8_t, kTypicalRegisterByteSize> BytesContainer;

  enum Type {
    eTypeInvalid,
    eTypeUInt8,
    eTypeUInt16,
    eTypeUInt32,
    eTypeUInt64,
    eTypeUInt128,
    eTypeFloat,
    eTypeDouble,
    eTypeLongDouble,
    eTypeBytes
  };

  RegisterValue() : m_scalar(static_cast<unsigned long>(0)) {}

  explicit RegisterValue(uint8_t inst) : m_type(eTypeUInt8) { m_scalar = inst; }

  explicit RegisterValue(uint16_t inst) : m_type(eTypeUInt16) {
    m_scalar = inst;
  }

  explicit RegisterValue(uint32_t inst) : m_type(eTypeUInt32) {
    m_scalar = inst;
  }

  explicit RegisterValue(uint64_t inst) : m_type(eTypeUInt64) {
    m_scalar = inst;
  }

  explicit RegisterValue(llvm::APInt inst) : m_type(eTypeUInt128) {
    m_scalar = llvm::APInt(std::move(inst));
  }

  explicit RegisterValue(float value) : m_type(eTypeFloat) { m_scalar = value; }

  explicit RegisterValue(double value) : m_type(eTypeDouble) {
    m_scalar = value;
  }

  explicit RegisterValue(long double value) : m_type(eTypeLongDouble) {
    m_scalar = value;
  }

  explicit RegisterValue(llvm::ArrayRef<uint8_t> bytes,
                         lldb::ByteOrder byte_order) {
    SetBytes(bytes.data(), bytes.size(), byte_order);
  }

  RegisterValue::Type GetType() const { return m_type; }

  bool CopyValue(const RegisterValue &rhs);

  void SetType(RegisterValue::Type type) { m_type = type; }

  RegisterValue::Type SetType(const RegisterInfo &reg_info);

  bool GetData(DataExtractor &data) const;

  // Copy the register value from this object into a buffer in "dst" and obey
  // the "dst_byte_order" when copying the data. Also watch out in case
  // "dst_len" is longer or shorter than the register value described by
  // "reg_info" and only copy the least significant bytes of the register
  // value, or pad the destination with zeroes if the register byte size is
  // shorter that "dst_len" (all while correctly abiding the "dst_byte_order").
  // Returns the number of bytes copied into "dst".
  uint32_t GetAsMemoryData(const RegisterInfo &reg_info, void *dst,
                           uint32_t dst_len, lldb::ByteOrder dst_byte_order,
                           Status &error) const;

  uint32_t SetFromMemoryData(const RegisterInfo &reg_info, const void *src,
                             uint32_t src_len, lldb::ByteOrder src_byte_order,
                             Status &error);

  bool GetScalarValue(Scalar &scalar) const;

  uint8_t GetAsUInt8(uint8_t fail_value = UINT8_MAX,
                     bool *success_ptr = nullptr) const {
    if (m_type == eTypeUInt8) {
      if (success_ptr)
        *success_ptr = true;
      return m_scalar.UChar(fail_value);
    }
    if (success_ptr)
      *success_ptr = true;
    return fail_value;
  }

  uint16_t GetAsUInt16(uint16_t fail_value = UINT16_MAX,
                       bool *success_ptr = nullptr) const;

  uint32_t GetAsUInt32(uint32_t fail_value = UINT32_MAX,
                       bool *success_ptr = nullptr) const;

  uint64_t GetAsUInt64(uint64_t fail_value = UINT64_MAX,
                       bool *success_ptr = nullptr) const;

  llvm::APInt GetAsUInt128(const llvm::APInt &fail_value,
                           bool *success_ptr = nullptr) const;

  float GetAsFloat(float fail_value = 0.0f, bool *success_ptr = nullptr) const;

  double GetAsDouble(double fail_value = 0.0,
                     bool *success_ptr = nullptr) const;

  long double GetAsLongDouble(long double fail_value = 0.0,
                              bool *success_ptr = nullptr) const;

  void SetValueToInvalid() { m_type = eTypeInvalid; }

  bool ClearBit(uint32_t bit);

  bool SetBit(uint32_t bit);

  bool operator==(const RegisterValue &rhs) const;

  bool operator!=(const RegisterValue &rhs) const;

  void operator=(uint8_t uint) {
    m_type = eTypeUInt8;
    m_scalar = uint;
  }

  void operator=(uint16_t uint) {
    m_type = eTypeUInt16;
    m_scalar = uint;
  }

  void operator=(uint32_t uint) {
    m_type = eTypeUInt32;
    m_scalar = uint;
  }

  void operator=(uint64_t uint) {
    m_type = eTypeUInt64;
    m_scalar = uint;
  }

  void operator=(llvm::APInt uint) {
    m_type = eTypeUInt128;
    m_scalar = llvm::APInt(std::move(uint));
  }

  void operator=(float f) {
    m_type = eTypeFloat;
    m_scalar = f;
  }

  void operator=(double f) {
    m_type = eTypeDouble;
    m_scalar = f;
  }

  void operator=(long double f) {
    m_type = eTypeLongDouble;
    m_scalar = f;
  }

  void SetUInt8(uint8_t uint) {
    m_type = eTypeUInt8;
    m_scalar = uint;
  }

  void SetUInt16(uint16_t uint) {
    m_type = eTypeUInt16;
    m_scalar = uint;
  }

  void SetUInt32(uint32_t uint, Type t = eTypeUInt32) {
    m_type = t;
    m_scalar = uint;
  }

  void SetUInt64(uint64_t uint, Type t = eTypeUInt64) {
    m_type = t;
    m_scalar = uint;
  }

  void SetUInt128(llvm::APInt uint) {
    m_type = eTypeUInt128;
    m_scalar = std::move(uint);
  }

  bool SetUInt(uint64_t uint, uint32_t byte_size);

  void SetFloat(float f) {
    m_type = eTypeFloat;
    m_scalar = f;
  }

  void SetDouble(double f) {
    m_type = eTypeDouble;
    m_scalar = f;
  }

  void SetLongDouble(long double f) {
    m_type = eTypeLongDouble;
    m_scalar = f;
  }

  void SetBytes(const void *bytes, size_t length, lldb::ByteOrder byte_order);

  bool SignExtend(uint32_t sign_bitpos);

  Status SetValueFromString(const RegisterInfo *reg_info,
                            llvm::StringRef value_str);
  Status SetValueFromString(const RegisterInfo *reg_info,
                            const char *value_str) = delete;

  Status SetValueFromData(const RegisterInfo &reg_info, DataExtractor &data,
                          lldb::offset_t offset, bool partial_data_ok);

  const void *GetBytes() const;

  lldb::ByteOrder GetByteOrder() const {
    if (m_type == eTypeBytes)
      return buffer.byte_order;
    return endian::InlHostByteOrder();
  }

  uint32_t GetByteSize() const;

  void Clear();

protected:
  RegisterValue::Type m_type = eTypeInvalid;
  Scalar m_scalar;

  struct RegisterValueBuffer {
    // Start at max stack storage size. Move to the heap for anything larger.
    RegisterValueBuffer() : bytes(kTypicalRegisterByteSize) {}

    mutable BytesContainer bytes;
    lldb::ByteOrder byte_order = lldb::eByteOrderInvalid;
  } buffer;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_REGISTERVALUE_H
