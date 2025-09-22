//===-- Scalar.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_SCALAR_H
#define LLDB_UTILITY_SCALAR_H

#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-types.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include <cstddef>
#include <cstdint>
#include <utility>

namespace lldb_private {

class DataExtractor;
class Stream;

#define NUM_OF_WORDS_INT128 2
#define BITWIDTH_INT128 128

// A class designed to hold onto values and their corresponding types.
// Operators are defined and Scalar objects will correctly promote their types
// and values before performing these operations. Type promotion currently
// follows the ANSI C type promotion rules.
class Scalar {
  template<typename T>
  static llvm::APSInt MakeAPSInt(T v) {
    static_assert(std::is_integral<T>::value);
    static_assert(sizeof(T) <= sizeof(uint64_t), "Conversion loses precision!");
    return llvm::APSInt(
        llvm::APInt(sizeof(T) * 8, uint64_t(v), std::is_signed<T>::value),
        std::is_unsigned<T>::value);
  }

public:
  enum Type {
    e_void = 0,
    e_int,
    e_float,
  };

  // Constructors and Destructors
  Scalar() : m_float(0.0f) {}
  Scalar(int v) : m_type(e_int), m_integer(MakeAPSInt(v)), m_float(0.0f) {}
  Scalar(unsigned int v)
      : m_type(e_int), m_integer(MakeAPSInt(v)), m_float(0.0f) {}
  Scalar(long v) : m_type(e_int), m_integer(MakeAPSInt(v)), m_float(0.0f) {}
  Scalar(unsigned long v)
      : m_type(e_int), m_integer(MakeAPSInt(v)), m_float(0.0f) {}
  Scalar(long long v)
      : m_type(e_int), m_integer(MakeAPSInt(v)), m_float(0.0f) {}
  Scalar(unsigned long long v)
      : m_type(e_int), m_integer(MakeAPSInt(v)), m_float(0.0f) {}
  Scalar(float v) : m_type(e_float), m_float(v) {}
  Scalar(double v) : m_type(e_float), m_float(v) {}
  Scalar(long double v) : m_type(e_float), m_float(double(v)) {
    bool ignore;
    m_float.convert(llvm::APFloat::x87DoubleExtended(),
                    llvm::APFloat::rmNearestTiesToEven, &ignore);
  }
  Scalar(llvm::APInt v)
      : m_type(e_int), m_integer(std::move(v), false), m_float(0.0f) {}
  Scalar(llvm::APSInt v)
      : m_type(e_int), m_integer(std::move(v)), m_float(0.0f) {}
  Scalar(llvm::APFloat v) : m_type(e_float), m_integer(0), m_float(v) {}

  bool SignExtend(uint32_t bit_pos);

  bool ExtractBitfield(uint32_t bit_size, uint32_t bit_offset);

  bool SetBit(uint32_t bit);

  bool ClearBit(uint32_t bit);

  /// Store the binary representation of this value into the given storage.
  /// Exactly GetByteSize() bytes will be stored, and the buffer must be large
  /// enough to hold this data.
  void GetBytes(llvm::MutableArrayRef<uint8_t> storage) const;

  size_t GetByteSize() const;

  bool GetData(DataExtractor &data, size_t limit_byte_size = UINT32_MAX) const;

  size_t GetAsMemoryData(void *dst, size_t dst_len,
                         lldb::ByteOrder dst_byte_order, Status &error) const;

  bool IsZero() const;

  void Clear() {
    m_type = e_void;
    m_integer.clearAllBits();
  }

  const char *GetTypeAsCString() const { return GetValueTypeAsCString(m_type); }

  void GetValue(Stream &s, bool show_type) const;

  bool IsValid() const { return (m_type >= e_int) && (m_type <= e_float); }

  /// Convert to an integer with \p bits and the given signedness.
  void TruncOrExtendTo(uint16_t bits, bool sign);

  bool IntegralPromote(uint16_t bits, bool sign);
  bool FloatPromote(const llvm::fltSemantics &semantics);

  bool IsSigned() const;
  bool MakeSigned();

  bool MakeUnsigned();

  static const char *GetValueTypeAsCString(Scalar::Type value_type);

  // All operators can benefits from the implicit conversions that will happen
  // automagically by the compiler, so no temporary objects will need to be
  // created. As a result, we currently don't need a variety of overloaded set
  // value accessors.
  Scalar &operator+=(Scalar rhs);
  Scalar &operator<<=(const Scalar &rhs); // Shift left
  Scalar &operator>>=(const Scalar &rhs); // Shift right (arithmetic)
  Scalar &operator&=(const Scalar &rhs);

  // Shifts the current value to the right without maintaining the current sign
  // of the value (if it is signed).
  bool ShiftRightLogical(const Scalar &rhs); // Returns true on success

  // Takes the absolute value of the current value if it is signed, else the
  // value remains unchanged. Returns false if the contained value has a void
  // type.
  bool AbsoluteValue(); // Returns true on success
  // Negates the current value (even for unsigned values). Returns false if the
  // contained value has a void type.
  bool UnaryNegate(); // Returns true on success
  // Inverts all bits in the current value as long as it isn't void or a
  // float/double/long double type. Returns false if the contained value has a
  // void/float/double/long double type, else the value is inverted and true is
  // returned.
  bool OnesComplement(); // Returns true on success

  // Access the type of the current value.
  Scalar::Type GetType() const { return m_type; }

  // Returns a casted value of the current contained data without modifying the
  // current value. FAIL_VALUE will be returned if the type of the value is
  // void or invalid.
  int SInt(int fail_value = 0) const;

  unsigned char UChar(unsigned char fail_value = 0) const;

  signed char SChar(signed char fail_value = 0) const;

  unsigned short UShort(unsigned short fail_value = 0) const;

  short SShort(short fail_value = 0) const;

  unsigned int UInt(unsigned int fail_value = 0) const;

  long SLong(long fail_value = 0) const;

  unsigned long ULong(unsigned long fail_value = 0) const;

  long long SLongLong(long long fail_value = 0) const;

  unsigned long long ULongLong(unsigned long long fail_value = 0) const;

  llvm::APInt SInt128(const llvm::APInt &fail_value) const;

  llvm::APInt UInt128(const llvm::APInt &fail_value) const;

  float Float(float fail_value = 0.0f) const;

  double Double(double fail_value = 0.0) const;

  long double LongDouble(long double fail_value = 0.0) const;

  llvm::APSInt GetAPSInt() const { return m_integer; }

  llvm::APFloat GetAPFloat() const { return m_float; }

  Status SetValueFromCString(const char *s, lldb::Encoding encoding,
                             size_t byte_size);

  Status SetValueFromData(const DataExtractor &data, lldb::Encoding encoding,
                          size_t byte_size);

  llvm::APFloat CreateAPFloatFromAPSInt(lldb::BasicType basic_type);

  llvm::APFloat CreateAPFloatFromAPFloat(lldb::BasicType basic_type);

protected:
  Scalar::Type m_type = e_void;
  llvm::APSInt m_integer;
  llvm::APFloat m_float;

  template <typename T> T GetAs(T fail_value) const;

  static Type PromoteToMaxType(Scalar &lhs, Scalar &rhs);

  using PromotionKey = std::tuple<Type, unsigned, bool>;
  PromotionKey GetPromoKey() const;

  static PromotionKey GetFloatPromoKey(const llvm::fltSemantics &semantics);

private:
  friend const Scalar operator+(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator-(Scalar lhs, Scalar rhs);
  friend const Scalar operator/(Scalar lhs, Scalar rhs);
  friend const Scalar operator*(Scalar lhs, Scalar rhs);
  friend const Scalar operator&(Scalar lhs, Scalar rhs);
  friend const Scalar operator|(Scalar lhs, Scalar rhs);
  friend const Scalar operator%(Scalar lhs, Scalar rhs);
  friend const Scalar operator^(Scalar lhs, Scalar rhs);
  friend const Scalar operator<<(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator>>(const Scalar &lhs, const Scalar &rhs);
  friend bool operator==(Scalar lhs, Scalar rhs);
  friend bool operator!=(const Scalar &lhs, const Scalar &rhs);
  friend bool operator<(Scalar lhs, Scalar rhs);
  friend bool operator<=(const Scalar &lhs, const Scalar &rhs);
  friend bool operator>(const Scalar &lhs, const Scalar &rhs);
  friend bool operator>=(const Scalar &lhs, const Scalar &rhs);
};

// Split out the operators into a format where the compiler will be able to
// implicitly convert numbers into Scalar objects.
//
// This allows code like:
//      Scalar two(2);
//      Scalar four = two * 2;
//      Scalar eight = 2 * four;    // This would cause an error if the
//                                  // operator* was implemented as a
//                                  // member function.
// SEE:
//  Item 19 of "Effective C++ Second Edition" by Scott Meyers
//  Differentiate among members functions, non-member functions, and
//  friend functions
const Scalar operator+(const Scalar &lhs, const Scalar &rhs);
const Scalar operator-(Scalar lhs, Scalar rhs);
const Scalar operator/(Scalar lhs, Scalar rhs);
const Scalar operator*(Scalar lhs, Scalar rhs);
const Scalar operator&(Scalar lhs, Scalar rhs);
const Scalar operator|(Scalar lhs, Scalar rhs);
const Scalar operator%(Scalar lhs, Scalar rhs);
const Scalar operator^(Scalar lhs, Scalar rhs);
const Scalar operator<<(const Scalar &lhs, const Scalar &rhs);
const Scalar operator>>(const Scalar &lhs, const Scalar &rhs);
bool operator==(Scalar lhs, Scalar rhs);
bool operator!=(const Scalar &lhs, const Scalar &rhs);
bool operator<(Scalar lhs, Scalar rhs);
bool operator<=(const Scalar &lhs, const Scalar &rhs);
bool operator>(const Scalar &lhs, const Scalar &rhs);
bool operator>=(const Scalar &lhs, const Scalar &rhs);

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Scalar &scalar);

} // namespace lldb_private

#endif // LLDB_UTILITY_SCALAR_H
