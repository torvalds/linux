/*===-------------- avxneconvertintrin.h - AVXNECONVERT --------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error                                                                         \
    "Never use <avxneconvertintrin.h> directly; include <immintrin.h> instead."
#endif // __IMMINTRIN_H

#ifdef __SSE2__

#ifndef __AVXNECONVERTINTRIN_H
#define __AVXNECONVERTINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128                                                  \
  __attribute__((__always_inline__, __nodebug__, __target__("avxneconvert"),   \
                 __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256                                                  \
  __attribute__((__always_inline__, __nodebug__, __target__("avxneconvert"),   \
                 __min_vector_width__(256)))

/// Convert scalar BF16 (16-bit) floating-point element
/// stored at memory locations starting at location \a __A to a
/// single-precision (32-bit) floating-point, broadcast it to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm_bcstnebf16_ps(const void *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VBCSTNEBF162PS instruction.
///
/// \param __A
///    A pointer to a 16-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \returns
///    A 128-bit vector of [4 x float].
///
/// \code{.operation}
/// b := Convert_BF16_To_FP32(MEM[__A+15:__A])
/// FOR j := 0 to 3
///   m := j*32
///   dst[m+31:m] := b
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_bcstnebf16_ps(const void *__A) {
  return (__m128)__builtin_ia32_vbcstnebf162ps128((const __bf16 *)__A);
}

/// Convert scalar BF16 (16-bit) floating-point element
/// stored at memory locations starting at location \a __A to a
/// single-precision (32-bit) floating-point, broadcast it to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm256_bcstnebf16_ps(const void *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VBCSTNEBF162PS instruction.
///
/// \param __A
///    A pointer to a 16-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \returns
///    A 256-bit vector of [8 x float].
///
/// \code{.operation}
/// b := Convert_BF16_To_FP32(MEM[__A+15:__A])
/// FOR j := 0 to 7
///   m := j*32
///   dst[m+31:m] := b
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_bcstnebf16_ps(const void *__A) {
  return (__m256)__builtin_ia32_vbcstnebf162ps256((const __bf16 *)__A);
}

/// Convert scalar half-precision (16-bit) floating-point element
/// stored at memory locations starting at location \a __A to a
/// single-precision (32-bit) floating-point, broadcast it to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm_bcstnesh_ps(const void *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VBCSTNESH2PS instruction.
///
/// \param __A
///    A pointer to a 16-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \returns
///    A 128-bit vector of [4 x float].
///
/// \code{.operation}
/// b := Convert_FP16_To_FP32(MEM[__A+15:__A])
/// FOR j := 0 to 3
///   m := j*32
///   dst[m+31:m] := b
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_bcstnesh_ps(const void *__A) {
  return (__m128)__builtin_ia32_vbcstnesh2ps128((const _Float16 *)__A);
}

/// Convert scalar half-precision (16-bit) floating-point element
/// stored at memory locations starting at location \a __A to a
/// single-precision (32-bit) floating-point, broadcast it to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm256_bcstnesh_ps(const void *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VBCSTNESH2PS instruction.
///
/// \param __A
///    A pointer to a 16-bit memory location. The address of the memory
///    location does not have to be aligned.
/// \returns
///    A 256-bit vector of [8 x float].
///
/// \code{.operation}
/// b := Convert_FP16_To_FP32(MEM[__A+15:__A])
/// FOR j := 0 to 7
///   m := j*32
///   dst[m+31:m] := b
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_bcstnesh_ps(const void *__A) {
  return (__m256)__builtin_ia32_vbcstnesh2ps256((const _Float16 *)__A);
}

/// Convert packed BF16 (16-bit) floating-point even-indexed elements
/// stored at memory locations starting at location \a __A to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm_cvtneebf16_ps(const __m128bh *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEEBF162PS instruction.
///
/// \param __A
///    A pointer to a 128-bit memory location containing 8 consecutive
///    BF16 (16-bit) floating-point values.
/// \returns
///    A 128-bit vector of [4 x float].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	k := j*2
/// 	i := k*16
/// 	m := j*32
/// 	dst[m+31:m] := Convert_BF16_To_FP32(MEM[__A+i+15:__A+i])
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_cvtneebf16_ps(const __m128bh *__A) {
  return (__m128)__builtin_ia32_vcvtneebf162ps128((const __v8bf *)__A);
}

/// Convert packed BF16 (16-bit) floating-point even-indexed elements
/// stored at memory locations starting at location \a __A to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm256_cvtneebf16_ps(const __m256bh *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEEBF162PS instruction.
///
/// \param __A
///    A pointer to a 256-bit memory location containing 16 consecutive
///    BF16 (16-bit) floating-point values.
/// \returns
///    A 256-bit vector of [8 x float].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	k := j*2
/// 	i := k*16
/// 	m := j*32
/// 	dst[m+31:m] := Convert_BF16_To_FP32(MEM[__A+i+15:__A+i])
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_cvtneebf16_ps(const __m256bh *__A) {
  return (__m256)__builtin_ia32_vcvtneebf162ps256((const __v16bf *)__A);
}

/// Convert packed half-precision (16-bit) floating-point even-indexed elements
/// stored at memory locations starting at location \a __A to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm_cvtneeph_ps(const __m128h *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEEPH2PS instruction.
///
/// \param __A
///    A pointer to a 128-bit memory location containing 8 consecutive
///    half-precision (16-bit) floating-point values.
/// \returns
///    A 128-bit vector of [4 x float].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	k := j*2
/// 	i := k*16
/// 	m := j*32
/// 	dst[m+31:m] := Convert_FP16_To_FP32(MEM[__A+i+15:__A+i])
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_cvtneeph_ps(const __m128h *__A) {
  return (__m128)__builtin_ia32_vcvtneeph2ps128((const __v8hf *)__A);
}

/// Convert packed half-precision (16-bit) floating-point even-indexed elements
/// stored at memory locations starting at location \a __A to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm256_cvtneeph_ps(const __m256h *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEEPH2PS instruction.
///
/// \param __A
///    A pointer to a 256-bit memory location containing 16 consecutive
///    half-precision (16-bit) floating-point values.
/// \returns
///    A 256-bit vector of [8 x float].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	k := j*2
/// 	i := k*16
/// 	m := j*32
/// 	dst[m+31:m] := Convert_FP16_To_FP32(MEM[__A+i+15:__A+i])
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_cvtneeph_ps(const __m256h *__A) {
  return (__m256)__builtin_ia32_vcvtneeph2ps256((const __v16hf *)__A);
}

/// Convert packed BF16 (16-bit) floating-point odd-indexed elements
/// stored at memory locations starting at location \a __A to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm_cvtneobf16_ps(const __m128bh *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEOBF162PS instruction.
///
/// \param __A
///    A pointer to a 128-bit memory location containing 8 consecutive
///    BF16 (16-bit) floating-point values.
/// \returns
///    A 128-bit vector of [4 x float].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	k := j*2+1
/// 	i := k*16
/// 	m := j*32
/// 	dst[m+31:m] := Convert_BF16_To_FP32(MEM[__A+i+15:__A+i])
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_cvtneobf16_ps(const __m128bh *__A) {
  return (__m128)__builtin_ia32_vcvtneobf162ps128((const __v8bf *)__A);
}

/// Convert packed BF16 (16-bit) floating-point odd-indexed elements
/// stored at memory locations starting at location \a __A to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm256_cvtneobf16_ps(const __m256bh *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEOBF162PS instruction.
///
/// \param __A
///    A pointer to a 256-bit memory location containing 16 consecutive
///    BF16 (16-bit) floating-point values.
/// \returns
///    A 256-bit vector of [8 x float].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	k := j*2+1
/// 	i := k*16
/// 	m := j*32
/// 	dst[m+31:m] := Convert_BF16_To_FP32(MEM[__A+i+15:__A+i])
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_cvtneobf16_ps(const __m256bh *__A) {
  return (__m256)__builtin_ia32_vcvtneobf162ps256((const __v16bf *)__A);
}

/// Convert packed half-precision (16-bit) floating-point odd-indexed elements
/// stored at memory locations starting at location \a __A to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm_cvtneoph_ps(const __m128h *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEOPH2PS instruction.
///
/// \param __A
///    A pointer to a 128-bit memory location containing 8 consecutive
///    half-precision (16-bit) floating-point values.
/// \returns
///    A 128-bit vector of [4 x float].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	k := j*2+1
/// 	i := k*16
/// 	m := j*32
/// 	dst[m+31:m] := Convert_FP16_To_FP32(MEM[__A+i+15:__A+i])
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_cvtneoph_ps(const __m128h *__A) {
  return (__m128)__builtin_ia32_vcvtneoph2ps128((const __v8hf *)__A);
}

/// Convert packed half-precision (16-bit) floating-point odd-indexed elements
/// stored at memory locations starting at location \a __A to packed
/// single-precision (32-bit) floating-point elements, and store the results in
/// \a dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm256_cvtneoph_ps(const __m256h *__A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEOPH2PS instruction.
///
/// \param __A
///    A pointer to a 256-bit memory location containing 16 consecutive
///    half-precision (16-bit) floating-point values.
/// \returns
///    A 256-bit vector of [8 x float].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	k := j*2+1
/// 	i := k*16
/// 	m := j*32
/// 	dst[m+31:m] := Convert_FP16_To_FP32(MEM[__A+i+15:__A+i])
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_cvtneoph_ps(const __m256h *__A) {
  return (__m256)__builtin_ia32_vcvtneoph2ps256((const __v16hf *)__A);
}

/// Convert packed single-precision (32-bit) floating-point elements in \a __A
/// to packed BF16 (16-bit) floating-point elements, and store the results in \a
/// dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm_cvtneps_avx_pbh(__m128 __A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEPS2BF16 instruction.
///
/// \param __A
///    A 128-bit vector of [4 x float].
/// \returns
///    A 128-bit vector of [8 x bfloat].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	dst.word[j] := Convert_FP32_To_BF16(__A.fp32[j])
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128bh __DEFAULT_FN_ATTRS128
_mm_cvtneps_avx_pbh(__m128 __A) {
  return (__m128bh)__builtin_ia32_vcvtneps2bf16128((__v4sf)__A);
}

/// Convert packed single-precision (32-bit) floating-point elements in \a __A
/// to packed BF16 (16-bit) floating-point elements, and store the results in \a
/// dst.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// _mm256_cvtneps_avx_pbh(__m256 __A);
/// \endcode
///
/// This intrinsic corresponds to the \c VCVTNEPS2BF16 instruction.
///
/// \param __A
///    A 256-bit vector of [8 x float].
/// \returns
///    A 128-bit vector of [8 x bfloat].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	dst.word[j] := Convert_FP32_To_BF16(a.fp32[j])
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128bh __DEFAULT_FN_ATTRS256
_mm256_cvtneps_avx_pbh(__m256 __A) {
  return (__m128bh)__builtin_ia32_vcvtneps2bf16256((__v8sf)__A);
}

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif // __AVXNECONVERTINTRIN_H
#endif // __SSE2__
