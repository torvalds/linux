//===-- mulxc3.c - Implement __mulxc3 -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __mulxc3 for the compiler_rt library.
//
//===----------------------------------------------------------------------===//

#if !_ARCH_PPC

#include "int_lib.h"
#include "int_math.h"

// Returns: the product of a + ib and c + id

COMPILER_RT_ABI Lcomplex __mulxc3(xf_float __a, xf_float __b, xf_float __c,
                                  xf_float __d) {
  xf_float __ac = __a * __c;
  xf_float __bd = __b * __d;
  xf_float __ad = __a * __d;
  xf_float __bc = __b * __c;
  Lcomplex z;
  COMPLEX_REAL(z) = __ac - __bd;
  COMPLEX_IMAGINARY(z) = __ad + __bc;
  if (crt_isnan(COMPLEX_REAL(z)) && crt_isnan(COMPLEX_IMAGINARY(z))) {
    int __recalc = 0;
    if (crt_isinf(__a) || crt_isinf(__b)) {
      __a = crt_copysignl(crt_isinf(__a) ? 1 : 0, __a);
      __b = crt_copysignl(crt_isinf(__b) ? 1 : 0, __b);
      if (crt_isnan(__c))
        __c = crt_copysignl(0, __c);
      if (crt_isnan(__d))
        __d = crt_copysignl(0, __d);
      __recalc = 1;
    }
    if (crt_isinf(__c) || crt_isinf(__d)) {
      __c = crt_copysignl(crt_isinf(__c) ? 1 : 0, __c);
      __d = crt_copysignl(crt_isinf(__d) ? 1 : 0, __d);
      if (crt_isnan(__a))
        __a = crt_copysignl(0, __a);
      if (crt_isnan(__b))
        __b = crt_copysignl(0, __b);
      __recalc = 1;
    }
    if (!__recalc && (crt_isinf(__ac) || crt_isinf(__bd) || crt_isinf(__ad) ||
                      crt_isinf(__bc))) {
      if (crt_isnan(__a))
        __a = crt_copysignl(0, __a);
      if (crt_isnan(__b))
        __b = crt_copysignl(0, __b);
      if (crt_isnan(__c))
        __c = crt_copysignl(0, __c);
      if (crt_isnan(__d))
        __d = crt_copysignl(0, __d);
      __recalc = 1;
    }
    if (__recalc) {
      COMPLEX_REAL(z) = CRT_INFINITY * (__a * __c - __b * __d);
      COMPLEX_IMAGINARY(z) = CRT_INFINITY * (__a * __d + __b * __c);
    }
  }
  return z;
}

#endif
