/*===---- __wmmintrin_pclmul.h - PCMUL intrinsics ---------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __WMMINTRIN_H
#error "Never use <__wmmintrin_pclmul.h> directly; include <wmmintrin.h> instead."
#endif

#ifndef __WMMINTRIN_PCLMUL_H
#define __WMMINTRIN_PCLMUL_H

/// Multiplies two 64-bit integer values, which are selected from source
///    operands using the immediate-value operand. The multiplication is a
///    carry-less multiplication, and the 128-bit integer product is stored in
///    the destination.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_clmulepi64_si128(__m128i X, __m128i Y, const int I);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCLMULQDQ </c> instruction.
///
/// \param X
///    A 128-bit vector of [2 x i64] containing one of the source operands.
/// \param Y
///    A 128-bit vector of [2 x i64] containing one of the source operands.
/// \param I
///    An immediate value specifying which 64-bit values to select from the
///    operands. Bit 0 is used to select a value from operand \a X, and bit
///    4 is used to select a value from operand \a Y: \n
///    Bit[0]=0 indicates that bits[63:0] of operand \a X are used. \n
///    Bit[0]=1 indicates that bits[127:64] of operand \a X are used. \n
///    Bit[4]=0 indicates that bits[63:0] of operand \a Y are used. \n
///    Bit[4]=1 indicates that bits[127:64] of operand \a Y are used.
/// \returns The 128-bit integer vector containing the result of the carry-less
///    multiplication of the selected 64-bit values.
#define _mm_clmulepi64_si128(X, Y, I) \
  ((__m128i)__builtin_ia32_pclmulqdq128((__v2di)(__m128i)(X), \
                                        (__v2di)(__m128i)(Y), (char)(I)))

#endif /* __WMMINTRIN_PCLMUL_H */
