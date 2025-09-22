//===-- llvm/Support/MathExtras.h - Useful math functions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains some functions that are useful for math stuff.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MATHEXTRAS_H
#define LLVM_SUPPORT_MATHEXTRAS_H

#include "llvm/ADT/bit.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace llvm {
/// Some template parameter helpers to optimize for bitwidth, for functions that
/// take multiple arguments.

// We can't verify signedness, since callers rely on implicit coercions to
// signed/unsigned.
template <typename T, typename U>
using enableif_int =
    std::enable_if_t<std::is_integral_v<T> && std::is_integral_v<U>>;

// Use std::common_type_t to widen only up to the widest argument.
template <typename T, typename U, typename = enableif_int<T, U>>
using common_uint =
    std::common_type_t<std::make_unsigned_t<T>, std::make_unsigned_t<U>>;
template <typename T, typename U, typename = enableif_int<T, U>>
using common_sint =
    std::common_type_t<std::make_signed_t<T>, std::make_signed_t<U>>;

/// Mathematical constants.
namespace numbers {
// TODO: Track C++20 std::numbers.
// TODO: Favor using the hexadecimal FP constants (requires C++17).
constexpr double e          = 2.7182818284590452354, // (0x1.5bf0a8b145749P+1) https://oeis.org/A001113
                 egamma     = .57721566490153286061, // (0x1.2788cfc6fb619P-1) https://oeis.org/A001620
                 ln2        = .69314718055994530942, // (0x1.62e42fefa39efP-1) https://oeis.org/A002162
                 ln10       = 2.3025850929940456840, // (0x1.24bb1bbb55516P+1) https://oeis.org/A002392
                 log2e      = 1.4426950408889634074, // (0x1.71547652b82feP+0)
                 log10e     = .43429448190325182765, // (0x1.bcb7b1526e50eP-2)
                 pi         = 3.1415926535897932385, // (0x1.921fb54442d18P+1) https://oeis.org/A000796
                 inv_pi     = .31830988618379067154, // (0x1.45f306bc9c883P-2) https://oeis.org/A049541
                 sqrtpi     = 1.7724538509055160273, // (0x1.c5bf891b4ef6bP+0) https://oeis.org/A002161
                 inv_sqrtpi = .56418958354775628695, // (0x1.20dd750429b6dP-1) https://oeis.org/A087197
                 sqrt2      = 1.4142135623730950488, // (0x1.6a09e667f3bcdP+0) https://oeis.org/A00219
                 inv_sqrt2  = .70710678118654752440, // (0x1.6a09e667f3bcdP-1)
                 sqrt3      = 1.7320508075688772935, // (0x1.bb67ae8584caaP+0) https://oeis.org/A002194
                 inv_sqrt3  = .57735026918962576451, // (0x1.279a74590331cP-1)
                 phi        = 1.6180339887498948482; // (0x1.9e3779b97f4a8P+0) https://oeis.org/A001622
constexpr float ef          = 2.71828183F, // (0x1.5bf0a8P+1) https://oeis.org/A001113
                egammaf     = .577215665F, // (0x1.2788d0P-1) https://oeis.org/A001620
                ln2f        = .693147181F, // (0x1.62e430P-1) https://oeis.org/A002162
                ln10f       = 2.30258509F, // (0x1.26bb1cP+1) https://oeis.org/A002392
                log2ef      = 1.44269504F, // (0x1.715476P+0)
                log10ef     = .434294482F, // (0x1.bcb7b2P-2)
                pif         = 3.14159265F, // (0x1.921fb6P+1) https://oeis.org/A000796
                inv_pif     = .318309886F, // (0x1.45f306P-2) https://oeis.org/A049541
                sqrtpif     = 1.77245385F, // (0x1.c5bf8aP+0) https://oeis.org/A002161
                inv_sqrtpif = .564189584F, // (0x1.20dd76P-1) https://oeis.org/A087197
                sqrt2f      = 1.41421356F, // (0x1.6a09e6P+0) https://oeis.org/A002193
                inv_sqrt2f  = .707106781F, // (0x1.6a09e6P-1)
                sqrt3f      = 1.73205081F, // (0x1.bb67aeP+0) https://oeis.org/A002194
                inv_sqrt3f  = .577350269F, // (0x1.279a74P-1)
                phif        = 1.61803399F; // (0x1.9e377aP+0) https://oeis.org/A001622
} // namespace numbers

/// Create a bitmask with the N right-most bits set to 1, and all other
/// bits set to 0.  Only unsigned types are allowed.
template <typename T> T maskTrailingOnes(unsigned N) {
  static_assert(std::is_unsigned_v<T>, "Invalid type!");
  const unsigned Bits = CHAR_BIT * sizeof(T);
  assert(N <= Bits && "Invalid bit index");
  if (N == 0)
    return 0;
  return T(-1) >> (Bits - N);
}

/// Create a bitmask with the N left-most bits set to 1, and all other
/// bits set to 0.  Only unsigned types are allowed.
template <typename T> T maskLeadingOnes(unsigned N) {
  return ~maskTrailingOnes<T>(CHAR_BIT * sizeof(T) - N);
}

/// Create a bitmask with the N right-most bits set to 0, and all other
/// bits set to 1.  Only unsigned types are allowed.
template <typename T> T maskTrailingZeros(unsigned N) {
  return maskLeadingOnes<T>(CHAR_BIT * sizeof(T) - N);
}

/// Create a bitmask with the N left-most bits set to 0, and all other
/// bits set to 1.  Only unsigned types are allowed.
template <typename T> T maskLeadingZeros(unsigned N) {
  return maskTrailingOnes<T>(CHAR_BIT * sizeof(T) - N);
}

/// Macro compressed bit reversal table for 256 bits.
///
/// http://graphics.stanford.edu/~seander/bithacks.html#BitReverseTable
static const unsigned char BitReverseTable256[256] = {
#define R2(n) n, n + 2 * 64, n + 1 * 64, n + 3 * 64
#define R4(n) R2(n), R2(n + 2 * 16), R2(n + 1 * 16), R2(n + 3 * 16)
#define R6(n) R4(n), R4(n + 2 * 4), R4(n + 1 * 4), R4(n + 3 * 4)
  R6(0), R6(2), R6(1), R6(3)
#undef R2
#undef R4
#undef R6
};

/// Reverse the bits in \p Val.
template <typename T> T reverseBits(T Val) {
#if __has_builtin(__builtin_bitreverse8)
  if constexpr (std::is_same_v<T, uint8_t>)
    return __builtin_bitreverse8(Val);
#endif
#if __has_builtin(__builtin_bitreverse16)
  if constexpr (std::is_same_v<T, uint16_t>)
    return __builtin_bitreverse16(Val);
#endif
#if __has_builtin(__builtin_bitreverse32)
  if constexpr (std::is_same_v<T, uint32_t>)
    return __builtin_bitreverse32(Val);
#endif
#if __has_builtin(__builtin_bitreverse64)
  if constexpr (std::is_same_v<T, uint64_t>)
    return __builtin_bitreverse64(Val);
#endif

  unsigned char in[sizeof(Val)];
  unsigned char out[sizeof(Val)];
  std::memcpy(in, &Val, sizeof(Val));
  for (unsigned i = 0; i < sizeof(Val); ++i)
    out[(sizeof(Val) - i) - 1] = BitReverseTable256[in[i]];
  std::memcpy(&Val, out, sizeof(Val));
  return Val;
}

// NOTE: The following support functions use the _32/_64 extensions instead of
// type overloading so that signed and unsigned integers can be used without
// ambiguity.

/// Return the high 32 bits of a 64 bit value.
constexpr uint32_t Hi_32(uint64_t Value) {
  return static_cast<uint32_t>(Value >> 32);
}

/// Return the low 32 bits of a 64 bit value.
constexpr uint32_t Lo_32(uint64_t Value) {
  return static_cast<uint32_t>(Value);
}

/// Make a 64-bit integer from a high / low pair of 32-bit integers.
constexpr uint64_t Make_64(uint32_t High, uint32_t Low) {
  return ((uint64_t)High << 32) | (uint64_t)Low;
}

/// Checks if an integer fits into the given bit width.
template <unsigned N> constexpr bool isInt(int64_t x) {
  if constexpr (N == 0)
    return 0 == x;
  if constexpr (N == 8)
    return static_cast<int8_t>(x) == x;
  if constexpr (N == 16)
    return static_cast<int16_t>(x) == x;
  if constexpr (N == 32)
    return static_cast<int32_t>(x) == x;
  if constexpr (N < 64)
    return -(INT64_C(1) << (N - 1)) <= x && x < (INT64_C(1) << (N - 1));
  (void)x; // MSVC v19.25 warns that x is unused.
  return true;
}

/// Checks if a signed integer is an N bit number shifted left by S.
template <unsigned N, unsigned S>
constexpr bool isShiftedInt(int64_t x) {
  static_assert(S < 64, "isShiftedInt<N, S> with S >= 64 is too much.");
  static_assert(N + S <= 64, "isShiftedInt<N, S> with N + S > 64 is too wide.");
  return isInt<N + S>(x) && (x % (UINT64_C(1) << S) == 0);
}

/// Checks if an unsigned integer fits into the given bit width.
template <unsigned N> constexpr bool isUInt(uint64_t x) {
  if constexpr (N == 0)
    return 0 == x;
  if constexpr (N == 8)
    return static_cast<uint8_t>(x) == x;
  if constexpr (N == 16)
    return static_cast<uint16_t>(x) == x;
  if constexpr (N == 32)
    return static_cast<uint32_t>(x) == x;
  if constexpr (N < 64)
    return x < (UINT64_C(1) << (N));
  (void)x; // MSVC v19.25 warns that x is unused.
  return true;
}

/// Checks if a unsigned integer is an N bit number shifted left by S.
template <unsigned N, unsigned S>
constexpr bool isShiftedUInt(uint64_t x) {
  static_assert(S < 64, "isShiftedUInt<N, S> with S >= 64 is too much.");
  static_assert(N + S <= 64,
                "isShiftedUInt<N, S> with N + S > 64 is too wide.");
  // S must be strictly less than 64. So 1 << S is not undefined behavior.
  return isUInt<N + S>(x) && (x % (UINT64_C(1) << S) == 0);
}

/// Gets the maximum value for a N-bit unsigned integer.
inline uint64_t maxUIntN(uint64_t N) {
  assert(N <= 64 && "integer width out of range");

  // uint64_t(1) << 64 is undefined behavior, so we can't do
  //   (uint64_t(1) << N) - 1
  // without checking first that N != 64.  But this works and doesn't have a
  // branch for N != 0.
  // Unfortunately, shifting a uint64_t right by 64 bit is undefined
  // behavior, so the condition on N == 0 is necessary. Fortunately, most
  // optimizers do not emit branches for this check.
  if (N == 0)
    return 0;
  return UINT64_MAX >> (64 - N);
}

/// Gets the minimum value for a N-bit signed integer.
inline int64_t minIntN(int64_t N) {
  assert(N <= 64 && "integer width out of range");

  if (N == 0)
    return 0;
  return UINT64_C(1) + ~(UINT64_C(1) << (N - 1));
}

/// Gets the maximum value for a N-bit signed integer.
inline int64_t maxIntN(int64_t N) {
  assert(N <= 64 && "integer width out of range");

  // This relies on two's complement wraparound when N == 64, so we convert to
  // int64_t only at the very end to avoid UB.
  if (N == 0)
    return 0;
  return (UINT64_C(1) << (N - 1)) - 1;
}

/// Checks if an unsigned integer fits into the given (dynamic) bit width.
inline bool isUIntN(unsigned N, uint64_t x) {
  return N >= 64 || x <= maxUIntN(N);
}

/// Checks if an signed integer fits into the given (dynamic) bit width.
inline bool isIntN(unsigned N, int64_t x) {
  return N >= 64 || (minIntN(N) <= x && x <= maxIntN(N));
}

/// Return true if the argument is a non-empty sequence of ones starting at the
/// least significant bit with the remainder zero (32 bit version).
/// Ex. isMask_32(0x0000FFFFU) == true.
constexpr bool isMask_32(uint32_t Value) {
  return Value && ((Value + 1) & Value) == 0;
}

/// Return true if the argument is a non-empty sequence of ones starting at the
/// least significant bit with the remainder zero (64 bit version).
constexpr bool isMask_64(uint64_t Value) {
  return Value && ((Value + 1) & Value) == 0;
}

/// Return true if the argument contains a non-empty sequence of ones with the
/// remainder zero (32 bit version.) Ex. isShiftedMask_32(0x0000FF00U) == true.
constexpr bool isShiftedMask_32(uint32_t Value) {
  return Value && isMask_32((Value - 1) | Value);
}

/// Return true if the argument contains a non-empty sequence of ones with the
/// remainder zero (64 bit version.)
constexpr bool isShiftedMask_64(uint64_t Value) {
  return Value && isMask_64((Value - 1) | Value);
}

/// Return true if the argument is a power of two > 0.
/// Ex. isPowerOf2_32(0x00100000U) == true (32 bit edition.)
constexpr bool isPowerOf2_32(uint32_t Value) {
  return llvm::has_single_bit(Value);
}

/// Return true if the argument is a power of two > 0 (64 bit edition.)
constexpr bool isPowerOf2_64(uint64_t Value) {
  return llvm::has_single_bit(Value);
}

/// Return true if the argument contains a non-empty sequence of ones with the
/// remainder zero (32 bit version.) Ex. isShiftedMask_32(0x0000FF00U) == true.
/// If true, \p MaskIdx will specify the index of the lowest set bit and \p
/// MaskLen is updated to specify the length of the mask, else neither are
/// updated.
inline bool isShiftedMask_32(uint32_t Value, unsigned &MaskIdx,
                             unsigned &MaskLen) {
  if (!isShiftedMask_32(Value))
    return false;
  MaskIdx = llvm::countr_zero(Value);
  MaskLen = llvm::popcount(Value);
  return true;
}

/// Return true if the argument contains a non-empty sequence of ones with the
/// remainder zero (64 bit version.) If true, \p MaskIdx will specify the index
/// of the lowest set bit and \p MaskLen is updated to specify the length of the
/// mask, else neither are updated.
inline bool isShiftedMask_64(uint64_t Value, unsigned &MaskIdx,
                             unsigned &MaskLen) {
  if (!isShiftedMask_64(Value))
    return false;
  MaskIdx = llvm::countr_zero(Value);
  MaskLen = llvm::popcount(Value);
  return true;
}

/// Compile time Log2.
/// Valid only for positive powers of two.
template <size_t kValue> constexpr size_t CTLog2() {
  static_assert(kValue > 0 && llvm::isPowerOf2_64(kValue),
                "Value is not a valid power of 2");
  return 1 + CTLog2<kValue / 2>();
}

template <> constexpr size_t CTLog2<1>() { return 0; }

/// Return the floor log base 2 of the specified value, -1 if the value is zero.
/// (32 bit edition.)
/// Ex. Log2_32(32) == 5, Log2_32(1) == 0, Log2_32(0) == -1, Log2_32(6) == 2
inline unsigned Log2_32(uint32_t Value) {
  return 31 - llvm::countl_zero(Value);
}

/// Return the floor log base 2 of the specified value, -1 if the value is zero.
/// (64 bit edition.)
inline unsigned Log2_64(uint64_t Value) {
  return 63 - llvm::countl_zero(Value);
}

/// Return the ceil log base 2 of the specified value, 32 if the value is zero.
/// (32 bit edition).
/// Ex. Log2_32_Ceil(32) == 5, Log2_32_Ceil(1) == 0, Log2_32_Ceil(6) == 3
inline unsigned Log2_32_Ceil(uint32_t Value) {
  return 32 - llvm::countl_zero(Value - 1);
}

/// Return the ceil log base 2 of the specified value, 64 if the value is zero.
/// (64 bit edition.)
inline unsigned Log2_64_Ceil(uint64_t Value) {
  return 64 - llvm::countl_zero(Value - 1);
}

/// A and B are either alignments or offsets. Return the minimum alignment that
/// may be assumed after adding the two together.
template <typename U, typename V, typename T = common_uint<U, V>>
constexpr T MinAlign(U A, V B) {
  // The largest power of 2 that divides both A and B.
  //
  // Replace "-Value" by "1+~Value" in the following commented code to avoid
  // MSVC warning C4146
  //    return (A | B) & -(A | B);
  return (A | B) & (1 + ~(A | B));
}

/// Fallback when arguments aren't integral.
constexpr uint64_t MinAlign(uint64_t A, uint64_t B) {
  return (A | B) & (1 + ~(A | B));
}

/// Returns the next power of two (in 64-bits) that is strictly greater than A.
/// Returns zero on overflow.
constexpr uint64_t NextPowerOf2(uint64_t A) {
  A |= (A >> 1);
  A |= (A >> 2);
  A |= (A >> 4);
  A |= (A >> 8);
  A |= (A >> 16);
  A |= (A >> 32);
  return A + 1;
}

/// Returns the power of two which is greater than or equal to the given value.
/// Essentially, it is a ceil operation across the domain of powers of two.
inline uint64_t PowerOf2Ceil(uint64_t A) {
  if (!A || A > UINT64_MAX / 2)
    return 0;
  return UINT64_C(1) << Log2_64_Ceil(A);
}

/// Returns the integer ceil(Numerator / Denominator). Unsigned version.
/// Guaranteed to never overflow.
template <typename U, typename V, typename T = common_uint<U, V>>
constexpr T divideCeil(U Numerator, V Denominator) {
  assert(Denominator && "Division by zero");
  T Bias = (Numerator != 0);
  return (Numerator - Bias) / Denominator + Bias;
}

/// Fallback when arguments aren't integral.
constexpr uint64_t divideCeil(uint64_t Numerator, uint64_t Denominator) {
  assert(Denominator && "Division by zero");
  uint64_t Bias = (Numerator != 0);
  return (Numerator - Bias) / Denominator + Bias;
}

// Check whether divideCeilSigned or divideFloorSigned would overflow. This
// happens only when Numerator = INT_MIN and Denominator = -1.
template <typename U, typename V>
constexpr bool divideSignedWouldOverflow(U Numerator, V Denominator) {
  return Numerator == std::numeric_limits<U>::min() && Denominator == -1;
}

/// Returns the integer ceil(Numerator / Denominator). Signed version.
/// Overflow is explicitly forbidden with an assert.
template <typename U, typename V, typename T = common_sint<U, V>>
constexpr T divideCeilSigned(U Numerator, V Denominator) {
  assert(Denominator && "Division by zero");
  assert(!divideSignedWouldOverflow(Numerator, Denominator) &&
         "Divide would overflow");
  if (!Numerator)
    return 0;
  // C's integer division rounds towards 0.
  T Bias = Denominator >= 0 ? 1 : -1;
  bool SameSign = (Numerator >= 0) == (Denominator >= 0);
  return SameSign ? (Numerator - Bias) / Denominator + 1
                  : Numerator / Denominator;
}

/// Returns the integer floor(Numerator / Denominator). Signed version.
/// Overflow is explicitly forbidden with an assert.
template <typename U, typename V, typename T = common_sint<U, V>>
constexpr T divideFloorSigned(U Numerator, V Denominator) {
  assert(Denominator && "Division by zero");
  assert(!divideSignedWouldOverflow(Numerator, Denominator) &&
         "Divide would overflow");
  if (!Numerator)
    return 0;
  // C's integer division rounds towards 0.
  T Bias = Denominator >= 0 ? -1 : 1;
  bool SameSign = (Numerator >= 0) == (Denominator >= 0);
  return SameSign ? Numerator / Denominator
                  : (Numerator - Bias) / Denominator - 1;
}

/// Returns the remainder of the Euclidean division of LHS by RHS. Result is
/// always non-negative.
template <typename U, typename V, typename T = common_sint<U, V>>
constexpr T mod(U Numerator, V Denominator) {
  assert(Denominator >= 1 && "Mod by non-positive number");
  T Mod = Numerator % Denominator;
  return Mod < 0 ? Mod + Denominator : Mod;
}

/// Returns (Numerator / Denominator) rounded by round-half-up. Guaranteed to
/// never overflow.
template <typename U, typename V, typename T = common_uint<U, V>>
constexpr T divideNearest(U Numerator, V Denominator) {
  assert(Denominator && "Division by zero");
  T Mod = Numerator % Denominator;
  return (Numerator / Denominator) +
         (Mod > (static_cast<T>(Denominator) - 1) / 2);
}

/// Returns the next integer (mod 2**nbits) that is greater than or equal to
/// \p Value and is a multiple of \p Align. \p Align must be non-zero.
///
/// Examples:
/// \code
///   alignTo(5, 8) = 8
///   alignTo(17, 8) = 24
///   alignTo(~0LL, 8) = 0
///   alignTo(321, 255) = 510
/// \endcode
///
/// Will overflow only if result is not representable in T.
template <typename U, typename V, typename T = common_uint<U, V>>
constexpr T alignTo(U Value, V Align) {
  assert(Align != 0u && "Align can't be 0.");
  T CeilDiv = divideCeil(Value, Align);
  return CeilDiv * Align;
}

/// Fallback when arguments aren't integral.
constexpr uint64_t alignTo(uint64_t Value, uint64_t Align) {
  assert(Align != 0u && "Align can't be 0.");
  uint64_t CeilDiv = divideCeil(Value, Align);
  return CeilDiv * Align;
}

constexpr uint64_t alignToPowerOf2(uint64_t Value, uint64_t Align) {
  assert(Align != 0 && (Align & (Align - 1)) == 0 &&
         "Align must be a power of 2");
  // Replace unary minus to avoid compilation error on Windows:
  // "unary minus operator applied to unsigned type, result still unsigned"
  uint64_t NegAlign = (~Align) + 1;
  return (Value + Align - 1) & NegAlign;
}

/// If non-zero \p Skew is specified, the return value will be a minimal integer
/// that is greater than or equal to \p Size and equal to \p A * N + \p Skew for
/// some integer N. If \p Skew is larger than \p A, its value is adjusted to '\p
/// Skew mod \p A'. \p Align must be non-zero.
///
/// Examples:
/// \code
///   alignTo(5, 8, 7) = 7
///   alignTo(17, 8, 1) = 17
///   alignTo(~0LL, 8, 3) = 3
///   alignTo(321, 255, 42) = 552
/// \endcode
///
/// May overflow.
template <typename U, typename V, typename W,
          typename T = common_uint<common_uint<U, V>, W>>
constexpr T alignTo(U Value, V Align, W Skew) {
  assert(Align != 0u && "Align can't be 0.");
  Skew %= Align;
  return alignTo(Value - Skew, Align) + Skew;
}

/// Returns the next integer (mod 2**nbits) that is greater than or equal to
/// \p Value and is a multiple of \c Align. \c Align must be non-zero.
///
/// Will overflow only if result is not representable in T.
template <auto Align, typename V, typename T = common_uint<decltype(Align), V>>
constexpr T alignTo(V Value) {
  static_assert(Align != 0u, "Align must be non-zero");
  T CeilDiv = divideCeil(Value, Align);
  return CeilDiv * Align;
}

/// Returns the largest unsigned integer less than or equal to \p Value and is
/// \p Skew mod \p Align. \p Align must be non-zero. Guaranteed to never
/// overflow.
template <typename U, typename V, typename W = uint8_t,
          typename T = common_uint<common_uint<U, V>, W>>
constexpr T alignDown(U Value, V Align, W Skew = 0) {
  assert(Align != 0u && "Align can't be 0.");
  Skew %= Align;
  return (Value - Skew) / Align * Align + Skew;
}

/// Sign-extend the number in the bottom B bits of X to a 32-bit integer.
/// Requires B <= 32.
template <unsigned B> constexpr int32_t SignExtend32(uint32_t X) {
  static_assert(B <= 32, "Bit width out of range.");
  if constexpr (B == 0)
    return 0;
  return int32_t(X << (32 - B)) >> (32 - B);
}

/// Sign-extend the number in the bottom B bits of X to a 32-bit integer.
/// Requires B <= 32.
inline int32_t SignExtend32(uint32_t X, unsigned B) {
  assert(B <= 32 && "Bit width out of range.");
  if (B == 0)
    return 0;
  return int32_t(X << (32 - B)) >> (32 - B);
}

/// Sign-extend the number in the bottom B bits of X to a 64-bit integer.
/// Requires B <= 64.
template <unsigned B> constexpr int64_t SignExtend64(uint64_t x) {
  static_assert(B <= 64, "Bit width out of range.");
  if constexpr (B == 0)
    return 0;
  return int64_t(x << (64 - B)) >> (64 - B);
}

/// Sign-extend the number in the bottom B bits of X to a 64-bit integer.
/// Requires B <= 64.
inline int64_t SignExtend64(uint64_t X, unsigned B) {
  assert(B <= 64 && "Bit width out of range.");
  if (B == 0)
    return 0;
  return int64_t(X << (64 - B)) >> (64 - B);
}

/// Subtract two unsigned integers, X and Y, of type T and return the absolute
/// value of the result.
template <typename U, typename V, typename T = common_uint<U, V>>
constexpr T AbsoluteDifference(U X, V Y) {
  return X > Y ? (X - Y) : (Y - X);
}

/// Add two unsigned integers, X and Y, of type T.  Clamp the result to the
/// maximum representable value of T on overflow.  ResultOverflowed indicates if
/// the result is larger than the maximum representable value of type T.
template <typename T>
std::enable_if_t<std::is_unsigned_v<T>, T>
SaturatingAdd(T X, T Y, bool *ResultOverflowed = nullptr) {
  bool Dummy;
  bool &Overflowed = ResultOverflowed ? *ResultOverflowed : Dummy;
  // Hacker's Delight, p. 29
  T Z = X + Y;
  Overflowed = (Z < X || Z < Y);
  if (Overflowed)
    return std::numeric_limits<T>::max();
  else
    return Z;
}

/// Add multiple unsigned integers of type T.  Clamp the result to the
/// maximum representable value of T on overflow.
template <class T, class... Ts>
std::enable_if_t<std::is_unsigned_v<T>, T> SaturatingAdd(T X, T Y, T Z,
                                                         Ts... Args) {
  bool Overflowed = false;
  T XY = SaturatingAdd(X, Y, &Overflowed);
  if (Overflowed)
    return SaturatingAdd(std::numeric_limits<T>::max(), T(1), Args...);
  return SaturatingAdd(XY, Z, Args...);
}

/// Multiply two unsigned integers, X and Y, of type T.  Clamp the result to the
/// maximum representable value of T on overflow.  ResultOverflowed indicates if
/// the result is larger than the maximum representable value of type T.
template <typename T>
std::enable_if_t<std::is_unsigned_v<T>, T>
SaturatingMultiply(T X, T Y, bool *ResultOverflowed = nullptr) {
  bool Dummy;
  bool &Overflowed = ResultOverflowed ? *ResultOverflowed : Dummy;

  // Hacker's Delight, p. 30 has a different algorithm, but we don't use that
  // because it fails for uint16_t (where multiplication can have undefined
  // behavior due to promotion to int), and requires a division in addition
  // to the multiplication.

  Overflowed = false;

  // Log2(Z) would be either Log2Z or Log2Z + 1.
  // Special case: if X or Y is 0, Log2_64 gives -1, and Log2Z
  // will necessarily be less than Log2Max as desired.
  int Log2Z = Log2_64(X) + Log2_64(Y);
  const T Max = std::numeric_limits<T>::max();
  int Log2Max = Log2_64(Max);
  if (Log2Z < Log2Max) {
    return X * Y;
  }
  if (Log2Z > Log2Max) {
    Overflowed = true;
    return Max;
  }

  // We're going to use the top bit, and maybe overflow one
  // bit past it. Multiply all but the bottom bit then add
  // that on at the end.
  T Z = (X >> 1) * Y;
  if (Z & ~(Max >> 1)) {
    Overflowed = true;
    return Max;
  }
  Z <<= 1;
  if (X & 1)
    return SaturatingAdd(Z, Y, ResultOverflowed);

  return Z;
}

/// Multiply two unsigned integers, X and Y, and add the unsigned integer, A to
/// the product. Clamp the result to the maximum representable value of T on
/// overflow. ResultOverflowed indicates if the result is larger than the
/// maximum representable value of type T.
template <typename T>
std::enable_if_t<std::is_unsigned_v<T>, T>
SaturatingMultiplyAdd(T X, T Y, T A, bool *ResultOverflowed = nullptr) {
  bool Dummy;
  bool &Overflowed = ResultOverflowed ? *ResultOverflowed : Dummy;

  T Product = SaturatingMultiply(X, Y, &Overflowed);
  if (Overflowed)
    return Product;

  return SaturatingAdd(A, Product, &Overflowed);
}

/// Use this rather than HUGE_VALF; the latter causes warnings on MSVC.
extern const float huge_valf;

/// Add two signed integers, computing the two's complement truncated result,
/// returning true if overflow occurred.
template <typename T>
std::enable_if_t<std::is_signed_v<T>, T> AddOverflow(T X, T Y, T &Result) {
#if __has_builtin(__builtin_add_overflow)
  return __builtin_add_overflow(X, Y, &Result);
#else
  // Perform the unsigned addition.
  using U = std::make_unsigned_t<T>;
  const U UX = static_cast<U>(X);
  const U UY = static_cast<U>(Y);
  const U UResult = UX + UY;

  // Convert to signed.
  Result = static_cast<T>(UResult);

  // Adding two positive numbers should result in a positive number.
  if (X > 0 && Y > 0)
    return Result <= 0;
  // Adding two negatives should result in a negative number.
  if (X < 0 && Y < 0)
    return Result >= 0;
  return false;
#endif
}

/// Subtract two signed integers, computing the two's complement truncated
/// result, returning true if an overflow ocurred.
template <typename T>
std::enable_if_t<std::is_signed_v<T>, T> SubOverflow(T X, T Y, T &Result) {
#if __has_builtin(__builtin_sub_overflow)
  return __builtin_sub_overflow(X, Y, &Result);
#else
  // Perform the unsigned addition.
  using U = std::make_unsigned_t<T>;
  const U UX = static_cast<U>(X);
  const U UY = static_cast<U>(Y);
  const U UResult = UX - UY;

  // Convert to signed.
  Result = static_cast<T>(UResult);

  // Subtracting a positive number from a negative results in a negative number.
  if (X <= 0 && Y > 0)
    return Result >= 0;
  // Subtracting a negative number from a positive results in a positive number.
  if (X >= 0 && Y < 0)
    return Result <= 0;
  return false;
#endif
}

/// Multiply two signed integers, computing the two's complement truncated
/// result, returning true if an overflow ocurred.
template <typename T>
std::enable_if_t<std::is_signed_v<T>, T> MulOverflow(T X, T Y, T &Result) {
#if __has_builtin(__builtin_mul_overflow)
  return __builtin_mul_overflow(X, Y, &Result);
#else
  // Perform the unsigned multiplication on absolute values.
  using U = std::make_unsigned_t<T>;
  const U UX = X < 0 ? (0 - static_cast<U>(X)) : static_cast<U>(X);
  const U UY = Y < 0 ? (0 - static_cast<U>(Y)) : static_cast<U>(Y);
  const U UResult = UX * UY;

  // Convert to signed.
  const bool IsNegative = (X < 0) ^ (Y < 0);
  Result = IsNegative ? (0 - UResult) : UResult;

  // If any of the args was 0, result is 0 and no overflow occurs.
  if (UX == 0 || UY == 0)
    return false;

  // UX and UY are in [1, 2^n], where n is the number of digits.
  // Check how the max allowed absolute value (2^n for negative, 2^(n-1) for
  // positive) divided by an argument compares to the other.
  if (IsNegative)
    return UX > (static_cast<U>(std::numeric_limits<T>::max()) + U(1)) / UY;
  else
    return UX > (static_cast<U>(std::numeric_limits<T>::max())) / UY;
#endif
}

/// Type to force float point values onto the stack, so that x86 doesn't add
/// hidden precision, avoiding rounding differences on various platforms.
#if defined(__i386__) || defined(_M_IX86)
using stack_float_t = volatile float;
#else
using stack_float_t = float;
#endif

} // namespace llvm

#endif
