//===-- nsan.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of NumericalStabilitySanitizer.
//
// Private NSan header.
//===----------------------------------------------------------------------===//

#ifndef NSAN_H
#define NSAN_H

#include "sanitizer_common/sanitizer_internal_defs.h"

using __sanitizer::sptr;
using __sanitizer::u16;
using __sanitizer::u8;
using __sanitizer::uptr;

#include "nsan_platform.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

// Private nsan interface. Used e.g. by interceptors.
extern "C" {

void __nsan_init();

// This marks the shadow type of the given block of application memory as
// unknown.
// printf-free (see comment in nsan_interceptors.cc).
void __nsan_set_value_unknown(const u8 *addr, uptr size);

// Copies annotations in the shadow memory for a block of application memory to
// a new address. This function is used together with memory-copying functions
// in application memory, e.g. the instrumentation inserts
// `__nsan_copy_values(dest, src, size)` after builtin calls to
// `memcpy(dest, src, size)`. Intercepted memcpy calls also call this function.
// printf-free (see comment in nsan_interceptors.cc).
void __nsan_copy_values(const u8 *daddr, const u8 *saddr, uptr size);

SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE const char *
__nsan_default_options();
}

namespace __nsan {

extern bool nsan_initialized;
extern bool nsan_init_is_running;

void InitializeInterceptors();
void InitializeMallocInterceptors();

// See notes in nsan_platform.
// printf-free (see comment in nsan_interceptors.cc).
inline u8 *GetShadowAddrFor(u8 *Ptr) {
  uptr AppOffset = ((uptr)Ptr) & ShadowMask();
  return (u8 *)(AppOffset * kShadowScale + ShadowAddr());
}

// printf-free (see comment in nsan_interceptors.cc).
inline const u8 *GetShadowAddrFor(const u8 *Ptr) {
  return GetShadowAddrFor(const_cast<u8 *>(Ptr));
}

// printf-free (see comment in nsan_interceptors.cc).
inline u8 *GetShadowTypeAddrFor(u8 *Ptr) {
  uptr AppOffset = ((uptr)Ptr) & ShadowMask();
  return (u8 *)(AppOffset + TypesAddr());
}

// printf-free (see comment in nsan_interceptors.cc).
inline const u8 *GetShadowTypeAddrFor(const u8 *Ptr) {
  return GetShadowTypeAddrFor(const_cast<u8 *>(Ptr));
}

// Information about value types and their shadow counterparts.
template <typename FT> struct FTInfo {};
template <> struct FTInfo<float> {
  using orig_type = float;
  using orig_bits_type = __sanitizer::u32;
  using mantissa_bits_type = __sanitizer::u32;
  using shadow_type = double;
  static const char *kCppTypeName;
  static constexpr unsigned kMantissaBits = 23;
  static constexpr int kExponentBits = 8;
  static constexpr int kExponentBias = 127;
  static constexpr int kValueType = kFloatValueType;
  static constexpr char kTypePattern[sizeof(float)] = {
      static_cast<unsigned char>(kValueType | (0 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (1 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (2 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (3 << kValueSizeSizeBits)),
  };
  static constexpr const float kEpsilon = FLT_EPSILON;
};
template <> struct FTInfo<double> {
  using orig_type = double;
  using orig_bits_type = __sanitizer::u64;
  using mantissa_bits_type = __sanitizer::u64;
  using shadow_type = __float128;
  static const char *kCppTypeName;
  static constexpr unsigned kMantissaBits = 52;
  static constexpr int kExponentBits = 11;
  static constexpr int kExponentBias = 1023;
  static constexpr int kValueType = kDoubleValueType;
  static constexpr char kTypePattern[sizeof(double)] = {
      static_cast<unsigned char>(kValueType | (0 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (1 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (2 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (3 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (4 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (5 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (6 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (7 << kValueSizeSizeBits)),
  };
  static constexpr const float kEpsilon = DBL_EPSILON;
};
template <> struct FTInfo<long double> {
  using orig_type = long double;
  using mantissa_bits_type = __sanitizer::u64;
  using shadow_type = __float128;
  static const char *kCppTypeName;
  static constexpr unsigned kMantissaBits = 63;
  static constexpr int kExponentBits = 15;
  static constexpr int kExponentBias = (1 << (kExponentBits - 1)) - 1;
  static constexpr int kValueType = kFp80ValueType;
  static constexpr char kTypePattern[sizeof(long double)] = {
      static_cast<unsigned char>(kValueType | (0 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (1 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (2 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (3 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (4 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (5 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (6 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (7 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (8 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (9 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (10 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (11 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (12 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (13 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (14 << kValueSizeSizeBits)),
      static_cast<unsigned char>(kValueType | (15 << kValueSizeSizeBits)),
  };
  static constexpr const float kEpsilon = LDBL_EPSILON;
};

template <> struct FTInfo<__float128> {
  using orig_type = __float128;
  using orig_bits_type = __uint128_t;
  using mantissa_bits_type = __uint128_t;
  static const char *kCppTypeName;
  static constexpr unsigned kMantissaBits = 112;
  static constexpr int kExponentBits = 15;
  static constexpr int kExponentBias = (1 << (kExponentBits - 1)) - 1;
};

constexpr double kMaxULPDiff = INFINITY;

// Helper for getULPDiff that works on bit representations.
template <typename BT> double GetULPDiffBits(BT v1_bits, BT v2_bits) {
  // If the integer representations of two same-sign floats are subtracted then
  // the absolute value of the result is equal to one plus the number of
  // representable floats between them.
  return v1_bits >= v2_bits ? v1_bits - v2_bits : v2_bits - v1_bits;
}

// Returns the the number of floating point values between v1 and v2, capped to
// u64max. Return 0 for (-0.0,0.0).
template <typename FT> double GetULPDiff(FT v1, FT v2) {
  if (v1 == v2) {
    return 0; // Typically, -0.0 and 0.0
  }
  using BT = typename FTInfo<FT>::orig_bits_type;
  static_assert(sizeof(FT) == sizeof(BT), "not implemented");
  static_assert(sizeof(BT) <= 64, "not implemented");
  BT v1_bits;
  __builtin_memcpy(&v1_bits, &v1, sizeof(BT));
  BT v2_bits;
  __builtin_memcpy(&v2_bits, &v2, sizeof(BT));
  // Check whether the signs differ. IEEE-754 float types always store the sign
  // in the most significant bit. NaNs and infinities are handled by the calling
  // code.
  constexpr BT kSignMask = BT{1} << (CHAR_BIT * sizeof(BT) - 1);
  if ((v1_bits ^ v2_bits) & kSignMask) {
    // Signs differ. We can get the ULPs as `getULPDiff(negative_number, -0.0)
    // + getULPDiff(0.0, positive_number)`.
    if (v1_bits & kSignMask) {
      return GetULPDiffBits<BT>(v1_bits, kSignMask) +
             GetULPDiffBits<BT>(0, v2_bits);
    } else {
      return GetULPDiffBits<BT>(v2_bits, kSignMask) +
             GetULPDiffBits<BT>(0, v1_bits);
    }
  }
  return GetULPDiffBits(v1_bits, v2_bits);
}

// FIXME: This needs mor work: Because there is no 80-bit integer type, we have
// to go through __uint128_t. Therefore the assumptions about the sign bit do
// not hold.
template <> inline double GetULPDiff(long double v1, long double v2) {
  using BT = __uint128_t;
  BT v1_bits = 0;
  __builtin_memcpy(&v1_bits, &v1, sizeof(long double));
  BT v2_bits = 0;
  __builtin_memcpy(&v2_bits, &v2, sizeof(long double));
  if ((v1_bits ^ v2_bits) & (BT{1} << (CHAR_BIT * sizeof(BT) - 1)))
    return v1 == v2 ? __sanitizer::u64{0} : kMaxULPDiff; // Signs differ.
  // If the integer representations of two same-sign floats are subtracted then
  // the absolute value of the result is equal to one plus the number of
  // representable floats between them.
  BT diff = v1_bits >= v2_bits ? v1_bits - v2_bits : v2_bits - v1_bits;
  return diff >= kMaxULPDiff ? kMaxULPDiff : diff;
}

} // end namespace __nsan

#endif // NSAN_H
