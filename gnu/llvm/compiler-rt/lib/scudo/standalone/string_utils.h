//===-- string_utils.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_STRING_UTILS_H_
#define SCUDO_STRING_UTILS_H_

#include "internal_defs.h"
#include "vector.h"

#include <stdarg.h>

namespace scudo {

class ScopedString {
public:
  explicit ScopedString() { String.push_back('\0'); }
  uptr length() { return String.size() - 1; }
  const char *data() { return String.data(); }
  void clear() {
    String.clear();
    String.push_back('\0');
  }
  void vappend(const char *Format, va_list &Args);
  void append(const char *Format, ...) FORMAT(2, 3);
  void output() const { outputRaw(String.data()); }
  void reserve(size_t Size) { String.reserve(Size + 1); }
  uptr capacity() { return String.capacity() - 1; }

private:
  void appendNumber(u64 AbsoluteValue, u8 Base, u8 MinNumberLength,
                    bool PadWithZero, bool Negative, bool Upper);
  void appendUnsigned(u64 Num, u8 Base, u8 MinNumberLength, bool PadWithZero,
                      bool Upper);
  void appendSignedDecimal(s64 Num, u8 MinNumberLength, bool PadWithZero);
  void appendString(int Width, int MaxChars, const char *S);
  void appendPointer(u64 ptr_value);

  Vector<char, 256> String;
};

void Printf(const char *Format, ...) FORMAT(1, 2);

} // namespace scudo

#endif // SCUDO_STRING_UTILS_H_
