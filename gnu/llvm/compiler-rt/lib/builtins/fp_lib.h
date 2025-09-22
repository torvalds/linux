//===-- lib/fp_lib.h - Floating-point utilities -------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a configuration header for soft-float routines in compiler-rt.
// This file does not provide any part of the compiler-rt interface, but defines
// many useful constants and utility routines that are used in the
// implementation of the soft-float routines in compiler-rt.
//
// Assumes that float, double and long double correspond to the IEEE-754
// binary32, binary64 and binary 128 types, respectively, and that integer
// endianness matches floating point endianness on the target platform.
//
//===----------------------------------------------------------------------===//

#ifndef FP_LIB_HEADER
#define FP_LIB_HEADER

#include "int_lib.h"
#include "int_math.h"
#include "int_types.h"
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#if defined SINGLE_PRECISION

typedef uint16_t half_rep_t;
typedef uint32_t rep_t;
typedef uint64_t twice_rep_t;
typedef int32_t srep_t;
typedef float fp_t;
#define HALF_REP_C UINT16_C
#define REP_C UINT32_C
#define significandBits 23

static __inline int rep_clz(rep_t a) { return clzsi(a); }

// 32x32 --> 64 bit multiply
static __inline void wideMultiply(rep_t a, rep_t b, rep_t *hi, rep_t *lo) {
  const uint64_t product = (uint64_t)a * b;
  *hi = (rep_t)(product >> 32);
  *lo = (rep_t)product;
}
COMPILER_RT_ABI fp_t __addsf3(fp_t a, fp_t b);

#elif defined DOUBLE_PRECISION

typedef uint32_t half_rep_t;
typedef uint64_t rep_t;
typedef int64_t srep_t;
typedef double fp_t;
#define HALF_REP_C UINT32_C
#define REP_C UINT64_C
#define significandBits 52

static inline int rep_clz(rep_t a) { return __builtin_clzll(a); }

#define loWord(a) (a & 0xffffffffU)
#define hiWord(a) (a >> 32)

// 64x64 -> 128 wide multiply for platforms that don't have such an operation;
// many 64-bit platforms have this operation, but they tend to have hardware
// floating-point, so we don't bother with a special case for them here.
static __inline void wideMultiply(rep_t a, rep_t b, rep_t *hi, rep_t *lo) {
  // Each of the component 32x32 -> 64 products
  const uint64_t plolo = loWord(a) * loWord(b);
  const uint64_t plohi = loWord(a) * hiWord(b);
  const uint64_t philo = hiWord(a) * loWord(b);
  const uint64_t phihi = hiWord(a) * hiWord(b);
  // Sum terms that contribute to lo in a way that allows us to get the carry
  const uint64_t r0 = loWord(plolo);
  const uint64_t r1 = hiWord(plolo) + loWord(plohi) + loWord(philo);
  *lo = r0 + (r1 << 32);
  // Sum terms contributing to hi with the carry from lo
  *hi = hiWord(plohi) + hiWord(philo) + hiWord(r1) + phihi;
}
#undef loWord
#undef hiWord

COMPILER_RT_ABI fp_t __adddf3(fp_t a, fp_t b);

#elif defined QUAD_PRECISION
#if defined(CRT_HAS_F128) && defined(CRT_HAS_128BIT)
typedef uint64_t half_rep_t;
typedef __uint128_t rep_t;
typedef __int128_t srep_t;
typedef tf_float fp_t;
#define HALF_REP_C UINT64_C
#define REP_C (__uint128_t)
#if defined(CRT_HAS_IEEE_TF)
// Note: Since there is no explicit way to tell compiler the constant is a
// 128-bit integer, we let the constant be casted to 128-bit integer
#define significandBits 112
#define TF_MANT_DIG (significandBits + 1)

static __inline int rep_clz(rep_t a) {
  const union {
    __uint128_t ll;
#if _YUGA_BIG_ENDIAN
    struct {
      uint64_t high, low;
    } s;
#else
    struct {
      uint64_t low, high;
    } s;
#endif
  } uu = {.ll = a};

  uint64_t word;
  uint64_t add;

  if (uu.s.high) {
    word = uu.s.high;
    add = 0;
  } else {
    word = uu.s.low;
    add = 64;
  }
  return __builtin_clzll(word) + add;
}

#define Word_LoMask UINT64_C(0x00000000ffffffff)
#define Word_HiMask UINT64_C(0xffffffff00000000)
#define Word_FullMask UINT64_C(0xffffffffffffffff)
#define Word_1(a) (uint64_t)((a >> 96) & Word_LoMask)
#define Word_2(a) (uint64_t)((a >> 64) & Word_LoMask)
#define Word_3(a) (uint64_t)((a >> 32) & Word_LoMask)
#define Word_4(a) (uint64_t)(a & Word_LoMask)

// 128x128 -> 256 wide multiply for platforms that don't have such an operation;
// many 64-bit platforms have this operation, but they tend to have hardware
// floating-point, so we don't bother with a special case for them here.
static __inline void wideMultiply(rep_t a, rep_t b, rep_t *hi, rep_t *lo) {

  const uint64_t product11 = Word_1(a) * Word_1(b);
  const uint64_t product12 = Word_1(a) * Word_2(b);
  const uint64_t product13 = Word_1(a) * Word_3(b);
  const uint64_t product14 = Word_1(a) * Word_4(b);
  const uint64_t product21 = Word_2(a) * Word_1(b);
  const uint64_t product22 = Word_2(a) * Word_2(b);
  const uint64_t product23 = Word_2(a) * Word_3(b);
  const uint64_t product24 = Word_2(a) * Word_4(b);
  const uint64_t product31 = Word_3(a) * Word_1(b);
  const uint64_t product32 = Word_3(a) * Word_2(b);
  const uint64_t product33 = Word_3(a) * Word_3(b);
  const uint64_t product34 = Word_3(a) * Word_4(b);
  const uint64_t product41 = Word_4(a) * Word_1(b);
  const uint64_t product42 = Word_4(a) * Word_2(b);
  const uint64_t product43 = Word_4(a) * Word_3(b);
  const uint64_t product44 = Word_4(a) * Word_4(b);

  const __uint128_t sum0 = (__uint128_t)product44;
  const __uint128_t sum1 = (__uint128_t)product34 + (__uint128_t)product43;
  const __uint128_t sum2 =
      (__uint128_t)product24 + (__uint128_t)product33 + (__uint128_t)product42;
  const __uint128_t sum3 = (__uint128_t)product14 + (__uint128_t)product23 +
                           (__uint128_t)product32 + (__uint128_t)product41;
  const __uint128_t sum4 =
      (__uint128_t)product13 + (__uint128_t)product22 + (__uint128_t)product31;
  const __uint128_t sum5 = (__uint128_t)product12 + (__uint128_t)product21;
  const __uint128_t sum6 = (__uint128_t)product11;

  const __uint128_t r0 = (sum0 & Word_FullMask) + ((sum1 & Word_LoMask) << 32);
  const __uint128_t r1 = (sum0 >> 64) + ((sum1 >> 32) & Word_FullMask) +
                         (sum2 & Word_FullMask) + ((sum3 << 32) & Word_HiMask);

  *lo = r0 + (r1 << 64);
  *hi = (r1 >> 64) + (sum1 >> 96) + (sum2 >> 64) + (sum3 >> 32) + sum4 +
        (sum5 << 32) + (sum6 << 64);
}
#undef Word_1
#undef Word_2
#undef Word_3
#undef Word_4
#undef Word_HiMask
#undef Word_LoMask
#undef Word_FullMask
#endif // defined(CRT_HAS_IEEE_TF)
#else
typedef long double fp_t;
#endif // defined(CRT_HAS_F128) && defined(CRT_HAS_128BIT)
#else
#error SINGLE_PRECISION, DOUBLE_PRECISION or QUAD_PRECISION must be defined.
#endif

#if defined(SINGLE_PRECISION) || defined(DOUBLE_PRECISION) ||                  \
    (defined(QUAD_PRECISION) && defined(CRT_HAS_TF_MODE))
#define typeWidth (sizeof(rep_t) * CHAR_BIT)

static __inline rep_t toRep(fp_t x) {
  const union {
    fp_t f;
    rep_t i;
  } rep = {.f = x};
  return rep.i;
}

static __inline fp_t fromRep(rep_t x) {
  const union {
    fp_t f;
    rep_t i;
  } rep = {.i = x};
  return rep.f;
}

#if !defined(QUAD_PRECISION) || defined(CRT_HAS_IEEE_TF)
#define exponentBits (typeWidth - significandBits - 1)
#define maxExponent ((1 << exponentBits) - 1)
#define exponentBias (maxExponent >> 1)

#define implicitBit (REP_C(1) << significandBits)
#define significandMask (implicitBit - 1U)
#define signBit (REP_C(1) << (significandBits + exponentBits))
#define absMask (signBit - 1U)
#define exponentMask (absMask ^ significandMask)
#define oneRep ((rep_t)exponentBias << significandBits)
#define infRep exponentMask
#define quietBit (implicitBit >> 1)
#define qnanRep (exponentMask | quietBit)

static __inline int normalize(rep_t *significand) {
  const int shift = rep_clz(*significand) - rep_clz(implicitBit);
  *significand <<= shift;
  return 1 - shift;
}

static __inline void wideLeftShift(rep_t *hi, rep_t *lo, unsigned int count) {
  *hi = *hi << count | *lo >> (typeWidth - count);
  *lo = *lo << count;
}

static __inline void wideRightShiftWithSticky(rep_t *hi, rep_t *lo,
                                              unsigned int count) {
  if (count < typeWidth) {
    const bool sticky = (*lo << (typeWidth - count)) != 0;
    *lo = *hi << (typeWidth - count) | *lo >> count | sticky;
    *hi = *hi >> count;
  } else if (count < 2 * typeWidth) {
    const bool sticky = *hi << (2 * typeWidth - count) | *lo;
    *lo = *hi >> (count - typeWidth) | sticky;
    *hi = 0;
  } else {
    const bool sticky = *hi | *lo;
    *lo = sticky;
    *hi = 0;
  }
}

// Implements logb methods (logb, logbf, logbl) for IEEE-754. This avoids
// pulling in a libm dependency from compiler-rt, but is not meant to replace
// it (i.e. code calling logb() should get the one from libm, not this), hence
// the __compiler_rt prefix.
static __inline fp_t __compiler_rt_logbX(fp_t x) {
  rep_t rep = toRep(x);
  int exp = (rep & exponentMask) >> significandBits;

  // Abnormal cases:
  // 1) +/- inf returns +inf; NaN returns NaN
  // 2) 0.0 returns -inf
  if (exp == maxExponent) {
    if (((rep & signBit) == 0) || (x != x)) {
      return x; // NaN or +inf: return x
    } else {
      return -x; // -inf: return -x
    }
  } else if (x == 0.0) {
    // 0.0: return -inf
    return fromRep(infRep | signBit);
  }

  if (exp != 0) {
    // Normal number
    return exp - exponentBias; // Unbias exponent
  } else {
    // Subnormal number; normalize and repeat
    rep &= absMask;
    const int shift = 1 - normalize(&rep);
    exp = (rep & exponentMask) >> significandBits;
    return exp - exponentBias - shift; // Unbias exponent
  }
}

// Avoid using scalbn from libm. Unlike libc/libm scalbn, this function never
// sets errno on underflow/overflow.
static __inline fp_t __compiler_rt_scalbnX(fp_t x, int y) {
  const rep_t rep = toRep(x);
  int exp = (rep & exponentMask) >> significandBits;

  if (x == 0.0 || exp == maxExponent)
    return x; // +/- 0.0, NaN, or inf: return x

  // Normalize subnormal input.
  rep_t sig = rep & significandMask;
  if (exp == 0) {
    exp += normalize(&sig);
    sig &= ~implicitBit; // clear the implicit bit again
  }

  if (__builtin_sadd_overflow(exp, y, &exp)) {
    // Saturate the exponent, which will guarantee an underflow/overflow below.
    exp = (y >= 0) ? INT_MAX : INT_MIN;
  }

  // Return this value: [+/-] 1.sig * 2 ** (exp - exponentBias).
  const rep_t sign = rep & signBit;
  if (exp >= maxExponent) {
    // Overflow, which could produce infinity or the largest-magnitude value,
    // depending on the rounding mode.
    return fromRep(sign | ((rep_t)(maxExponent - 1) << significandBits)) * 2.0f;
  } else if (exp <= 0) {
    // Subnormal or underflow. Use floating-point multiply to handle truncation
    // correctly.
    fp_t tmp = fromRep(sign | (REP_C(1) << significandBits) | sig);
    exp += exponentBias - 1;
    if (exp < 1)
      exp = 1;
    tmp *= fromRep((rep_t)exp << significandBits);
    return tmp;
  } else
    return fromRep(sign | ((rep_t)exp << significandBits) | sig);
}

#endif // !defined(QUAD_PRECISION) || defined(CRT_HAS_IEEE_TF)

// Avoid using fmax from libm.
static __inline fp_t __compiler_rt_fmaxX(fp_t x, fp_t y) {
  // If either argument is NaN, return the other argument. If both are NaN,
  // arbitrarily return the second one. Otherwise, if both arguments are +/-0,
  // arbitrarily return the first one.
  return (crt_isnan(x) || x < y) ? y : x;
}

#endif

#if defined(SINGLE_PRECISION)

static __inline fp_t __compiler_rt_logbf(fp_t x) {
  return __compiler_rt_logbX(x);
}
static __inline fp_t __compiler_rt_scalbnf(fp_t x, int y) {
  return __compiler_rt_scalbnX(x, y);
}
static __inline fp_t __compiler_rt_fmaxf(fp_t x, fp_t y) {
#if defined(__aarch64__)
  // Use __builtin_fmaxf which turns into an fmaxnm instruction on AArch64.
  return __builtin_fmaxf(x, y);
#else
  // __builtin_fmaxf frequently turns into a libm call, so inline the function.
  return __compiler_rt_fmaxX(x, y);
#endif
}

#elif defined(DOUBLE_PRECISION)

static __inline fp_t __compiler_rt_logb(fp_t x) {
  return __compiler_rt_logbX(x);
}
static __inline fp_t __compiler_rt_scalbn(fp_t x, int y) {
  return __compiler_rt_scalbnX(x, y);
}
static __inline fp_t __compiler_rt_fmax(fp_t x, fp_t y) {
#if defined(__aarch64__)
  // Use __builtin_fmax which turns into an fmaxnm instruction on AArch64.
  return __builtin_fmax(x, y);
#else
  // __builtin_fmax frequently turns into a libm call, so inline the function.
  return __compiler_rt_fmaxX(x, y);
#endif
}

#elif defined(QUAD_PRECISION) && defined(CRT_HAS_TF_MODE)
// The generic implementation only works for ieee754 floating point. For other
// floating point types, continue to rely on the libm implementation for now.
#if defined(CRT_HAS_IEEE_TF)
static __inline tf_float __compiler_rt_logbtf(tf_float x) {
  return __compiler_rt_logbX(x);
}
static __inline tf_float __compiler_rt_scalbntf(tf_float x, int y) {
  return __compiler_rt_scalbnX(x, y);
}
static __inline tf_float __compiler_rt_fmaxtf(tf_float x, tf_float y) {
  return __compiler_rt_fmaxX(x, y);
}
#define __compiler_rt_logbl __compiler_rt_logbtf
#define __compiler_rt_scalbnl __compiler_rt_scalbntf
#define __compiler_rt_fmaxl __compiler_rt_fmaxtf
#define crt_fabstf crt_fabsf128
#define crt_copysigntf crt_copysignf128
#elif defined(CRT_LDBL_128BIT)
static __inline tf_float __compiler_rt_logbtf(tf_float x) {
  return crt_logbl(x);
}
static __inline tf_float __compiler_rt_scalbntf(tf_float x, int y) {
  return crt_scalbnl(x, y);
}
static __inline tf_float __compiler_rt_fmaxtf(tf_float x, tf_float y) {
  return crt_fmaxl(x, y);
}
#define __compiler_rt_logbl crt_logbl
#define __compiler_rt_scalbnl crt_scalbnl
#define __compiler_rt_fmaxl crt_fmaxl
#define crt_fabstf crt_fabsl
#define crt_copysigntf crt_copysignl
#else
#error Unsupported TF mode type
#endif

#endif // *_PRECISION

#endif // FP_LIB_HEADER
