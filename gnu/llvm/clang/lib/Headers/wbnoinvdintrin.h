/*===-------------- wbnoinvdintrin.h - wbnoinvd intrinsic-------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#if !defined __X86INTRIN_H && !defined __IMMINTRIN_H
#error "Never use <wbnoinvdintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __WBNOINVDINTRIN_H
#define __WBNOINVDINTRIN_H

static __inline__ void
  __attribute__((__always_inline__, __nodebug__,  __target__("wbnoinvd")))
_wbnoinvd (void)
{
  __builtin_ia32_wbnoinvd ();
}

#endif /* __WBNOINVDINTRIN_H */
