//===-- divxc3.c - Implement __divxc3 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __divxc3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#if !_ARCH_PPC

#include "int_lib.h"
#include "int_math.h"

// Returns: the quotient of (a + ib) / (c + id)

COMPILER_RT_ABI Lcomplex __divxc3(xf_float __a, xf_float __b, xf_float __c,
                                  xf_float __d) {
  int __ilogbw = 0;
  xf_float __logbw = crt_logbl(crt_fmaxl(crt_fabsl(__c), crt_fabsl(__d)));
  if (crt_isfinite(__logbw)) {
    __ilogbw = (int)__logbw;
    __c = crt_scalbnl(__c, -__ilogbw);
    __d = crt_scalbnl(__d, -__ilogbw);
  }
  xf_float __denom = __c * __c + __d * __d;
  Lcomplex z;
  COMPLEX_REAL(z) = crt_scalbnl((__a * __c + __b * __d) / __denom, -__ilogbw);
  COMPLEX_IMAGINARY(z) =
      crt_scalbnl((__b * __c - __a * __d) / __denom, -__ilogbw);
  if (crt_isnan(COMPLEX_REAL(z)) && crt_isnan(COMPLEX_IMAGINARY(z))) {
    if ((__denom == 0) && (!crt_isnan(__a) || !crt_isnan(__b))) {
      COMPLEX_REAL(z) = crt_copysignl(CRT_INFINITY, __c) * __a;
      COMPLEX_IMAGINARY(z) = crt_copysignl(CRT_INFINITY, __c) * __b;
    } else if ((crt_isinf(__a) || crt_isinf(__b)) && crt_isfinite(__c) &&
               crt_isfinite(__d)) {
      __a = crt_copysignl(crt_isinf(__a) ? 1 : 0, __a);
      __b = crt_copysignl(crt_isinf(__b) ? 1 : 0, __b);
      COMPLEX_REAL(z) = CRT_INFINITY * (__a * __c + __b * __d);
      COMPLEX_IMAGINARY(z) = CRT_INFINITY * (__b * __c - __a * __d);
    } else if (crt_isinf(__logbw) && __logbw > 0 && crt_isfinite(__a) &&
               crt_isfinite(__b)) {
      __c = crt_copysignl(crt_isinf(__c) ? 1 : 0, __c);
      __d = crt_copysignl(crt_isinf(__d) ? 1 : 0, __d);
      COMPLEX_REAL(z) = 0 * (__a * __c + __b * __d);
      COMPLEX_IMAGINARY(z) = 0 * (__b * __c - __a * __d);
    }
  }
  return z;
}

#endif
