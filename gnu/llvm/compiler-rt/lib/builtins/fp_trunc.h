//=== lib/fp_trunc.h - high precision -> low precision conversion *- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Set source and destination precision setting
//
//===----------------------------------------------------------------------===//

#ifndef FP_TRUNC_HEADER
#define FP_TRUNC_HEADER

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

#elif defined SRC_DOUBLE
typedef double src_t;
typedef uint64_t src_rep_t;
#define SRC_REP_C UINT64_C
static const int srcBits = sizeof(src_t) * CHAR_BIT;
static const int srcSigFracBits = 52;
// -1 accounts for the sign bit.
// srcBits - srcSigFracBits - 1
static const int srcExpBits = 11;

#elif defined SRC_QUAD
typedef tf_float src_t;
typedef __uint128_t src_rep_t;
#define SRC_REP_C (__uint128_t)
static const int srcBits = sizeof(src_t) * CHAR_BIT;
static const int srcSigFracBits = 112;
// -1 accounts for the sign bit.
// srcBits - srcSigFracBits - 1
static const int srcExpBits = 15;

#else
#error Source should be double precision or quad precision!
#endif // end source precision

#if defined DST_DOUBLE
typedef double dst_t;
typedef uint64_t dst_rep_t;
#define DST_REP_C UINT64_C
static const int dstBits = sizeof(dst_t) * CHAR_BIT;
static const int dstSigFracBits = 52;
// -1 accounts for the sign bit.
// dstBits - dstSigFracBits - 1
static const int dstExpBits = 11;

#elif defined DST_80
typedef xf_float dst_t;
typedef __uint128_t dst_rep_t;
#define DST_REP_C (__uint128_t)
static const int dstBits = 80;
static const int dstSigFracBits = 63;
// -1 accounts for the sign bit.
// -1 accounts for the explicitly stored integer bit.
// dstBits - dstSigFracBits - 1 - 1
static const int dstExpBits = 15;

#elif defined DST_SINGLE
typedef float dst_t;
typedef uint32_t dst_rep_t;
#define DST_REP_C UINT32_C
static const int dstBits = sizeof(dst_t) * CHAR_BIT;
static const int dstSigFracBits = 23;
// -1 accounts for the sign bit.
// dstBits - dstSigFracBits - 1
static const int dstExpBits = 8;

#elif defined DST_HALF
#ifdef COMPILER_RT_HAS_FLOAT16
typedef _Float16 dst_t;
#else
typedef uint16_t dst_t;
#endif
typedef uint16_t dst_rep_t;
#define DST_REP_C UINT16_C
static const int dstBits = sizeof(dst_t) * CHAR_BIT;
static const int dstSigFracBits = 10;
// -1 accounts for the sign bit.
// dstBits - dstSigFracBits - 1
static const int dstExpBits = 5;

#elif defined DST_BFLOAT
typedef __bf16 dst_t;
typedef uint16_t dst_rep_t;
#define DST_REP_C UINT16_C
static const int dstBits = sizeof(dst_t) * CHAR_BIT;
static const int dstSigFracBits = 7;
// -1 accounts for the sign bit.
// dstBits - dstSigFracBits - 1
static const int dstExpBits = 8;

#else
#error Destination should be single precision or double precision!
#endif // end destination precision

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

static inline dst_rep_t construct_dst_rep(dst_rep_t sign, dst_rep_t exp, dst_rep_t sigFrac) {
  dst_rep_t result = (sign << (dstBits - 1)) | (exp << (dstBits - 1 - dstExpBits)) | sigFrac;
  // Set the explicit integer bit in F80 if present.
  if (dstBits == 80 && exp) {
    result |= (DST_REP_C(1) << dstSigFracBits);
  }
  return result;
}

// End of specialization parameters.  Two helper routines for conversion to and
// from the representation of floating-point data as integer values follow.

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

#endif // FP_TRUNC_HEADER
