/*===---- fmaintrin.h - FMA intrinsics -------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <fmaintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __FMAINTRIN_H
#define __FMAINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128 __attribute__((__always_inline__, __nodebug__, __target__("fma"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256 __attribute__((__always_inline__, __nodebug__, __target__("fma"), __min_vector_width__(256)))

/// Computes a multiply-add of 128-bit vectors of [4 x float].
///    For each element, computes <c> (__A * __B) + __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADD213PS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier.
/// \param __C
///    A 128-bit vector of [4 x float] containing the addend.
/// \returns A 128-bit vector of [4 x float] containing the result.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fmadd_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddps((__v4sf)__A, (__v4sf)__B, (__v4sf)__C);
}

/// Computes a multiply-add of 128-bit vectors of [2 x double].
///    For each element, computes <c> (__A * __B) + __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADD213PD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier.
/// \param __C
///    A 128-bit vector of [2 x double] containing the addend.
/// \returns A 128-bit [2 x double] vector containing the result.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fmadd_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddpd((__v2df)__A, (__v2df)__B, (__v2df)__C);
}

/// Computes a scalar multiply-add of the single-precision values in the
///    low 32 bits of 128-bit vectors of [4 x float].
///
/// \code{.operation}
/// result[31:0] = (__A[31:0] * __B[31:0]) + __C[31:0]
/// result[127:32] = __A[127:32]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADD213SS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand in the low
///    32 bits.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier in the low
///    32 bits.
/// \param __C
///    A 128-bit vector of [4 x float] containing the addend in the low
///    32 bits.
/// \returns A 128-bit vector of [4 x float] containing the result in the low
///    32 bits and a copy of \a __A[127:32] in the upper 96 bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fmadd_ss(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddss3((__v4sf)__A, (__v4sf)__B, (__v4sf)__C);
}

/// Computes a scalar multiply-add of the double-precision values in the
///    low 64 bits of 128-bit vectors of [2 x double].
///
/// \code{.operation}
/// result[63:0] = (__A[63:0] * __B[63:0]) + __C[63:0]
/// result[127:64] = __A[127:64]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADD213SD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand in the low
///    64 bits.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier in the low
///    64 bits.
/// \param __C
///    A 128-bit vector of [2 x double] containing the addend in the low
///    64 bits.
/// \returns A 128-bit vector of [2 x double] containing the result in the low
///    64 bits and a copy of \a __A[127:64] in the upper 64 bits.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fmadd_sd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsd3((__v2df)__A, (__v2df)__B, (__v2df)__C);
}

/// Computes a multiply-subtract of 128-bit vectors of [4 x float].
///    For each element, computes <c> (__A * __B) - __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMSUB213PS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier.
/// \param __C
///    A 128-bit vector of [4 x float] containing the subtrahend.
/// \returns A 128-bit vector of [4 x float] containing the result.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fmsub_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddps((__v4sf)__A, (__v4sf)__B, -(__v4sf)__C);
}

/// Computes a multiply-subtract of 128-bit vectors of [2 x double].
///    For each element, computes <c> (__A * __B) - __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMSUB213PD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier.
/// \param __C
///    A 128-bit vector of [2 x double] containing the addend.
/// \returns A 128-bit vector of [2 x double] containing the result.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fmsub_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddpd((__v2df)__A, (__v2df)__B, -(__v2df)__C);
}

/// Computes a scalar multiply-subtract of the single-precision values in
///    the low 32 bits of 128-bit vectors of [4 x float].
///
/// \code{.operation}
/// result[31:0] = (__A[31:0] * __B[31:0]) - __C[31:0]
/// result[127:32] = __A[127:32]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMSUB213SS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand in the low
///    32 bits.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier in the low
///    32 bits.
/// \param __C
///    A 128-bit vector of [4 x float] containing the subtrahend in the low
///   32 bits.
/// \returns A 128-bit vector of [4 x float] containing the result in the low
///    32 bits, and a copy of \a __A[127:32] in the upper 96 bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fmsub_ss(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddss3((__v4sf)__A, (__v4sf)__B, -(__v4sf)__C);
}

/// Computes a scalar multiply-subtract of the double-precision values in
///    the low 64 bits of 128-bit vectors of [2 x double].
///
/// \code{.operation}
/// result[63:0] = (__A[63:0] * __B[63:0]) - __C[63:0]
/// result[127:64] = __A[127:64]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMSUB213SD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand in the low
///    64 bits.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier in the low
///    64 bits.
/// \param __C
///    A 128-bit vector of [2 x double] containing the subtrahend in the low
///    64 bits.
/// \returns A 128-bit vector of [2 x double] containing the result in the low
///    64 bits, and a copy of \a __A[127:64] in the upper 64 bits.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fmsub_sd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsd3((__v2df)__A, (__v2df)__B, -(__v2df)__C);
}

/// Computes a negated multiply-add of 128-bit vectors of [4 x float].
///    For each element, computes <c> -(__A * __B) + __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMADD213DPS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier.
/// \param __C
///    A 128-bit vector of [4 x float] containing the addend.
/// \returns A 128-bit [4 x float] vector containing the result.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fnmadd_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddps(-(__v4sf)__A, (__v4sf)__B, (__v4sf)__C);
}

/// Computes a negated multiply-add of 128-bit vectors of [2 x double].
///    For each element, computes <c> -(__A * __B) + __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMADD213PD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier.
/// \param __C
///    A 128-bit vector of [2 x double] containing the addend.
/// \returns A 128-bit vector of [2 x double] containing the result.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fnmadd_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddpd(-(__v2df)__A, (__v2df)__B, (__v2df)__C);
}

/// Computes a scalar negated multiply-add of the single-precision values in
///    the low 32 bits of 128-bit vectors of [4 x float].
///
/// \code{.operation}
/// result[31:0] = -(__A[31:0] * __B[31:0]) + __C[31:0]
/// result[127:32] = __A[127:32]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMADD213SS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand in the low
///    32 bits.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier in the low
///    32 bits.
/// \param __C
///    A 128-bit vector of [4 x float] containing the addend in the low
///    32 bits.
/// \returns A 128-bit vector of [4 x float] containing the result in the low
///    32 bits, and a copy of \a __A[127:32] in the upper 96 bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fnmadd_ss(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddss3((__v4sf)__A, -(__v4sf)__B, (__v4sf)__C);
}

/// Computes a scalar negated multiply-add of the double-precision values
///    in the low 64 bits of 128-bit vectors of [2 x double].
///
/// \code{.operation}
/// result[63:0] = -(__A[63:0] * __B[63:0]) + __C[63:0]
/// result[127:64] = __A[127:64]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMADD213SD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand in the low
///    64 bits.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier in the low
///    64 bits.
/// \param __C
///    A 128-bit vector of [2 x double] containing the addend in the low
///    64 bits.
/// \returns A 128-bit vector of [2 x double] containing the result in the low
///    64 bits, and a copy of \a __A[127:64] in the upper 64 bits.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fnmadd_sd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsd3((__v2df)__A, -(__v2df)__B, (__v2df)__C);
}

/// Computes a negated multiply-subtract of 128-bit vectors of [4 x float].
///    For each element, computes <c> -(__A * __B) - __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMSUB213PS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier.
/// \param __C
///    A 128-bit vector of [4 x float] containing the subtrahend.
/// \returns A 128-bit vector of [4 x float] containing the result.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fnmsub_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddps(-(__v4sf)__A, (__v4sf)__B, -(__v4sf)__C);
}

/// Computes a negated multiply-subtract of 128-bit vectors of [2 x double].
///    For each element, computes <c> -(__A * __B) - __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMSUB213PD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier.
/// \param __C
///    A 128-bit vector of [2 x double] containing the subtrahend.
/// \returns A 128-bit vector of [2 x double] containing the result.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fnmsub_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddpd(-(__v2df)__A, (__v2df)__B, -(__v2df)__C);
}

/// Computes a scalar negated multiply-subtract of the single-precision
///    values in the low 32 bits of 128-bit vectors of [4 x float].
///
/// \code{.operation}
/// result[31:0] = -(__A[31:0] * __B[31:0]) - __C[31:0]
/// result[127:32] = __A[127:32]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMSUB213SS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand in the low
///    32 bits.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier in the low
///    32 bits.
/// \param __C
///    A 128-bit vector of [4 x float] containing the subtrahend in the low
///    32 bits.
/// \returns A 128-bit vector of [4 x float] containing the result in the low
///    32 bits, and a copy of \a __A[127:32] in the upper 96 bits.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fnmsub_ss(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddss3((__v4sf)__A, -(__v4sf)__B, -(__v4sf)__C);
}

/// Computes a scalar negated multiply-subtract of the double-precision
///    values in the low 64 bits of 128-bit vectors of [2 x double].
///
/// \code{.operation}
/// result[63:0] = -(__A[63:0] * __B[63:0]) - __C[63:0]
/// result[127:64] = __A[127:64]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMSUB213SD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand in the low
///    64 bits.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier in the low
///    64 bits.
/// \param __C
///    A 128-bit vector of [2 x double] containing the subtrahend in the low
///    64 bits.
/// \returns A 128-bit vector of [2 x double] containing the result in the low
///    64 bits, and a copy of \a __A[127:64] in the upper 64 bits.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fnmsub_sd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsd3((__v2df)__A, -(__v2df)__B, -(__v2df)__C);
}

/// Computes a multiply with alternating add/subtract of 128-bit vectors of
///    [4 x float].
///
/// \code{.operation}
/// result[31:0]  = (__A[31:0] * __B[31:0]) - __C[31:0]
/// result[63:32] = (__A[63:32] * __B[63:32]) + __C[63:32]
/// result[95:64] = (__A[95:64] * __B[95:64]) - __C[95:64]
/// result[127:96] = (__A[127:96] * __B[127:96]) + __C[127:96]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADDSUB213PS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier.
/// \param __C
///    A 128-bit vector of [4 x float] containing the addend/subtrahend.
/// \returns A 128-bit vector of [4 x float] containing the result.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fmaddsub_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddsubps((__v4sf)__A, (__v4sf)__B, (__v4sf)__C);
}

/// Computes a multiply with alternating add/subtract of 128-bit vectors of
///    [2 x double].
///
/// \code{.operation}
/// result[63:0]  = (__A[63:0] * __B[63:0]) - __C[63:0]
/// result[127:64] = (__A[127:64] * __B[127:64]) + __C[127:64]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADDSUB213PD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier.
/// \param __C
///    A 128-bit vector of [2 x double] containing the addend/subtrahend.
/// \returns A 128-bit vector of [2 x double] containing the result.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fmaddsub_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsubpd((__v2df)__A, (__v2df)__B, (__v2df)__C);
}

/// Computes a multiply with alternating add/subtract of 128-bit vectors of
///    [4 x float].
///
/// \code{.operation}
/// result[31:0]  = (__A[31:0] * __B[31:0]) + __C[31:0]
/// result[63:32] = (__A[63:32] * __B[63:32]) - __C[63:32]
/// result[95:64] = (__A[95:64] * __B[95:64]) + __C[95:64]
/// result[127:96 = (__A[127:96] * __B[127:96]) - __C[127:96]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMSUBADD213PS instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [4 x float] containing the multiplier.
/// \param __C
///    A 128-bit vector of [4 x float] containing the addend/subtrahend.
/// \returns A 128-bit vector of [4 x float] containing the result.
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_fmsubadd_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddsubps((__v4sf)__A, (__v4sf)__B, -(__v4sf)__C);
}

/// Computes a multiply with alternating add/subtract of 128-bit vectors of
///    [2 x double].
///
/// \code{.operation}
/// result[63:0]  = (__A[63:0] * __B[63:0]) + __C[63:0]
/// result[127:64] = (__A[127:64] * __B[127:64]) - __C[127:64]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADDSUB213PD instruction.
///
/// \param __A
///    A 128-bit vector of [2 x double] containing the multiplicand.
/// \param __B
///    A 128-bit vector of [2 x double] containing the multiplier.
/// \param __C
///    A 128-bit vector of [2 x double] containing the addend/subtrahend.
/// \returns A 128-bit vector of [2 x double] containing the result.
static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_fmsubadd_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsubpd((__v2df)__A, (__v2df)__B, -(__v2df)__C);
}

/// Computes a multiply-add of 256-bit vectors of [8 x float].
///    For each element, computes <c> (__A * __B) + __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADD213PS instruction.
///
/// \param __A
///    A 256-bit vector of [8 x float] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [8 x float] containing the multiplier.
/// \param __C
///    A 256-bit vector of [8 x float] containing the addend.
/// \returns A 256-bit vector of [8 x float] containing the result.
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_fmadd_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddps256((__v8sf)__A, (__v8sf)__B, (__v8sf)__C);
}

/// Computes a multiply-add of 256-bit vectors of [4 x double].
///    For each element, computes <c> (__A * __B) + __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADD213PD instruction.
///
/// \param __A
///    A 256-bit vector of [4 x double] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [4 x double] containing the multiplier.
/// \param __C
///    A 256-bit vector of [4 x double] containing the addend.
/// \returns A 256-bit vector of [4 x double] containing the result.
static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_fmadd_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddpd256((__v4df)__A, (__v4df)__B, (__v4df)__C);
}

/// Computes a multiply-subtract of 256-bit vectors of [8 x float].
///    For each element, computes <c> (__A * __B) - __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMSUB213PS instruction.
///
/// \param __A
///    A 256-bit vector of [8 x float] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [8 x float] containing the multiplier.
/// \param __C
///    A 256-bit vector of [8 x float] containing the subtrahend.
/// \returns A 256-bit vector of [8 x float] containing the result.
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_fmsub_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddps256((__v8sf)__A, (__v8sf)__B, -(__v8sf)__C);
}

/// Computes a multiply-subtract of 256-bit vectors of [4 x double].
///    For each element, computes <c> (__A * __B) - __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMSUB213PD instruction.
///
/// \param __A
///    A 256-bit vector of [4 x double] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [4 x double] containing the multiplier.
/// \param __C
///    A 256-bit vector of [4 x double] containing the subtrahend.
/// \returns A 256-bit vector of [4 x double] containing the result.
static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_fmsub_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddpd256((__v4df)__A, (__v4df)__B, -(__v4df)__C);
}

/// Computes a negated multiply-add of 256-bit vectors of [8 x float].
///    For each element, computes <c> -(__A * __B) + __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMADD213PS instruction.
///
/// \param __A
///    A 256-bit vector of [8 x float] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [8 x float] containing the multiplier.
/// \param __C
///    A 256-bit vector of [8 x float] containing the addend.
/// \returns A 256-bit vector of [8 x float] containing the result.
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_fnmadd_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddps256(-(__v8sf)__A, (__v8sf)__B, (__v8sf)__C);
}

/// Computes a negated multiply-add of 256-bit vectors of [4 x double].
///    For each element, computes <c> -(__A * __B) + __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMADD213PD instruction.
///
/// \param __A
///    A 256-bit vector of [4 x double] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [4 x double] containing the multiplier.
/// \param __C
///    A 256-bit vector of [4 x double] containing the addend.
/// \returns A 256-bit vector of [4 x double] containing the result.
static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_fnmadd_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddpd256(-(__v4df)__A, (__v4df)__B, (__v4df)__C);
}

/// Computes a negated multiply-subtract of 256-bit vectors of [8 x float].
///    For each element, computes <c> -(__A * __B) - __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMSUB213PS instruction.
///
/// \param __A
///    A 256-bit vector of [8 x float] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [8 x float] containing the multiplier.
/// \param __C
///    A 256-bit vector of [8 x float] containing the subtrahend.
/// \returns A 256-bit vector of [8 x float] containing the result.
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_fnmsub_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddps256(-(__v8sf)__A, (__v8sf)__B, -(__v8sf)__C);
}

/// Computes a negated multiply-subtract of 256-bit vectors of [4 x double].
///    For each element, computes <c> -(__A * __B) - __C </c>.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFNMSUB213PD instruction.
///
/// \param __A
///    A 256-bit vector of [4 x double] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [4 x double] containing the multiplier.
/// \param __C
///    A 256-bit vector of [4 x double] containing the subtrahend.
/// \returns A 256-bit vector of [4 x double] containing the result.
static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_fnmsub_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddpd256(-(__v4df)__A, (__v4df)__B, -(__v4df)__C);
}

/// Computes a multiply with alternating add/subtract of 256-bit vectors of
///    [8 x float].
///
/// \code{.operation}
/// result[31:0] = (__A[31:0] * __B[31:0]) - __C[31:0]
/// result[63:32] = (__A[63:32] * __B[63:32]) + __C[63:32]
/// result[95:64] = (__A[95:64] * __B[95:64]) - __C[95:64]
/// result[127:96] = (__A[127:96] * __B[127:96]) + __C[127:96]
/// result[159:128] = (__A[159:128] * __B[159:128]) - __C[159:128]
/// result[191:160] = (__A[191:160] * __B[191:160]) + __C[191:160]
/// result[223:192] = (__A[223:192] * __B[223:192]) - __C[223:192]
/// result[255:224] = (__A[255:224] * __B[255:224]) + __C[255:224]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADDSUB213PS instruction.
///
/// \param __A
///    A 256-bit vector of [8 x float] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [8 x float] containing the multiplier.
/// \param __C
///    A 256-bit vector of [8 x float] containing the addend/subtrahend.
/// \returns A 256-bit vector of [8 x float] containing the result.
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_fmaddsub_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddsubps256((__v8sf)__A, (__v8sf)__B, (__v8sf)__C);
}

/// Computes a multiply with alternating add/subtract of 256-bit vectors of
///    [4 x double].
///
/// \code{.operation}
/// result[63:0] = (__A[63:0] * __B[63:0]) - __C[63:0]
/// result[127:64] = (__A[127:64] * __B[127:64]) + __C[127:64]
/// result[191:128] = (__A[191:128] * __B[191:128]) - __C[191:128]
/// result[255:192] = (__A[255:192] * __B[255:192]) + __C[255:192]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMADDSUB213PD instruction.
///
/// \param __A
///    A 256-bit vector of [4 x double] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [4 x double] containing the multiplier.
/// \param __C
///    A 256-bit vector of [4 x double] containing the addend/subtrahend.
/// \returns A 256-bit vector of [4 x double] containing the result.
static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_fmaddsub_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddsubpd256((__v4df)__A, (__v4df)__B, (__v4df)__C);
}

/// Computes a vector multiply with alternating add/subtract of 256-bit
///    vectors of [8 x float].
///
/// \code{.operation}
/// result[31:0] = (__A[31:0] * __B[31:0]) + __C[31:0]
/// result[63:32] = (__A[63:32] * __B[63:32]) - __C[63:32]
/// result[95:64] = (__A[95:64] * __B[95:64]) + __C[95:64]
/// result[127:96] = (__A[127:96] * __B[127:96]) - __C[127:96]
/// result[159:128] = (__A[159:128] * __B[159:128]) + __C[159:128]
/// result[191:160] = (__A[191:160] * __B[191:160]) - __C[191:160]
/// result[223:192] = (__A[223:192] * __B[223:192]) + __C[223:192]
/// result[255:224] = (__A[255:224] * __B[255:224]) - __C[255:224]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMSUBADD213PS instruction.
///
/// \param __A
///    A 256-bit vector of [8 x float] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [8 x float] containing the multiplier.
/// \param __C
///    A 256-bit vector of [8 x float] containing the addend/subtrahend.
/// \returns A 256-bit vector of [8 x float] containing the result.
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_fmsubadd_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddsubps256((__v8sf)__A, (__v8sf)__B, -(__v8sf)__C);
}

/// Computes a vector multiply with alternating add/subtract of 256-bit
///    vectors of [4 x double].
///
/// \code{.operation}
/// result[63:0] = (__A[63:0] * __B[63:0]) + __C[63:0]
/// result[127:64] = (__A[127:64] * __B[127:64]) - __C[127:64]
/// result[191:128] = (__A[191:128] * __B[191:128]) + __C[191:128]
/// result[255:192] = (__A[255:192] * __B[255:192]) - __C[255:192]
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c VFMSUBADD213PD instruction.
///
/// \param __A
///    A 256-bit vector of [4 x double] containing the multiplicand.
/// \param __B
///    A 256-bit vector of [4 x double] containing the multiplier.
/// \param __C
///    A 256-bit vector of [4 x double] containing the addend/subtrahend.
/// \returns A 256-bit vector of [4 x double] containing the result.
static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_fmsubadd_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddsubpd256((__v4df)__A, (__v4df)__B, -(__v4df)__C);
}

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif /* __FMAINTRIN_H */
