/*===----------------- avxifmaintrin.h - IFMA intrinsics -------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <avxifmaintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVXIFMAINTRIN_H
#define __AVXIFMAINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128                                                  \
  __attribute__((__always_inline__, __nodebug__, __target__("avxifma"),        \
                 __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256                                                  \
  __attribute__((__always_inline__, __nodebug__, __target__("avxifma"),        \
                 __min_vector_width__(256)))

// must vex-encoding

/// Multiply packed unsigned 52-bit integers in each 64-bit element of \a __Y
/// and \a __Z to form a 104-bit intermediate result. Add the high 52-bit
/// unsigned integer from the intermediate result with the corresponding
/// unsigned 64-bit integer in \a __X, and store the results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i
/// _mm_madd52hi_avx_epu64 (__m128i __X, __m128i __Y, __m128i __Z)
/// \endcode
///
/// This intrinsic corresponds to the \c VPMADD52HUQ instruction.
///
/// \return
/// 	return __m128i dst.
/// \param __X
/// 	A 128-bit vector of [2 x i64]
/// \param __Y
/// 	A 128-bit vector of [2 x i64]
/// \param __Z
/// 	A 128-bit vector of [2 x i64]
///
/// \code{.operation}
/// FOR j := 0 to 1
/// 	i := j*64
/// 	tmp[127:0] := ZeroExtend64(__Y[i+51:i]) * ZeroExtend64(__Z[i+51:i])
/// 	dst[i+63:i] := __X[i+63:i] + ZeroExtend64(tmp[103:52])
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_madd52hi_avx_epu64(__m128i __X, __m128i __Y, __m128i __Z) {
  return (__m128i)__builtin_ia32_vpmadd52huq128((__v2di)__X, (__v2di)__Y,
                                                (__v2di)__Z);
}

/// Multiply packed unsigned 52-bit integers in each 64-bit element of \a __Y
/// and \a __Z to form a 104-bit intermediate result. Add the high 52-bit
/// unsigned integer from the intermediate result with the corresponding
/// unsigned 64-bit integer in \a __X, and store the results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i
/// _mm256_madd52hi_avx_epu64 (__m256i __X, __m256i __Y, __m256i __Z)
/// \endcode
///
/// This intrinsic corresponds to the \c VPMADD52HUQ instruction.
///
/// \return
/// 	return __m256i dst.
/// \param __X
/// 	A 256-bit vector of [4 x i64]
/// \param __Y
/// 	A 256-bit vector of [4 x i64]
/// \param __Z
/// 	A 256-bit vector of [4 x i64]
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	i := j*64
/// 	tmp[127:0] := ZeroExtend64(__Y[i+51:i]) * ZeroExtend64(__Z[i+51:i])
/// 	dst[i+63:i] := __X[i+63:i] + ZeroExtend64(tmp[103:52])
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_madd52hi_avx_epu64(__m256i __X, __m256i __Y, __m256i __Z) {
  return (__m256i)__builtin_ia32_vpmadd52huq256((__v4di)__X, (__v4di)__Y,
                                                (__v4di)__Z);
}

/// Multiply packed unsigned 52-bit integers in each 64-bit element of \a __Y
/// and \a __Z to form a 104-bit intermediate result. Add the low 52-bit
/// unsigned integer from the intermediate result with the corresponding
/// unsigned 64-bit integer in \a __X, and store the results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i
/// _mm_madd52lo_avx_epu64 (__m128i __X, __m128i __Y, __m128i __Z)
/// \endcode
///
/// This intrinsic corresponds to the \c VPMADD52LUQ instruction.
///
/// \return
/// 	return __m128i dst.
/// \param __X
/// 	A 128-bit vector of [2 x i64]
/// \param __Y
/// 	A 128-bit vector of [2 x i64]
/// \param __Z
/// 	A 128-bit vector of [2 x i64]
///
/// \code{.operation}
/// FOR j := 0 to 1
/// 	i := j*64
/// 	tmp[127:0] := ZeroExtend64(__Y[i+51:i]) * ZeroExtend64(__Z[i+51:i])
/// 	dst[i+63:i] := __X[i+63:i] + ZeroExtend64(tmp[51:0])
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_madd52lo_avx_epu64(__m128i __X, __m128i __Y, __m128i __Z) {
  return (__m128i)__builtin_ia32_vpmadd52luq128((__v2di)__X, (__v2di)__Y,
                                                (__v2di)__Z);
}

/// Multiply packed unsigned 52-bit integers in each 64-bit element of \a __Y
/// and \a __Z to form a 104-bit intermediate result. Add the low 52-bit
/// unsigned integer from the intermediate result with the corresponding
/// unsigned 64-bit integer in \a __X, and store the results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i
/// _mm256_madd52lo_avx_epu64 (__m256i __X, __m256i __Y, __m256i __Z)
/// \endcode
///
/// This intrinsic corresponds to the \c VPMADD52LUQ instruction.
///
/// \return
/// 	return __m256i dst.
/// \param __X
/// 	A 256-bit vector of [4 x i64]
/// \param __Y
/// 	A 256-bit vector of [4 x i64]
/// \param __Z
/// 	A 256-bit vector of [4 x i64]
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	i := j*64
/// 	tmp[127:0] := ZeroExtend64(__Y[i+51:i]) * ZeroExtend64(__Z[i+51:i])
/// 	dst[i+63:i] := __X[i+63:i] + ZeroExtend64(tmp[51:0])
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_madd52lo_avx_epu64(__m256i __X, __m256i __Y, __m256i __Z) {
  return (__m256i)__builtin_ia32_vpmadd52luq256((__v4di)__X, (__v4di)__Y,
                                                (__v4di)__Z);
}
#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif // __AVXIFMAINTRIN_H
