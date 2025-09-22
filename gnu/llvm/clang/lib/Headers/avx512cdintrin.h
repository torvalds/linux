/*===------------- avx512cdintrin.h - AVX512CD intrinsics ------------------===
 *
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <avx512cdintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512CDINTRIN_H
#define __AVX512CDINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512cd,evex512"), __min_vector_width__(512)))

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_conflict_epi64 (__m512i __A)
{
  return (__m512i) __builtin_ia32_vpconflictdi_512 ((__v8di) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_conflict_epi64 (__m512i __W, __mmask8 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectq_512((__mmask8)__U,
                                             (__v8di)_mm512_conflict_epi64(__A),
                                             (__v8di)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_conflict_epi64 (__mmask8 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectq_512((__mmask8)__U,
                                             (__v8di)_mm512_conflict_epi64(__A),
                                             (__v8di)_mm512_setzero_si512 ());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_conflict_epi32 (__m512i __A)
{
  return (__m512i) __builtin_ia32_vpconflictsi_512 ((__v16si) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_conflict_epi32 (__m512i __W, __mmask16 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectd_512((__mmask16)__U,
                                            (__v16si)_mm512_conflict_epi32(__A),
                                            (__v16si)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_conflict_epi32 (__mmask16 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectd_512((__mmask16)__U,
                                            (__v16si)_mm512_conflict_epi32(__A),
                                            (__v16si)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_lzcnt_epi32 (__m512i __A)
{
  return (__m512i) __builtin_ia32_vplzcntd_512 ((__v16si) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_lzcnt_epi32 (__m512i __W, __mmask16 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectd_512((__mmask16)__U,
                                             (__v16si)_mm512_lzcnt_epi32(__A),
                                             (__v16si)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_lzcnt_epi32 (__mmask16 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectd_512((__mmask16)__U,
                                             (__v16si)_mm512_lzcnt_epi32(__A),
                                             (__v16si)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_lzcnt_epi64 (__m512i __A)
{
  return (__m512i) __builtin_ia32_vplzcntq_512 ((__v8di) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_lzcnt_epi64 (__m512i __W, __mmask8 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectq_512((__mmask8)__U,
                                             (__v8di)_mm512_lzcnt_epi64(__A),
                                             (__v8di)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_lzcnt_epi64 (__mmask8 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectq_512((__mmask8)__U,
                                             (__v8di)_mm512_lzcnt_epi64(__A),
                                             (__v8di)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_broadcastmb_epi64 (__mmask8 __A)
{
  return (__m512i) _mm512_set1_epi64((long long) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_broadcastmw_epi32 (__mmask16 __A)
{
  return (__m512i) _mm512_set1_epi32((int) __A);

}

#undef __DEFAULT_FN_ATTRS

#endif
