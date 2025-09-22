/*===--------- avx512vlbf16intrin.h - AVX512_BF16 intrinsics ---------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <avx512vlbf16intrin.h> directly; include <immintrin.h> instead."
#endif

#ifdef __SSE2__

#ifndef __AVX512VLBF16INTRIN_H
#define __AVX512VLBF16INTRIN_H

#define __DEFAULT_FN_ATTRS128                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512vl,avx512bf16,no-evex512"),                 \
                 __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512vl,avx512bf16,no-evex512"),                 \
                 __min_vector_width__(256)))

/// Convert Two Packed Single Data to One Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNE2PS2BF16 </c> instructions.
///
/// \param __A
///    A 128-bit vector of [4 x float].
/// \param __B
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [8 x bfloat] whose lower 64 bits come from
///    conversion of __B, and higher 64 bits come from conversion of __A.
static __inline__ __m128bh __DEFAULT_FN_ATTRS128
_mm_cvtne2ps_pbh(__m128 __A, __m128 __B) {
  return (__m128bh)__builtin_ia32_cvtne2ps2bf16_128((__v4sf) __A,
                                                    (__v4sf) __B);
}

/// Convert Two Packed Single Data to One Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNE2PS2BF16 </c> instructions.
///
/// \param __A
///    A 128-bit vector of [4 x float].
/// \param __B
///    A 128-bit vector of [4 x float].
/// \param __W
///    A 128-bit vector of [8 x bfloat].
/// \param __U
///    A 8-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A or __B. A 0 means element from __W.
/// \returns A 128-bit vector of [8 x bfloat] whose lower 64 bits come from
///    conversion of __B, and higher 64 bits come from conversion of __A.
static __inline__ __m128bh __DEFAULT_FN_ATTRS128
_mm_mask_cvtne2ps_pbh(__m128bh __W, __mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128bh)__builtin_ia32_selectpbf_128((__mmask8)__U,
                                             (__v8bf)_mm_cvtne2ps_pbh(__A, __B),
                                             (__v8bf)__W);
}

/// Convert Two Packed Single Data to One Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNE2PS2BF16 </c> instructions.
///
/// \param __A
///    A 128-bit vector of [4 x float].
/// \param __B
///    A 128-bit vector of [4 x float].
/// \param __U
///    A 8-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A or __B. A 0 means element is zero.
/// \returns A 128-bit vector of [8 x bfloat] whose lower 64 bits come from
///    conversion of __B, and higher 64 bits come from conversion of __A.
static __inline__ __m128bh __DEFAULT_FN_ATTRS128
_mm_maskz_cvtne2ps_pbh(__mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128bh)__builtin_ia32_selectpbf_128((__mmask8)__U,
                                             (__v8bf)_mm_cvtne2ps_pbh(__A, __B),
                                             (__v8bf)_mm_setzero_si128());
}

/// Convert Two Packed Single Data to One Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNE2PS2BF16 </c> instructions.
///
/// \param __A
///    A 256-bit vector of [8 x float].
/// \param __B
///    A 256-bit vector of [8 x float].
/// \returns A 256-bit vector of [16 x bfloat] whose lower 128 bits come from
///    conversion of __B, and higher 128 bits come from conversion of __A.
static __inline__ __m256bh __DEFAULT_FN_ATTRS256
_mm256_cvtne2ps_pbh(__m256 __A, __m256 __B) {
  return (__m256bh)__builtin_ia32_cvtne2ps2bf16_256((__v8sf) __A,
                                                    (__v8sf) __B);
}

/// Convert Two Packed Single Data to One Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNE2PS2BF16 </c> instructions.
///
/// \param __A
///    A 256-bit vector of [8 x float].
/// \param __B
///    A 256-bit vector of [8 x float].
/// \param __W
///    A 256-bit vector of [16 x bfloat].
/// \param __U
///    A 16-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A or __B. A 0 means element from __W.
/// \returns A 256-bit vector of [16 x bfloat] whose lower 128 bits come from
///    conversion of __B, and higher 128 bits come from conversion of __A.
static __inline__ __m256bh __DEFAULT_FN_ATTRS256
_mm256_mask_cvtne2ps_pbh(__m256bh __W, __mmask16 __U, __m256 __A, __m256 __B) {
  return (__m256bh)__builtin_ia32_selectpbf_256((__mmask16)__U,
                                         (__v16bf)_mm256_cvtne2ps_pbh(__A, __B),
                                         (__v16bf)__W);
}

/// Convert Two Packed Single Data to One Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNE2PS2BF16 </c> instructions.
///
/// \param __A
///    A 256-bit vector of [8 x float].
/// \param __B
///    A 256-bit vector of [8 x float].
/// \param __U
///    A 16-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A or __B. A 0 means element is zero.
/// \returns A 256-bit vector of [16 x bfloat] whose lower 128 bits come from
///    conversion of __B, and higher 128 bits come from conversion of __A.
static __inline__ __m256bh __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtne2ps_pbh(__mmask16 __U, __m256 __A, __m256 __B) {
  return (__m256bh)__builtin_ia32_selectpbf_256((__mmask16)__U,
                                         (__v16bf)_mm256_cvtne2ps_pbh(__A, __B),
                                         (__v16bf)_mm256_setzero_si256());
}

/// Convert Packed Single Data to Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [8 x bfloat] whose lower 64 bits come from
///    conversion of __A, and higher 64 bits are 0.
#define _mm_cvtneps_pbh(A)                                                     \
  ((__m128bh)__builtin_ia32_vcvtneps2bf16128((__v4sf)(A)))

/// Convert Packed Single Data to Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A 128-bit vector of [4 x float].
/// \param __W
///    A 128-bit vector of [8 x bfloat].
/// \param __U
///    A 4-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A. A 0 means element from __W.
/// \returns A 128-bit vector of [8 x bfloat] whose lower 64 bits come from
///    conversion of __A, and higher 64 bits are 0.
static __inline__ __m128bh __DEFAULT_FN_ATTRS128
_mm_mask_cvtneps_pbh(__m128bh __W, __mmask8 __U, __m128 __A) {
  return (__m128bh)__builtin_ia32_cvtneps2bf16_128_mask((__v4sf) __A,
                                                        (__v8bf)__W,
                                                        (__mmask8)__U);
}

/// Convert Packed Single Data to Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A 128-bit vector of [4 x float].
/// \param __U
///    A 4-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A. A 0 means element is zero.
/// \returns A 128-bit vector of [8 x bfloat] whose lower 64 bits come from
///    conversion of __A, and higher 64 bits are 0.
static __inline__ __m128bh __DEFAULT_FN_ATTRS128
_mm_maskz_cvtneps_pbh(__mmask8 __U, __m128 __A) {
  return (__m128bh)__builtin_ia32_cvtneps2bf16_128_mask((__v4sf) __A,
                                                    (__v8bf)_mm_setzero_si128(),
                                                    (__mmask8)__U);
}

/// Convert Packed Single Data to Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A 256-bit vector of [8 x float].
/// \returns A 128-bit vector of [8 x bfloat] comes from conversion of __A.
#define _mm256_cvtneps_pbh(A)                                                  \
  ((__m128bh)__builtin_ia32_vcvtneps2bf16256((__v8sf)(A)))

/// Convert Packed Single Data to Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A 256-bit vector of [8 x float].
/// \param __W
///    A 256-bit vector of [8 x bfloat].
/// \param __U
///    A 8-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A. A 0 means element from __W.
/// \returns A 128-bit vector of [8 x bfloat] comes from conversion of __A.
static __inline__ __m128bh __DEFAULT_FN_ATTRS256
_mm256_mask_cvtneps_pbh(__m128bh __W, __mmask8 __U, __m256 __A) {
  return (__m128bh)__builtin_ia32_cvtneps2bf16_256_mask((__v8sf)__A,
                                                        (__v8bf)__W,
                                                        (__mmask8)__U);
}

/// Convert Packed Single Data to Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A 256-bit vector of [8 x float].
/// \param __U
///    A 8-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A. A 0 means element is zero.
/// \returns A 128-bit vector of [8 x bfloat] comes from conversion of __A.
static __inline__ __m128bh __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtneps_pbh(__mmask8 __U, __m256 __A) {
  return (__m128bh)__builtin_ia32_cvtneps2bf16_256_mask((__v8sf)__A,
                                                    (__v8bf)_mm_setzero_si128(),
                                                    (__mmask8)__U);
}

/// Dot Product of BF16 Pairs Accumulated into Packed Single Precision.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDPBF16PS </c> instructions.
///
/// \param __A
///    A 128-bit vector of [8 x bfloat].
/// \param __B
///    A 128-bit vector of [8 x bfloat].
/// \param __D
///    A 128-bit vector of [4 x float].
/// \returns A 128-bit vector of [4 x float] comes from  Dot Product of
///  __A, __B and __D
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_dpbf16_ps(__m128 __D, __m128bh __A, __m128bh __B) {
  return (__m128)__builtin_ia32_dpbf16ps_128((__v4sf)__D,
                                             (__v8bf)__A,
                                             (__v8bf)__B);
}

/// Dot Product of BF16 Pairs Accumulated into Packed Single Precision.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDPBF16PS </c> instructions.
///
/// \param __A
///    A 128-bit vector of [8 x bfloat].
/// \param __B
///    A 128-bit vector of [8 x bfloat].
/// \param __D
///    A 128-bit vector of [4 x float].
/// \param __U
///    A 8-bit mask value specifying what is chosen for each element.
///    A 1 means __A and __B's dot product accumulated with __D. A 0 means __D.
/// \returns A 128-bit vector of [4 x float] comes from  Dot Product of
///  __A, __B and __D
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_mask_dpbf16_ps(__m128 __D, __mmask8 __U, __m128bh __A, __m128bh __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                           (__v4sf)_mm_dpbf16_ps(__D, __A, __B),
                                           (__v4sf)__D);
}

/// Dot Product of BF16 Pairs Accumulated into Packed Single Precision.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDPBF16PS </c> instructions.
///
/// \param __A
///    A 128-bit vector of [8 x bfloat].
/// \param __B
///    A 128-bit vector of [8 x bfloat].
/// \param __D
///    A 128-bit vector of [4 x float].
/// \param __U
///    A 8-bit mask value specifying what is chosen for each element.
///    A 1 means __A and __B's dot product accumulated with __D. A 0 means 0.
/// \returns A 128-bit vector of [4 x float] comes from  Dot Product of
///  __A, __B and __D
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maskz_dpbf16_ps(__mmask8 __U, __m128 __D, __m128bh __A, __m128bh __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                           (__v4sf)_mm_dpbf16_ps(__D, __A, __B),
                                           (__v4sf)_mm_setzero_si128());
}

/// Dot Product of BF16 Pairs Accumulated into Packed Single Precision.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDPBF16PS </c> instructions.
///
/// \param __A
///    A 256-bit vector of [16 x bfloat].
/// \param __B
///    A 256-bit vector of [16 x bfloat].
/// \param __D
///    A 256-bit vector of [8 x float].
/// \returns A 256-bit vector of [8 x float] comes from  Dot Product of
///  __A, __B and __D
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_dpbf16_ps(__m256 __D, __m256bh __A, __m256bh __B) {
  return (__m256)__builtin_ia32_dpbf16ps_256((__v8sf)__D,
                                             (__v16bf)__A,
                                             (__v16bf)__B);
}

/// Dot Product of BF16 Pairs Accumulated into Packed Single Precision.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDPBF16PS </c> instructions.
///
/// \param __A
///    A 256-bit vector of [16 x bfloat].
/// \param __B
///    A 256-bit vector of [16 x bfloat].
/// \param __D
///    A 256-bit vector of [8 x float].
/// \param __U
///    A 16-bit mask value specifying what is chosen for each element.
///    A 1 means __A and __B's dot product accumulated with __D. A 0 means __D.
/// \returns A 256-bit vector of [8 x float] comes from  Dot Product of
///  __A, __B and __D
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_mask_dpbf16_ps(__m256 __D, __mmask8 __U, __m256bh __A, __m256bh __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                        (__v8sf)_mm256_dpbf16_ps(__D, __A, __B),
                                        (__v8sf)__D);
}

/// Dot Product of BF16 Pairs Accumulated into Packed Single Precision.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDPBF16PS </c> instructions.
///
/// \param __A
///    A 256-bit vector of [16 x bfloat].
/// \param __B
///    A 256-bit vector of [16 x bfloat].
/// \param __D
///    A 256-bit vector of [8 x float].
/// \param __U
///    A 8-bit mask value specifying what is chosen for each element.
///    A 1 means __A and __B's dot product accumulated with __D. A 0 means 0.
/// \returns A 256-bit vector of [8 x float] comes from  Dot Product of
///  __A, __B and __D
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_maskz_dpbf16_ps(__mmask8 __U, __m256 __D, __m256bh __A, __m256bh __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                        (__v8sf)_mm256_dpbf16_ps(__D, __A, __B),
                                        (__v8sf)_mm256_setzero_si256());
}

/// Convert One Single float Data to One BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A float data.
/// \returns A bf16 data whose sign field and exponent field keep unchanged,
///    and fraction field is truncated to 7 bits.
static __inline__ __bf16 __DEFAULT_FN_ATTRS128 _mm_cvtness_sbh(float __A) {
  __v4sf __V = {__A, 0, 0, 0};
  __v8bf __R = __builtin_ia32_cvtneps2bf16_128_mask(
      (__v4sf)__V, (__v8bf)_mm_undefined_si128(), (__mmask8)-1);
  return (__bf16)__R[0];
}

/// Convert Packed BF16 Data to Packed float Data.
///
/// \headerfile <x86intrin.h>
///
/// \param __A
///    A 128-bit vector of [4 x bfloat].
/// \returns A 128-bit vector of [4 x float] come from conversion of __A
static __inline__ __m128 __DEFAULT_FN_ATTRS128 _mm_cvtpbh_ps(__m128bh __A) {
  return _mm_castsi128_ps(
      (__m128i)_mm_slli_epi32((__m128i)_mm_cvtepi16_epi32((__m128i)__A), 16));
}

/// Convert Packed BF16 Data to Packed float Data.
///
/// \headerfile <x86intrin.h>
///
/// \param __A
///    A 128-bit vector of [8 x bfloat].
/// \returns A 256-bit vector of [8 x float] come from conversion of __A
static __inline__ __m256 __DEFAULT_FN_ATTRS256 _mm256_cvtpbh_ps(__m128bh __A) {
  return _mm256_castsi256_ps((__m256i)_mm256_slli_epi32(
      (__m256i)_mm256_cvtepi16_epi32((__m128i)__A), 16));
}

/// Convert Packed BF16 Data to Packed float Data using zeroing mask.
///
/// \headerfile <x86intrin.h>
///
/// \param __U
///    A 4-bit mask. Elements are zeroed out when the corresponding mask
///    bit is not set.
/// \param __A
///    A 128-bit vector of [4 x bfloat].
/// \returns A 128-bit vector of [4 x float] come from conversion of __A
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maskz_cvtpbh_ps(__mmask8 __U, __m128bh __A) {
  return _mm_castsi128_ps((__m128i)_mm_slli_epi32(
      (__m128i)_mm_maskz_cvtepi16_epi32((__mmask8)__U, (__m128i)__A), 16));
}

/// Convert Packed BF16 Data to Packed float Data using zeroing mask.
///
/// \headerfile <x86intrin.h>
///
/// \param __U
///    A 8-bit mask. Elements are zeroed out when the corresponding mask
///    bit is not set.
/// \param __A
///    A 128-bit vector of [8 x bfloat].
/// \returns A 256-bit vector of [8 x float] come from conversion of __A
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtpbh_ps(__mmask8 __U, __m128bh __A) {
  return _mm256_castsi256_ps((__m256i)_mm256_slli_epi32(
      (__m256i)_mm256_maskz_cvtepi16_epi32((__mmask8)__U, (__m128i)__A), 16));
}

/// Convert Packed BF16 Data to Packed float Data using merging mask.
///
/// \headerfile <x86intrin.h>
///
/// \param __S
///    A 128-bit vector of [4 x float]. Elements are copied from __S when
///     the corresponding mask bit is not set.
/// \param __U
///    A 4-bit mask. Elements are zeroed out when the corresponding mask
///    bit is not set.
/// \param __A
///    A 128-bit vector of [4 x bfloat].
/// \returns A 128-bit vector of [4 x float] come from conversion of __A
static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_mask_cvtpbh_ps(__m128 __S, __mmask8 __U, __m128bh __A) {
  return _mm_castsi128_ps((__m128i)_mm_mask_slli_epi32(
      (__m128i)__S, (__mmask8)__U, (__m128i)_mm_cvtepi16_epi32((__m128i)__A),
      16));
}

/// Convert Packed BF16 Data to Packed float Data using merging mask.
///
/// \headerfile <x86intrin.h>
///
/// \param __S
///    A 256-bit vector of [8 x float]. Elements are copied from __S when
///     the corresponding mask bit is not set.
/// \param __U
///    A 8-bit mask. Elements are zeroed out when the corresponding mask
///    bit is not set.
/// \param __A
///    A 128-bit vector of [8 x bfloat].
/// \returns A 256-bit vector of [8 x float] come from conversion of __A
static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_mask_cvtpbh_ps(__m256 __S, __mmask8 __U, __m128bh __A) {
  return _mm256_castsi256_ps((__m256i)_mm256_mask_slli_epi32(
      (__m256i)__S, (__mmask8)__U, (__m256i)_mm256_cvtepi16_epi32((__m128i)__A),
      16));
}

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif
#endif
