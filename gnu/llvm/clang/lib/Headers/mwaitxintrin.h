/*===---- mwaitxintrin.h - MONITORX/MWAITX intrinsics ----------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __X86INTRIN_H
#error "Never use <mwaitxintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __MWAITXINTRIN_H
#define __MWAITXINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__,  __target__("mwaitx")))

/// Establishes a linear address memory range to be monitored and puts
///    the processor in the monitor event pending state. Data stored in the
///    monitored address range causes the processor to exit the pending state.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c MONITORX instruction.
///
/// \param __p
///    The memory range to be monitored. The size of the range is determined by
///    CPUID function 0000_0005h.
/// \param __extensions
///    Optional extensions for the monitoring state.
/// \param __hints
///    Optional hints for the monitoring state.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_monitorx(void * __p, unsigned __extensions, unsigned __hints)
{
  __builtin_ia32_monitorx(__p, __extensions, __hints);
}

/// Used with the \c MONITORX instruction to wait while the processor is in
///    the monitor event pending state. Data stored in the monitored address
///    range, or an interrupt, causes the processor to exit the pending state.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c MWAITX instruction.
///
/// \param __extensions
///    Optional extensions for the monitoring state, which can vary by
///    processor.
/// \param __hints
///    Optional hints for the monitoring state, which can vary by processor.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_mwaitx(unsigned __extensions, unsigned __hints, unsigned __clock)
{
  __builtin_ia32_mwaitx(__extensions, __hints, __clock);
}

#undef __DEFAULT_FN_ATTRS

#endif /* __MWAITXINTRIN_H */
