/*===------------------ uintrintrin.h - UINTR intrinsics -------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __X86GPRINTRIN_H
#error "Never use <uintrintrin.h> directly; include <x86gprintrin.h> instead."
#endif

#ifndef __UINTRINTRIN_H
#define __UINTRINTRIN_H

/* Define the default attributes for the functions in this file */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__, __target__("uintr")))

#ifdef __x86_64__

struct __uintr_frame
{
  unsigned long long rip;
  unsigned long long rflags;
  unsigned long long rsp;
};

/// Clears the user interrupt flag (UIF). Its effect takes place immediately: a
///    user interrupt cannot be delivered on the instruction boundary following
///    CLUI. Can be executed only if CR4.UINT = 1, the logical processor is in
///    64-bit mode, and software is not executing inside an enclave; otherwise,
///    each causes an invalid-opcode exception. Causes a transactional abort if
///    executed inside a transactional region; the abort loads EAX as it would
///    had it been due to an execution of CLI.
///
/// \headerfile <x86gprintrin.h>
///
/// This intrinsic corresponds to the <c> CLUI </c> instruction.
///
/// \code{.operation}
///   UIF := 0
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS
_clui (void)
{
  __builtin_ia32_clui();
}

/// Sets the user interrupt flag (UIF). Its effect takes place immediately; a
///    user interrupt may be delivered on the instruction boundary following
///    STUI. Can be executed only if CR4.UINT = 1, the logical processor is in
///    64-bit mode, and software is not executing inside an enclave; otherwise,
///    each causes an invalid-opcode exception. Causes a transactional abort if
///    executed inside a transactional region; the abort loads EAX as it would
///    had it been due to an execution of STI.
///
/// \headerfile <x86gprintrin.h>
///
/// This intrinsic corresponds to the <c> STUI </c> instruction.
///
/// \code{.operation}
///   UIF := 1
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS
_stui (void)
{
  __builtin_ia32_stui();
}

/// Get the current value of the user interrupt flag (UIF). Can be executed
///    regardless of CPL and inside a transactional region. Can be executed only
///    if CR4.UINT = 1, the logical processor is in 64-bit mode, and software is
///    not executing inside an enclave; otherwise, it causes an invalid-opcode
///    exception.
///
/// \headerfile <x86gprintrin.h>
///
/// This intrinsic corresponds to the <c> TESTUI </c> instruction.
///
/// \returns The current value of the user interrupt flag (UIF).
///
/// \code{.operation}
///   CF := UIF
///   ZF := 0
///   AF := 0
///   OF := 0
///   PF := 0
///   SF := 0
///   dst := CF
/// \endcode
static __inline__ unsigned char __DEFAULT_FN_ATTRS
_testui (void)
{
  return __builtin_ia32_testui();
}

/// Send interprocessor user interrupt. Can be executed only if
///    CR4.UINT = IA32_UINT_TT[0] = 1, the logical processor is in 64-bit mode,
///    and software is not executing inside an enclave; otherwise, it causes an
///    invalid-opcode exception. May be executed at any privilege level, all of
///    its memory accesses are performed with supervisor privilege.
///
/// \headerfile <x86gprintrin.h>
///
/// This intrinsic corresponds to the <c> SENDUIPI </c> instruction
///
/// \param __a
///    Index of user-interrupt target table entry in user-interrupt target
///    table.
///
/// \code{.operation}
///   IF __a > UITTSZ
///     GP (0)
///   FI
///   tempUITTE := MEM[UITTADDR + (a<<4)]
///   // tempUITTE must be valid, and can't have any reserved bit set
///   IF (tempUITTE.V == 0 OR tempUITTE[7:1] != 0)
///     GP (0)
///   FI
///   tempUPID := MEM[tempUITTE.UPIDADDR] // under lock
///   // tempUPID can't have any reserved bit set
///   IF (tempUPID[15:2] != 0 OR tempUPID[31:24] != 0)
///     GP (0) // release lock
///   FI
///   tempUPID.PIR[tempUITTE.UV] := 1;
///   IF (tempUPID.SN == 0 AND tempUPID.ON == 0)
///     tempUPID.ON := 1
///     sendNotify := 1
///   ELSE
///     sendNotify := 0
///   FI
///   MEM[tempUITTE.UPIDADDR] := tempUPID // release lock
///   IF sendNotify == 1
///     IF IA32_APIC_BASE[10] == 1 // local APIC is in x2APIC mode
///       // send ordinary IPI with vector tempUPID.NV to 32-bit physical APIC
///       // ID tempUPID.NDST
///       SendOrdinaryIPI(tempUPID.NV, tempUPID.NDST)
///     ELSE
///       // send ordinary IPI with vector tempUPID.NV to 8-bit physical APIC
///       // ID tempUPID.NDST[15:8]
///       SendOrdinaryIPI(tempUPID.NV, tempUPID.NDST[15:8])
///     FI
///   FI
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS
_senduipi (unsigned long long __a)
{
  __builtin_ia32_senduipi(__a);
}

#endif /* __x86_64__ */

#undef __DEFAULT_FN_ATTRS

#endif /* __UINTRINTRIN_H */
