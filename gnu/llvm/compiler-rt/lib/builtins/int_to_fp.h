//===-- int_to_fp.h - integer to floating point conversion ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Set source and destination defines in order to use a correctly
// parameterised floatXiYf implementation.
//
//===----------------------------------------------------------------------===//

#ifndef INT_TO_FP_H
#define INT_TO_FP_H

#include "int_lib.h"

#if defined SRC_I64
typedef int64_t src_t;
typedef uint64_t usrc_t;
static __inline int clzSrcT(usrc_t x) { return __builtin_clzll(x); }

#elif defined SRC_U64
typedef uint64_t src_t;
typedef uint64_t usrc_t;
static __inline int clzSrcT(usrc_t x) { return __builtin_clzll(x); }

#elif defined SRC_I128
typedef __int128_t src_t;
typedef __uint128_t usrc_t;
static __inline int clzSrcT(usrc_t x) { return __clzti2(x); }

#elif defined SRC_U128
typedef __uint128_t src_t;
typedef __uint128_t usrc_t;
static __inline int clzSrcT(usrc_t x) { return __clzti2(x); }

#else
#error Source should be a handled integer type.
#endif

#if defined DST_SINGLE
typedef float dst_t;
typedef uint32_t dst_rep_t;
#define DST_REP_C UINT32_C

enum {
  dstSigBits = 23,
};

#elif defined DST_DOUBLE
typedef double dst_t;
typedef uint64_t dst_rep_t;
#define DST_REP_C UINT64_C

enum {
  dstSigBits = 52,
};

#elif defined DST_QUAD
typedef tf_float dst_t;
typedef __uint128_t dst_rep_t;
#define DST_REP_C (__uint128_t)

enum {
  dstSigBits = 112,
};

#else
#error Destination should be a handled floating point type
#endif

static __inline dst_t dstFromRep(dst_rep_t x) {
  const union {
    dst_t f;
    dst_rep_t i;
  } rep = {.i = x};
  return rep.f;
}

#endif // INT_TO_FP_H
