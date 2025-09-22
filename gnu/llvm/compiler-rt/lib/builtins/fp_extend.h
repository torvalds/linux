//===-lib/fp_extend.h - low precision -> high precision conversion -*- C
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Set source and destination setting
//
//===----------------------------------------------------------------------===//

#ifndef FP_EXTEND_HEADER
#define FP_EXTEND_HEADER

#include "int_lib.h"

#if defined SRC_SINGLE
typedef float src_t;
typedef uint32_t src_rep_t;
#define SRC_REP_C UINT32_C
static const int srcBits = sizeof(src_t) * CHAR_BIT;
static const int srcSigFracBits = 23;
// -1 accounts for the sign bit.
// srcBits - srcSigFracBits - 1
static const int srcExpBits = 8;
#define src_rep_t_clz clzsi

#elif defined SRC_DOUBLE
typedef double src_t;
typedef uint64_t src_rep_t;
#define SRC_REP_C UINT64_C
static const int srcBits = sizeof(src_t) * CHAR_BIT;
static const int srcSigFracBits = 52;
// -1 accounts for the sign bit.
// srcBits - srcSigFracBits - 1
static const int srcExpBits = 11;

static inline int src_rep_t_clz_impl(src_rep_t a) { return __builtin_clzll(a); }
#define src_rep_t_clz src_rep_t_clz_impl

#elif defined SRC_80
typedef xf_float src_t;
typedef __uint128_t src_rep_t;
#define SRC_REP_C (__uint128_t)
// sign bit, exponent and significand occupy the lower 80 bits.
static const int srcBits = 80;
static const int srcSigFracBits = 63;
// -1 accounts for the sign bit.
// -1 accounts for the explicitly stored integer bit.
// srcBits - srcSigFracBits - 1 - 1
static const int srcExpBits = 15;

#elif defined SRC_HALF
#ifdef COMPILER_RT_HAS_FLOAT16
typedef _Float16 src_t;
#else
typedef uint16_t src_t;
#endif
typedef uint16_t src_rep_t;
#define SRC_REP_C UINT16_C
static const int srcBits = sizeof(src_t) * CHAR_BIT;
static const int srcSigFracBits = 10;
// -1 accounts for the sign bit.
// srcBits - srcSigFracBits - 1
static const int srcExpBits = 5;

static inline int src_rep_t_clz_impl(src_rep_t a) {
  return __builtin_clz(a) - 16;
}

#define src_rep_t_clz src_rep_t_clz_impl

#elif defined SRC_BFLOAT16
#ifdef COMPILER_RT_HAS_BFLOAT16
typedef __bf16 src_t;
#else
typedef uint16_t src_t;
#endif
typedef uint16_t src_rep_t;
#define SRC_REP_C UINT16_C
static const int srcBits = sizeof(src_t) * CHAR_BIT;
static const int srcSigFracBits = 7;
// -1 accounts for the sign bit.
// srcBits - srcSigFracBits - 1
static const int srcExpBits = 8;
#define src_rep_t_clz __builtin_clz

#else
#error Source should be half, single, or double precision!
#endif // end source precision

#if defined DST_SINGLE
typedef float dst_t;
typedef uint32_t dst_rep_t;
#define DST_REP_C UINT32_C
static const int dstBits = sizeof(dst_t) * CHAR_BIT;
static const int dstSigFracBits = 23;
// -1 accounts for the sign bit.
// dstBits - dstSigFracBits - 1
static const int dstExpBits = 8;

#elif defined DST_DOUBLE
typedef double dst_t;
typedef uint64_t dst_rep_t;
#define DST_REP_C UINT64_C
static const int dstBits = sizeof(dst_t) * CHAR_BIT;
static const int dstSigFracBits = 52;
// -1 accounts for the sign bit.
// dstBits - dstSigFracBits - 1
static const int dstExpBits = 11;

#elif defined DST_QUAD
typedef tf_float dst_t;
typedef __uint128_t dst_rep_t;
#define DST_REP_C (__uint128_t)
static const int dstBits = sizeof(dst_t) * CHAR_BIT;
static const int dstSigFracBits = 112;
// -1 accounts for the sign bit.
// dstBits - dstSigFracBits - 1
static const int dstExpBits = 15;

#else
#error Destination should be single, double, or quad precision!
#endif // end destination precision

// End of specialization parameters.

// TODO: These helper routines should be placed into fp_lib.h
// Currently they depend on macros/constants defined above.

static inline src_rep_t extract_sign_from_src(src_rep_t x) {
  const src_rep_t srcSignMask = SRC_REP_C(1) << (srcBits - 1);
  return (x & srcSignMask) >> (srcBits - 1);
}

static inline src_rep_t extract_exp_from_src(src_rep_t x) {
  const int srcSigBits = srcBits - 1 - srcExpBits;
  const src_rep_t srcExpMask = ((SRC_REP_C(1) << srcExpBits) - 1) << srcSigBits;
  return (x & srcExpMask) >> srcSigBits;
}

static inline src_rep_t extract_sig_frac_from_src(src_rep_t x) {
  const src_rep_t srcSigFracMask = (SRC_REP_C(1) << srcSigFracBits) - 1;
  return x & srcSigFracMask;
}

#ifdef src_rep_t_clz
static inline int clz_in_sig_frac(src_rep_t sigFrac) {
      const int skip = 1 + srcExpBits;
      return src_rep_t_clz(sigFrac) - skip;
}
#endif

static inline dst_rep_t construct_dst_rep(dst_rep_t sign, dst_rep_t exp, dst_rep_t sigFrac) {
  return (sign << (dstBits - 1)) | (exp << (dstBits - 1 - dstExpBits)) | sigFrac;
}

// Two helper routines for conversion to and from the representation of
// floating-point data as integer values follow.

static inline src_rep_t srcToRep(src_t x) {
  const union {
    src_t f;
    src_rep_t i;
  } rep = {.f = x};
  return rep.i;
}

static inline dst_t dstFromRep(dst_rep_t x) {
  const union {
    dst_t f;
    dst_rep_t i;
  } rep = {.i = x};
  return rep.f;
}
// End helper routines.  Conversion implementation follows.

#endif // FP_EXTEND_HEADER
