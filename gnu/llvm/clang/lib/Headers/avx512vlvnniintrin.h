/*===------------- avx512vlvnniintrin.h - VNNI intrinsics ------------------===
 *
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <avx512vlvnniintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512VLVNNIINTRIN_H
#define __AVX512VLVNNIINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512vl,avx512vnni,no-evex512"),                 \
                 __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512vl,avx512vnni,no-evex512"),                 \
                 __min_vector_width__(256)))

/// Multiply groups of 4 adjacent pairs of unsigned 8-bit integers in \a A with
/// corresponding signed 8-bit integers in \a B, producing 4 intermediate signed
/// 16-bit results. Sum these 4 results with the corresponding 32-bit integer
/// in \a S, and store the packed 32-bit results in DST.
///
/// This intrinsic corresponds to the <c> VPDPBUSD </c> instructions.
///
/// \code{.operation}
///    FOR j := 0 to 7
///      tmp1.word := Signed(ZeroExtend16(A.byte[4*j]) * SignExtend16(B.byte[4*j]))
///      tmp2.word := Signed(ZeroExtend16(A.byte[4*j+1]) * SignExtend16(B.byte[4*j+1]))
///      tmp3.word := Signed(ZeroExtend16(A.byte[4*j+2]) * SignExtend16(B.byte[4*j+2]))
///      tmp4.word := Signed(ZeroExtend16(A.byte[4*j+3]) * SignExtend16(B.byte[4*j+3]))
///      DST.dword[j] := S.dword[j] + tmp1 + tmp2 + tmp3 + tmp4
///    ENDFOR
///    DST[MAX:256] := 0
/// \endcode
#define _mm256_dpbusd_epi32(S, A, B) \
  ((__m256i)__builtin_ia32_vpdpbusd256((__v8si)(S), (__v8si)(A), (__v8si)(B)))

/// Multiply groups of 4 adjacent pairs of unsigned 8-bit integers in \a A with
/// corresponding signed 8-bit integers in \a B, producing 4 intermediate signed
/// 16-bit results. Sum these 4 results with the corresponding 32-bit integer
/// in \a S using signed saturation, and store the packed 32-bit results in DST.
///
/// This intrinsic corresponds to the <c> VPDPBUSDS </c> instructions.
///
/// \code{.operation}
///    FOR j := 0 to 7
///      tmp1.word := Signed(ZeroExtend16(A.byte[4*j]) * SignExtend16(B.byte[4*j]))
///      tmp2.word := Signed(ZeroExtend16(A.byte[4*j+1]) * SignExtend16(B.byte[4*j+1]))
///      tmp3.word := Signed(ZeroExtend16(A.byte[4*j+2]) * SignExtend16(B.byte[4*j+2]))
///      tmp4.word := Signed(ZeroExtend16(A.byte[4*j+3]) * SignExtend16(B.byte[4*j+3]))
///      DST.dword[j] := Saturate32(S.dword[j] + tmp1 + tmp2 + tmp3 + tmp4)
///    ENDFOR
///    DST[MAX:256] := 0
/// \endcode
#define _mm256_dpbusds_epi32(S, A, B) \
  ((__m256i)__builtin_ia32_vpdpbusds256((__v8si)(S), (__v8si)(A), (__v8si)(B)))

/// Multiply groups of 2 adjacent pairs of signed 16-bit integers in \a A with
/// corresponding 16-bit integers in \a B, producing 2 intermediate signed 32-bit
/// results. Sum these 2 results with the corresponding 32-bit integer in \a S,
///  and store the packed 32-bit results in DST.
///
/// This intrinsic corresponds to the <c> VPDPWSSD </c> instructions.
///
/// \code{.operation}
///    FOR j := 0 to 7
///      tmp1.dword := SignExtend32(A.word[2*j]) * SignExtend32(B.word[2*j])
///      tmp2.dword := SignExtend32(A.word[2*j+1]) * SignExtend32(B.word[2*j+1])
///      DST.dword[j] := S.dword[j] + tmp1 + tmp2
///    ENDFOR
///    DST[MAX:256] := 0
/// \endcode
#define _mm256_dpwssd_epi32(S, A, B) \
  ((__m256i)__builtin_ia32_vpdpwssd256((__v8si)(S), (__v8si)(A), (__v8si)(B)))

/// Multiply groups of 2 adjacent pairs of signed 16-bit integers in \a A with
/// corresponding 16-bit integers in \a B, producing 2 intermediate signed 32-bit
/// results. Sum these 2 results with the corresponding 32-bit integer in \a S
/// using signed saturation, and store the packed 32-bit results in DST.
///
/// This intrinsic corresponds to the <c> VPDPWSSDS </c> instructions.
///
/// \code{.operation}
///    FOR j := 0 to 7
///      tmp1.dword := SignExtend32(A.word[2*j]) * SignExtend32(B.word[2*j])
///      tmp2.dword := SignExtend32(A.word[2*j+1]) * SignExtend32(B.word[2*j+1])
///      DST.dword[j] := Saturate32(S.dword[j] + tmp1 + tmp2)
///    ENDFOR
///    DST[MAX:256] := 0
/// \endcode
#define _mm256_dpwssds_epi32(S, A, B) \
  ((__m256i)__builtin_ia32_vpdpwssds256((__v8si)(S), (__v8si)(A), (__v8si)(B)))

/// Multiply groups of 4 adjacent pairs of unsigned 8-bit integers in \a A with
/// corresponding signed 8-bit integers in \a B, producing 4 intermediate signed
/// 16-bit results. Sum these 4 results with the corresponding 32-bit integer
/// in \a S, and store the packed 32-bit results in DST.
///
/// This intrinsic corresponds to the <c> VPDPBUSD </c> instructions.
///
/// \code{.operation}
///    FOR j := 0 to 3
///      tmp1.word := Signed(ZeroExtend16(A.byte[4*j]) * SignExtend16(B.byte[4*j]))
///      tmp2.word := Signed(ZeroExtend16(A.byte[4*j+1]) * SignExtend16(B.byte[4*j+1]))
///      tmp3.word := Signed(ZeroExtend16(A.byte[4*j+2]) * SignExtend16(B.byte[4*j+2]))
///      tmp4.word := Signed(ZeroExtend16(A.byte[4*j+3]) * SignExtend16(B.byte[4*j+3]))
///      DST.dword[j] := S.dword[j] + tmp1 + tmp2 + tmp3 + tmp4
///    ENDFOR
///    DST[MAX:128] := 0
/// \endcode
#define _mm_dpbusd_epi32(S, A, B) \
  ((__m128i)__builtin_ia32_vpdpbusd128((__v4si)(S), (__v4si)(A), (__v4si)(B)))

/// Multiply groups of 4 adjacent pairs of unsigned 8-bit integers in \a A with
/// corresponding signed 8-bit integers in \a B, producing 4 intermediate signed
/// 16-bit results. Sum these 4 results with the corresponding 32-bit integer
/// in \a S using signed saturation, and store the packed 32-bit results in DST.
///
/// This intrinsic corresponds to the <c> VPDPBUSDS </c> instructions.
///
/// \code{.operation}
///    FOR j := 0 to 3
///      tmp1.word := Signed(ZeroExtend16(A.byte[4*j]) * SignExtend16(B.byte[4*j]))
///      tmp2.word := Signed(ZeroExtend16(A.byte[4*j+1]) * SignExtend16(B.byte[4*j+1]))
///      tmp3.word := Signed(ZeroExtend16(A.byte[4*j+2]) * SignExtend16(B.byte[4*j+2]))
///      tmp4.word := Signed(ZeroExtend16(A.byte[4*j+3]) * SignExtend16(B.byte[4*j+3]))
///      DST.dword[j] := Saturate32(S.dword[j] + tmp1 + tmp2 + tmp3 + tmp4)
///    ENDFOR
///    DST[MAX:128] := 0
/// \endcode
#define _mm_dpbusds_epi32(S, A, B) \
  ((__m128i)__builtin_ia32_vpdpbusds128((__v4si)(S), (__v4si)(A), (__v4si)(B)))

/// Multiply groups of 2 adjacent pairs of signed 16-bit integers in \a A with
/// corresponding 16-bit integers in \a B, producing 2 intermediate signed 32-bit
/// results. Sum these 2 results with the corresponding 32-bit integer in \a S,
/// and store the packed 32-bit results in DST.
///
/// This intrinsic corresponds to the <c> VPDPWSSD </c> instructions.
///
/// \code{.operation}
///    FOR j := 0 to 3
///      tmp1.dword := SignExtend32(A.word[2*j]) * SignExtend32(B.word[2*j])
///      tmp2.dword := SignExtend32(A.word[2*j+1]) * SignExtend32(B.word[2*j+1])
///      DST.dword[j] := S.dword[j] + tmp1 + tmp2
///    ENDFOR
///    DST[MAX:128] := 0
/// \endcode
#define _mm_dpwssd_epi32(S, A, B) \
  ((__m128i)__builtin_ia32_vpdpwssd128((__v4si)(S), (__v4si)(A), (__v4si)(B)))

/// Multiply groups of 2 adjacent pairs of signed 16-bit integers in \a A with
/// corresponding 16-bit integers in \a B, producing 2 intermediate signed 32-bit
/// results. Sum these 2 results with the corresponding 32-bit integer in \a S
/// using signed saturation, and store the packed 32-bit results in DST.
///
/// This intrinsic corresponds to the <c> VPDPWSSDS </c> instructions.
///
/// \code{.operation}
///    FOR j := 0 to 3
///      tmp1.dword := SignExtend32(A.word[2*j]) * SignExtend32(B.word[2*j])
///      tmp2.dword := SignExtend32(A.word[2*j+1]) * SignExtend32(B.word[2*j+1])
///      DST.dword[j] := Saturate32(S.dword[j] + tmp1 + tmp2)
///    ENDFOR
///    DST[MAX:128] := 0
/// \endcode
#define _mm_dpwssds_epi32(S, A, B) \
  ((__m128i)__builtin_ia32_vpdpwssds128((__v4si)(S), (__v4si)(A), (__v4si)(B)))

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_dpbusd_epi32(__m256i __S, __mmask8 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                     (__v8si)_mm256_dpbusd_epi32(__S, __A, __B),
                                     (__v8si)__S);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_dpbusd_epi32(__mmask8 __U, __m256i __S, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                     (__v8si)_mm256_dpbusd_epi32(__S, __A, __B),
                                     (__v8si)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_dpbusds_epi32(__m256i __S, __mmask8 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                    (__v8si)_mm256_dpbusds_epi32(__S, __A, __B),
                                    (__v8si)__S);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_dpbusds_epi32(__mmask8 __U, __m256i __S, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                     (__v8si)_mm256_dpbusds_epi32(__S, __A, __B),
                                     (__v8si)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_dpwssd_epi32(__m256i __S, __mmask8 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                     (__v8si)_mm256_dpwssd_epi32(__S, __A, __B),
                                     (__v8si)__S);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_dpwssd_epi32(__mmask8 __U, __m256i __S, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                     (__v8si)_mm256_dpwssd_epi32(__S, __A, __B),
                                     (__v8si)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_dpwssds_epi32(__m256i __S, __mmask8 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                    (__v8si)_mm256_dpwssds_epi32(__S, __A, __B),
                                    (__v8si)__S);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_dpwssds_epi32(__mmask8 __U, __m256i __S, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                    (__v8si)_mm256_dpwssds_epi32(__S, __A, __B),
                                    (__v8si)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_dpbusd_epi32(__m128i __S, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                        (__v4si)_mm_dpbusd_epi32(__S, __A, __B),
                                        (__v4si)__S);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_dpbusd_epi32(__mmask8 __U, __m128i __S, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                        (__v4si)_mm_dpbusd_epi32(__S, __A, __B),
                                        (__v4si)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_dpbusds_epi32(__m128i __S, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                       (__v4si)_mm_dpbusds_epi32(__S, __A, __B),
                                       (__v4si)__S);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_dpbusds_epi32(__mmask8 __U, __m128i __S, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                       (__v4si)_mm_dpbusds_epi32(__S, __A, __B),
                                       (__v4si)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_dpwssd_epi32(__m128i __S, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                        (__v4si)_mm_dpwssd_epi32(__S, __A, __B),
                                        (__v4si)__S);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_dpwssd_epi32(__mmask8 __U, __m128i __S, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                        (__v4si)_mm_dpwssd_epi32(__S, __A, __B),
                                        (__v4si)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_dpwssds_epi32(__m128i __S, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                       (__v4si)_mm_dpwssds_epi32(__S, __A, __B),
                                       (__v4si)__S);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_dpwssds_epi32(__mmask8 __U, __m128i __S, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                       (__v4si)_mm_dpwssds_epi32(__S, __A, __B),
                                       (__v4si)_mm_setzero_si128());
}

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif
