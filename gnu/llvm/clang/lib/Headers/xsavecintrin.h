/*===---- xsavecintrin.h - XSAVEC intrinsic --------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <xsavecintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __XSAVECINTRIN_H
#define __XSAVECINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__,  __target__("xsavec")))

/// Performs a full or partial save of processor state to the memory at
///    \a __p. The exact state saved depends on the 64-bit mask \a __m and
///    processor control register \c XCR0.
///
/// \code{.operation}
/// mask[62:0] := __m[62:0] AND XCR0[62:0]
/// FOR i := 0 TO 62
///   IF mask[i] == 1
///     CASE (i) OF
///     0: save X87 FPU state
///     1: save SSE state
///     DEFAULT: __p.Ext_Save_Area[i] := ProcessorState[i]
///   FI
/// ENDFOR
/// __p.Header.XSTATE_BV[62:0] := INIT_FUNCTION(mask[62:0])
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c XSAVEC instruction.
///
/// \param __p
///    Pointer to the save area; must be 64-byte aligned.
/// \param __m
///    A 64-bit mask indicating what state should be saved.
static __inline__ void __DEFAULT_FN_ATTRS
_xsavec(void *__p, unsigned long long __m) {
  __builtin_ia32_xsavec(__p, __m);
}

#ifdef __x86_64__
/// Performs a full or partial save of processor state to the memory at
///    \a __p. The exact state saved depends on the 64-bit mask \a __m and
///    processor control register \c XCR0.
///
/// \code{.operation}
/// mask[62:0] := __m[62:0] AND XCR0[62:0]
/// FOR i := 0 TO 62
///   IF mask[i] == 1
///     CASE (i) OF
///     0: save X87 FPU state
///     1: save SSE state
///     DEFAULT: __p.Ext_Save_Area[i] := ProcessorState[i]
///   FI
/// ENDFOR
/// __p.Header.XSTATE_BV[62:0] := INIT_FUNCTION(mask[62:0])
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c XSAVEC64 instruction.
///
/// \param __p
///    Pointer to the save area; must be 64-byte aligned.
/// \param __m
///    A 64-bit mask indicating what state should be saved.
static __inline__ void __DEFAULT_FN_ATTRS
_xsavec64(void *__p, unsigned long long __m) {
  __builtin_ia32_xsavec64(__p, __m);
}
#endif

#undef __DEFAULT_FN_ATTRS

#endif
