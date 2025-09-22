//===-- llvm/ADT/bit.h - C++20 <bit> ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the C++20 <bit> header.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_BIT_H
#define LLVM_ADT_BIT_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <limits>
#include <type_traits>

#if !__has_builtin(__builtin_bit_cast)
#include <cstring>
#endif

#if defined(_MSC_VER) && !defined(_DEBUG)
#include <cstdlib>  // for _byteswap_{ushort,ulong,uint64}
#endif

#if defined(__linux__) || defined(__GNU__) || defined(__HAIKU__) ||            \
    defined(__Fuchsia__) || defined(__EMSCRIPTEN__) || defined(__NetBSD__) ||  \
    defined(__OpenBSD__) || defined(__DragonFly__)
#include <endian.h>
#elif defined(_AIX)
#include <sys/machine.h>
#elif defined(__sun)
/* Solaris provides _BIG_ENDIAN/_LITTLE_ENDIAN selector in sys/types.h */
#include <sys/types.h>
#define BIG_ENDIAN 4321
#define LITTLE_ENDIAN 1234
#if defined(_BIG_ENDIAN)
#define BYTE_ORDER BIG_ENDIAN
#else
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#elif defined(__MVS__)
#define BIG_ENDIAN 4321
#define LITTLE_ENDIAN 1234
#define BYTE_ORDER BIG_ENDIAN
#else
#if !defined(BYTE_ORDER) && !defined(_WIN32)
#include <machine/endian.h>
#endif
#endif

#ifdef _MSC_VER
// Declare these intrinsics manually rather including intrin.h. It's very
// expensive, and bit.h is popular via MathExtras.h.
// #include <intrin.h>
extern "C" {
unsigned char _BitScanForward(unsigned long *_Index, unsigned long _Mask);
unsigned char _BitScanForward64(unsigned long *_Index, unsigned __int64 _Mask);
unsigned char _BitScanReverse(unsigned long *_Index, unsigned long _Mask);
unsigned char _BitScanReverse64(unsigned long *_Index, unsigned __int64 _Mask);
}
#endif

namespace llvm {

enum class endianness {
  big,
  little,
#if defined(BYTE_ORDER) && defined(BIG_ENDIAN) && BYTE_ORDER == BIG_ENDIAN
  native = big
#else
  native = little
#endif
};

// This implementation of bit_cast is different from the C++20 one in two ways:
//  - It isn't constexpr because that requires compiler support.
//  - It requires trivially-constructible To, to avoid UB in the implementation.
template <
    typename To, typename From,
    typename = std::enable_if_t<sizeof(To) == sizeof(From)>,
    typename = std::enable_if_t<std::is_trivially_constructible<To>::value>,
    typename = std::enable_if_t<std::is_trivially_copyable<To>::value>,
    typename = std::enable_if_t<std::is_trivially_copyable<From>::value>>
[[nodiscard]] inline To bit_cast(const From &from) noexcept {
#if __has_builtin(__builtin_bit_cast)
  return __builtin_bit_cast(To, from);
#else
  To to;
  std::memcpy(&to, &from, sizeof(To));
  return to;
#endif
}

/// Reverses the bytes in the given integer value V.
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
[[nodiscard]] constexpr T byteswap(T V) noexcept {
  if constexpr (sizeof(T) == 1) {
    return V;
  } else if constexpr (sizeof(T) == 2) {
    uint16_t UV = V;
#if defined(_MSC_VER) && !defined(_DEBUG)
    // The DLL version of the runtime lacks these functions (bug!?), but in a
    // release build they're replaced with BSWAP instructions anyway.
    return _byteswap_ushort(UV);
#else
    uint16_t Hi = UV << 8;
    uint16_t Lo = UV >> 8;
    return Hi | Lo;
#endif
  } else if constexpr (sizeof(T) == 4) {
    uint32_t UV = V;
#if __has_builtin(__builtin_bswap32)
    return __builtin_bswap32(UV);
#elif defined(_MSC_VER) && !defined(_DEBUG)
    return _byteswap_ulong(UV);
#else
    uint32_t Byte0 = UV & 0x000000FF;
    uint32_t Byte1 = UV & 0x0000FF00;
    uint32_t Byte2 = UV & 0x00FF0000;
    uint32_t Byte3 = UV & 0xFF000000;
    return (Byte0 << 24) | (Byte1 << 8) | (Byte2 >> 8) | (Byte3 >> 24);
#endif
  } else if constexpr (sizeof(T) == 8) {
    uint64_t UV = V;
#if __has_builtin(__builtin_bswap64)
    return __builtin_bswap64(UV);
#elif defined(_MSC_VER) && !defined(_DEBUG)
    return _byteswap_uint64(UV);
#else
    uint64_t Hi = llvm::byteswap<uint32_t>(UV);
    uint32_t Lo = llvm::byteswap<uint32_t>(UV >> 32);
    return (Hi << 32) | Lo;
#endif
  } else {
    static_assert(!sizeof(T *), "Don't know how to handle the given type.");
    return 0;
  }
}

template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
[[nodiscard]] constexpr inline bool has_single_bit(T Value) noexcept {
  return (Value != 0) && ((Value & (Value - 1)) == 0);
}

namespace detail {
template <typename T, std::size_t SizeOfT> struct TrailingZerosCounter {
  static unsigned count(T Val) {
    if (!Val)
      return std::numeric_limits<T>::digits;
    if (Val & 0x1)
      return 0;

    // Bisection method.
    unsigned ZeroBits = 0;
    T Shift = std::numeric_limits<T>::digits >> 1;
    T Mask = std::numeric_limits<T>::max() >> Shift;
    while (Shift) {
      if ((Val & Mask) == 0) {
        Val >>= Shift;
        ZeroBits |= Shift;
      }
      Shift >>= 1;
      Mask >>= Shift;
    }
    return ZeroBits;
  }
};

#if defined(__GNUC__) || defined(_MSC_VER)
template <typename T> struct TrailingZerosCounter<T, 4> {
  static unsigned count(T Val) {
    if (Val == 0)
      return 32;

#if __has_builtin(__builtin_ctz) || defined(__GNUC__)
    return __builtin_ctz(Val);
#elif defined(_MSC_VER)
    unsigned long Index;
    _BitScanForward(&Index, Val);
    return Index;
#endif
  }
};

#if !defined(_MSC_VER) || defined(_M_X64)
template <typename T> struct TrailingZerosCounter<T, 8> {
  static unsigned count(T Val) {
    if (Val == 0)
      return 64;

#if __has_builtin(__builtin_ctzll) || defined(__GNUC__)
    return __builtin_ctzll(Val);
#elif defined(_MSC_VER)
    unsigned long Index;
    _BitScanForward64(&Index, Val);
    return Index;
#endif
  }
};
#endif
#endif
} // namespace detail

/// Count number of 0's from the least significant bit to the most
///   stopping at the first 1.
///
/// Only unsigned integral types are allowed.
///
/// Returns std::numeric_limits<T>::digits on an input of 0.
template <typename T> [[nodiscard]] int countr_zero(T Val) {
  static_assert(std::is_unsigned_v<T>,
                "Only unsigned integral types are allowed.");
  return llvm::detail::TrailingZerosCounter<T, sizeof(T)>::count(Val);
}

namespace detail {
template <typename T, std::size_t SizeOfT> struct LeadingZerosCounter {
  static unsigned count(T Val) {
    if (!Val)
      return std::numeric_limits<T>::digits;

    // Bisection method.
    unsigned ZeroBits = 0;
    for (T Shift = std::numeric_limits<T>::digits >> 1; Shift; Shift >>= 1) {
      T Tmp = Val >> Shift;
      if (Tmp)
        Val = Tmp;
      else
        ZeroBits |= Shift;
    }
    return ZeroBits;
  }
};

#if defined(__GNUC__) || defined(_MSC_VER)
template <typename T> struct LeadingZerosCounter<T, 4> {
  static unsigned count(T Val) {
    if (Val == 0)
      return 32;

#if __has_builtin(__builtin_clz) || defined(__GNUC__)
    return __builtin_clz(Val);
#elif defined(_MSC_VER)
    unsigned long Index;
    _BitScanReverse(&Index, Val);
    return Index ^ 31;
#endif
  }
};

#if !defined(_MSC_VER) || defined(_M_X64)
template <typename T> struct LeadingZerosCounter<T, 8> {
  static unsigned count(T Val) {
    if (Val == 0)
      return 64;

#if __has_builtin(__builtin_clzll) || defined(__GNUC__)
    return __builtin_clzll(Val);
#elif defined(_MSC_VER)
    unsigned long Index;
    _BitScanReverse64(&Index, Val);
    return Index ^ 63;
#endif
  }
};
#endif
#endif
} // namespace detail

/// Count number of 0's from the most significant bit to the least
///   stopping at the first 1.
///
/// Only unsigned integral types are allowed.
///
/// Returns std::numeric_limits<T>::digits on an input of 0.
template <typename T> [[nodiscard]] int countl_zero(T Val) {
  static_assert(std::is_unsigned_v<T>,
                "Only unsigned integral types are allowed.");
  return llvm::detail::LeadingZerosCounter<T, sizeof(T)>::count(Val);
}

/// Count the number of ones from the most significant bit to the first
/// zero bit.
///
/// Ex. countl_one(0xFF0FFF00) == 8.
/// Only unsigned integral types are allowed.
///
/// Returns std::numeric_limits<T>::digits on an input of all ones.
template <typename T> [[nodiscard]] int countl_one(T Value) {
  static_assert(std::is_unsigned_v<T>,
                "Only unsigned integral types are allowed.");
  return llvm::countl_zero<T>(~Value);
}

/// Count the number of ones from the least significant bit to the first
/// zero bit.
///
/// Ex. countr_one(0x00FF00FF) == 8.
/// Only unsigned integral types are allowed.
///
/// Returns std::numeric_limits<T>::digits on an input of all ones.
template <typename T> [[nodiscard]] int countr_one(T Value) {
  static_assert(std::is_unsigned_v<T>,
                "Only unsigned integral types are allowed.");
  return llvm::countr_zero<T>(~Value);
}

/// Returns the number of bits needed to represent Value if Value is nonzero.
/// Returns 0 otherwise.
///
/// Ex. bit_width(5) == 3.
template <typename T> [[nodiscard]] int bit_width(T Value) {
  static_assert(std::is_unsigned_v<T>,
                "Only unsigned integral types are allowed.");
  return std::numeric_limits<T>::digits - llvm::countl_zero(Value);
}

/// Returns the largest integral power of two no greater than Value if Value is
/// nonzero.  Returns 0 otherwise.
///
/// Ex. bit_floor(5) == 4.
template <typename T> [[nodiscard]] T bit_floor(T Value) {
  static_assert(std::is_unsigned_v<T>,
                "Only unsigned integral types are allowed.");
  if (!Value)
    return 0;
  return T(1) << (llvm::bit_width(Value) - 1);
}

/// Returns the smallest integral power of two no smaller than Value if Value is
/// nonzero.  Returns 1 otherwise.
///
/// Ex. bit_ceil(5) == 8.
///
/// The return value is undefined if the input is larger than the largest power
/// of two representable in T.
template <typename T> [[nodiscard]] T bit_ceil(T Value) {
  static_assert(std::is_unsigned_v<T>,
                "Only unsigned integral types are allowed.");
  if (Value < 2)
    return 1;
  return T(1) << llvm::bit_width<T>(Value - 1u);
}

namespace detail {
template <typename T, std::size_t SizeOfT> struct PopulationCounter {
  static int count(T Value) {
    // Generic version, forward to 32 bits.
    static_assert(SizeOfT <= 4, "Not implemented!");
#if defined(__GNUC__)
    return (int)__builtin_popcount(Value);
#else
    uint32_t v = Value;
    v = v - ((v >> 1) & 0x55555555);
    v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
    return int(((v + (v >> 4) & 0xF0F0F0F) * 0x1010101) >> 24);
#endif
  }
};

template <typename T> struct PopulationCounter<T, 8> {
  static int count(T Value) {
#if defined(__GNUC__)
    return (int)__builtin_popcountll(Value);
#else
    uint64_t v = Value;
    v = v - ((v >> 1) & 0x5555555555555555ULL);
    v = (v & 0x3333333333333333ULL) + ((v >> 2) & 0x3333333333333333ULL);
    v = (v + (v >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return int((uint64_t)(v * 0x0101010101010101ULL) >> 56);
#endif
  }
};
} // namespace detail

/// Count the number of set bits in a value.
/// Ex. popcount(0xF000F000) = 8
/// Returns 0 if the word is zero.
template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
[[nodiscard]] inline int popcount(T Value) noexcept {
  return detail::PopulationCounter<T, sizeof(T)>::count(Value);
}

// Forward-declare rotr so that rotl can use it.
template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
[[nodiscard]] constexpr T rotr(T V, int R);

template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
[[nodiscard]] constexpr T rotl(T V, int R) {
  unsigned N = std::numeric_limits<T>::digits;

  R = R % N;
  if (!R)
    return V;

  if (R < 0)
    return llvm::rotr(V, -R);

  return (V << R) | (V >> (N - R));
}

template <typename T, typename> [[nodiscard]] constexpr T rotr(T V, int R) {
  unsigned N = std::numeric_limits<T>::digits;

  R = R % N;
  if (!R)
    return V;

  if (R < 0)
    return llvm::rotl(V, -R);

  return (V >> R) | (V << (N - R));
}

} // namespace llvm

#endif
