/*===----------- avxvnniint16intrin.h - AVXVNNIINT16 intrinsics-------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error                                                                         \
    "Never use <avxvnniint16intrin.h> directly; include <immintrin.h> instead."
#endif // __IMMINTRIN_H

#ifndef __AVXVNNIINT16INTRIN_H
#define __AVXVNNIINT16INTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128                                                  \
  __attribute__((__always_inline__, __nodebug__, __target__("avxvnniint16"),   \
                 __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256                                                  \
  __attribute__((__always_inline__, __nodebug__, __target__("avxvnniint16"),   \
                 __min_vector_width__(256)))

/// Multiply groups of 2 adjacent pairs of signed 16-bit integers in \a __A with
///    corresponding unsigned 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W, and store the packed 32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_dpwsud_epi32(__m128i __W, __m128i __A, __m128i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWSUD instruction.
///
/// \param __W
///    A 128-bit vector of [4 x int].
/// \param __A
///    A 128-bit vector of [8 x short].
/// \param __B
///    A 128-bit vector of [8 x unsigned short].
/// \returns
///    A 128-bit vector of [4 x int].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	tmp1.dword := SignExtend32(__A.word[2*j]) * ZeroExtend32(__B.word[2*j])
/// 	tmp2.dword := SignExtend32(__A.word[2*j+1]) * ZeroExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := __W.dword[j] + tmp1 + tmp2
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_dpwsud_epi32(__m128i __W,
                                                                 __m128i __A,
                                                                 __m128i __B) {
  return (__m128i)__builtin_ia32_vpdpwsud128((__v4si)__W, (__v4si)__A,
                                             (__v4si)__B);
}

/// Multiply groups of 2 adjacent pairs of signed 16-bit integers in \a __A with
///    corresponding unsigned 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W, and store the packed 32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_dpwsud_epi32(__m256i __W, __m256i __A, __m256i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWSUD instruction.
///
/// \param __W
///    A 256-bit vector of [8 x int].
/// \param __A
///    A 256-bit vector of [16 x short].
/// \param __B
///    A 256-bit vector of [16 x unsigned short].
/// \returns
///    A 256-bit vector of [8 x int].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	tmp1.dword := SignExtend32(__A.word[2*j]) * ZeroExtend32(__B.word[2*j])
/// 	tmp2.dword := SignExtend32(__A.word[2*j+1]) * ZeroExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := __W.dword[j] + tmp1 + tmp2
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_dpwsud_epi32(__m256i __W, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_vpdpwsud256((__v8si)__W, (__v8si)__A,
                                             (__v8si)__B);
}

/// Multiply groups of 2 adjacent pairs of signed 16-bit integers in \a __A with
///    corresponding unsigned 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W with signed saturation, and store the packed
///    32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_dpwsuds_epi32(__m128i __W, __m128i __A, __m128i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWSUDS instruction.
///
/// \param __W
///    A 128-bit vector of [4 x int].
/// \param __A
///    A 128-bit vector of [8 x short].
/// \param __B
///    A 128-bit vector of [8 x unsigned short].
/// \returns
///    A 128-bit vector of [4 x int].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	tmp1.dword := SignExtend32(__A.word[2*j]) * ZeroExtend32(__B.word[2*j])
/// 	tmp2.dword := SignExtend32(__A.word[2*j+1]) * ZeroExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := SIGNED_DWORD_SATURATE(__W.dword[j] + tmp1 + tmp2)
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_dpwsuds_epi32(__m128i __W,
                                                                  __m128i __A,
                                                                  __m128i __B) {
  return (__m128i)__builtin_ia32_vpdpwsuds128((__v4si)__W, (__v4si)__A,
                                              (__v4si)__B);
}

/// Multiply groups of 2 adjacent pairs of signed 16-bit integers in \a __A with
///    corresponding unsigned 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W with signed saturation, and store the packed
///    32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_dpwsuds_epi32(__m256i __W, __m256i __A, __m256i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWSUDS instruction.
///
/// \param __W
///    A 256-bit vector of [8 x int].
/// \param __A
///    A 256-bit vector of [16 x short].
/// \param __B
///    A 256-bit vector of [16 x unsigned short].
/// \returns
///    A 256-bit vector of [8 x int].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	tmp1.dword := SignExtend32(__A.word[2*j]) * ZeroExtend32(__B.word[2*j])
/// 	tmp2.dword := SignExtend32(__A.word[2*j+1]) * ZeroExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := SIGNED_DWORD_SATURATE(__W.dword[j] + tmp1 + tmp2)
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_dpwsuds_epi32(__m256i __W, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_vpdpwsuds256((__v8si)__W, (__v8si)__A,
                                              (__v8si)__B);
}

/// Multiply groups of 2 adjacent pairs of unsigned 16-bit integers in \a __A with
///    corresponding signed 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W, and store the packed 32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_dpbusd_epi32(__m128i __W, __m128i __A, __m128i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWUSD instruction.
///
/// \param __W
///    A 128-bit vector of [4 x int].
/// \param __A
///    A 128-bit vector of [8 x unsigned short].
/// \param __B
///    A 128-bit vector of [8 x short].
/// \returns
///    A 128-bit vector of [4 x int].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	tmp1.dword := ZeroExtend32(__A.word[2*j]) * SignExtend32(__B.word[2*j])
/// 	tmp2.dword := ZeroExtend32(__A.word[2*j+1]) * SignExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := __W.dword[j] + tmp1 + tmp2
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_dpwusd_epi32(__m128i __W,
                                                                 __m128i __A,
                                                                 __m128i __B) {
  return (__m128i)__builtin_ia32_vpdpwusd128((__v4si)__W, (__v4si)__A,
                                             (__v4si)__B);
}

/// Multiply groups of 2 adjacent pairs of unsigned 16-bit integers in \a __A with
///    corresponding signed 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W, and store the packed 32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_dpwusd_epi32(__m256i __W, __m256i __A, __m256i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWUSD instruction.
///
/// \param __W
///    A 256-bit vector of [8 x int].
/// \param __A
///    A 256-bit vector of [16 x unsigned short].
/// \param __B
///    A 256-bit vector of [16 x short].
/// \returns
///    A 256-bit vector of [8 x int].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	tmp1.dword := ZeroExtend32(__A.word[2*j]) * SignExtend32(__B.word[2*j])
/// 	tmp2.dword := ZeroExtend32(__A.word[2*j+1]) * SignExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := __W.dword[j] + tmp1 + tmp2
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_dpwusd_epi32(__m256i __W, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_vpdpwusd256((__v8si)__W, (__v8si)__A,
                                             (__v8si)__B);
}

/// Multiply groups of 2 adjacent pairs of unsigned 16-bit integers in \a __A with
///    corresponding signed 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W with signed saturation, and store the packed
///    32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_dpwusds_epi32(__m128i __W, __m128i __A, __m128i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWSUDS instruction.
///
/// \param __W
///    A 128-bit vector of [4 x int].
/// \param __A
///    A 128-bit vector of [8 x unsigned short].
/// \param __B
///    A 128-bit vector of [8 x short].
/// \returns
///    A 128-bit vector of [4 x int].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	tmp1.dword := ZeroExtend32(__A.word[2*j]) * SignExtend32(__B.word[2*j])
/// 	tmp2.dword := ZeroExtend32(__A.word[2*j+1]) * SignExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := SIGNED_DWORD_SATURATE(__W.dword[j] + tmp1 + tmp2)
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_dpwusds_epi32(__m128i __W,
                                                                  __m128i __A,
                                                                  __m128i __B) {
  return (__m128i)__builtin_ia32_vpdpwusds128((__v4si)__W, (__v4si)__A,
                                              (__v4si)__B);
}

/// Multiply groups of 2 adjacent pairs of unsigned 16-bit integers in \a __A with
///    corresponding signed 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W with signed saturation, and store the packed
///    32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_dpwsuds_epi32(__m256i __W, __m256i __A, __m256i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWSUDS instruction.
///
/// \param __W
///    A 256-bit vector of [8 x int].
/// \param __A
///    A 256-bit vector of [16 x unsigned short].
/// \param __B
///    A 256-bit vector of [16 x short].
/// \returns
///    A 256-bit vector of [8 x int].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	tmp1.dword := ZeroExtend32(__A.word[2*j]) * SignExtend32(__B.word[2*j])
/// 	tmp2.dword := ZeroExtend32(__A.word[2*j+1]) * SignExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := SIGNED_DWORD_SATURATE(__W.dword[j] + tmp1 + tmp2)
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_dpwusds_epi32(__m256i __W, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_vpdpwusds256((__v8si)__W, (__v8si)__A,
                                              (__v8si)__B);
}

/// Multiply groups of 2 adjacent pairs of unsigned 16-bit integers in \a __A with
///    corresponding unsigned 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W, and store the packed 32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_dpwuud_epi32(__m128i __W, __m128i __A, __m128i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWUUD instruction.
///
/// \param __W
///    A 128-bit vector of [4 x unsigned int].
/// \param __A
///    A 128-bit vector of [8 x unsigned short].
/// \param __B
///    A 128-bit vector of [8 x unsigned short].
/// \returns
///    A 128-bit vector of [4 x unsigned int].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	tmp1.dword := ZeroExtend32(__A.word[2*j]) * ZeroExtend32(__B.word[2*j])
/// 	tmp2.dword := ZeroExtend32(__A.word[2*j+1]) * ZeroExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := __W.dword[j] + tmp1 + tmp2
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_dpwuud_epi32(__m128i __W,
                                                                 __m128i __A,
                                                                 __m128i __B) {
  return (__m128i)__builtin_ia32_vpdpwuud128((__v4si)__W, (__v4si)__A,
                                             (__v4si)__B);
}

/// Multiply groups of 2 adjacent pairs of unsigned 16-bit integers in \a __A with
///    corresponding unsigned 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W, and store the packed 32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_dpwuud_epi32(__m256i __W, __m256i __A, __m256i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWUUD instruction.
///
/// \param __W
///    A 256-bit vector of [8 x unsigned int].
/// \param __A
///    A 256-bit vector of [16 x unsigned short].
/// \param __B
///    A 256-bit vector of [16 x unsigned short].
/// \returns
///    A 256-bit vector of [8 x unsigned int].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	tmp1.dword := ZeroExtend32(__A.word[2*j]) * ZeroExtend32(__B.word[2*j])
/// 	tmp2.dword := ZeroExtend32(__A.word[2*j+1]) * ZeroExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := __W.dword[j] + tmp1 + tmp2
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_dpwuud_epi32(__m256i __W, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_vpdpwuud256((__v8si)__W, (__v8si)__A,
                                             (__v8si)__B);
}

/// Multiply groups of 2 adjacent pairs of unsigned 16-bit integers in \a __A with
///    corresponding unsigned 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W with signed saturation, and store the packed
///    32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_dpwsuds_epi32(__m128i __W, __m128i __A, __m128i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWSUDS instruction.
///
/// \param __W
///    A 128-bit vector of [4 x unsigned int].
/// \param __A
///    A 128-bit vector of [8 x unsigned short].
/// \param __B
///    A 128-bit vector of [8 x unsigned short].
/// \returns
///    A 128-bit vector of [4 x unsigned int].
///
/// \code{.operation}
/// FOR j := 0 to 3
/// 	tmp1.dword := ZeroExtend32(__A.word[2*j]) * ZeroExtend32(__B.word[2*j])
/// 	tmp2.dword := ZeroExtend32(__A.word[2*j+1]) * ZeroExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := UNSIGNED_DWORD_SATURATE(__W.dword[j] + tmp1 + tmp2)
/// ENDFOR
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_dpwuuds_epi32(__m128i __W,
                                                                  __m128i __A,
                                                                  __m128i __B) {
  return (__m128i)__builtin_ia32_vpdpwuuds128((__v4si)__W, (__v4si)__A,
                                              (__v4si)__B);
}

/// Multiply groups of 2 adjacent pairs of unsigned 16-bit integers in \a __A with
///    corresponding unsigned 16-bit integers in \a __B, producing 2 intermediate
///    signed 16-bit results. Sum these 2 results with the corresponding
///    32-bit integer in \a __W with signed saturation, and store the packed
///    32-bit results in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_dpwuuds_epi32(__m256i __W, __m256i __A, __m256i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VPDPWSUDS instruction.
///
/// \param __W
///    A 256-bit vector of [8 x unsigned int].
/// \param __A
///    A 256-bit vector of [16 x unsigned short].
/// \param __B
///    A 256-bit vector of [16 x unsigned short].
/// \returns
///    A 256-bit vector of [8 x unsigned int].
///
/// \code{.operation}
/// FOR j := 0 to 7
/// 	tmp1.dword := ZeroExtend32(__A.word[2*j]) * ZeroExtend32(__B.word[2*j])
/// 	tmp2.dword := ZeroExtend32(__A.word[2*j+1]) * ZeroExtend32(__B.word[2*j+1])
/// 	dst.dword[j] := UNSIGNED_DWORD_SATURATE(__W.dword[j] + tmp1 + tmp2)
/// ENDFOR
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_dpwuuds_epi32(__m256i __W, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_vpdpwuuds256((__v8si)__W, (__v8si)__A,
                                              (__v8si)__B);
}

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif // __AVXVNNIINT16INTRIN_H
