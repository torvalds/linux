/*===---- smmintrin.h - Implementation of SSE4 intrinsics on PowerPC -------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

/* Implemented from the specification included in the Intel C++ Compiler
   User Guide and Reference, version 9.0.

   NOTE: This is NOT a complete implementation of the SSE4 intrinsics!  */

#ifndef NO_WARN_X86_INTRINSICS
/* This header is distributed to simplify porting x86_64 code that
   makes explicit use of Intel intrinsics to powerpc64/powerpc64le.

   It is the user's responsibility to determine if the results are
   acceptable and make additional changes as necessary.

   Note that much code that uses Intel intrinsics can be rewritten in
   standard C or GNU C extensions, which are more portable and better
   optimized across multiple targets.  */
#error                                                                         \
    "Please read comment above.  Use -DNO_WARN_X86_INTRINSICS to disable this error."
#endif

#ifndef SMMINTRIN_H_
#define SMMINTRIN_H_

#if defined(__powerpc64__) &&                                                  \
    (defined(__linux__) || defined(__FreeBSD__) || defined(_AIX))

#include <altivec.h>
#include <tmmintrin.h>

/* Rounding mode macros. */
#define _MM_FROUND_TO_NEAREST_INT 0x00
#define _MM_FROUND_TO_ZERO 0x01
#define _MM_FROUND_TO_POS_INF 0x02
#define _MM_FROUND_TO_NEG_INF 0x03
#define _MM_FROUND_CUR_DIRECTION 0x04

#define _MM_FROUND_NINT (_MM_FROUND_TO_NEAREST_INT | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_FLOOR (_MM_FROUND_TO_NEG_INF | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_CEIL (_MM_FROUND_TO_POS_INF | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_TRUNC (_MM_FROUND_TO_ZERO | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_RINT (_MM_FROUND_CUR_DIRECTION | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_NEARBYINT (_MM_FROUND_CUR_DIRECTION | _MM_FROUND_NO_EXC)

#define _MM_FROUND_RAISE_EXC 0x00
#define _MM_FROUND_NO_EXC 0x08

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_round_pd(__m128d __A, int __rounding) {
  __v2df __r;
  union {
    double __fr;
    long long __fpscr;
  } __enables_save, __fpscr_save;

  if (__rounding & _MM_FROUND_NO_EXC) {
    /* Save enabled exceptions, disable all exceptions,
       and preserve the rounding mode.  */
#ifdef _ARCH_PWR9
    __asm__("mffsce %0" : "=f"(__fpscr_save.__fr));
    __enables_save.__fpscr = __fpscr_save.__fpscr & 0xf8;
#else
    __fpscr_save.__fr = __builtin_ppc_mffs();
    __enables_save.__fpscr = __fpscr_save.__fpscr & 0xf8;
    __fpscr_save.__fpscr &= ~0xf8;
    __builtin_ppc_mtfsf(0b00000011, __fpscr_save.__fr);
#endif
    /* Insert an artificial "read/write" reference to the variable
       read below, to ensure the compiler does not schedule
       a read/use of the variable before the FPSCR is modified, above.
       This can be removed if and when GCC PR102783 is fixed.
     */
    __asm__("" : "+wa"(__A));
  }

  switch (__rounding) {
  case _MM_FROUND_TO_NEAREST_INT:
#ifdef _ARCH_PWR9
    __fpscr_save.__fr = __builtin_ppc_mffsl();
#else
    __fpscr_save.__fr = __builtin_ppc_mffs();
    __fpscr_save.__fpscr &= 0x70007f0ffL;
#endif
    __attribute__((fallthrough));
  case _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC:
    __builtin_ppc_set_fpscr_rn(0b00);
    /* Insert an artificial "read/write" reference to the variable
       read below, to ensure the compiler does not schedule
       a read/use of the variable before the FPSCR is modified, above.
       This can be removed if and when GCC PR102783 is fixed.
     */
    __asm__("" : "+wa"(__A));

    __r = vec_rint((__v2df)__A);

    /* Insert an artificial "read" reference to the variable written
       above, to ensure the compiler does not schedule the computation
       of the value after the manipulation of the FPSCR, below.
       This can be removed if and when GCC PR102783 is fixed.
     */
    __asm__("" : : "wa"(__r));
    __builtin_ppc_set_fpscr_rn(__fpscr_save.__fpscr);
    break;
  case _MM_FROUND_TO_NEG_INF:
  case _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC:
    __r = vec_floor((__v2df)__A);
    break;
  case _MM_FROUND_TO_POS_INF:
  case _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC:
    __r = vec_ceil((__v2df)__A);
    break;
  case _MM_FROUND_TO_ZERO:
  case _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC:
    __r = vec_trunc((__v2df)__A);
    break;
  case _MM_FROUND_CUR_DIRECTION:
    __r = vec_rint((__v2df)__A);
    break;
  }
  if (__rounding & _MM_FROUND_NO_EXC) {
    /* Insert an artificial "read" reference to the variable written
       above, to ensure the compiler does not schedule the computation
       of the value after the manipulation of the FPSCR, below.
       This can be removed if and when GCC PR102783 is fixed.
     */
    __asm__("" : : "wa"(__r));
    /* Restore enabled exceptions.  */
#ifdef _ARCH_PWR9
    __fpscr_save.__fr = __builtin_ppc_mffsl();
#else
    __fpscr_save.__fr = __builtin_ppc_mffs();
    __fpscr_save.__fpscr &= 0x70007f0ffL;
#endif
    __fpscr_save.__fpscr |= __enables_save.__fpscr;
    __builtin_ppc_mtfsf(0b00000011, __fpscr_save.__fr);
  }
  return (__m128d)__r;
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_round_sd(__m128d __A, __m128d __B, int __rounding) {
  __B = _mm_round_pd(__B, __rounding);
  __v2df __r = {((__v2df)__B)[0], ((__v2df)__A)[1]};
  return (__m128d)__r;
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_round_ps(__m128 __A, int __rounding) {
  __v4sf __r;
  union {
    double __fr;
    long long __fpscr;
  } __enables_save, __fpscr_save;

  if (__rounding & _MM_FROUND_NO_EXC) {
    /* Save enabled exceptions, disable all exceptions,
       and preserve the rounding mode.  */
#ifdef _ARCH_PWR9
    __asm__("mffsce %0" : "=f"(__fpscr_save.__fr));
    __enables_save.__fpscr = __fpscr_save.__fpscr & 0xf8;
#else
    __fpscr_save.__fr = __builtin_ppc_mffs();
    __enables_save.__fpscr = __fpscr_save.__fpscr & 0xf8;
    __fpscr_save.__fpscr &= ~0xf8;
    __builtin_ppc_mtfsf(0b00000011, __fpscr_save.__fr);
#endif
    /* Insert an artificial "read/write" reference to the variable
       read below, to ensure the compiler does not schedule
       a read/use of the variable before the FPSCR is modified, above.
       This can be removed if and when GCC PR102783 is fixed.
     */
    __asm__("" : "+wa"(__A));
  }

  switch (__rounding) {
  case _MM_FROUND_TO_NEAREST_INT:
#ifdef _ARCH_PWR9
    __fpscr_save.__fr = __builtin_ppc_mffsl();
#else
    __fpscr_save.__fr = __builtin_ppc_mffs();
    __fpscr_save.__fpscr &= 0x70007f0ffL;
#endif
    __attribute__((fallthrough));
  case _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC:
    __builtin_ppc_set_fpscr_rn(0b00);
    /* Insert an artificial "read/write" reference to the variable
       read below, to ensure the compiler does not schedule
       a read/use of the variable before the FPSCR is modified, above.
       This can be removed if and when GCC PR102783 is fixed.
     */
    __asm__("" : "+wa"(__A));

    __r = vec_rint((__v4sf)__A);

    /* Insert an artificial "read" reference to the variable written
       above, to ensure the compiler does not schedule the computation
       of the value after the manipulation of the FPSCR, below.
       This can be removed if and when GCC PR102783 is fixed.
     */
    __asm__("" : : "wa"(__r));
    __builtin_ppc_set_fpscr_rn(__fpscr_save.__fpscr);
    break;
  case _MM_FROUND_TO_NEG_INF:
  case _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC:
    __r = vec_floor((__v4sf)__A);
    break;
  case _MM_FROUND_TO_POS_INF:
  case _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC:
    __r = vec_ceil((__v4sf)__A);
    break;
  case _MM_FROUND_TO_ZERO:
  case _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC:
    __r = vec_trunc((__v4sf)__A);
    break;
  case _MM_FROUND_CUR_DIRECTION:
    __r = vec_rint((__v4sf)__A);
    break;
  }
  if (__rounding & _MM_FROUND_NO_EXC) {
    /* Insert an artificial "read" reference to the variable written
       above, to ensure the compiler does not schedule the computation
       of the value after the manipulation of the FPSCR, below.
       This can be removed if and when GCC PR102783 is fixed.
     */
    __asm__("" : : "wa"(__r));
    /* Restore enabled exceptions.  */
#ifdef _ARCH_PWR9
    __fpscr_save.__fr = __builtin_ppc_mffsl();
#else
    __fpscr_save.__fr = __builtin_ppc_mffs();
    __fpscr_save.__fpscr &= 0x70007f0ffL;
#endif
    __fpscr_save.__fpscr |= __enables_save.__fpscr;
    __builtin_ppc_mtfsf(0b00000011, __fpscr_save.__fr);
  }
  return (__m128)__r;
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_round_ss(__m128 __A, __m128 __B, int __rounding) {
  __B = _mm_round_ps(__B, __rounding);
  __v4sf __r = (__v4sf)__A;
  __r[0] = ((__v4sf)__B)[0];
  return (__m128)__r;
}

#define _mm_ceil_pd(V) _mm_round_pd((V), _MM_FROUND_CEIL)
#define _mm_ceil_sd(D, V) _mm_round_sd((D), (V), _MM_FROUND_CEIL)

#define _mm_floor_pd(V) _mm_round_pd((V), _MM_FROUND_FLOOR)
#define _mm_floor_sd(D, V) _mm_round_sd((D), (V), _MM_FROUND_FLOOR)

#define _mm_ceil_ps(V) _mm_round_ps((V), _MM_FROUND_CEIL)
#define _mm_ceil_ss(D, V) _mm_round_ss((D), (V), _MM_FROUND_CEIL)

#define _mm_floor_ps(V) _mm_round_ps((V), _MM_FROUND_FLOOR)
#define _mm_floor_ss(D, V) _mm_round_ss((D), (V), _MM_FROUND_FLOOR)

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_insert_epi8(__m128i const __A, int const __D, int const __N) {
  __v16qi __result = (__v16qi)__A;

  __result[__N & 0xf] = __D;

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_insert_epi32(__m128i const __A, int const __D, int const __N) {
  __v4si __result = (__v4si)__A;

  __result[__N & 3] = __D;

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_insert_epi64(__m128i const __A, long long const __D, int const __N) {
  __v2di __result = (__v2di)__A;

  __result[__N & 1] = __D;

  return (__m128i)__result;
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_extract_epi8(__m128i __X, const int __N) {
  return (unsigned char)((__v16qi)__X)[__N & 15];
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_extract_epi32(__m128i __X, const int __N) {
  return ((__v4si)__X)[__N & 3];
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_extract_epi64(__m128i __X, const int __N) {
  return ((__v2di)__X)[__N & 1];
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_extract_ps(__m128 __X, const int __N) {
  return ((__v4si)__X)[__N & 3];
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_blend_epi16(__m128i __A, __m128i __B, const int __imm8) {
  __v16qu __charmask = vec_splats((unsigned char)__imm8);
  __charmask = vec_gb(__charmask);
  __v8hu __shortmask = (__v8hu)vec_unpackh((__v16qi)__charmask);
#ifdef __BIG_ENDIAN__
  __shortmask = vec_reve(__shortmask);
#endif
  return (__m128i)vec_sel((__v8hu)__A, (__v8hu)__B, __shortmask);
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_blendv_epi8(__m128i __A, __m128i __B, __m128i __mask) {
#ifdef _ARCH_PWR10
  return (__m128i)vec_blendv((__v16qi)__A, (__v16qi)__B, (__v16qu)__mask);
#else
  const __v16qu __seven = vec_splats((unsigned char)0x07);
  __v16qu __lmask = vec_sra((__v16qu)__mask, __seven);
  return (__m128i)vec_sel((__v16qi)__A, (__v16qi)__B, __lmask);
#endif
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_blend_ps(__m128 __A, __m128 __B, const int __imm8) {
  __v16qu __pcv[] = {
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
      {16, 17, 18, 19, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
      {0, 1, 2, 3, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15},
      {16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15},
      {0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27, 12, 13, 14, 15},
      {16, 17, 18, 19, 4, 5, 6, 7, 24, 25, 26, 27, 12, 13, 14, 15},
      {0, 1, 2, 3, 20, 21, 22, 23, 24, 25, 26, 27, 12, 13, 14, 15},
      {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 12, 13, 14, 15},
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 28, 29, 30, 31},
      {16, 17, 18, 19, 4, 5, 6, 7, 8, 9, 10, 11, 28, 29, 30, 31},
      {0, 1, 2, 3, 20, 21, 22, 23, 8, 9, 10, 11, 28, 29, 30, 31},
      {16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 28, 29, 30, 31},
      {0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31},
      {16, 17, 18, 19, 4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31},
      {0, 1, 2, 3, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31},
      {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31},
  };
  __v16qu __r = vec_perm((__v16qu)__A, (__v16qu)__B, __pcv[__imm8]);
  return (__m128)__r;
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_blendv_ps(__m128 __A, __m128 __B, __m128 __mask) {
#ifdef _ARCH_PWR10
  return (__m128)vec_blendv((__v4sf)__A, (__v4sf)__B, (__v4su)__mask);
#else
  const __v4si __zero = {0};
  const __vector __bool int __boolmask = vec_cmplt((__v4si)__mask, __zero);
  return (__m128)vec_sel((__v4su)__A, (__v4su)__B, (__v4su)__boolmask);
#endif
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_blend_pd(__m128d __A, __m128d __B, const int __imm8) {
  __v16qu __pcv[] = {
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
      {16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15},
      {0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31},
      {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31}};
  __v16qu __r = vec_perm((__v16qu)__A, (__v16qu)__B, __pcv[__imm8]);
  return (__m128d)__r;
}

#ifdef _ARCH_PWR8
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_blendv_pd(__m128d __A, __m128d __B, __m128d __mask) {
#ifdef _ARCH_PWR10
  return (__m128d)vec_blendv((__v2df)__A, (__v2df)__B, (__v2du)__mask);
#else
  const __v2di __zero = {0};
  const __vector __bool long long __boolmask =
      vec_cmplt((__v2di)__mask, __zero);
  return (__m128d)vec_sel((__v2du)__A, (__v2du)__B, (__v2du)__boolmask);
#endif
}
#endif

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_testz_si128(__m128i __A, __m128i __B) {
  /* Note: This implementation does NOT set "zero" or "carry" flags.  */
  const __v16qu __zero = {0};
  return vec_all_eq(vec_and((__v16qu)__A, (__v16qu)__B), __zero);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_testc_si128(__m128i __A, __m128i __B) {
  /* Note: This implementation does NOT set "zero" or "carry" flags.  */
  const __v16qu __zero = {0};
  const __v16qu __notA = vec_nor((__v16qu)__A, (__v16qu)__A);
  return vec_all_eq(vec_and((__v16qu)__notA, (__v16qu)__B), __zero);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_testnzc_si128(__m128i __A, __m128i __B) {
  /* Note: This implementation does NOT set "zero" or "carry" flags.  */
  return _mm_testz_si128(__A, __B) == 0 && _mm_testc_si128(__A, __B) == 0;
}

#define _mm_test_all_zeros(M, V) _mm_testz_si128((M), (V))

#define _mm_test_all_ones(V) _mm_testc_si128((V), _mm_cmpeq_epi32((V), (V)))

#define _mm_test_mix_ones_zeros(M, V) _mm_testnzc_si128((M), (V))

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpeq_epi64(__m128i __X, __m128i __Y) {
  return (__m128i)vec_cmpeq((__v2di)__X, (__v2di)__Y);
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_epi8(__m128i __X, __m128i __Y) {
  return (__m128i)vec_min((__v16qi)__X, (__v16qi)__Y);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_epu16(__m128i __X, __m128i __Y) {
  return (__m128i)vec_min((__v8hu)__X, (__v8hu)__Y);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_epi32(__m128i __X, __m128i __Y) {
  return (__m128i)vec_min((__v4si)__X, (__v4si)__Y);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_epu32(__m128i __X, __m128i __Y) {
  return (__m128i)vec_min((__v4su)__X, (__v4su)__Y);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_epi8(__m128i __X, __m128i __Y) {
  return (__m128i)vec_max((__v16qi)__X, (__v16qi)__Y);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_epu16(__m128i __X, __m128i __Y) {
  return (__m128i)vec_max((__v8hu)__X, (__v8hu)__Y);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_epi32(__m128i __X, __m128i __Y) {
  return (__m128i)vec_max((__v4si)__X, (__v4si)__Y);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_epu32(__m128i __X, __m128i __Y) {
  return (__m128i)vec_max((__v4su)__X, (__v4su)__Y);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mullo_epi32(__m128i __X, __m128i __Y) {
  return (__m128i)vec_mul((__v4su)__X, (__v4su)__Y);
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mul_epi32(__m128i __X, __m128i __Y) {
  return (__m128i)vec_mule((__v4si)__X, (__v4si)__Y);
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepi8_epi16(__m128i __A) {
  return (__m128i)vec_unpackh((__v16qi)__A);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepi8_epi32(__m128i __A) {
  __A = (__m128i)vec_unpackh((__v16qi)__A);
  return (__m128i)vec_unpackh((__v8hi)__A);
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepi8_epi64(__m128i __A) {
  __A = (__m128i)vec_unpackh((__v16qi)__A);
  __A = (__m128i)vec_unpackh((__v8hi)__A);
  return (__m128i)vec_unpackh((__v4si)__A);
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepi16_epi32(__m128i __A) {
  return (__m128i)vec_unpackh((__v8hi)__A);
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepi16_epi64(__m128i __A) {
  __A = (__m128i)vec_unpackh((__v8hi)__A);
  return (__m128i)vec_unpackh((__v4si)__A);
}
#endif

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepi32_epi64(__m128i __A) {
  return (__m128i)vec_unpackh((__v4si)__A);
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepu8_epi16(__m128i __A) {
  const __v16qu __zero = {0};
#ifdef __LITTLE_ENDIAN__
  __A = (__m128i)vec_mergeh((__v16qu)__A, __zero);
#else  /* __BIG_ENDIAN__.  */
  __A = (__m128i)vec_mergeh(__zero, (__v16qu)__A);
#endif /* __BIG_ENDIAN__.  */
  return __A;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepu8_epi32(__m128i __A) {
  const __v16qu __zero = {0};
#ifdef __LITTLE_ENDIAN__
  __A = (__m128i)vec_mergeh((__v16qu)__A, __zero);
  __A = (__m128i)vec_mergeh((__v8hu)__A, (__v8hu)__zero);
#else  /* __BIG_ENDIAN__.  */
  __A = (__m128i)vec_mergeh(__zero, (__v16qu)__A);
  __A = (__m128i)vec_mergeh((__v8hu)__zero, (__v8hu)__A);
#endif /* __BIG_ENDIAN__.  */
  return __A;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepu8_epi64(__m128i __A) {
  const __v16qu __zero = {0};
#ifdef __LITTLE_ENDIAN__
  __A = (__m128i)vec_mergeh((__v16qu)__A, __zero);
  __A = (__m128i)vec_mergeh((__v8hu)__A, (__v8hu)__zero);
  __A = (__m128i)vec_mergeh((__v4su)__A, (__v4su)__zero);
#else  /* __BIG_ENDIAN__.  */
  __A = (__m128i)vec_mergeh(__zero, (__v16qu)__A);
  __A = (__m128i)vec_mergeh((__v8hu)__zero, (__v8hu)__A);
  __A = (__m128i)vec_mergeh((__v4su)__zero, (__v4su)__A);
#endif /* __BIG_ENDIAN__.  */
  return __A;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepu16_epi32(__m128i __A) {
  const __v8hu __zero = {0};
#ifdef __LITTLE_ENDIAN__
  __A = (__m128i)vec_mergeh((__v8hu)__A, __zero);
#else  /* __BIG_ENDIAN__.  */
  __A = (__m128i)vec_mergeh(__zero, (__v8hu)__A);
#endif /* __BIG_ENDIAN__.  */
  return __A;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepu16_epi64(__m128i __A) {
  const __v8hu __zero = {0};
#ifdef __LITTLE_ENDIAN__
  __A = (__m128i)vec_mergeh((__v8hu)__A, __zero);
  __A = (__m128i)vec_mergeh((__v4su)__A, (__v4su)__zero);
#else  /* __BIG_ENDIAN__.  */
  __A = (__m128i)vec_mergeh(__zero, (__v8hu)__A);
  __A = (__m128i)vec_mergeh((__v4su)__zero, (__v4su)__A);
#endif /* __BIG_ENDIAN__.  */
  return __A;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepu32_epi64(__m128i __A) {
  const __v4su __zero = {0};
#ifdef __LITTLE_ENDIAN__
  __A = (__m128i)vec_mergeh((__v4su)__A, __zero);
#else  /* __BIG_ENDIAN__.  */
  __A = (__m128i)vec_mergeh(__zero, (__v4su)__A);
#endif /* __BIG_ENDIAN__.  */
  return __A;
}

/* Return horizontal packed word minimum and its index in bits [15:0]
   and bits [18:16] respectively.  */
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_minpos_epu16(__m128i __A) {
  union __u {
    __m128i __m;
    __v8hu __uh;
  };
  union __u __u = {.__m = __A}, __r = {.__m = {0}};
  unsigned short __ridx = 0;
  unsigned short __rmin = __u.__uh[__ridx];
  unsigned long __i;
  for (__i = 1; __i < 8; __i++) {
    if (__u.__uh[__i] < __rmin) {
      __rmin = __u.__uh[__i];
      __ridx = __i;
    }
  }
  __r.__uh[0] = __rmin;
  __r.__uh[1] = __ridx;
  return __r.__m;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_packus_epi32(__m128i __X, __m128i __Y) {
  return (__m128i)vec_packsu((__v4si)__X, (__v4si)__Y);
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpgt_epi64(__m128i __X, __m128i __Y) {
  return (__m128i)vec_cmpgt((__v2di)__X, (__v2di)__Y);
}
#endif

#else
#include_next <smmintrin.h>
#endif /* defined(__powerpc64__) &&                                            \
        *   (defined(__linux__) || defined(__FreeBSD__) || defined(_AIX)) */

#endif /* SMMINTRIN_H_ */
