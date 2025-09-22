//===-- string_utils.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "string_utils.h"
#include "common.h"

#include <stdarg.h>
#include <string.h>

namespace scudo {

// Appends number in a given Base to buffer. If its length is less than
// |MinNumberLength|, it is padded with leading zeroes or spaces, depending
// on the value of |PadWithZero|.
void ScopedString::appendNumber(u64 AbsoluteValue, u8 Base, u8 MinNumberLength,
                                bool PadWithZero, bool Negative, bool Upper) {
  constexpr uptr MaxLen = 30;
  RAW_CHECK(Base == 10 || Base == 16);
  RAW_CHECK(Base == 10 || !Negative);
  RAW_CHECK(AbsoluteValue || !Negative);
  RAW_CHECK(MinNumberLength < MaxLen);
  if (Negative && MinNumberLength)
    --MinNumberLength;
  if (Negative && PadWithZero) {
    String.push_back('-');
  }
  uptr NumBuffer[MaxLen];
  int Pos = 0;
  do {
    RAW_CHECK_MSG(static_cast<uptr>(Pos) < MaxLen,
                  "appendNumber buffer overflow");
    NumBuffer[Pos++] = static_cast<uptr>(AbsoluteValue % Base);
    AbsoluteValue /= Base;
  } while (AbsoluteValue > 0);
  if (Pos < MinNumberLength) {
    memset(&NumBuffer[Pos], 0,
           sizeof(NumBuffer[0]) * static_cast<uptr>(MinNumberLength - Pos));
    Pos = MinNumberLength;
  }
  RAW_CHECK(Pos > 0);
  Pos--;
  for (; Pos >= 0 && NumBuffer[Pos] == 0; Pos--) {
    char c = (PadWithZero || Pos == 0) ? '0' : ' ';
    String.push_back(c);
  }
  if (Negative && !PadWithZero)
    String.push_back('-');
  for (; Pos >= 0; Pos--) {
    char Digit = static_cast<char>(NumBuffer[Pos]);
    Digit = static_cast<char>((Digit < 10) ? '0' + Digit
                                           : (Upper ? 'A' : 'a') + Digit - 10);
    String.push_back(Digit);
  }
}

void ScopedString::appendUnsigned(u64 Num, u8 Base, u8 MinNumberLength,
                                  bool PadWithZero, bool Upper) {
  appendNumber(Num, Base, MinNumberLength, PadWithZero, /*Negative=*/false,
               Upper);
}

void ScopedString::appendSignedDecimal(s64 Num, u8 MinNumberLength,
                                       bool PadWithZero) {
  const bool Negative = (Num < 0);
  const u64 UnsignedNum = (Num == INT64_MIN)
                              ? static_cast<u64>(INT64_MAX) + 1
                              : static_cast<u64>(Negative ? -Num : Num);
  appendNumber(UnsignedNum, 10, MinNumberLength, PadWithZero, Negative,
               /*Upper=*/false);
}

// Use the fact that explicitly requesting 0 Width (%0s) results in UB and
// interpret Width == 0 as "no Width requested":
// Width == 0 - no Width requested
// Width  < 0 - left-justify S within and pad it to -Width chars, if necessary
// Width  > 0 - right-justify S, not implemented yet
void ScopedString::appendString(int Width, int MaxChars, const char *S) {
  if (!S)
    S = "<null>";
  int NumChars = 0;
  for (; *S; S++) {
    if (MaxChars >= 0 && NumChars >= MaxChars)
      break;
    String.push_back(*S);
    NumChars++;
  }
  if (Width < 0) {
    // Only left justification supported.
    Width = -Width - NumChars;
    while (Width-- > 0)
      String.push_back(' ');
  }
}

void ScopedString::appendPointer(u64 ptr_value) {
  appendString(0, -1, "0x");
  appendUnsigned(ptr_value, 16, SCUDO_POINTER_FORMAT_LENGTH,
                 /*PadWithZero=*/true,
                 /*Upper=*/false);
}

void ScopedString::vappend(const char *Format, va_list &Args) {
  // Since the string contains the '\0' terminator, put our size before it
  // so that push_back calls work correctly.
  DCHECK(String.size() > 0);
  String.resize(String.size() - 1);

  static const char *PrintfFormatsHelp =
      "Supported formats: %([0-9]*)?(z|ll)?{d,u,x,X}; %p; "
      "%[-]([0-9]*)?(\\.\\*)?s; %c\n";
  RAW_CHECK(Format);
  const char *Cur = Format;
  for (; *Cur; Cur++) {
    if (*Cur != '%') {
      String.push_back(*Cur);
      continue;
    }
    Cur++;
    const bool LeftJustified = *Cur == '-';
    if (LeftJustified)
      Cur++;
    bool HaveWidth = (*Cur >= '0' && *Cur <= '9');
    const bool PadWithZero = (*Cur == '0');
    u8 Width = 0;
    if (HaveWidth) {
      while (*Cur >= '0' && *Cur <= '9')
        Width = static_cast<u8>(Width * 10 + *Cur++ - '0');
    }
    const bool HavePrecision = (Cur[0] == '.' && Cur[1] == '*');
    int Precision = -1;
    if (HavePrecision) {
      Cur += 2;
      Precision = va_arg(Args, int);
    }
    const bool HaveZ = (*Cur == 'z');
    Cur += HaveZ;
    const bool HaveLL = !HaveZ && (Cur[0] == 'l' && Cur[1] == 'l');
    Cur += HaveLL * 2;
    s64 DVal;
    u64 UVal;
    const bool HaveLength = HaveZ || HaveLL;
    const bool HaveFlags = HaveWidth || HaveLength;
    // At the moment only %s supports precision and left-justification.
    CHECK(!((Precision >= 0 || LeftJustified) && *Cur != 's'));
    switch (*Cur) {
    case 'd': {
      DVal = HaveLL  ? va_arg(Args, s64)
             : HaveZ ? va_arg(Args, sptr)
                     : va_arg(Args, int);
      appendSignedDecimal(DVal, Width, PadWithZero);
      break;
    }
    case 'u':
    case 'x':
    case 'X': {
      UVal = HaveLL  ? va_arg(Args, u64)
             : HaveZ ? va_arg(Args, uptr)
                     : va_arg(Args, unsigned);
      const bool Upper = (*Cur == 'X');
      appendUnsigned(UVal, (*Cur == 'u') ? 10 : 16, Width, PadWithZero, Upper);
      break;
    }
    case 'p': {
      RAW_CHECK_MSG(!HaveFlags, PrintfFormatsHelp);
      appendPointer(va_arg(Args, uptr));
      break;
    }
    case 's': {
      RAW_CHECK_MSG(!HaveLength, PrintfFormatsHelp);
      // Only left-justified Width is supported.
      CHECK(!HaveWidth || LeftJustified);
      appendString(LeftJustified ? -Width : Width, Precision,
                   va_arg(Args, char *));
      break;
    }
    case 'c': {
      RAW_CHECK_MSG(!HaveFlags, PrintfFormatsHelp);
      String.push_back(static_cast<char>(va_arg(Args, int)));
      break;
    }
    // In Scudo, `s64`/`u64` are supposed to use `lld` and `llu` respectively.
    // However, `-Wformat` doesn't know we have a different parser for those
    // placeholders and it keeps complaining the type mismatch on 64-bit
    // platform which uses `ld`/`lu` for `s64`/`u64`. Therefore, in order to
    // silence the warning, we turn to use `PRId64`/`PRIu64` for printing
    // `s64`/`u64` and handle the `ld`/`lu` here.
    case 'l': {
      ++Cur;
      RAW_CHECK(*Cur == 'd' || *Cur == 'u');

      if (*Cur == 'd') {
        DVal = va_arg(Args, s64);
        appendSignedDecimal(DVal, Width, PadWithZero);
      } else {
        UVal = va_arg(Args, u64);
        appendUnsigned(UVal, 10, Width, PadWithZero, false);
      }

      break;
    }
    case '%': {
      RAW_CHECK_MSG(!HaveFlags, PrintfFormatsHelp);
      String.push_back('%');
      break;
    }
    default: {
      RAW_CHECK_MSG(false, PrintfFormatsHelp);
    }
    }
  }
  String.push_back('\0');
  if (String.back() != '\0') {
    // String truncated, make sure the string is terminated properly.
    // This can happen if there is no more memory when trying to resize
    // the string.
    String.back() = '\0';
  }
}

void ScopedString::append(const char *Format, ...) {
  va_list Args;
  va_start(Args, Format);
  vappend(Format, Args);
  va_end(Args);
}

void Printf(const char *Format, ...) {
  va_list Args;
  va_start(Args, Format);
  ScopedString Msg;
  Msg.vappend(Format, Args);
  outputRaw(Msg.data());
  va_end(Args);
}

} // namespace scudo
