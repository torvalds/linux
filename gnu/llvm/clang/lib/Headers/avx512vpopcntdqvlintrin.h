/*===---- avx512vpopcntdqintrin.h - AVX512VPOPCNTDQ intrinsics -------------===
 *
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error                                                                         \
    "Never use <avx512vpopcntdqvlintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512VPOPCNTDQVLINTRIN_H
#define __AVX512VPOPCNTDQVLINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512vpopcntdq,avx512vl,no-evex512"),            \
                 __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512vpopcntdq,avx512vl,no-evex512"),            \
                 __min_vector_width__(256)))

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_popcnt_epi64(__m128i __A) {
  return (__m128i)__builtin_ia32_vpopcntq_128((__v2di)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_popcnt_epi64(__m128i __W, __mmask8 __U, __m128i __A) {
  return (__m128i)__builtin_ia32_selectq_128(
      (__mmask8)__U, (__v2di)_mm_popcnt_epi64(__A), (__v2di)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_popcnt_epi64(__mmask8 __U, __m128i __A) {
  return _mm_mask_popcnt_epi64((__m128i)_mm_setzero_si128(), __U, __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_popcnt_epi32(__m128i __A) {
  return (__m128i)__builtin_ia32_vpopcntd_128((__v4si)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_popcnt_epi32(__m128i __W, __mmask8 __U, __m128i __A) {
  return (__m128i)__builtin_ia32_selectd_128(
      (__mmask8)__U, (__v4si)_mm_popcnt_epi32(__A), (__v4si)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_popcnt_epi32(__mmask8 __U, __m128i __A) {
  return _mm_mask_popcnt_epi32((__m128i)_mm_setzero_si128(), __U, __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_popcnt_epi64(__m256i __A) {
  return (__m256i)__builtin_ia32_vpopcntq_256((__v4di)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_popcnt_epi64(__m256i __W, __mmask8 __U, __m256i __A) {
  return (__m256i)__builtin_ia32_selectq_256(
      (__mmask8)__U, (__v4di)_mm256_popcnt_epi64(__A), (__v4di)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_popcnt_epi64(__mmask8 __U, __m256i __A) {
  return _mm256_mask_popcnt_epi64((__m256i)_mm256_setzero_si256(), __U, __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_popcnt_epi32(__m256i __A) {
  return (__m256i)__builtin_ia32_vpopcntd_256((__v8si)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_popcnt_epi32(__m256i __W, __mmask8 __U, __m256i __A) {
  return (__m256i)__builtin_ia32_selectd_256(
      (__mmask8)__U, (__v8si)_mm256_popcnt_epi32(__A), (__v8si)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_popcnt_epi32(__mmask8 __U, __m256i __A) {
  return _mm256_mask_popcnt_epi32((__m256i)_mm256_setzero_si256(), __U, __A);
}

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif
