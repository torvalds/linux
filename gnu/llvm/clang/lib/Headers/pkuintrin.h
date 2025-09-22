/*===---- pkuintrin.h - PKU intrinsics -------------------------------------===
 *
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <pkuintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __PKUINTRIN_H
#define __PKUINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("pku")))

static __inline__ unsigned int __DEFAULT_FN_ATTRS
_rdpkru_u32(void)
{
  return __builtin_ia32_rdpkru();
}

static __inline__ void __DEFAULT_FN_ATTRS
_wrpkru(unsigned int __val)
{
  __builtin_ia32_wrpkru(__val);
}

#undef __DEFAULT_FN_ATTRS

#endif
