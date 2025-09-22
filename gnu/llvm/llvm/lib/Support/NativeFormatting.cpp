//===- NativeFormatting.cpp - Low level formatting helpers -------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/NativeFormatting.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

#if defined(_WIN32) && !defined(__MINGW32__)
#include <float.h> // For _fpclass in llvm::write_double.
#endif

using namespace llvm;

template<typename T, std::size_t N>
static int format_to_buffer(T Value, char (&Buffer)[N]) {
  char *EndPtr = std::end(Buffer);
  char *CurPtr = EndPtr;

  do {
    *--CurPtr = '0' + char(Value % 10);
    Value /= 10;
  } while (Value);
  return EndPtr - CurPtr;
}

static void writeWithCommas(raw_ostream &S, ArrayRef<char> Buffer) {
  assert(!Buffer.empty());

  ArrayRef<char> ThisGroup;
  int InitialDigits = ((Buffer.size() - 1) % 3) + 1;
  ThisGroup = Buffer.take_front(InitialDigits);
  S.write(ThisGroup.data(), ThisGroup.size());

  Buffer = Buffer.drop_front(InitialDigits);
  assert(Buffer.size() % 3 == 0);
  while (!Buffer.empty()) {
    S << ',';
    ThisGroup = Buffer.take_front(3);
    S.write(ThisGroup.data(), 3);
    Buffer = Buffer.drop_front(3);
  }
}

template <typename T>
static void write_unsigned_impl(raw_ostream &S, T N, size_t MinDigits,
                                IntegerStyle Style, bool IsNegative) {
  static_assert(std::is_unsigned_v<T>, "Value is not unsigned!");

  char NumberBuffer[128];
  size_t Len = format_to_buffer(N, NumberBuffer);

  if (IsNegative)
    S << '-';

  if (Len < MinDigits && Style != IntegerStyle::Number) {
    for (size_t I = Len; I < MinDigits; ++I)
      S << '0';
  }

  if (Style == IntegerStyle::Number) {
    writeWithCommas(S, ArrayRef<char>(std::end(NumberBuffer) - Len, Len));
  } else {
    S.write(std::end(NumberBuffer) - Len, Len);
  }
}

template <typename T>
static void write_unsigned(raw_ostream &S, T N, size_t MinDigits,
                           IntegerStyle Style, bool IsNegative = false) {
  // Output using 32-bit div/mod if possible.
  if (N == static_cast<uint32_t>(N))
    write_unsigned_impl(S, static_cast<uint32_t>(N), MinDigits, Style,
                        IsNegative);
  else
    write_unsigned_impl(S, N, MinDigits, Style, IsNegative);
}

template <typename T>
static void write_signed(raw_ostream &S, T N, size_t MinDigits,
                         IntegerStyle Style) {
  static_assert(std::is_signed_v<T>, "Value is not signed!");

  using UnsignedT = std::make_unsigned_t<T>;

  if (N >= 0) {
    write_unsigned(S, static_cast<UnsignedT>(N), MinDigits, Style);
    return;
  }

  UnsignedT UN = -(UnsignedT)N;
  write_unsigned(S, UN, MinDigits, Style, true);
}

void llvm::write_integer(raw_ostream &S, unsigned int N, size_t MinDigits,
                         IntegerStyle Style) {
  write_unsigned(S, N, MinDigits, Style);
}

void llvm::write_integer(raw_ostream &S, int N, size_t MinDigits,
                         IntegerStyle Style) {
  write_signed(S, N, MinDigits, Style);
}

void llvm::write_integer(raw_ostream &S, unsigned long N, size_t MinDigits,
                         IntegerStyle Style) {
  write_unsigned(S, N, MinDigits, Style);
}

void llvm::write_integer(raw_ostream &S, long N, size_t MinDigits,
                         IntegerStyle Style) {
  write_signed(S, N, MinDigits, Style);
}

void llvm::write_integer(raw_ostream &S, unsigned long long N, size_t MinDigits,
                         IntegerStyle Style) {
  write_unsigned(S, N, MinDigits, Style);
}

void llvm::write_integer(raw_ostream &S, long long N, size_t MinDigits,
                         IntegerStyle Style) {
  write_signed(S, N, MinDigits, Style);
}

void llvm::write_hex(raw_ostream &S, uint64_t N, HexPrintStyle Style,
                     std::optional<size_t> Width) {
  const size_t kMaxWidth = 128u;

  size_t W = std::min(kMaxWidth, Width.value_or(0u));

  unsigned Nibbles = (llvm::bit_width(N) + 3) / 4;
  bool Prefix = (Style == HexPrintStyle::PrefixLower ||
                 Style == HexPrintStyle::PrefixUpper);
  bool Upper =
      (Style == HexPrintStyle::Upper || Style == HexPrintStyle::PrefixUpper);
  unsigned PrefixChars = Prefix ? 2 : 0;
  unsigned NumChars =
      std::max(static_cast<unsigned>(W), std::max(1u, Nibbles) + PrefixChars);

  char NumberBuffer[kMaxWidth];
  ::memset(NumberBuffer, '0', std::size(NumberBuffer));
  if (Prefix)
    NumberBuffer[1] = 'x';
  char *EndPtr = NumberBuffer + NumChars;
  char *CurPtr = EndPtr;
  while (N) {
    unsigned char x = static_cast<unsigned char>(N) % 16;
    *--CurPtr = hexdigit(x, !Upper);
    N /= 16;
  }

  S.write(NumberBuffer, NumChars);
}

void llvm::write_double(raw_ostream &S, double N, FloatStyle Style,
                        std::optional<size_t> Precision) {
  size_t Prec = Precision.value_or(getDefaultPrecision(Style));

  if (std::isnan(N)) {
    S << "nan";
    return;
  } else if (std::isinf(N)) {
    S << (std::signbit(N) ? "-INF" : "INF");
    return;
  }

  char Letter;
  if (Style == FloatStyle::Exponent)
    Letter = 'e';
  else if (Style == FloatStyle::ExponentUpper)
    Letter = 'E';
  else
    Letter = 'f';

  SmallString<8> Spec;
  llvm::raw_svector_ostream Out(Spec);
  Out << "%." << Prec << Letter;

  if (Style == FloatStyle::Exponent || Style == FloatStyle::ExponentUpper) {
#ifdef _WIN32
// On MSVCRT and compatible, output of %e is incompatible to Posix
// by default. Number of exponent digits should be at least 2. "%+03d"
// FIXME: Implement our formatter to here or Support/Format.h!
#if defined(__MINGW32__)
    // FIXME: It should be generic to C++11.
    if (N == 0.0 && std::signbit(N)) {
      char NegativeZero[] = "-0.000000e+00";
      if (Style == FloatStyle::ExponentUpper)
        NegativeZero[strlen(NegativeZero) - 4] = 'E';
      S << NegativeZero;
      return;
    }
#else
    int fpcl = _fpclass(N);

    // negative zero
    if (fpcl == _FPCLASS_NZ) {
      char NegativeZero[] = "-0.000000e+00";
      if (Style == FloatStyle::ExponentUpper)
        NegativeZero[strlen(NegativeZero) - 4] = 'E';
      S << NegativeZero;
      return;
    }
#endif

    char buf[32];
    unsigned len;
    len = format(Spec.c_str(), N).snprint(buf, sizeof(buf));
    if (len <= sizeof(buf) - 2) {
      if (len >= 5 && (buf[len - 5] == 'e' || buf[len - 5] == 'E') &&
          buf[len - 3] == '0') {
        int cs = buf[len - 4];
        if (cs == '+' || cs == '-') {
          int c1 = buf[len - 2];
          int c0 = buf[len - 1];
          if (isdigit(static_cast<unsigned char>(c1)) &&
              isdigit(static_cast<unsigned char>(c0))) {
            // Trim leading '0': "...e+012" -> "...e+12\0"
            buf[len - 3] = c1;
            buf[len - 2] = c0;
            buf[--len] = 0;
          }
        }
      }
      S << buf;
      return;
    }
#endif
  }

  if (Style == FloatStyle::Percent)
    N *= 100.0;

  char Buf[32];
  format(Spec.c_str(), N).snprint(Buf, sizeof(Buf));
  S << Buf;
  if (Style == FloatStyle::Percent)
    S << '%';
}

bool llvm::isPrefixedHexStyle(HexPrintStyle S) {
  return (S == HexPrintStyle::PrefixLower || S == HexPrintStyle::PrefixUpper);
}

size_t llvm::getDefaultPrecision(FloatStyle Style) {
  switch (Style) {
  case FloatStyle::Exponent:
  case FloatStyle::ExponentUpper:
    return 6; // Number of decimal places.
  case FloatStyle::Fixed:
  case FloatStyle::Percent:
    return 2; // Number of decimal places.
  }
  llvm_unreachable("Unknown FloatStyle enum");
}
