/*===------------- invpcidintrin.h - INVPCID intrinsic ---------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <invpcidintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __INVPCIDINTRIN_H
#define __INVPCIDINTRIN_H

static __inline__ void
  __attribute__((__always_inline__, __nodebug__,  __target__("invpcid")))
_invpcid(unsigned int __type, void *__descriptor) {
  __builtin_ia32_invpcid(__type, __descriptor);
}

#endif /* __INVPCIDINTRIN_H */
