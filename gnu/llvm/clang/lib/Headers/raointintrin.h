/*===----------------------- raointintrin.h - RAOINT ------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __X86GPRINTRIN_H
#error "Never use <raointintrin.h> directly; include <x86gprintrin.h> instead."
#endif // __X86GPRINTRIN_H

#ifndef __RAOINTINTRIN_H
#define __RAOINTINTRIN_H

#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__, __target__("raoint")))

/// Atomically add a 32-bit value at memory operand \a __A and a 32-bit \a __B,
///    and store the result to the same memory location.
///
///    This intrinsic should be used for contention or weak ordering. It may
///    result in bad performance for hot data used by single thread only.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c AADD instruction.
///
/// \param __A
///    A pointer to a 32-bit memory location.
/// \param __B
///    A 32-bit integer value.
///
/// \code{.operation}
/// MEM[__A+31:__A] := MEM[__A+31:__A] + __B[31:0]
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS _aadd_i32(int *__A, int __B) {
  __builtin_ia32_aadd32((int *)__A, __B);
}

/// Atomically and a 32-bit value at memory operand \a __A and a 32-bit \a __B,
///    and store the result to the same memory location.
///
///    This intrinsic should be used for contention or weak ordering. It may
///    result in bad performance for hot data used by single thread only.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c AAND instruction.
///
/// \param __A
///    A pointer to a 32-bit memory location.
/// \param __B
///    A 32-bit integer value.
///
/// \code{.operation}
/// MEM[__A+31:__A] := MEM[__A+31:__A] AND __B[31:0]
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS _aand_i32(int *__A, int __B) {
  __builtin_ia32_aand32((int *)__A, __B);
}

/// Atomically or a 32-bit value at memory operand \a __A and a 32-bit \a __B,
///    and store the result to the same memory location.
///
///    This intrinsic should be used for contention or weak ordering. It may
///    result in bad performance for hot data used by single thread only.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c AOR instruction.
///
/// \param __A
///    A pointer to a 32-bit memory location.
/// \param __B
///    A 32-bit integer value.
///
/// \code{.operation}
/// MEM[__A+31:__A] := MEM[__A+31:__A] OR __B[31:0]
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS _aor_i32(int *__A, int __B) {
  __builtin_ia32_aor32((int *)__A, __B);
}

/// Atomically xor a 32-bit value at memory operand \a __A and a 32-bit \a __B,
///    and store the result to the same memory location.
///
///    This intrinsic should be used for contention or weak ordering. It may
///    result in bad performance for hot data used by single thread only.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c AXOR instruction.
///
/// \param __A
///    A pointer to a 32-bit memory location.
/// \param __B
///    A 32-bit integer value.
///
/// \code{.operation}
/// MEM[__A+31:__A] := MEM[__A+31:__A] XOR __B[31:0]
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS _axor_i32(int *__A, int __B) {
  __builtin_ia32_axor32((int *)__A, __B);
}

#ifdef __x86_64__
/// Atomically add a 64-bit value at memory operand \a __A and a 64-bit \a __B,
///    and store the result to the same memory location.
///
///    This intrinsic should be used for contention or weak ordering. It may
///    result in bad performance for hot data used by single thread only.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c AADD instruction.
///
/// \param __A
///    A pointer to a 64-bit memory location.
/// \param __B
///    A 64-bit integer value.
///
/// \code{.operation}
/// MEM[__A+63:__A] := MEM[__A+63:__A] + __B[63:0]
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS _aadd_i64(long long *__A,
                                                    long long __B) {
  __builtin_ia32_aadd64((long long *)__A, __B);
}

/// Atomically and a 64-bit value at memory operand \a __A and a 64-bit \a __B,
///    and store the result to the same memory location.
///
///    This intrinsic should be used for contention or weak ordering. It may
///    result in bad performance for hot data used by single thread only.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c AAND instruction.
///
/// \param __A
///    A pointer to a 64-bit memory location.
/// \param __B
///    A 64-bit integer value.
///
/// \code{.operation}
/// MEM[__A+63:__A] := MEM[__A+63:__A] AND __B[63:0]
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS _aand_i64(long long *__A,
                                                    long long __B) {
  __builtin_ia32_aand64((long long *)__A, __B);
}

/// Atomically or a 64-bit value at memory operand \a __A and a 64-bit \a __B,
///    and store the result to the same memory location.
///
///    This intrinsic should be used for contention or weak ordering. It may
///    result in bad performance for hot data used by single thread only.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c AOR instruction.
///
/// \param __A
///    A pointer to a 64-bit memory location.
/// \param __B
///    A 64-bit integer value.
///
/// \code{.operation}
/// MEM[__A+63:__A] := MEM[__A+63:__A] OR __B[63:0]
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS _aor_i64(long long *__A,
                                                   long long __B) {
  __builtin_ia32_aor64((long long *)__A, __B);
}

/// Atomically xor a 64-bit value at memory operand \a __A and a 64-bit \a __B,
///    and store the result to the same memory location.
///
///    This intrinsic should be used for contention or weak ordering. It may
///    result in bad performance for hot data used by single thread only.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c AXOR instruction.
///
/// \param __A
///    A pointer to a 64-bit memory location.
/// \param __B
///    A 64-bit integer value.
///
/// \code{.operation}
/// MEM[__A+63:__A] := MEM[__A+63:__A] XOR __B[63:0]
/// \endcode
static __inline__ void __DEFAULT_FN_ATTRS _axor_i64(long long *__A,
                                                    long long __B) {
  __builtin_ia32_axor64((long long *)__A, __B);
}
#endif // __x86_64__

#undef __DEFAULT_FN_ATTRS
#endif // __RAOINTINTRIN_H
