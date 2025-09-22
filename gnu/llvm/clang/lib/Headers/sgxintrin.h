/*===---- sgxintrin.h - X86 SGX intrinsics configuration -------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#if !defined __X86INTRIN_H && !defined __IMMINTRIN_H
#error "Never use <sgxintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __SGXINTRIN_H
#define __SGXINTRIN_H

#if __has_extension(gnu_asm)

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__,  __target__("sgx")))

static __inline unsigned int __DEFAULT_FN_ATTRS
_enclu_u32(unsigned int __leaf, __SIZE_TYPE__ __d[])
{
  unsigned int __result;
  __asm__ ("enclu"
           : "=a" (__result), "=b" (__d[0]), "=c" (__d[1]), "=d" (__d[2])
           : "a" (__leaf), "b" (__d[0]), "c" (__d[1]), "d" (__d[2])
           : "cc");
  return __result;
}

static __inline unsigned int __DEFAULT_FN_ATTRS
_encls_u32(unsigned int __leaf, __SIZE_TYPE__ __d[])
{
  unsigned int __result;
  __asm__ ("encls"
           : "=a" (__result), "=b" (__d[0]), "=c" (__d[1]), "=d" (__d[2])
           : "a" (__leaf), "b" (__d[0]), "c" (__d[1]), "d" (__d[2])
           : "cc");
  return __result;
}

static __inline unsigned int __DEFAULT_FN_ATTRS
_enclv_u32(unsigned int __leaf, __SIZE_TYPE__ __d[])
{
  unsigned int __result;
  __asm__ ("enclv"
           : "=a" (__result), "=b" (__d[0]), "=c" (__d[1]), "=d" (__d[2])
           : "a" (__leaf), "b" (__d[0]), "c" (__d[1]), "d" (__d[2])
           : "cc");
  return __result;
}

#undef __DEFAULT_FN_ATTRS

#endif /* __has_extension(gnu_asm) */

#endif
