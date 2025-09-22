/*===----------------------- clzerointrin.h - CLZERO ----------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __X86INTRIN_H
#error "Never use <clzerointrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __CLZEROINTRIN_H
#define __CLZEROINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__,  __target__("clzero")))

/// Zeroes out the cache line for the address \a __line. This uses a
///    non-temporal store. Calling \c _mm_sfence() afterward might be needed
///    to enforce ordering.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c CLZERO instruction.
///
/// \param __line
///    An address within the cache line to zero out.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_clzero (void * __line)
{
  __builtin_ia32_clzero ((void *)__line);
}

#undef __DEFAULT_FN_ATTRS

#endif /* __CLZEROINTRIN_H */
