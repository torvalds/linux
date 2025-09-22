/*===---- emmintrin.h - SSE2 intrinsics ------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __EMMINTRIN_H
#define __EMMINTRIN_H

#if !defined(__i386__) && !defined(__x86_64__)
#error "This header is only meant to be used on x86 and x64 architecture"
#endif

#include <xmmintrin.h>

typedef double __m128d __attribute__((__vector_size__(16), __aligned__(16)));
typedef long long __m128i __attribute__((__vector_size__(16), __aligned__(16)));

typedef double __m128d_u __attribute__((__vector_size__(16), __aligned__(1)));
typedef long long __m128i_u
    __attribute__((__vector_size__(16), __aligned__(1)));

/* Type defines.  */
typedef double __v2df __attribute__((__vector_size__(16)));
typedef long long __v2di __attribute__((__vector_size__(16)));
typedef short __v8hi __attribute__((__vector_size__(16)));
typedef char __v16qi __attribute__((__vector_size__(16)));

/* Unsigned types */
typedef unsigned long long __v2du __attribute__((__vector_size__(16)));
typedef unsigned short __v8hu __attribute__((__vector_size__(16)));
typedef unsigned char __v16qu __attribute__((__vector_size__(16)));

/* We need an explicitly signed variant for char. Note that this shouldn't
 * appear in the interface though. */
typedef signed char __v16qs __attribute__((__vector_size__(16)));

#ifdef __SSE2__
/* Both _Float16 and __bf16 require SSE2 being enabled. */
typedef _Float16 __v8hf __attribute__((__vector_size__(16), __aligned__(16)));
typedef _Float16 __m128h __attribute__((__vector_size__(16), __aligned__(16)));
typedef _Float16 __m128h_u __attribute__((__vector_size__(16), __aligned__(1)));

typedef __bf16 __v8bf __attribute__((__vector_size__(16), __aligned__(16)));
typedef __bf16 __m128bh __attribute__((__vector_size__(16), __aligned__(16)));
#endif

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("sse2,no-evex512"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS_MMX                                                 \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("mmx,sse2,no-evex512"), __min_vector_width__(64)))

/// Adds lower double-precision values in both operands and returns the
///    sum in the lower 64 bits of the result. The upper 64 bits of the result
///    are copied from the upper double-precision value of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDSD / ADDSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \returns A 128-bit vector of [2 x double] whose lower 64 bits contain the
///    sum of the lower 64 bits of both operands. The upper 64 bits are copied
///    from the upper 64 bits of the first source operand.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_add_sd(__m128d __a,
                                                        __m128d __b) {
  __a[0] += __b[0];
  return __a;
}

/// Adds two 128-bit vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDPD / ADDPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \returns A 128-bit vector of [2 x double] containing the sums of both
///    operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_add_pd(__m128d __a,
                                                        __m128d __b) {
  return (__m128d)((__v2df)__a + (__v2df)__b);
}

/// Subtracts the lower double-precision value of the second operand
///    from the lower double-precision value of the first operand and returns
///    the difference in the lower 64 bits of the result. The upper 64 bits of
///    the result are copied from the upper double-precision value of the first
///    operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSUBSD / SUBSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing the minuend.
/// \param __b
///    A 128-bit vector of [2 x double] containing the subtrahend.
/// \returns A 128-bit vector of [2 x double] whose lower 64 bits contain the
///    difference of the lower 64 bits of both operands. The upper 64 bits are
///    copied from the upper 64 bits of the first source operand.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_sub_sd(__m128d __a,
                                                        __m128d __b) {
  __a[0] -= __b[0];
  return __a;
}

/// Subtracts two 128-bit vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSUBPD / SUBPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing the minuend.
/// \param __b
///    A 128-bit vector of [2 x double] containing the subtrahend.
/// \returns A 128-bit vector of [2 x double] containing the differences between
///    both operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_sub_pd(__m128d __a,
                                                        __m128d __b) {
  return (__m128d)((__v2df)__a - (__v2df)__b);
}

/// Multiplies lower double-precision values in both operands and returns
///    the product in the lower 64 bits of the result. The upper 64 bits of the
///    result are copied from the upper double-precision value of the first
///    operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMULSD / MULSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \returns A 128-bit vector of [2 x double] whose lower 64 bits contain the
///    product of the lower 64 bits of both operands. The upper 64 bits are
///    copied from the upper 64 bits of the first source operand.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_mul_sd(__m128d __a,
                                                        __m128d __b) {
  __a[0] *= __b[0];
  return __a;
}

/// Multiplies two 128-bit vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMULPD / MULPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the operands.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the operands.
/// \returns A 128-bit vector of [2 x double] containing the products of both
///    operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_mul_pd(__m128d __a,
                                                        __m128d __b) {
  return (__m128d)((__v2df)__a * (__v2df)__b);
}

/// Divides the lower double-precision value of the first operand by the
///    lower double-precision value of the second operand and returns the
///    quotient in the lower 64 bits of the result. The upper 64 bits of the
///    result are copied from the upper double-precision value of the first
///    operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDIVSD / DIVSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing the dividend.
/// \param __b
///    A 128-bit vector of [2 x double] containing divisor.
/// \returns A 128-bit vector of [2 x double] whose lower 64 bits contain the
///    quotient of the lower 64 bits of both operands. The upper 64 bits are
///    copied from the upper 64 bits of the first source operand.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_div_sd(__m128d __a,
                                                        __m128d __b) {
  __a[0] /= __b[0];
  return __a;
}

/// Performs an element-by-element division of two 128-bit vectors of
///    [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDIVPD / DIVPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing the dividend.
/// \param __b
///    A 128-bit vector of [2 x double] containing the divisor.
/// \returns A 128-bit vector of [2 x double] containing the quotients of both
///    operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_div_pd(__m128d __a,
                                                        __m128d __b) {
  return (__m128d)((__v2df)__a / (__v2df)__b);
}

/// Calculates the square root of the lower double-precision value of
///    the second operand and returns it in the lower 64 bits of the result.
///    The upper 64 bits of the result are copied from the upper
///    double-precision value of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSQRTSD / SQRTSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the operands. The
///    upper 64 bits of this operand are copied to the upper 64 bits of the
///    result.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the operands. The
///    square root is calculated using the lower 64 bits of this operand.
/// \returns A 128-bit vector of [2 x double] whose lower 64 bits contain the
///    square root of the lower 64 bits of operand \a __b, and whose upper 64
///    bits are copied from the upper 64 bits of operand \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_sqrt_sd(__m128d __a,
                                                         __m128d __b) {
  __m128d __c = __builtin_ia32_sqrtsd((__v2df)__b);
  return __extension__(__m128d){__c[0], __a[1]};
}

/// Calculates the square root of the each of two values stored in a
///    128-bit vector of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSQRTPD / SQRTPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector of [2 x double] containing the square roots of the
///    values in the operand.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_sqrt_pd(__m128d __a) {
  return __builtin_ia32_sqrtpd((__v2df)__a);
}

/// Compares lower 64-bit double-precision values of both operands, and
///    returns the lesser of the pair of values in the lower 64-bits of the
///    result. The upper 64 bits of the result are copied from the upper
///    double-precision value of the first operand.
///
///    If either value in a comparison is NaN, returns the value from \a __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMINSD / MINSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the operands. The
///    lower 64 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the operands. The
///    lower 64 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [2 x double] whose lower 64 bits contain the
///    minimum value between both operands. The upper 64 bits are copied from
///    the upper 64 bits of the first source operand.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_min_sd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_minsd((__v2df)__a, (__v2df)__b);
}

/// Performs element-by-element comparison of the two 128-bit vectors of
///    [2 x double] and returns a vector containing the lesser of each pair of
///    values.
///
///    If either value in a comparison is NaN, returns the value from \a __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMINPD / MINPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the operands.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the operands.
/// \returns A 128-bit vector of [2 x double] containing the minimum values
///    between both operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_min_pd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_minpd((__v2df)__a, (__v2df)__b);
}

/// Compares lower 64-bit double-precision values of both operands, and
///    returns the greater of the pair of values in the lower 64-bits of the
///    result. The upper 64 bits of the result are copied from the upper
///    double-precision value of the first operand.
///
///    If either value in a comparison is NaN, returns the value from \a __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMAXSD / MAXSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the operands. The
///    lower 64 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the operands. The
///    lower 64 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [2 x double] whose lower 64 bits contain the
///    maximum value between both operands. The upper 64 bits are copied from
///    the upper 64 bits of the first source operand.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_max_sd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_maxsd((__v2df)__a, (__v2df)__b);
}

/// Performs element-by-element comparison of the two 128-bit vectors of
///    [2 x double] and returns a vector containing the greater of each pair
///    of values.
///
///    If either value in a comparison is NaN, returns the value from \a __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMAXPD / MAXPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the operands.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the operands.
/// \returns A 128-bit vector of [2 x double] containing the maximum values
///    between both operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_max_pd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_maxpd((__v2df)__a, (__v2df)__b);
}

/// Performs a bitwise AND of two 128-bit vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPAND / PAND </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \returns A 128-bit vector of [2 x double] containing the bitwise AND of the
///    values between both operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_and_pd(__m128d __a,
                                                        __m128d __b) {
  return (__m128d)((__v2du)__a & (__v2du)__b);
}

/// Performs a bitwise AND of two 128-bit vectors of [2 x double], using
///    the one's complement of the values contained in the first source operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPANDN / PANDN </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing the left source operand. The
///    one's complement of this value is used in the bitwise AND.
/// \param __b
///    A 128-bit vector of [2 x double] containing the right source operand.
/// \returns A 128-bit vector of [2 x double] containing the bitwise AND of the
///    values in the second operand and the one's complement of the first
///    operand.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_andnot_pd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)(~(__v2du)__a & (__v2du)__b);
}

/// Performs a bitwise OR of two 128-bit vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPOR / POR </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \returns A 128-bit vector of [2 x double] containing the bitwise OR of the
///    values between both operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_or_pd(__m128d __a,
                                                       __m128d __b) {
  return (__m128d)((__v2du)__a | (__v2du)__b);
}

/// Performs a bitwise XOR of two 128-bit vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPXOR / PXOR </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the source operands.
/// \returns A 128-bit vector of [2 x double] containing the bitwise XOR of the
///    values between both operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_xor_pd(__m128d __a,
                                                        __m128d __b) {
  return (__m128d)((__v2du)__a ^ (__v2du)__b);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] for equality.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPEQPD / CMPEQPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpeq_pd(__m128d __a,
                                                          __m128d __b) {
  return (__m128d)__builtin_ia32_cmpeqpd((__v2df)__a, (__v2df)__b);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are less than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLTPD / CMPLTPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmplt_pd(__m128d __a,
                                                          __m128d __b) {
  return (__m128d)__builtin_ia32_cmpltpd((__v2df)__a, (__v2df)__b);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are less than or equal to those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLEPD / CMPLEPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmple_pd(__m128d __a,
                                                          __m128d __b) {
  return (__m128d)__builtin_ia32_cmplepd((__v2df)__a, (__v2df)__b);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are greater than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLTPD / CMPLTPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpgt_pd(__m128d __a,
                                                          __m128d __b) {
  return (__m128d)__builtin_ia32_cmpltpd((__v2df)__b, (__v2df)__a);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are greater than or equal to those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLEPD / CMPLEPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpge_pd(__m128d __a,
                                                          __m128d __b) {
  return (__m128d)__builtin_ia32_cmplepd((__v2df)__b, (__v2df)__a);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are ordered with respect to those in the second operand.
///
///    A pair of double-precision values are ordered with respect to each
///    other if neither value is a NaN. Each comparison returns 0x0 for false,
///    0xFFFFFFFFFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPORDPD / CMPORDPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpord_pd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpordpd((__v2df)__a, (__v2df)__b);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are unordered with respect to those in the second operand.
///
///    A pair of double-precision values are unordered with respect to each
///    other if one or both values are NaN. Each comparison returns 0x0 for
///    false, 0xFFFFFFFFFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPUNORDPD / CMPUNORDPD </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpunord_pd(__m128d __a,
                                                             __m128d __b) {
  return (__m128d)__builtin_ia32_cmpunordpd((__v2df)__a, (__v2df)__b);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are unequal to those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNEQPD / CMPNEQPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpneq_pd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpneqpd((__v2df)__a, (__v2df)__b);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are not less than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLTPD / CMPNLTPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpnlt_pd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpnltpd((__v2df)__a, (__v2df)__b);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are not less than or equal to those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLEPD / CMPNLEPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpnle_pd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpnlepd((__v2df)__a, (__v2df)__b);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are not greater than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLTPD / CMPNLTPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpngt_pd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpnltpd((__v2df)__b, (__v2df)__a);
}

/// Compares each of the corresponding double-precision values of the
///    128-bit vectors of [2 x double] to determine if the values in the first
///    operand are not greater than or equal to those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLEPD / CMPNLEPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector containing the comparison results.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpnge_pd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpnlepd((__v2df)__b, (__v2df)__a);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] for equality.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPEQSD / CMPEQSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpeq_sd(__m128d __a,
                                                          __m128d __b) {
  return (__m128d)__builtin_ia32_cmpeqsd((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is less than the corresponding value in
///    the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLTSD / CMPLTSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmplt_sd(__m128d __a,
                                                          __m128d __b) {
  return (__m128d)__builtin_ia32_cmpltsd((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is less than or equal to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLESD / CMPLESD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmple_sd(__m128d __a,
                                                          __m128d __b) {
  return (__m128d)__builtin_ia32_cmplesd((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is greater than the corresponding value
///    in the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLTSD / CMPLTSD </c> instruction.
///
/// \param __a
///     A 128-bit vector of [2 x double]. The lower double-precision value is
///     compared to the lower double-precision value of \a __b.
/// \param __b
///     A 128-bit vector of [2 x double]. The lower double-precision value is
///     compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///     results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpgt_sd(__m128d __a,
                                                          __m128d __b) {
  __m128d __c = __builtin_ia32_cmpltsd((__v2df)__b, (__v2df)__a);
  return __extension__(__m128d){__c[0], __a[1]};
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is greater than or equal to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLESD / CMPLESD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpge_sd(__m128d __a,
                                                          __m128d __b) {
  __m128d __c = __builtin_ia32_cmplesd((__v2df)__b, (__v2df)__a);
  return __extension__(__m128d){__c[0], __a[1]};
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is ordered with respect to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true. A pair
///    of double-precision values are ordered with respect to each other if
///    neither value is a NaN.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPORDSD / CMPORDSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpord_sd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpordsd((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is unordered with respect to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true. A pair
///    of double-precision values are unordered with respect to each other if
///    one or both values are NaN.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPUNORDSD / CMPUNORDSD </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpunord_sd(__m128d __a,
                                                             __m128d __b) {
  return (__m128d)__builtin_ia32_cmpunordsd((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is unequal to the corresponding value in
///    the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNEQSD / CMPNEQSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpneq_sd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpneqsd((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is not less than the corresponding
///    value in the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLTSD / CMPNLTSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpnlt_sd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpnltsd((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is not less than or equal to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLESD / CMPNLESD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns  A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpnle_sd(__m128d __a,
                                                           __m128d __b) {
  return (__m128d)__builtin_ia32_cmpnlesd((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is not greater than the corresponding
///    value in the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLTSD / CMPNLTSD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpngt_sd(__m128d __a,
                                                           __m128d __b) {
  __m128d __c = __builtin_ia32_cmpnltsd((__v2df)__b, (__v2df)__a);
  return __extension__(__m128d){__c[0], __a[1]};
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is not greater than or equal to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLESD / CMPNLESD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns A 128-bit vector. The lower 64 bits contains the comparison
///    results. The upper 64 bits are copied from the upper 64 bits of \a __a.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cmpnge_sd(__m128d __a,
                                                           __m128d __b) {
  __m128d __c = __builtin_ia32_cmpnlesd((__v2df)__b, (__v2df)__a);
  return __extension__(__m128d){__c[0], __a[1]};
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] for equality.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISD / COMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_comieq_sd(__m128d __a,
                                                       __m128d __b) {
  return __builtin_ia32_comisdeq((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is less than the corresponding value in
///    the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISD / COMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_comilt_sd(__m128d __a,
                                                       __m128d __b) {
  return __builtin_ia32_comisdlt((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is less than or equal to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISD / COMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///     A 128-bit vector of [2 x double]. The lower double-precision value is
///     compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_comile_sd(__m128d __a,
                                                       __m128d __b) {
  return __builtin_ia32_comisdle((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is greater than the corresponding value
///    in the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISD / COMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_comigt_sd(__m128d __a,
                                                       __m128d __b) {
  return __builtin_ia32_comisdgt((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is greater than or equal to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISD / COMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_comige_sd(__m128d __a,
                                                       __m128d __b) {
  return __builtin_ia32_comisdge((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is unequal to the corresponding value in
///    the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISD / COMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_comineq_sd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_comisdneq((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] for equality.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISD / UCOMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_ucomieq_sd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_ucomisdeq((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is less than the corresponding value in
///    the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISD / UCOMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_ucomilt_sd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_ucomisdlt((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is less than or equal to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISD / UCOMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///     A 128-bit vector of [2 x double]. The lower double-precision value is
///     compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_ucomile_sd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_ucomisdle((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is greater than the corresponding value
///    in the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISD / UCOMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///     A 128-bit vector of [2 x double]. The lower double-precision value is
///     compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_ucomigt_sd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_ucomisdgt((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is greater than or equal to the
///    corresponding value in the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISD / UCOMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS _mm_ucomige_sd(__m128d __a,
                                                        __m128d __b) {
  return __builtin_ia32_ucomisdge((__v2df)__a, (__v2df)__b);
}

/// Compares the lower double-precision floating-point values in each of
///    the two 128-bit floating-point vectors of [2 x double] to determine if
///    the value in the first parameter is unequal to the corresponding value in
///    the second parameter.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISD / UCOMISD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __b.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision value is
///    compared to the lower double-precision value of \a __a.
/// \returns An integer containing the comparison result.
static __inline__ int __DEFAULT_FN_ATTRS _mm_ucomineq_sd(__m128d __a,
                                                         __m128d __b) {
  return __builtin_ia32_ucomisdneq((__v2df)__a, (__v2df)__b);
}

/// Converts the two double-precision floating-point elements of a
///    128-bit vector of [2 x double] into two single-precision floating-point
///    values, returned in the lower 64 bits of a 128-bit vector of [4 x float].
///    The upper 64 bits of the result vector are set to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTPD2PS / CVTPD2PS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector of [4 x float] whose lower 64 bits contain the
///    converted values. The upper 64 bits are set to zero.
static __inline__ __m128 __DEFAULT_FN_ATTRS _mm_cvtpd_ps(__m128d __a) {
  return __builtin_ia32_cvtpd2ps((__v2df)__a);
}

/// Converts the lower two single-precision floating-point elements of a
///    128-bit vector of [4 x float] into two double-precision floating-point
///    values, returned in a 128-bit vector of [2 x double]. The upper two
///    elements of the input vector are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTPS2PD / CVTPS2PD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower two single-precision
///    floating-point elements are converted to double-precision values. The
///    upper two elements are unused.
/// \returns A 128-bit vector of [2 x double] containing the converted values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cvtps_pd(__m128 __a) {
  return (__m128d) __builtin_convertvector(
      __builtin_shufflevector((__v4sf)__a, (__v4sf)__a, 0, 1), __v2df);
}

/// Converts the lower two integer elements of a 128-bit vector of
///    [4 x i32] into two double-precision floating-point values, returned in a
///    128-bit vector of [2 x double].
///
///    The upper two elements of the input vector are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTDQ2PD / CVTDQ2PD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector of [4 x i32]. The lower two integer elements are
///    converted to double-precision values.
///
///    The upper two elements are unused.
/// \returns A 128-bit vector of [2 x double] containing the converted values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cvtepi32_pd(__m128i __a) {
  return (__m128d) __builtin_convertvector(
      __builtin_shufflevector((__v4si)__a, (__v4si)__a, 0, 1), __v2df);
}

/// Converts the two double-precision floating-point elements of a
///    128-bit vector of [2 x double] into two signed 32-bit integer values,
///    returned in the lower 64 bits of a 128-bit vector of [4 x i32]. The upper
///    64 bits of the result vector are set to zero.
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTPD2DQ / CVTPD2DQ </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector of [4 x i32] whose lower 64 bits contain the
///    converted values. The upper 64 bits are set to zero.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cvtpd_epi32(__m128d __a) {
  return __builtin_ia32_cvtpd2dq((__v2df)__a);
}

/// Converts the low-order element of a 128-bit vector of [2 x double]
///    into a 32-bit signed integer value.
///
///    If the converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSD2SI / CVTSD2SI </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower 64 bits are used in the
///    conversion.
/// \returns A 32-bit signed integer containing the converted value.
static __inline__ int __DEFAULT_FN_ATTRS _mm_cvtsd_si32(__m128d __a) {
  return __builtin_ia32_cvtsd2si((__v2df)__a);
}

/// Converts the lower double-precision floating-point element of a
///    128-bit vector of [2 x double], in the second parameter, into a
///    single-precision floating-point value, returned in the lower 32 bits of a
///    128-bit vector of [4 x float]. The upper 96 bits of the result vector are
///    copied from the upper 96 bits of the first parameter.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSD2SS / CVTSD2SS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The upper 96 bits of this parameter are
///    copied to the upper 96 bits of the result.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower double-precision
///    floating-point element is used in the conversion.
/// \returns A 128-bit vector of [4 x float]. The lower 32 bits contain the
///    converted value from the second parameter. The upper 96 bits are copied
///    from the upper 96 bits of the first parameter.
static __inline__ __m128 __DEFAULT_FN_ATTRS _mm_cvtsd_ss(__m128 __a,
                                                         __m128d __b) {
  return (__m128)__builtin_ia32_cvtsd2ss((__v4sf)__a, (__v2df)__b);
}

/// Converts a 32-bit signed integer value, in the second parameter, into
///    a double-precision floating-point value, returned in the lower 64 bits of
///    a 128-bit vector of [2 x double]. The upper 64 bits of the result vector
///    are copied from the upper 64 bits of the first parameter.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSI2SD / CVTSI2SD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The upper 64 bits of this parameter are
///    copied to the upper 64 bits of the result.
/// \param __b
///    A 32-bit signed integer containing the value to be converted.
/// \returns A 128-bit vector of [2 x double]. The lower 64 bits contain the
///    converted value from the second parameter. The upper 64 bits are copied
///    from the upper 64 bits of the first parameter.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cvtsi32_sd(__m128d __a,
                                                            int __b) {
  __a[0] = __b;
  return __a;
}

/// Converts the lower single-precision floating-point element of a
///    128-bit vector of [4 x float], in the second parameter, into a
///    double-precision floating-point value, returned in the lower 64 bits of
///    a 128-bit vector of [2 x double]. The upper 64 bits of the result vector
///    are copied from the upper 64 bits of the first parameter.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSS2SD / CVTSS2SD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The upper 64 bits of this parameter are
///    copied to the upper 64 bits of the result.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower single-precision
///    floating-point element is used in the conversion.
/// \returns A 128-bit vector of [2 x double]. The lower 64 bits contain the
///    converted value from the second parameter. The upper 64 bits are copied
///    from the upper 64 bits of the first parameter.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cvtss_sd(__m128d __a,
                                                          __m128 __b) {
  __a[0] = __b[0];
  return __a;
}

/// Converts the two double-precision floating-point elements of a
///    128-bit vector of [2 x double] into two signed truncated (rounded
///    toward zero) 32-bit integer values, returned in the lower 64 bits
///    of a 128-bit vector of [4 x i32].
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTTPD2DQ / CVTTPD2DQ </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector of [4 x i32] whose lower 64 bits contain the
///    converted values. The upper 64 bits are set to zero.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cvttpd_epi32(__m128d __a) {
  return (__m128i)__builtin_ia32_cvttpd2dq((__v2df)__a);
}

/// Converts the low-order element of a [2 x double] vector into a 32-bit
///    signed truncated (rounded toward zero) integer value.
///
///    If the converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTTSD2SI / CVTTSD2SI </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower 64 bits are used in the
///    conversion.
/// \returns A 32-bit signed integer containing the converted value.
static __inline__ int __DEFAULT_FN_ATTRS _mm_cvttsd_si32(__m128d __a) {
  return __builtin_ia32_cvttsd2si((__v2df)__a);
}

/// Converts the two double-precision floating-point elements of a
///    128-bit vector of [2 x double] into two signed 32-bit integer values,
///    returned in a 64-bit vector of [2 x i32].
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPD2PI </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \returns A 64-bit vector of [2 x i32] containing the converted values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX _mm_cvtpd_pi32(__m128d __a) {
  return (__m64)__builtin_ia32_cvtpd2pi((__v2df)__a);
}

/// Converts the two double-precision floating-point elements of a
///    128-bit vector of [2 x double] into two signed truncated (rounded toward
///    zero) 32-bit integer values, returned in a 64-bit vector of [2 x i32].
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTTPD2PI </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \returns A 64-bit vector of [2 x i32] containing the converted values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX _mm_cvttpd_pi32(__m128d __a) {
  return (__m64)__builtin_ia32_cvttpd2pi((__v2df)__a);
}

/// Converts the two signed 32-bit integer elements of a 64-bit vector of
///    [2 x i32] into two double-precision floating-point values, returned in a
///    128-bit vector of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPI2PD </c> instruction.
///
/// \param __a
///    A 64-bit vector of [2 x i32].
/// \returns A 128-bit vector of [2 x double] containing the converted values.
static __inline__ __m128d __DEFAULT_FN_ATTRS_MMX _mm_cvtpi32_pd(__m64 __a) {
  return __builtin_ia32_cvtpi2pd((__v2si)__a);
}

/// Returns the low-order element of a 128-bit vector of [2 x double] as
///    a double-precision floating-point value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower 64 bits are returned.
/// \returns A double-precision floating-point value copied from the lower 64
///    bits of \a __a.
static __inline__ double __DEFAULT_FN_ATTRS _mm_cvtsd_f64(__m128d __a) {
  return __a[0];
}

/// Loads a 128-bit floating-point vector of [2 x double] from an aligned
///    memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPD / MOVAPD </c> instruction.
///
/// \param __dp
///    A pointer to a 128-bit memory location. The address of the memory
///    location has to be 16-byte aligned.
/// \returns A 128-bit vector of [2 x double] containing the loaded values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_load_pd(double const *__dp) {
  return *(const __m128d *)__dp;
}

/// Loads a double-precision floating-point value from a specified memory
///    location and duplicates it to both vector elements of a 128-bit vector of
///    [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDDUP / MOVDDUP </c> instruction.
///
/// \param __dp
///    A pointer to a memory location containing a double-precision value.
/// \returns A 128-bit vector of [2 x double] containing the loaded and
///    duplicated values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_load1_pd(double const *__dp) {
  struct __mm_load1_pd_struct {
    double __u;
  } __attribute__((__packed__, __may_alias__));
  double __u = ((const struct __mm_load1_pd_struct *)__dp)->__u;
  return __extension__(__m128d){__u, __u};
}

#define _mm_load_pd1(dp) _mm_load1_pd(dp)

/// Loads two double-precision values, in reverse order, from an aligned
///    memory location into a 128-bit vector of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPD / MOVAPD </c> instruction +
/// needed shuffling instructions. In AVX mode, the shuffling may be combined
/// with the \c VMOVAPD, resulting in only a \c VPERMILPD instruction.
///
/// \param __dp
///    A 16-byte aligned pointer to an array of double-precision values to be
///    loaded in reverse order.
/// \returns A 128-bit vector of [2 x double] containing the reversed loaded
///    values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_loadr_pd(double const *__dp) {
  __m128d __u = *(const __m128d *)__dp;
  return __builtin_shufflevector((__v2df)__u, (__v2df)__u, 1, 0);
}

/// Loads a 128-bit floating-point vector of [2 x double] from an
///    unaligned memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVUPD / MOVUPD </c> instruction.
///
/// \param __dp
///    A pointer to a 128-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \returns A 128-bit vector of [2 x double] containing the loaded values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_loadu_pd(double const *__dp) {
  struct __loadu_pd {
    __m128d_u __v;
  } __attribute__((__packed__, __may_alias__));
  return ((const struct __loadu_pd *)__dp)->__v;
}

/// Loads a 64-bit integer value to the low element of a 128-bit integer
///    vector and clears the upper element.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVQ / MOVQ </c> instruction.
///
/// \param __a
///    A pointer to a 64-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \returns A 128-bit vector of [2 x i64] containing the loaded value.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_loadu_si64(void const *__a) {
  struct __loadu_si64 {
    long long __v;
  } __attribute__((__packed__, __may_alias__));
  long long __u = ((const struct __loadu_si64 *)__a)->__v;
  return __extension__(__m128i)(__v2di){__u, 0LL};
}

/// Loads a 32-bit integer value to the low element of a 128-bit integer
///    vector and clears the upper element.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVD / MOVD </c> instruction.
///
/// \param __a
///    A pointer to a 32-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \returns A 128-bit vector of [4 x i32] containing the loaded value.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_loadu_si32(void const *__a) {
  struct __loadu_si32 {
    int __v;
  } __attribute__((__packed__, __may_alias__));
  int __u = ((const struct __loadu_si32 *)__a)->__v;
  return __extension__(__m128i)(__v4si){__u, 0, 0, 0};
}

/// Loads a 16-bit integer value to the low element of a 128-bit integer
///    vector and clears the upper element.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic does not correspond to a specific instruction.
///
/// \param __a
///    A pointer to a 16-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \returns A 128-bit vector of [8 x i16] containing the loaded value.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_loadu_si16(void const *__a) {
  struct __loadu_si16 {
    short __v;
  } __attribute__((__packed__, __may_alias__));
  short __u = ((const struct __loadu_si16 *)__a)->__v;
  return __extension__(__m128i)(__v8hi){__u, 0, 0, 0, 0, 0, 0, 0};
}

/// Loads a 64-bit double-precision value to the low element of a
///    128-bit integer vector and clears the upper element.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVSD / MOVSD </c> instruction.
///
/// \param __dp
///    A pointer to a memory location containing a double-precision value.
///    The address of the memory location does not have to be aligned.
/// \returns A 128-bit vector of [2 x double] containing the loaded value.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_load_sd(double const *__dp) {
  struct __mm_load_sd_struct {
    double __u;
  } __attribute__((__packed__, __may_alias__));
  double __u = ((const struct __mm_load_sd_struct *)__dp)->__u;
  return __extension__(__m128d){__u, 0};
}

/// Loads a double-precision value into the high-order bits of a 128-bit
///    vector of [2 x double]. The low-order bits are copied from the low-order
///    bits of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVHPD / MOVHPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. \n
///    Bits [63:0] are written to bits [63:0] of the result.
/// \param __dp
///    A pointer to a 64-bit memory location containing a double-precision
///    floating-point value that is loaded. The loaded value is written to bits
///    [127:64] of the result. The address of the memory location does not have
///    to be aligned.
/// \returns A 128-bit vector of [2 x double] containing the moved values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_loadh_pd(__m128d __a,
                                                          double const *__dp) {
  struct __mm_loadh_pd_struct {
    double __u;
  } __attribute__((__packed__, __may_alias__));
  double __u = ((const struct __mm_loadh_pd_struct *)__dp)->__u;
  return __extension__(__m128d){__a[0], __u};
}

/// Loads a double-precision value into the low-order bits of a 128-bit
///    vector of [2 x double]. The high-order bits are copied from the
///    high-order bits of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVLPD / MOVLPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. \n
///    Bits [127:64] are written to bits [127:64] of the result.
/// \param __dp
///    A pointer to a 64-bit memory location containing a double-precision
///    floating-point value that is loaded. The loaded value is written to bits
///    [63:0] of the result. The address of the memory location does not have to
///    be aligned.
/// \returns A 128-bit vector of [2 x double] containing the moved values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_loadl_pd(__m128d __a,
                                                          double const *__dp) {
  struct __mm_loadl_pd_struct {
    double __u;
  } __attribute__((__packed__, __may_alias__));
  double __u = ((const struct __mm_loadl_pd_struct *)__dp)->__u;
  return __extension__(__m128d){__u, __a[1]};
}

/// Constructs a 128-bit floating-point vector of [2 x double] with
///    unspecified content. This could be used as an argument to another
///    intrinsic function where the argument is required but the value is not
///    actually used.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \returns A 128-bit floating-point vector of [2 x double] with unspecified
///    content.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_undefined_pd(void) {
  return (__m128d)__builtin_ia32_undef128();
}

/// Constructs a 128-bit floating-point vector of [2 x double]. The lower
///    64 bits of the vector are initialized with the specified double-precision
///    floating-point value. The upper 64 bits are set to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVQ / MOVQ </c> instruction.
///
/// \param __w
///    A double-precision floating-point value used to initialize the lower 64
///    bits of the result.
/// \returns An initialized 128-bit floating-point vector of [2 x double]. The
///    lower 64 bits contain the value of the parameter. The upper 64 bits are
///    set to zero.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_set_sd(double __w) {
  return __extension__(__m128d){__w, 0.0};
}

/// Constructs a 128-bit floating-point vector of [2 x double], with each
///    of the two double-precision floating-point vector elements set to the
///    specified double-precision floating-point value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDDUP / MOVLHPS </c> instruction.
///
/// \param __w
///    A double-precision floating-point value used to initialize each vector
///    element of the result.
/// \returns An initialized 128-bit floating-point vector of [2 x double].
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_set1_pd(double __w) {
  return __extension__(__m128d){__w, __w};
}

/// Constructs a 128-bit floating-point vector of [2 x double], with each
///    of the two double-precision floating-point vector elements set to the
///    specified double-precision floating-point value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDDUP / MOVLHPS </c> instruction.
///
/// \param __w
///    A double-precision floating-point value used to initialize each vector
///    element of the result.
/// \returns An initialized 128-bit floating-point vector of [2 x double].
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_set_pd1(double __w) {
  return _mm_set1_pd(__w);
}

/// Constructs a 128-bit floating-point vector of [2 x double]
///    initialized with the specified double-precision floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKLPD / UNPCKLPD </c> instruction.
///
/// \param __w
///    A double-precision floating-point value used to initialize the upper 64
///    bits of the result.
/// \param __x
///    A double-precision floating-point value used to initialize the lower 64
///    bits of the result.
/// \returns An initialized 128-bit floating-point vector of [2 x double].
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_set_pd(double __w,
                                                        double __x) {
  return __extension__(__m128d){__x, __w};
}

/// Constructs a 128-bit floating-point vector of [2 x double],
///    initialized in reverse order with the specified double-precision
///    floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKLPD / UNPCKLPD </c> instruction.
///
/// \param __w
///    A double-precision floating-point value used to initialize the lower 64
///    bits of the result.
/// \param __x
///    A double-precision floating-point value used to initialize the upper 64
///    bits of the result.
/// \returns An initialized 128-bit floating-point vector of [2 x double].
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_setr_pd(double __w,
                                                         double __x) {
  return __extension__(__m128d){__w, __x};
}

/// Constructs a 128-bit floating-point vector of [2 x double]
///    initialized to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VXORPS / XORPS </c> instruction.
///
/// \returns An initialized 128-bit floating-point vector of [2 x double] with
///    all elements set to zero.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_setzero_pd(void) {
  return __extension__(__m128d){0.0, 0.0};
}

/// Constructs a 128-bit floating-point vector of [2 x double]. The lower
///    64 bits are set to the lower 64 bits of the second parameter. The upper
///    64 bits are set to the upper 64 bits of the first parameter.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBLENDPD / BLENDPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The upper 64 bits are written to the
///    upper 64 bits of the result.
/// \param __b
///    A 128-bit vector of [2 x double]. The lower 64 bits are written to the
///    lower 64 bits of the result.
/// \returns A 128-bit vector of [2 x double] containing the moved values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_move_sd(__m128d __a,
                                                         __m128d __b) {
  __a[0] = __b[0];
  return __a;
}

/// Stores the lower 64 bits of a 128-bit vector of [2 x double] to a
///    memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVSD / MOVSD </c> instruction.
///
/// \param __dp
///    A pointer to a 64-bit memory location.
/// \param __a
///    A 128-bit vector of [2 x double] containing the value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_store_sd(double *__dp,
                                                       __m128d __a) {
  struct __mm_store_sd_struct {
    double __u;
  } __attribute__((__packed__, __may_alias__));
  ((struct __mm_store_sd_struct *)__dp)->__u = __a[0];
}

/// Moves packed double-precision values from a 128-bit vector of
///    [2 x double] to a memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c>VMOVAPD / MOVAPS</c> instruction.
///
/// \param __dp
///    A pointer to an aligned memory location that can store two
///    double-precision values.
/// \param __a
///    A packed 128-bit vector of [2 x double] containing the values to be
///    moved.
static __inline__ void __DEFAULT_FN_ATTRS _mm_store_pd(double *__dp,
                                                       __m128d __a) {
  *(__m128d *)__dp = __a;
}

/// Moves the lower 64 bits of a 128-bit vector of [2 x double] twice to
///    the upper and lower 64 bits of a memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the
///   <c> VMOVDDUP + VMOVAPD / MOVLHPS + MOVAPS </c> instruction.
///
/// \param __dp
///    A pointer to a memory location that can store two double-precision
///    values.
/// \param __a
///    A 128-bit vector of [2 x double] whose lower 64 bits are copied to each
///    of the values in \a __dp.
static __inline__ void __DEFAULT_FN_ATTRS _mm_store1_pd(double *__dp,
                                                        __m128d __a) {
  __a = __builtin_shufflevector((__v2df)__a, (__v2df)__a, 0, 0);
  _mm_store_pd(__dp, __a);
}

/// Moves the lower 64 bits of a 128-bit vector of [2 x double] twice to
///    the upper and lower 64 bits of a memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the
///   <c> VMOVDDUP + VMOVAPD / MOVLHPS + MOVAPS </c> instruction.
///
/// \param __dp
///    A pointer to a memory location that can store two double-precision
///    values.
/// \param __a
///    A 128-bit vector of [2 x double] whose lower 64 bits are copied to each
///    of the values in \a __dp.
static __inline__ void __DEFAULT_FN_ATTRS _mm_store_pd1(double *__dp,
                                                        __m128d __a) {
  _mm_store1_pd(__dp, __a);
}

/// Stores a 128-bit vector of [2 x double] into an unaligned memory
///    location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVUPD / MOVUPD </c> instruction.
///
/// \param __dp
///    A pointer to a 128-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \param __a
///    A 128-bit vector of [2 x double] containing the values to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_storeu_pd(double *__dp,
                                                        __m128d __a) {
  struct __storeu_pd {
    __m128d_u __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_pd *)__dp)->__v = __a;
}

/// Stores two double-precision values, in reverse order, from a 128-bit
///    vector of [2 x double] to a 16-byte aligned memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to a shuffling instruction followed by a
/// <c> VMOVAPD / MOVAPD </c> instruction.
///
/// \param __dp
///    A pointer to a 16-byte aligned memory location that can store two
///    double-precision values.
/// \param __a
///    A 128-bit vector of [2 x double] containing the values to be reversed and
///    stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_storer_pd(double *__dp,
                                                        __m128d __a) {
  __a = __builtin_shufflevector((__v2df)__a, (__v2df)__a, 1, 0);
  *(__m128d *)__dp = __a;
}

/// Stores the upper 64 bits of a 128-bit vector of [2 x double] to a
///    memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVHPD / MOVHPD </c> instruction.
///
/// \param __dp
///    A pointer to a 64-bit memory location.
/// \param __a
///    A 128-bit vector of [2 x double] containing the value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_storeh_pd(double *__dp,
                                                        __m128d __a) {
  struct __mm_storeh_pd_struct {
    double __u;
  } __attribute__((__packed__, __may_alias__));
  ((struct __mm_storeh_pd_struct *)__dp)->__u = __a[1];
}

/// Stores the lower 64 bits of a 128-bit vector of [2 x double] to a
///    memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVLPD / MOVLPD </c> instruction.
///
/// \param __dp
///    A pointer to a 64-bit memory location.
/// \param __a
///    A 128-bit vector of [2 x double] containing the value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_storel_pd(double *__dp,
                                                        __m128d __a) {
  struct __mm_storeh_pd_struct {
    double __u;
  } __attribute__((__packed__, __may_alias__));
  ((struct __mm_storeh_pd_struct *)__dp)->__u = __a[0];
}

/// Adds the corresponding elements of two 128-bit vectors of [16 x i8],
///    saving the lower 8 bits of each sum in the corresponding element of a
///    128-bit result vector of [16 x i8].
///
///    The integer elements of both parameters can be either signed or unsigned.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPADDB / PADDB </c> instruction.
///
/// \param __a
///    A 128-bit vector of [16 x i8].
/// \param __b
///    A 128-bit vector of [16 x i8].
/// \returns A 128-bit vector of [16 x i8] containing the sums of both
///    parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_add_epi8(__m128i __a,
                                                          __m128i __b) {
  return (__m128i)((__v16qu)__a + (__v16qu)__b);
}

/// Adds the corresponding elements of two 128-bit vectors of [8 x i16],
///    saving the lower 16 bits of each sum in the corresponding element of a
///    128-bit result vector of [8 x i16].
///
///    The integer elements of both parameters can be either signed or unsigned.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPADDW / PADDW </c> instruction.
///
/// \param __a
///    A 128-bit vector of [8 x i16].
/// \param __b
///    A 128-bit vector of [8 x i16].
/// \returns A 128-bit vector of [8 x i16] containing the sums of both
///    parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_add_epi16(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)((__v8hu)__a + (__v8hu)__b);
}

/// Adds the corresponding elements of two 128-bit vectors of [4 x i32],
///    saving the lower 32 bits of each sum in the corresponding element of a
///    128-bit result vector of [4 x i32].
///
///    The integer elements of both parameters can be either signed or unsigned.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPADDD / PADDD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x i32].
/// \param __b
///    A 128-bit vector of [4 x i32].
/// \returns A 128-bit vector of [4 x i32] containing the sums of both
///    parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_add_epi32(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)((__v4su)__a + (__v4su)__b);
}

/// Adds two signed or unsigned 64-bit integer values, returning the
///    lower 64 bits of the sum.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PADDQ </c> instruction.
///
/// \param __a
///    A 64-bit integer.
/// \param __b
///    A 64-bit integer.
/// \returns A 64-bit integer containing the sum of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX _mm_add_si64(__m64 __a,
                                                            __m64 __b) {
  return (__m64)__builtin_ia32_paddq((__v1di)__a, (__v1di)__b);
}

/// Adds the corresponding elements of two 128-bit vectors of [2 x i64],
///    saving the lower 64 bits of each sum in the corresponding element of a
///    128-bit result vector of [2 x i64].
///
///    The integer elements of both parameters can be either signed or unsigned.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPADDQ / PADDQ </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x i64].
/// \param __b
///    A 128-bit vector of [2 x i64].
/// \returns A 128-bit vector of [2 x i64] containing the sums of both
///    parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_add_epi64(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)((__v2du)__a + (__v2du)__b);
}

/// Adds, with saturation, the corresponding elements of two 128-bit
///    signed [16 x i8] vectors, saving each sum in the corresponding element
///    of a 128-bit result vector of [16 x i8].
///
///    Positive sums greater than 0x7F are saturated to 0x7F. Negative sums
///    less than 0x80 are saturated to 0x80.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPADDSB / PADDSB </c> instruction.
///
/// \param __a
///    A 128-bit signed [16 x i8] vector.
/// \param __b
///    A 128-bit signed [16 x i8] vector.
/// \returns A 128-bit signed [16 x i8] vector containing the saturated sums of
///    both parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_adds_epi8(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)__builtin_elementwise_add_sat((__v16qs)__a, (__v16qs)__b);
}

/// Adds, with saturation, the corresponding elements of two 128-bit
///    signed [8 x i16] vectors, saving each sum in the corresponding element
///    of a 128-bit result vector of [8 x i16].
///
///    Positive sums greater than 0x7FFF are saturated to 0x7FFF. Negative sums
///    less than 0x8000 are saturated to 0x8000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPADDSW / PADDSW </c> instruction.
///
/// \param __a
///    A 128-bit signed [8 x i16] vector.
/// \param __b
///    A 128-bit signed [8 x i16] vector.
/// \returns A 128-bit signed [8 x i16] vector containing the saturated sums of
///    both parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_adds_epi16(__m128i __a,
                                                            __m128i __b) {
  return (__m128i)__builtin_elementwise_add_sat((__v8hi)__a, (__v8hi)__b);
}

/// Adds, with saturation, the corresponding elements of two 128-bit
///    unsigned [16 x i8] vectors, saving each sum in the corresponding element
///    of a 128-bit result vector of [16 x i8].
///
///    Positive sums greater than 0xFF are saturated to 0xFF. Negative sums are
///    saturated to 0x00.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPADDUSB / PADDUSB </c> instruction.
///
/// \param __a
///    A 128-bit unsigned [16 x i8] vector.
/// \param __b
///    A 128-bit unsigned [16 x i8] vector.
/// \returns A 128-bit unsigned [16 x i8] vector containing the saturated sums
///    of both parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_adds_epu8(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)__builtin_elementwise_add_sat((__v16qu)__a, (__v16qu)__b);
}

/// Adds, with saturation, the corresponding elements of two 128-bit
///    unsigned [8 x i16] vectors, saving each sum in the corresponding element
///    of a 128-bit result vector of [8 x i16].
///
///    Positive sums greater than 0xFFFF are saturated to 0xFFFF. Negative sums
///    are saturated to 0x0000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPADDUSB / PADDUSB </c> instruction.
///
/// \param __a
///    A 128-bit unsigned [8 x i16] vector.
/// \param __b
///    A 128-bit unsigned [8 x i16] vector.
/// \returns A 128-bit unsigned [8 x i16] vector containing the saturated sums
///    of both parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_adds_epu16(__m128i __a,
                                                            __m128i __b) {
  return (__m128i)__builtin_elementwise_add_sat((__v8hu)__a, (__v8hu)__b);
}

/// Computes the rounded averages of corresponding elements of two
///    128-bit unsigned [16 x i8] vectors, saving each result in the
///    corresponding element of a 128-bit result vector of [16 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPAVGB / PAVGB </c> instruction.
///
/// \param __a
///    A 128-bit unsigned [16 x i8] vector.
/// \param __b
///    A 128-bit unsigned [16 x i8] vector.
/// \returns A 128-bit unsigned [16 x i8] vector containing the rounded
///    averages of both parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_avg_epu8(__m128i __a,
                                                          __m128i __b) {
  return (__m128i)__builtin_ia32_pavgb128((__v16qi)__a, (__v16qi)__b);
}

/// Computes the rounded averages of corresponding elements of two
///    128-bit unsigned [8 x i16] vectors, saving each result in the
///    corresponding element of a 128-bit result vector of [8 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPAVGW / PAVGW </c> instruction.
///
/// \param __a
///    A 128-bit unsigned [8 x i16] vector.
/// \param __b
///    A 128-bit unsigned [8 x i16] vector.
/// \returns A 128-bit unsigned [8 x i16] vector containing the rounded
///    averages of both parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_avg_epu16(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)__builtin_ia32_pavgw128((__v8hi)__a, (__v8hi)__b);
}

/// Multiplies the corresponding elements of two 128-bit signed [8 x i16]
///    vectors, producing eight intermediate 32-bit signed integer products, and
///    adds the consecutive pairs of 32-bit products to form a 128-bit signed
///    [4 x i32] vector.
///
///    For example, bits [15:0] of both parameters are multiplied producing a
///    32-bit product, bits [31:16] of both parameters are multiplied producing
///    a 32-bit product, and the sum of those two products becomes bits [31:0]
///    of the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMADDWD / PMADDWD </c> instruction.
///
/// \param __a
///    A 128-bit signed [8 x i16] vector.
/// \param __b
///    A 128-bit signed [8 x i16] vector.
/// \returns A 128-bit signed [4 x i32] vector containing the sums of products
///    of both parameters.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_madd_epi16(__m128i __a,
                                                            __m128i __b) {
  return (__m128i)__builtin_ia32_pmaddwd128((__v8hi)__a, (__v8hi)__b);
}

/// Compares corresponding elements of two 128-bit signed [8 x i16]
///    vectors, saving the greater value from each comparison in the
///    corresponding element of a 128-bit result vector of [8 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMAXSW / PMAXSW </c> instruction.
///
/// \param __a
///    A 128-bit signed [8 x i16] vector.
/// \param __b
///    A 128-bit signed [8 x i16] vector.
/// \returns A 128-bit signed [8 x i16] vector containing the greater value of
///    each comparison.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_max_epi16(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)__builtin_elementwise_max((__v8hi)__a, (__v8hi)__b);
}

/// Compares corresponding elements of two 128-bit unsigned [16 x i8]
///    vectors, saving the greater value from each comparison in the
///    corresponding element of a 128-bit result vector of [16 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMAXUB / PMAXUB </c> instruction.
///
/// \param __a
///    A 128-bit unsigned [16 x i8] vector.
/// \param __b
///    A 128-bit unsigned [16 x i8] vector.
/// \returns A 128-bit unsigned [16 x i8] vector containing the greater value of
///    each comparison.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_max_epu8(__m128i __a,
                                                          __m128i __b) {
  return (__m128i)__builtin_elementwise_max((__v16qu)__a, (__v16qu)__b);
}

/// Compares corresponding elements of two 128-bit signed [8 x i16]
///    vectors, saving the smaller value from each comparison in the
///    corresponding element of a 128-bit result vector of [8 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMINSW / PMINSW </c> instruction.
///
/// \param __a
///    A 128-bit signed [8 x i16] vector.
/// \param __b
///    A 128-bit signed [8 x i16] vector.
/// \returns A 128-bit signed [8 x i16] vector containing the smaller value of
///    each comparison.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_min_epi16(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)__builtin_elementwise_min((__v8hi)__a, (__v8hi)__b);
}

/// Compares corresponding elements of two 128-bit unsigned [16 x i8]
///    vectors, saving the smaller value from each comparison in the
///    corresponding element of a 128-bit result vector of [16 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMINUB / PMINUB </c> instruction.
///
/// \param __a
///    A 128-bit unsigned [16 x i8] vector.
/// \param __b
///    A 128-bit unsigned [16 x i8] vector.
/// \returns A 128-bit unsigned [16 x i8] vector containing the smaller value of
///    each comparison.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_min_epu8(__m128i __a,
                                                          __m128i __b) {
  return (__m128i)__builtin_elementwise_min((__v16qu)__a, (__v16qu)__b);
}

/// Multiplies the corresponding elements of two signed [8 x i16]
///    vectors, saving the upper 16 bits of each 32-bit product in the
///    corresponding element of a 128-bit signed [8 x i16] result vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMULHW / PMULHW </c> instruction.
///
/// \param __a
///    A 128-bit signed [8 x i16] vector.
/// \param __b
///    A 128-bit signed [8 x i16] vector.
/// \returns A 128-bit signed [8 x i16] vector containing the upper 16 bits of
///    each of the eight 32-bit products.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_mulhi_epi16(__m128i __a,
                                                             __m128i __b) {
  return (__m128i)__builtin_ia32_pmulhw128((__v8hi)__a, (__v8hi)__b);
}

/// Multiplies the corresponding elements of two unsigned [8 x i16]
///    vectors, saving the upper 16 bits of each 32-bit product in the
///    corresponding element of a 128-bit unsigned [8 x i16] result vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMULHUW / PMULHUW </c> instruction.
///
/// \param __a
///    A 128-bit unsigned [8 x i16] vector.
/// \param __b
///    A 128-bit unsigned [8 x i16] vector.
/// \returns A 128-bit unsigned [8 x i16] vector containing the upper 16 bits
///    of each of the eight 32-bit products.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_mulhi_epu16(__m128i __a,
                                                             __m128i __b) {
  return (__m128i)__builtin_ia32_pmulhuw128((__v8hi)__a, (__v8hi)__b);
}

/// Multiplies the corresponding elements of two signed [8 x i16]
///    vectors, saving the lower 16 bits of each 32-bit product in the
///    corresponding element of a 128-bit signed [8 x i16] result vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMULLW / PMULLW </c> instruction.
///
/// \param __a
///    A 128-bit signed [8 x i16] vector.
/// \param __b
///    A 128-bit signed [8 x i16] vector.
/// \returns A 128-bit signed [8 x i16] vector containing the lower 16 bits of
///    each of the eight 32-bit products.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_mullo_epi16(__m128i __a,
                                                             __m128i __b) {
  return (__m128i)((__v8hu)__a * (__v8hu)__b);
}

/// Multiplies 32-bit unsigned integer values contained in the lower bits
///    of the two 64-bit integer vectors and returns the 64-bit unsigned
///    product.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMULUDQ </c> instruction.
///
/// \param __a
///    A 64-bit integer containing one of the source operands.
/// \param __b
///    A 64-bit integer containing one of the source operands.
/// \returns A 64-bit integer vector containing the product of both operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX _mm_mul_su32(__m64 __a,
                                                            __m64 __b) {
  return __builtin_ia32_pmuludq((__v2si)__a, (__v2si)__b);
}

/// Multiplies 32-bit unsigned integer values contained in the lower
///    bits of the corresponding elements of two [2 x i64] vectors, and returns
///    the 64-bit products in the corresponding elements of a [2 x i64] vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMULUDQ / PMULUDQ </c> instruction.
///
/// \param __a
///    A [2 x i64] vector containing one of the source operands.
/// \param __b
///    A [2 x i64] vector containing one of the source operands.
/// \returns A [2 x i64] vector containing the product of both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_mul_epu32(__m128i __a,
                                                           __m128i __b) {
  return __builtin_ia32_pmuludq128((__v4si)__a, (__v4si)__b);
}

/// Computes the absolute differences of corresponding 8-bit integer
///    values in two 128-bit vectors. Sums the first 8 absolute differences, and
///    separately sums the second 8 absolute differences. Packs these two
///    unsigned 16-bit integer sums into the upper and lower elements of a
///    [2 x i64] vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSADBW / PSADBW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing one of the source operands.
/// \param __b
///    A 128-bit integer vector containing one of the source operands.
/// \returns A [2 x i64] vector containing the sums of the sets of absolute
///    differences between both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sad_epu8(__m128i __a,
                                                          __m128i __b) {
  return __builtin_ia32_psadbw128((__v16qi)__a, (__v16qi)__b);
}

/// Subtracts the corresponding 8-bit integer values in the operands.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSUBB / PSUBB </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the minuends.
/// \param __b
///    A 128-bit integer vector containing the subtrahends.
/// \returns A 128-bit integer vector containing the differences of the values
///    in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sub_epi8(__m128i __a,
                                                          __m128i __b) {
  return (__m128i)((__v16qu)__a - (__v16qu)__b);
}

/// Subtracts the corresponding 16-bit integer values in the operands.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSUBW / PSUBW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the minuends.
/// \param __b
///    A 128-bit integer vector containing the subtrahends.
/// \returns A 128-bit integer vector containing the differences of the values
///    in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sub_epi16(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)((__v8hu)__a - (__v8hu)__b);
}

/// Subtracts the corresponding 32-bit integer values in the operands.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSUBD / PSUBD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the minuends.
/// \param __b
///    A 128-bit integer vector containing the subtrahends.
/// \returns A 128-bit integer vector containing the differences of the values
///    in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sub_epi32(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)((__v4su)__a - (__v4su)__b);
}

/// Subtracts signed or unsigned 64-bit integer values and writes the
///    difference to the corresponding bits in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSUBQ </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing the minuend.
/// \param __b
///    A 64-bit integer vector containing the subtrahend.
/// \returns A 64-bit integer vector containing the difference of the values in
///    the operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX _mm_sub_si64(__m64 __a,
                                                            __m64 __b) {
  return (__m64)__builtin_ia32_psubq((__v1di)__a, (__v1di)__b);
}

/// Subtracts the corresponding elements of two [2 x i64] vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSUBQ / PSUBQ </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the minuends.
/// \param __b
///    A 128-bit integer vector containing the subtrahends.
/// \returns A 128-bit integer vector containing the differences of the values
///    in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sub_epi64(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)((__v2du)__a - (__v2du)__b);
}

/// Subtracts, with saturation, corresponding 8-bit signed integer values in
///    the input and returns the differences in the corresponding bytes in the
///    destination.
///
///    Differences greater than 0x7F are saturated to 0x7F, and differences
///    less than 0x80 are saturated to 0x80.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSUBSB / PSUBSB </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the minuends.
/// \param __b
///    A 128-bit integer vector containing the subtrahends.
/// \returns A 128-bit integer vector containing the differences of the values
///    in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_subs_epi8(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)__builtin_elementwise_sub_sat((__v16qs)__a, (__v16qs)__b);
}

/// Subtracts, with saturation, corresponding 16-bit signed integer values in
///    the input and returns the differences in the corresponding bytes in the
///    destination.
///
///    Differences greater than 0x7FFF are saturated to 0x7FFF, and values less
///    than 0x8000 are saturated to 0x8000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSUBSW / PSUBSW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the minuends.
/// \param __b
///    A 128-bit integer vector containing the subtrahends.
/// \returns A 128-bit integer vector containing the differences of the values
///    in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_subs_epi16(__m128i __a,
                                                            __m128i __b) {
  return (__m128i)__builtin_elementwise_sub_sat((__v8hi)__a, (__v8hi)__b);
}

/// Subtracts, with saturation, corresponding 8-bit unsigned integer values in
///    the input and returns the differences in the corresponding bytes in the
///    destination.
///
///    Differences less than 0x00 are saturated to 0x00.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSUBUSB / PSUBUSB </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the minuends.
/// \param __b
///    A 128-bit integer vector containing the subtrahends.
/// \returns A 128-bit integer vector containing the unsigned integer
///    differences of the values in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_subs_epu8(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)__builtin_elementwise_sub_sat((__v16qu)__a, (__v16qu)__b);
}

/// Subtracts, with saturation, corresponding 16-bit unsigned integer values in
///    the input and returns the differences in the corresponding bytes in the
///    destination.
///
///    Differences less than 0x0000 are saturated to 0x0000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSUBUSW / PSUBUSW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the minuends.
/// \param __b
///    A 128-bit integer vector containing the subtrahends.
/// \returns A 128-bit integer vector containing the unsigned integer
///    differences of the values in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_subs_epu16(__m128i __a,
                                                            __m128i __b) {
  return (__m128i)__builtin_elementwise_sub_sat((__v8hu)__a, (__v8hu)__b);
}

/// Performs a bitwise AND of two 128-bit integer vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPAND / PAND </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing one of the source operands.
/// \param __b
///    A 128-bit integer vector containing one of the source operands.
/// \returns A 128-bit integer vector containing the bitwise AND of the values
///    in both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_and_si128(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)((__v2du)__a & (__v2du)__b);
}

/// Performs a bitwise AND of two 128-bit integer vectors, using the
///    one's complement of the values contained in the first source operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPANDN / PANDN </c> instruction.
///
/// \param __a
///    A 128-bit vector containing the left source operand. The one's complement
///    of this value is used in the bitwise AND.
/// \param __b
///    A 128-bit vector containing the right source operand.
/// \returns A 128-bit integer vector containing the bitwise AND of the one's
///    complement of the first operand and the values in the second operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_andnot_si128(__m128i __a,
                                                              __m128i __b) {
  return (__m128i)(~(__v2du)__a & (__v2du)__b);
}
/// Performs a bitwise OR of two 128-bit integer vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPOR / POR </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing one of the source operands.
/// \param __b
///    A 128-bit integer vector containing one of the source operands.
/// \returns A 128-bit integer vector containing the bitwise OR of the values
///    in both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_or_si128(__m128i __a,
                                                          __m128i __b) {
  return (__m128i)((__v2du)__a | (__v2du)__b);
}

/// Performs a bitwise exclusive OR of two 128-bit integer vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPXOR / PXOR </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing one of the source operands.
/// \param __b
///    A 128-bit integer vector containing one of the source operands.
/// \returns A 128-bit integer vector containing the bitwise exclusive OR of the
///    values in both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_xor_si128(__m128i __a,
                                                           __m128i __b) {
  return (__m128i)((__v2du)__a ^ (__v2du)__b);
}

/// Left-shifts the 128-bit integer vector operand by the specified
///    number of bytes. Low-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_slli_si128(__m128i a, const int imm);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPSLLDQ / PSLLDQ </c> instruction.
///
/// \param a
///    A 128-bit integer vector containing the source operand.
/// \param imm
///    An immediate value specifying the number of bytes to left-shift operand
///    \a a.
/// \returns A 128-bit integer vector containing the left-shifted value.
#define _mm_slli_si128(a, imm)                                                 \
  ((__m128i)__builtin_ia32_pslldqi128_byteshift((__v2di)(__m128i)(a),          \
                                                (int)(imm)))

#define _mm_bslli_si128(a, imm)                                                \
  ((__m128i)__builtin_ia32_pslldqi128_byteshift((__v2di)(__m128i)(a),          \
                                                (int)(imm)))

/// Left-shifts each 16-bit value in the 128-bit integer vector operand
///    by the specified number of bits. Low-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSLLW / PSLLW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    An integer value specifying the number of bits to left-shift each value
///    in operand \a __a.
/// \returns A 128-bit integer vector containing the left-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_slli_epi16(__m128i __a,
                                                            int __count) {
  return (__m128i)__builtin_ia32_psllwi128((__v8hi)__a, __count);
}

/// Left-shifts each 16-bit value in the 128-bit integer vector operand
///    by the specified number of bits. Low-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSLLW / PSLLW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    A 128-bit integer vector in which bits [63:0] specify the number of bits
///    to left-shift each value in operand \a __a.
/// \returns A 128-bit integer vector containing the left-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sll_epi16(__m128i __a,
                                                           __m128i __count) {
  return (__m128i)__builtin_ia32_psllw128((__v8hi)__a, (__v8hi)__count);
}

/// Left-shifts each 32-bit value in the 128-bit integer vector operand
///    by the specified number of bits. Low-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSLLD / PSLLD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    An integer value specifying the number of bits to left-shift each value
///    in operand \a __a.
/// \returns A 128-bit integer vector containing the left-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_slli_epi32(__m128i __a,
                                                            int __count) {
  return (__m128i)__builtin_ia32_pslldi128((__v4si)__a, __count);
}

/// Left-shifts each 32-bit value in the 128-bit integer vector operand
///    by the specified number of bits. Low-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSLLD / PSLLD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    A 128-bit integer vector in which bits [63:0] specify the number of bits
///    to left-shift each value in operand \a __a.
/// \returns A 128-bit integer vector containing the left-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sll_epi32(__m128i __a,
                                                           __m128i __count) {
  return (__m128i)__builtin_ia32_pslld128((__v4si)__a, (__v4si)__count);
}

/// Left-shifts each 64-bit value in the 128-bit integer vector operand
///    by the specified number of bits. Low-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSLLQ / PSLLQ </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    An integer value specifying the number of bits to left-shift each value
///    in operand \a __a.
/// \returns A 128-bit integer vector containing the left-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_slli_epi64(__m128i __a,
                                                            int __count) {
  return __builtin_ia32_psllqi128((__v2di)__a, __count);
}

/// Left-shifts each 64-bit value in the 128-bit integer vector operand
///    by the specified number of bits. Low-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSLLQ / PSLLQ </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    A 128-bit integer vector in which bits [63:0] specify the number of bits
///    to left-shift each value in operand \a __a.
/// \returns A 128-bit integer vector containing the left-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sll_epi64(__m128i __a,
                                                           __m128i __count) {
  return __builtin_ia32_psllq128((__v2di)__a, (__v2di)__count);
}

/// Right-shifts each 16-bit value in the 128-bit integer vector operand
///    by the specified number of bits. High-order bits are filled with the sign
///    bit of the initial value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRAW / PSRAW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    An integer value specifying the number of bits to right-shift each value
///    in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_srai_epi16(__m128i __a,
                                                            int __count) {
  return (__m128i)__builtin_ia32_psrawi128((__v8hi)__a, __count);
}

/// Right-shifts each 16-bit value in the 128-bit integer vector operand
///    by the specified number of bits. High-order bits are filled with the sign
///    bit of the initial value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRAW / PSRAW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    A 128-bit integer vector in which bits [63:0] specify the number of bits
///    to right-shift each value in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sra_epi16(__m128i __a,
                                                           __m128i __count) {
  return (__m128i)__builtin_ia32_psraw128((__v8hi)__a, (__v8hi)__count);
}

/// Right-shifts each 32-bit value in the 128-bit integer vector operand
///    by the specified number of bits. High-order bits are filled with the sign
///    bit of the initial value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRAD / PSRAD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    An integer value specifying the number of bits to right-shift each value
///    in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_srai_epi32(__m128i __a,
                                                            int __count) {
  return (__m128i)__builtin_ia32_psradi128((__v4si)__a, __count);
}

/// Right-shifts each 32-bit value in the 128-bit integer vector operand
///    by the specified number of bits. High-order bits are filled with the sign
///    bit of the initial value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRAD / PSRAD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    A 128-bit integer vector in which bits [63:0] specify the number of bits
///    to right-shift each value in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_sra_epi32(__m128i __a,
                                                           __m128i __count) {
  return (__m128i)__builtin_ia32_psrad128((__v4si)__a, (__v4si)__count);
}

/// Right-shifts the 128-bit integer vector operand by the specified
///    number of bytes. High-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_srli_si128(__m128i a, const int imm);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPSRLDQ / PSRLDQ </c> instruction.
///
/// \param a
///    A 128-bit integer vector containing the source operand.
/// \param imm
///    An immediate value specifying the number of bytes to right-shift operand
///    \a a.
/// \returns A 128-bit integer vector containing the right-shifted value.
#define _mm_srli_si128(a, imm)                                                 \
  ((__m128i)__builtin_ia32_psrldqi128_byteshift((__v2di)(__m128i)(a),          \
                                                (int)(imm)))

#define _mm_bsrli_si128(a, imm)                                                \
  ((__m128i)__builtin_ia32_psrldqi128_byteshift((__v2di)(__m128i)(a),          \
                                                (int)(imm)))

/// Right-shifts each of 16-bit values in the 128-bit integer vector
///    operand by the specified number of bits. High-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRLW / PSRLW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    An integer value specifying the number of bits to right-shift each value
///    in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_srli_epi16(__m128i __a,
                                                            int __count) {
  return (__m128i)__builtin_ia32_psrlwi128((__v8hi)__a, __count);
}

/// Right-shifts each of 16-bit values in the 128-bit integer vector
///    operand by the specified number of bits. High-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRLW / PSRLW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    A 128-bit integer vector in which bits [63:0] specify the number of bits
///    to right-shift each value in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_srl_epi16(__m128i __a,
                                                           __m128i __count) {
  return (__m128i)__builtin_ia32_psrlw128((__v8hi)__a, (__v8hi)__count);
}

/// Right-shifts each of 32-bit values in the 128-bit integer vector
///    operand by the specified number of bits. High-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRLD / PSRLD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    An integer value specifying the number of bits to right-shift each value
///    in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_srli_epi32(__m128i __a,
                                                            int __count) {
  return (__m128i)__builtin_ia32_psrldi128((__v4si)__a, __count);
}

/// Right-shifts each of 32-bit values in the 128-bit integer vector
///    operand by the specified number of bits. High-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRLD / PSRLD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    A 128-bit integer vector in which bits [63:0] specify the number of bits
///    to right-shift each value in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_srl_epi32(__m128i __a,
                                                           __m128i __count) {
  return (__m128i)__builtin_ia32_psrld128((__v4si)__a, (__v4si)__count);
}

/// Right-shifts each of 64-bit values in the 128-bit integer vector
///    operand by the specified number of bits. High-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRLQ / PSRLQ </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    An integer value specifying the number of bits to right-shift each value
///    in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_srli_epi64(__m128i __a,
                                                            int __count) {
  return __builtin_ia32_psrlqi128((__v2di)__a, __count);
}

/// Right-shifts each of 64-bit values in the 128-bit integer vector
///    operand by the specified number of bits. High-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSRLQ / PSRLQ </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the source operand.
/// \param __count
///    A 128-bit integer vector in which bits [63:0] specify the number of bits
///    to right-shift each value in operand \a __a.
/// \returns A 128-bit integer vector containing the right-shifted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_srl_epi64(__m128i __a,
                                                           __m128i __count) {
  return __builtin_ia32_psrlq128((__v2di)__a, (__v2di)__count);
}

/// Compares each of the corresponding 8-bit values of the 128-bit
///    integer vectors for equality.
///
///    Each comparison returns 0x0 for false, 0xFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPEQB / PCMPEQB </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \param __b
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cmpeq_epi8(__m128i __a,
                                                            __m128i __b) {
  return (__m128i)((__v16qi)__a == (__v16qi)__b);
}

/// Compares each of the corresponding 16-bit values of the 128-bit
///    integer vectors for equality.
///
///    Each comparison returns 0x0 for false, 0xFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPEQW / PCMPEQW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \param __b
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cmpeq_epi16(__m128i __a,
                                                             __m128i __b) {
  return (__m128i)((__v8hi)__a == (__v8hi)__b);
}

/// Compares each of the corresponding 32-bit values of the 128-bit
///    integer vectors for equality.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPEQD / PCMPEQD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \param __b
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cmpeq_epi32(__m128i __a,
                                                             __m128i __b) {
  return (__m128i)((__v4si)__a == (__v4si)__b);
}

/// Compares each of the corresponding signed 8-bit values of the 128-bit
///    integer vectors to determine if the values in the first operand are
///    greater than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPGTB / PCMPGTB </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \param __b
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cmpgt_epi8(__m128i __a,
                                                            __m128i __b) {
  /* This function always performs a signed comparison, but __v16qi is a char
     which may be signed or unsigned, so use __v16qs. */
  return (__m128i)((__v16qs)__a > (__v16qs)__b);
}

/// Compares each of the corresponding signed 16-bit values of the
///    128-bit integer vectors to determine if the values in the first operand
///    are greater than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPGTW / PCMPGTW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \param __b
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cmpgt_epi16(__m128i __a,
                                                             __m128i __b) {
  return (__m128i)((__v8hi)__a > (__v8hi)__b);
}

/// Compares each of the corresponding signed 32-bit values of the
///    128-bit integer vectors to determine if the values in the first operand
///    are greater than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPGTD / PCMPGTD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \param __b
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cmpgt_epi32(__m128i __a,
                                                             __m128i __b) {
  return (__m128i)((__v4si)__a > (__v4si)__b);
}

/// Compares each of the corresponding signed 8-bit values of the 128-bit
///    integer vectors to determine if the values in the first operand are less
///    than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPGTB / PCMPGTB </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \param __b
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cmplt_epi8(__m128i __a,
                                                            __m128i __b) {
  return _mm_cmpgt_epi8(__b, __a);
}

/// Compares each of the corresponding signed 16-bit values of the
///    128-bit integer vectors to determine if the values in the first operand
///    are less than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPGTW / PCMPGTW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \param __b
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cmplt_epi16(__m128i __a,
                                                             __m128i __b) {
  return _mm_cmpgt_epi16(__b, __a);
}

/// Compares each of the corresponding signed 32-bit values of the
///    128-bit integer vectors to determine if the values in the first operand
///    are less than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPGTD / PCMPGTD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \param __b
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cmplt_epi32(__m128i __a,
                                                             __m128i __b) {
  return _mm_cmpgt_epi32(__b, __a);
}

#ifdef __x86_64__
/// Converts a 64-bit signed integer value from the second operand into a
///    double-precision value and returns it in the lower element of a [2 x
///    double] vector; the upper element of the returned vector is copied from
///    the upper element of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSI2SD / CVTSI2SD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The upper 64 bits of this operand are
///    copied to the upper 64 bits of the destination.
/// \param __b
///    A 64-bit signed integer operand containing the value to be converted.
/// \returns A 128-bit vector of [2 x double] whose lower 64 bits contain the
///    converted value of the second operand. The upper 64 bits are copied from
///    the upper 64 bits of the first operand.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_cvtsi64_sd(__m128d __a,
                                                            long long __b) {
  __a[0] = __b;
  return __a;
}

/// Converts the first (lower) element of a vector of [2 x double] into a
///    64-bit signed integer value.
///
///    If the converted value does not fit in a 64-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSD2SI / CVTSD2SI </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower 64 bits are used in the
///    conversion.
/// \returns A 64-bit signed integer containing the converted value.
static __inline__ long long __DEFAULT_FN_ATTRS _mm_cvtsd_si64(__m128d __a) {
  return __builtin_ia32_cvtsd2si64((__v2df)__a);
}

/// Converts the first (lower) element of a vector of [2 x double] into a
///    64-bit signed truncated (rounded toward zero) integer value.
///
///    If a converted value does not fit in a 64-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTTSD2SI / CVTTSD2SI </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. The lower 64 bits are used in the
///    conversion.
/// \returns A 64-bit signed integer containing the converted value.
static __inline__ long long __DEFAULT_FN_ATTRS _mm_cvttsd_si64(__m128d __a) {
  return __builtin_ia32_cvttsd2si64((__v2df)__a);
}
#endif

/// Converts a vector of [4 x i32] into a vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTDQ2PS / CVTDQ2PS </c> instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \returns A 128-bit vector of [4 x float] containing the converted values.
static __inline__ __m128 __DEFAULT_FN_ATTRS _mm_cvtepi32_ps(__m128i __a) {
  return (__m128) __builtin_convertvector((__v4si)__a, __v4sf);
}

/// Converts a vector of [4 x float] into a vector of [4 x i32].
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTPS2DQ / CVTPS2DQ </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit integer vector of [4 x i32] containing the converted
///    values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cvtps_epi32(__m128 __a) {
  return (__m128i)__builtin_ia32_cvtps2dq((__v4sf)__a);
}

/// Converts a vector of [4 x float] into four signed truncated (rounded toward
///    zero) 32-bit integers, returned in a vector of [4 x i32].
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTTPS2DQ / CVTTPS2DQ </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x i32] containing the converted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cvttps_epi32(__m128 __a) {
  return (__m128i)__builtin_ia32_cvttps2dq((__v4sf)__a);
}

/// Returns a vector of [4 x i32] where the lowest element is the input
///    operand and the remaining elements are zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVD / MOVD </c> instruction.
///
/// \param __a
///    A 32-bit signed integer operand.
/// \returns A 128-bit vector of [4 x i32].
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cvtsi32_si128(int __a) {
  return __extension__(__m128i)(__v4si){__a, 0, 0, 0};
}

/// Returns a vector of [2 x i64] where the lower element is the input
///    operand and the upper element is zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVQ / MOVQ </c> instruction
/// in 64-bit mode.
///
/// \param __a
///    A 64-bit signed integer operand containing the value to be converted.
/// \returns A 128-bit vector of [2 x i64] containing the converted value.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_cvtsi64_si128(long long __a) {
  return __extension__(__m128i)(__v2di){__a, 0};
}

/// Moves the least significant 32 bits of a vector of [4 x i32] to a
///    32-bit signed integer value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVD / MOVD </c> instruction.
///
/// \param __a
///    A vector of [4 x i32]. The least significant 32 bits are moved to the
///    destination.
/// \returns A 32-bit signed integer containing the moved value.
static __inline__ int __DEFAULT_FN_ATTRS _mm_cvtsi128_si32(__m128i __a) {
  __v4si __b = (__v4si)__a;
  return __b[0];
}

/// Moves the least significant 64 bits of a vector of [2 x i64] to a
///    64-bit signed integer value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVQ / MOVQ </c> instruction.
///
/// \param __a
///    A vector of [2 x i64]. The least significant 64 bits are moved to the
///    destination.
/// \returns A 64-bit signed integer containing the moved value.
static __inline__ long long __DEFAULT_FN_ATTRS _mm_cvtsi128_si64(__m128i __a) {
  return __a[0];
}

/// Moves packed integer values from an aligned 128-bit memory location
///    to elements in a 128-bit integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDQA / MOVDQA </c> instruction.
///
/// \param __p
///    An aligned pointer to a memory location containing integer values.
/// \returns A 128-bit integer vector containing the moved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_load_si128(__m128i const *__p) {
  return *__p;
}

/// Moves packed integer values from an unaligned 128-bit memory location
///    to elements in a 128-bit integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDQU / MOVDQU </c> instruction.
///
/// \param __p
///    A pointer to a memory location containing integer values.
/// \returns A 128-bit integer vector containing the moved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_loadu_si128(__m128i_u const *__p) {
  struct __loadu_si128 {
    __m128i_u __v;
  } __attribute__((__packed__, __may_alias__));
  return ((const struct __loadu_si128 *)__p)->__v;
}

/// Returns a vector of [2 x i64] where the lower element is taken from
///    the lower element of the operand, and the upper element is zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVQ / MOVQ </c> instruction.
///
/// \param __p
///    A 128-bit vector of [2 x i64]. Bits [63:0] are written to bits [63:0] of
///    the destination.
/// \returns A 128-bit vector of [2 x i64]. The lower order bits contain the
///    moved value. The higher order bits are cleared.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_loadl_epi64(__m128i_u const *__p) {
  struct __mm_loadl_epi64_struct {
    long long __u;
  } __attribute__((__packed__, __may_alias__));
  return __extension__(__m128i){
      ((const struct __mm_loadl_epi64_struct *)__p)->__u, 0};
}

/// Generates a 128-bit vector of [4 x i32] with unspecified content.
///    This could be used as an argument to another intrinsic function where the
///    argument is required but the value is not actually used.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \returns A 128-bit vector of [4 x i32] with unspecified content.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_undefined_si128(void) {
  return (__m128i)__builtin_ia32_undef128();
}

/// Initializes both 64-bit values in a 128-bit vector of [2 x i64] with
///    the specified 64-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __q1
///    A 64-bit integer value used to initialize the upper 64 bits of the
///    destination vector of [2 x i64].
/// \param __q0
///    A 64-bit integer value used to initialize the lower 64 bits of the
///    destination vector of [2 x i64].
/// \returns An initialized 128-bit vector of [2 x i64] containing the values
///    provided in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_set_epi64x(long long __q1,
                                                            long long __q0) {
  return __extension__(__m128i)(__v2di){__q0, __q1};
}

/// Initializes both 64-bit values in a 128-bit vector of [2 x i64] with
///    the specified 64-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __q1
///    A 64-bit integer value used to initialize the upper 64 bits of the
///    destination vector of [2 x i64].
/// \param __q0
///    A 64-bit integer value used to initialize the lower 64 bits of the
///    destination vector of [2 x i64].
/// \returns An initialized 128-bit vector of [2 x i64] containing the values
///    provided in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_set_epi64(__m64 __q1,
                                                           __m64 __q0) {
  return _mm_set_epi64x((long long)__q1, (long long)__q0);
}

/// Initializes the 32-bit values in a 128-bit vector of [4 x i32] with
///    the specified 32-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __i3
///    A 32-bit integer value used to initialize bits [127:96] of the
///    destination vector.
/// \param __i2
///    A 32-bit integer value used to initialize bits [95:64] of the destination
///    vector.
/// \param __i1
///    A 32-bit integer value used to initialize bits [63:32] of the destination
///    vector.
/// \param __i0
///    A 32-bit integer value used to initialize bits [31:0] of the destination
///    vector.
/// \returns An initialized 128-bit vector of [4 x i32] containing the values
///    provided in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_set_epi32(int __i3, int __i2,
                                                           int __i1, int __i0) {
  return __extension__(__m128i)(__v4si){__i0, __i1, __i2, __i3};
}

/// Initializes the 16-bit values in a 128-bit vector of [8 x i16] with
///    the specified 16-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __w7
///    A 16-bit integer value used to initialize bits [127:112] of the
///    destination vector.
/// \param __w6
///    A 16-bit integer value used to initialize bits [111:96] of the
///    destination vector.
/// \param __w5
///    A 16-bit integer value used to initialize bits [95:80] of the destination
///    vector.
/// \param __w4
///    A 16-bit integer value used to initialize bits [79:64] of the destination
///    vector.
/// \param __w3
///    A 16-bit integer value used to initialize bits [63:48] of the destination
///    vector.
/// \param __w2
///    A 16-bit integer value used to initialize bits [47:32] of the destination
///    vector.
/// \param __w1
///    A 16-bit integer value used to initialize bits [31:16] of the destination
///    vector.
/// \param __w0
///    A 16-bit integer value used to initialize bits [15:0] of the destination
///    vector.
/// \returns An initialized 128-bit vector of [8 x i16] containing the values
///    provided in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_set_epi16(short __w7, short __w6, short __w5, short __w4, short __w3,
              short __w2, short __w1, short __w0) {
  return __extension__(__m128i)(__v8hi){__w0, __w1, __w2, __w3,
                                        __w4, __w5, __w6, __w7};
}

/// Initializes the 8-bit values in a 128-bit vector of [16 x i8] with
///    the specified 8-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __b15
///    Initializes bits [127:120] of the destination vector.
/// \param __b14
///    Initializes bits [119:112] of the destination vector.
/// \param __b13
///    Initializes bits [111:104] of the destination vector.
/// \param __b12
///    Initializes bits [103:96] of the destination vector.
/// \param __b11
///    Initializes bits [95:88] of the destination vector.
/// \param __b10
///    Initializes bits [87:80] of the destination vector.
/// \param __b9
///    Initializes bits [79:72] of the destination vector.
/// \param __b8
///    Initializes bits [71:64] of the destination vector.
/// \param __b7
///    Initializes bits [63:56] of the destination vector.
/// \param __b6
///    Initializes bits [55:48] of the destination vector.
/// \param __b5
///    Initializes bits [47:40] of the destination vector.
/// \param __b4
///    Initializes bits [39:32] of the destination vector.
/// \param __b3
///    Initializes bits [31:24] of the destination vector.
/// \param __b2
///    Initializes bits [23:16] of the destination vector.
/// \param __b1
///    Initializes bits [15:8] of the destination vector.
/// \param __b0
///    Initializes bits [7:0] of the destination vector.
/// \returns An initialized 128-bit vector of [16 x i8] containing the values
///    provided in the operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_set_epi8(char __b15, char __b14, char __b13, char __b12, char __b11,
             char __b10, char __b9, char __b8, char __b7, char __b6, char __b5,
             char __b4, char __b3, char __b2, char __b1, char __b0) {
  return __extension__(__m128i)(__v16qi){
      __b0, __b1, __b2,  __b3,  __b4,  __b5,  __b6,  __b7,
      __b8, __b9, __b10, __b11, __b12, __b13, __b14, __b15};
}

/// Initializes both values in a 128-bit integer vector with the
///    specified 64-bit integer value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __q
///    Integer value used to initialize the elements of the destination integer
///    vector.
/// \returns An initialized 128-bit integer vector of [2 x i64] with both
///    elements containing the value provided in the operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_set1_epi64x(long long __q) {
  return _mm_set_epi64x(__q, __q);
}

/// Initializes both values in a 128-bit vector of [2 x i64] with the
///    specified 64-bit value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __q
///    A 64-bit value used to initialize the elements of the destination integer
///    vector.
/// \returns An initialized 128-bit vector of [2 x i64] with all elements
///    containing the value provided in the operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_set1_epi64(__m64 __q) {
  return _mm_set_epi64(__q, __q);
}

/// Initializes all values in a 128-bit vector of [4 x i32] with the
///    specified 32-bit value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __i
///    A 32-bit value used to initialize the elements of the destination integer
///    vector.
/// \returns An initialized 128-bit vector of [4 x i32] with all elements
///    containing the value provided in the operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_set1_epi32(int __i) {
  return _mm_set_epi32(__i, __i, __i, __i);
}

/// Initializes all values in a 128-bit vector of [8 x i16] with the
///    specified 16-bit value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __w
///    A 16-bit value used to initialize the elements of the destination integer
///    vector.
/// \returns An initialized 128-bit vector of [8 x i16] with all elements
///    containing the value provided in the operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_set1_epi16(short __w) {
  return _mm_set_epi16(__w, __w, __w, __w, __w, __w, __w, __w);
}

/// Initializes all values in a 128-bit vector of [16 x i8] with the
///    specified 8-bit value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __b
///    An 8-bit value used to initialize the elements of the destination integer
///    vector.
/// \returns An initialized 128-bit vector of [16 x i8] with all elements
///    containing the value provided in the operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_set1_epi8(char __b) {
  return _mm_set_epi8(__b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b,
                      __b, __b, __b, __b, __b);
}

/// Constructs a 128-bit integer vector, initialized in reverse order
///     with the specified 64-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic does not correspond to a specific instruction.
///
/// \param __q0
///    A 64-bit integral value used to initialize the lower 64 bits of the
///    result.
/// \param __q1
///    A 64-bit integral value used to initialize the upper 64 bits of the
///    result.
/// \returns An initialized 128-bit integer vector.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_setr_epi64(__m64 __q0,
                                                            __m64 __q1) {
  return _mm_set_epi64(__q1, __q0);
}

/// Constructs a 128-bit integer vector, initialized in reverse order
///     with the specified 32-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __i0
///    A 32-bit integral value used to initialize bits [31:0] of the result.
/// \param __i1
///    A 32-bit integral value used to initialize bits [63:32] of the result.
/// \param __i2
///    A 32-bit integral value used to initialize bits [95:64] of the result.
/// \param __i3
///    A 32-bit integral value used to initialize bits [127:96] of the result.
/// \returns An initialized 128-bit integer vector.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_setr_epi32(int __i0, int __i1,
                                                            int __i2,
                                                            int __i3) {
  return _mm_set_epi32(__i3, __i2, __i1, __i0);
}

/// Constructs a 128-bit integer vector, initialized in reverse order
///     with the specified 16-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __w0
///    A 16-bit integral value used to initialize bits [15:0] of the result.
/// \param __w1
///    A 16-bit integral value used to initialize bits [31:16] of the result.
/// \param __w2
///    A 16-bit integral value used to initialize bits [47:32] of the result.
/// \param __w3
///    A 16-bit integral value used to initialize bits [63:48] of the result.
/// \param __w4
///    A 16-bit integral value used to initialize bits [79:64] of the result.
/// \param __w5
///    A 16-bit integral value used to initialize bits [95:80] of the result.
/// \param __w6
///    A 16-bit integral value used to initialize bits [111:96] of the result.
/// \param __w7
///    A 16-bit integral value used to initialize bits [127:112] of the result.
/// \returns An initialized 128-bit integer vector.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_setr_epi16(short __w0, short __w1, short __w2, short __w3, short __w4,
               short __w5, short __w6, short __w7) {
  return _mm_set_epi16(__w7, __w6, __w5, __w4, __w3, __w2, __w1, __w0);
}

/// Constructs a 128-bit integer vector, initialized in reverse order
///     with the specified 8-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __b0
///    An 8-bit integral value used to initialize bits [7:0] of the result.
/// \param __b1
///    An 8-bit integral value used to initialize bits [15:8] of the result.
/// \param __b2
///    An 8-bit integral value used to initialize bits [23:16] of the result.
/// \param __b3
///    An 8-bit integral value used to initialize bits [31:24] of the result.
/// \param __b4
///    An 8-bit integral value used to initialize bits [39:32] of the result.
/// \param __b5
///    An 8-bit integral value used to initialize bits [47:40] of the result.
/// \param __b6
///    An 8-bit integral value used to initialize bits [55:48] of the result.
/// \param __b7
///    An 8-bit integral value used to initialize bits [63:56] of the result.
/// \param __b8
///    An 8-bit integral value used to initialize bits [71:64] of the result.
/// \param __b9
///    An 8-bit integral value used to initialize bits [79:72] of the result.
/// \param __b10
///    An 8-bit integral value used to initialize bits [87:80] of the result.
/// \param __b11
///    An 8-bit integral value used to initialize bits [95:88] of the result.
/// \param __b12
///    An 8-bit integral value used to initialize bits [103:96] of the result.
/// \param __b13
///    An 8-bit integral value used to initialize bits [111:104] of the result.
/// \param __b14
///    An 8-bit integral value used to initialize bits [119:112] of the result.
/// \param __b15
///    An 8-bit integral value used to initialize bits [127:120] of the result.
/// \returns An initialized 128-bit integer vector.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_setr_epi8(char __b0, char __b1, char __b2, char __b3, char __b4, char __b5,
              char __b6, char __b7, char __b8, char __b9, char __b10,
              char __b11, char __b12, char __b13, char __b14, char __b15) {
  return _mm_set_epi8(__b15, __b14, __b13, __b12, __b11, __b10, __b9, __b8,
                      __b7, __b6, __b5, __b4, __b3, __b2, __b1, __b0);
}

/// Creates a 128-bit integer vector initialized to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VXORPS / XORPS </c> instruction.
///
/// \returns An initialized 128-bit integer vector with all elements set to
///    zero.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_setzero_si128(void) {
  return __extension__(__m128i)(__v2di){0LL, 0LL};
}

/// Stores a 128-bit integer vector to a memory location aligned on a
///    128-bit boundary.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPS / MOVAPS </c> instruction.
///
/// \param __p
///    A pointer to an aligned memory location that will receive the integer
///    values.
/// \param __b
///    A 128-bit integer vector containing the values to be moved.
static __inline__ void __DEFAULT_FN_ATTRS _mm_store_si128(__m128i *__p,
                                                          __m128i __b) {
  *__p = __b;
}

/// Stores a 128-bit integer vector to an unaligned memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVUPS / MOVUPS </c> instruction.
///
/// \param __p
///    A pointer to a memory location that will receive the integer values.
/// \param __b
///    A 128-bit integer vector containing the values to be moved.
static __inline__ void __DEFAULT_FN_ATTRS _mm_storeu_si128(__m128i_u *__p,
                                                           __m128i __b) {
  struct __storeu_si128 {
    __m128i_u __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_si128 *)__p)->__v = __b;
}

/// Stores a 64-bit integer value from the low element of a 128-bit integer
///    vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVQ / MOVQ </c> instruction.
///
/// \param __p
///    A pointer to a 64-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \param __b
///    A 128-bit integer vector containing the value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_storeu_si64(void *__p,
                                                          __m128i __b) {
  struct __storeu_si64 {
    long long __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_si64 *)__p)->__v = ((__v2di)__b)[0];
}

/// Stores a 32-bit integer value from the low element of a 128-bit integer
///    vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVD / MOVD </c> instruction.
///
/// \param __p
///    A pointer to a 32-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \param __b
///    A 128-bit integer vector containing the value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_storeu_si32(void *__p,
                                                          __m128i __b) {
  struct __storeu_si32 {
    int __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_si32 *)__p)->__v = ((__v4si)__b)[0];
}

/// Stores a 16-bit integer value from the low element of a 128-bit integer
///    vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic does not correspond to a specific instruction.
///
/// \param __p
///    A pointer to a 16-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \param __b
///    A 128-bit integer vector containing the value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_storeu_si16(void *__p,
                                                          __m128i __b) {
  struct __storeu_si16 {
    short __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_si16 *)__p)->__v = ((__v8hi)__b)[0];
}

/// Moves bytes selected by the mask from the first operand to the
///    specified unaligned memory location. When a mask bit is 1, the
///    corresponding byte is written, otherwise it is not written.
///
///    To minimize caching, the data is flagged as non-temporal (unlikely to be
///    used again soon). Exception and trap behavior for elements not selected
///    for storage to memory are implementation dependent.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMASKMOVDQU / MASKMOVDQU </c>
///   instruction.
///
/// \param __d
///    A 128-bit integer vector containing the values to be moved.
/// \param __n
///    A 128-bit integer vector containing the mask. The most significant bit of
///    each byte represents the mask bits.
/// \param __p
///    A pointer to an unaligned 128-bit memory location where the specified
///    values are moved.
static __inline__ void __DEFAULT_FN_ATTRS _mm_maskmoveu_si128(__m128i __d,
                                                              __m128i __n,
                                                              char *__p) {
  __builtin_ia32_maskmovdqu((__v16qi)__d, (__v16qi)__n, __p);
}

/// Stores the lower 64 bits of a 128-bit integer vector of [2 x i64] to
///    a memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVLPS / MOVLPS </c> instruction.
///
/// \param __p
///    A pointer to a 64-bit memory location that will receive the lower 64 bits
///    of the integer vector parameter.
/// \param __a
///    A 128-bit integer vector of [2 x i64]. The lower 64 bits contain the
///    value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_storel_epi64(__m128i_u *__p,
                                                           __m128i __a) {
  struct __mm_storel_epi64_struct {
    long long __u;
  } __attribute__((__packed__, __may_alias__));
  ((struct __mm_storel_epi64_struct *)__p)->__u = __a[0];
}

/// Stores a 128-bit floating point vector of [2 x double] to a 128-bit
///    aligned memory location.
///
///    To minimize caching, the data is flagged as non-temporal (unlikely to be
///    used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVNTPS / MOVNTPS </c> instruction.
///
/// \param __p
///    A pointer to the 128-bit aligned memory location used to store the value.
/// \param __a
///    A vector of [2 x double] containing the 64-bit values to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_stream_pd(void *__p,
                                                        __m128d __a) {
  __builtin_nontemporal_store((__v2df)__a, (__v2df *)__p);
}

/// Stores a 128-bit integer vector to a 128-bit aligned memory location.
///
///    To minimize caching, the data is flagged as non-temporal (unlikely to be
///    used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVNTPS / MOVNTPS </c> instruction.
///
/// \param __p
///    A pointer to the 128-bit aligned memory location used to store the value.
/// \param __a
///    A 128-bit integer vector containing the values to be stored.
static __inline__ void __DEFAULT_FN_ATTRS _mm_stream_si128(void *__p,
                                                           __m128i __a) {
  __builtin_nontemporal_store((__v2di)__a, (__v2di *)__p);
}

/// Stores a 32-bit integer value in the specified memory location.
///
///    To minimize caching, the data is flagged as non-temporal (unlikely to be
///    used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVNTI </c> instruction.
///
/// \param __p
///    A pointer to the 32-bit memory location used to store the value.
/// \param __a
///    A 32-bit integer containing the value to be stored.
static __inline__ void
    __attribute__((__always_inline__, __nodebug__, __target__("sse2")))
    _mm_stream_si32(void *__p, int __a) {
  __builtin_ia32_movnti((int *)__p, __a);
}

#ifdef __x86_64__
/// Stores a 64-bit integer value in the specified memory location.
///
///    To minimize caching, the data is flagged as non-temporal (unlikely to be
///    used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVNTIQ </c> instruction.
///
/// \param __p
///    A pointer to the 64-bit memory location used to store the value.
/// \param __a
///    A 64-bit integer containing the value to be stored.
static __inline__ void
    __attribute__((__always_inline__, __nodebug__, __target__("sse2")))
    _mm_stream_si64(void *__p, long long __a) {
  __builtin_ia32_movnti64((long long *)__p, __a);
}
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/// The cache line containing \a __p is flushed and invalidated from all
///    caches in the coherency domain.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CLFLUSH </c> instruction.
///
/// \param __p
///    A pointer to the memory location used to identify the cache line to be
///    flushed.
void _mm_clflush(void const *__p);

/// Forces strong memory ordering (serialization) between load
///    instructions preceding this instruction and load instructions following
///    this instruction, ensuring the system completes all previous loads before
///    executing subsequent loads.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> LFENCE </c> instruction.
///
void _mm_lfence(void);

/// Forces strong memory ordering (serialization) between load and store
///    instructions preceding this instruction and load and store instructions
///    following this instruction, ensuring that the system completes all
///    previous memory accesses before executing subsequent memory accesses.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MFENCE </c> instruction.
///
void _mm_mfence(void);

#if defined(__cplusplus)
} // extern "C"
#endif

/// Converts, with saturation, 16-bit signed integers from both 128-bit integer
///    vector operands into 8-bit signed integers, and packs the results into
///    the destination.
///
///    Positive values greater than 0x7F are saturated to 0x7F. Negative values
///    less than 0x80 are saturated to 0x80.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPACKSSWB / PACKSSWB </c> instruction.
///
/// \param __a
///   A 128-bit integer vector of [8 x i16]. The converted [8 x i8] values are
///   written to the lower 64 bits of the result.
/// \param __b
///   A 128-bit integer vector of [8 x i16]. The converted [8 x i8] values are
///   written to the higher 64 bits of the result.
/// \returns A 128-bit vector of [16 x i8] containing the converted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_packs_epi16(__m128i __a,
                                                             __m128i __b) {
  return (__m128i)__builtin_ia32_packsswb128((__v8hi)__a, (__v8hi)__b);
}

/// Converts, with saturation, 32-bit signed integers from both 128-bit integer
///    vector operands into 16-bit signed integers, and packs the results into
///    the destination.
///
///    Positive values greater than 0x7FFF are saturated to 0x7FFF. Negative
///    values less than 0x8000 are saturated to 0x8000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPACKSSDW / PACKSSDW </c> instruction.
///
/// \param __a
///    A 128-bit integer vector of [4 x i32]. The converted [4 x i16] values
///    are written to the lower 64 bits of the result.
/// \param __b
///    A 128-bit integer vector of [4 x i32]. The converted [4 x i16] values
///    are written to the higher 64 bits of the result.
/// \returns A 128-bit vector of [8 x i16] containing the converted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_packs_epi32(__m128i __a,
                                                             __m128i __b) {
  return (__m128i)__builtin_ia32_packssdw128((__v4si)__a, (__v4si)__b);
}

/// Converts, with saturation, 16-bit signed integers from both 128-bit integer
///    vector operands into 8-bit unsigned integers, and packs the results into
///    the destination.
///
///    Values greater than 0xFF are saturated to 0xFF. Values less than 0x00
///    are saturated to 0x00.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPACKUSWB / PACKUSWB </c> instruction.
///
/// \param __a
///    A 128-bit integer vector of [8 x i16]. The converted [8 x i8] values are
///    written to the lower 64 bits of the result.
/// \param __b
///    A 128-bit integer vector of [8 x i16]. The converted [8 x i8] values are
///    written to the higher 64 bits of the result.
/// \returns A 128-bit vector of [16 x i8] containing the converted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_packus_epi16(__m128i __a,
                                                              __m128i __b) {
  return (__m128i)__builtin_ia32_packuswb128((__v8hi)__a, (__v8hi)__b);
}

/// Extracts 16 bits from a 128-bit integer vector of [8 x i16], using
///    the immediate-value parameter as a selector.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_extract_epi16(__m128i a, const int imm);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPEXTRW / PEXTRW </c> instruction.
///
/// \param a
///    A 128-bit integer vector.
/// \param imm
///    An immediate value. Bits [2:0] selects values from \a a to be assigned
///    to bits[15:0] of the result. \n
///    000: assign values from bits [15:0] of \a a. \n
///    001: assign values from bits [31:16] of \a a. \n
///    010: assign values from bits [47:32] of \a a. \n
///    011: assign values from bits [63:48] of \a a. \n
///    100: assign values from bits [79:64] of \a a. \n
///    101: assign values from bits [95:80] of \a a. \n
///    110: assign values from bits [111:96] of \a a. \n
///    111: assign values from bits [127:112] of \a a.
/// \returns An integer, whose lower 16 bits are selected from the 128-bit
///    integer vector parameter and the remaining bits are assigned zeros.
#define _mm_extract_epi16(a, imm)                                              \
  ((int)(unsigned short)__builtin_ia32_vec_ext_v8hi((__v8hi)(__m128i)(a),      \
                                                    (int)(imm)))

/// Constructs a 128-bit integer vector by first making a copy of the
///    128-bit integer vector parameter, and then inserting the lower 16 bits
///    of an integer parameter into an offset specified by the immediate-value
///    parameter.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_insert_epi16(__m128i a, int b, const int imm);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPINSRW / PINSRW </c> instruction.
///
/// \param a
///    A 128-bit integer vector of [8 x i16]. This vector is copied to the
///    result and then one of the eight elements in the result is replaced by
///    the lower 16 bits of \a b.
/// \param b
///    An integer. The lower 16 bits of this parameter are written to the
///    result beginning at an offset specified by \a imm.
/// \param imm
///    An immediate value specifying the bit offset in the result at which the
///    lower 16 bits of \a b are written.
/// \returns A 128-bit integer vector containing the constructed values.
#define _mm_insert_epi16(a, b, imm)                                            \
  ((__m128i)__builtin_ia32_vec_set_v8hi((__v8hi)(__m128i)(a), (int)(b),        \
                                        (int)(imm)))

/// Copies the values of the most significant bits from each 8-bit
///    element in a 128-bit integer vector of [16 x i8] to create a 16-bit mask
///    value, zero-extends the value, and writes it to the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVMSKB / PMOVMSKB </c> instruction.
///
/// \param __a
///    A 128-bit integer vector containing the values with bits to be extracted.
/// \returns The most significant bits from each 8-bit element in \a __a,
///    written to bits [15:0]. The other bits are assigned zeros.
static __inline__ int __DEFAULT_FN_ATTRS _mm_movemask_epi8(__m128i __a) {
  return __builtin_ia32_pmovmskb128((__v16qi)__a);
}

/// Constructs a 128-bit integer vector by shuffling four 32-bit
///    elements of a 128-bit integer vector parameter, using the immediate-value
///    parameter as a specifier.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_shuffle_epi32(__m128i a, const int imm);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPSHUFD / PSHUFD </c> instruction.
///
/// \param a
///    A 128-bit integer vector containing the values to be copied.
/// \param imm
///    An immediate value containing an 8-bit value specifying which elements to
///    copy from a. The destinations within the 128-bit destination are assigned
///    values as follows: \n
///    Bits [1:0] are used to assign values to bits [31:0] of the result. \n
///    Bits [3:2] are used to assign values to bits [63:32] of the result. \n
///    Bits [5:4] are used to assign values to bits [95:64] of the result. \n
///    Bits [7:6] are used to assign values to bits [127:96] of the result. \n
///    Bit value assignments: \n
///    00: assign values from bits [31:0] of \a a. \n
///    01: assign values from bits [63:32] of \a a. \n
///    10: assign values from bits [95:64] of \a a. \n
///    11: assign values from bits [127:96] of \a a. \n
///    Note: To generate a mask, you can use the \c _MM_SHUFFLE macro.
///    <c>_MM_SHUFFLE(b6, b4, b2, b0)</c> can create an 8-bit mask of the form
///    <c>[b6, b4, b2, b0]</c>.
/// \returns A 128-bit integer vector containing the shuffled values.
#define _mm_shuffle_epi32(a, imm)                                              \
  ((__m128i)__builtin_ia32_pshufd((__v4si)(__m128i)(a), (int)(imm)))

/// Constructs a 128-bit integer vector by shuffling four lower 16-bit
///    elements of a 128-bit integer vector of [8 x i16], using the immediate
///    value parameter as a specifier.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_shufflelo_epi16(__m128i a, const int imm);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPSHUFLW / PSHUFLW </c> instruction.
///
/// \param a
///    A 128-bit integer vector of [8 x i16]. Bits [127:64] are copied to bits
///    [127:64] of the result.
/// \param imm
///    An 8-bit immediate value specifying which elements to copy from \a a. \n
///    Bits[1:0] are used to assign values to bits [15:0] of the result. \n
///    Bits[3:2] are used to assign values to bits [31:16] of the result. \n
///    Bits[5:4] are used to assign values to bits [47:32] of the result. \n
///    Bits[7:6] are used to assign values to bits [63:48] of the result. \n
///    Bit value assignments: \n
///    00: assign values from bits [15:0] of \a a. \n
///    01: assign values from bits [31:16] of \a a. \n
///    10: assign values from bits [47:32] of \a a. \n
///    11: assign values from bits [63:48] of \a a. \n
///    Note: To generate a mask, you can use the \c _MM_SHUFFLE macro.
///    <c>_MM_SHUFFLE(b6, b4, b2, b0)</c> can create an 8-bit mask of the form
///    <c>[b6, b4, b2, b0]</c>.
/// \returns A 128-bit integer vector containing the shuffled values.
#define _mm_shufflelo_epi16(a, imm)                                            \
  ((__m128i)__builtin_ia32_pshuflw((__v8hi)(__m128i)(a), (int)(imm)))

/// Constructs a 128-bit integer vector by shuffling four upper 16-bit
///    elements of a 128-bit integer vector of [8 x i16], using the immediate
///    value parameter as a specifier.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_shufflehi_epi16(__m128i a, const int imm);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPSHUFHW / PSHUFHW </c> instruction.
///
/// \param a
///    A 128-bit integer vector of [8 x i16]. Bits [63:0] are copied to bits
///    [63:0] of the result.
/// \param imm
///    An 8-bit immediate value specifying which elements to copy from \a a. \n
///    Bits[1:0] are used to assign values to bits [79:64] of the result. \n
///    Bits[3:2] are used to assign values to bits [95:80] of the result. \n
///    Bits[5:4] are used to assign values to bits [111:96] of the result. \n
///    Bits[7:6] are used to assign values to bits [127:112] of the result. \n
///    Bit value assignments: \n
///    00: assign values from bits [79:64] of \a a. \n
///    01: assign values from bits [95:80] of \a a. \n
///    10: assign values from bits [111:96] of \a a. \n
///    11: assign values from bits [127:112] of \a a. \n
///    Note: To generate a mask, you can use the \c _MM_SHUFFLE macro.
///    <c>_MM_SHUFFLE(b6, b4, b2, b0)</c> can create an 8-bit mask of the form
///    <c>[b6, b4, b2, b0]</c>.
/// \returns A 128-bit integer vector containing the shuffled values.
#define _mm_shufflehi_epi16(a, imm)                                            \
  ((__m128i)__builtin_ia32_pshufhw((__v8hi)(__m128i)(a), (int)(imm)))

/// Unpacks the high-order (index 8-15) values from two 128-bit vectors
///    of [16 x i8] and interleaves them into a 128-bit vector of [16 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKHBW / PUNPCKHBW </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [16 x i8].
///    Bits [71:64] are written to bits [7:0] of the result. \n
///    Bits [79:72] are written to bits [23:16] of the result. \n
///    Bits [87:80] are written to bits [39:32] of the result. \n
///    Bits [95:88] are written to bits [55:48] of the result. \n
///    Bits [103:96] are written to bits [71:64] of the result. \n
///    Bits [111:104] are written to bits [87:80] of the result. \n
///    Bits [119:112] are written to bits [103:96] of the result. \n
///    Bits [127:120] are written to bits [119:112] of the result.
/// \param __b
///    A 128-bit vector of [16 x i8]. \n
///    Bits [71:64] are written to bits [15:8] of the result. \n
///    Bits [79:72] are written to bits [31:24] of the result. \n
///    Bits [87:80] are written to bits [47:40] of the result. \n
///    Bits [95:88] are written to bits [63:56] of the result. \n
///    Bits [103:96] are written to bits [79:72] of the result. \n
///    Bits [111:104] are written to bits [95:88] of the result. \n
///    Bits [119:112] are written to bits [111:104] of the result. \n
///    Bits [127:120] are written to bits [127:120] of the result.
/// \returns A 128-bit vector of [16 x i8] containing the interleaved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_unpackhi_epi8(__m128i __a,
                                                               __m128i __b) {
  return (__m128i)__builtin_shufflevector(
      (__v16qi)__a, (__v16qi)__b, 8, 16 + 8, 9, 16 + 9, 10, 16 + 10, 11,
      16 + 11, 12, 16 + 12, 13, 16 + 13, 14, 16 + 14, 15, 16 + 15);
}

/// Unpacks the high-order (index 4-7) values from two 128-bit vectors of
///    [8 x i16] and interleaves them into a 128-bit vector of [8 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKHWD / PUNPCKHWD </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [8 x i16].
///    Bits [79:64] are written to bits [15:0] of the result. \n
///    Bits [95:80] are written to bits [47:32] of the result. \n
///    Bits [111:96] are written to bits [79:64] of the result. \n
///    Bits [127:112] are written to bits [111:96] of the result.
/// \param __b
///    A 128-bit vector of [8 x i16].
///    Bits [79:64] are written to bits [31:16] of the result. \n
///    Bits [95:80] are written to bits [63:48] of the result. \n
///    Bits [111:96] are written to bits [95:80] of the result. \n
///    Bits [127:112] are written to bits [127:112] of the result.
/// \returns A 128-bit vector of [8 x i16] containing the interleaved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_unpackhi_epi16(__m128i __a,
                                                                __m128i __b) {
  return (__m128i)__builtin_shufflevector((__v8hi)__a, (__v8hi)__b, 4, 8 + 4, 5,
                                          8 + 5, 6, 8 + 6, 7, 8 + 7);
}

/// Unpacks the high-order (index 2,3) values from two 128-bit vectors of
///    [4 x i32] and interleaves them into a 128-bit vector of [4 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKHDQ / PUNPCKHDQ </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [4 x i32]. \n
///    Bits [95:64] are written to bits [31:0] of the destination. \n
///    Bits [127:96] are written to bits [95:64] of the destination.
/// \param __b
///    A 128-bit vector of [4 x i32]. \n
///    Bits [95:64] are written to bits [64:32] of the destination. \n
///    Bits [127:96] are written to bits [127:96] of the destination.
/// \returns A 128-bit vector of [4 x i32] containing the interleaved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_unpackhi_epi32(__m128i __a,
                                                                __m128i __b) {
  return (__m128i)__builtin_shufflevector((__v4si)__a, (__v4si)__b, 2, 4 + 2, 3,
                                          4 + 3);
}

/// Unpacks the high-order 64-bit elements from two 128-bit vectors of
///    [2 x i64] and interleaves them into a 128-bit vector of [2 x i64].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKHQDQ / PUNPCKHQDQ </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [2 x i64]. \n
///    Bits [127:64] are written to bits [63:0] of the destination.
/// \param __b
///    A 128-bit vector of [2 x i64]. \n
///    Bits [127:64] are written to bits [127:64] of the destination.
/// \returns A 128-bit vector of [2 x i64] containing the interleaved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_unpackhi_epi64(__m128i __a,
                                                                __m128i __b) {
  return (__m128i)__builtin_shufflevector((__v2di)__a, (__v2di)__b, 1, 2 + 1);
}

/// Unpacks the low-order (index 0-7) values from two 128-bit vectors of
///    [16 x i8] and interleaves them into a 128-bit vector of [16 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKLBW / PUNPCKLBW </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [16 x i8]. \n
///    Bits [7:0] are written to bits [7:0] of the result. \n
///    Bits [15:8] are written to bits [23:16] of the result. \n
///    Bits [23:16] are written to bits [39:32] of the result. \n
///    Bits [31:24] are written to bits [55:48] of the result. \n
///    Bits [39:32] are written to bits [71:64] of the result. \n
///    Bits [47:40] are written to bits [87:80] of the result. \n
///    Bits [55:48] are written to bits [103:96] of the result. \n
///    Bits [63:56] are written to bits [119:112] of the result.
/// \param __b
///    A 128-bit vector of [16 x i8].
///    Bits [7:0] are written to bits [15:8] of the result. \n
///    Bits [15:8] are written to bits [31:24] of the result. \n
///    Bits [23:16] are written to bits [47:40] of the result. \n
///    Bits [31:24] are written to bits [63:56] of the result. \n
///    Bits [39:32] are written to bits [79:72] of the result. \n
///    Bits [47:40] are written to bits [95:88] of the result. \n
///    Bits [55:48] are written to bits [111:104] of the result. \n
///    Bits [63:56] are written to bits [127:120] of the result.
/// \returns A 128-bit vector of [16 x i8] containing the interleaved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_unpacklo_epi8(__m128i __a,
                                                               __m128i __b) {
  return (__m128i)__builtin_shufflevector(
      (__v16qi)__a, (__v16qi)__b, 0, 16 + 0, 1, 16 + 1, 2, 16 + 2, 3, 16 + 3, 4,
      16 + 4, 5, 16 + 5, 6, 16 + 6, 7, 16 + 7);
}

/// Unpacks the low-order (index 0-3) values from each of the two 128-bit
///    vectors of [8 x i16] and interleaves them into a 128-bit vector of
///    [8 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKLWD / PUNPCKLWD </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [8 x i16].
///    Bits [15:0] are written to bits [15:0] of the result. \n
///    Bits [31:16] are written to bits [47:32] of the result. \n
///    Bits [47:32] are written to bits [79:64] of the result. \n
///    Bits [63:48] are written to bits [111:96] of the result.
/// \param __b
///    A 128-bit vector of [8 x i16].
///    Bits [15:0] are written to bits [31:16] of the result. \n
///    Bits [31:16] are written to bits [63:48] of the result. \n
///    Bits [47:32] are written to bits [95:80] of the result. \n
///    Bits [63:48] are written to bits [127:112] of the result.
/// \returns A 128-bit vector of [8 x i16] containing the interleaved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_unpacklo_epi16(__m128i __a,
                                                                __m128i __b) {
  return (__m128i)__builtin_shufflevector((__v8hi)__a, (__v8hi)__b, 0, 8 + 0, 1,
                                          8 + 1, 2, 8 + 2, 3, 8 + 3);
}

/// Unpacks the low-order (index 0,1) values from two 128-bit vectors of
///    [4 x i32] and interleaves them into a 128-bit vector of [4 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKLDQ / PUNPCKLDQ </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [4 x i32]. \n
///    Bits [31:0] are written to bits [31:0] of the destination. \n
///    Bits [63:32] are written to bits [95:64] of the destination.
/// \param __b
///    A 128-bit vector of [4 x i32]. \n
///    Bits [31:0] are written to bits [64:32] of the destination. \n
///    Bits [63:32] are written to bits [127:96] of the destination.
/// \returns A 128-bit vector of [4 x i32] containing the interleaved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_unpacklo_epi32(__m128i __a,
                                                                __m128i __b) {
  return (__m128i)__builtin_shufflevector((__v4si)__a, (__v4si)__b, 0, 4 + 0, 1,
                                          4 + 1);
}

/// Unpacks the low-order 64-bit elements from two 128-bit vectors of
///    [2 x i64] and interleaves them into a 128-bit vector of [2 x i64].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKLQDQ / PUNPCKLQDQ </c>
///   instruction.
///
/// \param __a
///    A 128-bit vector of [2 x i64]. \n
///    Bits [63:0] are written to bits [63:0] of the destination. \n
/// \param __b
///    A 128-bit vector of [2 x i64]. \n
///    Bits [63:0] are written to bits [127:64] of the destination. \n
/// \returns A 128-bit vector of [2 x i64] containing the interleaved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_unpacklo_epi64(__m128i __a,
                                                                __m128i __b) {
  return (__m128i)__builtin_shufflevector((__v2di)__a, (__v2di)__b, 0, 2 + 0);
}

/// Returns the lower 64 bits of a 128-bit integer vector as a 64-bit
///    integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVDQ2Q </c> instruction.
///
/// \param __a
///    A 128-bit integer vector operand. The lower 64 bits are moved to the
///    destination.
/// \returns A 64-bit integer containing the lower 64 bits of the parameter.
static __inline__ __m64 __DEFAULT_FN_ATTRS _mm_movepi64_pi64(__m128i __a) {
  return (__m64)__a[0];
}

/// Moves the 64-bit operand to a 128-bit integer vector, zeroing the
///    upper bits.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVD+VMOVQ </c> instruction.
///
/// \param __a
///    A 64-bit value.
/// \returns A 128-bit integer vector. The lower 64 bits contain the value from
///    the operand. The upper 64 bits are assigned zeros.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_movpi64_epi64(__m64 __a) {
  return __extension__(__m128i)(__v2di){(long long)__a, 0};
}

/// Moves the lower 64 bits of a 128-bit integer vector to a 128-bit
///    integer vector, zeroing the upper bits.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVQ / MOVQ </c> instruction.
///
/// \param __a
///    A 128-bit integer vector operand. The lower 64 bits are moved to the
///    destination.
/// \returns A 128-bit integer vector. The lower 64 bits contain the value from
///    the operand. The upper 64 bits are assigned zeros.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_move_epi64(__m128i __a) {
  return __builtin_shufflevector((__v2di)__a, _mm_setzero_si128(), 0, 2);
}

/// Unpacks the high-order 64-bit elements from two 128-bit vectors of
///    [2 x double] and interleaves them into a 128-bit vector of [2 x
///    double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKHPD / UNPCKHPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. \n
///    Bits [127:64] are written to bits [63:0] of the destination.
/// \param __b
///    A 128-bit vector of [2 x double]. \n
///    Bits [127:64] are written to bits [127:64] of the destination.
/// \returns A 128-bit vector of [2 x double] containing the interleaved values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_unpackhi_pd(__m128d __a,
                                                             __m128d __b) {
  return __builtin_shufflevector((__v2df)__a, (__v2df)__b, 1, 2 + 1);
}

/// Unpacks the low-order 64-bit elements from two 128-bit vectors
///    of [2 x double] and interleaves them into a 128-bit vector of [2 x
///    double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKLPD / UNPCKLPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. \n
///    Bits [63:0] are written to bits [63:0] of the destination.
/// \param __b
///    A 128-bit vector of [2 x double]. \n
///    Bits [63:0] are written to bits [127:64] of the destination.
/// \returns A 128-bit vector of [2 x double] containing the interleaved values.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_unpacklo_pd(__m128d __a,
                                                             __m128d __b) {
  return __builtin_shufflevector((__v2df)__a, (__v2df)__b, 0, 2 + 0);
}

/// Extracts the sign bits of the double-precision values in the 128-bit
///    vector of [2 x double], zero-extends the value, and writes it to the
///    low-order bits of the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVMSKPD / MOVMSKPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing the values with sign bits to
///    be extracted.
/// \returns The sign bits from each of the double-precision elements in \a __a,
///    written to bits [1:0]. The remaining bits are assigned values of zero.
static __inline__ int __DEFAULT_FN_ATTRS _mm_movemask_pd(__m128d __a) {
  return __builtin_ia32_movmskpd((__v2df)__a);
}

/// Constructs a 128-bit floating-point vector of [2 x double] from two
///    128-bit vector parameters of [2 x double], using the immediate-value
///     parameter as a specifier.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_shuffle_pd(__m128d a, __m128d b, const int i);
/// \endcode
///
/// This intrinsic corresponds to the <c> VSHUFPD / SHUFPD </c> instruction.
///
/// \param a
///    A 128-bit vector of [2 x double].
/// \param b
///    A 128-bit vector of [2 x double].
/// \param i
///    An 8-bit immediate value. The least significant two bits specify which
///    elements to copy from \a a and \a b: \n
///    Bit[0] = 0: lower element of \a a copied to lower element of result. \n
///    Bit[0] = 1: upper element of \a a copied to lower element of result. \n
///    Bit[1] = 0: lower element of \a b copied to upper element of result. \n
///    Bit[1] = 1: upper element of \a b copied to upper element of result. \n
///    Note: To generate a mask, you can use the \c _MM_SHUFFLE2 macro.
///    <c>_MM_SHUFFLE2(b1, b0)</c> can create a 2-bit mask of the form
///    <c>[b1, b0]</c>.
/// \returns A 128-bit vector of [2 x double] containing the shuffled values.
#define _mm_shuffle_pd(a, b, i)                                                \
  ((__m128d)__builtin_ia32_shufpd((__v2df)(__m128d)(a), (__v2df)(__m128d)(b),  \
                                  (int)(i)))

/// Casts a 128-bit floating-point vector of [2 x double] into a 128-bit
///    floating-point vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit floating-point vector of [2 x double].
/// \returns A 128-bit floating-point vector of [4 x float] containing the same
///    bitwise pattern as the parameter.
static __inline__ __m128 __DEFAULT_FN_ATTRS _mm_castpd_ps(__m128d __a) {
  return (__m128)__a;
}

/// Casts a 128-bit floating-point vector of [2 x double] into a 128-bit
///    integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit floating-point vector of [2 x double].
/// \returns A 128-bit integer vector containing the same bitwise pattern as the
///    parameter.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_castpd_si128(__m128d __a) {
  return (__m128i)__a;
}

/// Casts a 128-bit floating-point vector of [4 x float] into a 128-bit
///    floating-point vector of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit floating-point vector of [4 x float].
/// \returns A 128-bit floating-point vector of [2 x double] containing the same
///    bitwise pattern as the parameter.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_castps_pd(__m128 __a) {
  return (__m128d)__a;
}

/// Casts a 128-bit floating-point vector of [4 x float] into a 128-bit
///    integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit floating-point vector of [4 x float].
/// \returns A 128-bit integer vector containing the same bitwise pattern as the
///    parameter.
static __inline__ __m128i __DEFAULT_FN_ATTRS _mm_castps_si128(__m128 __a) {
  return (__m128i)__a;
}

/// Casts a 128-bit integer vector into a 128-bit floating-point vector
///    of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \returns A 128-bit floating-point vector of [4 x float] containing the same
///    bitwise pattern as the parameter.
static __inline__ __m128 __DEFAULT_FN_ATTRS _mm_castsi128_ps(__m128i __a) {
  return (__m128)__a;
}

/// Casts a 128-bit integer vector into a 128-bit floating-point vector
///    of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \returns A 128-bit floating-point vector of [2 x double] containing the same
///    bitwise pattern as the parameter.
static __inline__ __m128d __DEFAULT_FN_ATTRS _mm_castsi128_pd(__m128i __a) {
  return (__m128d)__a;
}

/// Compares each of the corresponding double-precision values of two
///    128-bit vectors of [2 x double], using the operation specified by the
///    immediate integer operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, comparisons that are ordered
///    return false, and comparisons that are unordered return true.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_cmp_pd(__m128d a, __m128d b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> (V)CMPPD </c> instruction.
///
/// \param a
///    A 128-bit vector of [2 x double].
/// \param b
///    A 128-bit vector of [2 x double].
/// \param c
///    An immediate integer operand, with bits [4:0] specifying which comparison
///    operation to use: \n
///    0x00: Equal (ordered, non-signaling) \n
///    0x01: Less-than (ordered, signaling) \n
///    0x02: Less-than-or-equal (ordered, signaling) \n
///    0x03: Unordered (non-signaling) \n
///    0x04: Not-equal (unordered, non-signaling) \n
///    0x05: Not-less-than (unordered, signaling) \n
///    0x06: Not-less-than-or-equal (unordered, signaling) \n
///    0x07: Ordered (non-signaling) \n
/// \returns A 128-bit vector of [2 x double] containing the comparison results.
#define _mm_cmp_pd(a, b, c)                                                    \
  ((__m128d)__builtin_ia32_cmppd((__v2df)(__m128d)(a), (__v2df)(__m128d)(b),   \
                                 (c)))

/// Compares each of the corresponding scalar double-precision values of
///    two 128-bit vectors of [2 x double], using the operation specified by the
///    immediate integer operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, comparisons that are ordered
///    return false, and comparisons that are unordered return true.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_cmp_sd(__m128d a, __m128d b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> (V)CMPSD </c> instruction.
///
/// \param a
///    A 128-bit vector of [2 x double].
/// \param b
///    A 128-bit vector of [2 x double].
/// \param c
///    An immediate integer operand, with bits [4:0] specifying which comparison
///    operation to use: \n
///    0x00: Equal (ordered, non-signaling) \n
///    0x01: Less-than (ordered, signaling) \n
///    0x02: Less-than-or-equal (ordered, signaling) \n
///    0x03: Unordered (non-signaling) \n
///    0x04: Not-equal (unordered, non-signaling) \n
///    0x05: Not-less-than (unordered, signaling) \n
///    0x06: Not-less-than-or-equal (unordered, signaling) \n
///    0x07: Ordered (non-signaling) \n
/// \returns A 128-bit vector of [2 x double] containing the comparison results.
#define _mm_cmp_sd(a, b, c)                                                    \
  ((__m128d)__builtin_ia32_cmpsd((__v2df)(__m128d)(a), (__v2df)(__m128d)(b),   \
                                 (c)))

#if defined(__cplusplus)
extern "C" {
#endif

/// Indicates that a spin loop is being executed for the purposes of
///    optimizing power consumption during the loop.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PAUSE </c> instruction.
///
void _mm_pause(void);

#if defined(__cplusplus)
} // extern "C"
#endif
#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS_MMX

#define _MM_SHUFFLE2(x, y) (((x) << 1) | (y))

#define _MM_DENORMALS_ZERO_ON (0x0040U)
#define _MM_DENORMALS_ZERO_OFF (0x0000U)

#define _MM_DENORMALS_ZERO_MASK (0x0040U)

#define _MM_GET_DENORMALS_ZERO_MODE() (_mm_getcsr() & _MM_DENORMALS_ZERO_MASK)
#define _MM_SET_DENORMALS_ZERO_MODE(x)                                         \
  (_mm_setcsr((_mm_getcsr() & ~_MM_DENORMALS_ZERO_MASK) | (x)))

#endif /* __EMMINTRIN_H */
