//===-- llvm/Support/Alignment.h - Useful alignment functions ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains types to represent alignments.
// They are instrumented to guarantee some invariants are preserved and prevent
// invalid manipulations.
//
// - Align represents an alignment in bytes, it is always set and always a valid
// power of two, its minimum value is 1 which means no alignment requirements.
//
// - MaybeAlign is an optional type, it may be undefined or set. When it's set
// you can get the underlying Align type by using the getValue() method.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ALIGNMENT_H_
#define LLVM_SUPPORT_ALIGNMENT_H_

#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <optional>
#ifndef NDEBUG
#include <string>
#endif // NDEBUG

namespace llvm {

#define ALIGN_CHECK_ISPOSITIVE(decl)                                           \
  assert(decl > 0 && (#decl " should be defined"))

/// This struct is a compact representation of a valid (non-zero power of two)
/// alignment.
/// It is suitable for use as static global constants.
struct Align {
private:
  uint8_t ShiftValue = 0; /// The log2 of the required alignment.
                          /// ShiftValue is less than 64 by construction.

  friend struct MaybeAlign;
  friend unsigned Log2(Align);
  friend bool operator==(Align Lhs, Align Rhs);
  friend bool operator!=(Align Lhs, Align Rhs);
  friend bool operator<=(Align Lhs, Align Rhs);
  friend bool operator>=(Align Lhs, Align Rhs);
  friend bool operator<(Align Lhs, Align Rhs);
  friend bool operator>(Align Lhs, Align Rhs);
  friend unsigned encode(struct MaybeAlign A);
  friend struct MaybeAlign decodeMaybeAlign(unsigned Value);

  /// A trivial type to allow construction of constexpr Align.
  /// This is currently needed to workaround a bug in GCC 5.3 which prevents
  /// definition of constexpr assign operators.
  /// https://stackoverflow.com/questions/46756288/explicitly-defaulted-function-cannot-be-declared-as-constexpr-because-the-implic
  /// FIXME: Remove this, make all assign operators constexpr and introduce user
  /// defined literals when we don't have to support GCC 5.3 anymore.
  /// https://llvm.org/docs/GettingStarted.html#getting-a-modern-host-c-toolchain
  struct LogValue {
    uint8_t Log;
  };

public:
  /// Default is byte-aligned.
  constexpr Align() = default;
  /// Do not perform checks in case of copy/move construct/assign, because the
  /// checks have been performed when building `Other`.
  constexpr Align(const Align &Other) = default;
  constexpr Align(Align &&Other) = default;
  Align &operator=(const Align &Other) = default;
  Align &operator=(Align &&Other) = default;

  explicit Align(uint64_t Value) {
    assert(Value > 0 && "Value must not be 0");
    assert(llvm::isPowerOf2_64(Value) && "Alignment is not a power of 2");
    ShiftValue = Log2_64(Value);
    assert(ShiftValue < 64 && "Broken invariant");
  }

  /// This is a hole in the type system and should not be abused.
  /// Needed to interact with C for instance.
  uint64_t value() const { return uint64_t(1) << ShiftValue; }

  // Returns the previous alignment.
  Align previous() const {
    assert(ShiftValue != 0 && "Undefined operation");
    Align Out;
    Out.ShiftValue = ShiftValue - 1;
    return Out;
  }

  /// Allow constructions of constexpr Align.
  template <size_t kValue> constexpr static Align Constant() {
    return LogValue{static_cast<uint8_t>(CTLog2<kValue>())};
  }

  /// Allow constructions of constexpr Align from types.
  /// Compile time equivalent to Align(alignof(T)).
  template <typename T> constexpr static Align Of() {
    return Constant<std::alignment_of_v<T>>();
  }

  /// Constexpr constructor from LogValue type.
  constexpr Align(LogValue CA) : ShiftValue(CA.Log) {}
};

/// Treats the value 0 as a 1, so Align is always at least 1.
inline Align assumeAligned(uint64_t Value) {
  return Value ? Align(Value) : Align();
}

/// This struct is a compact representation of a valid (power of two) or
/// undefined (0) alignment.
struct MaybeAlign : public std::optional<Align> {
private:
  using UP = std::optional<Align>;

public:
  /// Default is undefined.
  MaybeAlign() = default;
  /// Do not perform checks in case of copy/move construct/assign, because the
  /// checks have been performed when building `Other`.
  MaybeAlign(const MaybeAlign &Other) = default;
  MaybeAlign &operator=(const MaybeAlign &Other) = default;
  MaybeAlign(MaybeAlign &&Other) = default;
  MaybeAlign &operator=(MaybeAlign &&Other) = default;

  constexpr MaybeAlign(std::nullopt_t None) : UP(None) {}
  constexpr MaybeAlign(Align Value) : UP(Value) {}
  explicit MaybeAlign(uint64_t Value) {
    assert((Value == 0 || llvm::isPowerOf2_64(Value)) &&
           "Alignment is neither 0 nor a power of 2");
    if (Value)
      emplace(Value);
  }

  /// For convenience, returns a valid alignment or 1 if undefined.
  Align valueOrOne() const { return value_or(Align()); }
};

/// Checks that SizeInBytes is a multiple of the alignment.
inline bool isAligned(Align Lhs, uint64_t SizeInBytes) {
  return SizeInBytes % Lhs.value() == 0;
}

/// Checks that Addr is a multiple of the alignment.
inline bool isAddrAligned(Align Lhs, const void *Addr) {
  return isAligned(Lhs, reinterpret_cast<uintptr_t>(Addr));
}

/// Returns a multiple of A needed to store `Size` bytes.
inline uint64_t alignTo(uint64_t Size, Align A) {
  const uint64_t Value = A.value();
  // The following line is equivalent to `(Size + Value - 1) / Value * Value`.

  // The division followed by a multiplication can be thought of as a right
  // shift followed by a left shift which zeros out the extra bits produced in
  // the bump; `~(Value - 1)` is a mask where all those bits being zeroed out
  // are just zero.

  // Most compilers can generate this code but the pattern may be missed when
  // multiple functions gets inlined.
  return (Size + Value - 1) & ~(Value - 1U);
}

/// If non-zero \p Skew is specified, the return value will be a minimal integer
/// that is greater than or equal to \p Size and equal to \p A * N + \p Skew for
/// some integer N. If \p Skew is larger than \p A, its value is adjusted to '\p
/// Skew mod \p A'.
///
/// Examples:
/// \code
///   alignTo(5, Align(8), 7) = 7
///   alignTo(17, Align(8), 1) = 17
///   alignTo(~0LL, Align(8), 3) = 3
/// \endcode
inline uint64_t alignTo(uint64_t Size, Align A, uint64_t Skew) {
  const uint64_t Value = A.value();
  Skew %= Value;
  return alignTo(Size - Skew, A) + Skew;
}

/// Aligns `Addr` to `Alignment` bytes, rounding up.
inline uintptr_t alignAddr(const void *Addr, Align Alignment) {
  uintptr_t ArithAddr = reinterpret_cast<uintptr_t>(Addr);
  assert(static_cast<uintptr_t>(ArithAddr + Alignment.value() - 1) >=
             ArithAddr &&
         "Overflow");
  return alignTo(ArithAddr, Alignment);
}

/// Returns the offset to the next integer (mod 2**64) that is greater than
/// or equal to \p Value and is a multiple of \p Align.
inline uint64_t offsetToAlignment(uint64_t Value, Align Alignment) {
  return alignTo(Value, Alignment) - Value;
}

/// Returns the necessary adjustment for aligning `Addr` to `Alignment`
/// bytes, rounding up.
inline uint64_t offsetToAlignedAddr(const void *Addr, Align Alignment) {
  return offsetToAlignment(reinterpret_cast<uintptr_t>(Addr), Alignment);
}

/// Returns the log2 of the alignment.
inline unsigned Log2(Align A) { return A.ShiftValue; }

/// Returns the alignment that satisfies both alignments.
/// Same semantic as MinAlign.
inline Align commonAlignment(Align A, uint64_t Offset) {
  return Align(MinAlign(A.value(), Offset));
}

/// Returns a representation of the alignment that encodes undefined as 0.
inline unsigned encode(MaybeAlign A) { return A ? A->ShiftValue + 1 : 0; }

/// Dual operation of the encode function above.
inline MaybeAlign decodeMaybeAlign(unsigned Value) {
  if (Value == 0)
    return MaybeAlign();
  Align Out;
  Out.ShiftValue = Value - 1;
  return Out;
}

/// Returns a representation of the alignment, the encoded value is positive by
/// definition.
inline unsigned encode(Align A) { return encode(MaybeAlign(A)); }

/// Comparisons between Align and scalars. Rhs must be positive.
inline bool operator==(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() == Rhs;
}
inline bool operator!=(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() != Rhs;
}
inline bool operator<=(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() <= Rhs;
}
inline bool operator>=(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() >= Rhs;
}
inline bool operator<(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() < Rhs;
}
inline bool operator>(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() > Rhs;
}

/// Comparisons operators between Align.
inline bool operator==(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue == Rhs.ShiftValue;
}
inline bool operator!=(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue != Rhs.ShiftValue;
}
inline bool operator<=(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue <= Rhs.ShiftValue;
}
inline bool operator>=(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue >= Rhs.ShiftValue;
}
inline bool operator<(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue < Rhs.ShiftValue;
}
inline bool operator>(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue > Rhs.ShiftValue;
}

// Don't allow relational comparisons with MaybeAlign.
bool operator<=(Align Lhs, MaybeAlign Rhs) = delete;
bool operator>=(Align Lhs, MaybeAlign Rhs) = delete;
bool operator<(Align Lhs, MaybeAlign Rhs) = delete;
bool operator>(Align Lhs, MaybeAlign Rhs) = delete;

bool operator<=(MaybeAlign Lhs, Align Rhs) = delete;
bool operator>=(MaybeAlign Lhs, Align Rhs) = delete;
bool operator<(MaybeAlign Lhs, Align Rhs) = delete;
bool operator>(MaybeAlign Lhs, Align Rhs) = delete;

bool operator<=(MaybeAlign Lhs, MaybeAlign Rhs) = delete;
bool operator>=(MaybeAlign Lhs, MaybeAlign Rhs) = delete;
bool operator<(MaybeAlign Lhs, MaybeAlign Rhs) = delete;
bool operator>(MaybeAlign Lhs, MaybeAlign Rhs) = delete;

// Allow equality comparisons between Align and MaybeAlign.
inline bool operator==(MaybeAlign Lhs, Align Rhs) { return Lhs && *Lhs == Rhs; }
inline bool operator!=(MaybeAlign Lhs, Align Rhs) { return !(Lhs == Rhs); }
inline bool operator==(Align Lhs, MaybeAlign Rhs) { return Rhs == Lhs; }
inline bool operator!=(Align Lhs, MaybeAlign Rhs) { return !(Rhs == Lhs); }
// Allow equality comparisons with MaybeAlign.
inline bool operator==(MaybeAlign Lhs, MaybeAlign Rhs) {
  return (Lhs && Rhs && (*Lhs == *Rhs)) || (!Lhs && !Rhs);
}
inline bool operator!=(MaybeAlign Lhs, MaybeAlign Rhs) { return !(Lhs == Rhs); }
// Allow equality comparisons with std::nullopt.
inline bool operator==(MaybeAlign Lhs, std::nullopt_t) { return !bool(Lhs); }
inline bool operator!=(MaybeAlign Lhs, std::nullopt_t) { return bool(Lhs); }
inline bool operator==(std::nullopt_t, MaybeAlign Rhs) { return !bool(Rhs); }
inline bool operator!=(std::nullopt_t, MaybeAlign Rhs) { return bool(Rhs); }

#ifndef NDEBUG
// For usage in LLVM_DEBUG macros.
inline std::string DebugStr(const Align &A) {
  return std::to_string(A.value());
}
// For usage in LLVM_DEBUG macros.
inline std::string DebugStr(const MaybeAlign &MA) {
  if (MA)
    return std::to_string(MA->value());
  return "None";
}
#endif // NDEBUG

#undef ALIGN_CHECK_ISPOSITIVE

} // namespace llvm

#endif // LLVM_SUPPORT_ALIGNMENT_H_
