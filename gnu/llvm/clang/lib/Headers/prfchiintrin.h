/*===---- prfchiintrin.h - PREFETCHI intrinsic -----------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __PRFCHIINTRIN_H
#define __PRFCHIINTRIN_H

#ifdef __x86_64__

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__, __target__("prefetchi")))

/// Loads an instruction sequence containing the specified memory address into
///    all level cache.
///
///    Note that the effect of this intrinsic is dependent on the processor
///    implementation.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PREFETCHIT0 instruction.
///
/// \param __P
///    A pointer specifying the memory address to be prefetched.
static __inline__ void __DEFAULT_FN_ATTRS
_m_prefetchit0(volatile const void *__P) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
  __builtin_ia32_prefetchi((const void *)__P, 3 /* _MM_HINT_T0 */);
#pragma clang diagnostic pop
}

/// Loads an instruction sequence containing the specified memory address into
///    all but the first-level cache.
///
///    Note that the effect of this intrinsic is dependent on the processor
///    implementation.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PREFETCHIT1 instruction.
///
/// \param __P
///    A pointer specifying the memory address to be prefetched.
static __inline__ void __DEFAULT_FN_ATTRS
_m_prefetchit1(volatile const void *__P) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
  __builtin_ia32_prefetchi((const void *)__P, 2 /* _MM_HINT_T1 */);
#pragma clang diagnostic pop
}
#endif /* __x86_64__ */
#undef __DEFAULT_FN_ATTRS

#endif /* __PRFCHWINTRIN_H */
