/*===---- __wmmintrin_aes.h - AES intrinsics -------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __WMMINTRIN_H
#error "Never use <__wmmintrin_aes.h> directly; include <wmmintrin.h> instead."
#endif

#ifndef __WMMINTRIN_AES_H
#define __WMMINTRIN_AES_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("aes"), __min_vector_width__(128)))

/// Performs a single round of AES encryption using the Equivalent
///    Inverse Cipher, transforming the state value from the first source
///    operand using a 128-bit round key value contained in the second source
///    operand, and writes the result to the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VAESENC </c> instruction.
///
/// \param __V
///    A 128-bit integer vector containing the state value.
/// \param __R
///    A 128-bit integer vector containing the round key value.
/// \returns A 128-bit integer vector containing the encrypted value.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_aesenc_si128(__m128i __V, __m128i __R)
{
  return (__m128i)__builtin_ia32_aesenc128((__v2di)__V, (__v2di)__R);
}

/// Performs the final round of AES encryption using the Equivalent
///    Inverse Cipher, transforming the state value from the first source
///    operand using a 128-bit round key value contained in the second source
///    operand, and writes the result to the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VAESENCLAST </c> instruction.
///
/// \param __V
///    A 128-bit integer vector containing the state value.
/// \param __R
///    A 128-bit integer vector containing the round key value.
/// \returns A 128-bit integer vector containing the encrypted value.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_aesenclast_si128(__m128i __V, __m128i __R)
{
  return (__m128i)__builtin_ia32_aesenclast128((__v2di)__V, (__v2di)__R);
}

/// Performs a single round of AES decryption using the Equivalent
///    Inverse Cipher, transforming the state value from the first source
///    operand using a 128-bit round key value contained in the second source
///    operand, and writes the result to the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VAESDEC </c> instruction.
///
/// \param __V
///    A 128-bit integer vector containing the state value.
/// \param __R
///    A 128-bit integer vector containing the round key value.
/// \returns A 128-bit integer vector containing the decrypted value.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_aesdec_si128(__m128i __V, __m128i __R)
{
  return (__m128i)__builtin_ia32_aesdec128((__v2di)__V, (__v2di)__R);
}

/// Performs the final round of AES decryption using the Equivalent
///    Inverse Cipher, transforming the state value from the first source
///    operand using a 128-bit round key value contained in the second source
///    operand, and writes the result to the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VAESDECLAST </c> instruction.
///
/// \param __V
///    A 128-bit integer vector containing the state value.
/// \param __R
///    A 128-bit integer vector containing the round key value.
/// \returns A 128-bit integer vector containing the decrypted value.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_aesdeclast_si128(__m128i __V, __m128i __R)
{
  return (__m128i)__builtin_ia32_aesdeclast128((__v2di)__V, (__v2di)__R);
}

/// Applies the AES InvMixColumns() transformation to an expanded key
///    contained in the source operand, and writes the result to the
///    destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VAESIMC </c> instruction.
///
/// \param __V
///    A 128-bit integer vector containing the expanded key.
/// \returns A 128-bit integer vector containing the transformed value.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_aesimc_si128(__m128i __V)
{
  return (__m128i)__builtin_ia32_aesimc128((__v2di)__V);
}

/// Generates a round key for AES encryption, operating on 128-bit data
///    specified in the first source operand and using an 8-bit round constant
///    specified by the second source operand, and writes the result to the
///    destination.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_aeskeygenassist_si128(__m128i C, const int R);
/// \endcode
///
/// This intrinsic corresponds to the <c> AESKEYGENASSIST </c> instruction.
///
/// \param C
///    A 128-bit integer vector that is used to generate the AES encryption key.
/// \param R
///    An 8-bit round constant used to generate the AES encryption key.
/// \returns A 128-bit round key for AES encryption.
#define _mm_aeskeygenassist_si128(C, R) \
  ((__m128i)__builtin_ia32_aeskeygenassist128((__v2di)(__m128i)(C), (int)(R)))

#undef __DEFAULT_FN_ATTRS

#endif  /* __WMMINTRIN_AES_H */
