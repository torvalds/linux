/*===---- prfchwintrin.h - PREFETCHW intrinsic -----------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#if !defined(__X86INTRIN_H) && !defined(_MM3DNOW_H_INCLUDED)
#error "Never use <prfchwintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __PRFCHWINTRIN_H
#define __PRFCHWINTRIN_H

/// Loads a memory sequence containing the specified memory address into
///    all data cache levels.
///
///    The cache-coherency state is set to exclusive. Data can be read from
///    and written to the cache line without additional delay.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PREFETCHT0 instruction.
///
/// \param __P
///    A pointer specifying the memory address to be prefetched.
static __inline__ void __attribute__((__always_inline__, __nodebug__))
_m_prefetch(void *__P)
{
  __builtin_prefetch (__P, 0, 3 /* _MM_HINT_T0 */);
}

/// Loads a memory sequence containing the specified memory address into
///    the L1 data cache and sets the cache-coherency state to modified.
///
///    This provides a hint to the processor that the cache line will be
///    modified. It is intended for use when the cache line will be written to
///    shortly after the prefetch is performed.
///
///    Note that the effect of this intrinsic is dependent on the processor
///    implementation.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PREFETCHW instruction.
///
/// \param __P
///    A pointer specifying the memory address to be prefetched.
static __inline__ void __attribute__((__always_inline__, __nodebug__))
_m_prefetchw(volatile const void *__P)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
  __builtin_prefetch ((const void*)__P, 1, 3 /* _MM_HINT_T0 */);
#pragma clang diagnostic pop
}

#endif /* __PRFCHWINTRIN_H */
