//===--- Utility.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provide some utility classes for use in the demangler.
// There are two copies of this file in the source tree.  The one in libcxxabi
// is the original and the one in llvm is the copy.  Use cp-to-llvm.sh to update
// the copy.  See README.txt for more details.
//
//===----------------------------------------------------------------------===//

#ifndef DEMANGLE_UTILITY_H
#define DEMANGLE_UTILITY_H

#include "DemangleConfig.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

DEMANGLE_NAMESPACE_BEGIN

// Stream that AST nodes write their string representation into after the AST
// has been parsed.
class OutputBuffer {
  char *Buffer = nullptr;
  size_t CurrentPosition = 0;
  size_t BufferCapacity = 0;

  // Ensure there are at least N more positions in the buffer.
  void grow(size_t N) {
    size_t Need = N + CurrentPosition;
    if (Need > BufferCapacity) {
      // Reduce the number of reallocations, with a bit of hysteresis. The
      // number here is chosen so the first allocation will more-than-likely not
      // allocate more than 1K.
      Need += 1024 - 32;
      BufferCapacity *= 2;
      if (BufferCapacity < Need)
        BufferCapacity = Need;
      Buffer = static_cast<char *>(std::realloc(Buffer, BufferCapacity));
      if (Buffer == nullptr)
        std::abort();
    }
  }

  OutputBuffer &writeUnsigned(uint64_t N, bool isNeg = false) {
    std::array<char, 21> Temp;
    char *TempPtr = Temp.data() + Temp.size();

    // Output at least one character.
    do {
      *--TempPtr = char('0' + N % 10);
      N /= 10;
    } while (N);

    // Add negative sign.
    if (isNeg)
      *--TempPtr = '-';

    return operator+=(
        std::string_view(TempPtr, Temp.data() + Temp.size() - TempPtr));
  }

public:
  OutputBuffer(char *StartBuf, size_t Size)
      : Buffer(StartBuf), BufferCapacity(Size) {}
  OutputBuffer(char *StartBuf, size_t *SizePtr)
      : OutputBuffer(StartBuf, StartBuf ? *SizePtr : 0) {}
  OutputBuffer() = default;
  // Non-copyable
  OutputBuffer(const OutputBuffer &) = delete;
  OutputBuffer &operator=(const OutputBuffer &) = delete;

  operator std::string_view() const {
    return std::string_view(Buffer, CurrentPosition);
  }

  /// If a ParameterPackExpansion (or similar type) is encountered, the offset
  /// into the pack that we're currently printing.
  unsigned CurrentPackIndex = std::numeric_limits<unsigned>::max();
  unsigned CurrentPackMax = std::numeric_limits<unsigned>::max();

  /// When zero, we're printing template args and '>' needs to be parenthesized.
  /// Use a counter so we can simply increment inside parentheses.
  unsigned GtIsGt = 1;

  bool isGtInsideTemplateArgs() const { return GtIsGt == 0; }

  void printOpen(char Open = '(') {
    GtIsGt++;
    *this += Open;
  }
  void printClose(char Close = ')') {
    GtIsGt--;
    *this += Close;
  }

  OutputBuffer &operator+=(std::string_view R) {
    if (size_t Size = R.size()) {
      grow(Size);
      std::memcpy(Buffer + CurrentPosition, &*R.begin(), Size);
      CurrentPosition += Size;
    }
    return *this;
  }

  OutputBuffer &operator+=(char C) {
    grow(1);
    Buffer[CurrentPosition++] = C;
    return *this;
  }

  OutputBuffer &prepend(std::string_view R) {
    size_t Size = R.size();

    grow(Size);
    std::memmove(Buffer + Size, Buffer, CurrentPosition);
    std::memcpy(Buffer, &*R.begin(), Size);
    CurrentPosition += Size;

    return *this;
  }

  OutputBuffer &operator<<(std::string_view R) { return (*this += R); }

  OutputBuffer &operator<<(char C) { return (*this += C); }

  OutputBuffer &operator<<(long long N) {
    return writeUnsigned(static_cast<unsigned long long>(std::abs(N)), N < 0);
  }

  OutputBuffer &operator<<(unsigned long long N) {
    return writeUnsigned(N, false);
  }

  OutputBuffer &operator<<(long N) {
    return this->operator<<(static_cast<long long>(N));
  }

  OutputBuffer &operator<<(unsigned long N) {
    return this->operator<<(static_cast<unsigned long long>(N));
  }

  OutputBuffer &operator<<(int N) {
    return this->operator<<(static_cast<long long>(N));
  }

  OutputBuffer &operator<<(unsigned int N) {
    return this->operator<<(static_cast<unsigned long long>(N));
  }

  void insert(size_t Pos, const char *S, size_t N) {
    DEMANGLE_ASSERT(Pos <= CurrentPosition, "");
    if (N == 0)
      return;
    grow(N);
    std::memmove(Buffer + Pos + N, Buffer + Pos, CurrentPosition - Pos);
    std::memcpy(Buffer + Pos, S, N);
    CurrentPosition += N;
  }

  size_t getCurrentPosition() const { return CurrentPosition; }
  void setCurrentPosition(size_t NewPos) { CurrentPosition = NewPos; }

  char back() const {
    DEMANGLE_ASSERT(CurrentPosition, "");
    return Buffer[CurrentPosition - 1];
  }

  bool empty() const { return CurrentPosition == 0; }

  char *getBuffer() { return Buffer; }
  char *getBufferEnd() { return Buffer + CurrentPosition - 1; }
  size_t getBufferCapacity() const { return BufferCapacity; }
};

template <class T> class ScopedOverride {
  T &Loc;
  T Original;

public:
  ScopedOverride(T &Loc_) : ScopedOverride(Loc_, Loc_) {}

  ScopedOverride(T &Loc_, T NewVal) : Loc(Loc_), Original(Loc_) {
    Loc_ = std::move(NewVal);
  }
  ~ScopedOverride() { Loc = std::move(Original); }

  ScopedOverride(const ScopedOverride &) = delete;
  ScopedOverride &operator=(const ScopedOverride &) = delete;
};

DEMANGLE_NAMESPACE_END

#endif
