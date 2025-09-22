/*===--------------- usermsrintrin.h - USERMSR intrinsics -----------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __X86GPRINTRIN_H
#error "Never use <usermsrintrin.h> directly; include <x86gprintrin.h> instead."
#endif // __X86GPRINTRIN_H

#ifndef __USERMSRINTRIN_H
#define __USERMSRINTRIN_H
#ifdef __x86_64__

/// Reads the contents of a 64-bit MSR specified in \a __A into \a dst.
///
/// This intrinsic corresponds to the <c> URDMSR </c> instruction.
/// \param __A
///    An unsigned long long.
///
/// \code{.operation}
///    DEST := MSR[__A]
/// \endcode
static __inline__ unsigned long long
    __attribute__((__always_inline__, __nodebug__, __target__("usermsr")))
    _urdmsr(unsigned long long __A) {
  return __builtin_ia32_urdmsr(__A);
}

/// Writes the contents of \a __B into the 64-bit MSR specified in \a __A.
///
/// This intrinsic corresponds to the <c> UWRMSR </c> instruction.
///
/// \param __A
///    An unsigned long long.
/// \param __B
///    An unsigned long long.
///
/// \code{.operation}
///    MSR[__A] := __B
/// \endcode
static __inline__ void
    __attribute__((__always_inline__, __nodebug__, __target__("usermsr")))
    _uwrmsr(unsigned long long __A, unsigned long long __B) {
  return __builtin_ia32_uwrmsr(__A, __B);
}

#endif // __x86_64__
#endif // __USERMSRINTRIN_H
