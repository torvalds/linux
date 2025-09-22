/*===--------------- cmpccxaddintrin.h - CMPCCXADD intrinsics--------------===
 *
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __X86GPRINTRIN_H
#error                                                                         \
    "Never use <cmpccxaddintrin.h> directly; include <x86gprintrin.h> instead."
#endif // __X86GPRINTRIN_H

#ifndef __CMPCCXADDINTRIN_H
#define __CMPCCXADDINTRIN_H
#ifdef __x86_64__

typedef enum {
  _CMPCCX_O,   /* Overflow.  */
  _CMPCCX_NO,  /* No overflow.  */
  _CMPCCX_B,   /* Below.  */
  _CMPCCX_NB,  /* Not below.  */
  _CMPCCX_Z,   /* Zero.  */
  _CMPCCX_NZ,  /* Not zero.  */
  _CMPCCX_BE,  /* Below or equal.  */
  _CMPCCX_NBE, /* Neither below nor equal.  */
  _CMPCCX_S,   /* Sign.  */
  _CMPCCX_NS,  /* No sign.  */
  _CMPCCX_P,   /* Parity.  */
  _CMPCCX_NP,  /* No parity.  */
  _CMPCCX_L,   /* Less.  */
  _CMPCCX_NL,  /* Not less.  */
  _CMPCCX_LE,  /* Less or equal.  */
  _CMPCCX_NLE, /* Neither less nor equal.  */
} _CMPCCX_ENUM;

/// Compares the value from the memory __A with the value of __B. If the
/// specified condition __D is met, then add the third operand __C to the
/// __A and write it into __A, else the value of __A is unchanged. The return
/// value is the original value of __A.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c CMPCCXADD instructions.
///
/// \param __A
///    __A pointer specifying the memory address.
///
/// \param __B
///   A integer operand.
///
/// \param __C
///   A integer operand.
///
/// \param __D
///   The specified condition.
///
/// \returns a integer which is the original value of first operand.

#define _cmpccxadd_epi32(__A, __B, __C, __D)                                   \
  ((int)(__builtin_ia32_cmpccxadd32((void *)(__A), (int)(__B), (int)(__C),     \
                                    (int)(__D))))

#define _cmpccxadd_epi64(__A, __B, __C, __D)                                   \
  ((long long)(__builtin_ia32_cmpccxadd64((void *)(__A), (long long)(__B),     \
                                          (long long)(__C), (int)(__D))))

#endif // __x86_64__
#endif // __CMPCCXADDINTRIN_H
