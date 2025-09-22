/*===---- xmmintrin.h - SSE intrinsics -------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __XMMINTRIN_H
#define __XMMINTRIN_H

#if !defined(__i386__) && !defined(__x86_64__)
#error "This header is only meant to be used on x86 and x64 architecture"
#endif

#include <mmintrin.h>

typedef int __v4si __attribute__((__vector_size__(16)));
typedef float __v4sf __attribute__((__vector_size__(16)));
typedef float __m128 __attribute__((__vector_size__(16), __aligned__(16)));

typedef float __m128_u __attribute__((__vector_size__(16), __aligned__(1)));

/* Unsigned types */
typedef unsigned int __v4su __attribute__((__vector_size__(16)));

/* This header should only be included in a hosted environment as it depends on
 * a standard library to provide allocation routines. */
#if __STDC_HOSTED__
#include <mm_malloc.h>
#endif

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__, __target__("sse,no-evex512"), \
                 __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS_MMX                                                 \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("mmx,sse,no-evex512"), __min_vector_width__(64)))

/// Adds the 32-bit float values in the low-order bits of the operands.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDSS / ADDSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the source operands.
///    The lower 32 bits of this operand are used in the calculation.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the source operands.
///    The lower 32 bits of this operand are used in the calculation.
/// \returns A 128-bit vector of [4 x float] whose lower 32 bits contain the sum
///    of the lower 32 bits of both operands. The upper 96 bits are copied from
///    the upper 96 bits of the first source operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_add_ss(__m128 __a, __m128 __b)
{
  __a[0] += __b[0];
  return __a;
}

/// Adds two 128-bit vectors of [4 x float], and returns the results of
///    the addition.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDPS / ADDPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the source operands.
/// \returns A 128-bit vector of [4 x float] containing the sums of both
///    operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_add_ps(__m128 __a, __m128 __b)
{
  return (__m128)((__v4sf)__a + (__v4sf)__b);
}

/// Subtracts the 32-bit float value in the low-order bits of the second
///    operand from the corresponding value in the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSUBSS / SUBSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing the minuend. The lower 32 bits
///    of this operand are used in the calculation.
/// \param __b
///    A 128-bit vector of [4 x float] containing the subtrahend. The lower 32
///    bits of this operand are used in the calculation.
/// \returns A 128-bit vector of [4 x float] whose lower 32 bits contain the
///    difference of the lower 32 bits of both operands. The upper 96 bits are
///    copied from the upper 96 bits of the first source operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_sub_ss(__m128 __a, __m128 __b)
{
  __a[0] -= __b[0];
  return __a;
}

/// Subtracts each of the values of the second operand from the first
///    operand, both of which are 128-bit vectors of [4 x float] and returns
///    the results of the subtraction.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSUBPS / SUBPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing the minuend.
/// \param __b
///    A 128-bit vector of [4 x float] containing the subtrahend.
/// \returns A 128-bit vector of [4 x float] containing the differences between
///    both operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_sub_ps(__m128 __a, __m128 __b)
{
  return (__m128)((__v4sf)__a - (__v4sf)__b);
}

/// Multiplies two 32-bit float values in the low-order bits of the
///    operands.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMULSS / MULSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the source operands.
///    The lower 32 bits of this operand are used in the calculation.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the source operands.
///    The lower 32 bits of this operand are used in the calculation.
/// \returns A 128-bit vector of [4 x float] containing the product of the lower
///    32 bits of both operands. The upper 96 bits are copied from the upper 96
///    bits of the first source operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_mul_ss(__m128 __a, __m128 __b)
{
  __a[0] *= __b[0];
  return __a;
}

/// Multiplies two 128-bit vectors of [4 x float] and returns the
///    results of the multiplication.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMULPS / MULPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the source operands.
/// \returns A 128-bit vector of [4 x float] containing the products of both
///    operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_mul_ps(__m128 __a, __m128 __b)
{
  return (__m128)((__v4sf)__a * (__v4sf)__b);
}

/// Divides the value in the low-order 32 bits of the first operand by
///    the corresponding value in the second operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDIVSS / DIVSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing the dividend. The lower 32
///    bits of this operand are used in the calculation.
/// \param __b
///    A 128-bit vector of [4 x float] containing the divisor. The lower 32 bits
///    of this operand are used in the calculation.
/// \returns A 128-bit vector of [4 x float] containing the quotients of the
///    lower 32 bits of both operands. The upper 96 bits are copied from the
///    upper 96 bits of the first source operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_div_ss(__m128 __a, __m128 __b)
{
  __a[0] /= __b[0];
  return __a;
}

/// Divides two 128-bit vectors of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDIVPS / DIVPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing the dividend.
/// \param __b
///    A 128-bit vector of [4 x float] containing the divisor.
/// \returns A 128-bit vector of [4 x float] containing the quotients of both
///    operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_div_ps(__m128 __a, __m128 __b)
{
  return (__m128)((__v4sf)__a / (__v4sf)__b);
}

/// Calculates the square root of the value stored in the low-order bits
///    of a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSQRTSS / SQRTSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the calculation.
/// \returns A 128-bit vector of [4 x float] containing the square root of the
///    value in the low-order bits of the operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_sqrt_ss(__m128 __a)
{
  return (__m128)__builtin_ia32_sqrtss((__v4sf)__a);
}

/// Calculates the square roots of the values stored in a 128-bit vector
///    of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSQRTPS / SQRTPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the square roots of the
///    values in the operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_sqrt_ps(__m128 __a)
{
  return __builtin_ia32_sqrtps((__v4sf)__a);
}

/// Calculates the approximate reciprocal of the value stored in the
///    low-order bits of a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VRCPSS / RCPSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the calculation.
/// \returns A 128-bit vector of [4 x float] containing the approximate
///    reciprocal of the value in the low-order bits of the operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_rcp_ss(__m128 __a)
{
  return (__m128)__builtin_ia32_rcpss((__v4sf)__a);
}

/// Calculates the approximate reciprocals of the values stored in a
///    128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VRCPPS / RCPPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the approximate
///    reciprocals of the values in the operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_rcp_ps(__m128 __a)
{
  return (__m128)__builtin_ia32_rcpps((__v4sf)__a);
}

/// Calculates the approximate reciprocal of the square root of the value
///    stored in the low-order bits of a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VRSQRTSS / RSQRTSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the calculation.
/// \returns A 128-bit vector of [4 x float] containing the approximate
///    reciprocal of the square root of the value in the low-order bits of the
///    operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_rsqrt_ss(__m128 __a)
{
  return __builtin_ia32_rsqrtss((__v4sf)__a);
}

/// Calculates the approximate reciprocals of the square roots of the
///    values stored in a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VRSQRTPS / RSQRTPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the approximate
///    reciprocals of the square roots of the values in the operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_rsqrt_ps(__m128 __a)
{
  return __builtin_ia32_rsqrtps((__v4sf)__a);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands and returns the lesser value in the low-order bits of the
///    vector of [4 x float].
///
///    If either value in a comparison is NaN, returns the value from \a __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMINSS / MINSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] whose lower 32 bits contain the
///    minimum value between both operands. The upper 96 bits are copied from
///    the upper 96 bits of the first source operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_min_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_minss((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 128-bit vectors of [4 x float] and returns the lesser
///    of each pair of values.
///
///    If either value in a comparison is NaN, returns the value from \a __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMINPS / MINPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands.
/// \returns A 128-bit vector of [4 x float] containing the minimum values
///    between both operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_min_ps(__m128 __a, __m128 __b)
{
  return __builtin_ia32_minps((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands and returns the greater value in the low-order bits of a 128-bit
///    vector of [4 x float].
///
///    If either value in a comparison is NaN, returns the value from \a __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMAXSS / MAXSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] whose lower 32 bits contain the
///    maximum value between both operands. The upper 96 bits are copied from
///    the upper 96 bits of the first source operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_max_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_maxss((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 128-bit vectors of [4 x float] and returns the greater
///    of each pair of values.
///
///    If either value in a comparison is NaN, returns the value from \a __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMAXPS / MAXPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands.
/// \returns A 128-bit vector of [4 x float] containing the maximum values
///    between both operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_max_ps(__m128 __a, __m128 __b)
{
  return __builtin_ia32_maxps((__v4sf)__a, (__v4sf)__b);
}

/// Performs a bitwise AND of two 128-bit vectors of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VANDPS / ANDPS </c> instructions.
///
/// \param __a
///    A 128-bit vector containing one of the source operands.
/// \param __b
///    A 128-bit vector containing one of the source operands.
/// \returns A 128-bit vector of [4 x float] containing the bitwise AND of the
///    values between both operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_and_ps(__m128 __a, __m128 __b)
{
  return (__m128)((__v4su)__a & (__v4su)__b);
}

/// Performs a bitwise AND of two 128-bit vectors of [4 x float], using
///    the one's complement of the values contained in the first source
///    operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VANDNPS / ANDNPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing the first source operand. The
///    one's complement of this value is used in the bitwise AND.
/// \param __b
///    A 128-bit vector of [4 x float] containing the second source operand.
/// \returns A 128-bit vector of [4 x float] containing the bitwise AND of the
///    one's complement of the first operand and the values in the second
///    operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_andnot_ps(__m128 __a, __m128 __b)
{
  return (__m128)(~(__v4su)__a & (__v4su)__b);
}

/// Performs a bitwise OR of two 128-bit vectors of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VORPS / ORPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the source operands.
/// \returns A 128-bit vector of [4 x float] containing the bitwise OR of the
///    values between both operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_or_ps(__m128 __a, __m128 __b)
{
  return (__m128)((__v4su)__a | (__v4su)__b);
}

/// Performs a bitwise exclusive OR of two 128-bit vectors of
///    [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VXORPS / XORPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the source operands.
/// \returns A 128-bit vector of [4 x float] containing the bitwise exclusive OR
///    of the values between both operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_xor_ps(__m128 __a, __m128 __b)
{
  return (__m128)((__v4su)__a ^ (__v4su)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands for equality.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFF for true, in the
///    low-order bits of a vector [4 x float].
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPEQSS / CMPEQSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpeq_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpeqss((__v4sf)__a, (__v4sf)__b);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] for equality.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPEQPS / CMPEQPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpeq_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpeqps((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is less than the
///    corresponding value in the second operand.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFF for true, in the
///    low-order bits of a vector of [4 x float].
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLTSS / CMPLTSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmplt_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpltss((__v4sf)__a, (__v4sf)__b);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are less than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLTPS / CMPLTPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmplt_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpltps((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is less than or
///    equal to the corresponding value in the second operand.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true, in
///    the low-order bits of a vector of [4 x float].
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLESS / CMPLESS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmple_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpless((__v4sf)__a, (__v4sf)__b);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are less than or equal to those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLEPS / CMPLEPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmple_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpleps((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is greater than
///    the corresponding value in the second operand.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFF for true, in the
///    low-order bits of a vector of [4 x float].
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLTSS / CMPLTSS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpgt_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_shufflevector((__v4sf)__a,
                                         (__v4sf)__builtin_ia32_cmpltss((__v4sf)__b, (__v4sf)__a),
                                         4, 1, 2, 3);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are greater than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLTPS / CMPLTPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpgt_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpltps((__v4sf)__b, (__v4sf)__a);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is greater than
///    or equal to the corresponding value in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true, in the
///    low-order bits of a vector of [4 x float].
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLESS / CMPLESS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpge_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_shufflevector((__v4sf)__a,
                                         (__v4sf)__builtin_ia32_cmpless((__v4sf)__b, (__v4sf)__a),
                                         4, 1, 2, 3);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are greater than or equal to those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFFFFFFFFFF for true.
///    If either value in a comparison is NaN, returns false.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPLEPS / CMPLEPS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpge_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpleps((__v4sf)__b, (__v4sf)__a);
}

/// Compares two 32-bit float values in the low-order bits of both operands
///    for inequality.
///
///    The comparison returns 0x0 for false, 0xFFFFFFFF for true, in the
///    low-order bits of a vector of [4 x float].
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNEQSS / CMPNEQSS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpneq_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpneqss((__v4sf)__a, (__v4sf)__b);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] for inequality.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNEQPS / CMPNEQPS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpneq_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpneqps((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is not less than
///    the corresponding value in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true, in the
///    low-order bits of a vector of [4 x float].
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLTSS / CMPNLTSS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpnlt_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpnltss((__v4sf)__a, (__v4sf)__b);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are not less than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLTPS / CMPNLTPS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpnlt_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpnltps((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is not less than
///    or equal to the corresponding value in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true, in the
///    low-order bits of a vector of [4 x float].
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLESS / CMPNLESS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpnle_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpnless((__v4sf)__a, (__v4sf)__b);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are not less than or equal to those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLEPS / CMPNLEPS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpnle_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpnleps((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is not greater
///    than the corresponding value in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true, in the
///    low-order bits of a vector of [4 x float].
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLTSS / CMPNLTSS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpngt_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_shufflevector((__v4sf)__a,
                                         (__v4sf)__builtin_ia32_cmpnltss((__v4sf)__b, (__v4sf)__a),
                                         4, 1, 2, 3);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are not greater than those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLTPS / CMPNLTPS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpngt_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpnltps((__v4sf)__b, (__v4sf)__a);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is not greater
///    than or equal to the corresponding value in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true, in the
///    low-order bits of a vector of [4 x float].
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLESS / CMPNLESS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpnge_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_shufflevector((__v4sf)__a,
                                         (__v4sf)__builtin_ia32_cmpnless((__v4sf)__b, (__v4sf)__a),
                                         4, 1, 2, 3);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are not greater than or equal to those in the second operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, returns true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPNLEPS / CMPNLEPS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpnge_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpnleps((__v4sf)__b, (__v4sf)__a);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is ordered with
///    respect to the corresponding value in the second operand.
///
///    A pair of floating-point values are ordered with respect to each
///    other if neither value is a NaN. Each comparison returns 0x0 for false,
///    0xFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPORDSS / CMPORDSS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpord_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpordss((__v4sf)__a, (__v4sf)__b);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are ordered with respect to those in the second operand.
///
///    A pair of floating-point values are ordered with respect to each
///    other if neither value is a NaN. Each comparison returns 0x0 for false,
///    0xFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPORDPS / CMPORDPS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpord_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpordps((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the value in the first operand is unordered
///    with respect to the corresponding value in the second operand.
///
///    A pair of double-precision values are unordered with respect to each
///    other if one or both values are NaN. Each comparison returns 0x0 for
///    false, 0xFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPUNORDSS / CMPUNORDSS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the operands. The lower
///    32 bits of this operand are used in the comparison.
/// \returns A 128-bit vector of [4 x float] containing the comparison results
///    in the low-order bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpunord_ss(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpunordss((__v4sf)__a, (__v4sf)__b);
}

/// Compares each of the corresponding 32-bit float values of the
///    128-bit vectors of [4 x float] to determine if the values in the first
///    operand are unordered with respect to those in the second operand.
///
///    A pair of double-precision values are unordered with respect to each
///    other if one or both values are NaN. Each comparison returns 0x0 for
///    false, 0xFFFFFFFFFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCMPUNORDPS / CMPUNORDPS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cmpunord_ps(__m128 __a, __m128 __b)
{
  return (__m128)__builtin_ia32_cmpunordps((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands for equality.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISS / COMISS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_comieq_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_comieq((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the first operand is less than the second
///    operand.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISS / COMISS </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_comilt_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_comilt((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the first operand is less than or equal to the
///    second operand.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISS / COMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_comile_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_comile((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the first operand is greater than the second
///    operand.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISS / COMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_comigt_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_comigt((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the first operand is greater than or equal to
///    the second operand.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISS / COMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_comige_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_comige((__v4sf)__a, (__v4sf)__b);
}

/// Compares two 32-bit float values in the low-order bits of both
///    operands to determine if the first operand is not equal to the second
///    operand.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCOMISS / COMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_comineq_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_comineq((__v4sf)__a, (__v4sf)__b);
}

/// Performs an unordered comparison of two 32-bit float values using
///    the low-order bits of both operands to determine equality.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISS / UCOMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_ucomieq_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_ucomieq((__v4sf)__a, (__v4sf)__b);
}

/// Performs an unordered comparison of two 32-bit float values using
///    the low-order bits of both operands to determine if the first operand is
///    less than the second operand.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISS / UCOMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_ucomilt_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_ucomilt((__v4sf)__a, (__v4sf)__b);
}

/// Performs an unordered comparison of two 32-bit float values using
///    the low-order bits of both operands to determine if the first operand is
///    less than or equal to the second operand.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISS / UCOMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_ucomile_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_ucomile((__v4sf)__a, (__v4sf)__b);
}

/// Performs an unordered comparison of two 32-bit float values using
///    the low-order bits of both operands to determine if the first operand is
///    greater than the second operand.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISS / UCOMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_ucomigt_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_ucomigt((__v4sf)__a, (__v4sf)__b);
}

/// Performs an unordered comparison of two 32-bit float values using
///    the low-order bits of both operands to determine if the first operand is
///    greater than or equal to the second operand.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISS / UCOMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_ucomige_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_ucomige((__v4sf)__a, (__v4sf)__b);
}

/// Performs an unordered comparison of two 32-bit float values using
///    the low-order bits of both operands to determine inequality.
///
///    The comparison returns 0 for false, 1 for true. If either value in a
///    comparison is NaN, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUCOMISS / UCOMISS </c> instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \param __b
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the comparison.
/// \returns An integer containing the comparison results.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_ucomineq_ss(__m128 __a, __m128 __b)
{
  return __builtin_ia32_ucomineq((__v4sf)__a, (__v4sf)__b);
}

/// Converts a float value contained in the lower 32 bits of a vector of
///    [4 x float] into a 32-bit integer.
///
///    If the converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSS2SI / CVTSS2SI </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the conversion.
/// \returns A 32-bit integer containing the converted value.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_cvtss_si32(__m128 __a)
{
  return __builtin_ia32_cvtss2si((__v4sf)__a);
}

/// Converts a float value contained in the lower 32 bits of a vector of
///    [4 x float] into a 32-bit integer.
///
///    If the converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSS2SI / CVTSS2SI </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the conversion.
/// \returns A 32-bit integer containing the converted value.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_cvt_ss2si(__m128 __a)
{
  return _mm_cvtss_si32(__a);
}

#ifdef __x86_64__

/// Converts a float value contained in the lower 32 bits of a vector of
///    [4 x float] into a 64-bit integer.
///
///    If the converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSS2SI / CVTSS2SI </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the conversion.
/// \returns A 64-bit integer containing the converted value.
static __inline__ long long __DEFAULT_FN_ATTRS
_mm_cvtss_si64(__m128 __a)
{
  return __builtin_ia32_cvtss2si64((__v4sf)__a);
}

#endif

/// Converts two low-order float values in a 128-bit vector of
///    [4 x float] into a 64-bit vector of [2 x i32].
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPS2PI </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 64-bit integer vector containing the converted values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_cvtps_pi32(__m128 __a)
{
  return (__m64)__builtin_ia32_cvtps2pi((__v4sf)__a);
}

/// Converts two low-order float values in a 128-bit vector of
///    [4 x float] into a 64-bit vector of [2 x i32].
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPS2PI </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 64-bit integer vector containing the converted values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_cvt_ps2pi(__m128 __a)
{
  return _mm_cvtps_pi32(__a);
}

/// Converts the lower (first) element of a vector of [4 x float] into a signed
///    truncated (rounded toward zero) 32-bit integer.
///
///    If the converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTTSS2SI / CVTTSS2SI </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the conversion.
/// \returns A 32-bit integer containing the converted value.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_cvttss_si32(__m128 __a)
{
  return __builtin_ia32_cvttss2si((__v4sf)__a);
}

/// Converts the lower (first) element of a vector of [4 x float] into a signed
///    truncated (rounded toward zero) 32-bit integer.
///
///    If the converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTTSS2SI / CVTTSS2SI </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the conversion.
/// \returns A 32-bit integer containing the converted value.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_cvtt_ss2si(__m128 __a)
{
  return _mm_cvttss_si32(__a);
}

#ifdef __x86_64__
/// Converts the lower (first) element of a vector of [4 x float] into a signed
///    truncated (rounded toward zero) 64-bit integer.
///
///    If the converted value does not fit in a 64-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTTSS2SI / CVTTSS2SI </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the conversion.
/// \returns A 64-bit integer containing the converted value.
static __inline__ long long __DEFAULT_FN_ATTRS
_mm_cvttss_si64(__m128 __a)
{
  return __builtin_ia32_cvttss2si64((__v4sf)__a);
}
#endif

/// Converts the lower (first) two elements of a 128-bit vector of [4 x float]
///    into two signed truncated (rounded toward zero) 32-bit integers,
///    returned in a 64-bit vector of [2 x i32].
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTTPS2PI / VTTPS2PI </c>
///   instructions.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 64-bit integer vector containing the converted values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_cvttps_pi32(__m128 __a)
{
  return (__m64)__builtin_ia32_cvttps2pi((__v4sf)__a);
}

/// Converts the lower (first) two elements of a 128-bit vector of [4 x float]
///    into two signed truncated (rounded toward zero) 64-bit integers,
///    returned in a 64-bit vector of [2 x i32].
///
///    If a converted value does not fit in a 32-bit integer, raises a
///    floating-point invalid exception. If the exception is masked, returns
///    the most negative integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTTPS2PI </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 64-bit integer vector containing the converted values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_cvtt_ps2pi(__m128 __a)
{
  return _mm_cvttps_pi32(__a);
}

/// Converts a 32-bit signed integer value into a floating point value
///    and writes it to the lower 32 bits of the destination. The remaining
///    higher order elements of the destination vector are copied from the
///    corresponding elements in the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSI2SS / CVTSI2SS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 32-bit signed integer operand containing the value to be converted.
/// \returns A 128-bit vector of [4 x float] whose lower 32 bits contain the
///    converted value of the second operand. The upper 96 bits are copied from
///    the upper 96 bits of the first operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cvtsi32_ss(__m128 __a, int __b)
{
  __a[0] = __b;
  return __a;
}

/// Converts a 32-bit signed integer value into a floating point value
///    and writes it to the lower 32 bits of the destination. The remaining
///    higher order elements of the destination are copied from the
///    corresponding elements in the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSI2SS / CVTSI2SS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 32-bit signed integer operand containing the value to be converted.
/// \returns A 128-bit vector of [4 x float] whose lower 32 bits contain the
///    converted value of the second operand. The upper 96 bits are copied from
///    the upper 96 bits of the first operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cvt_si2ss(__m128 __a, int __b)
{
  return _mm_cvtsi32_ss(__a, __b);
}

#ifdef __x86_64__

/// Converts a 64-bit signed integer value into a floating point value
///    and writes it to the lower 32 bits of the destination. The remaining
///    higher order elements of the destination are copied from the
///    corresponding elements in the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTSI2SS / CVTSI2SS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 64-bit signed integer operand containing the value to be converted.
/// \returns A 128-bit vector of [4 x float] whose lower 32 bits contain the
///    converted value of the second operand. The upper 96 bits are copied from
///    the upper 96 bits of the first operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_cvtsi64_ss(__m128 __a, long long __b)
{
  __a[0] = __b;
  return __a;
}

#endif

/// Converts two elements of a 64-bit vector of [2 x i32] into two
///    floating point values and writes them to the lower 64-bits of the
///    destination. The remaining higher order elements of the destination are
///    copied from the corresponding elements in the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPI2PS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 64-bit vector of [2 x i32]. The elements in this vector are converted
///    and written to the corresponding low-order elements in the destination.
/// \returns A 128-bit vector of [4 x float] whose lower 64 bits contain the
///    converted value of the second operand. The upper 64 bits are copied from
///    the upper 64 bits of the first operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS_MMX
_mm_cvtpi32_ps(__m128 __a, __m64 __b)
{
  return __builtin_ia32_cvtpi2ps((__v4sf)__a, (__v2si)__b);
}

/// Converts two elements of a 64-bit vector of [2 x i32] into two
///    floating point values and writes them to the lower 64-bits of the
///    destination. The remaining higher order elements of the destination are
///    copied from the corresponding elements in the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPI2PS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 64-bit vector of [2 x i32]. The elements in this vector are converted
///    and written to the corresponding low-order elements in the destination.
/// \returns A 128-bit vector of [4 x float] whose lower 64 bits contain the
///    converted value from the second operand. The upper 64 bits are copied
///    from the upper 64 bits of the first operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS_MMX
_mm_cvt_pi2ps(__m128 __a, __m64 __b)
{
  return _mm_cvtpi32_ps(__a, __b);
}

/// Extracts a float value contained in the lower 32 bits of a vector of
///    [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float]. The lower 32 bits of this operand are
///    used in the extraction.
/// \returns A 32-bit float containing the extracted value.
static __inline__ float __DEFAULT_FN_ATTRS
_mm_cvtss_f32(__m128 __a)
{
  return __a[0];
}

/// Loads two packed float values from the address \a __p into the
///     high-order bits of a 128-bit vector of [4 x float]. The low-order bits
///     are copied from the low-order bits of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVHPD / MOVHPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float]. Bits [63:0] are written to bits [63:0]
///    of the destination.
/// \param __p
///    A pointer to two packed float values. Bits [63:0] are written to bits
///    [127:64] of the destination.
/// \returns A 128-bit vector of [4 x float] containing the moved values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_loadh_pi(__m128 __a, const __m64 *__p)
{
  typedef float __mm_loadh_pi_v2f32 __attribute__((__vector_size__(8)));
  struct __mm_loadh_pi_struct {
    __mm_loadh_pi_v2f32 __u;
  } __attribute__((__packed__, __may_alias__));
  __mm_loadh_pi_v2f32 __b = ((const struct __mm_loadh_pi_struct*)__p)->__u;
  __m128 __bb = __builtin_shufflevector(__b, __b, 0, 1, 0, 1);
  return __builtin_shufflevector(__a, __bb, 0, 1, 4, 5);
}

/// Loads two packed float values from the address \a __p into the
///    low-order bits of a 128-bit vector of [4 x float]. The high-order bits
///    are copied from the high-order bits of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVLPD / MOVLPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float]. Bits [127:64] are written to bits
///    [127:64] of the destination.
/// \param __p
///    A pointer to two packed float values. Bits [63:0] are written to bits
///    [63:0] of the destination.
/// \returns A 128-bit vector of [4 x float] containing the moved values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_loadl_pi(__m128 __a, const __m64 *__p)
{
  typedef float __mm_loadl_pi_v2f32 __attribute__((__vector_size__(8)));
  struct __mm_loadl_pi_struct {
    __mm_loadl_pi_v2f32 __u;
  } __attribute__((__packed__, __may_alias__));
  __mm_loadl_pi_v2f32 __b = ((const struct __mm_loadl_pi_struct*)__p)->__u;
  __m128 __bb = __builtin_shufflevector(__b, __b, 0, 1, 0, 1);
  return __builtin_shufflevector(__a, __bb, 4, 5, 2, 3);
}

/// Constructs a 128-bit floating-point vector of [4 x float]. The lower
///    32 bits of the vector are initialized with the single-precision
///    floating-point value loaded from a specified memory location. The upper
///    96 bits are set to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVSS / MOVSS </c> instruction.
///
/// \param __p
///    A pointer to a 32-bit memory location containing a single-precision
///    floating-point value.
/// \returns An initialized 128-bit floating-point vector of [4 x float]. The
///    lower 32 bits contain the value loaded from the memory location. The
///    upper 96 bits are set to zero.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_load_ss(const float *__p)
{
  struct __mm_load_ss_struct {
    float __u;
  } __attribute__((__packed__, __may_alias__));
  float __u = ((const struct __mm_load_ss_struct*)__p)->__u;
  return __extension__ (__m128){ __u, 0, 0, 0 };
}

/// Loads a 32-bit float value and duplicates it to all four vector
///    elements of a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBROADCASTSS / MOVSS + shuffling </c>
///    instruction.
///
/// \param __p
///    A pointer to a float value to be loaded and duplicated.
/// \returns A 128-bit vector of [4 x float] containing the loaded and
///    duplicated values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_load1_ps(const float *__p)
{
  struct __mm_load1_ps_struct {
    float __u;
  } __attribute__((__packed__, __may_alias__));
  float __u = ((const struct __mm_load1_ps_struct*)__p)->__u;
  return __extension__ (__m128){ __u, __u, __u, __u };
}

#define        _mm_load_ps1(p) _mm_load1_ps(p)

/// Loads a 128-bit floating-point vector of [4 x float] from an aligned
///    memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPS / MOVAPS </c> instruction.
///
/// \param __p
///    A pointer to a 128-bit memory location. The address of the memory
///    location has to be 128-bit aligned.
/// \returns A 128-bit vector of [4 x float] containing the loaded values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_load_ps(const float *__p)
{
  return *(const __m128*)__p;
}

/// Loads a 128-bit floating-point vector of [4 x float] from an
///    unaligned memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVUPS / MOVUPS </c> instruction.
///
/// \param __p
///    A pointer to a 128-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \returns A 128-bit vector of [4 x float] containing the loaded values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_loadu_ps(const float *__p)
{
  struct __loadu_ps {
    __m128_u __v;
  } __attribute__((__packed__, __may_alias__));
  return ((const struct __loadu_ps*)__p)->__v;
}

/// Loads four packed float values, in reverse order, from an aligned
///    memory location to 32-bit elements in a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPS / MOVAPS + shuffling </c>
///    instruction.
///
/// \param __p
///    A pointer to a 128-bit memory location. The address of the memory
///    location has to be 128-bit aligned.
/// \returns A 128-bit vector of [4 x float] containing the moved values, loaded
///    in reverse order.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_loadr_ps(const float *__p)
{
  __m128 __a = _mm_load_ps(__p);
  return __builtin_shufflevector((__v4sf)__a, (__v4sf)__a, 3, 2, 1, 0);
}

/// Create a 128-bit vector of [4 x float] with undefined values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \returns A 128-bit vector of [4 x float] containing undefined values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_undefined_ps(void)
{
  return (__m128)__builtin_ia32_undef128();
}

/// Constructs a 128-bit floating-point vector of [4 x float]. The lower
///    32 bits of the vector are initialized with the specified single-precision
///    floating-point value. The upper 96 bits are set to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVSS / MOVSS </c> instruction.
///
/// \param __w
///    A single-precision floating-point value used to initialize the lower 32
///    bits of the result.
/// \returns An initialized 128-bit floating-point vector of [4 x float]. The
///    lower 32 bits contain the value provided in the source operand. The
///    upper 96 bits are set to zero.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_set_ss(float __w)
{
  return __extension__ (__m128){ __w, 0.0f, 0.0f, 0.0f };
}

/// Constructs a 128-bit floating-point vector of [4 x float], with each
///    of the four single-precision floating-point vector elements set to the
///    specified single-precision floating-point value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPERMILPS / PERMILPS </c> instruction.
///
/// \param __w
///    A single-precision floating-point value used to initialize each vector
///    element of the result.
/// \returns An initialized 128-bit floating-point vector of [4 x float].
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_set1_ps(float __w)
{
  return __extension__ (__m128){ __w, __w, __w, __w };
}

/* Microsoft specific. */
/// Constructs a 128-bit floating-point vector of [4 x float], with each
///    of the four single-precision floating-point vector elements set to the
///    specified single-precision floating-point value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPERMILPS / PERMILPS </c> instruction.
///
/// \param __w
///    A single-precision floating-point value used to initialize each vector
///    element of the result.
/// \returns An initialized 128-bit floating-point vector of [4 x float].
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_set_ps1(float __w)
{
    return _mm_set1_ps(__w);
}

/// Constructs a 128-bit floating-point vector of [4 x float]
///    initialized with the specified single-precision floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __z
///    A single-precision floating-point value used to initialize bits [127:96]
///    of the result.
/// \param __y
///    A single-precision floating-point value used to initialize bits [95:64]
///    of the result.
/// \param __x
///    A single-precision floating-point value used to initialize bits [63:32]
///    of the result.
/// \param __w
///    A single-precision floating-point value used to initialize bits [31:0]
///    of the result.
/// \returns An initialized 128-bit floating-point vector of [4 x float].
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_set_ps(float __z, float __y, float __x, float __w)
{
  return __extension__ (__m128){ __w, __x, __y, __z };
}

/// Constructs a 128-bit floating-point vector of [4 x float],
///    initialized in reverse order with the specified 32-bit single-precision
///    float-point values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __z
///    A single-precision floating-point value used to initialize bits [31:0]
///    of the result.
/// \param __y
///    A single-precision floating-point value used to initialize bits [63:32]
///    of the result.
/// \param __x
///    A single-precision floating-point value used to initialize bits [95:64]
///    of the result.
/// \param __w
///    A single-precision floating-point value used to initialize bits [127:96]
///    of the result.
/// \returns An initialized 128-bit floating-point vector of [4 x float].
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_setr_ps(float __z, float __y, float __x, float __w)
{
  return __extension__ (__m128){ __z, __y, __x, __w };
}

/// Constructs a 128-bit floating-point vector of [4 x float] initialized
///    to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VXORPS / XORPS </c> instruction.
///
/// \returns An initialized 128-bit floating-point vector of [4 x float] with
///    all elements set to zero.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_setzero_ps(void)
{
  return __extension__ (__m128){ 0.0f, 0.0f, 0.0f, 0.0f };
}

/// Stores the upper 64 bits of a 128-bit vector of [4 x float] to a
///    memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPEXTRQ / PEXTRQ </c> instruction.
///
/// \param __p
///    A pointer to a 64-bit memory location.
/// \param __a
///    A 128-bit vector of [4 x float] containing the values to be stored.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_storeh_pi(__m64 *__p, __m128 __a)
{
  typedef float __mm_storeh_pi_v2f32 __attribute__((__vector_size__(8)));
  struct __mm_storeh_pi_struct {
    __mm_storeh_pi_v2f32 __u;
  } __attribute__((__packed__, __may_alias__));
  ((struct __mm_storeh_pi_struct*)__p)->__u = __builtin_shufflevector(__a, __a, 2, 3);
}

/// Stores the lower 64 bits of a 128-bit vector of [4 x float] to a
///     memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVLPS / MOVLPS </c> instruction.
///
/// \param __p
///    A pointer to a memory location that will receive the float values.
/// \param __a
///    A 128-bit vector of [4 x float] containing the values to be stored.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_storel_pi(__m64 *__p, __m128 __a)
{
  typedef float __mm_storeh_pi_v2f32 __attribute__((__vector_size__(8)));
  struct __mm_storeh_pi_struct {
    __mm_storeh_pi_v2f32 __u;
  } __attribute__((__packed__, __may_alias__));
  ((struct __mm_storeh_pi_struct*)__p)->__u = __builtin_shufflevector(__a, __a, 0, 1);
}

/// Stores the lower 32 bits of a 128-bit vector of [4 x float] to a
///     memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVSS / MOVSS </c> instruction.
///
/// \param __p
///    A pointer to a 32-bit memory location.
/// \param __a
///    A 128-bit vector of [4 x float] containing the value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_store_ss(float *__p, __m128 __a)
{
  struct __mm_store_ss_struct {
    float __u;
  } __attribute__((__packed__, __may_alias__));
  ((struct __mm_store_ss_struct*)__p)->__u = __a[0];
}

/// Stores a 128-bit vector of [4 x float] to an unaligned memory
///    location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVUPS / MOVUPS </c> instruction.
///
/// \param __p
///    A pointer to a 128-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \param __a
///    A 128-bit vector of [4 x float] containing the values to be stored.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_storeu_ps(float *__p, __m128 __a)
{
  struct __storeu_ps {
    __m128_u __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_ps*)__p)->__v = __a;
}

/// Stores a 128-bit vector of [4 x float] into an aligned memory
///    location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPS / MOVAPS </c> instruction.
///
/// \param __p
///    A pointer to a 128-bit memory location. The address of the memory
///    location has to be 16-byte aligned.
/// \param __a
///    A 128-bit vector of [4 x float] containing the values to be stored.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_store_ps(float *__p, __m128 __a)
{
  *(__m128*)__p = __a;
}

/// Stores the lower 32 bits of a 128-bit vector of [4 x float] into
///    four contiguous elements in an aligned memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to <c> VMOVAPS / MOVAPS + shuffling </c>
///    instruction.
///
/// \param __p
///    A pointer to a 128-bit memory location.
/// \param __a
///    A 128-bit vector of [4 x float] whose lower 32 bits are stored to each
///    of the four contiguous elements pointed by \a __p.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_store1_ps(float *__p, __m128 __a)
{
  __a = __builtin_shufflevector((__v4sf)__a, (__v4sf)__a, 0, 0, 0, 0);
  _mm_store_ps(__p, __a);
}

/// Stores the lower 32 bits of a 128-bit vector of [4 x float] into
///    four contiguous elements in an aligned memory location.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to <c> VMOVAPS / MOVAPS + shuffling </c>
///    instruction.
///
/// \param __p
///    A pointer to a 128-bit memory location.
/// \param __a
///    A 128-bit vector of [4 x float] whose lower 32 bits are stored to each
///    of the four contiguous elements pointed by \a __p.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_store_ps1(float *__p, __m128 __a)
{
  _mm_store1_ps(__p, __a);
}

/// Stores float values from a 128-bit vector of [4 x float] to an
///    aligned memory location in reverse order.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPS / MOVAPS + shuffling </c>
///    instruction.
///
/// \param __p
///    A pointer to a 128-bit memory location. The address of the memory
///    location has to be 128-bit aligned.
/// \param __a
///    A 128-bit vector of [4 x float] containing the values to be stored.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_storer_ps(float *__p, __m128 __a)
{
  __a = __builtin_shufflevector((__v4sf)__a, (__v4sf)__a, 3, 2, 1, 0);
  _mm_store_ps(__p, __a);
}

#define _MM_HINT_ET0 7
#define _MM_HINT_ET1 6
#define _MM_HINT_T0  3
#define _MM_HINT_T1  2
#define _MM_HINT_T2  1
#define _MM_HINT_NTA 0

#ifndef _MSC_VER
/* FIXME: We have to #define this because "sel" must be a constant integer, and
   Sema doesn't do any form of constant propagation yet. */

/// Loads one cache line of data from the specified address to a location
///    closer to the processor.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// void _mm_prefetch(const void *a, const int sel);
/// \endcode
///
/// This intrinsic corresponds to the <c> PREFETCHNTA </c> instruction.
///
/// \param a
///    A pointer to a memory location containing a cache line of data.
/// \param sel
///    A predefined integer constant specifying the type of prefetch
///    operation: \n
///    _MM_HINT_NTA: Move data using the non-temporal access (NTA) hint. The
///    PREFETCHNTA instruction will be generated. \n
///    _MM_HINT_T0: Move data using the T0 hint. The PREFETCHT0 instruction will
///    be generated. \n
///    _MM_HINT_T1: Move data using the T1 hint. The PREFETCHT1 instruction will
///    be generated. \n
///    _MM_HINT_T2: Move data using the T2 hint. The PREFETCHT2 instruction will
///    be generated.
#define _mm_prefetch(a, sel) (__builtin_prefetch((const void *)(a), \
                                                 ((sel) >> 2) & 1, (sel) & 0x3))
#endif

/// Stores a 64-bit integer in the specified aligned memory location. To
///    minimize caching, the data is flagged as non-temporal (unlikely to be
///    used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVNTQ </c> instruction.
///
/// \param __p
///    A pointer to an aligned memory location used to store the register value.
/// \param __a
///    A 64-bit integer containing the value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS_MMX
_mm_stream_pi(void *__p, __m64 __a)
{
  __builtin_ia32_movntq((__m64 *)__p, __a);
}

/// Moves packed float values from a 128-bit vector of [4 x float] to a
///    128-bit aligned memory location. To minimize caching, the data is flagged
///    as non-temporal (unlikely to be used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVNTPS / MOVNTPS </c> instruction.
///
/// \param __p
///    A pointer to a 128-bit aligned memory location that will receive the
///    single-precision floating-point values.
/// \param __a
///    A 128-bit vector of [4 x float] containing the values to be moved.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_stream_ps(void *__p, __m128 __a)
{
  __builtin_nontemporal_store((__v4sf)__a, (__v4sf*)__p);
}

#if defined(__cplusplus)
extern "C" {
#endif

/// Forces strong memory ordering (serialization) between store
///    instructions preceding this instruction and store instructions following
///    this instruction, ensuring the system completes all previous stores
///    before executing subsequent stores.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> SFENCE </c> instruction.
///
void _mm_sfence(void);

#if defined(__cplusplus)
} // extern "C"
#endif

/// Extracts 16-bit element from a 64-bit vector of [4 x i16] and
///    returns it, as specified by the immediate integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_extract_pi16(__m64 a, int n);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPEXTRW / PEXTRW </c> instruction.
///
/// \param a
///    A 64-bit vector of [4 x i16].
/// \param n
///    An immediate integer operand that determines which bits are extracted: \n
///    0: Bits [15:0] are copied to the destination. \n
///    1: Bits [31:16] are copied to the destination. \n
///    2: Bits [47:32] are copied to the destination. \n
///    3: Bits [63:48] are copied to the destination.
/// \returns A 16-bit integer containing the extracted 16 bits of packed data.
#define _mm_extract_pi16(a, n) \
  ((int)__builtin_ia32_vec_ext_v4hi((__v4hi)a, (int)n))

/// Copies data from the 64-bit vector of [4 x i16] to the destination,
///    and inserts the lower 16-bits of an integer operand at the 16-bit offset
///    specified by the immediate operand \a n.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m64 _mm_insert_pi16(__m64 a, int d, int n);
/// \endcode
///
/// This intrinsic corresponds to the <c> PINSRW </c> instruction.
///
/// \param a
///    A 64-bit vector of [4 x i16].
/// \param d
///    An integer. The lower 16-bit value from this operand is written to the
///    destination at the offset specified by operand \a n.
/// \param n
///    An immediate integer operant that determines which the bits to be used
///    in the destination. \n
///    0: Bits [15:0] are copied to the destination. \n
///    1: Bits [31:16] are copied to the destination. \n
///    2: Bits [47:32] are copied to the destination. \n
///    3: Bits [63:48] are copied to the destination.  \n
///    The remaining bits in the destination are copied from the corresponding
///    bits in operand \a a.
/// \returns A 64-bit integer vector containing the copied packed data from the
///    operands.
#define _mm_insert_pi16(a, d, n) \
  ((__m64)__builtin_ia32_vec_set_v4hi((__v4hi)a, (int)d, (int)n))

/// Compares each of the corresponding packed 16-bit integer values of
///    the 64-bit integer vectors, and writes the greater value to the
///    corresponding bits in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMAXSW </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing one of the source operands.
/// \param __b
///    A 64-bit integer vector containing one of the source operands.
/// \returns A 64-bit integer vector containing the comparison results.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_max_pi16(__m64 __a, __m64 __b)
{
  return (__m64)__builtin_ia32_pmaxsw((__v4hi)__a, (__v4hi)__b);
}

/// Compares each of the corresponding packed 8-bit unsigned integer
///    values of the 64-bit integer vectors, and writes the greater value to the
///    corresponding bits in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMAXUB </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing one of the source operands.
/// \param __b
///    A 64-bit integer vector containing one of the source operands.
/// \returns A 64-bit integer vector containing the comparison results.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_max_pu8(__m64 __a, __m64 __b)
{
  return (__m64)__builtin_ia32_pmaxub((__v8qi)__a, (__v8qi)__b);
}

/// Compares each of the corresponding packed 16-bit integer values of
///    the 64-bit integer vectors, and writes the lesser value to the
///    corresponding bits in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMINSW </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing one of the source operands.
/// \param __b
///    A 64-bit integer vector containing one of the source operands.
/// \returns A 64-bit integer vector containing the comparison results.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_min_pi16(__m64 __a, __m64 __b)
{
  return (__m64)__builtin_ia32_pminsw((__v4hi)__a, (__v4hi)__b);
}

/// Compares each of the corresponding packed 8-bit unsigned integer
///    values of the 64-bit integer vectors, and writes the lesser value to the
///    corresponding bits in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMINUB </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing one of the source operands.
/// \param __b
///    A 64-bit integer vector containing one of the source operands.
/// \returns A 64-bit integer vector containing the comparison results.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_min_pu8(__m64 __a, __m64 __b)
{
  return (__m64)__builtin_ia32_pminub((__v8qi)__a, (__v8qi)__b);
}

/// Takes the most significant bit from each 8-bit element in a 64-bit
///    integer vector to create an 8-bit mask value. Zero-extends the value to
///    32-bit integer and writes it to the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMOVMSKB </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing the values with bits to be extracted.
/// \returns The most significant bit from each 8-bit element in \a __a,
///    written to bits [7:0].
static __inline__ int __DEFAULT_FN_ATTRS_MMX
_mm_movemask_pi8(__m64 __a)
{
  return __builtin_ia32_pmovmskb((__v8qi)__a);
}

/// Multiplies packed 16-bit unsigned integer values and writes the
///    high-order 16 bits of each 32-bit product to the corresponding bits in
///    the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMULHUW </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing one of the source operands.
/// \param __b
///    A 64-bit integer vector containing one of the source operands.
/// \returns A 64-bit integer vector containing the products of both operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_mulhi_pu16(__m64 __a, __m64 __b)
{
  return (__m64)__builtin_ia32_pmulhuw((__v4hi)__a, (__v4hi)__b);
}

/// Shuffles the 4 16-bit integers from a 64-bit integer vector to the
///    destination, as specified by the immediate value operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m64 _mm_shuffle_pi16(__m64 a, const int n);
/// \endcode
///
/// This intrinsic corresponds to the <c> PSHUFW </c> instruction.
///
/// \param a
///    A 64-bit integer vector containing the values to be shuffled.
/// \param n
///    An immediate value containing an 8-bit value specifying which elements to
///    copy from \a a. The destinations within the 64-bit destination are
///    assigned values as follows: \n
///    Bits [1:0] are used to assign values to bits [15:0] in the
///    destination. \n
///    Bits [3:2] are used to assign values to bits [31:16] in the
///    destination. \n
///    Bits [5:4] are used to assign values to bits [47:32] in the
///    destination. \n
///    Bits [7:6] are used to assign values to bits [63:48] in the
///    destination. \n
///    Bit value assignments: \n
///    00: assigned from bits [15:0] of \a a. \n
///    01: assigned from bits [31:16] of \a a. \n
///    10: assigned from bits [47:32] of \a a. \n
///    11: assigned from bits [63:48] of \a a. \n
///    Note: To generate a mask, you can use the \c _MM_SHUFFLE macro.
///    <c>_MM_SHUFFLE(b6, b4, b2, b0)</c> can create an 8-bit mask of the form
///    <c>[b6, b4, b2, b0]</c>.
/// \returns A 64-bit integer vector containing the shuffled values.
#define _mm_shuffle_pi16(a, n) \
  ((__m64)__builtin_ia32_pshufw((__v4hi)(__m64)(a), (n)))

/// Conditionally copies the values from each 8-bit element in the first
///    64-bit integer vector operand to the specified memory location, as
///    specified by the most significant bit in the corresponding element in the
///    second 64-bit integer vector operand.
///
///    To minimize caching, the data is flagged as non-temporal
///    (unlikely to be used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MASKMOVQ </c> instruction.
///
/// \param __d
///    A 64-bit integer vector containing the values with elements to be copied.
/// \param __n
///    A 64-bit integer vector operand. The most significant bit from each 8-bit
///    element determines whether the corresponding element in operand \a __d
///    is copied. If the most significant bit of a given element is 1, the
///    corresponding element in operand \a __d is copied.
/// \param __p
///    A pointer to a 64-bit memory location that will receive the conditionally
///    copied integer values. The address of the memory location does not have
///    to be aligned.
static __inline__ void __DEFAULT_FN_ATTRS_MMX
_mm_maskmove_si64(__m64 __d, __m64 __n, char *__p)
{
  __builtin_ia32_maskmovq((__v8qi)__d, (__v8qi)__n, __p);
}

/// Computes the rounded averages of the packed unsigned 8-bit integer
///    values and writes the averages to the corresponding bits in the
///    destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PAVGB </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing one of the source operands.
/// \param __b
///    A 64-bit integer vector containing one of the source operands.
/// \returns A 64-bit integer vector containing the averages of both operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_avg_pu8(__m64 __a, __m64 __b)
{
  return (__m64)__builtin_ia32_pavgb((__v8qi)__a, (__v8qi)__b);
}

/// Computes the rounded averages of the packed unsigned 16-bit integer
///    values and writes the averages to the corresponding bits in the
///    destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PAVGW </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing one of the source operands.
/// \param __b
///    A 64-bit integer vector containing one of the source operands.
/// \returns A 64-bit integer vector containing the averages of both operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_avg_pu16(__m64 __a, __m64 __b)
{
  return (__m64)__builtin_ia32_pavgw((__v4hi)__a, (__v4hi)__b);
}

/// Subtracts the corresponding 8-bit unsigned integer values of the two
///    64-bit vector operands and computes the absolute value for each of the
///    difference. Then sum of the 8 absolute differences is written to the
///    bits [15:0] of the destination; the remaining bits [63:16] are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSADBW </c> instruction.
///
/// \param __a
///    A 64-bit integer vector containing one of the source operands.
/// \param __b
///    A 64-bit integer vector containing one of the source operands.
/// \returns A 64-bit integer vector whose lower 16 bits contain the sums of the
///    sets of absolute differences between both operands. The upper bits are
///    cleared.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_sad_pu8(__m64 __a, __m64 __b)
{
  return (__m64)__builtin_ia32_psadbw((__v8qi)__a, (__v8qi)__b);
}

#if defined(__cplusplus)
extern "C" {
#endif

/// Returns the contents of the MXCSR register as a 32-bit unsigned
///    integer value.
///
///    There are several groups of macros associated with this
///    intrinsic, including:
///    <ul>
///    <li>
///      For checking exception states: _MM_EXCEPT_INVALID, _MM_EXCEPT_DIV_ZERO,
///      _MM_EXCEPT_DENORM, _MM_EXCEPT_OVERFLOW, _MM_EXCEPT_UNDERFLOW,
///      _MM_EXCEPT_INEXACT. There is a convenience wrapper
///      _MM_GET_EXCEPTION_STATE().
///    </li>
///    <li>
///      For checking exception masks: _MM_MASK_UNDERFLOW, _MM_MASK_OVERFLOW,
///      _MM_MASK_INVALID, _MM_MASK_DENORM, _MM_MASK_DIV_ZERO, _MM_MASK_INEXACT.
///      There is a convenience wrapper _MM_GET_EXCEPTION_MASK().
///    </li>
///    <li>
///      For checking rounding modes: _MM_ROUND_NEAREST, _MM_ROUND_DOWN,
///      _MM_ROUND_UP, _MM_ROUND_TOWARD_ZERO. There is a convenience wrapper
///      _MM_GET_ROUNDING_MODE().
///    </li>
///    <li>
///      For checking flush-to-zero mode: _MM_FLUSH_ZERO_ON, _MM_FLUSH_ZERO_OFF.
///      There is a convenience wrapper _MM_GET_FLUSH_ZERO_MODE().
///    </li>
///    <li>
///      For checking denormals-are-zero mode: _MM_DENORMALS_ZERO_ON,
///      _MM_DENORMALS_ZERO_OFF. There is a convenience wrapper
///      _MM_GET_DENORMALS_ZERO_MODE().
///    </li>
///    </ul>
///
///    For example, the following expression checks if an overflow exception has
///    occurred:
///    \code
///      ( _mm_getcsr() & _MM_EXCEPT_OVERFLOW )
///    \endcode
///
///    The following expression gets the current rounding mode:
///    \code
///      _MM_GET_ROUNDING_MODE()
///    \endcode
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSTMXCSR / STMXCSR </c> instruction.
///
/// \returns A 32-bit unsigned integer containing the contents of the MXCSR
///    register.
unsigned int _mm_getcsr(void);

/// Sets the MXCSR register with the 32-bit unsigned integer value.
///
///    There are several groups of macros associated with this intrinsic,
///    including:
///    <ul>
///    <li>
///      For setting exception states: _MM_EXCEPT_INVALID, _MM_EXCEPT_DIV_ZERO,
///      _MM_EXCEPT_DENORM, _MM_EXCEPT_OVERFLOW, _MM_EXCEPT_UNDERFLOW,
///      _MM_EXCEPT_INEXACT. There is a convenience wrapper
///      _MM_SET_EXCEPTION_STATE(x) where x is one of these macros.
///    </li>
///    <li>
///      For setting exception masks: _MM_MASK_UNDERFLOW, _MM_MASK_OVERFLOW,
///      _MM_MASK_INVALID, _MM_MASK_DENORM, _MM_MASK_DIV_ZERO, _MM_MASK_INEXACT.
///      There is a convenience wrapper _MM_SET_EXCEPTION_MASK(x) where x is one
///      of these macros.
///    </li>
///    <li>
///      For setting rounding modes: _MM_ROUND_NEAREST, _MM_ROUND_DOWN,
///      _MM_ROUND_UP, _MM_ROUND_TOWARD_ZERO. There is a convenience wrapper
///      _MM_SET_ROUNDING_MODE(x) where x is one of these macros.
///    </li>
///    <li>
///      For setting flush-to-zero mode: _MM_FLUSH_ZERO_ON, _MM_FLUSH_ZERO_OFF.
///      There is a convenience wrapper _MM_SET_FLUSH_ZERO_MODE(x) where x is
///      one of these macros.
///    </li>
///    <li>
///      For setting denormals-are-zero mode: _MM_DENORMALS_ZERO_ON,
///      _MM_DENORMALS_ZERO_OFF. There is a convenience wrapper
///      _MM_SET_DENORMALS_ZERO_MODE(x) where x is one of these macros.
///    </li>
///    </ul>
///
///    For example, the following expression causes subsequent floating-point
///    operations to round up:
///      _mm_setcsr(_mm_getcsr() | _MM_ROUND_UP)
///
///    The following example sets the DAZ and FTZ flags:
///    \code
///    void setFlags() {
///      _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
///      _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
///    }
///    \endcode
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VLDMXCSR / LDMXCSR </c> instruction.
///
/// \param __i
///    A 32-bit unsigned integer value to be written to the MXCSR register.
void _mm_setcsr(unsigned int __i);

#if defined(__cplusplus)
} // extern "C"
#endif

/// Selects 4 float values from the 128-bit operands of [4 x float], as
///    specified by the immediate value operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_shuffle_ps(__m128 a, __m128 b, const int mask);
/// \endcode
///
/// This intrinsic corresponds to the <c> VSHUFPS / SHUFPS </c> instruction.
///
/// \param a
///    A 128-bit vector of [4 x float].
/// \param b
///    A 128-bit vector of [4 x float].
/// \param mask
///    An immediate value containing an 8-bit value specifying which elements to
///    copy from \a a and \a b. \n
///    Bits [3:0] specify the values copied from operand \a a. \n
///    Bits [7:4] specify the values copied from operand \a b. \n
///    The destinations within the 128-bit destination are assigned values as
///    follows: \n
///    Bits [1:0] are used to assign values to bits [31:0] in the
///    destination. \n
///    Bits [3:2] are used to assign values to bits [63:32] in the
///    destination. \n
///    Bits [5:4] are used to assign values to bits [95:64] in the
///    destination. \n
///    Bits [7:6] are used to assign values to bits [127:96] in the
///    destination. \n
///    Bit value assignments: \n
///    00: Bits [31:0] copied from the specified operand. \n
///    01: Bits [63:32] copied from the specified operand. \n
///    10: Bits [95:64] copied from the specified operand. \n
///    11: Bits [127:96] copied from the specified operand. \n
///    Note: To generate a mask, you can use the \c _MM_SHUFFLE macro.
///    <c>_MM_SHUFFLE(b6, b4, b2, b0)</c> can create an 8-bit mask of the form
///    <c>[b6, b4, b2, b0]</c>.
/// \returns A 128-bit vector of [4 x float] containing the shuffled values.
#define _mm_shuffle_ps(a, b, mask) \
  ((__m128)__builtin_ia32_shufps((__v4sf)(__m128)(a), (__v4sf)(__m128)(b), \
                                 (int)(mask)))

/// Unpacks the high-order (index 2,3) values from two 128-bit vectors of
///    [4 x float] and interleaves them into a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKHPS / UNPCKHPS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float]. \n
///    Bits [95:64] are written to bits [31:0] of the destination. \n
///    Bits [127:96] are written to bits [95:64] of the destination.
/// \param __b
///    A 128-bit vector of [4 x float].
///    Bits [95:64] are written to bits [63:32] of the destination. \n
///    Bits [127:96] are written to bits [127:96] of the destination.
/// \returns A 128-bit vector of [4 x float] containing the interleaved values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_unpackhi_ps(__m128 __a, __m128 __b)
{
  return __builtin_shufflevector((__v4sf)__a, (__v4sf)__b, 2, 6, 3, 7);
}

/// Unpacks the low-order (index 0,1) values from two 128-bit vectors of
///    [4 x float] and interleaves them into a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKLPS / UNPCKLPS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float]. \n
///    Bits [31:0] are written to bits [31:0] of the destination.  \n
///    Bits [63:32] are written to bits [95:64] of the destination.
/// \param __b
///    A 128-bit vector of [4 x float]. \n
///    Bits [31:0] are written to bits [63:32] of the destination. \n
///    Bits [63:32] are written to bits [127:96] of the destination.
/// \returns A 128-bit vector of [4 x float] containing the interleaved values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_unpacklo_ps(__m128 __a, __m128 __b)
{
  return __builtin_shufflevector((__v4sf)__a, (__v4sf)__b, 0, 4, 1, 5);
}

/// Constructs a 128-bit floating-point vector of [4 x float]. The lower
///    32 bits are set to the lower 32 bits of the second parameter. The upper
///    96 bits are set to the upper 96 bits of the first parameter.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBLENDPS / BLENDPS / MOVSS </c>
///    instruction.
///
/// \param __a
///    A 128-bit floating-point vector of [4 x float]. The upper 96 bits are
///    written to the upper 96 bits of the result.
/// \param __b
///    A 128-bit floating-point vector of [4 x float]. The lower 32 bits are
///    written to the lower 32 bits of the result.
/// \returns A 128-bit floating-point vector of [4 x float].
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_move_ss(__m128 __a, __m128 __b)
{
  __a[0] = __b[0];
  return __a;
}

/// Constructs a 128-bit floating-point vector of [4 x float]. The lower
///    64 bits are set to the upper 64 bits of the second parameter. The upper
///    64 bits are set to the upper 64 bits of the first parameter.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKHPD / UNPCKHPD </c> instruction.
///
/// \param __a
///    A 128-bit floating-point vector of [4 x float]. The upper 64 bits are
///    written to the upper 64 bits of the result.
/// \param __b
///    A 128-bit floating-point vector of [4 x float]. The upper 64 bits are
///    written to the lower 64 bits of the result.
/// \returns A 128-bit floating-point vector of [4 x float].
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_movehl_ps(__m128 __a, __m128 __b)
{
  return __builtin_shufflevector((__v4sf)__a, (__v4sf)__b, 6, 7, 2, 3);
}

/// Constructs a 128-bit floating-point vector of [4 x float]. The lower
///    64 bits are set to the lower 64 bits of the first parameter. The upper
///    64 bits are set to the lower 64 bits of the second parameter.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKLPD / UNPCKLPD </c> instruction.
///
/// \param __a
///    A 128-bit floating-point vector of [4 x float]. The lower 64 bits are
///    written to the lower 64 bits of the result.
/// \param __b
///    A 128-bit floating-point vector of [4 x float]. The lower 64 bits are
///    written to the upper 64 bits of the result.
/// \returns A 128-bit floating-point vector of [4 x float].
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_movelh_ps(__m128 __a, __m128 __b)
{
  return __builtin_shufflevector((__v4sf)__a, (__v4sf)__b, 0, 1, 4, 5);
}

/// Converts a 64-bit vector of [4 x i16] into a 128-bit vector of [4 x
///    float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPI2PS + COMPOSITE </c> instruction.
///
/// \param __a
///    A 64-bit vector of [4 x i16]. The elements of the destination are copied
///    from the corresponding elements in this operand.
/// \returns A 128-bit vector of [4 x float] containing the copied and converted
///    values from the operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS_MMX
_mm_cvtpi16_ps(__m64 __a)
{
  __m64 __b, __c;
  __m128 __r;

  __b = _mm_setzero_si64();
  __b = _mm_cmpgt_pi16(__b, __a);
  __c = _mm_unpackhi_pi16(__a, __b);
  __r = _mm_setzero_ps();
  __r = _mm_cvtpi32_ps(__r, __c);
  __r = _mm_movelh_ps(__r, __r);
  __c = _mm_unpacklo_pi16(__a, __b);
  __r = _mm_cvtpi32_ps(__r, __c);

  return __r;
}

/// Converts a 64-bit vector of 16-bit unsigned integer values into a
///    128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPI2PS + COMPOSITE </c> instruction.
///
/// \param __a
///    A 64-bit vector of 16-bit unsigned integer values. The elements of the
///    destination are copied from the corresponding elements in this operand.
/// \returns A 128-bit vector of [4 x float] containing the copied and converted
///    values from the operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS_MMX
_mm_cvtpu16_ps(__m64 __a)
{
  __m64 __b, __c;
  __m128 __r;

  __b = _mm_setzero_si64();
  __c = _mm_unpackhi_pi16(__a, __b);
  __r = _mm_setzero_ps();
  __r = _mm_cvtpi32_ps(__r, __c);
  __r = _mm_movelh_ps(__r, __r);
  __c = _mm_unpacklo_pi16(__a, __b);
  __r = _mm_cvtpi32_ps(__r, __c);

  return __r;
}

/// Converts the lower four 8-bit values from a 64-bit vector of [8 x i8]
///    into a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPI2PS + COMPOSITE </c> instruction.
///
/// \param __a
///    A 64-bit vector of [8 x i8]. The elements of the destination are copied
///    from the corresponding lower 4 elements in this operand.
/// \returns A 128-bit vector of [4 x float] containing the copied and converted
///    values from the operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS_MMX
_mm_cvtpi8_ps(__m64 __a)
{
  __m64 __b;

  __b = _mm_setzero_si64();
  __b = _mm_cmpgt_pi8(__b, __a);
  __b = _mm_unpacklo_pi8(__a, __b);

  return _mm_cvtpi16_ps(__b);
}

/// Converts the lower four unsigned 8-bit integer values from a 64-bit
///    vector of [8 x u8] into a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPI2PS + COMPOSITE </c> instruction.
///
/// \param __a
///    A 64-bit vector of unsigned 8-bit integer values. The elements of the
///    destination are copied from the corresponding lower 4 elements in this
///    operand.
/// \returns A 128-bit vector of [4 x float] containing the copied and converted
///    values from the source operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS_MMX
_mm_cvtpu8_ps(__m64 __a)
{
  __m64 __b;

  __b = _mm_setzero_si64();
  __b = _mm_unpacklo_pi8(__a, __b);

  return _mm_cvtpi16_ps(__b);
}

/// Converts the two 32-bit signed integer values from each 64-bit vector
///    operand of [2 x i32] into a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPI2PS + COMPOSITE </c> instruction.
///
/// \param __a
///    A 64-bit vector of [2 x i32]. The lower elements of the destination are
///    copied from the elements in this operand.
/// \param __b
///    A 64-bit vector of [2 x i32]. The upper elements of the destination are
///    copied from the elements in this operand.
/// \returns A 128-bit vector of [4 x float] whose lower 64 bits contain the
///    copied and converted values from the first operand. The upper 64 bits
///    contain the copied and converted values from the second operand.
static __inline__ __m128 __DEFAULT_FN_ATTRS_MMX
_mm_cvtpi32x2_ps(__m64 __a, __m64 __b)
{
  __m128 __c;

  __c = _mm_setzero_ps();
  __c = _mm_cvtpi32_ps(__c, __b);
  __c = _mm_movelh_ps(__c, __c);

  return _mm_cvtpi32_ps(__c, __a);
}

/// Converts each single-precision floating-point element of a 128-bit
///    floating-point vector of [4 x float] into a 16-bit signed integer, and
///    packs the results into a 64-bit integer vector of [4 x i16].
///
///    If the floating-point element is NaN or infinity, or if the
///    floating-point element is greater than 0x7FFFFFFF or less than -0x8000,
///    it is converted to 0x8000. Otherwise if the floating-point element is
///    greater than 0x7FFF, it is converted to 0x7FFF.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPS2PI + COMPOSITE </c> instruction.
///
/// \param __a
///    A 128-bit floating-point vector of [4 x float].
/// \returns A 64-bit integer vector of [4 x i16] containing the converted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_cvtps_pi16(__m128 __a)
{
  __m64 __b, __c;

  __b = _mm_cvtps_pi32(__a);
  __a = _mm_movehl_ps(__a, __a);
  __c = _mm_cvtps_pi32(__a);

  return _mm_packs_pi32(__b, __c);
}

/// Converts each single-precision floating-point element of a 128-bit
///    floating-point vector of [4 x float] into an 8-bit signed integer, and
///    packs the results into the lower 32 bits of a 64-bit integer vector of
///    [8 x i8]. The upper 32 bits of the vector are set to 0.
///
///    If the floating-point element is NaN or infinity, or if the
///    floating-point element is greater than 0x7FFFFFFF or less than -0x80, it
///    is converted to 0x80. Otherwise if the floating-point element is greater
///    than 0x7F, it is converted to 0x7F.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CVTPS2PI + COMPOSITE </c> instruction.
///
/// \param __a
///    128-bit floating-point vector of [4 x float].
/// \returns A 64-bit integer vector of [8 x i8]. The lower 32 bits contain the
///    converted values and the uppper 32 bits are set to zero.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_cvtps_pi8(__m128 __a)
{
  __m64 __b, __c;

  __b = _mm_cvtps_pi16(__a);
  __c = _mm_setzero_si64();

  return _mm_packs_pi16(__b, __c);
}

/// Extracts the sign bits from each single-precision floating-point
///    element of a 128-bit floating-point vector of [4 x float] and returns the
///    sign bits in bits [0:3] of the result. Bits [31:4] of the result are set
///    to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVMSKPS / MOVMSKPS </c> instruction.
///
/// \param __a
///    A 128-bit floating-point vector of [4 x float].
/// \returns A 32-bit integer value. Bits [3:0] contain the sign bits from each
///    single-precision floating-point element of the parameter. Bits [31:4] are
///    set to zero.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_movemask_ps(__m128 __a)
{
  return __builtin_ia32_movmskps((__v4sf)__a);
}

/* Compare */
#define _CMP_EQ_OQ    0x00 /* Equal (ordered, non-signaling)  */
#define _CMP_LT_OS    0x01 /* Less-than (ordered, signaling)  */
#define _CMP_LE_OS    0x02 /* Less-than-or-equal (ordered, signaling)  */
#define _CMP_UNORD_Q  0x03 /* Unordered (non-signaling)  */
#define _CMP_NEQ_UQ   0x04 /* Not-equal (unordered, non-signaling)  */
#define _CMP_NLT_US   0x05 /* Not-less-than (unordered, signaling)  */
#define _CMP_NLE_US   0x06 /* Not-less-than-or-equal (unordered, signaling)  */
#define _CMP_ORD_Q    0x07 /* Ordered (non-signaling)   */

/// Compares each of the corresponding values of two 128-bit vectors of
///    [4 x float], using the operation specified by the immediate integer
///    operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, comparisons that are ordered
///    return false, and comparisons that are unordered return true.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_cmp_ps(__m128 a, __m128 b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> (V)CMPPS </c> instruction.
///
/// \param a
///    A 128-bit vector of [4 x float].
/// \param b
///    A 128-bit vector of [4 x float].
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
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
#define _mm_cmp_ps(a, b, c)                                                    \
  ((__m128)__builtin_ia32_cmpps((__v4sf)(__m128)(a), (__v4sf)(__m128)(b), (c)))

/// Compares each of the corresponding scalar values of two 128-bit
///    vectors of [4 x float], using the operation specified by the immediate
///    integer operand.
///
///    Each comparison returns 0x0 for false, 0xFFFFFFFF for true.
///    If either value in a comparison is NaN, comparisons that are ordered
///    return false, and comparisons that are unordered return true.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_cmp_ss(__m128 a, __m128 b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> (V)CMPSS </c> instruction.
///
/// \param a
///    A 128-bit vector of [4 x float].
/// \param b
///    A 128-bit vector of [4 x float].
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
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
#define _mm_cmp_ss(a, b, c)                                                    \
  ((__m128)__builtin_ia32_cmpss((__v4sf)(__m128)(a), (__v4sf)(__m128)(b), (c)))

#define _MM_ALIGN16 __attribute__((aligned(16)))

#define _MM_SHUFFLE(z, y, x, w) (((z) << 6) | ((y) << 4) | ((x) << 2) | (w))

#define _MM_EXCEPT_INVALID    (0x0001U)
#define _MM_EXCEPT_DENORM     (0x0002U)
#define _MM_EXCEPT_DIV_ZERO   (0x0004U)
#define _MM_EXCEPT_OVERFLOW   (0x0008U)
#define _MM_EXCEPT_UNDERFLOW  (0x0010U)
#define _MM_EXCEPT_INEXACT    (0x0020U)
#define _MM_EXCEPT_MASK       (0x003fU)

#define _MM_MASK_INVALID      (0x0080U)
#define _MM_MASK_DENORM       (0x0100U)
#define _MM_MASK_DIV_ZERO     (0x0200U)
#define _MM_MASK_OVERFLOW     (0x0400U)
#define _MM_MASK_UNDERFLOW    (0x0800U)
#define _MM_MASK_INEXACT      (0x1000U)
#define _MM_MASK_MASK         (0x1f80U)

#define _MM_ROUND_NEAREST     (0x0000U)
#define _MM_ROUND_DOWN        (0x2000U)
#define _MM_ROUND_UP          (0x4000U)
#define _MM_ROUND_TOWARD_ZERO (0x6000U)
#define _MM_ROUND_MASK        (0x6000U)

#define _MM_FLUSH_ZERO_MASK   (0x8000U)
#define _MM_FLUSH_ZERO_ON     (0x8000U)
#define _MM_FLUSH_ZERO_OFF    (0x0000U)

#define _MM_GET_EXCEPTION_MASK() (_mm_getcsr() & _MM_MASK_MASK)
#define _MM_GET_EXCEPTION_STATE() (_mm_getcsr() & _MM_EXCEPT_MASK)
#define _MM_GET_FLUSH_ZERO_MODE() (_mm_getcsr() & _MM_FLUSH_ZERO_MASK)
#define _MM_GET_ROUNDING_MODE() (_mm_getcsr() & _MM_ROUND_MASK)

#define _MM_SET_EXCEPTION_MASK(x) (_mm_setcsr((_mm_getcsr() & ~_MM_MASK_MASK) | (x)))
#define _MM_SET_EXCEPTION_STATE(x) (_mm_setcsr((_mm_getcsr() & ~_MM_EXCEPT_MASK) | (x)))
#define _MM_SET_FLUSH_ZERO_MODE(x) (_mm_setcsr((_mm_getcsr() & ~_MM_FLUSH_ZERO_MASK) | (x)))
#define _MM_SET_ROUNDING_MODE(x) (_mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | (x)))

#define _MM_TRANSPOSE4_PS(row0, row1, row2, row3) \
do { \
  __m128 tmp3, tmp2, tmp1, tmp0; \
  tmp0 = _mm_unpacklo_ps((row0), (row1)); \
  tmp2 = _mm_unpacklo_ps((row2), (row3)); \
  tmp1 = _mm_unpackhi_ps((row0), (row1)); \
  tmp3 = _mm_unpackhi_ps((row2), (row3)); \
  (row0) = _mm_movelh_ps(tmp0, tmp2); \
  (row1) = _mm_movehl_ps(tmp2, tmp0); \
  (row2) = _mm_movelh_ps(tmp1, tmp3); \
  (row3) = _mm_movehl_ps(tmp3, tmp1); \
} while (0)

/* Aliases for compatibility. */
#define _m_pextrw _mm_extract_pi16
#define _m_pinsrw _mm_insert_pi16
#define _m_pmaxsw _mm_max_pi16
#define _m_pmaxub _mm_max_pu8
#define _m_pminsw _mm_min_pi16
#define _m_pminub _mm_min_pu8
#define _m_pmovmskb _mm_movemask_pi8
#define _m_pmulhuw _mm_mulhi_pu16
#define _m_pshufw _mm_shuffle_pi16
#define _m_maskmovq _mm_maskmove_si64
#define _m_pavgb _mm_avg_pu8
#define _m_pavgw _mm_avg_pu16
#define _m_psadbw _mm_sad_pu8
#define _m_ _mm_

#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS_MMX

/* Ugly hack for backwards-compatibility (compatible with gcc) */
#if defined(__SSE2__) && !__building_module(_Builtin_intrinsics)
#include <emmintrin.h>
#endif

#endif /* __XMMINTRIN_H */
