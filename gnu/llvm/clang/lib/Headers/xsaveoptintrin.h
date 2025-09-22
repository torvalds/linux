/*===---- xsaveoptintrin.h - XSAVEOPT intrinsic ----------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <xsaveoptintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __XSAVEOPTINTRIN_H
#define __XSAVEOPTINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__,  __target__("xsaveopt")))

static __inline__ void __DEFAULT_FN_ATTRS
_xsaveopt(void *__p, unsigned long long __m) {
  __builtin_ia32_xsaveopt(__p, __m);
}

#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS
_xsaveopt64(void *__p, unsigned long long __m) {
  __builtin_ia32_xsaveopt64(__p, __m);
}
#endif

#undef __DEFAULT_FN_ATTRS

#endif
