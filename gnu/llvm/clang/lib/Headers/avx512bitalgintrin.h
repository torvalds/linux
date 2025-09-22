/*===------------- avx512bitalgintrin.h - BITALG intrinsics ------------------===
 *
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <avx512bitalgintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512BITALGINTRIN_H
#define __AVX512BITALGINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512bitalg,evex512"),                           \
                 __min_vector_width__(512)))

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_popcnt_epi16(__m512i __A)
{
  return (__m512i) __builtin_ia32_vpopcntw_512((__v32hi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_popcnt_epi16(__m512i __A, __mmask32 __U, __m512i __B)
{
  return (__m512i) __builtin_ia32_selectw_512((__mmask32) __U,
              (__v32hi) _mm512_popcnt_epi16(__B),
              (__v32hi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_popcnt_epi16(__mmask32 __U, __m512i __B)
{
  return _mm512_mask_popcnt_epi16((__m512i) _mm512_setzero_si512(),
              __U,
              __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_popcnt_epi8(__m512i __A)
{
  return (__m512i) __builtin_ia32_vpopcntb_512((__v64qi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_popcnt_epi8(__m512i __A, __mmask64 __U, __m512i __B)
{
  return (__m512i) __builtin_ia32_selectb_512((__mmask64) __U,
              (__v64qi) _mm512_popcnt_epi8(__B),
              (__v64qi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_popcnt_epi8(__mmask64 __U, __m512i __B)
{
  return _mm512_mask_popcnt_epi8((__m512i) _mm512_setzero_si512(),
              __U,
              __B);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_mm512_mask_bitshuffle_epi64_mask(__mmask64 __U, __m512i __A, __m512i __B)
{
  return (__mmask64) __builtin_ia32_vpshufbitqmb512_mask((__v64qi) __A,
              (__v64qi) __B,
              __U);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_mm512_bitshuffle_epi64_mask(__m512i __A, __m512i __B)
{
  return _mm512_mask_bitshuffle_epi64_mask((__mmask64) -1,
              __A,
              __B);
}


#undef __DEFAULT_FN_ATTRS

#endif
