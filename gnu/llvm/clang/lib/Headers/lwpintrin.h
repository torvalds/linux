/*===---- lwpintrin.h - LWP intrinsics -------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __X86INTRIN_H
#error "Never use <lwpintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __LWPINTRIN_H
#define __LWPINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("lwp")))

/// Parses the LWPCB at the specified address and enables
///        profiling if valid.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> LLWPCB </c> instruction.
///
/// \param __addr
///    Address to the new Lightweight Profiling Control Block (LWPCB). If the
///    LWPCB is valid, writes the address into the LWP_CBADDR MSR and enables
///    Lightweight Profiling.
static __inline__ void __DEFAULT_FN_ATTRS
__llwpcb (void *__addr)
{
  __builtin_ia32_llwpcb(__addr);
}

/// Flushes the LWP state to memory and returns the address of the LWPCB.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> SLWPCB </c> instruction.
///
/// \return
///    Address to the current Lightweight Profiling Control Block (LWPCB).
///    If LWP is not currently enabled, returns NULL.
static __inline__ void* __DEFAULT_FN_ATTRS
__slwpcb (void)
{
  return __builtin_ia32_slwpcb();
}

/// Inserts programmed event record into the LWP event ring buffer
///        and advances the ring buffer pointer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> LWPINS </c> instruction.
///
/// \param DATA2
///    A 32-bit value is zero-extended and inserted into the 64-bit Data2 field.
/// \param DATA1
///    A 32-bit value is inserted into the 32-bit Data1 field.
/// \param FLAGS
///    A 32-bit immediate value is inserted into the 32-bit Flags field.
/// \returns If the ring buffer is full and LWP is running in Synchronized Mode,
///    the event record overwrites the last record in the buffer, the MissedEvents
///    counter in the LWPCB is incremented, the head pointer is not advanced, and
///    1 is returned. Otherwise 0 is returned.
#define __lwpins32(DATA2, DATA1, FLAGS) \
  (__builtin_ia32_lwpins32((unsigned int) (DATA2), (unsigned int) (DATA1), \
                           (unsigned int) (FLAGS)))

/// Decrements the LWP programmed value sample event counter. If the result is
///        negative, inserts an event record into the LWP event ring buffer in memory
///        and advances the ring buffer pointer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> LWPVAL </c> instruction.
///
/// \param DATA2
///    A 32-bit value is zero-extended and inserted into the 64-bit Data2 field.
/// \param DATA1
///    A 32-bit value is inserted into the 32-bit Data1 field.
/// \param FLAGS
///    A 32-bit immediate value is inserted into the 32-bit Flags field.
#define __lwpval32(DATA2, DATA1, FLAGS) \
  (__builtin_ia32_lwpval32((unsigned int) (DATA2), (unsigned int) (DATA1), \
                           (unsigned int) (FLAGS)))

#ifdef __x86_64__

/// Inserts programmed event record into the LWP event ring buffer
///        and advances the ring buffer pointer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> LWPINS </c> instruction.
///
/// \param DATA2
///    A 64-bit value is inserted into the 64-bit Data2 field.
/// \param DATA1
///    A 32-bit value is inserted into the 32-bit Data1 field.
/// \param FLAGS
///    A 32-bit immediate value is inserted into the 32-bit Flags field.
/// \returns If the ring buffer is full and LWP is running in Synchronized Mode,
///    the event record overwrites the last record in the buffer, the MissedEvents
///    counter in the LWPCB is incremented, the head pointer is not advanced, and
///    1 is returned. Otherwise 0 is returned.
#define __lwpins64(DATA2, DATA1, FLAGS) \
  (__builtin_ia32_lwpins64((unsigned long long) (DATA2), (unsigned int) (DATA1), \
                           (unsigned int) (FLAGS)))

/// Decrements the LWP programmed value sample event counter. If the result is
///        negative, inserts an event record into the LWP event ring buffer in memory
///        and advances the ring buffer pointer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> LWPVAL </c> instruction.
///
/// \param DATA2
///    A 64-bit value is and inserted into the 64-bit Data2 field.
/// \param DATA1
///    A 32-bit value is inserted into the 32-bit Data1 field.
/// \param FLAGS
///    A 32-bit immediate value is inserted into the 32-bit Flags field.
#define __lwpval64(DATA2, DATA1, FLAGS) \
  (__builtin_ia32_lwpval64((unsigned long long) (DATA2), (unsigned int) (DATA1), \
                           (unsigned int) (FLAGS)))

#endif

#undef __DEFAULT_FN_ATTRS

#endif /* __LWPINTRIN_H */
