/*===------------ avx512bf16intrin.h - AVX512_BF16 intrinsics --------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <avx512bf16intrin.h> directly; include <immintrin.h> instead."
#endif

#ifdef __SSE2__

#ifndef __AVX512BF16INTRIN_H
#define __AVX512BF16INTRIN_H

typedef __bf16 __v32bf __attribute__((__vector_size__(64), __aligned__(64)));
typedef __bf16 __m512bh __attribute__((__vector_size__(64), __aligned__(64)));
typedef __bf16 __bfloat16 __attribute__((deprecated("use __bf16 instead")));

#define __DEFAULT_FN_ATTRS512 \
  __attribute__((__always_inline__, __nodebug__, __target__("avx512bf16,evex512"), \
                 __min_vector_width__(512)))
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512bf16,no-evex512")))

/// Convert One BF16 Data to One Single Float Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic does not correspond to a specific instruction.
///
/// \param __A
///    A bfloat data.
/// \returns A float data whose sign field and exponent field keep unchanged,
///    and fraction field is extended to 23 bits.
static __inline__ float __DEFAULT_FN_ATTRS _mm_cvtsbh_ss(__bf16 __A) {
  return __builtin_ia32_cvtsbf162ss_32(__A);
}

/// Convert Two Packed Single Data to One Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNE2PS2BF16 </c> instructions.
///
/// \param __A
///    A 512-bit vector of [16 x float].
/// \param __B
///    A 512-bit vector of [16 x float].
/// \returns A 512-bit vector of [32 x bfloat] whose lower 256 bits come from
///    conversion of __B, and higher 256 bits come from conversion of __A.
static __inline__ __m512bh __DEFAULT_FN_ATTRS512
_mm512_cvtne2ps_pbh(__m512 __A, __m512 __B) {
  return (__m512bh)__builtin_ia32_cvtne2ps2bf16_512((__v16sf) __A,
                                                    (__v16sf) __B);
}

/// Convert Two Packed Single Data to One Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNE2PS2BF16 </c> instructions.
///
/// \param __A
///    A 512-bit vector of [16 x float].
/// \param __B
///    A 512-bit vector of [16 x float].
/// \param __W
///    A 512-bit vector of [32 x bfloat].
/// \param __U
///    A 32-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A or __B. A 0 means element from __W.
/// \returns A 512-bit vector of [32 x bfloat] whose lower 256 bits come from
///    conversion of __B, and higher 256 bits come from conversion of __A.
static __inline__ __m512bh __DEFAULT_FN_ATTRS512
_mm512_mask_cvtne2ps_pbh(__m512bh __W, __mmask32 __U, __m512 __A, __m512 __B) {
  return (__m512bh)__builtin_ia32_selectpbf_512((__mmask32)__U,
                                        (__v32bf)_mm512_cvtne2ps_pbh(__A, __B),
                                        (__v32bf)__W);
}

/// Convert Two Packed Single Data to One Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNE2PS2BF16 </c> instructions.
///
/// \param __A
///    A 512-bit vector of [16 x float].
/// \param __B
///    A 512-bit vector of [16 x float].
/// \param __U
///    A 32-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A or __B. A 0 means element is zero.
/// \returns A 512-bit vector of [32 x bfloat] whose lower 256 bits come from
///    conversion of __B, and higher 256 bits come from conversion of __A.
static __inline__ __m512bh __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtne2ps_pbh(__mmask32 __U, __m512 __A, __m512 __B) {
  return (__m512bh)__builtin_ia32_selectpbf_512((__mmask32)__U,
                                        (__v32bf)_mm512_cvtne2ps_pbh(__A, __B),
                                        (__v32bf)_mm512_setzero_si512());
}

/// Convert Packed Single Data to Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A 512-bit vector of [16 x float].
/// \returns A 256-bit vector of [16 x bfloat] come from conversion of __A.
static __inline__ __m256bh __DEFAULT_FN_ATTRS512
_mm512_cvtneps_pbh(__m512 __A) {
  return (__m256bh)__builtin_ia32_cvtneps2bf16_512_mask((__v16sf)__A,
                                              (__v16bf)_mm256_undefined_si256(),
                                              (__mmask16)-1);
}

/// Convert Packed Single Data to Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A 512-bit vector of [16 x float].
/// \param __W
///    A 256-bit vector of [16 x bfloat].
/// \param __U
///    A 16-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A. A 0 means element from __W.
/// \returns A 256-bit vector of [16 x bfloat] come from conversion of __A.
static __inline__ __m256bh __DEFAULT_FN_ATTRS512
_mm512_mask_cvtneps_pbh(__m256bh __W, __mmask16 __U, __m512 __A) {
  return (__m256bh)__builtin_ia32_cvtneps2bf16_512_mask((__v16sf)__A,
                                                        (__v16bf)__W,
                                                        (__mmask16)__U);
}

/// Convert Packed Single Data to Packed BF16 Data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTNEPS2BF16 </c> instructions.
///
/// \param __A
///    A 512-bit vector of [16 x float].
/// \param __U
///    A 16-bit mask value specifying what is chosen for each element.
///    A 1 means conversion of __A. A 0 means element is zero.
/// \returns A 256-bit vector of [16 x bfloat] come from conversion of __A.
static __inline__ __m256bh __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtneps_pbh(__mmask16 __U, __m512 __A) {
  return (__m256bh)__builtin_ia32_cvtneps2bf16_512_mask((__v16sf)__A,
                                                (__v16bf)_mm256_setzero_si256(),
                                                (__mmask16)__U);
}

/// Dot Product of BF16 Pairs Accumulated into Packed Single Precision.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDPBF16PS </c> instructions.
///
/// \param __A
///    A 512-bit vector of [32 x bfloat].
/// \param __B
///    A 512-bit vector of [32 x bfloat].
/// \param __D
///    A 512-bit vector of [16 x float].
/// \returns A 512-bit vector of [16 x float] comes from  Dot Product of
///  __A, __B and __D
static __inline__ __m512 __DEFAULT_FN_ATTRS512
_mm512_dpbf16_ps(__m512 __D, __m512bh __A, __m512bh __B) {
  return (__m512)__builtin_ia32_dpbf16ps_512((__v16sf) __D,
                                             (__v32bf) __A,
                                             (__v32bf) __B);
}

/// Dot Product of BF16 Pairs Accumulated into Packed Single Precision.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDPBF16PS </c> instructions.
///
/// \param __A
///    A 512-bit vector of [32 x bfloat].
/// \param __B
///    A 512-bit vector of [32 x bfloat].
/// \param __D
///    A 512-bit vector of [16 x float].
/// \param __U
///    A 16-bit mask value specifying what is chosen for each element.
///    A 1 means __A and __B's dot product accumulated with __D. A 0 means __D.
/// \returns A 512-bit vector of [16 x float] comes from  Dot Product of
///  __A, __B and __D
static __inline__ __m512 __DEFAULT_FN_ATTRS512
_mm512_mask_dpbf16_ps(__m512 __D, __mmask16 __U, __m512bh __A, __m512bh __B) {
  return (__m512)__builtin_ia32_selectps_512((__mmask16)__U,
                                       (__v16sf)_mm512_dpbf16_ps(__D, __A, __B),
                                       (__v16sf)__D);
}

/// Dot Product of BF16 Pairs Accumulated into Packed Single Precision.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDPBF16PS </c> instructions.
///
/// \param __A
///    A 512-bit vector of [32 x bfloat].
/// \param __B
///    A 512-bit vector of [32 x bfloat].
/// \param __D
///    A 512-bit vector of [16 x float].
/// \param __U
///    A 16-bit mask value specifying what is chosen for each element.
///    A 1 means __A and __B's dot product accumulated with __D. A 0 means 0.
/// \returns A 512-bit vector of [16 x float] comes from  Dot Product of
///  __A, __B and __D
static __inline__ __m512 __DEFAULT_FN_ATTRS512
_mm512_maskz_dpbf16_ps(__mmask16 __U, __m512 __D, __m512bh __A, __m512bh __B) {
  return (__m512)__builtin_ia32_selectps_512((__mmask16)__U,
                                       (__v16sf)_mm512_dpbf16_ps(__D, __A, __B),
                                       (__v16sf)_mm512_setzero_si512());
}

/// Convert Packed BF16 Data to Packed float Data.
///
/// \headerfile <x86intrin.h>
///
/// \param __A
///    A 256-bit vector of [16 x bfloat].
/// \returns A 512-bit vector of [16 x float] come from conversion of __A
static __inline__ __m512 __DEFAULT_FN_ATTRS512 _mm512_cvtpbh_ps(__m256bh __A) {
  return _mm512_castsi512_ps((__m512i)_mm512_slli_epi32(
      (__m512i)_mm512_cvtepi16_epi32((__m256i)__A), 16));
}

/// Convert Packed BF16 Data to Packed float Data using zeroing mask.
///
/// \headerfile <x86intrin.h>
///
/// \param __U
///    A 16-bit mask. Elements are zeroed out when the corresponding mask
///    bit is not set.
/// \param __A
///    A 256-bit vector of [16 x bfloat].
/// \returns A 512-bit vector of [16 x float] come from conversion of __A
static __inline__ __m512 __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtpbh_ps(__mmask16 __U, __m256bh __A) {
  return _mm512_castsi512_ps((__m512i)_mm512_slli_epi32(
      (__m512i)_mm512_maskz_cvtepi16_epi32((__mmask16)__U, (__m256i)__A), 16));
}

/// Convert Packed BF16 Data to Packed float Data using merging mask.
///
/// \headerfile <x86intrin.h>
///
/// \param __S
///    A 512-bit vector of [16 x float]. Elements are copied from __S when
///     the corresponding mask bit is not set.
/// \param __U
///    A 16-bit mask.
/// \param __A
///    A 256-bit vector of [16 x bfloat].
/// \returns A 512-bit vector of [16 x float] come from conversion of __A
static __inline__ __m512 __DEFAULT_FN_ATTRS512
_mm512_mask_cvtpbh_ps(__m512 __S, __mmask16 __U, __m256bh __A) {
  return _mm512_castsi512_ps((__m512i)_mm512_mask_slli_epi32(
      (__m512i)__S, (__mmask16)__U,
      (__m512i)_mm512_cvtepi16_epi32((__m256i)__A), 16));
}

#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS512

#endif
#endif
