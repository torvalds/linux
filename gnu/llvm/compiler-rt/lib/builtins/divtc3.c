//===-- divtc3.c - Implement __divtc3 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __divtc3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_128BIT) && defined(CRT_HAS_F128)

// Returns: the quotient of (a + ib) / (c + id)

COMPILER_RT_ABI Qcomplex __divtc3(fp_t __a, fp_t __b, fp_t __c, fp_t __d) {
  int __ilogbw = 0;
  fp_t __logbw = __compiler_rt_logbtf(
      __compiler_rt_fmaxtf(crt_fabstf(__c), crt_fabstf(__d)));
  if (crt_isfinite(__logbw)) {
    __ilogbw = (int)__logbw;
    __c = __compiler_rt_scalbntf(__c, -__ilogbw);
    __d = __compiler_rt_scalbntf(__d, -__ilogbw);
  }
  fp_t __denom = __c * __c + __d * __d;
  Qcomplex z;
  COMPLEXTF_REAL(z) =
      __compiler_rt_scalbntf((__a * __c + __b * __d) / __denom, -__ilogbw);
  COMPLEXTF_IMAGINARY(z) =
      __compiler_rt_scalbntf((__b * __c - __a * __d) / __denom, -__ilogbw);
  if (crt_isnan(COMPLEXTF_REAL(z)) && crt_isnan(COMPLEXTF_IMAGINARY(z))) {
    if ((__denom == 0.0) && (!crt_isnan(__a) || !crt_isnan(__b))) {
      COMPLEXTF_REAL(z) = crt_copysigntf(CRT_INFINITY, __c) * __a;
      COMPLEXTF_IMAGINARY(z) = crt_copysigntf(CRT_INFINITY, __c) * __b;
    } else if ((crt_isinf(__a) || crt_isinf(__b)) && crt_isfinite(__c) &&
               crt_isfinite(__d)) {
      __a = crt_copysigntf(crt_isinf(__a) ? (fp_t)1.0 : (fp_t)0.0, __a);
      __b = crt_copysigntf(crt_isinf(__b) ? (fp_t)1.0 : (fp_t)0.0, __b);
      COMPLEXTF_REAL(z) = CRT_INFINITY * (__a * __c + __b * __d);
      COMPLEXTF_IMAGINARY(z) = CRT_INFINITY * (__b * __c - __a * __d);
    } else if (crt_isinf(__logbw) && __logbw > 0.0 && crt_isfinite(__a) &&
               crt_isfinite(__b)) {
      __c = crt_copysigntf(crt_isinf(__c) ? (fp_t)1.0 : (fp_t)0.0, __c);
      __d = crt_copysigntf(crt_isinf(__d) ? (fp_t)1.0 : (fp_t)0.0, __d);
      COMPLEXTF_REAL(z) = 0.0 * (__a * __c + __b * __d);
      COMPLEXTF_IMAGINARY(z) = 0.0 * (__b * __c - __a * __d);
    }
  }
  return z;
}

#endif
