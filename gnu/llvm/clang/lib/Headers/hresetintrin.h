/*===---------------- hresetintrin.h - HRESET intrinsics -------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __X86GPRINTRIN_H
#error "Never use <hresetintrin.h> directly; include <x86gprintrin.h> instead."
#endif

#ifndef __HRESETINTRIN_H
#define __HRESETINTRIN_H

#if __has_extension(gnu_asm)

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__, __target__("hreset")))

/// Provides a hint to the processor to selectively reset the prediction
///    history of the current logical processor specified by a 32-bit integer
///    value \a __eax.
///
/// This intrinsic corresponds to the <c> HRESET </c> instruction.
///
/// \code{.operation}
///    IF __eax == 0
///      // nop
///    ELSE
///      FOR i := 0 to 31
///        IF __eax[i]
///          ResetPredictionFeature(i)
///        FI
///      ENDFOR
///    FI
/// \endcode
static __inline void __DEFAULT_FN_ATTRS
_hreset(int __eax)
{
  __asm__ ("hreset $0" :: "a"(__eax));
}

#undef __DEFAULT_FN_ATTRS

#endif /* __has_extension(gnu_asm) */

#endif /* __HRESETINTRIN_H */
