/*===---- rdpruintrin.h - RDPRU intrinsics ---------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#if !defined __X86INTRIN_H
#error "Never use <rdpruintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __RDPRUINTRIN_H
#define __RDPRUINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__,  __target__("rdpru")))


/// Reads the content of a processor register.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> RDPRU </c> instruction.
///
/// \param reg_id
///    A processor register identifier.
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
__rdpru (int reg_id)
{
  return __builtin_ia32_rdpru(reg_id);
}

#define __RDPRU_MPERF 0
#define __RDPRU_APERF 1

/// Reads the content of processor register MPERF.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic generates instruction <c> RDPRU </c> to read the value of
/// register MPERF.
#define __mperf() __builtin_ia32_rdpru(__RDPRU_MPERF)

/// Reads the content of processor register APERF.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic generates instruction <c> RDPRU </c> to read the value of
/// register APERF.
#define __aperf() __builtin_ia32_rdpru(__RDPRU_APERF)

#undef __DEFAULT_FN_ATTRS

#endif /* __RDPRUINTRIN_H */
