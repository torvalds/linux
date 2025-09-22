/*===----------------- gfniintrin.h - GFNI intrinsics ----------------------===
 *
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <gfniintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __GFNIINTRIN_H
#define __GFNIINTRIN_H

/* Default attributes for simple form (no masking). */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("gfni,no-evex512"), __min_vector_width__(128)))

/* Default attributes for YMM unmasked form. */
#define __DEFAULT_FN_ATTRS_Y                                                   \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx,gfni,no-evex512"),                            \
                 __min_vector_width__(256)))

/* Default attributes for ZMM unmasked forms. */
#define __DEFAULT_FN_ATTRS_Z                                                   \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512f,evex512,gfni"),                           \
                 __min_vector_width__(512)))
/* Default attributes for ZMM masked forms. */
#define __DEFAULT_FN_ATTRS_Z_MASK                                              \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512bw,evex512,gfni"),                          \
                 __min_vector_width__(512)))

/* Default attributes for VLX masked forms. */
#define __DEFAULT_FN_ATTRS_VL128                                               \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512bw,avx512vl,gfni,no-evex512"),              \
                 __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS_VL256                                               \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512bw,avx512vl,gfni,no-evex512"),              \
                 __min_vector_width__(256)))

#define _mm_gf2p8affineinv_epi64_epi8(A, B, I) \
  ((__m128i)__builtin_ia32_vgf2p8affineinvqb_v16qi((__v16qi)(__m128i)(A), \
                                                   (__v16qi)(__m128i)(B), \
                                                   (char)(I)))

#define _mm_gf2p8affine_epi64_epi8(A, B, I) \
  ((__m128i)__builtin_ia32_vgf2p8affineqb_v16qi((__v16qi)(__m128i)(A), \
                                                   (__v16qi)(__m128i)(B), \
                                                   (char)(I)))

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_gf2p8mul_epi8(__m128i __A, __m128i __B)
{
  return (__m128i) __builtin_ia32_vgf2p8mulb_v16qi((__v16qi) __A,
              (__v16qi) __B);
}

#ifdef __AVXINTRIN_H
#define _mm256_gf2p8affineinv_epi64_epi8(A, B, I) \
  ((__m256i)__builtin_ia32_vgf2p8affineinvqb_v32qi((__v32qi)(__m256i)(A), \
                                                   (__v32qi)(__m256i)(B), \
                                                   (char)(I)))

#define _mm256_gf2p8affine_epi64_epi8(A, B, I) \
  ((__m256i)__builtin_ia32_vgf2p8affineqb_v32qi((__v32qi)(__m256i)(A), \
                                                   (__v32qi)(__m256i)(B), \
                                                   (char)(I)))

static __inline__ __m256i __DEFAULT_FN_ATTRS_Y
_mm256_gf2p8mul_epi8(__m256i __A, __m256i __B)
{
  return (__m256i) __builtin_ia32_vgf2p8mulb_v32qi((__v32qi) __A,
              (__v32qi) __B);
}
#endif /* __AVXINTRIN_H */

#ifdef __AVX512BWINTRIN_H
#define _mm512_gf2p8affineinv_epi64_epi8(A, B, I) \
  ((__m512i)__builtin_ia32_vgf2p8affineinvqb_v64qi((__v64qi)(__m512i)(A), \
                                                   (__v64qi)(__m512i)(B), \
                                                   (char)(I)))

#define _mm512_mask_gf2p8affineinv_epi64_epi8(S, U, A, B, I) \
  ((__m512i)__builtin_ia32_selectb_512((__mmask64)(U), \
         (__v64qi)_mm512_gf2p8affineinv_epi64_epi8(A, B, I), \
         (__v64qi)(__m512i)(S)))

#define _mm512_maskz_gf2p8affineinv_epi64_epi8(U, A, B, I) \
  _mm512_mask_gf2p8affineinv_epi64_epi8((__m512i)_mm512_setzero_si512(), \
         U, A, B, I)

#define _mm512_gf2p8affine_epi64_epi8(A, B, I) \
  ((__m512i)__builtin_ia32_vgf2p8affineqb_v64qi((__v64qi)(__m512i)(A), \
                                                   (__v64qi)(__m512i)(B), \
                                                   (char)(I)))

#define _mm512_mask_gf2p8affine_epi64_epi8(S, U, A, B, I) \
  ((__m512i)__builtin_ia32_selectb_512((__mmask64)(U), \
         (__v64qi)_mm512_gf2p8affine_epi64_epi8((A), (B), (I)), \
         (__v64qi)(__m512i)(S)))

#define _mm512_maskz_gf2p8affine_epi64_epi8(U, A, B, I) \
  _mm512_mask_gf2p8affine_epi64_epi8((__m512i)_mm512_setzero_si512(), \
         U, A, B, I)

static __inline__ __m512i __DEFAULT_FN_ATTRS_Z
_mm512_gf2p8mul_epi8(__m512i __A, __m512i __B)
{
  return (__m512i) __builtin_ia32_vgf2p8mulb_v64qi((__v64qi) __A,
              (__v64qi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS_Z_MASK
_mm512_mask_gf2p8mul_epi8(__m512i __S, __mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i) __builtin_ia32_selectb_512(__U,
              (__v64qi) _mm512_gf2p8mul_epi8(__A, __B),
              (__v64qi) __S);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS_Z_MASK
_mm512_maskz_gf2p8mul_epi8(__mmask64 __U, __m512i __A, __m512i __B)
{
  return _mm512_mask_gf2p8mul_epi8((__m512i)_mm512_setzero_si512(),
              __U, __A, __B);
}
#endif /* __AVX512BWINTRIN_H */

#ifdef __AVX512VLBWINTRIN_H
#define _mm_mask_gf2p8affineinv_epi64_epi8(S, U, A, B, I) \
  ((__m128i)__builtin_ia32_selectb_128((__mmask16)(U), \
         (__v16qi)_mm_gf2p8affineinv_epi64_epi8(A, B, I), \
         (__v16qi)(__m128i)(S)))

#define _mm_maskz_gf2p8affineinv_epi64_epi8(U, A, B, I) \
  _mm_mask_gf2p8affineinv_epi64_epi8((__m128i)_mm_setzero_si128(), \
         U, A, B, I)

#define _mm256_mask_gf2p8affineinv_epi64_epi8(S, U, A, B, I) \
  ((__m256i)__builtin_ia32_selectb_256((__mmask32)(U), \
         (__v32qi)_mm256_gf2p8affineinv_epi64_epi8(A, B, I), \
         (__v32qi)(__m256i)(S)))

#define _mm256_maskz_gf2p8affineinv_epi64_epi8(U, A, B, I) \
  _mm256_mask_gf2p8affineinv_epi64_epi8((__m256i)_mm256_setzero_si256(), \
         U, A, B, I)

#define _mm_mask_gf2p8affine_epi64_epi8(S, U, A, B, I) \
  ((__m128i)__builtin_ia32_selectb_128((__mmask16)(U), \
         (__v16qi)_mm_gf2p8affine_epi64_epi8(A, B, I), \
         (__v16qi)(__m128i)(S)))

#define _mm_maskz_gf2p8affine_epi64_epi8(U, A, B, I) \
  _mm_mask_gf2p8affine_epi64_epi8((__m128i)_mm_setzero_si128(), U, A, B, I)

#define _mm256_mask_gf2p8affine_epi64_epi8(S, U, A, B, I) \
  ((__m256i)__builtin_ia32_selectb_256((__mmask32)(U), \
         (__v32qi)_mm256_gf2p8affine_epi64_epi8(A, B, I), \
         (__v32qi)(__m256i)(S)))

#define _mm256_maskz_gf2p8affine_epi64_epi8(U, A, B, I) \
  _mm256_mask_gf2p8affine_epi64_epi8((__m256i)_mm256_setzero_si256(), \
         U, A, B, I)

static __inline__ __m128i __DEFAULT_FN_ATTRS_VL128
_mm_mask_gf2p8mul_epi8(__m128i __S, __mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i) __builtin_ia32_selectb_128(__U,
              (__v16qi) _mm_gf2p8mul_epi8(__A, __B),
              (__v16qi) __S);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS_VL128
_mm_maskz_gf2p8mul_epi8(__mmask16 __U, __m128i __A, __m128i __B)
{
  return _mm_mask_gf2p8mul_epi8((__m128i)_mm_setzero_si128(),
              __U, __A, __B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS_VL256
_mm256_mask_gf2p8mul_epi8(__m256i __S, __mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i) __builtin_ia32_selectb_256(__U,
              (__v32qi) _mm256_gf2p8mul_epi8(__A, __B),
              (__v32qi) __S);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS_VL256
_mm256_maskz_gf2p8mul_epi8(__mmask32 __U, __m256i __A, __m256i __B)
{
  return _mm256_mask_gf2p8mul_epi8((__m256i)_mm256_setzero_si256(),
              __U, __A, __B);
}
#endif /* __AVX512VLBWINTRIN_H */

#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS_Y
#undef __DEFAULT_FN_ATTRS_Z
#undef __DEFAULT_FN_ATTRS_VL128
#undef __DEFAULT_FN_ATTRS_VL256

#endif /* __GFNIINTRIN_H */

