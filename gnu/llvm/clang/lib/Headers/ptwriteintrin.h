/*===------------ ptwriteintrin.h - PTWRITE intrinsic --------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#if !defined __X86INTRIN_H && !defined __IMMINTRIN_H
#error "Never use <ptwriteintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __PTWRITEINTRIN_H
#define __PTWRITEINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__,  __target__("ptwrite")))

static __inline__ void __DEFAULT_FN_ATTRS
_ptwrite32(unsigned int __value) {
  __builtin_ia32_ptwrite32(__value);
}

#ifdef __x86_64__

static __inline__ void __DEFAULT_FN_ATTRS
_ptwrite64(unsigned long long __value) {
  __builtin_ia32_ptwrite64(__value);
}

#endif /* __x86_64__ */

#undef __DEFAULT_FN_ATTRS

#endif /* __PTWRITEINTRIN_H */
