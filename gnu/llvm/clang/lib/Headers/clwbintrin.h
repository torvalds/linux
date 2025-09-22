/*===---- clwbintrin.h - CLWB intrinsic ------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <clwbintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __CLWBINTRIN_H
#define __CLWBINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__,  __target__("clwb")))

/// Writes back to memory the cache line (if modified) that contains the
/// linear address specified in \a __p from any level of the cache hierarchy in
/// the cache coherence domain
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the <c> CLWB </c> instruction.
///
/// \param __p
///    A pointer to the memory location used to identify the cache line to be
///    written back.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_clwb(void const *__p) {
  __builtin_ia32_clwb(__p);
}

#undef __DEFAULT_FN_ATTRS

#endif
