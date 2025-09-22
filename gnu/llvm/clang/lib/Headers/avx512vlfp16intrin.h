/*===---------- avx512vlfp16intrin.h - AVX512-FP16 intrinsics --------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error                                                                         \
    "Never use <avx512vlfp16intrin.h> directly; include <immintrin.h> instead."
#endif

#ifdef __SSE2__

#ifndef __AVX512VLFP16INTRIN_H
#define __AVX512VLFP16INTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS256                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512fp16,avx512vl,no-evex512"),                 \
                 __min_vector_width__(256)))
#define __DEFAULT_FN_ATTRS128                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512fp16,avx512vl,no-evex512"),                 \
                 __min_vector_width__(128)))

static __inline__ _Float16 __DEFAULT_FN_ATTRS128 _mm_cvtsh_h(__m128h __a) {
  return __a[0];
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS256 _mm256_cvtsh_h(__m256h __a) {
  return __a[0];
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_set_sh(_Float16 __h) {
  return __extension__(__m128h){__h, 0, 0, 0, 0, 0, 0, 0};
}

static __inline __m128h __DEFAULT_FN_ATTRS128 _mm_set1_ph(_Float16 __h) {
  return (__m128h)(__v8hf){__h, __h, __h, __h, __h, __h, __h, __h};
}

static __inline __m256h __DEFAULT_FN_ATTRS256 _mm256_set1_ph(_Float16 __h) {
  return (__m256h)(__v16hf){__h, __h, __h, __h, __h, __h, __h, __h,
                            __h, __h, __h, __h, __h, __h, __h, __h};
}

static __inline __m128h __DEFAULT_FN_ATTRS128
_mm_set_ph(_Float16 __h1, _Float16 __h2, _Float16 __h3, _Float16 __h4,
           _Float16 __h5, _Float16 __h6, _Float16 __h7, _Float16 __h8) {
  return (__m128h)(__v8hf){__h8, __h7, __h6, __h5, __h4, __h3, __h2, __h1};
}

static __inline __m256h __DEFAULT_FN_ATTRS256
_mm256_set1_pch(_Float16 _Complex h) {
  return (__m256h)_mm256_set1_ps(__builtin_bit_cast(float, h));
}

static __inline __m128h __DEFAULT_FN_ATTRS128
_mm_set1_pch(_Float16 _Complex h) {
  return (__m128h)_mm_set1_ps(__builtin_bit_cast(float, h));
}

static __inline __m256h __DEFAULT_FN_ATTRS256
_mm256_set_ph(_Float16 __h1, _Float16 __h2, _Float16 __h3, _Float16 __h4,
              _Float16 __h5, _Float16 __h6, _Float16 __h7, _Float16 __h8,
              _Float16 __h9, _Float16 __h10, _Float16 __h11, _Float16 __h12,
              _Float16 __h13, _Float16 __h14, _Float16 __h15, _Float16 __h16) {
  return (__m256h)(__v16hf){__h16, __h15, __h14, __h13, __h12, __h11,
                            __h10, __h9,  __h8,  __h7,  __h6,  __h5,
                            __h4,  __h3,  __h2,  __h1};
}

#define _mm_setr_ph(h1, h2, h3, h4, h5, h6, h7, h8)                            \
  _mm_set_ph((h8), (h7), (h6), (h5), (h4), (h3), (h2), (h1))

#define _mm256_setr_ph(h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11, h12, h13, \
                       h14, h15, h16)                                          \
  _mm256_set_ph((h16), (h15), (h14), (h13), (h12), (h11), (h10), (h9), (h8),   \
                (h7), (h6), (h5), (h4), (h3), (h2), (h1))

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_add_ph(__m256h __A,
                                                              __m256h __B) {
  return (__m256h)((__v16hf)__A + (__v16hf)__B);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_add_ph(__m256h __W, __mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      __U, (__v16hf)_mm256_add_ph(__A, __B), (__v16hf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_add_ph(__mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      __U, (__v16hf)_mm256_add_ph(__A, __B), (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_add_ph(__m128h __A,
                                                           __m128h __B) {
  return (__m128h)((__v8hf)__A + (__v8hf)__B);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_add_ph(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(__U, (__v8hf)_mm_add_ph(__A, __B),
                                              (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_add_ph(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(__U, (__v8hf)_mm_add_ph(__A, __B),
                                              (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_sub_ph(__m256h __A,
                                                              __m256h __B) {
  return (__m256h)((__v16hf)__A - (__v16hf)__B);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_sub_ph(__m256h __W, __mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      __U, (__v16hf)_mm256_sub_ph(__A, __B), (__v16hf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_sub_ph(__mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      __U, (__v16hf)_mm256_sub_ph(__A, __B), (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_sub_ph(__m128h __A,
                                                           __m128h __B) {
  return (__m128h)((__v8hf)__A - (__v8hf)__B);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_sub_ph(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(__U, (__v8hf)_mm_sub_ph(__A, __B),
                                              (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_sub_ph(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(__U, (__v8hf)_mm_sub_ph(__A, __B),
                                              (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_mul_ph(__m256h __A,
                                                              __m256h __B) {
  return (__m256h)((__v16hf)__A * (__v16hf)__B);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_mul_ph(__m256h __W, __mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      __U, (__v16hf)_mm256_mul_ph(__A, __B), (__v16hf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_mul_ph(__mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      __U, (__v16hf)_mm256_mul_ph(__A, __B), (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mul_ph(__m128h __A,
                                                           __m128h __B) {
  return (__m128h)((__v8hf)__A * (__v8hf)__B);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_mul_ph(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(__U, (__v8hf)_mm_mul_ph(__A, __B),
                                              (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_mul_ph(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(__U, (__v8hf)_mm_mul_ph(__A, __B),
                                              (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_div_ph(__m256h __A,
                                                              __m256h __B) {
  return (__m256h)((__v16hf)__A / (__v16hf)__B);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_div_ph(__m256h __W, __mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      __U, (__v16hf)_mm256_div_ph(__A, __B), (__v16hf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_div_ph(__mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      __U, (__v16hf)_mm256_div_ph(__A, __B), (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_div_ph(__m128h __A,
                                                           __m128h __B) {
  return (__m128h)((__v8hf)__A / (__v8hf)__B);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_div_ph(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(__U, (__v8hf)_mm_div_ph(__A, __B),
                                              (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_div_ph(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(__U, (__v8hf)_mm_div_ph(__A, __B),
                                              (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_min_ph(__m256h __A,
                                                              __m256h __B) {
  return (__m256h)__builtin_ia32_minph256((__v16hf)__A, (__v16hf)__B);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_min_ph(__m256h __W, __mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      (__v16hf)__builtin_ia32_minph256((__v16hf)__A, (__v16hf)__B),
      (__v16hf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_min_ph(__mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      (__v16hf)__builtin_ia32_minph256((__v16hf)__A, (__v16hf)__B),
      (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_min_ph(__m128h __A,
                                                           __m128h __B) {
  return (__m128h)__builtin_ia32_minph128((__v8hf)__A, (__v8hf)__B);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_min_ph(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)__builtin_ia32_minph128((__v8hf)__A, (__v8hf)__B),
      (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_min_ph(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)__builtin_ia32_minph128((__v8hf)__A, (__v8hf)__B),
      (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_max_ph(__m256h __A,
                                                              __m256h __B) {
  return (__m256h)__builtin_ia32_maxph256((__v16hf)__A, (__v16hf)__B);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_max_ph(__m256h __W, __mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      (__v16hf)__builtin_ia32_maxph256((__v16hf)__A, (__v16hf)__B),
      (__v16hf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_max_ph(__mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      (__v16hf)__builtin_ia32_maxph256((__v16hf)__A, (__v16hf)__B),
      (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_max_ph(__m128h __A,
                                                           __m128h __B) {
  return (__m128h)__builtin_ia32_maxph128((__v8hf)__A, (__v8hf)__B);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_max_ph(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)__builtin_ia32_maxph128((__v8hf)__A, (__v8hf)__B),
      (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_max_ph(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)__builtin_ia32_maxph128((__v8hf)__A, (__v8hf)__B),
      (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_abs_ph(__m256h __A) {
  return (__m256h)_mm256_and_epi32(_mm256_set1_epi32(0x7FFF7FFF), (__m256i)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_abs_ph(__m128h __A) {
  return (__m128h)_mm_and_epi32(_mm_set1_epi32(0x7FFF7FFF), (__m128i)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_conj_pch(__m256h __A) {
  return (__m256h)_mm256_xor_ps((__m256)__A, _mm256_set1_ps(-0.0f));
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_conj_pch(__m256h __W, __mmask8 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_selectps_256(
      (__mmask8)__U, (__v8sf)_mm256_conj_pch(__A), (__v8sf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_conj_pch(__mmask8 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_selectps_256(
      (__mmask8)__U, (__v8sf)_mm256_conj_pch(__A), (__v8sf)_mm256_setzero_ps());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_conj_pch(__m128h __A) {
  return (__m128h)_mm_xor_ps((__m128)__A, _mm_set1_ps(-0.0f));
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_conj_pch(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A) {
  return (__m128h)__builtin_ia32_selectps_128(
      (__mmask8)__U, (__v4sf)_mm_conj_pch(__A), (__v4sf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_conj_pch(__mmask8 __U, __m128h __A) {
  return (__m128h)__builtin_ia32_selectps_128(
      (__mmask8)__U, (__v4sf)_mm_conj_pch(__A), (__v4sf)_mm_setzero_ps());
}

#define _mm256_cmp_ph_mask(a, b, p)                                            \
  ((__mmask16)__builtin_ia32_cmpph256_mask(                                    \
      (__v16hf)(__m256h)(a), (__v16hf)(__m256h)(b), (int)(p), (__mmask16)-1))

#define _mm256_mask_cmp_ph_mask(m, a, b, p)                                    \
  ((__mmask16)__builtin_ia32_cmpph256_mask(                                    \
      (__v16hf)(__m256h)(a), (__v16hf)(__m256h)(b), (int)(p), (__mmask16)(m)))

#define _mm_cmp_ph_mask(a, b, p)                                               \
  ((__mmask8)__builtin_ia32_cmpph128_mask(                                     \
      (__v8hf)(__m128h)(a), (__v8hf)(__m128h)(b), (int)(p), (__mmask8)-1))

#define _mm_mask_cmp_ph_mask(m, a, b, p)                                       \
  ((__mmask8)__builtin_ia32_cmpph128_mask(                                     \
      (__v8hf)(__m128h)(a), (__v8hf)(__m128h)(b), (int)(p), (__mmask8)(m)))

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_rcp_ph(__m256h __A) {
  return (__m256h)__builtin_ia32_rcpph256_mask(
      (__v16hf)__A, (__v16hf)_mm256_undefined_ph(), (__mmask16)-1);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_rcp_ph(__m256h __W, __mmask16 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_rcpph256_mask((__v16hf)__A, (__v16hf)__W,
                                               (__mmask16)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_rcp_ph(__mmask16 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_rcpph256_mask(
      (__v16hf)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_rcp_ph(__m128h __A) {
  return (__m128h)__builtin_ia32_rcpph128_mask(
      (__v8hf)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_rcp_ph(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A) {
  return (__m128h)__builtin_ia32_rcpph128_mask((__v8hf)__A, (__v8hf)__W,
                                               (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_rcp_ph(__mmask8 __U,
                                                                 __m128h __A) {
  return (__m128h)__builtin_ia32_rcpph128_mask(
      (__v8hf)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_rsqrt_ph(__m256h __A) {
  return (__m256h)__builtin_ia32_rsqrtph256_mask(
      (__v16hf)__A, (__v16hf)_mm256_undefined_ph(), (__mmask16)-1);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_rsqrt_ph(__m256h __W, __mmask16 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_rsqrtph256_mask((__v16hf)__A, (__v16hf)__W,
                                                 (__mmask16)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_rsqrt_ph(__mmask16 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_rsqrtph256_mask(
      (__v16hf)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_rsqrt_ph(__m128h __A) {
  return (__m128h)__builtin_ia32_rsqrtph128_mask(
      (__v8hf)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_rsqrt_ph(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A) {
  return (__m128h)__builtin_ia32_rsqrtph128_mask((__v8hf)__A, (__v8hf)__W,
                                                 (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_rsqrt_ph(__mmask8 __U, __m128h __A) {
  return (__m128h)__builtin_ia32_rsqrtph128_mask(
      (__v8hf)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_getexp_ph(__m128h __A) {
  return (__m128h)__builtin_ia32_getexpph128_mask(
      (__v8hf)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_getexp_ph(__m128h __W, __mmask8 __U, __m128h __A) {
  return (__m128h)__builtin_ia32_getexpph128_mask((__v8hf)__A, (__v8hf)__W,
                                                  (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_getexp_ph(__mmask8 __U, __m128h __A) {
  return (__m128h)__builtin_ia32_getexpph128_mask(
      (__v8hf)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_getexp_ph(__m256h __A) {
  return (__m256h)__builtin_ia32_getexpph256_mask(
      (__v16hf)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)-1);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_getexp_ph(__m256h __W, __mmask16 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_getexpph256_mask((__v16hf)__A, (__v16hf)__W,
                                                  (__mmask16)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_getexp_ph(__mmask16 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_getexpph256_mask(
      (__v16hf)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)__U);
}

#define _mm_getmant_ph(A, B, C)                                                \
  ((__m128h)__builtin_ia32_getmantph128_mask(                                  \
      (__v8hf)(__m128h)(A), (int)(((C) << 2) | (B)), (__v8hf)_mm_setzero_ph(), \
      (__mmask8)-1))

#define _mm_mask_getmant_ph(W, U, A, B, C)                                     \
  ((__m128h)__builtin_ia32_getmantph128_mask(                                  \
      (__v8hf)(__m128h)(A), (int)(((C) << 2) | (B)), (__v8hf)(__m128h)(W),     \
      (__mmask8)(U)))

#define _mm_maskz_getmant_ph(U, A, B, C)                                       \
  ((__m128h)__builtin_ia32_getmantph128_mask(                                  \
      (__v8hf)(__m128h)(A), (int)(((C) << 2) | (B)), (__v8hf)_mm_setzero_ph(), \
      (__mmask8)(U)))

#define _mm256_getmant_ph(A, B, C)                                             \
  ((__m256h)__builtin_ia32_getmantph256_mask(                                  \
      (__v16hf)(__m256h)(A), (int)(((C) << 2) | (B)),                          \
      (__v16hf)_mm256_setzero_ph(), (__mmask16)-1))

#define _mm256_mask_getmant_ph(W, U, A, B, C)                                  \
  ((__m256h)__builtin_ia32_getmantph256_mask(                                  \
      (__v16hf)(__m256h)(A), (int)(((C) << 2) | (B)), (__v16hf)(__m256h)(W),   \
      (__mmask16)(U)))

#define _mm256_maskz_getmant_ph(U, A, B, C)                                    \
  ((__m256h)__builtin_ia32_getmantph256_mask(                                  \
      (__v16hf)(__m256h)(A), (int)(((C) << 2) | (B)),                          \
      (__v16hf)_mm256_setzero_ph(), (__mmask16)(U)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_scalef_ph(__m128h __A,
                                                              __m128h __B) {
  return (__m128h)__builtin_ia32_scalefph128_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_scalef_ph(__m128h __W, __mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_scalefph128_mask((__v8hf)__A, (__v8hf)__B,
                                                  (__v8hf)__W, (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_scalef_ph(__mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_scalefph128_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_scalef_ph(__m256h __A,
                                                                 __m256h __B) {
  return (__m256h)__builtin_ia32_scalefph256_mask(
      (__v16hf)__A, (__v16hf)__B, (__v16hf)_mm256_setzero_ph(), (__mmask16)-1);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_scalef_ph(__m256h __W, __mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_scalefph256_mask((__v16hf)__A, (__v16hf)__B,
                                                  (__v16hf)__W, (__mmask16)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_scalef_ph(__mmask16 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_scalefph256_mask(
      (__v16hf)__A, (__v16hf)__B, (__v16hf)_mm256_setzero_ph(), (__mmask16)__U);
}

#define _mm_roundscale_ph(A, imm)                                              \
  ((__m128h)__builtin_ia32_rndscaleph_128_mask(                                \
      (__v8hf)(__m128h)(A), (int)(imm), (__v8hf)_mm_setzero_ph(),              \
      (__mmask8)-1))

#define _mm_mask_roundscale_ph(W, U, A, imm)                                   \
  ((__m128h)__builtin_ia32_rndscaleph_128_mask(                                \
      (__v8hf)(__m128h)(A), (int)(imm), (__v8hf)(__m128h)(W), (__mmask8)(U)))

#define _mm_maskz_roundscale_ph(U, A, imm)                                     \
  ((__m128h)__builtin_ia32_rndscaleph_128_mask(                                \
      (__v8hf)(__m128h)(A), (int)(imm), (__v8hf)_mm_setzero_ph(),              \
      (__mmask8)(U)))

#define _mm256_roundscale_ph(A, imm)                                           \
  ((__m256h)__builtin_ia32_rndscaleph_256_mask(                                \
      (__v16hf)(__m256h)(A), (int)(imm), (__v16hf)_mm256_setzero_ph(),         \
      (__mmask16)-1))

#define _mm256_mask_roundscale_ph(W, U, A, imm)                                \
  ((__m256h)__builtin_ia32_rndscaleph_256_mask(                                \
      (__v16hf)(__m256h)(A), (int)(imm), (__v16hf)(__m256h)(W),                \
      (__mmask16)(U)))

#define _mm256_maskz_roundscale_ph(U, A, imm)                                  \
  ((__m256h)__builtin_ia32_rndscaleph_256_mask(                                \
      (__v16hf)(__m256h)(A), (int)(imm), (__v16hf)_mm256_setzero_ph(),         \
      (__mmask16)(U)))

#define _mm_reduce_ph(A, imm)                                                  \
  ((__m128h)__builtin_ia32_reduceph128_mask((__v8hf)(__m128h)(A), (int)(imm),  \
                                            (__v8hf)_mm_setzero_ph(),          \
                                            (__mmask8)-1))

#define _mm_mask_reduce_ph(W, U, A, imm)                                       \
  ((__m128h)__builtin_ia32_reduceph128_mask(                                   \
      (__v8hf)(__m128h)(A), (int)(imm), (__v8hf)(__m128h)(W), (__mmask8)(U)))

#define _mm_maskz_reduce_ph(U, A, imm)                                         \
  ((__m128h)__builtin_ia32_reduceph128_mask((__v8hf)(__m128h)(A), (int)(imm),  \
                                            (__v8hf)_mm_setzero_ph(),          \
                                            (__mmask8)(U)))

#define _mm256_reduce_ph(A, imm)                                               \
  ((__m256h)__builtin_ia32_reduceph256_mask((__v16hf)(__m256h)(A), (int)(imm), \
                                            (__v16hf)_mm256_setzero_ph(),      \
                                            (__mmask16)-1))

#define _mm256_mask_reduce_ph(W, U, A, imm)                                    \
  ((__m256h)__builtin_ia32_reduceph256_mask((__v16hf)(__m256h)(A), (int)(imm), \
                                            (__v16hf)(__m256h)(W),             \
                                            (__mmask16)(U)))

#define _mm256_maskz_reduce_ph(U, A, imm)                                      \
  ((__m256h)__builtin_ia32_reduceph256_mask((__v16hf)(__m256h)(A), (int)(imm), \
                                            (__v16hf)_mm256_setzero_ph(),      \
                                            (__mmask16)(U)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_sqrt_ph(__m128h __a) {
  return __builtin_ia32_sqrtph((__v8hf)__a);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_sqrt_ph(__m128h __W,
                                                                 __mmask8 __U,
                                                                 __m128h __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm_sqrt_ph(__A), (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_sqrt_ph(__mmask8 __U,
                                                                  __m128h __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm_sqrt_ph(__A), (__v8hf)_mm_setzero_ph());
}

static __inline __m256h __DEFAULT_FN_ATTRS256 _mm256_sqrt_ph(__m256h __a) {
  return (__m256h)__builtin_ia32_sqrtph256((__v16hf)__a);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_sqrt_ph(__m256h __W, __mmask16 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U, (__v16hf)_mm256_sqrt_ph(__A), (__v16hf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_sqrt_ph(__mmask16 __U, __m256h __A) {
  return (__m256h)__builtin_ia32_selectph_256((__mmask16)__U,
                                              (__v16hf)_mm256_sqrt_ph(__A),
                                              (__v16hf)_mm256_setzero_ph());
}

#define _mm_mask_fpclass_ph_mask(U, A, imm)                                    \
  ((__mmask8)__builtin_ia32_fpclassph128_mask((__v8hf)(__m128h)(A),            \
                                              (int)(imm), (__mmask8)(U)))

#define _mm_fpclass_ph_mask(A, imm)                                            \
  ((__mmask8)__builtin_ia32_fpclassph128_mask((__v8hf)(__m128h)(A),            \
                                              (int)(imm), (__mmask8)-1))

#define _mm256_mask_fpclass_ph_mask(U, A, imm)                                 \
  ((__mmask16)__builtin_ia32_fpclassph256_mask((__v16hf)(__m256h)(A),          \
                                               (int)(imm), (__mmask16)(U)))

#define _mm256_fpclass_ph_mask(A, imm)                                         \
  ((__mmask16)__builtin_ia32_fpclassph256_mask((__v16hf)(__m256h)(A),          \
                                               (int)(imm), (__mmask16)-1))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtpd_ph(__m128d __A) {
  return (__m128h)__builtin_ia32_vcvtpd2ph128_mask(
      (__v2df)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_cvtpd_ph(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128d __A) {
  return (__m128h)__builtin_ia32_vcvtpd2ph128_mask((__v2df)__A, (__v8hf)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_cvtpd_ph(__mmask8 __U, __m128d __A) {
  return (__m128h)__builtin_ia32_vcvtpd2ph128_mask(
      (__v2df)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256 _mm256_cvtpd_ph(__m256d __A) {
  return (__m128h)__builtin_ia32_vcvtpd2ph256_mask(
      (__v4df)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_mask_cvtpd_ph(__m128h __W, __mmask8 __U, __m256d __A) {
  return (__m128h)__builtin_ia32_vcvtpd2ph256_mask((__v4df)__A, (__v8hf)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtpd_ph(__mmask8 __U, __m256d __A) {
  return (__m128h)__builtin_ia32_vcvtpd2ph256_mask(
      (__v4df)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128 _mm_cvtph_pd(__m128h __A) {
  return (__m128d)__builtin_ia32_vcvtph2pd128_mask(
      (__v8hf)__A, (__v2df)_mm_undefined_pd(), (__mmask8)-1);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128 _mm_mask_cvtph_pd(__m128d __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A) {
  return (__m128d)__builtin_ia32_vcvtph2pd128_mask((__v8hf)__A, (__v2df)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_maskz_cvtph_pd(__mmask8 __U, __m128h __A) {
  return (__m128d)__builtin_ia32_vcvtph2pd128_mask(
      (__v8hf)__A, (__v2df)_mm_setzero_pd(), (__mmask8)__U);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256 _mm256_cvtph_pd(__m128h __A) {
  return (__m256d)__builtin_ia32_vcvtph2pd256_mask(
      (__v8hf)__A, (__v4df)_mm256_undefined_pd(), (__mmask8)-1);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_mask_cvtph_pd(__m256d __W, __mmask8 __U, __m128h __A) {
  return (__m256d)__builtin_ia32_vcvtph2pd256_mask((__v8hf)__A, (__v4df)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtph_pd(__mmask8 __U, __m128h __A) {
  return (__m256d)__builtin_ia32_vcvtph2pd256_mask(
      (__v8hf)__A, (__v4df)_mm256_setzero_pd(), (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvtph_epi16(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2w128_mask(
      (__v8hf)__A, (__v8hi)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtph_epi16(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2w128_mask((__v8hf)__A, (__v8hi)__W,
                                                  (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtph_epi16(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2w128_mask(
      (__v8hf)__A, (__v8hi)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtph_epi16(__m256h __A) {
  return (__m256i)__builtin_ia32_vcvtph2w256_mask(
      (__v16hf)__A, (__v16hi)_mm256_undefined_si256(), (__mmask16)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtph_epi16(__m256i __W, __mmask16 __U, __m256h __A) {
  return (__m256i)__builtin_ia32_vcvtph2w256_mask((__v16hf)__A, (__v16hi)__W,
                                                  (__mmask16)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtph_epi16(__mmask16 __U, __m256h __A) {
  return (__m256i)__builtin_ia32_vcvtph2w256_mask(
      (__v16hf)__A, (__v16hi)_mm256_setzero_si256(), (__mmask16)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvttph_epi16(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2w128_mask(
      (__v8hf)__A, (__v8hi)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttph_epi16(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2w128_mask((__v8hf)__A, (__v8hi)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttph_epi16(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2w128_mask(
      (__v8hf)__A, (__v8hi)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttph_epi16(__m256h __A) {
  return (__m256i)__builtin_ia32_vcvttph2w256_mask(
      (__v16hf)__A, (__v16hi)_mm256_undefined_si256(), (__mmask16)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttph_epi16(__m256i __W, __mmask16 __U, __m256h __A) {
  return (__m256i)__builtin_ia32_vcvttph2w256_mask((__v16hf)__A, (__v16hi)__W,
                                                   (__mmask16)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttph_epi16(__mmask16 __U, __m256h __A) {
  return (__m256i)__builtin_ia32_vcvttph2w256_mask(
      (__v16hf)__A, (__v16hi)_mm256_setzero_si256(), (__mmask16)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtepi16_ph(__m128i __A) {
  return (__m128h) __builtin_convertvector((__v8hi)__A, __v8hf);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_cvtepi16_ph(__m128h __W, __mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm_cvtepi16_ph(__A), (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepi16_ph(__mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm_cvtepi16_ph(__A), (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_cvtepi16_ph(__m256i __A) {
  return (__m256h) __builtin_convertvector((__v16hi)__A, __v16hf);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepi16_ph(__m256h __W, __mmask16 __U, __m256i __A) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U, (__v16hf)_mm256_cvtepi16_ph(__A), (__v16hf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepi16_ph(__mmask16 __U, __m256i __A) {
  return (__m256h)__builtin_ia32_selectph_256((__mmask16)__U,
                                              (__v16hf)_mm256_cvtepi16_ph(__A),
                                              (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvtph_epu16(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2uw128_mask(
      (__v8hf)__A, (__v8hu)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtph_epu16(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2uw128_mask((__v8hf)__A, (__v8hu)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtph_epu16(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2uw128_mask(
      (__v8hf)__A, (__v8hu)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtph_epu16(__m256h __A) {
  return (__m256i)__builtin_ia32_vcvtph2uw256_mask(
      (__v16hf)__A, (__v16hu)_mm256_undefined_si256(), (__mmask16)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtph_epu16(__m256i __W, __mmask16 __U, __m256h __A) {
  return (__m256i)__builtin_ia32_vcvtph2uw256_mask((__v16hf)__A, (__v16hu)__W,
                                                   (__mmask16)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtph_epu16(__mmask16 __U, __m256h __A) {
  return (__m256i)__builtin_ia32_vcvtph2uw256_mask(
      (__v16hf)__A, (__v16hu)_mm256_setzero_si256(), (__mmask16)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvttph_epu16(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2uw128_mask(
      (__v8hf)__A, (__v8hu)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttph_epu16(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2uw128_mask((__v8hf)__A, (__v8hu)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttph_epu16(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2uw128_mask(
      (__v8hf)__A, (__v8hu)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttph_epu16(__m256h __A) {
  return (__m256i)__builtin_ia32_vcvttph2uw256_mask(
      (__v16hf)__A, (__v16hu)_mm256_undefined_si256(), (__mmask16)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttph_epu16(__m256i __W, __mmask16 __U, __m256h __A) {
  return (__m256i)__builtin_ia32_vcvttph2uw256_mask((__v16hf)__A, (__v16hu)__W,
                                                    (__mmask16)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttph_epu16(__mmask16 __U, __m256h __A) {
  return (__m256i)__builtin_ia32_vcvttph2uw256_mask(
      (__v16hf)__A, (__v16hu)_mm256_setzero_si256(), (__mmask16)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtepu16_ph(__m128i __A) {
  return (__m128h) __builtin_convertvector((__v8hu)__A, __v8hf);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_cvtepu16_ph(__m128h __W, __mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm_cvtepu16_ph(__A), (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepu16_ph(__mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm_cvtepu16_ph(__A), (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_cvtepu16_ph(__m256i __A) {
  return (__m256h) __builtin_convertvector((__v16hu)__A, __v16hf);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepu16_ph(__m256h __W, __mmask16 __U, __m256i __A) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U, (__v16hf)_mm256_cvtepu16_ph(__A), (__v16hf)__W);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepu16_ph(__mmask16 __U, __m256i __A) {
  return (__m256h)__builtin_ia32_selectph_256((__mmask16)__U,
                                              (__v16hf)_mm256_cvtepu16_ph(__A),
                                              (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvtph_epi32(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2dq128_mask(
      (__v8hf)__A, (__v4si)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtph_epi32(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2dq128_mask((__v8hf)__A, (__v4si)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtph_epi32(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2dq128_mask(
      (__v8hf)__A, (__v4si)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtph_epi32(__m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2dq256_mask(
      (__v8hf)__A, (__v8si)_mm256_undefined_si256(), (__mmask8)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtph_epi32(__m256i __W, __mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2dq256_mask((__v8hf)__A, (__v8si)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtph_epi32(__mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2dq256_mask(
      (__v8hf)__A, (__v8si)_mm256_setzero_si256(), (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvtph_epu32(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2udq128_mask(
      (__v8hf)__A, (__v4su)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtph_epu32(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2udq128_mask((__v8hf)__A, (__v4su)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtph_epu32(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2udq128_mask(
      (__v8hf)__A, (__v4su)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtph_epu32(__m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2udq256_mask(
      (__v8hf)__A, (__v8su)_mm256_undefined_si256(), (__mmask8)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtph_epu32(__m256i __W, __mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2udq256_mask((__v8hf)__A, (__v8su)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtph_epu32(__mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2udq256_mask(
      (__v8hf)__A, (__v8su)_mm256_setzero_si256(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtepi32_ph(__m128i __A) {
  return (__m128h)__builtin_ia32_vcvtdq2ph128_mask(
      (__v4si)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_cvtepi32_ph(__m128h __W, __mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_vcvtdq2ph128_mask((__v4si)__A, (__v8hf)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepi32_ph(__mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_vcvtdq2ph128_mask(
      (__v4si)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_cvtepi32_ph(__m256i __A) {
  return (__m128h) __builtin_convertvector((__v8si)__A, __v8hf);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepi32_ph(__m128h __W, __mmask8 __U, __m256i __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm256_cvtepi32_ph(__A), (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepi32_ph(__mmask8 __U, __m256i __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm256_cvtepi32_ph(__A), (__v8hf)_mm_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtepu32_ph(__m128i __A) {
  return (__m128h)__builtin_ia32_vcvtudq2ph128_mask(
      (__v4su)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_cvtepu32_ph(__m128h __W, __mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_vcvtudq2ph128_mask((__v4su)__A, (__v8hf)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepu32_ph(__mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_vcvtudq2ph128_mask(
      (__v4su)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_cvtepu32_ph(__m256i __A) {
  return (__m128h) __builtin_convertvector((__v8su)__A, __v8hf);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepu32_ph(__m128h __W, __mmask8 __U, __m256i __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm256_cvtepu32_ph(__A), (__v8hf)__W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepu32_ph(__mmask8 __U, __m256i __A) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, (__v8hf)_mm256_cvtepu32_ph(__A), (__v8hf)_mm_setzero_ph());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvttph_epi32(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2dq128_mask(
      (__v8hf)__A, (__v4si)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttph_epi32(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2dq128_mask((__v8hf)__A, (__v4si)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttph_epi32(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2dq128_mask(
      (__v8hf)__A, (__v4si)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttph_epi32(__m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2dq256_mask(
      (__v8hf)__A, (__v8si)_mm256_undefined_si256(), (__mmask8)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttph_epi32(__m256i __W, __mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2dq256_mask((__v8hf)__A, (__v8si)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttph_epi32(__mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2dq256_mask(
      (__v8hf)__A, (__v8si)_mm256_setzero_si256(), (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvttph_epu32(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2udq128_mask(
      (__v8hf)__A, (__v4su)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttph_epu32(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2udq128_mask((__v8hf)__A, (__v4su)__W,
                                                     (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttph_epu32(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2udq128_mask(
      (__v8hf)__A, (__v4su)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttph_epu32(__m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2udq256_mask(
      (__v8hf)__A, (__v8su)_mm256_undefined_si256(), (__mmask8)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttph_epu32(__m256i __W, __mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2udq256_mask((__v8hf)__A, (__v8su)__W,
                                                     (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttph_epu32(__mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2udq256_mask(
      (__v8hf)__A, (__v8su)_mm256_setzero_si256(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtepi64_ph(__m128i __A) {
  return (__m128h)__builtin_ia32_vcvtqq2ph128_mask(
      (__v2di)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_cvtepi64_ph(__m128h __W, __mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_vcvtqq2ph128_mask((__v2di)__A, (__v8hf)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepi64_ph(__mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_vcvtqq2ph128_mask(
      (__v2di)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_cvtepi64_ph(__m256i __A) {
  return (__m128h)__builtin_ia32_vcvtqq2ph256_mask(
      (__v4di)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepi64_ph(__m128h __W, __mmask8 __U, __m256i __A) {
  return (__m128h)__builtin_ia32_vcvtqq2ph256_mask((__v4di)__A, (__v8hf)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepi64_ph(__mmask8 __U, __m256i __A) {
  return (__m128h)__builtin_ia32_vcvtqq2ph256_mask(
      (__v4di)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvtph_epi64(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2qq128_mask(
      (__v8hf)__A, (__v2di)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtph_epi64(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2qq128_mask((__v8hf)__A, (__v2di)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtph_epi64(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2qq128_mask(
      (__v8hf)__A, (__v2di)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtph_epi64(__m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2qq256_mask(
      (__v8hf)__A, (__v4di)_mm256_undefined_si256(), (__mmask8)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtph_epi64(__m256i __W, __mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2qq256_mask((__v8hf)__A, (__v4di)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtph_epi64(__mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2qq256_mask(
      (__v8hf)__A, (__v4di)_mm256_setzero_si256(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtepu64_ph(__m128i __A) {
  return (__m128h)__builtin_ia32_vcvtuqq2ph128_mask(
      (__v2du)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_cvtepu64_ph(__m128h __W, __mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_vcvtuqq2ph128_mask((__v2du)__A, (__v8hf)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepu64_ph(__mmask8 __U, __m128i __A) {
  return (__m128h)__builtin_ia32_vcvtuqq2ph128_mask(
      (__v2du)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_cvtepu64_ph(__m256i __A) {
  return (__m128h)__builtin_ia32_vcvtuqq2ph256_mask(
      (__v4du)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepu64_ph(__m128h __W, __mmask8 __U, __m256i __A) {
  return (__m128h)__builtin_ia32_vcvtuqq2ph256_mask((__v4du)__A, (__v8hf)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepu64_ph(__mmask8 __U, __m256i __A) {
  return (__m128h)__builtin_ia32_vcvtuqq2ph256_mask(
      (__v4du)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvtph_epu64(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2uqq128_mask(
      (__v8hf)__A, (__v2du)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtph_epu64(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2uqq128_mask((__v8hf)__A, (__v2du)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtph_epu64(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvtph2uqq128_mask(
      (__v8hf)__A, (__v2du)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtph_epu64(__m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2uqq256_mask(
      (__v8hf)__A, (__v4du)_mm256_undefined_si256(), (__mmask8)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtph_epu64(__m256i __W, __mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2uqq256_mask((__v8hf)__A, (__v4du)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtph_epu64(__mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvtph2uqq256_mask(
      (__v8hf)__A, (__v4du)_mm256_setzero_si256(), (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvttph_epi64(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2qq128_mask(
      (__v8hf)__A, (__v2di)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttph_epi64(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2qq128_mask((__v8hf)__A, (__v2di)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttph_epi64(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2qq128_mask(
      (__v8hf)__A, (__v2di)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttph_epi64(__m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2qq256_mask(
      (__v8hf)__A, (__v4di)_mm256_undefined_si256(), (__mmask8)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttph_epi64(__m256i __W, __mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2qq256_mask((__v8hf)__A, (__v4di)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttph_epi64(__mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2qq256_mask(
      (__v8hf)__A, (__v4di)_mm256_setzero_si256(), (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvttph_epu64(__m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2uqq128_mask(
      (__v8hf)__A, (__v2du)_mm_undefined_si128(), (__mmask8)-1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttph_epu64(__m128i __W, __mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2uqq128_mask((__v8hf)__A, (__v2du)__W,
                                                     (__mmask8)__U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttph_epu64(__mmask8 __U, __m128h __A) {
  return (__m128i)__builtin_ia32_vcvttph2uqq128_mask(
      (__v8hf)__A, (__v2du)_mm_setzero_si128(), (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttph_epu64(__m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2uqq256_mask(
      (__v8hf)__A, (__v4du)_mm256_undefined_si256(), (__mmask8)-1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttph_epu64(__m256i __W, __mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2uqq256_mask((__v8hf)__A, (__v4du)__W,
                                                     (__mmask8)__U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttph_epu64(__mmask8 __U, __m128h __A) {
  return (__m256i)__builtin_ia32_vcvttph2uqq256_mask(
      (__v8hf)__A, (__v4du)_mm256_setzero_si256(), (__mmask8)__U);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128 _mm_cvtxph_ps(__m128h __A) {
  return (__m128)__builtin_ia32_vcvtph2psx128_mask(
      (__v8hf)__A, (__v4sf)_mm_undefined_ps(), (__mmask8)-1);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128 _mm_mask_cvtxph_ps(__m128 __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A) {
  return (__m128)__builtin_ia32_vcvtph2psx128_mask((__v8hf)__A, (__v4sf)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maskz_cvtxph_ps(__mmask8 __U, __m128h __A) {
  return (__m128)__builtin_ia32_vcvtph2psx128_mask(
      (__v8hf)__A, (__v4sf)_mm_setzero_ps(), (__mmask8)__U);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256 _mm256_cvtxph_ps(__m128h __A) {
  return (__m256)__builtin_ia32_vcvtph2psx256_mask(
      (__v8hf)__A, (__v8sf)_mm256_undefined_ps(), (__mmask8)-1);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_mask_cvtxph_ps(__m256 __W, __mmask8 __U, __m128h __A) {
  return (__m256)__builtin_ia32_vcvtph2psx256_mask((__v8hf)__A, (__v8sf)__W,
                                                   (__mmask8)__U);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtxph_ps(__mmask8 __U, __m128h __A) {
  return (__m256)__builtin_ia32_vcvtph2psx256_mask(
      (__v8hf)__A, (__v8sf)_mm256_setzero_ps(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtxps_ph(__m128 __A) {
  return (__m128h)__builtin_ia32_vcvtps2phx128_mask(
      (__v4sf)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_cvtxps_ph(__m128h __W,
                                                                   __mmask8 __U,
                                                                   __m128 __A) {
  return (__m128h)__builtin_ia32_vcvtps2phx128_mask((__v4sf)__A, (__v8hf)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_cvtxps_ph(__mmask8 __U, __m128 __A) {
  return (__m128h)__builtin_ia32_vcvtps2phx128_mask(
      (__v4sf)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256 _mm256_cvtxps_ph(__m256 __A) {
  return (__m128h)__builtin_ia32_vcvtps2phx256_mask(
      (__v8sf)__A, (__v8hf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_mask_cvtxps_ph(__m128h __W, __mmask8 __U, __m256 __A) {
  return (__m128h)__builtin_ia32_vcvtps2phx256_mask((__v8sf)__A, (__v8hf)__W,
                                                    (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtxps_ph(__mmask8 __U, __m256 __A) {
  return (__m128h)__builtin_ia32_vcvtps2phx256_mask(
      (__v8sf)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmadd_ph(__m128h __A,
                                                             __m128h __B,
                                                             __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddph((__v8hf)__A, (__v8hf)__B,
                                          (__v8hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_fmadd_ph(__m128h __A,
                                                                  __mmask8 __U,
                                                                  __m128h __B,
                                                                  __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph((__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fmadd_ph(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph((__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmadd_ph(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph((__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)_mm_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmsub_ph(__m128h __A,
                                                             __m128h __B,
                                                             __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddph((__v8hf)__A, (__v8hf)__B,
                                          -(__v8hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_fmsub_ph(__m128h __A,
                                                                  __mmask8 __U,
                                                                  __m128h __B,
                                                                  __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, _mm_fmsub_ph((__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmsub_ph(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U, _mm_fmsub_ph((__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)_mm_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fnmadd_ph(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph(-(__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fnmadd_ph(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph(-(__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)_mm_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fnmsub_ph(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph(-(__v8hf)__A, (__v8hf)__B, -(__v8hf)__C),
      (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_fmadd_ph(__m256h __A,
                                                                __m256h __B,
                                                                __m256h __C) {
  return (__m256h)__builtin_ia32_vfmaddph256((__v16hf)__A, (__v16hf)__B,
                                             (__v16hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fmadd_ph(__m256h __A, __mmask16 __U, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256((__v16hf)__A, (__v16hf)__B, (__v16hf)__C),
      (__v16hf)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask3_fmadd_ph(__m256h __A, __m256h __B, __m256h __C, __mmask16 __U) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256((__v16hf)__A, (__v16hf)__B, (__v16hf)__C),
      (__v16hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fmadd_ph(__mmask16 __U, __m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256((__v16hf)__A, (__v16hf)__B, (__v16hf)__C),
      (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_fmsub_ph(__m256h __A,
                                                                __m256h __B,
                                                                __m256h __C) {
  return (__m256h)__builtin_ia32_vfmaddph256((__v16hf)__A, (__v16hf)__B,
                                             -(__v16hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fmsub_ph(__m256h __A, __mmask16 __U, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256((__v16hf)__A, (__v16hf)__B, -(__v16hf)__C),
      (__v16hf)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fmsub_ph(__mmask16 __U, __m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256((__v16hf)__A, (__v16hf)__B, -(__v16hf)__C),
      (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask3_fnmadd_ph(__m256h __A, __m256h __B, __m256h __C, __mmask16 __U) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256(-(__v16hf)__A, (__v16hf)__B, (__v16hf)__C),
      (__v16hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fnmadd_ph(__mmask16 __U, __m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256(-(__v16hf)__A, (__v16hf)__B, (__v16hf)__C),
      (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fnmsub_ph(__mmask16 __U, __m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256(-(__v16hf)__A, (__v16hf)__B, -(__v16hf)__C),
      (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmaddsub_ph(__m128h __A,
                                                                __m128h __B,
                                                                __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddsubph((__v8hf)__A, (__v8hf)__B,
                                             (__v8hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fmaddsub_ph(__m128h __A, __mmask8 __U, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddsubph((__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fmaddsub_ph(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddsubph((__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmaddsub_ph(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddsubph((__v8hf)__A, (__v8hf)__B, (__v8hf)__C),
      (__v8hf)_mm_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmsubadd_ph(__m128h __A,
                                                                __m128h __B,
                                                                __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddsubph((__v8hf)__A, (__v8hf)__B,
                                             -(__v8hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fmsubadd_ph(__m128h __A, __mmask8 __U, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddsubph((__v8hf)__A, (__v8hf)__B, -(__v8hf)__C),
      (__v8hf)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmsubadd_ph(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddsubph((__v8hf)__A, (__v8hf)__B, -(__v8hf)__C),
      (__v8hf)_mm_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_fmaddsub_ph(__m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_vfmaddsubph256((__v16hf)__A, (__v16hf)__B,
                                                (__v16hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fmaddsub_ph(__m256h __A, __mmask16 __U, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddsubph256((__v16hf)__A, (__v16hf)__B, (__v16hf)__C),
      (__v16hf)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask3_fmaddsub_ph(__m256h __A, __m256h __B, __m256h __C, __mmask16 __U) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddsubph256((__v16hf)__A, (__v16hf)__B, (__v16hf)__C),
      (__v16hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fmaddsub_ph(__mmask16 __U, __m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddsubph256((__v16hf)__A, (__v16hf)__B, (__v16hf)__C),
      (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_fmsubadd_ph(__m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_vfmaddsubph256((__v16hf)__A, (__v16hf)__B,
                                                -(__v16hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fmsubadd_ph(__m256h __A, __mmask16 __U, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddsubph256((__v16hf)__A, (__v16hf)__B, -(__v16hf)__C),
      (__v16hf)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fmsubadd_ph(__mmask16 __U, __m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddsubph256((__v16hf)__A, (__v16hf)__B, -(__v16hf)__C),
      (__v16hf)_mm256_setzero_ph());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fmsub_ph(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph((__v8hf)__A, (__v8hf)__B, -(__v8hf)__C),
      (__v8hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask3_fmsub_ph(__m256h __A, __m256h __B, __m256h __C, __mmask16 __U) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256((__v16hf)__A, (__v16hf)__B, -(__v16hf)__C),
      (__v16hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fmsubadd_ph(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddsubph((__v8hf)__A, (__v8hf)__B, -(__v8hf)__C),
      (__v8hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask3_fmsubadd_ph(__m256h __A, __m256h __B, __m256h __C, __mmask16 __U) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddsubph256((__v16hf)__A, (__v16hf)__B, -(__v16hf)__C),
      (__v16hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fnmadd_ph(__m128h __A,
                                                              __m128h __B,
                                                              __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddph((__v8hf)__A, -(__v8hf)__B,
                                          (__v8hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fnmadd_ph(__m128h __A, __mmask8 __U, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph((__v8hf)__A, -(__v8hf)__B, (__v8hf)__C),
      (__v8hf)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_fnmadd_ph(__m256h __A,
                                                                 __m256h __B,
                                                                 __m256h __C) {
  return (__m256h)__builtin_ia32_vfmaddph256((__v16hf)__A, -(__v16hf)__B,
                                             (__v16hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fnmadd_ph(__m256h __A, __mmask16 __U, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256((__v16hf)__A, -(__v16hf)__B, (__v16hf)__C),
      (__v16hf)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fnmsub_ph(__m128h __A,
                                                              __m128h __B,
                                                              __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddph((__v8hf)__A, -(__v8hf)__B,
                                          -(__v8hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fnmsub_ph(__m128h __A, __mmask8 __U, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph((__v8hf)__A, -(__v8hf)__B, -(__v8hf)__C),
      (__v8hf)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fnmsub_ph(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_selectph_128(
      (__mmask8)__U,
      __builtin_ia32_vfmaddph((__v8hf)__A, -(__v8hf)__B, -(__v8hf)__C),
      (__v8hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_fnmsub_ph(__m256h __A,
                                                                 __m256h __B,
                                                                 __m256h __C) {
  return (__m256h)__builtin_ia32_vfmaddph256((__v16hf)__A, -(__v16hf)__B,
                                             -(__v16hf)__C);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fnmsub_ph(__m256h __A, __mmask16 __U, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256((__v16hf)__A, -(__v16hf)__B, -(__v16hf)__C),
      (__v16hf)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask3_fnmsub_ph(__m256h __A, __m256h __B, __m256h __C, __mmask16 __U) {
  return (__m256h)__builtin_ia32_selectph_256(
      (__mmask16)__U,
      __builtin_ia32_vfmaddph256((__v16hf)__A, -(__v16hf)__B, -(__v16hf)__C),
      (__v16hf)__C);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fcmul_pch(__m128h __A,
                                                              __m128h __B) {
  return (__m128h)__builtin_ia32_vfcmulcph128_mask(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fcmul_pch(__m128h __W, __mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_vfcmulcph128_mask((__v4sf)__A, (__v4sf)__B,
                                                   (__v4sf)__W, (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fcmul_pch(__mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_vfcmulcph128_mask(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS128 _mm256_fcmul_pch(__m256h __A,
                                                                 __m256h __B) {
  return (__m256h)__builtin_ia32_vfcmulcph256_mask(
      (__v8sf)__A, (__v8sf)__B, (__v8sf)_mm256_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fcmul_pch(__m256h __W, __mmask8 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_vfcmulcph256_mask((__v8sf)__A, (__v8sf)__B,
                                                   (__v8sf)__W, (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fcmul_pch(__mmask8 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_vfcmulcph256_mask(
      (__v8sf)__A, (__v8sf)__B, (__v8sf)_mm256_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fcmadd_pch(__m128h __A,
                                                               __m128h __B,
                                                               __m128h __C) {
  return (__m128h)__builtin_ia32_vfcmaddcph128_mask((__v4sf)__A, (__v4sf)__B,
                                                    (__v4sf)__C, (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fcmadd_pch(__m128h __A, __mmask8 __U, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectps_128(
      __U,
      __builtin_ia32_vfcmaddcph128_mask((__v4sf)__A, (__v4sf)(__m128h)__B,
                                        (__v4sf)__C, (__mmask8)__U),
      (__v4sf)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fcmadd_pch(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_vfcmaddcph128_mask((__v4sf)__A, (__v4sf)__B,
                                                    (__v4sf)__C, (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fcmadd_pch(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_vfcmaddcph128_maskz(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)__C, (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_fcmadd_pch(__m256h __A,
                                                                  __m256h __B,
                                                                  __m256h __C) {
  return (__m256h)__builtin_ia32_vfcmaddcph256_mask((__v8sf)__A, (__v8sf)__B,
                                                    (__v8sf)__C, (__mmask8)-1);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fcmadd_pch(__m256h __A, __mmask8 __U, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectps_256(
      __U,
      __builtin_ia32_vfcmaddcph256_mask((__v8sf)__A, (__v8sf)__B, (__v8sf)__C,
                                        (__mmask8)__U),
      (__v8sf)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask3_fcmadd_pch(__m256h __A, __m256h __B, __m256h __C, __mmask8 __U) {
  return (__m256h)__builtin_ia32_vfcmaddcph256_mask((__v8sf)__A, (__v8sf)__B,
                                                    (__v8sf)__C, (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fcmadd_pch(__mmask8 __U, __m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_vfcmaddcph256_maskz(
      (__v8sf)__A, (__v8sf)__B, (__v8sf)__C, (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmul_pch(__m128h __A,
                                                             __m128h __B) {
  return (__m128h)__builtin_ia32_vfmulcph128_mask(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)_mm_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_fmul_pch(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A,
                                                                  __m128h __B) {
  return (__m128h)__builtin_ia32_vfmulcph128_mask((__v4sf)__A, (__v4sf)__B,
                                                  (__v4sf)__W, (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmul_pch(__mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_vfmulcph128_mask(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_fmul_pch(__m256h __A,
                                                                __m256h __B) {
  return (__m256h)__builtin_ia32_vfmulcph256_mask(
      (__v8sf)__A, (__v8sf)__B, (__v8sf)_mm256_undefined_ph(), (__mmask8)-1);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fmul_pch(__m256h __W, __mmask8 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_vfmulcph256_mask((__v8sf)__A, (__v8sf)__B,
                                                  (__v8sf)__W, (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fmul_pch(__mmask8 __U, __m256h __A, __m256h __B) {
  return (__m256h)__builtin_ia32_vfmulcph256_mask(
      (__v8sf)__A, (__v8sf)__B, (__v8sf)_mm256_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmadd_pch(__m128h __A,
                                                              __m128h __B,
                                                              __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddcph128_mask((__v4sf)__A, (__v4sf)__B,
                                                   (__v4sf)__C, (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fmadd_pch(__m128h __A, __mmask8 __U, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_selectps_128(
      __U,
      __builtin_ia32_vfmaddcph128_mask((__v4sf)__A, (__v4sf)__B, (__v4sf)__C,
                                       (__mmask8)__U),
      (__v4sf)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fmadd_pch(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_vfmaddcph128_mask((__v4sf)__A, (__v4sf)__B,
                                                   (__v4sf)__C, (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmadd_pch(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddcph128_maskz((__v4sf)__A, (__v4sf)__B,
                                                    (__v4sf)__C, (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_fmadd_pch(__m256h __A,
                                                                 __m256h __B,
                                                                 __m256h __C) {
  return (__m256h)__builtin_ia32_vfmaddcph256_mask((__v8sf)__A, (__v8sf)__B,
                                                   (__v8sf)__C, (__mmask8)-1);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_fmadd_pch(__m256h __A, __mmask8 __U, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_selectps_256(
      __U,
      __builtin_ia32_vfmaddcph256_mask((__v8sf)__A, (__v8sf)__B, (__v8sf)__C,
                                       (__mmask8)__U),
      (__v8sf)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask3_fmadd_pch(__m256h __A, __m256h __B, __m256h __C, __mmask8 __U) {
  return (__m256h)__builtin_ia32_vfmaddcph256_mask((__v8sf)__A, (__v8sf)__B,
                                                   (__v8sf)__C, (__mmask8)__U);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_maskz_fmadd_pch(__mmask8 __U, __m256h __A, __m256h __B, __m256h __C) {
  return (__m256h)__builtin_ia32_vfmaddcph256_maskz((__v8sf)__A, (__v8sf)__B,
                                                    (__v8sf)__C, (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_blend_ph(__mmask8 __U,
                                                                  __m128h __A,
                                                                  __m128h __W) {
  return (__m128h)__builtin_ia32_selectph_128((__mmask8)__U, (__v8hf)__W,
                                              (__v8hf)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_mask_blend_ph(__mmask16 __U, __m256h __A, __m256h __W) {
  return (__m256h)__builtin_ia32_selectph_256((__mmask16)__U, (__v16hf)__W,
                                              (__v16hf)__A);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_permutex2var_ph(__m128h __A, __m128i __I, __m128h __B) {
  return (__m128h)__builtin_ia32_vpermi2varhi128((__v8hi)__A, (__v8hi)__I,
                                                 (__v8hi)__B);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_permutex2var_ph(__m256h __A, __m256i __I, __m256h __B) {
  return (__m256h)__builtin_ia32_vpermi2varhi256((__v16hi)__A, (__v16hi)__I,
                                                 (__v16hi)__B);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_permutexvar_ph(__m128i __A, __m128h __B) {
  return (__m128h)__builtin_ia32_permvarhi128((__v8hi)__B, (__v8hi)__A);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_permutexvar_ph(__m256i __A, __m256h __B) {
  return (__m256h)__builtin_ia32_permvarhi256((__v16hi)__B, (__v16hi)__A);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS256
_mm256_reduce_add_ph(__m256h __W) {
  return __builtin_ia32_reduce_fadd_ph256(-0.0f16, __W);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS256
_mm256_reduce_mul_ph(__m256h __W) {
  return __builtin_ia32_reduce_fmul_ph256(1.0f16, __W);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS256
_mm256_reduce_max_ph(__m256h __V) {
  return __builtin_ia32_reduce_fmax_ph256(__V);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS256
_mm256_reduce_min_ph(__m256h __V) {
  return __builtin_ia32_reduce_fmin_ph256(__V);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS128
_mm_reduce_add_ph(__m128h __W) {
  return __builtin_ia32_reduce_fadd_ph128(-0.0f16, __W);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS128
_mm_reduce_mul_ph(__m128h __W) {
  return __builtin_ia32_reduce_fmul_ph128(1.0f16, __W);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS128
_mm_reduce_max_ph(__m128h __V) {
  return __builtin_ia32_reduce_fmax_ph128(__V);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS128
_mm_reduce_min_ph(__m128h __V) {
  return __builtin_ia32_reduce_fmin_ph128(__V);
}

// intrinsics below are alias for f*mul_*ch
#define _mm_mul_pch(A, B) _mm_fmul_pch(A, B)
#define _mm_mask_mul_pch(W, U, A, B) _mm_mask_fmul_pch(W, U, A, B)
#define _mm_maskz_mul_pch(U, A, B) _mm_maskz_fmul_pch(U, A, B)
#define _mm256_mul_pch(A, B) _mm256_fmul_pch(A, B)
#define _mm256_mask_mul_pch(W, U, A, B) _mm256_mask_fmul_pch(W, U, A, B)
#define _mm256_maskz_mul_pch(U, A, B) _mm256_maskz_fmul_pch(U, A, B)

#define _mm_cmul_pch(A, B) _mm_fcmul_pch(A, B)
#define _mm_mask_cmul_pch(W, U, A, B) _mm_mask_fcmul_pch(W, U, A, B)
#define _mm_maskz_cmul_pch(U, A, B) _mm_maskz_fcmul_pch(U, A, B)
#define _mm256_cmul_pch(A, B) _mm256_fcmul_pch(A, B)
#define _mm256_mask_cmul_pch(W, U, A, B) _mm256_mask_fcmul_pch(W, U, A, B)
#define _mm256_maskz_cmul_pch(U, A, B) _mm256_maskz_fcmul_pch(U, A, B)

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif
#endif
