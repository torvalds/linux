/*===----------- avx512fp16intrin.h - AVX512-FP16 intrinsics ---------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <avx512fp16intrin.h> directly; include <immintrin.h> instead."
#endif

#ifdef __SSE2__

#ifndef __AVX512FP16INTRIN_H
#define __AVX512FP16INTRIN_H

/* Define the default attributes for the functions in this file. */
typedef _Float16 __v32hf __attribute__((__vector_size__(64), __aligned__(64)));
typedef _Float16 __m512h __attribute__((__vector_size__(64), __aligned__(64)));
typedef _Float16 __m512h_u __attribute__((__vector_size__(64), __aligned__(1)));

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS512                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512fp16,evex512"), __min_vector_width__(512)))
#define __DEFAULT_FN_ATTRS256                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512fp16,no-evex512"),                          \
                 __min_vector_width__(256)))
#define __DEFAULT_FN_ATTRS128                                                  \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512fp16,no-evex512"),                          \
                 __min_vector_width__(128)))

static __inline__ _Float16 __DEFAULT_FN_ATTRS512 _mm512_cvtsh_h(__m512h __a) {
  return __a[0];
}

static __inline __m128h __DEFAULT_FN_ATTRS128 _mm_setzero_ph(void) {
  return (__m128h){0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
}

static __inline __m256h __DEFAULT_FN_ATTRS256 _mm256_setzero_ph(void) {
  return (__m256h){0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                   0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_undefined_ph(void) {
  return (__m256h)__builtin_ia32_undef256();
}

static __inline __m512h __DEFAULT_FN_ATTRS512 _mm512_setzero_ph(void) {
  return (__m512h){0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                   0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                   0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_undefined_ph(void) {
  return (__m128h)__builtin_ia32_undef128();
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_undefined_ph(void) {
  return (__m512h)__builtin_ia32_undef512();
}

static __inline __m512h __DEFAULT_FN_ATTRS512 _mm512_set1_ph(_Float16 __h) {
  return (__m512h)(__v32hf){__h, __h, __h, __h, __h, __h, __h, __h,
                            __h, __h, __h, __h, __h, __h, __h, __h,
                            __h, __h, __h, __h, __h, __h, __h, __h,
                            __h, __h, __h, __h, __h, __h, __h, __h};
}

static __inline __m512h __DEFAULT_FN_ATTRS512
_mm512_set_ph(_Float16 __h1, _Float16 __h2, _Float16 __h3, _Float16 __h4,
              _Float16 __h5, _Float16 __h6, _Float16 __h7, _Float16 __h8,
              _Float16 __h9, _Float16 __h10, _Float16 __h11, _Float16 __h12,
              _Float16 __h13, _Float16 __h14, _Float16 __h15, _Float16 __h16,
              _Float16 __h17, _Float16 __h18, _Float16 __h19, _Float16 __h20,
              _Float16 __h21, _Float16 __h22, _Float16 __h23, _Float16 __h24,
              _Float16 __h25, _Float16 __h26, _Float16 __h27, _Float16 __h28,
              _Float16 __h29, _Float16 __h30, _Float16 __h31, _Float16 __h32) {
  return (__m512h)(__v32hf){__h32, __h31, __h30, __h29, __h28, __h27, __h26,
                            __h25, __h24, __h23, __h22, __h21, __h20, __h19,
                            __h18, __h17, __h16, __h15, __h14, __h13, __h12,
                            __h11, __h10, __h9,  __h8,  __h7,  __h6,  __h5,
                            __h4,  __h3,  __h2,  __h1};
}

#define _mm512_setr_ph(h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11, h12, h13, \
                       h14, h15, h16, h17, h18, h19, h20, h21, h22, h23, h24,  \
                       h25, h26, h27, h28, h29, h30, h31, h32)                 \
  _mm512_set_ph((h32), (h31), (h30), (h29), (h28), (h27), (h26), (h25), (h24), \
                (h23), (h22), (h21), (h20), (h19), (h18), (h17), (h16), (h15), \
                (h14), (h13), (h12), (h11), (h10), (h9), (h8), (h7), (h6),     \
                (h5), (h4), (h3), (h2), (h1))

static __inline __m512h __DEFAULT_FN_ATTRS512
_mm512_set1_pch(_Float16 _Complex __h) {
  return (__m512h)_mm512_set1_ps(__builtin_bit_cast(float, __h));
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128 _mm_castph_ps(__m128h __a) {
  return (__m128)__a;
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256 _mm256_castph_ps(__m256h __a) {
  return (__m256)__a;
}

static __inline__ __m512 __DEFAULT_FN_ATTRS512 _mm512_castph_ps(__m512h __a) {
  return (__m512)__a;
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128 _mm_castph_pd(__m128h __a) {
  return (__m128d)__a;
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256 _mm256_castph_pd(__m256h __a) {
  return (__m256d)__a;
}

static __inline__ __m512d __DEFAULT_FN_ATTRS512 _mm512_castph_pd(__m512h __a) {
  return (__m512d)__a;
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_castph_si128(__m128h __a) {
  return (__m128i)__a;
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_castph_si256(__m256h __a) {
  return (__m256i)__a;
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_castph_si512(__m512h __a) {
  return (__m512i)__a;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_castps_ph(__m128 __a) {
  return (__m128h)__a;
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_castps_ph(__m256 __a) {
  return (__m256h)__a;
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_castps_ph(__m512 __a) {
  return (__m512h)__a;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_castpd_ph(__m128d __a) {
  return (__m128h)__a;
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256 _mm256_castpd_ph(__m256d __a) {
  return (__m256h)__a;
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_castpd_ph(__m512d __a) {
  return (__m512h)__a;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_castsi128_ph(__m128i __a) {
  return (__m128h)__a;
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_castsi256_ph(__m256i __a) {
  return (__m256h)__a;
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_castsi512_ph(__m512i __a) {
  return (__m512h)__a;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS256
_mm256_castph256_ph128(__m256h __a) {
  return __builtin_shufflevector(__a, __a, 0, 1, 2, 3, 4, 5, 6, 7);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS512
_mm512_castph512_ph128(__m512h __a) {
  return __builtin_shufflevector(__a, __a, 0, 1, 2, 3, 4, 5, 6, 7);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS512
_mm512_castph512_ph256(__m512h __a) {
  return __builtin_shufflevector(__a, __a, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                 12, 13, 14, 15);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_castph128_ph256(__m128h __a) {
  return __builtin_shufflevector(__a, __builtin_nondeterministic_value(__a),
                                  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_castph128_ph512(__m128h __a) {
  __m256h __b = __builtin_nondeterministic_value(__b);
  return __builtin_shufflevector(
      __builtin_shufflevector(__a, __builtin_nondeterministic_value(__a),
                              0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
      __b, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
      20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_castph256_ph512(__m256h __a) {
  return __builtin_shufflevector(__a, __builtin_nondeterministic_value(__a), 0,
                                 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
                                 27, 28, 29, 30, 31);
}

/// Constructs a 256-bit floating-point vector of [16 x half] from a
///    128-bit floating-point vector of [8 x half]. The lower 128 bits
///    contain the value of the source vector. The upper 384 bits are set
///    to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit vector of [8 x half].
/// \returns A 512-bit floating-point vector of [16 x half]. The lower 128 bits
///    contain the value of the parameter. The upper 384 bits are set to zero.
static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_zextph128_ph256(__m128h __a) {
  return __builtin_shufflevector(__a, (__v8hf)_mm_setzero_ph(), 0, 1, 2, 3, 4,
                                 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
}

/// Constructs a 512-bit floating-point vector of [32 x half] from a
///    128-bit floating-point vector of [8 x half]. The lower 128 bits
///    contain the value of the source vector. The upper 384 bits are set
///    to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit vector of [8 x half].
/// \returns A 512-bit floating-point vector of [32 x half]. The lower 128 bits
///    contain the value of the parameter. The upper 384 bits are set to zero.
static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_zextph128_ph512(__m128h __a) {
  return __builtin_shufflevector(
      __a, (__v8hf)_mm_setzero_ph(), 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
      13, 14, 15, 8, 9, 10, 11, 12, 13, 14, 15, 8, 9, 10, 11, 12, 13, 14, 15);
}

/// Constructs a 512-bit floating-point vector of [32 x half] from a
///    256-bit floating-point vector of [16 x half]. The lower 256 bits
///    contain the value of the source vector. The upper 256 bits are set
///    to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit vector of [16 x half].
/// \returns A 512-bit floating-point vector of [32 x half]. The lower 256 bits
///    contain the value of the parameter. The upper 256 bits are set to zero.
static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_zextph256_ph512(__m256h __a) {
  return __builtin_shufflevector(__a, (__v16hf)_mm256_setzero_ph(), 0, 1, 2, 3,
                                 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                                 29, 30, 31);
}

#define _mm_comi_round_sh(A, B, P, R)                                          \
  __builtin_ia32_vcomish((__v8hf)A, (__v8hf)B, (int)(P), (int)(R))

#define _mm_comi_sh(A, B, pred)                                                \
  _mm_comi_round_sh((A), (B), (pred), _MM_FROUND_CUR_DIRECTION)

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_comieq_sh(__m128h __A,
                                                          __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_EQ_OS,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_comilt_sh(__m128h __A,
                                                          __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_LT_OS,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_comile_sh(__m128h __A,
                                                          __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_LE_OS,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_comigt_sh(__m128h __A,
                                                          __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_GT_OS,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_comige_sh(__m128h __A,
                                                          __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_GE_OS,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_comineq_sh(__m128h __A,
                                                           __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_NEQ_US,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_ucomieq_sh(__m128h __A,
                                                           __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_EQ_OQ,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_ucomilt_sh(__m128h __A,
                                                           __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_LT_OQ,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_ucomile_sh(__m128h __A,
                                                           __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_LE_OQ,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_ucomigt_sh(__m128h __A,
                                                           __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_GT_OQ,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_ucomige_sh(__m128h __A,
                                                           __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_GE_OQ,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_ucomineq_sh(__m128h __A,
                                                            __m128h __B) {
  return __builtin_ia32_vcomish((__v8hf)__A, (__v8hf)__B, _CMP_NEQ_UQ,
                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_add_ph(__m512h __A,
                                                              __m512h __B) {
  return (__m512h)((__v32hf)__A + (__v32hf)__B);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_add_ph(__m512h __W, __mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512(
      (__mmask32)__U, (__v32hf)_mm512_add_ph(__A, __B), (__v32hf)__W);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_add_ph(__mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512((__mmask32)__U,
                                              (__v32hf)_mm512_add_ph(__A, __B),
                                              (__v32hf)_mm512_setzero_ph());
}

#define _mm512_add_round_ph(A, B, R)                                           \
  ((__m512h)__builtin_ia32_addph512((__v32hf)(__m512h)(A),                     \
                                    (__v32hf)(__m512h)(B), (int)(R)))

#define _mm512_mask_add_round_ph(W, U, A, B, R)                                \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_add_round_ph((A), (B), (R)),             \
      (__v32hf)(__m512h)(W)))

#define _mm512_maskz_add_round_ph(U, A, B, R)                                  \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_add_round_ph((A), (B), (R)),             \
      (__v32hf)_mm512_setzero_ph()))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_sub_ph(__m512h __A,
                                                              __m512h __B) {
  return (__m512h)((__v32hf)__A - (__v32hf)__B);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_sub_ph(__m512h __W, __mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512(
      (__mmask32)__U, (__v32hf)_mm512_sub_ph(__A, __B), (__v32hf)__W);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_sub_ph(__mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512((__mmask32)__U,
                                              (__v32hf)_mm512_sub_ph(__A, __B),
                                              (__v32hf)_mm512_setzero_ph());
}

#define _mm512_sub_round_ph(A, B, R)                                           \
  ((__m512h)__builtin_ia32_subph512((__v32hf)(__m512h)(A),                     \
                                    (__v32hf)(__m512h)(B), (int)(R)))

#define _mm512_mask_sub_round_ph(W, U, A, B, R)                                \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_sub_round_ph((A), (B), (R)),             \
      (__v32hf)(__m512h)(W)))

#define _mm512_maskz_sub_round_ph(U, A, B, R)                                  \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_sub_round_ph((A), (B), (R)),             \
      (__v32hf)_mm512_setzero_ph()))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_mul_ph(__m512h __A,
                                                              __m512h __B) {
  return (__m512h)((__v32hf)__A * (__v32hf)__B);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_mul_ph(__m512h __W, __mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512(
      (__mmask32)__U, (__v32hf)_mm512_mul_ph(__A, __B), (__v32hf)__W);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_mul_ph(__mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512((__mmask32)__U,
                                              (__v32hf)_mm512_mul_ph(__A, __B),
                                              (__v32hf)_mm512_setzero_ph());
}

#define _mm512_mul_round_ph(A, B, R)                                           \
  ((__m512h)__builtin_ia32_mulph512((__v32hf)(__m512h)(A),                     \
                                    (__v32hf)(__m512h)(B), (int)(R)))

#define _mm512_mask_mul_round_ph(W, U, A, B, R)                                \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_mul_round_ph((A), (B), (R)),             \
      (__v32hf)(__m512h)(W)))

#define _mm512_maskz_mul_round_ph(U, A, B, R)                                  \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_mul_round_ph((A), (B), (R)),             \
      (__v32hf)_mm512_setzero_ph()))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_div_ph(__m512h __A,
                                                              __m512h __B) {
  return (__m512h)((__v32hf)__A / (__v32hf)__B);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_div_ph(__m512h __W, __mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512(
      (__mmask32)__U, (__v32hf)_mm512_div_ph(__A, __B), (__v32hf)__W);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_div_ph(__mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512((__mmask32)__U,
                                              (__v32hf)_mm512_div_ph(__A, __B),
                                              (__v32hf)_mm512_setzero_ph());
}

#define _mm512_div_round_ph(A, B, R)                                           \
  ((__m512h)__builtin_ia32_divph512((__v32hf)(__m512h)(A),                     \
                                    (__v32hf)(__m512h)(B), (int)(R)))

#define _mm512_mask_div_round_ph(W, U, A, B, R)                                \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_div_round_ph((A), (B), (R)),             \
      (__v32hf)(__m512h)(W)))

#define _mm512_maskz_div_round_ph(U, A, B, R)                                  \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_div_round_ph((A), (B), (R)),             \
      (__v32hf)_mm512_setzero_ph()))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_min_ph(__m512h __A,
                                                              __m512h __B) {
  return (__m512h)__builtin_ia32_minph512((__v32hf)__A, (__v32hf)__B,
                                          _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_min_ph(__m512h __W, __mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512(
      (__mmask32)__U, (__v32hf)_mm512_min_ph(__A, __B), (__v32hf)__W);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_min_ph(__mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512((__mmask32)__U,
                                              (__v32hf)_mm512_min_ph(__A, __B),
                                              (__v32hf)_mm512_setzero_ph());
}

#define _mm512_min_round_ph(A, B, R)                                           \
  ((__m512h)__builtin_ia32_minph512((__v32hf)(__m512h)(A),                     \
                                    (__v32hf)(__m512h)(B), (int)(R)))

#define _mm512_mask_min_round_ph(W, U, A, B, R)                                \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_min_round_ph((A), (B), (R)),             \
      (__v32hf)(__m512h)(W)))

#define _mm512_maskz_min_round_ph(U, A, B, R)                                  \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_min_round_ph((A), (B), (R)),             \
      (__v32hf)_mm512_setzero_ph()))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_max_ph(__m512h __A,
                                                              __m512h __B) {
  return (__m512h)__builtin_ia32_maxph512((__v32hf)__A, (__v32hf)__B,
                                          _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_max_ph(__m512h __W, __mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512(
      (__mmask32)__U, (__v32hf)_mm512_max_ph(__A, __B), (__v32hf)__W);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_max_ph(__mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_selectph_512((__mmask32)__U,
                                              (__v32hf)_mm512_max_ph(__A, __B),
                                              (__v32hf)_mm512_setzero_ph());
}

#define _mm512_max_round_ph(A, B, R)                                           \
  ((__m512h)__builtin_ia32_maxph512((__v32hf)(__m512h)(A),                     \
                                    (__v32hf)(__m512h)(B), (int)(R)))

#define _mm512_mask_max_round_ph(W, U, A, B, R)                                \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_max_round_ph((A), (B), (R)),             \
      (__v32hf)(__m512h)(W)))

#define _mm512_maskz_max_round_ph(U, A, B, R)                                  \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_max_round_ph((A), (B), (R)),             \
      (__v32hf)_mm512_setzero_ph()))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_abs_ph(__m512h __A) {
  return (__m512h)_mm512_and_epi32(_mm512_set1_epi32(0x7FFF7FFF), (__m512i)__A);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_conj_pch(__m512h __A) {
  return (__m512h)_mm512_xor_ps((__m512)__A, _mm512_set1_ps(-0.0f));
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_conj_pch(__m512h __W, __mmask16 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_selectps_512(
      (__mmask16)__U, (__v16sf)_mm512_conj_pch(__A), (__v16sf)__W);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_conj_pch(__mmask16 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_selectps_512((__mmask16)__U,
                                              (__v16sf)_mm512_conj_pch(__A),
                                              (__v16sf)_mm512_setzero_ps());
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_add_sh(__m128h __A,
                                                           __m128h __B) {
  __A[0] += __B[0];
  return __A;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_add_sh(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  __A = _mm_add_sh(__A, __B);
  return __builtin_ia32_selectsh_128(__U, __A, __W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_add_sh(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  __A = _mm_add_sh(__A, __B);
  return __builtin_ia32_selectsh_128(__U, __A, _mm_setzero_ph());
}

#define _mm_add_round_sh(A, B, R)                                              \
  ((__m128h)__builtin_ia32_addsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_add_round_sh(W, U, A, B, R)                                   \
  ((__m128h)__builtin_ia32_addsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_add_round_sh(U, A, B, R)                                     \
  ((__m128h)__builtin_ia32_addsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_sub_sh(__m128h __A,
                                                           __m128h __B) {
  __A[0] -= __B[0];
  return __A;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_sub_sh(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  __A = _mm_sub_sh(__A, __B);
  return __builtin_ia32_selectsh_128(__U, __A, __W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_sub_sh(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  __A = _mm_sub_sh(__A, __B);
  return __builtin_ia32_selectsh_128(__U, __A, _mm_setzero_ph());
}

#define _mm_sub_round_sh(A, B, R)                                              \
  ((__m128h)__builtin_ia32_subsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_sub_round_sh(W, U, A, B, R)                                   \
  ((__m128h)__builtin_ia32_subsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_sub_round_sh(U, A, B, R)                                     \
  ((__m128h)__builtin_ia32_subsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mul_sh(__m128h __A,
                                                           __m128h __B) {
  __A[0] *= __B[0];
  return __A;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_mul_sh(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  __A = _mm_mul_sh(__A, __B);
  return __builtin_ia32_selectsh_128(__U, __A, __W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_mul_sh(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  __A = _mm_mul_sh(__A, __B);
  return __builtin_ia32_selectsh_128(__U, __A, _mm_setzero_ph());
}

#define _mm_mul_round_sh(A, B, R)                                              \
  ((__m128h)__builtin_ia32_mulsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_mul_round_sh(W, U, A, B, R)                                   \
  ((__m128h)__builtin_ia32_mulsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_mul_round_sh(U, A, B, R)                                     \
  ((__m128h)__builtin_ia32_mulsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_div_sh(__m128h __A,
                                                           __m128h __B) {
  __A[0] /= __B[0];
  return __A;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_div_sh(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  __A = _mm_div_sh(__A, __B);
  return __builtin_ia32_selectsh_128(__U, __A, __W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_div_sh(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  __A = _mm_div_sh(__A, __B);
  return __builtin_ia32_selectsh_128(__U, __A, _mm_setzero_ph());
}

#define _mm_div_round_sh(A, B, R)                                              \
  ((__m128h)__builtin_ia32_divsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_div_round_sh(W, U, A, B, R)                                   \
  ((__m128h)__builtin_ia32_divsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_div_round_sh(U, A, B, R)                                     \
  ((__m128h)__builtin_ia32_divsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_min_sh(__m128h __A,
                                                           __m128h __B) {
  return (__m128h)__builtin_ia32_minsh_round_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_min_sh(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  return (__m128h)__builtin_ia32_minsh_round_mask((__v8hf)__A, (__v8hf)__B,
                                                  (__v8hf)__W, (__mmask8)__U,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_min_sh(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_minsh_round_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_min_round_sh(A, B, R)                                              \
  ((__m128h)__builtin_ia32_minsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_min_round_sh(W, U, A, B, R)                                   \
  ((__m128h)__builtin_ia32_minsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_min_round_sh(U, A, B, R)                                     \
  ((__m128h)__builtin_ia32_minsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_max_sh(__m128h __A,
                                                           __m128h __B) {
  return (__m128h)__builtin_ia32_maxsh_round_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_max_sh(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  return (__m128h)__builtin_ia32_maxsh_round_mask((__v8hf)__A, (__v8hf)__B,
                                                  (__v8hf)__W, (__mmask8)__U,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_max_sh(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_maxsh_round_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_max_round_sh(A, B, R)                                              \
  ((__m128h)__builtin_ia32_maxsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_max_round_sh(W, U, A, B, R)                                   \
  ((__m128h)__builtin_ia32_maxsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_max_round_sh(U, A, B, R)                                     \
  ((__m128h)__builtin_ia32_maxsh_round_mask(                                   \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(R)))

#define _mm512_cmp_round_ph_mask(A, B, P, R)                                   \
  ((__mmask32)__builtin_ia32_cmpph512_mask((__v32hf)(__m512h)(A),              \
                                           (__v32hf)(__m512h)(B), (int)(P),    \
                                           (__mmask32)-1, (int)(R)))

#define _mm512_mask_cmp_round_ph_mask(U, A, B, P, R)                           \
  ((__mmask32)__builtin_ia32_cmpph512_mask((__v32hf)(__m512h)(A),              \
                                           (__v32hf)(__m512h)(B), (int)(P),    \
                                           (__mmask32)(U), (int)(R)))

#define _mm512_cmp_ph_mask(A, B, P)                                            \
  _mm512_cmp_round_ph_mask((A), (B), (P), _MM_FROUND_CUR_DIRECTION)

#define _mm512_mask_cmp_ph_mask(U, A, B, P)                                    \
  _mm512_mask_cmp_round_ph_mask((U), (A), (B), (P), _MM_FROUND_CUR_DIRECTION)

#define _mm_cmp_round_sh_mask(X, Y, P, R)                                      \
  ((__mmask8)__builtin_ia32_cmpsh_mask((__v8hf)(__m128h)(X),                   \
                                       (__v8hf)(__m128h)(Y), (int)(P),         \
                                       (__mmask8)-1, (int)(R)))

#define _mm_mask_cmp_round_sh_mask(M, X, Y, P, R)                              \
  ((__mmask8)__builtin_ia32_cmpsh_mask((__v8hf)(__m128h)(X),                   \
                                       (__v8hf)(__m128h)(Y), (int)(P),         \
                                       (__mmask8)(M), (int)(R)))

#define _mm_cmp_sh_mask(X, Y, P)                                               \
  ((__mmask8)__builtin_ia32_cmpsh_mask(                                        \
      (__v8hf)(__m128h)(X), (__v8hf)(__m128h)(Y), (int)(P), (__mmask8)-1,      \
      _MM_FROUND_CUR_DIRECTION))

#define _mm_mask_cmp_sh_mask(M, X, Y, P)                                       \
  ((__mmask8)__builtin_ia32_cmpsh_mask(                                        \
      (__v8hf)(__m128h)(X), (__v8hf)(__m128h)(Y), (int)(P), (__mmask8)(M),     \
      _MM_FROUND_CUR_DIRECTION))
// loads with vmovsh:
static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_load_sh(void const *__dp) {
  struct __mm_load_sh_struct {
    _Float16 __u;
  } __attribute__((__packed__, __may_alias__));
  _Float16 __u = ((const struct __mm_load_sh_struct *)__dp)->__u;
  return (__m128h){__u, 0, 0, 0, 0, 0, 0, 0};
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_load_sh(__m128h __W, __mmask8 __U, const void *__A) {
  __m128h src = (__v8hf)__builtin_shufflevector(
      (__v8hf)__W, (__v8hf)_mm_setzero_ph(), 0, 8, 8, 8, 8, 8, 8, 8);

  return (__m128h)__builtin_ia32_loadsh128_mask((const __v8hf *)__A, src, __U & 1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_load_sh(__mmask8 __U, const void *__A) {
  return (__m128h)__builtin_ia32_loadsh128_mask(
      (const __v8hf *)__A, (__v8hf)_mm_setzero_ph(), __U & 1);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_load_ph(void const *__p) {
  return *(const __m512h *)__p;
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_load_ph(void const *__p) {
  return *(const __m256h *)__p;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_load_ph(void const *__p) {
  return *(const __m128h *)__p;
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_loadu_ph(void const *__p) {
  struct __loadu_ph {
    __m512h_u __v;
  } __attribute__((__packed__, __may_alias__));
  return ((const struct __loadu_ph *)__p)->__v;
}

static __inline__ __m256h __DEFAULT_FN_ATTRS256
_mm256_loadu_ph(void const *__p) {
  struct __loadu_ph {
    __m256h_u __v;
  } __attribute__((__packed__, __may_alias__));
  return ((const struct __loadu_ph *)__p)->__v;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_loadu_ph(void const *__p) {
  struct __loadu_ph {
    __m128h_u __v;
  } __attribute__((__packed__, __may_alias__));
  return ((const struct __loadu_ph *)__p)->__v;
}

// stores with vmovsh:
static __inline__ void __DEFAULT_FN_ATTRS128 _mm_store_sh(void *__dp,
                                                          __m128h __a) {
  struct __mm_store_sh_struct {
    _Float16 __u;
  } __attribute__((__packed__, __may_alias__));
  ((struct __mm_store_sh_struct *)__dp)->__u = __a[0];
}

static __inline__ void __DEFAULT_FN_ATTRS128 _mm_mask_store_sh(void *__W,
                                                               __mmask8 __U,
                                                               __m128h __A) {
  __builtin_ia32_storesh128_mask((__v8hf *)__W, __A, __U & 1);
}

static __inline__ void __DEFAULT_FN_ATTRS512 _mm512_store_ph(void *__P,
                                                             __m512h __A) {
  *(__m512h *)__P = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS256 _mm256_store_ph(void *__P,
                                                             __m256h __A) {
  *(__m256h *)__P = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS128 _mm_store_ph(void *__P,
                                                          __m128h __A) {
  *(__m128h *)__P = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS512 _mm512_storeu_ph(void *__P,
                                                              __m512h __A) {
  struct __storeu_ph {
    __m512h_u __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_ph *)__P)->__v = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS256 _mm256_storeu_ph(void *__P,
                                                              __m256h __A) {
  struct __storeu_ph {
    __m256h_u __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_ph *)__P)->__v = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS128 _mm_storeu_ph(void *__P,
                                                           __m128h __A) {
  struct __storeu_ph {
    __m128h_u __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_ph *)__P)->__v = __A;
}

// moves with vmovsh:
static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_move_sh(__m128h __a,
                                                            __m128h __b) {
  __a[0] = __b[0];
  return __a;
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_move_sh(__m128h __W,
                                                                 __mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return __builtin_ia32_selectsh_128(__U, _mm_move_sh(__A, __B), __W);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_move_sh(__mmask8 __U,
                                                                  __m128h __A,
                                                                  __m128h __B) {
  return __builtin_ia32_selectsh_128(__U, _mm_move_sh(__A, __B),
                                     _mm_setzero_ph());
}

// vmovw:
static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_cvtsi16_si128(short __a) {
  return (__m128i)(__v8hi){__a, 0, 0, 0, 0, 0, 0, 0};
}

static __inline__ short __DEFAULT_FN_ATTRS128 _mm_cvtsi128_si16(__m128i __a) {
  __v8hi __b = (__v8hi)__a;
  return __b[0];
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_rcp_ph(__m512h __A) {
  return (__m512h)__builtin_ia32_rcpph512_mask(
      (__v32hf)__A, (__v32hf)_mm512_undefined_ph(), (__mmask32)-1);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_rcp_ph(__m512h __W, __mmask32 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_rcpph512_mask((__v32hf)__A, (__v32hf)__W,
                                               (__mmask32)__U);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_rcp_ph(__mmask32 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_rcpph512_mask(
      (__v32hf)__A, (__v32hf)_mm512_setzero_ph(), (__mmask32)__U);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_rsqrt_ph(__m512h __A) {
  return (__m512h)__builtin_ia32_rsqrtph512_mask(
      (__v32hf)__A, (__v32hf)_mm512_undefined_ph(), (__mmask32)-1);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_rsqrt_ph(__m512h __W, __mmask32 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_rsqrtph512_mask((__v32hf)__A, (__v32hf)__W,
                                                 (__mmask32)__U);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_rsqrt_ph(__mmask32 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_rsqrtph512_mask(
      (__v32hf)__A, (__v32hf)_mm512_setzero_ph(), (__mmask32)__U);
}

#define _mm512_getmant_ph(A, B, C)                                             \
  ((__m512h)__builtin_ia32_getmantph512_mask(                                  \
      (__v32hf)(__m512h)(A), (int)(((C) << 2) | (B)),                          \
      (__v32hf)_mm512_undefined_ph(), (__mmask32)-1,                           \
      _MM_FROUND_CUR_DIRECTION))

#define _mm512_mask_getmant_ph(W, U, A, B, C)                                  \
  ((__m512h)__builtin_ia32_getmantph512_mask(                                  \
      (__v32hf)(__m512h)(A), (int)(((C) << 2) | (B)), (__v32hf)(__m512h)(W),   \
      (__mmask32)(U), _MM_FROUND_CUR_DIRECTION))

#define _mm512_maskz_getmant_ph(U, A, B, C)                                    \
  ((__m512h)__builtin_ia32_getmantph512_mask(                                  \
      (__v32hf)(__m512h)(A), (int)(((C) << 2) | (B)),                          \
      (__v32hf)_mm512_setzero_ph(), (__mmask32)(U), _MM_FROUND_CUR_DIRECTION))

#define _mm512_getmant_round_ph(A, B, C, R)                                    \
  ((__m512h)__builtin_ia32_getmantph512_mask(                                  \
      (__v32hf)(__m512h)(A), (int)(((C) << 2) | (B)),                          \
      (__v32hf)_mm512_undefined_ph(), (__mmask32)-1, (int)(R)))

#define _mm512_mask_getmant_round_ph(W, U, A, B, C, R)                         \
  ((__m512h)__builtin_ia32_getmantph512_mask(                                  \
      (__v32hf)(__m512h)(A), (int)(((C) << 2) | (B)), (__v32hf)(__m512h)(W),   \
      (__mmask32)(U), (int)(R)))

#define _mm512_maskz_getmant_round_ph(U, A, B, C, R)                           \
  ((__m512h)__builtin_ia32_getmantph512_mask(                                  \
      (__v32hf)(__m512h)(A), (int)(((C) << 2) | (B)),                          \
      (__v32hf)_mm512_setzero_ph(), (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_getexp_ph(__m512h __A) {
  return (__m512h)__builtin_ia32_getexpph512_mask(
      (__v32hf)__A, (__v32hf)_mm512_undefined_ph(), (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_getexp_ph(__m512h __W, __mmask32 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_getexpph512_mask(
      (__v32hf)__A, (__v32hf)__W, (__mmask32)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_getexp_ph(__mmask32 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_getexpph512_mask(
      (__v32hf)__A, (__v32hf)_mm512_setzero_ph(), (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_getexp_round_ph(A, R)                                           \
  ((__m512h)__builtin_ia32_getexpph512_mask((__v32hf)(__m512h)(A),             \
                                            (__v32hf)_mm512_undefined_ph(),    \
                                            (__mmask32)-1, (int)(R)))

#define _mm512_mask_getexp_round_ph(W, U, A, R)                                \
  ((__m512h)__builtin_ia32_getexpph512_mask(                                   \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(W), (__mmask32)(U), (int)(R)))

#define _mm512_maskz_getexp_round_ph(U, A, R)                                  \
  ((__m512h)__builtin_ia32_getexpph512_mask((__v32hf)(__m512h)(A),             \
                                            (__v32hf)_mm512_setzero_ph(),      \
                                            (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_scalef_ph(__m512h __A,
                                                                 __m512h __B) {
  return (__m512h)__builtin_ia32_scalefph512_mask(
      (__v32hf)__A, (__v32hf)__B, (__v32hf)_mm512_undefined_ph(), (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_scalef_ph(__m512h __W, __mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_scalefph512_mask((__v32hf)__A, (__v32hf)__B,
                                                  (__v32hf)__W, (__mmask32)__U,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_scalef_ph(__mmask32 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_scalefph512_mask(
      (__v32hf)__A, (__v32hf)__B, (__v32hf)_mm512_setzero_ph(), (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_scalef_round_ph(A, B, R)                                        \
  ((__m512h)__builtin_ia32_scalefph512_mask(                                   \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B),                            \
      (__v32hf)_mm512_undefined_ph(), (__mmask32)-1, (int)(R)))

#define _mm512_mask_scalef_round_ph(W, U, A, B, R)                             \
  ((__m512h)__builtin_ia32_scalefph512_mask(                                   \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(W),     \
      (__mmask32)(U), (int)(R)))

#define _mm512_maskz_scalef_round_ph(U, A, B, R)                               \
  ((__m512h)__builtin_ia32_scalefph512_mask(                                   \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B),                            \
      (__v32hf)_mm512_setzero_ph(), (__mmask32)(U), (int)(R)))

#define _mm512_roundscale_ph(A, B)                                             \
  ((__m512h)__builtin_ia32_rndscaleph_mask(                                    \
      (__v32hf)(__m512h)(A), (int)(B), (__v32hf)(__m512h)(A), (__mmask32)-1,   \
      _MM_FROUND_CUR_DIRECTION))

#define _mm512_mask_roundscale_ph(A, B, C, imm)                                \
  ((__m512h)__builtin_ia32_rndscaleph_mask(                                    \
      (__v32hf)(__m512h)(C), (int)(imm), (__v32hf)(__m512h)(A),                \
      (__mmask32)(B), _MM_FROUND_CUR_DIRECTION))

#define _mm512_maskz_roundscale_ph(A, B, imm)                                  \
  ((__m512h)__builtin_ia32_rndscaleph_mask(                                    \
      (__v32hf)(__m512h)(B), (int)(imm), (__v32hf)_mm512_setzero_ph(),         \
      (__mmask32)(A), _MM_FROUND_CUR_DIRECTION))

#define _mm512_mask_roundscale_round_ph(A, B, C, imm, R)                       \
  ((__m512h)__builtin_ia32_rndscaleph_mask((__v32hf)(__m512h)(C), (int)(imm),  \
                                           (__v32hf)(__m512h)(A),              \
                                           (__mmask32)(B), (int)(R)))

#define _mm512_maskz_roundscale_round_ph(A, B, imm, R)                         \
  ((__m512h)__builtin_ia32_rndscaleph_mask((__v32hf)(__m512h)(B), (int)(imm),  \
                                           (__v32hf)_mm512_setzero_ph(),       \
                                           (__mmask32)(A), (int)(R)))

#define _mm512_roundscale_round_ph(A, imm, R)                                  \
  ((__m512h)__builtin_ia32_rndscaleph_mask((__v32hf)(__m512h)(A), (int)(imm),  \
                                           (__v32hf)_mm512_undefined_ph(),     \
                                           (__mmask32)-1, (int)(R)))

#define _mm512_reduce_ph(A, imm)                                               \
  ((__m512h)__builtin_ia32_reduceph512_mask(                                   \
      (__v32hf)(__m512h)(A), (int)(imm), (__v32hf)_mm512_undefined_ph(),       \
      (__mmask32)-1, _MM_FROUND_CUR_DIRECTION))

#define _mm512_mask_reduce_ph(W, U, A, imm)                                    \
  ((__m512h)__builtin_ia32_reduceph512_mask(                                   \
      (__v32hf)(__m512h)(A), (int)(imm), (__v32hf)(__m512h)(W),                \
      (__mmask32)(U), _MM_FROUND_CUR_DIRECTION))

#define _mm512_maskz_reduce_ph(U, A, imm)                                      \
  ((__m512h)__builtin_ia32_reduceph512_mask(                                   \
      (__v32hf)(__m512h)(A), (int)(imm), (__v32hf)_mm512_setzero_ph(),         \
      (__mmask32)(U), _MM_FROUND_CUR_DIRECTION))

#define _mm512_mask_reduce_round_ph(W, U, A, imm, R)                           \
  ((__m512h)__builtin_ia32_reduceph512_mask((__v32hf)(__m512h)(A), (int)(imm), \
                                            (__v32hf)(__m512h)(W),             \
                                            (__mmask32)(U), (int)(R)))

#define _mm512_maskz_reduce_round_ph(U, A, imm, R)                             \
  ((__m512h)__builtin_ia32_reduceph512_mask((__v32hf)(__m512h)(A), (int)(imm), \
                                            (__v32hf)_mm512_setzero_ph(),      \
                                            (__mmask32)(U), (int)(R)))

#define _mm512_reduce_round_ph(A, imm, R)                                      \
  ((__m512h)__builtin_ia32_reduceph512_mask((__v32hf)(__m512h)(A), (int)(imm), \
                                            (__v32hf)_mm512_undefined_ph(),    \
                                            (__mmask32)-1, (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_rcp_sh(__m128h __A,
                                                           __m128h __B) {
  return (__m128h)__builtin_ia32_rcpsh_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_rcp_sh(__m128h __W,
                                                                __mmask8 __U,
                                                                __m128h __A,
                                                                __m128h __B) {
  return (__m128h)__builtin_ia32_rcpsh_mask((__v8hf)__A, (__v8hf)__B,
                                            (__v8hf)__W, (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_rcp_sh(__mmask8 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_rcpsh_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_rsqrt_sh(__m128h __A,
                                                             __m128h __B) {
  return (__m128h)__builtin_ia32_rsqrtsh_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)-1);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_rsqrt_sh(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A,
                                                                  __m128h __B) {
  return (__m128h)__builtin_ia32_rsqrtsh_mask((__v8hf)__A, (__v8hf)__B,
                                              (__v8hf)__W, (__mmask8)__U);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_rsqrt_sh(__mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_rsqrtsh_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)__U);
}

#define _mm_getmant_round_sh(A, B, C, D, R)                                    \
  ((__m128h)__builtin_ia32_getmantsh_round_mask(                               \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (int)(((D) << 2) | (C)),     \
      (__v8hf)_mm_setzero_ph(), (__mmask8)-1, (int)(R)))

#define _mm_getmant_sh(A, B, C, D)                                             \
  ((__m128h)__builtin_ia32_getmantsh_round_mask(                               \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (int)(((D) << 2) | (C)),     \
      (__v8hf)_mm_setzero_ph(), (__mmask8)-1, _MM_FROUND_CUR_DIRECTION))

#define _mm_mask_getmant_sh(W, U, A, B, C, D)                                  \
  ((__m128h)__builtin_ia32_getmantsh_round_mask(                               \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (int)(((D) << 2) | (C)),     \
      (__v8hf)(__m128h)(W), (__mmask8)(U), _MM_FROUND_CUR_DIRECTION))

#define _mm_mask_getmant_round_sh(W, U, A, B, C, D, R)                         \
  ((__m128h)__builtin_ia32_getmantsh_round_mask(                               \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (int)(((D) << 2) | (C)),     \
      (__v8hf)(__m128h)(W), (__mmask8)(U), (int)(R)))

#define _mm_maskz_getmant_sh(U, A, B, C, D)                                    \
  ((__m128h)__builtin_ia32_getmantsh_round_mask(                               \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (int)(((D) << 2) | (C)),     \
      (__v8hf)_mm_setzero_ph(), (__mmask8)(U), _MM_FROUND_CUR_DIRECTION))

#define _mm_maskz_getmant_round_sh(U, A, B, C, D, R)                           \
  ((__m128h)__builtin_ia32_getmantsh_round_mask(                               \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (int)(((D) << 2) | (C)),     \
      (__v8hf)_mm_setzero_ph(), (__mmask8)(U), (int)(R)))

#define _mm_getexp_round_sh(A, B, R)                                           \
  ((__m128h)__builtin_ia32_getexpsh128_round_mask(                             \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_getexp_sh(__m128h __A,
                                                              __m128h __B) {
  return (__m128h)__builtin_ia32_getexpsh128_round_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_getexp_sh(__m128h __W, __mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_getexpsh128_round_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)__W, (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_mask_getexp_round_sh(W, U, A, B, R)                                \
  ((__m128h)__builtin_ia32_getexpsh128_round_mask(                             \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_getexp_sh(__mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_getexpsh128_round_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_maskz_getexp_round_sh(U, A, B, R)                                  \
  ((__m128h)__builtin_ia32_getexpsh128_round_mask(                             \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(R)))

#define _mm_scalef_round_sh(A, B, R)                                           \
  ((__m128h)__builtin_ia32_scalefsh_round_mask(                                \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_scalef_sh(__m128h __A,
                                                              __m128h __B) {
  return (__m128h)__builtin_ia32_scalefsh_round_mask(
      (__v8hf)__A, (__v8hf)(__B), (__v8hf)_mm_setzero_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_scalef_sh(__m128h __W, __mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_scalefsh_round_mask((__v8hf)__A, (__v8hf)__B,
                                                     (__v8hf)__W, (__mmask8)__U,
                                                     _MM_FROUND_CUR_DIRECTION);
}

#define _mm_mask_scalef_round_sh(W, U, A, B, R)                                \
  ((__m128h)__builtin_ia32_scalefsh_round_mask(                                \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_scalef_sh(__mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_scalefsh_round_mask(
      (__v8hf)__A, (__v8hf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_maskz_scalef_round_sh(U, A, B, R)                                  \
  ((__m128h)__builtin_ia32_scalefsh_round_mask(                                \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(R)))

#define _mm_roundscale_round_sh(A, B, imm, R)                                  \
  ((__m128h)__builtin_ia32_rndscalesh_round_mask(                              \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(imm), (int)(R)))

#define _mm_roundscale_sh(A, B, imm)                                           \
  ((__m128h)__builtin_ia32_rndscalesh_round_mask(                              \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(imm), _MM_FROUND_CUR_DIRECTION))

#define _mm_mask_roundscale_sh(W, U, A, B, I)                                  \
  ((__m128h)__builtin_ia32_rndscalesh_round_mask(                              \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(I), _MM_FROUND_CUR_DIRECTION))

#define _mm_mask_roundscale_round_sh(W, U, A, B, I, R)                         \
  ((__m128h)__builtin_ia32_rndscalesh_round_mask(                              \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(I), (int)(R)))

#define _mm_maskz_roundscale_sh(U, A, B, I)                                    \
  ((__m128h)__builtin_ia32_rndscalesh_round_mask(                              \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(I), _MM_FROUND_CUR_DIRECTION))

#define _mm_maskz_roundscale_round_sh(U, A, B, I, R)                           \
  ((__m128h)__builtin_ia32_rndscalesh_round_mask(                              \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(I), (int)(R)))

#define _mm_reduce_sh(A, B, C)                                                 \
  ((__m128h)__builtin_ia32_reducesh_mask(                                      \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(C), _MM_FROUND_CUR_DIRECTION))

#define _mm_mask_reduce_sh(W, U, A, B, C)                                      \
  ((__m128h)__builtin_ia32_reducesh_mask(                                      \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(C), _MM_FROUND_CUR_DIRECTION))

#define _mm_maskz_reduce_sh(U, A, B, C)                                        \
  ((__m128h)__builtin_ia32_reducesh_mask(                                      \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(C), _MM_FROUND_CUR_DIRECTION))

#define _mm_reduce_round_sh(A, B, C, R)                                        \
  ((__m128h)__builtin_ia32_reducesh_mask(                                      \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(C), (int)(R)))

#define _mm_mask_reduce_round_sh(W, U, A, B, C, R)                             \
  ((__m128h)__builtin_ia32_reducesh_mask(                                      \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(C), (int)(R)))

#define _mm_maskz_reduce_round_sh(U, A, B, C, R)                               \
  ((__m128h)__builtin_ia32_reducesh_mask(                                      \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(C), (int)(R)))

#define _mm512_sqrt_round_ph(A, R)                                             \
  ((__m512h)__builtin_ia32_sqrtph512((__v32hf)(__m512h)(A), (int)(R)))

#define _mm512_mask_sqrt_round_ph(W, U, A, R)                                  \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_sqrt_round_ph((A), (R)),                 \
      (__v32hf)(__m512h)(W)))

#define _mm512_maskz_sqrt_round_ph(U, A, R)                                    \
  ((__m512h)__builtin_ia32_selectph_512(                                       \
      (__mmask32)(U), (__v32hf)_mm512_sqrt_round_ph((A), (R)),                 \
      (__v32hf)_mm512_setzero_ph()))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_sqrt_ph(__m512h __A) {
  return (__m512h)__builtin_ia32_sqrtph512((__v32hf)__A,
                                           _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_sqrt_ph(__m512h __W, __mmask32 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_selectph_512(
      (__mmask32)(__U),
      (__v32hf)__builtin_ia32_sqrtph512((__A), (_MM_FROUND_CUR_DIRECTION)),
      (__v32hf)(__m512h)(__W));
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_sqrt_ph(__mmask32 __U, __m512h __A) {
  return (__m512h)__builtin_ia32_selectph_512(
      (__mmask32)(__U),
      (__v32hf)__builtin_ia32_sqrtph512((__A), (_MM_FROUND_CUR_DIRECTION)),
      (__v32hf)_mm512_setzero_ph());
}

#define _mm_sqrt_round_sh(A, B, R)                                             \
  ((__m128h)__builtin_ia32_sqrtsh_round_mask(                                  \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_sqrt_round_sh(W, U, A, B, R)                                  \
  ((__m128h)__builtin_ia32_sqrtsh_round_mask(                                  \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_sqrt_round_sh(U, A, B, R)                                    \
  ((__m128h)__builtin_ia32_sqrtsh_round_mask(                                  \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)_mm_setzero_ph(),    \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_sqrt_sh(__m128h __A,
                                                            __m128h __B) {
  return (__m128h)__builtin_ia32_sqrtsh_round_mask(
      (__v8hf)(__m128h)(__A), (__v8hf)(__m128h)(__B), (__v8hf)_mm_setzero_ph(),
      (__mmask8)-1, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_sqrt_sh(__m128h __W,
                                                                 __mmask32 __U,
                                                                 __m128h __A,
                                                                 __m128h __B) {
  return (__m128h)__builtin_ia32_sqrtsh_round_mask(
      (__v8hf)(__m128h)(__A), (__v8hf)(__m128h)(__B), (__v8hf)(__m128h)(__W),
      (__mmask8)(__U), _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_sqrt_sh(__mmask32 __U,
                                                                  __m128h __A,
                                                                  __m128h __B) {
  return (__m128h)__builtin_ia32_sqrtsh_round_mask(
      (__v8hf)(__m128h)(__A), (__v8hf)(__m128h)(__B), (__v8hf)_mm_setzero_ph(),
      (__mmask8)(__U), _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_mask_fpclass_ph_mask(U, A, imm)                                 \
  ((__mmask32)__builtin_ia32_fpclassph512_mask((__v32hf)(__m512h)(A),          \
                                               (int)(imm), (__mmask32)(U)))

#define _mm512_fpclass_ph_mask(A, imm)                                         \
  ((__mmask32)__builtin_ia32_fpclassph512_mask((__v32hf)(__m512h)(A),          \
                                               (int)(imm), (__mmask32)-1))

#define _mm_fpclass_sh_mask(A, imm)                                            \
  ((__mmask8)__builtin_ia32_fpclasssh_mask((__v8hf)(__m128h)(A), (int)(imm),   \
                                           (__mmask8)-1))

#define _mm_mask_fpclass_sh_mask(U, A, imm)                                    \
  ((__mmask8)__builtin_ia32_fpclasssh_mask((__v8hf)(__m128h)(A), (int)(imm),   \
                                           (__mmask8)(U)))

#define _mm512_cvt_roundpd_ph(A, R)                                            \
  ((__m128h)__builtin_ia32_vcvtpd2ph512_mask(                                  \
      (__v8df)(A), (__v8hf)_mm_undefined_ph(), (__mmask8)(-1), (int)(R)))

#define _mm512_mask_cvt_roundpd_ph(W, U, A, R)                                 \
  ((__m128h)__builtin_ia32_vcvtpd2ph512_mask((__v8df)(A), (__v8hf)(W),         \
                                             (__mmask8)(U), (int)(R)))

#define _mm512_maskz_cvt_roundpd_ph(U, A, R)                                   \
  ((__m128h)__builtin_ia32_vcvtpd2ph512_mask(                                  \
      (__v8df)(A), (__v8hf)_mm_setzero_ph(), (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS512 _mm512_cvtpd_ph(__m512d __A) {
  return (__m128h)__builtin_ia32_vcvtpd2ph512_mask(
      (__v8df)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS512
_mm512_mask_cvtpd_ph(__m128h __W, __mmask8 __U, __m512d __A) {
  return (__m128h)__builtin_ia32_vcvtpd2ph512_mask(
      (__v8df)__A, (__v8hf)__W, (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtpd_ph(__mmask8 __U, __m512d __A) {
  return (__m128h)__builtin_ia32_vcvtpd2ph512_mask(
      (__v8df)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundph_pd(A, R)                                            \
  ((__m512d)__builtin_ia32_vcvtph2pd512_mask(                                  \
      (__v8hf)(A), (__v8df)_mm512_undefined_pd(), (__mmask8)(-1), (int)(R)))

#define _mm512_mask_cvt_roundph_pd(W, U, A, R)                                 \
  ((__m512d)__builtin_ia32_vcvtph2pd512_mask((__v8hf)(A), (__v8df)(W),         \
                                             (__mmask8)(U), (int)(R)))

#define _mm512_maskz_cvt_roundph_pd(U, A, R)                                   \
  ((__m512d)__builtin_ia32_vcvtph2pd512_mask(                                  \
      (__v8hf)(A), (__v8df)_mm512_setzero_pd(), (__mmask8)(U), (int)(R)))

static __inline__ __m512d __DEFAULT_FN_ATTRS512 _mm512_cvtph_pd(__m128h __A) {
  return (__m512d)__builtin_ia32_vcvtph2pd512_mask(
      (__v8hf)__A, (__v8df)_mm512_setzero_pd(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512d __DEFAULT_FN_ATTRS512
_mm512_mask_cvtph_pd(__m512d __W, __mmask8 __U, __m128h __A) {
  return (__m512d)__builtin_ia32_vcvtph2pd512_mask(
      (__v8hf)__A, (__v8df)__W, (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512d __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtph_pd(__mmask8 __U, __m128h __A) {
  return (__m512d)__builtin_ia32_vcvtph2pd512_mask(
      (__v8hf)__A, (__v8df)_mm512_setzero_pd(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_cvt_roundsh_ss(A, B, R)                                            \
  ((__m128)__builtin_ia32_vcvtsh2ss_round_mask((__v4sf)(A), (__v8hf)(B),       \
                                               (__v4sf)_mm_undefined_ps(),     \
                                               (__mmask8)(-1), (int)(R)))

#define _mm_mask_cvt_roundsh_ss(W, U, A, B, R)                                 \
  ((__m128)__builtin_ia32_vcvtsh2ss_round_mask(                                \
      (__v4sf)(A), (__v8hf)(B), (__v4sf)(W), (__mmask8)(U), (int)(R)))

#define _mm_maskz_cvt_roundsh_ss(U, A, B, R)                                   \
  ((__m128)__builtin_ia32_vcvtsh2ss_round_mask((__v4sf)(A), (__v8hf)(B),       \
                                               (__v4sf)_mm_setzero_ps(),       \
                                               (__mmask8)(U), (int)(R)))

static __inline__ __m128 __DEFAULT_FN_ATTRS128 _mm_cvtsh_ss(__m128 __A,
                                                            __m128h __B) {
  return (__m128)__builtin_ia32_vcvtsh2ss_round_mask(
      (__v4sf)__A, (__v8hf)__B, (__v4sf)_mm_undefined_ps(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128 _mm_mask_cvtsh_ss(__m128 __W,
                                                                 __mmask8 __U,
                                                                 __m128 __A,
                                                                 __m128h __B) {
  return (__m128)__builtin_ia32_vcvtsh2ss_round_mask((__v4sf)__A, (__v8hf)__B,
                                                     (__v4sf)__W, (__mmask8)__U,
                                                     _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128 _mm_maskz_cvtsh_ss(__mmask8 __U,
                                                                  __m128 __A,
                                                                  __m128h __B) {
  return (__m128)__builtin_ia32_vcvtsh2ss_round_mask(
      (__v4sf)__A, (__v8hf)__B, (__v4sf)_mm_setzero_ps(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_cvt_roundss_sh(A, B, R)                                            \
  ((__m128h)__builtin_ia32_vcvtss2sh_round_mask((__v8hf)(A), (__v4sf)(B),      \
                                                (__v8hf)_mm_undefined_ph(),    \
                                                (__mmask8)(-1), (int)(R)))

#define _mm_mask_cvt_roundss_sh(W, U, A, B, R)                                 \
  ((__m128h)__builtin_ia32_vcvtss2sh_round_mask(                               \
      (__v8hf)(A), (__v4sf)(B), (__v8hf)(W), (__mmask8)(U), (int)(R)))

#define _mm_maskz_cvt_roundss_sh(U, A, B, R)                                   \
  ((__m128h)__builtin_ia32_vcvtss2sh_round_mask((__v8hf)(A), (__v4sf)(B),      \
                                                (__v8hf)_mm_setzero_ph(),      \
                                                (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtss_sh(__m128h __A,
                                                             __m128 __B) {
  return (__m128h)__builtin_ia32_vcvtss2sh_round_mask(
      (__v8hf)__A, (__v4sf)__B, (__v8hf)_mm_undefined_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_cvtss_sh(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A,
                                                                  __m128 __B) {
  return (__m128h)__builtin_ia32_vcvtss2sh_round_mask(
      (__v8hf)__A, (__v4sf)__B, (__v8hf)__W, (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_maskz_cvtss_sh(__mmask8 __U,
                                                                   __m128h __A,
                                                                   __m128 __B) {
  return (__m128h)__builtin_ia32_vcvtss2sh_round_mask(
      (__v8hf)__A, (__v4sf)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_cvt_roundsd_sh(A, B, R)                                            \
  ((__m128h)__builtin_ia32_vcvtsd2sh_round_mask((__v8hf)(A), (__v2df)(B),      \
                                                (__v8hf)_mm_undefined_ph(),    \
                                                (__mmask8)(-1), (int)(R)))

#define _mm_mask_cvt_roundsd_sh(W, U, A, B, R)                                 \
  ((__m128h)__builtin_ia32_vcvtsd2sh_round_mask(                               \
      (__v8hf)(A), (__v2df)(B), (__v8hf)(W), (__mmask8)(U), (int)(R)))

#define _mm_maskz_cvt_roundsd_sh(U, A, B, R)                                   \
  ((__m128h)__builtin_ia32_vcvtsd2sh_round_mask((__v8hf)(A), (__v2df)(B),      \
                                                (__v8hf)_mm_setzero_ph(),      \
                                                (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvtsd_sh(__m128h __A,
                                                             __m128d __B) {
  return (__m128h)__builtin_ia32_vcvtsd2sh_round_mask(
      (__v8hf)__A, (__v2df)__B, (__v8hf)_mm_undefined_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_cvtsd_sh(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A,
                                                                  __m128d __B) {
  return (__m128h)__builtin_ia32_vcvtsd2sh_round_mask(
      (__v8hf)__A, (__v2df)__B, (__v8hf)__W, (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_cvtsd_sh(__mmask8 __U, __m128h __A, __m128d __B) {
  return (__m128h)__builtin_ia32_vcvtsd2sh_round_mask(
      (__v8hf)__A, (__v2df)__B, (__v8hf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_cvt_roundsh_sd(A, B, R)                                            \
  ((__m128d)__builtin_ia32_vcvtsh2sd_round_mask((__v2df)(A), (__v8hf)(B),      \
                                                (__v2df)_mm_undefined_pd(),    \
                                                (__mmask8)(-1), (int)(R)))

#define _mm_mask_cvt_roundsh_sd(W, U, A, B, R)                                 \
  ((__m128d)__builtin_ia32_vcvtsh2sd_round_mask(                               \
      (__v2df)(A), (__v8hf)(B), (__v2df)(W), (__mmask8)(U), (int)(R)))

#define _mm_maskz_cvt_roundsh_sd(U, A, B, R)                                   \
  ((__m128d)__builtin_ia32_vcvtsh2sd_round_mask((__v2df)(A), (__v8hf)(B),      \
                                                (__v2df)_mm_setzero_pd(),      \
                                                (__mmask8)(U), (int)(R)))

static __inline__ __m128d __DEFAULT_FN_ATTRS128 _mm_cvtsh_sd(__m128d __A,
                                                             __m128h __B) {
  return (__m128d)__builtin_ia32_vcvtsh2sd_round_mask(
      (__v2df)__A, (__v8hf)__B, (__v2df)_mm_undefined_pd(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128 _mm_mask_cvtsh_sd(__m128d __W,
                                                                  __mmask8 __U,
                                                                  __m128d __A,
                                                                  __m128h __B) {
  return (__m128d)__builtin_ia32_vcvtsh2sd_round_mask(
      (__v2df)__A, (__v8hf)__B, (__v2df)__W, (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_maskz_cvtsh_sd(__mmask8 __U, __m128d __A, __m128h __B) {
  return (__m128d)__builtin_ia32_vcvtsh2sd_round_mask(
      (__v2df)__A, (__v8hf)__B, (__v2df)_mm_setzero_pd(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundph_epi16(A, R)                                         \
  ((__m512i)__builtin_ia32_vcvtph2w512_mask((__v32hf)(A),                      \
                                            (__v32hi)_mm512_undefined_epi32(), \
                                            (__mmask32)(-1), (int)(R)))

#define _mm512_mask_cvt_roundph_epi16(W, U, A, R)                              \
  ((__m512i)__builtin_ia32_vcvtph2w512_mask((__v32hf)(A), (__v32hi)(W),        \
                                            (__mmask32)(U), (int)(R)))

#define _mm512_maskz_cvt_roundph_epi16(U, A, R)                                \
  ((__m512i)__builtin_ia32_vcvtph2w512_mask((__v32hf)(A),                      \
                                            (__v32hi)_mm512_setzero_epi32(),   \
                                            (__mmask32)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvtph_epi16(__m512h __A) {
  return (__m512i)__builtin_ia32_vcvtph2w512_mask(
      (__v32hf)__A, (__v32hi)_mm512_setzero_epi32(), (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtph_epi16(__m512i __W, __mmask32 __U, __m512h __A) {
  return (__m512i)__builtin_ia32_vcvtph2w512_mask(
      (__v32hf)__A, (__v32hi)__W, (__mmask32)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtph_epi16(__mmask32 __U, __m512h __A) {
  return (__m512i)__builtin_ia32_vcvtph2w512_mask(
      (__v32hf)__A, (__v32hi)_mm512_setzero_epi32(), (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvtt_roundph_epi16(A, R)                                        \
  ((__m512i)__builtin_ia32_vcvttph2w512_mask(                                  \
      (__v32hf)(A), (__v32hi)_mm512_undefined_epi32(), (__mmask32)(-1),        \
      (int)(R)))

#define _mm512_mask_cvtt_roundph_epi16(W, U, A, R)                             \
  ((__m512i)__builtin_ia32_vcvttph2w512_mask((__v32hf)(A), (__v32hi)(W),       \
                                             (__mmask32)(U), (int)(R)))

#define _mm512_maskz_cvtt_roundph_epi16(U, A, R)                               \
  ((__m512i)__builtin_ia32_vcvttph2w512_mask((__v32hf)(A),                     \
                                             (__v32hi)_mm512_setzero_epi32(),  \
                                             (__mmask32)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvttph_epi16(__m512h __A) {
  return (__m512i)__builtin_ia32_vcvttph2w512_mask(
      (__v32hf)__A, (__v32hi)_mm512_setzero_epi32(), (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvttph_epi16(__m512i __W, __mmask32 __U, __m512h __A) {
  return (__m512i)__builtin_ia32_vcvttph2w512_mask(
      (__v32hf)__A, (__v32hi)__W, (__mmask32)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvttph_epi16(__mmask32 __U, __m512h __A) {
  return (__m512i)__builtin_ia32_vcvttph2w512_mask(
      (__v32hf)__A, (__v32hi)_mm512_setzero_epi32(), (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundepi16_ph(A, R)                                         \
  ((__m512h)__builtin_ia32_vcvtw2ph512_mask((__v32hi)(A),                      \
                                            (__v32hf)_mm512_undefined_ph(),    \
                                            (__mmask32)(-1), (int)(R)))

#define _mm512_mask_cvt_roundepi16_ph(W, U, A, R)                              \
  ((__m512h)__builtin_ia32_vcvtw2ph512_mask((__v32hi)(A), (__v32hf)(W),        \
                                            (__mmask32)(U), (int)(R)))

#define _mm512_maskz_cvt_roundepi16_ph(U, A, R)                                \
  ((__m512h)__builtin_ia32_vcvtw2ph512_mask(                                   \
      (__v32hi)(A), (__v32hf)_mm512_setzero_ph(), (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_cvtepi16_ph(__m512i __A) {
  return (__m512h)__builtin_ia32_vcvtw2ph512_mask(
      (__v32hi)__A, (__v32hf)_mm512_setzero_ph(), (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepi16_ph(__m512h __W, __mmask32 __U, __m512i __A) {
  return (__m512h)__builtin_ia32_vcvtw2ph512_mask(
      (__v32hi)__A, (__v32hf)__W, (__mmask32)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtepi16_ph(__mmask32 __U, __m512i __A) {
  return (__m512h)__builtin_ia32_vcvtw2ph512_mask(
      (__v32hi)__A, (__v32hf)_mm512_setzero_ph(), (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundph_epu16(A, R)                                         \
  ((__m512i)__builtin_ia32_vcvtph2uw512_mask(                                  \
      (__v32hf)(A), (__v32hu)_mm512_undefined_epi32(), (__mmask32)(-1),        \
      (int)(R)))

#define _mm512_mask_cvt_roundph_epu16(W, U, A, R)                              \
  ((__m512i)__builtin_ia32_vcvtph2uw512_mask((__v32hf)(A), (__v32hu)(W),       \
                                             (__mmask32)(U), (int)(R)))

#define _mm512_maskz_cvt_roundph_epu16(U, A, R)                                \
  ((__m512i)__builtin_ia32_vcvtph2uw512_mask((__v32hf)(A),                     \
                                             (__v32hu)_mm512_setzero_epi32(),  \
                                             (__mmask32)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvtph_epu16(__m512h __A) {
  return (__m512i)__builtin_ia32_vcvtph2uw512_mask(
      (__v32hf)__A, (__v32hu)_mm512_setzero_epi32(), (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtph_epu16(__m512i __W, __mmask32 __U, __m512h __A) {
  return (__m512i)__builtin_ia32_vcvtph2uw512_mask(
      (__v32hf)__A, (__v32hu)__W, (__mmask32)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtph_epu16(__mmask32 __U, __m512h __A) {
  return (__m512i)__builtin_ia32_vcvtph2uw512_mask(
      (__v32hf)__A, (__v32hu)_mm512_setzero_epi32(), (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvtt_roundph_epu16(A, R)                                        \
  ((__m512i)__builtin_ia32_vcvttph2uw512_mask(                                 \
      (__v32hf)(A), (__v32hu)_mm512_undefined_epi32(), (__mmask32)(-1),        \
      (int)(R)))

#define _mm512_mask_cvtt_roundph_epu16(W, U, A, R)                             \
  ((__m512i)__builtin_ia32_vcvttph2uw512_mask((__v32hf)(A), (__v32hu)(W),      \
                                              (__mmask32)(U), (int)(R)))

#define _mm512_maskz_cvtt_roundph_epu16(U, A, R)                               \
  ((__m512i)__builtin_ia32_vcvttph2uw512_mask((__v32hf)(A),                    \
                                              (__v32hu)_mm512_setzero_epi32(), \
                                              (__mmask32)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvttph_epu16(__m512h __A) {
  return (__m512i)__builtin_ia32_vcvttph2uw512_mask(
      (__v32hf)__A, (__v32hu)_mm512_setzero_epi32(), (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvttph_epu16(__m512i __W, __mmask32 __U, __m512h __A) {
  return (__m512i)__builtin_ia32_vcvttph2uw512_mask(
      (__v32hf)__A, (__v32hu)__W, (__mmask32)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvttph_epu16(__mmask32 __U, __m512h __A) {
  return (__m512i)__builtin_ia32_vcvttph2uw512_mask(
      (__v32hf)__A, (__v32hu)_mm512_setzero_epi32(), (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundepu16_ph(A, R)                                         \
  ((__m512h)__builtin_ia32_vcvtuw2ph512_mask((__v32hu)(A),                     \
                                             (__v32hf)_mm512_undefined_ph(),   \
                                             (__mmask32)(-1), (int)(R)))

#define _mm512_mask_cvt_roundepu16_ph(W, U, A, R)                              \
  ((__m512h)__builtin_ia32_vcvtuw2ph512_mask((__v32hu)(A), (__v32hf)(W),       \
                                             (__mmask32)(U), (int)(R)))

#define _mm512_maskz_cvt_roundepu16_ph(U, A, R)                                \
  ((__m512h)__builtin_ia32_vcvtuw2ph512_mask(                                  \
      (__v32hu)(A), (__v32hf)_mm512_setzero_ph(), (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_cvtepu16_ph(__m512i __A) {
  return (__m512h)__builtin_ia32_vcvtuw2ph512_mask(
      (__v32hu)__A, (__v32hf)_mm512_setzero_ph(), (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepu16_ph(__m512h __W, __mmask32 __U, __m512i __A) {
  return (__m512h)__builtin_ia32_vcvtuw2ph512_mask(
      (__v32hu)__A, (__v32hf)__W, (__mmask32)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtepu16_ph(__mmask32 __U, __m512i __A) {
  return (__m512h)__builtin_ia32_vcvtuw2ph512_mask(
      (__v32hu)__A, (__v32hf)_mm512_setzero_ph(), (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundph_epi32(A, R)                                         \
  ((__m512i)__builtin_ia32_vcvtph2dq512_mask(                                  \
      (__v16hf)(A), (__v16si)_mm512_undefined_epi32(), (__mmask16)(-1),        \
      (int)(R)))

#define _mm512_mask_cvt_roundph_epi32(W, U, A, R)                              \
  ((__m512i)__builtin_ia32_vcvtph2dq512_mask((__v16hf)(A), (__v16si)(W),       \
                                             (__mmask16)(U), (int)(R)))

#define _mm512_maskz_cvt_roundph_epi32(U, A, R)                                \
  ((__m512i)__builtin_ia32_vcvtph2dq512_mask((__v16hf)(A),                     \
                                             (__v16si)_mm512_setzero_epi32(),  \
                                             (__mmask16)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvtph_epi32(__m256h __A) {
  return (__m512i)__builtin_ia32_vcvtph2dq512_mask(
      (__v16hf)__A, (__v16si)_mm512_setzero_epi32(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtph_epi32(__m512i __W, __mmask16 __U, __m256h __A) {
  return (__m512i)__builtin_ia32_vcvtph2dq512_mask(
      (__v16hf)__A, (__v16si)__W, (__mmask16)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtph_epi32(__mmask16 __U, __m256h __A) {
  return (__m512i)__builtin_ia32_vcvtph2dq512_mask(
      (__v16hf)__A, (__v16si)_mm512_setzero_epi32(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundph_epu32(A, R)                                         \
  ((__m512i)__builtin_ia32_vcvtph2udq512_mask(                                 \
      (__v16hf)(A), (__v16su)_mm512_undefined_epi32(), (__mmask16)(-1),        \
      (int)(R)))

#define _mm512_mask_cvt_roundph_epu32(W, U, A, R)                              \
  ((__m512i)__builtin_ia32_vcvtph2udq512_mask((__v16hf)(A), (__v16su)(W),      \
                                              (__mmask16)(U), (int)(R)))

#define _mm512_maskz_cvt_roundph_epu32(U, A, R)                                \
  ((__m512i)__builtin_ia32_vcvtph2udq512_mask((__v16hf)(A),                    \
                                              (__v16su)_mm512_setzero_epi32(), \
                                              (__mmask16)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvtph_epu32(__m256h __A) {
  return (__m512i)__builtin_ia32_vcvtph2udq512_mask(
      (__v16hf)__A, (__v16su)_mm512_setzero_epi32(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtph_epu32(__m512i __W, __mmask16 __U, __m256h __A) {
  return (__m512i)__builtin_ia32_vcvtph2udq512_mask(
      (__v16hf)__A, (__v16su)__W, (__mmask16)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtph_epu32(__mmask16 __U, __m256h __A) {
  return (__m512i)__builtin_ia32_vcvtph2udq512_mask(
      (__v16hf)__A, (__v16su)_mm512_setzero_epi32(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundepi32_ph(A, R)                                         \
  ((__m256h)__builtin_ia32_vcvtdq2ph512_mask((__v16si)(A),                     \
                                             (__v16hf)_mm256_undefined_ph(),   \
                                             (__mmask16)(-1), (int)(R)))

#define _mm512_mask_cvt_roundepi32_ph(W, U, A, R)                              \
  ((__m256h)__builtin_ia32_vcvtdq2ph512_mask((__v16si)(A), (__v16hf)(W),       \
                                             (__mmask16)(U), (int)(R)))

#define _mm512_maskz_cvt_roundepi32_ph(U, A, R)                                \
  ((__m256h)__builtin_ia32_vcvtdq2ph512_mask(                                  \
      (__v16si)(A), (__v16hf)_mm256_setzero_ph(), (__mmask16)(U), (int)(R)))

static __inline__ __m256h __DEFAULT_FN_ATTRS512
_mm512_cvtepi32_ph(__m512i __A) {
  return (__m256h)__builtin_ia32_vcvtdq2ph512_mask(
      (__v16si)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepi32_ph(__m256h __W, __mmask16 __U, __m512i __A) {
  return (__m256h)__builtin_ia32_vcvtdq2ph512_mask(
      (__v16si)__A, (__v16hf)__W, (__mmask16)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtepi32_ph(__mmask16 __U, __m512i __A) {
  return (__m256h)__builtin_ia32_vcvtdq2ph512_mask(
      (__v16si)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundepu32_ph(A, R)                                         \
  ((__m256h)__builtin_ia32_vcvtudq2ph512_mask((__v16su)(A),                    \
                                              (__v16hf)_mm256_undefined_ph(),  \
                                              (__mmask16)(-1), (int)(R)))

#define _mm512_mask_cvt_roundepu32_ph(W, U, A, R)                              \
  ((__m256h)__builtin_ia32_vcvtudq2ph512_mask((__v16su)(A), (__v16hf)(W),      \
                                              (__mmask16)(U), (int)(R)))

#define _mm512_maskz_cvt_roundepu32_ph(U, A, R)                                \
  ((__m256h)__builtin_ia32_vcvtudq2ph512_mask(                                 \
      (__v16su)(A), (__v16hf)_mm256_setzero_ph(), (__mmask16)(U), (int)(R)))

static __inline__ __m256h __DEFAULT_FN_ATTRS512
_mm512_cvtepu32_ph(__m512i __A) {
  return (__m256h)__builtin_ia32_vcvtudq2ph512_mask(
      (__v16su)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepu32_ph(__m256h __W, __mmask16 __U, __m512i __A) {
  return (__m256h)__builtin_ia32_vcvtudq2ph512_mask(
      (__v16su)__A, (__v16hf)__W, (__mmask16)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtepu32_ph(__mmask16 __U, __m512i __A) {
  return (__m256h)__builtin_ia32_vcvtudq2ph512_mask(
      (__v16su)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvtt_roundph_epi32(A, R)                                        \
  ((__m512i)__builtin_ia32_vcvttph2dq512_mask(                                 \
      (__v16hf)(A), (__v16si)_mm512_undefined_epi32(), (__mmask16)(-1),        \
      (int)(R)))

#define _mm512_mask_cvtt_roundph_epi32(W, U, A, R)                             \
  ((__m512i)__builtin_ia32_vcvttph2dq512_mask((__v16hf)(A), (__v16si)(W),      \
                                              (__mmask16)(U), (int)(R)))

#define _mm512_maskz_cvtt_roundph_epi32(U, A, R)                               \
  ((__m512i)__builtin_ia32_vcvttph2dq512_mask((__v16hf)(A),                    \
                                              (__v16si)_mm512_setzero_epi32(), \
                                              (__mmask16)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvttph_epi32(__m256h __A) {
  return (__m512i)__builtin_ia32_vcvttph2dq512_mask(
      (__v16hf)__A, (__v16si)_mm512_setzero_epi32(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvttph_epi32(__m512i __W, __mmask16 __U, __m256h __A) {
  return (__m512i)__builtin_ia32_vcvttph2dq512_mask(
      (__v16hf)__A, (__v16si)__W, (__mmask16)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvttph_epi32(__mmask16 __U, __m256h __A) {
  return (__m512i)__builtin_ia32_vcvttph2dq512_mask(
      (__v16hf)__A, (__v16si)_mm512_setzero_epi32(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvtt_roundph_epu32(A, R)                                        \
  ((__m512i)__builtin_ia32_vcvttph2udq512_mask(                                \
      (__v16hf)(A), (__v16su)_mm512_undefined_epi32(), (__mmask16)(-1),        \
      (int)(R)))

#define _mm512_mask_cvtt_roundph_epu32(W, U, A, R)                             \
  ((__m512i)__builtin_ia32_vcvttph2udq512_mask((__v16hf)(A), (__v16su)(W),     \
                                               (__mmask16)(U), (int)(R)))

#define _mm512_maskz_cvtt_roundph_epu32(U, A, R)                               \
  ((__m512i)__builtin_ia32_vcvttph2udq512_mask(                                \
      (__v16hf)(A), (__v16su)_mm512_setzero_epi32(), (__mmask16)(U),           \
      (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvttph_epu32(__m256h __A) {
  return (__m512i)__builtin_ia32_vcvttph2udq512_mask(
      (__v16hf)__A, (__v16su)_mm512_setzero_epi32(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvttph_epu32(__m512i __W, __mmask16 __U, __m256h __A) {
  return (__m512i)__builtin_ia32_vcvttph2udq512_mask(
      (__v16hf)__A, (__v16su)__W, (__mmask16)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvttph_epu32(__mmask16 __U, __m256h __A) {
  return (__m512i)__builtin_ia32_vcvttph2udq512_mask(
      (__v16hf)__A, (__v16su)_mm512_setzero_epi32(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundepi64_ph(A, R)                                         \
  ((__m128h)__builtin_ia32_vcvtqq2ph512_mask(                                  \
      (__v8di)(A), (__v8hf)_mm_undefined_ph(), (__mmask8)(-1), (int)(R)))

#define _mm512_mask_cvt_roundepi64_ph(W, U, A, R)                              \
  ((__m128h)__builtin_ia32_vcvtqq2ph512_mask((__v8di)(A), (__v8hf)(W),         \
                                             (__mmask8)(U), (int)(R)))

#define _mm512_maskz_cvt_roundepi64_ph(U, A, R)                                \
  ((__m128h)__builtin_ia32_vcvtqq2ph512_mask(                                  \
      (__v8di)(A), (__v8hf)_mm_setzero_ph(), (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS512
_mm512_cvtepi64_ph(__m512i __A) {
  return (__m128h)__builtin_ia32_vcvtqq2ph512_mask(
      (__v8di)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepi64_ph(__m128h __W, __mmask8 __U, __m512i __A) {
  return (__m128h)__builtin_ia32_vcvtqq2ph512_mask(
      (__v8di)__A, (__v8hf)__W, (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtepi64_ph(__mmask8 __U, __m512i __A) {
  return (__m128h)__builtin_ia32_vcvtqq2ph512_mask(
      (__v8di)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundph_epi64(A, R)                                         \
  ((__m512i)__builtin_ia32_vcvtph2qq512_mask((__v8hf)(A),                      \
                                             (__v8di)_mm512_undefined_epi32(), \
                                             (__mmask8)(-1), (int)(R)))

#define _mm512_mask_cvt_roundph_epi64(W, U, A, R)                              \
  ((__m512i)__builtin_ia32_vcvtph2qq512_mask((__v8hf)(A), (__v8di)(W),         \
                                             (__mmask8)(U), (int)(R)))

#define _mm512_maskz_cvt_roundph_epi64(U, A, R)                                \
  ((__m512i)__builtin_ia32_vcvtph2qq512_mask(                                  \
      (__v8hf)(A), (__v8di)_mm512_setzero_epi32(), (__mmask8)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvtph_epi64(__m128h __A) {
  return (__m512i)__builtin_ia32_vcvtph2qq512_mask(
      (__v8hf)__A, (__v8di)_mm512_setzero_epi32(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtph_epi64(__m512i __W, __mmask8 __U, __m128h __A) {
  return (__m512i)__builtin_ia32_vcvtph2qq512_mask(
      (__v8hf)__A, (__v8di)__W, (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtph_epi64(__mmask8 __U, __m128h __A) {
  return (__m512i)__builtin_ia32_vcvtph2qq512_mask(
      (__v8hf)__A, (__v8di)_mm512_setzero_epi32(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundepu64_ph(A, R)                                         \
  ((__m128h)__builtin_ia32_vcvtuqq2ph512_mask(                                 \
      (__v8du)(A), (__v8hf)_mm_undefined_ph(), (__mmask8)(-1), (int)(R)))

#define _mm512_mask_cvt_roundepu64_ph(W, U, A, R)                              \
  ((__m128h)__builtin_ia32_vcvtuqq2ph512_mask((__v8du)(A), (__v8hf)(W),        \
                                              (__mmask8)(U), (int)(R)))

#define _mm512_maskz_cvt_roundepu64_ph(U, A, R)                                \
  ((__m128h)__builtin_ia32_vcvtuqq2ph512_mask(                                 \
      (__v8du)(A), (__v8hf)_mm_setzero_ph(), (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS512
_mm512_cvtepu64_ph(__m512i __A) {
  return (__m128h)__builtin_ia32_vcvtuqq2ph512_mask(
      (__v8du)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepu64_ph(__m128h __W, __mmask8 __U, __m512i __A) {
  return (__m128h)__builtin_ia32_vcvtuqq2ph512_mask(
      (__v8du)__A, (__v8hf)__W, (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtepu64_ph(__mmask8 __U, __m512i __A) {
  return (__m128h)__builtin_ia32_vcvtuqq2ph512_mask(
      (__v8du)__A, (__v8hf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvt_roundph_epu64(A, R)                                         \
  ((__m512i)__builtin_ia32_vcvtph2uqq512_mask(                                 \
      (__v8hf)(A), (__v8du)_mm512_undefined_epi32(), (__mmask8)(-1),           \
      (int)(R)))

#define _mm512_mask_cvt_roundph_epu64(W, U, A, R)                              \
  ((__m512i)__builtin_ia32_vcvtph2uqq512_mask((__v8hf)(A), (__v8du)(W),        \
                                              (__mmask8)(U), (int)(R)))

#define _mm512_maskz_cvt_roundph_epu64(U, A, R)                                \
  ((__m512i)__builtin_ia32_vcvtph2uqq512_mask(                                 \
      (__v8hf)(A), (__v8du)_mm512_setzero_epi32(), (__mmask8)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvtph_epu64(__m128h __A) {
  return (__m512i)__builtin_ia32_vcvtph2uqq512_mask(
      (__v8hf)__A, (__v8du)_mm512_setzero_epi32(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtph_epu64(__m512i __W, __mmask8 __U, __m128h __A) {
  return (__m512i)__builtin_ia32_vcvtph2uqq512_mask(
      (__v8hf)__A, (__v8du)__W, (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtph_epu64(__mmask8 __U, __m128h __A) {
  return (__m512i)__builtin_ia32_vcvtph2uqq512_mask(
      (__v8hf)__A, (__v8du)_mm512_setzero_epi32(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvtt_roundph_epi64(A, R)                                        \
  ((__m512i)__builtin_ia32_vcvttph2qq512_mask(                                 \
      (__v8hf)(A), (__v8di)_mm512_undefined_epi32(), (__mmask8)(-1),           \
      (int)(R)))

#define _mm512_mask_cvtt_roundph_epi64(W, U, A, R)                             \
  ((__m512i)__builtin_ia32_vcvttph2qq512_mask((__v8hf)(A), (__v8di)(W),        \
                                              (__mmask8)(U), (int)(R)))

#define _mm512_maskz_cvtt_roundph_epi64(U, A, R)                               \
  ((__m512i)__builtin_ia32_vcvttph2qq512_mask(                                 \
      (__v8hf)(A), (__v8di)_mm512_setzero_epi32(), (__mmask8)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvttph_epi64(__m128h __A) {
  return (__m512i)__builtin_ia32_vcvttph2qq512_mask(
      (__v8hf)__A, (__v8di)_mm512_setzero_epi32(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvttph_epi64(__m512i __W, __mmask8 __U, __m128h __A) {
  return (__m512i)__builtin_ia32_vcvttph2qq512_mask(
      (__v8hf)__A, (__v8di)__W, (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvttph_epi64(__mmask8 __U, __m128h __A) {
  return (__m512i)__builtin_ia32_vcvttph2qq512_mask(
      (__v8hf)__A, (__v8di)_mm512_setzero_epi32(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvtt_roundph_epu64(A, R)                                        \
  ((__m512i)__builtin_ia32_vcvttph2uqq512_mask(                                \
      (__v8hf)(A), (__v8du)_mm512_undefined_epi32(), (__mmask8)(-1),           \
      (int)(R)))

#define _mm512_mask_cvtt_roundph_epu64(W, U, A, R)                             \
  ((__m512i)__builtin_ia32_vcvttph2uqq512_mask((__v8hf)(A), (__v8du)(W),       \
                                               (__mmask8)(U), (int)(R)))

#define _mm512_maskz_cvtt_roundph_epu64(U, A, R)                               \
  ((__m512i)__builtin_ia32_vcvttph2uqq512_mask(                                \
      (__v8hf)(A), (__v8du)_mm512_setzero_epi32(), (__mmask8)(U), (int)(R)))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvttph_epu64(__m128h __A) {
  return (__m512i)__builtin_ia32_vcvttph2uqq512_mask(
      (__v8hf)__A, (__v8du)_mm512_setzero_epi32(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvttph_epu64(__m512i __W, __mmask8 __U, __m128h __A) {
  return (__m512i)__builtin_ia32_vcvttph2uqq512_mask(
      (__v8hf)__A, (__v8du)__W, (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvttph_epu64(__mmask8 __U, __m128h __A) {
  return (__m512i)__builtin_ia32_vcvttph2uqq512_mask(
      (__v8hf)__A, (__v8du)_mm512_setzero_epi32(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_cvt_roundsh_i32(A, R)                                              \
  ((int)__builtin_ia32_vcvtsh2si32((__v8hf)(A), (int)(R)))

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_cvtsh_i32(__m128h __A) {
  return (int)__builtin_ia32_vcvtsh2si32((__v8hf)__A, _MM_FROUND_CUR_DIRECTION);
}

#define _mm_cvt_roundsh_u32(A, R)                                              \
  ((unsigned int)__builtin_ia32_vcvtsh2usi32((__v8hf)(A), (int)(R)))

static __inline__ unsigned int __DEFAULT_FN_ATTRS128
_mm_cvtsh_u32(__m128h __A) {
  return (unsigned int)__builtin_ia32_vcvtsh2usi32((__v8hf)__A,
                                                   _MM_FROUND_CUR_DIRECTION);
}

#ifdef __x86_64__
#define _mm_cvt_roundsh_i64(A, R)                                              \
  ((long long)__builtin_ia32_vcvtsh2si64((__v8hf)(A), (int)(R)))

static __inline__ long long __DEFAULT_FN_ATTRS128 _mm_cvtsh_i64(__m128h __A) {
  return (long long)__builtin_ia32_vcvtsh2si64((__v8hf)__A,
                                               _MM_FROUND_CUR_DIRECTION);
}

#define _mm_cvt_roundsh_u64(A, R)                                              \
  ((unsigned long long)__builtin_ia32_vcvtsh2usi64((__v8hf)(A), (int)(R)))

static __inline__ unsigned long long __DEFAULT_FN_ATTRS128
_mm_cvtsh_u64(__m128h __A) {
  return (unsigned long long)__builtin_ia32_vcvtsh2usi64(
      (__v8hf)__A, _MM_FROUND_CUR_DIRECTION);
}
#endif // __x86_64__

#define _mm_cvt_roundu32_sh(A, B, R)                                           \
  ((__m128h)__builtin_ia32_vcvtusi2sh((__v8hf)(A), (unsigned int)(B), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_cvtu32_sh(__m128h __A, unsigned int __B) {
  __A[0] = __B;
  return __A;
}

#ifdef __x86_64__
#define _mm_cvt_roundu64_sh(A, B, R)                                           \
  ((__m128h)__builtin_ia32_vcvtusi642sh((__v8hf)(A), (unsigned long long)(B),  \
                                        (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_cvtu64_sh(__m128h __A, unsigned long long __B) {
  __A[0] = __B;
  return __A;
}
#endif

#define _mm_cvt_roundi32_sh(A, B, R)                                           \
  ((__m128h)__builtin_ia32_vcvtsi2sh((__v8hf)(A), (int)(B), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvti32_sh(__m128h __A,
                                                              int __B) {
  __A[0] = __B;
  return __A;
}

#ifdef __x86_64__
#define _mm_cvt_roundi64_sh(A, B, R)                                           \
  ((__m128h)__builtin_ia32_vcvtsi642sh((__v8hf)(A), (long long)(B), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_cvti64_sh(__m128h __A,
                                                              long long __B) {
  __A[0] = __B;
  return __A;
}
#endif

#define _mm_cvtt_roundsh_i32(A, R)                                             \
  ((int)__builtin_ia32_vcvttsh2si32((__v8hf)(A), (int)(R)))

static __inline__ int __DEFAULT_FN_ATTRS128 _mm_cvttsh_i32(__m128h __A) {
  return (int)__builtin_ia32_vcvttsh2si32((__v8hf)__A,
                                          _MM_FROUND_CUR_DIRECTION);
}

#ifdef __x86_64__
#define _mm_cvtt_roundsh_i64(A, R)                                             \
  ((long long)__builtin_ia32_vcvttsh2si64((__v8hf)(A), (int)(R)))

static __inline__ long long __DEFAULT_FN_ATTRS128 _mm_cvttsh_i64(__m128h __A) {
  return (long long)__builtin_ia32_vcvttsh2si64((__v8hf)__A,
                                                _MM_FROUND_CUR_DIRECTION);
}
#endif

#define _mm_cvtt_roundsh_u32(A, R)                                             \
  ((unsigned int)__builtin_ia32_vcvttsh2usi32((__v8hf)(A), (int)(R)))

static __inline__ unsigned int __DEFAULT_FN_ATTRS128
_mm_cvttsh_u32(__m128h __A) {
  return (unsigned int)__builtin_ia32_vcvttsh2usi32((__v8hf)__A,
                                                    _MM_FROUND_CUR_DIRECTION);
}

#ifdef __x86_64__
#define _mm_cvtt_roundsh_u64(A, R)                                             \
  ((unsigned long long)__builtin_ia32_vcvttsh2usi64((__v8hf)(A), (int)(R)))

static __inline__ unsigned long long __DEFAULT_FN_ATTRS128
_mm_cvttsh_u64(__m128h __A) {
  return (unsigned long long)__builtin_ia32_vcvttsh2usi64(
      (__v8hf)__A, _MM_FROUND_CUR_DIRECTION);
}
#endif

#define _mm512_cvtx_roundph_ps(A, R)                                           \
  ((__m512)__builtin_ia32_vcvtph2psx512_mask((__v16hf)(A),                     \
                                             (__v16sf)_mm512_undefined_ps(),   \
                                             (__mmask16)(-1), (int)(R)))

#define _mm512_mask_cvtx_roundph_ps(W, U, A, R)                                \
  ((__m512)__builtin_ia32_vcvtph2psx512_mask((__v16hf)(A), (__v16sf)(W),       \
                                             (__mmask16)(U), (int)(R)))

#define _mm512_maskz_cvtx_roundph_ps(U, A, R)                                  \
  ((__m512)__builtin_ia32_vcvtph2psx512_mask(                                  \
      (__v16hf)(A), (__v16sf)_mm512_setzero_ps(), (__mmask16)(U), (int)(R)))

static __inline__ __m512 __DEFAULT_FN_ATTRS512 _mm512_cvtxph_ps(__m256h __A) {
  return (__m512)__builtin_ia32_vcvtph2psx512_mask(
      (__v16hf)__A, (__v16sf)_mm512_setzero_ps(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512 __DEFAULT_FN_ATTRS512
_mm512_mask_cvtxph_ps(__m512 __W, __mmask16 __U, __m256h __A) {
  return (__m512)__builtin_ia32_vcvtph2psx512_mask(
      (__v16hf)__A, (__v16sf)__W, (__mmask16)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512 __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtxph_ps(__mmask16 __U, __m256h __A) {
  return (__m512)__builtin_ia32_vcvtph2psx512_mask(
      (__v16hf)__A, (__v16sf)_mm512_setzero_ps(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_cvtx_roundps_ph(A, R)                                           \
  ((__m256h)__builtin_ia32_vcvtps2phx512_mask((__v16sf)(A),                    \
                                              (__v16hf)_mm256_undefined_ph(),  \
                                              (__mmask16)(-1), (int)(R)))

#define _mm512_mask_cvtx_roundps_ph(W, U, A, R)                                \
  ((__m256h)__builtin_ia32_vcvtps2phx512_mask((__v16sf)(A), (__v16hf)(W),      \
                                              (__mmask16)(U), (int)(R)))

#define _mm512_maskz_cvtx_roundps_ph(U, A, R)                                  \
  ((__m256h)__builtin_ia32_vcvtps2phx512_mask(                                 \
      (__v16sf)(A), (__v16hf)_mm256_setzero_ph(), (__mmask16)(U), (int)(R)))

static __inline__ __m256h __DEFAULT_FN_ATTRS512 _mm512_cvtxps_ph(__m512 __A) {
  return (__m256h)__builtin_ia32_vcvtps2phx512_mask(
      (__v16sf)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS512
_mm512_mask_cvtxps_ph(__m256h __W, __mmask16 __U, __m512 __A) {
  return (__m256h)__builtin_ia32_vcvtps2phx512_mask(
      (__v16sf)__A, (__v16hf)__W, (__mmask16)__U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m256h __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtxps_ph(__mmask16 __U, __m512 __A) {
  return (__m256h)__builtin_ia32_vcvtps2phx512_mask(
      (__v16sf)__A, (__v16hf)_mm256_setzero_ph(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_fmadd_round_ph(A, B, C, R)                                      \
  ((__m512h)__builtin_ia32_vfmaddph512_mask(                                   \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)-1, (int)(R)))

#define _mm512_mask_fmadd_round_ph(A, U, B, C, R)                              \
  ((__m512h)__builtin_ia32_vfmaddph512_mask(                                   \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)(U), (int)(R)))

#define _mm512_mask3_fmadd_round_ph(A, B, C, U, R)                             \
  ((__m512h)__builtin_ia32_vfmaddph512_mask3(                                  \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)(U), (int)(R)))

#define _mm512_maskz_fmadd_round_ph(U, A, B, C, R)                             \
  ((__m512h)__builtin_ia32_vfmaddph512_maskz(                                  \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)(U), (int)(R)))

#define _mm512_fmsub_round_ph(A, B, C, R)                                      \
  ((__m512h)__builtin_ia32_vfmaddph512_mask(                                   \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), -(__v32hf)(__m512h)(C),    \
      (__mmask32)-1, (int)(R)))

#define _mm512_mask_fmsub_round_ph(A, U, B, C, R)                              \
  ((__m512h)__builtin_ia32_vfmaddph512_mask(                                   \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), -(__v32hf)(__m512h)(C),    \
      (__mmask32)(U), (int)(R)))

#define _mm512_maskz_fmsub_round_ph(U, A, B, C, R)                             \
  ((__m512h)__builtin_ia32_vfmaddph512_maskz(                                  \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), -(__v32hf)(__m512h)(C),    \
      (__mmask32)(U), (int)(R)))

#define _mm512_fnmadd_round_ph(A, B, C, R)                                     \
  ((__m512h)__builtin_ia32_vfmaddph512_mask(                                   \
      (__v32hf)(__m512h)(A), -(__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),    \
      (__mmask32)-1, (int)(R)))

#define _mm512_mask3_fnmadd_round_ph(A, B, C, U, R)                            \
  ((__m512h)__builtin_ia32_vfmaddph512_mask3(                                  \
      -(__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),    \
      (__mmask32)(U), (int)(R)))

#define _mm512_maskz_fnmadd_round_ph(U, A, B, C, R)                            \
  ((__m512h)__builtin_ia32_vfmaddph512_maskz(                                  \
      -(__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),    \
      (__mmask32)(U), (int)(R)))

#define _mm512_fnmsub_round_ph(A, B, C, R)                                     \
  ((__m512h)__builtin_ia32_vfmaddph512_mask(                                   \
      (__v32hf)(__m512h)(A), -(__v32hf)(__m512h)(B), -(__v32hf)(__m512h)(C),   \
      (__mmask32)-1, (int)(R)))

#define _mm512_maskz_fnmsub_round_ph(U, A, B, C, R)                            \
  ((__m512h)__builtin_ia32_vfmaddph512_maskz(                                  \
      -(__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), -(__v32hf)(__m512h)(C),   \
      (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_fmadd_ph(__m512h __A,
                                                                __m512h __B,
                                                                __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask((__v32hf)__A, (__v32hf)__B,
                                                  (__v32hf)__C, (__mmask32)-1,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fmadd_ph(__m512h __A, __mmask32 __U, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask((__v32hf)__A, (__v32hf)__B,
                                                  (__v32hf)__C, (__mmask32)__U,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask3_fmadd_ph(__m512h __A, __m512h __B, __m512h __C, __mmask32 __U) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask3((__v32hf)__A, (__v32hf)__B,
                                                   (__v32hf)__C, (__mmask32)__U,
                                                   _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fmadd_ph(__mmask32 __U, __m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_maskz((__v32hf)__A, (__v32hf)__B,
                                                   (__v32hf)__C, (__mmask32)__U,
                                                   _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_fmsub_ph(__m512h __A,
                                                                __m512h __B,
                                                                __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask((__v32hf)__A, (__v32hf)__B,
                                                  -(__v32hf)__C, (__mmask32)-1,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fmsub_ph(__m512h __A, __mmask32 __U, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask((__v32hf)__A, (__v32hf)__B,
                                                  -(__v32hf)__C, (__mmask32)__U,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fmsub_ph(__mmask32 __U, __m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_maskz(
      (__v32hf)__A, (__v32hf)__B, -(__v32hf)__C, (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_fnmadd_ph(__m512h __A,
                                                                 __m512h __B,
                                                                 __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask((__v32hf)__A, -(__v32hf)__B,
                                                  (__v32hf)__C, (__mmask32)-1,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask3_fnmadd_ph(__m512h __A, __m512h __B, __m512h __C, __mmask32 __U) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask3(-(__v32hf)__A, (__v32hf)__B,
                                                   (__v32hf)__C, (__mmask32)__U,
                                                   _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fnmadd_ph(__mmask32 __U, __m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_maskz(-(__v32hf)__A, (__v32hf)__B,
                                                   (__v32hf)__C, (__mmask32)__U,
                                                   _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_fnmsub_ph(__m512h __A,
                                                                 __m512h __B,
                                                                 __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask((__v32hf)__A, -(__v32hf)__B,
                                                  -(__v32hf)__C, (__mmask32)-1,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fnmsub_ph(__mmask32 __U, __m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_maskz(
      -(__v32hf)__A, (__v32hf)__B, -(__v32hf)__C, (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_fmaddsub_round_ph(A, B, C, R)                                   \
  ((__m512h)__builtin_ia32_vfmaddsubph512_mask(                                \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)-1, (int)(R)))

#define _mm512_mask_fmaddsub_round_ph(A, U, B, C, R)                           \
  ((__m512h)__builtin_ia32_vfmaddsubph512_mask(                                \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)(U), (int)(R)))

#define _mm512_mask3_fmaddsub_round_ph(A, B, C, U, R)                          \
  ((__m512h)__builtin_ia32_vfmaddsubph512_mask3(                               \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)(U), (int)(R)))

#define _mm512_maskz_fmaddsub_round_ph(U, A, B, C, R)                          \
  ((__m512h)__builtin_ia32_vfmaddsubph512_maskz(                               \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)(U), (int)(R)))

#define _mm512_fmsubadd_round_ph(A, B, C, R)                                   \
  ((__m512h)__builtin_ia32_vfmaddsubph512_mask(                                \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), -(__v32hf)(__m512h)(C),    \
      (__mmask32)-1, (int)(R)))

#define _mm512_mask_fmsubadd_round_ph(A, U, B, C, R)                           \
  ((__m512h)__builtin_ia32_vfmaddsubph512_mask(                                \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), -(__v32hf)(__m512h)(C),    \
      (__mmask32)(U), (int)(R)))

#define _mm512_maskz_fmsubadd_round_ph(U, A, B, C, R)                          \
  ((__m512h)__builtin_ia32_vfmaddsubph512_maskz(                               \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), -(__v32hf)(__m512h)(C),    \
      (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_fmaddsub_ph(__m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddsubph512_mask(
      (__v32hf)__A, (__v32hf)__B, (__v32hf)__C, (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fmaddsub_ph(__m512h __A, __mmask32 __U, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddsubph512_mask(
      (__v32hf)__A, (__v32hf)__B, (__v32hf)__C, (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask3_fmaddsub_ph(__m512h __A, __m512h __B, __m512h __C, __mmask32 __U) {
  return (__m512h)__builtin_ia32_vfmaddsubph512_mask3(
      (__v32hf)__A, (__v32hf)__B, (__v32hf)__C, (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fmaddsub_ph(__mmask32 __U, __m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddsubph512_maskz(
      (__v32hf)__A, (__v32hf)__B, (__v32hf)__C, (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_fmsubadd_ph(__m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddsubph512_mask(
      (__v32hf)__A, (__v32hf)__B, -(__v32hf)__C, (__mmask32)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fmsubadd_ph(__m512h __A, __mmask32 __U, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddsubph512_mask(
      (__v32hf)__A, (__v32hf)__B, -(__v32hf)__C, (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fmsubadd_ph(__mmask32 __U, __m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddsubph512_maskz(
      (__v32hf)__A, (__v32hf)__B, -(__v32hf)__C, (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_mask3_fmsub_round_ph(A, B, C, U, R)                             \
  ((__m512h)__builtin_ia32_vfmsubph512_mask3(                                  \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask3_fmsub_ph(__m512h __A, __m512h __B, __m512h __C, __mmask32 __U) {
  return (__m512h)__builtin_ia32_vfmsubph512_mask3((__v32hf)__A, (__v32hf)__B,
                                                   (__v32hf)__C, (__mmask32)__U,
                                                   _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_mask3_fmsubadd_round_ph(A, B, C, U, R)                          \
  ((__m512h)__builtin_ia32_vfmsubaddph512_mask3(                               \
      (__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),     \
      (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask3_fmsubadd_ph(__m512h __A, __m512h __B, __m512h __C, __mmask32 __U) {
  return (__m512h)__builtin_ia32_vfmsubaddph512_mask3(
      (__v32hf)__A, (__v32hf)__B, (__v32hf)__C, (__mmask32)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_mask_fnmadd_round_ph(A, U, B, C, R)                             \
  ((__m512h)__builtin_ia32_vfmaddph512_mask(                                   \
      (__v32hf)(__m512h)(A), -(__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),    \
      (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fnmadd_ph(__m512h __A, __mmask32 __U, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask((__v32hf)__A, -(__v32hf)__B,
                                                  (__v32hf)__C, (__mmask32)__U,
                                                  _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_mask_fnmsub_round_ph(A, U, B, C, R)                             \
  ((__m512h)__builtin_ia32_vfmaddph512_mask(                                   \
      (__v32hf)(__m512h)(A), -(__v32hf)(__m512h)(B), -(__v32hf)(__m512h)(C),   \
      (__mmask32)(U), (int)(R)))

#define _mm512_mask3_fnmsub_round_ph(A, B, C, U, R)                            \
  ((__m512h)__builtin_ia32_vfmsubph512_mask3(                                  \
      -(__v32hf)(__m512h)(A), (__v32hf)(__m512h)(B), (__v32hf)(__m512h)(C),    \
      (__mmask32)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fnmsub_ph(__m512h __A, __mmask32 __U, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddph512_mask((__v32hf)__A, -(__v32hf)__B,
                                                  -(__v32hf)__C, (__mmask32)__U,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask3_fnmsub_ph(__m512h __A, __m512h __B, __m512h __C, __mmask32 __U) {
  return (__m512h)__builtin_ia32_vfmsubph512_mask3(-(__v32hf)__A, (__v32hf)__B,
                                                   (__v32hf)__C, (__mmask32)__U,
                                                   _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmadd_sh(__m128h __W,
                                                             __m128h __A,
                                                             __m128h __B) {
  return __builtin_ia32_vfmaddsh3_mask((__v8hf)__W, (__v8hf)__A, (__v8hf)__B,
                                       (__mmask8)-1, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_fmadd_sh(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A,
                                                                  __m128h __B) {
  return __builtin_ia32_vfmaddsh3_mask((__v8hf)__W, (__v8hf)__A, (__v8hf)__B,
                                       (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

#define _mm_fmadd_round_sh(A, B, C, R)                                         \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask(                                     \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(C),        \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_fmadd_round_sh(W, U, A, B, R)                                 \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask(                                     \
      (__v8hf)(__m128h)(W), (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B),        \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmadd_sh(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return __builtin_ia32_vfmaddsh3_maskz((__v8hf)__A, (__v8hf)__B, (__v8hf)__C,
                                        (__mmask8)__U,
                                        _MM_FROUND_CUR_DIRECTION);
}

#define _mm_maskz_fmadd_round_sh(U, A, B, C, R)                                \
  ((__m128h)__builtin_ia32_vfmaddsh3_maskz(                                    \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), (__v8hf)(__m128h)(C),        \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fmadd_sh(__m128h __W, __m128h __X, __m128h __Y, __mmask8 __U) {
  return __builtin_ia32_vfmaddsh3_mask3((__v8hf)__W, (__v8hf)__X, (__v8hf)__Y,
                                        (__mmask8)__U,
                                        _MM_FROUND_CUR_DIRECTION);
}

#define _mm_mask3_fmadd_round_sh(W, X, Y, U, R)                                \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask3(                                    \
      (__v8hf)(__m128h)(W), (__v8hf)(__m128h)(X), (__v8hf)(__m128h)(Y),        \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmsub_sh(__m128h __W,
                                                             __m128h __A,
                                                             __m128h __B) {
  return (__m128h)__builtin_ia32_vfmaddsh3_mask((__v8hf)__W, (__v8hf)__A,
                                                -(__v8hf)__B, (__mmask8)-1,
                                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_fmsub_sh(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A,
                                                                  __m128h __B) {
  return (__m128h)__builtin_ia32_vfmaddsh3_mask((__v8hf)__W, (__v8hf)__A,
                                                -(__v8hf)__B, (__mmask8)__U,
                                                _MM_FROUND_CUR_DIRECTION);
}

#define _mm_fmsub_round_sh(A, B, C, R)                                         \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask(                                     \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), -(__v8hf)(__m128h)(C),       \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_fmsub_round_sh(W, U, A, B, R)                                 \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask(                                     \
      (__v8hf)(__m128h)(W), (__v8hf)(__m128h)(A), -(__v8hf)(__m128h)(B),       \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmsub_sh(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddsh3_maskz((__v8hf)__A, (__v8hf)__B,
                                                 -(__v8hf)__C, (__mmask8)__U,
                                                 _MM_FROUND_CUR_DIRECTION);
}

#define _mm_maskz_fmsub_round_sh(U, A, B, C, R)                                \
  ((__m128h)__builtin_ia32_vfmaddsh3_maskz(                                    \
      (__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B), -(__v8hf)(__m128h)(C),       \
      (__mmask8)(U), (int)R))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fmsub_sh(__m128h __W, __m128h __X, __m128h __Y, __mmask8 __U) {
  return __builtin_ia32_vfmsubsh3_mask3((__v8hf)__W, (__v8hf)__X, (__v8hf)__Y,
                                        (__mmask8)__U,
                                        _MM_FROUND_CUR_DIRECTION);
}

#define _mm_mask3_fmsub_round_sh(W, X, Y, U, R)                                \
  ((__m128h)__builtin_ia32_vfmsubsh3_mask3(                                    \
      (__v8hf)(__m128h)(W), (__v8hf)(__m128h)(X), (__v8hf)(__m128h)(Y),        \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fnmadd_sh(__m128h __W,
                                                              __m128h __A,
                                                              __m128h __B) {
  return __builtin_ia32_vfmaddsh3_mask((__v8hf)__W, -(__v8hf)__A, (__v8hf)__B,
                                       (__mmask8)-1, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fnmadd_sh(__m128h __W, __mmask8 __U, __m128h __A, __m128h __B) {
  return __builtin_ia32_vfmaddsh3_mask((__v8hf)__W, -(__v8hf)__A, (__v8hf)__B,
                                       (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

#define _mm_fnmadd_round_sh(A, B, C, R)                                        \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask(                                     \
      (__v8hf)(__m128h)(A), -(__v8hf)(__m128h)(B), (__v8hf)(__m128h)(C),       \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_fnmadd_round_sh(W, U, A, B, R)                                \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask(                                     \
      (__v8hf)(__m128h)(W), -(__v8hf)(__m128h)(A), (__v8hf)(__m128h)(B),       \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fnmadd_sh(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return __builtin_ia32_vfmaddsh3_maskz((__v8hf)__A, -(__v8hf)__B, (__v8hf)__C,
                                        (__mmask8)__U,
                                        _MM_FROUND_CUR_DIRECTION);
}

#define _mm_maskz_fnmadd_round_sh(U, A, B, C, R)                               \
  ((__m128h)__builtin_ia32_vfmaddsh3_maskz(                                    \
      (__v8hf)(__m128h)(A), -(__v8hf)(__m128h)(B), (__v8hf)(__m128h)(C),       \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fnmadd_sh(__m128h __W, __m128h __X, __m128h __Y, __mmask8 __U) {
  return __builtin_ia32_vfmaddsh3_mask3((__v8hf)__W, -(__v8hf)__X, (__v8hf)__Y,
                                        (__mmask8)__U,
                                        _MM_FROUND_CUR_DIRECTION);
}

#define _mm_mask3_fnmadd_round_sh(W, X, Y, U, R)                               \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask3(                                    \
      (__v8hf)(__m128h)(W), -(__v8hf)(__m128h)(X), (__v8hf)(__m128h)(Y),       \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fnmsub_sh(__m128h __W,
                                                              __m128h __A,
                                                              __m128h __B) {
  return __builtin_ia32_vfmaddsh3_mask((__v8hf)__W, -(__v8hf)__A, -(__v8hf)__B,
                                       (__mmask8)-1, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fnmsub_sh(__m128h __W, __mmask8 __U, __m128h __A, __m128h __B) {
  return __builtin_ia32_vfmaddsh3_mask((__v8hf)__W, -(__v8hf)__A, -(__v8hf)__B,
                                       (__mmask8)__U, _MM_FROUND_CUR_DIRECTION);
}

#define _mm_fnmsub_round_sh(A, B, C, R)                                        \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask(                                     \
      (__v8hf)(__m128h)(A), -(__v8hf)(__m128h)(B), -(__v8hf)(__m128h)(C),      \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_fnmsub_round_sh(W, U, A, B, R)                                \
  ((__m128h)__builtin_ia32_vfmaddsh3_mask(                                     \
      (__v8hf)(__m128h)(W), -(__v8hf)(__m128h)(A), -(__v8hf)(__m128h)(B),      \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fnmsub_sh(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return __builtin_ia32_vfmaddsh3_maskz((__v8hf)__A, -(__v8hf)__B, -(__v8hf)__C,
                                        (__mmask8)__U,
                                        _MM_FROUND_CUR_DIRECTION);
}

#define _mm_maskz_fnmsub_round_sh(U, A, B, C, R)                               \
  ((__m128h)__builtin_ia32_vfmaddsh3_maskz(                                    \
      (__v8hf)(__m128h)(A), -(__v8hf)(__m128h)(B), -(__v8hf)(__m128h)(C),      \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fnmsub_sh(__m128h __W, __m128h __X, __m128h __Y, __mmask8 __U) {
  return __builtin_ia32_vfmsubsh3_mask3((__v8hf)__W, -(__v8hf)__X, (__v8hf)__Y,
                                        (__mmask8)__U,
                                        _MM_FROUND_CUR_DIRECTION);
}

#define _mm_mask3_fnmsub_round_sh(W, X, Y, U, R)                               \
  ((__m128h)__builtin_ia32_vfmsubsh3_mask3(                                    \
      (__v8hf)(__m128h)(W), -(__v8hf)(__m128h)(X), (__v8hf)(__m128h)(Y),       \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fcmadd_sch(__m128h __A,
                                                               __m128h __B,
                                                               __m128h __C) {
  return (__m128h)__builtin_ia32_vfcmaddcsh_mask((__v4sf)__A, (__v4sf)__B,
                                                 (__v4sf)__C, (__mmask8)-1,
                                                 _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fcmadd_sch(__m128h __A, __mmask8 __U, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_vfcmaddcsh_round_mask(
      (__v4sf)__A, (__v4sf)(__B), (__v4sf)(__C), __U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fcmadd_sch(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_vfcmaddcsh_maskz((__v4sf)__A, (__v4sf)__B,
                                                  (__v4sf)__C, (__mmask8)__U,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fcmadd_sch(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_vfcmaddcsh_round_mask3(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)__C, __U, _MM_FROUND_CUR_DIRECTION);
}

#define _mm_fcmadd_round_sch(A, B, C, R)                                       \
  ((__m128h)__builtin_ia32_vfcmaddcsh_mask(                                    \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(C),        \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_fcmadd_round_sch(A, U, B, C, R)                               \
  ((__m128h)__builtin_ia32_vfcmaddcsh_round_mask(                              \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(C),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_fcmadd_round_sch(U, A, B, C, R)                              \
  ((__m128h)__builtin_ia32_vfcmaddcsh_maskz(                                   \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(C),        \
      (__mmask8)(U), (int)(R)))

#define _mm_mask3_fcmadd_round_sch(A, B, C, U, R)                              \
  ((__m128h)__builtin_ia32_vfcmaddcsh_round_mask3(                             \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(C),        \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmadd_sch(__m128h __A,
                                                              __m128h __B,
                                                              __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddcsh_mask((__v4sf)__A, (__v4sf)__B,
                                                (__v4sf)__C, (__mmask8)-1,
                                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fmadd_sch(__m128h __A, __mmask8 __U, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddcsh_round_mask(
      (__v4sf)__A, (__v4sf)(__B), (__v4sf)(__C), __U, _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmadd_sch(__mmask8 __U, __m128h __A, __m128h __B, __m128h __C) {
  return (__m128h)__builtin_ia32_vfmaddcsh_maskz((__v4sf)__A, (__v4sf)__B,
                                                 (__v4sf)__C, (__mmask8)__U,
                                                 _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask3_fmadd_sch(__m128h __A, __m128h __B, __m128h __C, __mmask8 __U) {
  return (__m128h)__builtin_ia32_vfmaddcsh_round_mask3(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)__C, __U, _MM_FROUND_CUR_DIRECTION);
}

#define _mm_fmadd_round_sch(A, B, C, R)                                        \
  ((__m128h)__builtin_ia32_vfmaddcsh_mask(                                     \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(C),        \
      (__mmask8)-1, (int)(R)))

#define _mm_mask_fmadd_round_sch(A, U, B, C, R)                                \
  ((__m128h)__builtin_ia32_vfmaddcsh_round_mask(                               \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(C),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_fmadd_round_sch(U, A, B, C, R)                               \
  ((__m128h)__builtin_ia32_vfmaddcsh_maskz(                                    \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(C),        \
      (__mmask8)(U), (int)(R)))

#define _mm_mask3_fmadd_round_sch(A, B, C, U, R)                               \
  ((__m128h)__builtin_ia32_vfmaddcsh_round_mask3(                              \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(C),        \
      (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fcmul_sch(__m128h __A,
                                                              __m128h __B) {
  return (__m128h)__builtin_ia32_vfcmulcsh_mask(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)_mm_undefined_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_mask_fcmul_sch(__m128h __W, __mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_vfcmulcsh_mask((__v4sf)__A, (__v4sf)__B,
                                                (__v4sf)__W, (__mmask8)__U,
                                                _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fcmul_sch(__mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_vfcmulcsh_mask(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_fcmul_round_sch(A, B, R)                                           \
  ((__m128h)__builtin_ia32_vfcmulcsh_mask(                                     \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B),                              \
      (__v4sf)(__m128h)_mm_undefined_ph(), (__mmask8)-1, (int)(R)))

#define _mm_mask_fcmul_round_sch(W, U, A, B, R)                                \
  ((__m128h)__builtin_ia32_vfcmulcsh_mask(                                     \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_fcmul_round_sch(U, A, B, R)                                  \
  ((__m128h)__builtin_ia32_vfcmulcsh_mask(                                     \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B),                              \
      (__v4sf)(__m128h)_mm_setzero_ph(), (__mmask8)(U), (int)(R)))

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_fmul_sch(__m128h __A,
                                                             __m128h __B) {
  return (__m128h)__builtin_ia32_vfmulcsh_mask(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)_mm_undefined_ph(), (__mmask8)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128 _mm_mask_fmul_sch(__m128h __W,
                                                                  __mmask8 __U,
                                                                  __m128h __A,
                                                                  __m128h __B) {
  return (__m128h)__builtin_ia32_vfmulcsh_mask((__v4sf)__A, (__v4sf)__B,
                                               (__v4sf)__W, (__mmask8)__U,
                                               _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m128h __DEFAULT_FN_ATTRS128
_mm_maskz_fmul_sch(__mmask8 __U, __m128h __A, __m128h __B) {
  return (__m128h)__builtin_ia32_vfmulcsh_mask(
      (__v4sf)__A, (__v4sf)__B, (__v4sf)_mm_setzero_ph(), (__mmask8)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm_fmul_round_sch(A, B, R)                                            \
  ((__m128h)__builtin_ia32_vfmulcsh_mask(                                      \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B),                              \
      (__v4sf)(__m128h)_mm_undefined_ph(), (__mmask8)-1, (int)(R)))

#define _mm_mask_fmul_round_sch(W, U, A, B, R)                                 \
  ((__m128h)__builtin_ia32_vfmulcsh_mask(                                      \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B), (__v4sf)(__m128h)(W),        \
      (__mmask8)(U), (int)(R)))

#define _mm_maskz_fmul_round_sch(U, A, B, R)                                   \
  ((__m128h)__builtin_ia32_vfmulcsh_mask(                                      \
      (__v4sf)(__m128h)(A), (__v4sf)(__m128h)(B),                              \
      (__v4sf)(__m128h)_mm_setzero_ph(), (__mmask8)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_fcmul_pch(__m512h __A,
                                                                 __m512h __B) {
  return (__m512h)__builtin_ia32_vfcmulcph512_mask(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)_mm512_undefined_ph(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fcmul_pch(__m512h __W, __mmask16 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_vfcmulcph512_mask((__v16sf)__A, (__v16sf)__B,
                                                   (__v16sf)__W, (__mmask16)__U,
                                                   _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fcmul_pch(__mmask16 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_vfcmulcph512_mask(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)_mm512_setzero_ph(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_fcmul_round_pch(A, B, R)                                        \
  ((__m512h)__builtin_ia32_vfcmulcph512_mask(                                  \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B),                            \
      (__v16sf)(__m512h)_mm512_undefined_ph(), (__mmask16)-1, (int)(R)))

#define _mm512_mask_fcmul_round_pch(W, U, A, B, R)                             \
  ((__m512h)__builtin_ia32_vfcmulcph512_mask(                                  \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(W),     \
      (__mmask16)(U), (int)(R)))

#define _mm512_maskz_fcmul_round_pch(U, A, B, R)                               \
  ((__m512h)__builtin_ia32_vfcmulcph512_mask(                                  \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B),                            \
      (__v16sf)(__m512h)_mm512_setzero_ph(), (__mmask16)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_fmul_pch(__m512h __A,
                                                                __m512h __B) {
  return (__m512h)__builtin_ia32_vfmulcph512_mask(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)_mm512_undefined_ph(), (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fmul_pch(__m512h __W, __mmask16 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_vfmulcph512_mask((__v16sf)__A, (__v16sf)__B,
                                                  (__v16sf)__W, (__mmask16)__U,
                                                  _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fmul_pch(__mmask16 __U, __m512h __A, __m512h __B) {
  return (__m512h)__builtin_ia32_vfmulcph512_mask(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)_mm512_setzero_ph(), (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_fmul_round_pch(A, B, R)                                         \
  ((__m512h)__builtin_ia32_vfmulcph512_mask(                                   \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B),                            \
      (__v16sf)(__m512h)_mm512_undefined_ph(), (__mmask16)-1, (int)(R)))

#define _mm512_mask_fmul_round_pch(W, U, A, B, R)                              \
  ((__m512h)__builtin_ia32_vfmulcph512_mask(                                   \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(W),     \
      (__mmask16)(U), (int)(R)))

#define _mm512_maskz_fmul_round_pch(U, A, B, R)                                \
  ((__m512h)__builtin_ia32_vfmulcph512_mask(                                   \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B),                            \
      (__v16sf)(__m512h)_mm512_setzero_ph(), (__mmask16)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_fcmadd_pch(__m512h __A,
                                                                  __m512h __B,
                                                                  __m512h __C) {
  return (__m512h)__builtin_ia32_vfcmaddcph512_mask3(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)__C, (__mmask16)-1,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fcmadd_pch(__m512h __A, __mmask16 __U, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfcmaddcph512_mask(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)__C, (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask3_fcmadd_pch(__m512h __A, __m512h __B, __m512h __C, __mmask16 __U) {
  return (__m512h)__builtin_ia32_vfcmaddcph512_mask3(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)__C, (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fcmadd_pch(__mmask16 __U, __m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfcmaddcph512_maskz(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)__C, (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_fcmadd_round_pch(A, B, C, R)                                    \
  ((__m512h)__builtin_ia32_vfcmaddcph512_mask3(                                \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(C),     \
      (__mmask16)-1, (int)(R)))

#define _mm512_mask_fcmadd_round_pch(A, U, B, C, R)                            \
  ((__m512h)__builtin_ia32_vfcmaddcph512_mask(                                 \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(C),     \
      (__mmask16)(U), (int)(R)))

#define _mm512_mask3_fcmadd_round_pch(A, B, C, U, R)                           \
  ((__m512h)__builtin_ia32_vfcmaddcph512_mask3(                                \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(C),     \
      (__mmask16)(U), (int)(R)))

#define _mm512_maskz_fcmadd_round_pch(U, A, B, C, R)                           \
  ((__m512h)__builtin_ia32_vfcmaddcph512_maskz(                                \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(C),     \
      (__mmask16)(U), (int)(R)))

static __inline__ __m512h __DEFAULT_FN_ATTRS512 _mm512_fmadd_pch(__m512h __A,
                                                                 __m512h __B,
                                                                 __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddcph512_mask3((__v16sf)__A, (__v16sf)__B,
                                                    (__v16sf)__C, (__mmask16)-1,
                                                    _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_fmadd_pch(__m512h __A, __mmask16 __U, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddcph512_mask((__v16sf)__A, (__v16sf)__B,
                                                   (__v16sf)__C, (__mmask16)__U,
                                                   _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask3_fmadd_pch(__m512h __A, __m512h __B, __m512h __C, __mmask16 __U) {
  return (__m512h)__builtin_ia32_vfmaddcph512_mask3(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)__C, (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_maskz_fmadd_pch(__mmask16 __U, __m512h __A, __m512h __B, __m512h __C) {
  return (__m512h)__builtin_ia32_vfmaddcph512_maskz(
      (__v16sf)__A, (__v16sf)__B, (__v16sf)__C, (__mmask16)__U,
      _MM_FROUND_CUR_DIRECTION);
}

#define _mm512_fmadd_round_pch(A, B, C, R)                                     \
  ((__m512h)__builtin_ia32_vfmaddcph512_mask3(                                 \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(C),     \
      (__mmask16)-1, (int)(R)))

#define _mm512_mask_fmadd_round_pch(A, U, B, C, R)                             \
  ((__m512h)__builtin_ia32_vfmaddcph512_mask(                                  \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(C),     \
      (__mmask16)(U), (int)(R)))

#define _mm512_mask3_fmadd_round_pch(A, B, C, U, R)                            \
  ((__m512h)__builtin_ia32_vfmaddcph512_mask3(                                 \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(C),     \
      (__mmask16)(U), (int)(R)))

#define _mm512_maskz_fmadd_round_pch(U, A, B, C, R)                            \
  ((__m512h)__builtin_ia32_vfmaddcph512_maskz(                                 \
      (__v16sf)(__m512h)(A), (__v16sf)(__m512h)(B), (__v16sf)(__m512h)(C),     \
      (__mmask16)(U), (int)(R)))

static __inline__ _Float16 __DEFAULT_FN_ATTRS512
_mm512_reduce_add_ph(__m512h __W) {
  return __builtin_ia32_reduce_fadd_ph512(-0.0f16, __W);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS512
_mm512_reduce_mul_ph(__m512h __W) {
  return __builtin_ia32_reduce_fmul_ph512(1.0f16, __W);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS512
_mm512_reduce_max_ph(__m512h __V) {
  return __builtin_ia32_reduce_fmax_ph512(__V);
}

static __inline__ _Float16 __DEFAULT_FN_ATTRS512
_mm512_reduce_min_ph(__m512h __V) {
  return __builtin_ia32_reduce_fmin_ph512(__V);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_mask_blend_ph(__mmask32 __U, __m512h __A, __m512h __W) {
  return (__m512h)__builtin_ia32_selectph_512((__mmask32)__U, (__v32hf)__W,
                                              (__v32hf)__A);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_permutex2var_ph(__m512h __A, __m512i __I, __m512h __B) {
  return (__m512h)__builtin_ia32_vpermi2varhi512((__v32hi)__A, (__v32hi)__I,
                                                 (__v32hi)__B);
}

static __inline__ __m512h __DEFAULT_FN_ATTRS512
_mm512_permutexvar_ph(__m512i __A, __m512h __B) {
  return (__m512h)__builtin_ia32_permvarhi512((__v32hi)__B, (__v32hi)__A);
}

// intrinsics below are alias for f*mul_*ch
#define _mm512_mul_pch(A, B) _mm512_fmul_pch(A, B)
#define _mm512_mask_mul_pch(W, U, A, B) _mm512_mask_fmul_pch(W, U, A, B)
#define _mm512_maskz_mul_pch(U, A, B) _mm512_maskz_fmul_pch(U, A, B)
#define _mm512_mul_round_pch(A, B, R) _mm512_fmul_round_pch(A, B, R)
#define _mm512_mask_mul_round_pch(W, U, A, B, R)                               \
  _mm512_mask_fmul_round_pch(W, U, A, B, R)
#define _mm512_maskz_mul_round_pch(U, A, B, R)                                 \
  _mm512_maskz_fmul_round_pch(U, A, B, R)

#define _mm512_cmul_pch(A, B) _mm512_fcmul_pch(A, B)
#define _mm512_mask_cmul_pch(W, U, A, B) _mm512_mask_fcmul_pch(W, U, A, B)
#define _mm512_maskz_cmul_pch(U, A, B) _mm512_maskz_fcmul_pch(U, A, B)
#define _mm512_cmul_round_pch(A, B, R) _mm512_fcmul_round_pch(A, B, R)
#define _mm512_mask_cmul_round_pch(W, U, A, B, R)                              \
  _mm512_mask_fcmul_round_pch(W, U, A, B, R)
#define _mm512_maskz_cmul_round_pch(U, A, B, R)                                \
  _mm512_maskz_fcmul_round_pch(U, A, B, R)

#define _mm_mul_sch(A, B) _mm_fmul_sch(A, B)
#define _mm_mask_mul_sch(W, U, A, B) _mm_mask_fmul_sch(W, U, A, B)
#define _mm_maskz_mul_sch(U, A, B) _mm_maskz_fmul_sch(U, A, B)
#define _mm_mul_round_sch(A, B, R) _mm_fmul_round_sch(A, B, R)
#define _mm_mask_mul_round_sch(W, U, A, B, R)                                  \
  _mm_mask_fmul_round_sch(W, U, A, B, R)
#define _mm_maskz_mul_round_sch(U, A, B, R) _mm_maskz_fmul_round_sch(U, A, B, R)

#define _mm_cmul_sch(A, B) _mm_fcmul_sch(A, B)
#define _mm_mask_cmul_sch(W, U, A, B) _mm_mask_fcmul_sch(W, U, A, B)
#define _mm_maskz_cmul_sch(U, A, B) _mm_maskz_fcmul_sch(U, A, B)
#define _mm_cmul_round_sch(A, B, R) _mm_fcmul_round_sch(A, B, R)
#define _mm_mask_cmul_round_sch(W, U, A, B, R)                                 \
  _mm_mask_fcmul_round_sch(W, U, A, B, R)
#define _mm_maskz_cmul_round_sch(U, A, B, R)                                   \
  _mm_maskz_fcmul_round_sch(U, A, B, R)

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256
#undef __DEFAULT_FN_ATTRS512

#endif
#endif
