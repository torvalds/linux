/*===---- xmmintrin.h - Implementation of SSE intrinsics on PowerPC --------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

/* Implemented from the specification included in the Intel C++ Compiler
   User Guide and Reference, version 9.0.  */

#ifndef NO_WARN_X86_INTRINSICS
/* This header file is to help porting code using Intel intrinsics
   explicitly from x86_64 to powerpc64/powerpc64le.

   Since X86 SSE intrinsics mainly handles __m128 type, PowerPC
   VMX/VSX ISA is a good match for vector float SIMD operations.
   However scalar float operations in vector (XMM) registers require
   the POWER8 VSX ISA (2.07) level. There are differences for data
   format and placement of float scalars in the vector register, which
   require extra steps to match SSE scalar float semantics on POWER.

   It should be noted that there's much difference between X86_64's
   MXSCR and PowerISA's FPSCR/VSCR registers. It's recommended to use
   portable <fenv.h> instead of access MXSCR directly.

   Most SSE scalar float intrinsic operations can be performed more
   efficiently as C language float scalar operations or optimized to
   use vector SIMD operations. We recommend this for new applications. */
#error                                                                         \
    "Please read comment above. Use -DNO_WARN_X86_INTRINSICS to disable this error."
#endif

#ifndef XMMINTRIN_H_
#define XMMINTRIN_H_

#if defined(__powerpc64__) &&                                                  \
    (defined(__linux__) || defined(__FreeBSD__) || defined(_AIX))

/* Define four value permute mask */
#define _MM_SHUFFLE(w, x, y, z) (((w) << 6) | ((x) << 4) | ((y) << 2) | (z))

#include <altivec.h>

/* Avoid collisions between altivec.h and strict adherence to C++ and
   C11 standards.  This should eventually be done inside altivec.h itself,
   but only after testing a full distro build.  */
#if defined(__STRICT_ANSI__) &&                                                \
    (defined(__cplusplus) ||                                                   \
     (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
#undef vector
#undef pixel
#undef bool
#endif

/* We need type definitions from the MMX header file.  */
#include <mmintrin.h>

/* Get _mm_malloc () and _mm_free ().  */
#if __STDC_HOSTED__
#include <mm_malloc.h>
#endif

/* The Intel API is flexible enough that we must allow aliasing with other
   vector types, and their scalar components.  */
typedef vector float __m128 __attribute__((__may_alias__));

/* Unaligned version of the same type.  */
typedef vector float __m128_u __attribute__((__may_alias__, __aligned__(1)));

/* Internal data types for implementing the intrinsics.  */
typedef vector float __v4sf;

/* Create an undefined vector.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_undefined_ps(void) {
  __m128 __Y = __Y;
  return __Y;
}

/* Create a vector of zeros.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_setzero_ps(void) {
  return __extension__(__m128){0.0f, 0.0f, 0.0f, 0.0f};
}

/* Load four SPFP values from P.  The address must be 16-byte aligned.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_load_ps(float const *__P) {
  return ((__m128)vec_ld(0, (__v4sf *)__P));
}

/* Load four SPFP values from P.  The address need not be 16-byte aligned.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadu_ps(float const *__P) {
  return (vec_vsx_ld(0, __P));
}

/* Load four SPFP values in reverse order.  The address must be aligned.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadr_ps(float const *__P) {
  __v4sf __tmp;
  __m128 __result;
  static const __vector unsigned char __permute_vector = {
      0x1C, 0x1D, 0x1E, 0x1F, 0x18, 0x19, 0x1A, 0x1B,
      0x14, 0x15, 0x16, 0x17, 0x10, 0x11, 0x12, 0x13};

  __tmp = vec_ld(0, (__v4sf *)__P);
  __result = (__m128)vec_perm(__tmp, __tmp, __permute_vector);
  return __result;
}

/* Create a vector with all four elements equal to F.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set1_ps(float __F) {
  return __extension__(__m128)(__v4sf){__F, __F, __F, __F};
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_ps1(float __F) {
  return _mm_set1_ps(__F);
}

/* Create the vector [Z Y X W].  */
extern __inline __m128 __attribute__((__gnu_inline__, __always_inline__,
                                      __artificial__))
_mm_set_ps(const float __Z, const float __Y, const float __X, const float __W) {
  return __extension__(__m128)(__v4sf){__W, __X, __Y, __Z};
}

/* Create the vector [W X Y Z].  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_setr_ps(float __Z, float __Y, float __X, float __W) {
  return __extension__(__m128)(__v4sf){__Z, __Y, __X, __W};
}

/* Store four SPFP values.  The address must be 16-byte aligned.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_store_ps(float *__P, __m128 __A) {
  vec_st((__v4sf)__A, 0, (__v4sf *)__P);
}

/* Store four SPFP values.  The address need not be 16-byte aligned.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storeu_ps(float *__P, __m128 __A) {
  *(__m128_u *)__P = __A;
}

/* Store four SPFP values in reverse order.  The address must be aligned.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storer_ps(float *__P, __m128 __A) {
  __v4sf __tmp;
  static const __vector unsigned char __permute_vector = {
      0x1C, 0x1D, 0x1E, 0x1F, 0x18, 0x19, 0x1A, 0x1B,
      0x14, 0x15, 0x16, 0x17, 0x10, 0x11, 0x12, 0x13};

  __tmp = (__m128)vec_perm(__A, __A, __permute_vector);

  _mm_store_ps(__P, __tmp);
}

/* Store the lower SPFP value across four words.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_store1_ps(float *__P, __m128 __A) {
  __v4sf __va = vec_splat((__v4sf)__A, 0);
  _mm_store_ps(__P, __va);
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_store_ps1(float *__P, __m128 __A) {
  _mm_store1_ps(__P, __A);
}

/* Create a vector with element 0 as F and the rest zero.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_ss(float __F) {
  return __extension__(__m128)(__v4sf){__F, 0.0f, 0.0f, 0.0f};
}

/* Sets the low SPFP value of A from the low value of B.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_move_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};

  return (vec_sel((__v4sf)__A, (__v4sf)__B, __mask));
}

/* Create a vector with element 0 as *P and the rest zero.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_load_ss(float const *__P) {
  return _mm_set_ss(*__P);
}

/* Stores the lower SPFP value.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_store_ss(float *__P, __m128 __A) {
  *__P = ((__v4sf)__A)[0];
}

/* Perform the respective operation on the lower SPFP (single-precision
   floating-point) values of A and B; the upper three SPFP values are
   passed through from A.  */

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_add_ss(__m128 __A, __m128 __B) {
#ifdef _ARCH_PWR7
  __m128 __a, __b, __c;
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  /* PowerISA VSX does not allow partial (for just lower double)
     results. So to insure we don't generate spurious exceptions
     (from the upper double values) we splat the lower double
     before we to the operation.  */
  __a = vec_splat(__A, 0);
  __b = vec_splat(__B, 0);
  __c = __a + __b;
  /* Then we merge the lower float result with the original upper
     float elements from __A.  */
  return (vec_sel(__A, __c, __mask));
#else
  __A[0] = __A[0] + __B[0];
  return (__A);
#endif
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sub_ss(__m128 __A, __m128 __B) {
#ifdef _ARCH_PWR7
  __m128 __a, __b, __c;
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  /* PowerISA VSX does not allow partial (for just lower double)
     results. So to insure we don't generate spurious exceptions
     (from the upper double values) we splat the lower double
     before we to the operation.  */
  __a = vec_splat(__A, 0);
  __b = vec_splat(__B, 0);
  __c = __a - __b;
  /* Then we merge the lower float result with the original upper
     float elements from __A.  */
  return (vec_sel(__A, __c, __mask));
#else
  __A[0] = __A[0] - __B[0];
  return (__A);
#endif
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mul_ss(__m128 __A, __m128 __B) {
#ifdef _ARCH_PWR7
  __m128 __a, __b, __c;
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  /* PowerISA VSX does not allow partial (for just lower double)
     results. So to insure we don't generate spurious exceptions
     (from the upper double values) we splat the lower double
     before we to the operation.  */
  __a = vec_splat(__A, 0);
  __b = vec_splat(__B, 0);
  __c = __a * __b;
  /* Then we merge the lower float result with the original upper
     float elements from __A.  */
  return (vec_sel(__A, __c, __mask));
#else
  __A[0] = __A[0] * __B[0];
  return (__A);
#endif
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_div_ss(__m128 __A, __m128 __B) {
#ifdef _ARCH_PWR7
  __m128 __a, __b, __c;
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  /* PowerISA VSX does not allow partial (for just lower double)
     results. So to insure we don't generate spurious exceptions
     (from the upper double values) we splat the lower double
     before we to the operation.  */
  __a = vec_splat(__A, 0);
  __b = vec_splat(__B, 0);
  __c = __a / __b;
  /* Then we merge the lower float result with the original upper
     float elements from __A.  */
  return (vec_sel(__A, __c, __mask));
#else
  __A[0] = __A[0] / __B[0];
  return (__A);
#endif
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sqrt_ss(__m128 __A) {
  __m128 __a, __c;
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  /* PowerISA VSX does not allow partial (for just lower double)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper double values) we splat the lower double
   * before we to the operation. */
  __a = vec_splat(__A, 0);
  __c = vec_sqrt(__a);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return (vec_sel(__A, __c, __mask));
}

/* Perform the respective operation on the four SPFP values in A and B.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_add_ps(__m128 __A, __m128 __B) {
  return (__m128)((__v4sf)__A + (__v4sf)__B);
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sub_ps(__m128 __A, __m128 __B) {
  return (__m128)((__v4sf)__A - (__v4sf)__B);
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mul_ps(__m128 __A, __m128 __B) {
  return (__m128)((__v4sf)__A * (__v4sf)__B);
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_div_ps(__m128 __A, __m128 __B) {
  return (__m128)((__v4sf)__A / (__v4sf)__B);
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sqrt_ps(__m128 __A) {
  return (vec_sqrt((__v4sf)__A));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_rcp_ps(__m128 __A) {
  return (vec_re((__v4sf)__A));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_rsqrt_ps(__m128 __A) {
  return (vec_rsqrte(__A));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_rcp_ss(__m128 __A) {
  __m128 __a, __c;
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  /* PowerISA VSX does not allow partial (for just lower double)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper double values) we splat the lower double
   * before we to the operation. */
  __a = vec_splat(__A, 0);
  __c = _mm_rcp_ps(__a);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return (vec_sel(__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_rsqrt_ss(__m128 __A) {
  __m128 __a, __c;
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  /* PowerISA VSX does not allow partial (for just lower double)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper double values) we splat the lower double
   * before we to the operation. */
  __a = vec_splat(__A, 0);
  __c = vec_rsqrte(__a);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return (vec_sel(__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_ss(__m128 __A, __m128 __B) {
  __v4sf __a, __b, __c;
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  /* PowerISA VSX does not allow partial (for just lower float)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper float values) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = vec_min(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return (vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_ss(__m128 __A, __m128 __B) {
  __v4sf __a, __b, __c;
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  /* PowerISA VSX does not allow partial (for just lower float)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper float values) we splat the lower float
   * before we to the operation. */
  __a = vec_splat(__A, 0);
  __b = vec_splat(__B, 0);
  __c = vec_max(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return (vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_ps(__m128 __A, __m128 __B) {
  __vector __bool int __m = vec_cmpgt((__v4sf)__B, (__v4sf)__A);
  return vec_sel(__B, __A, __m);
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_ps(__m128 __A, __m128 __B) {
  __vector __bool int __m = vec_cmpgt((__v4sf)__A, (__v4sf)__B);
  return vec_sel(__B, __A, __m);
}

/* Perform logical bit-wise operations on 128-bit values.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_and_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_and((__v4sf)__A, (__v4sf)__B));
  //  return __builtin_ia32_andps (__A, __B);
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_andnot_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_andc((__v4sf)__B, (__v4sf)__A));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_or_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_or((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_xor_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_xor((__v4sf)__A, (__v4sf)__B));
}

/* Perform a comparison on the four SPFP values of A and B.  For each
   element, if the comparison is true, place a mask of all ones in the
   result, otherwise a mask of zeros.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpeq_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_cmpeq((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmplt_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_cmplt((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmple_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_cmple((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpgt_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_cmpgt((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpge_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_cmpge((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpneq_ps(__m128 __A, __m128 __B) {
  __v4sf __temp = (__v4sf)vec_cmpeq((__v4sf)__A, (__v4sf)__B);
  return ((__m128)vec_nor(__temp, __temp));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnlt_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_cmpge((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnle_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_cmpgt((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpngt_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_cmple((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnge_ps(__m128 __A, __m128 __B) {
  return ((__m128)vec_cmplt((__v4sf)__A, (__v4sf)__B));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpord_ps(__m128 __A, __m128 __B) {
  __vector unsigned int __a, __b;
  __vector unsigned int __c, __d;
  static const __vector unsigned int __float_exp_mask = {
      0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000};

  __a = (__vector unsigned int)vec_abs((__v4sf)__A);
  __b = (__vector unsigned int)vec_abs((__v4sf)__B);
  __c = (__vector unsigned int)vec_cmpgt(__float_exp_mask, __a);
  __d = (__vector unsigned int)vec_cmpgt(__float_exp_mask, __b);
  return ((__m128)vec_and(__c, __d));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpunord_ps(__m128 __A, __m128 __B) {
  __vector unsigned int __a, __b;
  __vector unsigned int __c, __d;
  static const __vector unsigned int __float_exp_mask = {
      0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000};

  __a = (__vector unsigned int)vec_abs((__v4sf)__A);
  __b = (__vector unsigned int)vec_abs((__v4sf)__B);
  __c = (__vector unsigned int)vec_cmpgt(__a, __float_exp_mask);
  __d = (__vector unsigned int)vec_cmpgt(__b, __float_exp_mask);
  return ((__m128)vec_or(__c, __d));
}

/* Perform a comparison on the lower SPFP values of A and B.  If the
   comparison is true, place a mask of all ones in the result, otherwise a
   mask of zeros.  The upper three SPFP values are passed through from A.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpeq_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmpeq(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmplt_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmplt(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmple_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmple(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpgt_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmpgt(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpge_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmpge(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpneq_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmpeq(__a, __b);
  __c = vec_nor(__c, __c);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnlt_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmpge(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnle_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmpgt(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpngt_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we to the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmple(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnge_ss(__m128 __A, __m128 __B) {
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};
  __v4sf __a, __b, __c;
  /* PowerISA VMX does not allow partial (for just element 0)
   * results. So to insure we don't generate spurious exceptions
   * (from the upper elements) we splat the lower float
   * before we do the operation. */
  __a = vec_splat((__v4sf)__A, 0);
  __b = vec_splat((__v4sf)__B, 0);
  __c = (__v4sf)vec_cmplt(__a, __b);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, __c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpord_ss(__m128 __A, __m128 __B) {
  __vector unsigned int __a, __b;
  __vector unsigned int __c, __d;
  static const __vector unsigned int __float_exp_mask = {
      0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000};
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};

  __a = (__vector unsigned int)vec_abs((__v4sf)__A);
  __b = (__vector unsigned int)vec_abs((__v4sf)__B);
  __c = (__vector unsigned int)vec_cmpgt(__float_exp_mask, __a);
  __d = (__vector unsigned int)vec_cmpgt(__float_exp_mask, __b);
  __c = vec_and(__c, __d);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, (__v4sf)__c, __mask));
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpunord_ss(__m128 __A, __m128 __B) {
  __vector unsigned int __a, __b;
  __vector unsigned int __c, __d;
  static const __vector unsigned int __float_exp_mask = {
      0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000};
  static const __vector unsigned int __mask = {0xffffffff, 0, 0, 0};

  __a = (__vector unsigned int)vec_abs((__v4sf)__A);
  __b = (__vector unsigned int)vec_abs((__v4sf)__B);
  __c = (__vector unsigned int)vec_cmpgt(__a, __float_exp_mask);
  __d = (__vector unsigned int)vec_cmpgt(__b, __float_exp_mask);
  __c = vec_or(__c, __d);
  /* Then we merge the lower float result with the original upper
   * float elements from __A.  */
  return ((__m128)vec_sel((__v4sf)__A, (__v4sf)__c, __mask));
}

/* Compare the lower SPFP values of A and B and return 1 if true
   and 0 if false.  */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comieq_ss(__m128 __A, __m128 __B) {
  return (__A[0] == __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comilt_ss(__m128 __A, __m128 __B) {
  return (__A[0] < __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comile_ss(__m128 __A, __m128 __B) {
  return (__A[0] <= __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comigt_ss(__m128 __A, __m128 __B) {
  return (__A[0] > __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comige_ss(__m128 __A, __m128 __B) {
  return (__A[0] >= __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comineq_ss(__m128 __A, __m128 __B) {
  return (__A[0] != __B[0]);
}

/* FIXME
 * The __mm_ucomi??_ss implementations below are exactly the same as
 * __mm_comi??_ss because GCC for PowerPC only generates unordered
 * compares (scalar and vector).
 * Technically __mm_comieq_ss et al should be using the ordered
 * compare and signal for QNaNs.
 * The __mm_ucomieq_sd et all should be OK, as is.
 */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomieq_ss(__m128 __A, __m128 __B) {
  return (__A[0] == __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomilt_ss(__m128 __A, __m128 __B) {
  return (__A[0] < __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomile_ss(__m128 __A, __m128 __B) {
  return (__A[0] <= __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomigt_ss(__m128 __A, __m128 __B) {
  return (__A[0] > __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomige_ss(__m128 __A, __m128 __B) {
  return (__A[0] >= __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomineq_ss(__m128 __A, __m128 __B) {
  return (__A[0] != __B[0]);
}

extern __inline float
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtss_f32(__m128 __A) {
  return ((__v4sf)__A)[0];
}

/* Convert the lower SPFP value to a 32-bit integer according to the current
   rounding mode.  */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtss_si32(__m128 __A) {
  int __res;
#ifdef _ARCH_PWR8
  double __dtmp;
  __asm__(
#ifdef __LITTLE_ENDIAN__
      "xxsldwi %x0,%x0,%x0,3;\n"
#endif
      "xscvspdp %x2,%x0;\n"
      "fctiw  %2,%2;\n"
      "mfvsrd  %1,%x2;\n"
      : "+wa"(__A), "=r"(__res), "=f"(__dtmp)
      :);
#else
  __res = __builtin_rint(__A[0]);
#endif
  return __res;
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvt_ss2si(__m128 __A) {
  return _mm_cvtss_si32(__A);
}

/* Convert the lower SPFP value to a 32-bit integer according to the
   current rounding mode.  */

/* Intel intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtss_si64(__m128 __A) {
  long long __res;
#if defined(_ARCH_PWR8) && defined(__powerpc64__)
  double __dtmp;
  __asm__(
#ifdef __LITTLE_ENDIAN__
      "xxsldwi %x0,%x0,%x0,3;\n"
#endif
      "xscvspdp %x2,%x0;\n"
      "fctid  %2,%2;\n"
      "mfvsrd  %1,%x2;\n"
      : "+wa"(__A), "=r"(__res), "=f"(__dtmp)
      :);
#else
  __res = __builtin_llrint(__A[0]);
#endif
  return __res;
}

/* Microsoft intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtss_si64x(__m128 __A) {
  return _mm_cvtss_si64((__v4sf)__A);
}

/* Constants for use with _mm_prefetch.  */
enum _mm_hint {
  /* _MM_HINT_ET is _MM_HINT_T with set 3rd bit.  */
  _MM_HINT_ET0 = 7,
  _MM_HINT_ET1 = 6,
  _MM_HINT_T0 = 3,
  _MM_HINT_T1 = 2,
  _MM_HINT_T2 = 1,
  _MM_HINT_NTA = 0
};

/* Loads one cache line from address P to a location "closer" to the
   processor.  The selector I specifies the type of prefetch operation.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_prefetch(const void *__P, enum _mm_hint __I) {
  /* Current PowerPC will ignores the hint parameters.  */
  __builtin_prefetch(__P);
}

/* Convert the two lower SPFP values to 32-bit integers according to the
   current rounding mode.  Return the integers in packed form.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtps_pi32(__m128 __A) {
  /* Splat two lower SPFP values to both halves.  */
  __v4sf __temp, __rounded;
  __vector unsigned long long __result;

  /* Splat two lower SPFP values to both halves.  */
  __temp = (__v4sf)vec_splat((__vector long long)__A, 0);
  __rounded = vec_rint(__temp);
  __result = (__vector unsigned long long)vec_cts(__rounded, 0);

  return (__m64)((__vector long long)__result)[0];
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvt_ps2pi(__m128 __A) {
  return _mm_cvtps_pi32(__A);
}

/* Truncate the lower SPFP value to a 32-bit integer.  */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttss_si32(__m128 __A) {
  /* Extract the lower float element.  */
  float __temp = __A[0];
  /* truncate to 32-bit integer and return.  */
  return __temp;
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtt_ss2si(__m128 __A) {
  return _mm_cvttss_si32(__A);
}

/* Intel intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttss_si64(__m128 __A) {
  /* Extract the lower float element.  */
  float __temp = __A[0];
  /* truncate to 32-bit integer and return.  */
  return __temp;
}

/* Microsoft intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttss_si64x(__m128 __A) {
  /* Extract the lower float element.  */
  float __temp = __A[0];
  /* truncate to 32-bit integer and return.  */
  return __temp;
}

/* Truncate the two lower SPFP values to 32-bit integers.  Return the
   integers in packed form.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttps_pi32(__m128 __A) {
  __v4sf __temp;
  __vector unsigned long long __result;

  /* Splat two lower SPFP values to both halves.  */
  __temp = (__v4sf)vec_splat((__vector long long)__A, 0);
  __result = (__vector unsigned long long)vec_cts(__temp, 0);

  return (__m64)((__vector long long)__result)[0];
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtt_ps2pi(__m128 __A) {
  return _mm_cvttps_pi32(__A);
}

/* Convert B to a SPFP value and insert it as element zero in A.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi32_ss(__m128 __A, int __B) {
  float __temp = __B;
  __A[0] = __temp;

  return __A;
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvt_si2ss(__m128 __A, int __B) {
  return _mm_cvtsi32_ss(__A, __B);
}

/* Convert B to a SPFP value and insert it as element zero in A.  */
/* Intel intrinsic.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi64_ss(__m128 __A, long long __B) {
  float __temp = __B;
  __A[0] = __temp;

  return __A;
}

/* Microsoft intrinsic.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi64x_ss(__m128 __A, long long __B) {
  return _mm_cvtsi64_ss(__A, __B);
}

/* Convert the two 32-bit values in B to SPFP form and insert them
   as the two lower elements in A.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtpi32_ps(__m128 __A, __m64 __B) {
  __vector signed int __vm1;
  __vector float __vf1;

  __vm1 = (__vector signed int)(__vector unsigned long long){__B, __B};
  __vf1 = (__vector float)vec_ctf(__vm1, 0);

  return ((__m128)(__vector unsigned long long){
      ((__vector unsigned long long)__vf1)[0],
      ((__vector unsigned long long)__A)[1]});
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvt_pi2ps(__m128 __A, __m64 __B) {
  return _mm_cvtpi32_ps(__A, __B);
}

/* Convert the four signed 16-bit values in A to SPFP form.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtpi16_ps(__m64 __A) {
  __vector signed short __vs8;
  __vector signed int __vi4;
  __vector float __vf1;

  __vs8 = (__vector signed short)(__vector unsigned long long){__A, __A};
  __vi4 = vec_vupklsh(__vs8);
  __vf1 = (__vector float)vec_ctf(__vi4, 0);

  return (__m128)__vf1;
}

/* Convert the four unsigned 16-bit values in A to SPFP form.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtpu16_ps(__m64 __A) {
  const __vector unsigned short __zero = {0, 0, 0, 0, 0, 0, 0, 0};
  __vector unsigned short __vs8;
  __vector unsigned int __vi4;
  __vector float __vf1;

  __vs8 = (__vector unsigned short)(__vector unsigned long long){__A, __A};
  __vi4 = (__vector unsigned int)vec_mergel
#ifdef __LITTLE_ENDIAN__
      (__vs8, __zero);
#else
      (__zero, __vs8);
#endif
  __vf1 = (__vector float)vec_ctf(__vi4, 0);

  return (__m128)__vf1;
}

/* Convert the low four signed 8-bit values in A to SPFP form.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtpi8_ps(__m64 __A) {
  __vector signed char __vc16;
  __vector signed short __vs8;
  __vector signed int __vi4;
  __vector float __vf1;

  __vc16 = (__vector signed char)(__vector unsigned long long){__A, __A};
  __vs8 = vec_vupkhsb(__vc16);
  __vi4 = vec_vupkhsh(__vs8);
  __vf1 = (__vector float)vec_ctf(__vi4, 0);

  return (__m128)__vf1;
}

/* Convert the low four unsigned 8-bit values in A to SPFP form.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))

    _mm_cvtpu8_ps(__m64 __A) {
  const __vector unsigned char __zero = {0, 0, 0, 0, 0, 0, 0, 0};
  __vector unsigned char __vc16;
  __vector unsigned short __vs8;
  __vector unsigned int __vi4;
  __vector float __vf1;

  __vc16 = (__vector unsigned char)(__vector unsigned long long){__A, __A};
#ifdef __LITTLE_ENDIAN__
  __vs8 = (__vector unsigned short)vec_mergel(__vc16, __zero);
  __vi4 =
      (__vector unsigned int)vec_mergeh(__vs8, (__vector unsigned short)__zero);
#else
  __vs8 = (__vector unsigned short)vec_mergel(__zero, __vc16);
  __vi4 =
      (__vector unsigned int)vec_mergeh((__vector unsigned short)__zero, __vs8);
#endif
  __vf1 = (__vector float)vec_ctf(__vi4, 0);

  return (__m128)__vf1;
}

/* Convert the four signed 32-bit values in A and B to SPFP form.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtpi32x2_ps(__m64 __A, __m64 __B) {
  __vector signed int __vi4;
  __vector float __vf4;

  __vi4 = (__vector signed int)(__vector unsigned long long){__A, __B};
  __vf4 = (__vector float)vec_ctf(__vi4, 0);
  return (__m128)__vf4;
}

/* Convert the four SPFP values in A to four signed 16-bit integers.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtps_pi16(__m128 __A) {
  __v4sf __rounded;
  __vector signed int __temp;
  __vector unsigned long long __result;

  __rounded = vec_rint(__A);
  __temp = vec_cts(__rounded, 0);
  __result = (__vector unsigned long long)vec_pack(__temp, __temp);

  return (__m64)((__vector long long)__result)[0];
}

/* Convert the four SPFP values in A to four signed 8-bit integers.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtps_pi8(__m128 __A) {
  __v4sf __rounded;
  __vector signed int __tmp_i;
  static const __vector signed int __zero = {0, 0, 0, 0};
  __vector signed short __tmp_s;
  __vector signed char __res_v;

  __rounded = vec_rint(__A);
  __tmp_i = vec_cts(__rounded, 0);
  __tmp_s = vec_pack(__tmp_i, __zero);
  __res_v = vec_pack(__tmp_s, __tmp_s);
  return (__m64)((__vector long long)__res_v)[0];
}

/* Selects four specific SPFP values from A and B based on MASK.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))

    _mm_shuffle_ps(__m128 __A, __m128 __B, int const __mask) {
  unsigned long __element_selector_10 = __mask & 0x03;
  unsigned long __element_selector_32 = (__mask >> 2) & 0x03;
  unsigned long __element_selector_54 = (__mask >> 4) & 0x03;
  unsigned long __element_selector_76 = (__mask >> 6) & 0x03;
  static const unsigned int __permute_selectors[4] = {
#ifdef __LITTLE_ENDIAN__
      0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
#else
      0x00010203, 0x04050607, 0x08090A0B, 0x0C0D0E0F
#endif
  };
  __vector unsigned int __t;

  __t[0] = __permute_selectors[__element_selector_10];
  __t[1] = __permute_selectors[__element_selector_32];
  __t[2] = __permute_selectors[__element_selector_54] + 0x10101010;
  __t[3] = __permute_selectors[__element_selector_76] + 0x10101010;
  return vec_perm((__v4sf)__A, (__v4sf)__B, (__vector unsigned char)__t);
}

/* Selects and interleaves the upper two SPFP values from A and B.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpackhi_ps(__m128 __A, __m128 __B) {
  return (__m128)vec_vmrglw((__v4sf)__A, (__v4sf)__B);
}

/* Selects and interleaves the lower two SPFP values from A and B.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpacklo_ps(__m128 __A, __m128 __B) {
  return (__m128)vec_vmrghw((__v4sf)__A, (__v4sf)__B);
}

/* Sets the upper two SPFP values with 64-bits of data loaded from P;
   the lower two values are passed through from A.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadh_pi(__m128 __A, __m64 const *__P) {
  __vector unsigned long long __a = (__vector unsigned long long)__A;
  __vector unsigned long long __p = vec_splats(*__P);
  __a[1] = __p[1];

  return (__m128)__a;
}

/* Stores the upper two SPFP values of A into P.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storeh_pi(__m64 *__P, __m128 __A) {
  __vector unsigned long long __a = (__vector unsigned long long)__A;

  *__P = __a[1];
}

/* Moves the upper two values of B into the lower two values of A.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_movehl_ps(__m128 __A, __m128 __B) {
  return (__m128)vec_mergel((__vector unsigned long long)__B,
                            (__vector unsigned long long)__A);
}

/* Moves the lower two values of B into the upper two values of A.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_movelh_ps(__m128 __A, __m128 __B) {
  return (__m128)vec_mergeh((__vector unsigned long long)__A,
                            (__vector unsigned long long)__B);
}

/* Sets the lower two SPFP values with 64-bits of data loaded from P;
   the upper two values are passed through from A.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadl_pi(__m128 __A, __m64 const *__P) {
  __vector unsigned long long __a = (__vector unsigned long long)__A;
  __vector unsigned long long __p = vec_splats(*__P);
  __a[0] = __p[0];

  return (__m128)__a;
}

/* Stores the lower two SPFP values of A into P.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storel_pi(__m64 *__P, __m128 __A) {
  __vector unsigned long long __a = (__vector unsigned long long)__A;

  *__P = __a[0];
}

#ifdef _ARCH_PWR8
/* Intrinsic functions that require PowerISA 2.07 minimum.  */

/* Creates a 4-bit mask from the most significant bits of the SPFP values.  */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_movemask_ps(__m128 __A) {
#ifdef _ARCH_PWR10
  return vec_extractm((__vector unsigned int)__A);
#else
  __vector unsigned long long __result;
  static const __vector unsigned int __perm_mask = {
#ifdef __LITTLE_ENDIAN__
      0x00204060, 0x80808080, 0x80808080, 0x80808080
#else
      0x80808080, 0x80808080, 0x80808080, 0x00204060
#endif
  };

  __result = ((__vector unsigned long long)vec_vbpermq(
      (__vector unsigned char)__A, (__vector unsigned char)__perm_mask));

#ifdef __LITTLE_ENDIAN__
  return __result[1];
#else
  return __result[0];
#endif
#endif /* !_ARCH_PWR10 */
}
#endif /* _ARCH_PWR8 */

/* Create a vector with all four elements equal to *P.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_load1_ps(float const *__P) {
  return _mm_set1_ps(*__P);
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_load_ps1(float const *__P) {
  return _mm_load1_ps(__P);
}

/* Extracts one of the four words of A.  The selector N must be immediate.  */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_extract_pi16(__m64 const __A, int const __N) {
  unsigned int __shiftr = __N & 3;
#ifdef __BIG_ENDIAN__
  __shiftr = 3 - __shiftr;
#endif

  return ((__A >> (__shiftr * 16)) & 0xffff);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pextrw(__m64 const __A, int const __N) {
  return _mm_extract_pi16(__A, __N);
}

/* Inserts word D into one of four words of A.  The selector N must be
   immediate.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_insert_pi16(__m64 const __A, int const __D, int const __N) {
  const int __shiftl = (__N & 3) * 16;
  const __m64 __shiftD = (const __m64)__D << __shiftl;
  const __m64 __mask = 0xffffUL << __shiftl;
  __m64 __result = (__A & (~__mask)) | (__shiftD & __mask);

  return __result;
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pinsrw(__m64 const __A, int const __D, int const __N) {
  return _mm_insert_pi16(__A, __D, __N);
}

/* Compute the element-wise maximum of signed 16-bit values.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))

    _mm_max_pi16(__m64 __A, __m64 __B) {
#if _ARCH_PWR8
  __vector signed short __a, __b, __r;
  __vector __bool short __c;

  __a = (__vector signed short)vec_splats(__A);
  __b = (__vector signed short)vec_splats(__B);
  __c = (__vector __bool short)vec_cmpgt(__a, __b);
  __r = vec_sel(__b, __a, __c);
  return (__m64)((__vector long long)__r)[0];
#else
  __m64_union __m1, __m2, __res;

  __m1.as_m64 = __A;
  __m2.as_m64 = __B;

  __res.as_short[0] = (__m1.as_short[0] > __m2.as_short[0]) ? __m1.as_short[0]
                                                            : __m2.as_short[0];
  __res.as_short[1] = (__m1.as_short[1] > __m2.as_short[1]) ? __m1.as_short[1]
                                                            : __m2.as_short[1];
  __res.as_short[2] = (__m1.as_short[2] > __m2.as_short[2]) ? __m1.as_short[2]
                                                            : __m2.as_short[2];
  __res.as_short[3] = (__m1.as_short[3] > __m2.as_short[3]) ? __m1.as_short[3]
                                                            : __m2.as_short[3];

  return (__m64)__res.as_m64;
#endif
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pmaxsw(__m64 __A, __m64 __B) {
  return _mm_max_pi16(__A, __B);
}

/* Compute the element-wise maximum of unsigned 8-bit values.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_pu8(__m64 __A, __m64 __B) {
#if _ARCH_PWR8
  __vector unsigned char __a, __b, __r;
  __vector __bool char __c;

  __a = (__vector unsigned char)vec_splats(__A);
  __b = (__vector unsigned char)vec_splats(__B);
  __c = (__vector __bool char)vec_cmpgt(__a, __b);
  __r = vec_sel(__b, __a, __c);
  return (__m64)((__vector long long)__r)[0];
#else
  __m64_union __m1, __m2, __res;
  long __i;

  __m1.as_m64 = __A;
  __m2.as_m64 = __B;

  for (__i = 0; __i < 8; __i++)
    __res.as_char[__i] =
        ((unsigned char)__m1.as_char[__i] > (unsigned char)__m2.as_char[__i])
            ? __m1.as_char[__i]
            : __m2.as_char[__i];

  return (__m64)__res.as_m64;
#endif
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pmaxub(__m64 __A, __m64 __B) {
  return _mm_max_pu8(__A, __B);
}

/* Compute the element-wise minimum of signed 16-bit values.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_pi16(__m64 __A, __m64 __B) {
#if _ARCH_PWR8
  __vector signed short __a, __b, __r;
  __vector __bool short __c;

  __a = (__vector signed short)vec_splats(__A);
  __b = (__vector signed short)vec_splats(__B);
  __c = (__vector __bool short)vec_cmplt(__a, __b);
  __r = vec_sel(__b, __a, __c);
  return (__m64)((__vector long long)__r)[0];
#else
  __m64_union __m1, __m2, __res;

  __m1.as_m64 = __A;
  __m2.as_m64 = __B;

  __res.as_short[0] = (__m1.as_short[0] < __m2.as_short[0]) ? __m1.as_short[0]
                                                            : __m2.as_short[0];
  __res.as_short[1] = (__m1.as_short[1] < __m2.as_short[1]) ? __m1.as_short[1]
                                                            : __m2.as_short[1];
  __res.as_short[2] = (__m1.as_short[2] < __m2.as_short[2]) ? __m1.as_short[2]
                                                            : __m2.as_short[2];
  __res.as_short[3] = (__m1.as_short[3] < __m2.as_short[3]) ? __m1.as_short[3]
                                                            : __m2.as_short[3];

  return (__m64)__res.as_m64;
#endif
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pminsw(__m64 __A, __m64 __B) {
  return _mm_min_pi16(__A, __B);
}

/* Compute the element-wise minimum of unsigned 8-bit values.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_pu8(__m64 __A, __m64 __B) {
#if _ARCH_PWR8
  __vector unsigned char __a, __b, __r;
  __vector __bool char __c;

  __a = (__vector unsigned char)vec_splats(__A);
  __b = (__vector unsigned char)vec_splats(__B);
  __c = (__vector __bool char)vec_cmplt(__a, __b);
  __r = vec_sel(__b, __a, __c);
  return (__m64)((__vector long long)__r)[0];
#else
  __m64_union __m1, __m2, __res;
  long __i;

  __m1.as_m64 = __A;
  __m2.as_m64 = __B;

  for (__i = 0; __i < 8; __i++)
    __res.as_char[__i] =
        ((unsigned char)__m1.as_char[__i] < (unsigned char)__m2.as_char[__i])
            ? __m1.as_char[__i]
            : __m2.as_char[__i];

  return (__m64)__res.as_m64;
#endif
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pminub(__m64 __A, __m64 __B) {
  return _mm_min_pu8(__A, __B);
}

/* Create an 8-bit mask of the signs of 8-bit values.  */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_movemask_pi8(__m64 __A) {
#ifdef __powerpc64__
  unsigned long long __p =
#ifdef __LITTLE_ENDIAN__
      0x0008101820283038UL; // permute control for sign bits
#else
      0x3830282018100800UL; // permute control for sign bits
#endif
  return __builtin_bpermd(__p, __A);
#else
#ifdef __LITTLE_ENDIAN__
  unsigned int __mask = 0x20283038UL;
  unsigned int __r1 = __builtin_bpermd(__mask, __A) & 0xf;
  unsigned int __r2 = __builtin_bpermd(__mask, __A >> 32) & 0xf;
#else
  unsigned int __mask = 0x38302820UL;
  unsigned int __r1 = __builtin_bpermd(__mask, __A >> 32) & 0xf;
  unsigned int __r2 = __builtin_bpermd(__mask, __A) & 0xf;
#endif
  return (__r2 << 4) | __r1;
#endif
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pmovmskb(__m64 __A) {
  return _mm_movemask_pi8(__A);
}

/* Multiply four unsigned 16-bit values in A by four unsigned 16-bit values
   in B and produce the high 16 bits of the 32-bit results.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mulhi_pu16(__m64 __A, __m64 __B) {
  __vector unsigned short __a, __b;
  __vector unsigned short __c;
  __vector unsigned int __w0, __w1;
  __vector unsigned char __xform1 = {
#ifdef __LITTLE_ENDIAN__
      0x02, 0x03, 0x12, 0x13, 0x06, 0x07, 0x16, 0x17, 0x0A,
      0x0B, 0x1A, 0x1B, 0x0E, 0x0F, 0x1E, 0x1F
#else
      0x00, 0x01, 0x10, 0x11, 0x04, 0x05, 0x14, 0x15, 0x00,
      0x01, 0x10, 0x11, 0x04, 0x05, 0x14, 0x15
#endif
  };

  __a = (__vector unsigned short)vec_splats(__A);
  __b = (__vector unsigned short)vec_splats(__B);

  __w0 = vec_vmuleuh(__a, __b);
  __w1 = vec_vmulouh(__a, __b);
  __c = (__vector unsigned short)vec_perm(__w0, __w1, __xform1);

  return (__m64)((__vector long long)__c)[0];
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pmulhuw(__m64 __A, __m64 __B) {
  return _mm_mulhi_pu16(__A, __B);
}

/* Return a combination of the four 16-bit values in A.  The selector
   must be an immediate.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_shuffle_pi16(__m64 __A, int const __N) {
  unsigned long __element_selector_10 = __N & 0x03;
  unsigned long __element_selector_32 = (__N >> 2) & 0x03;
  unsigned long __element_selector_54 = (__N >> 4) & 0x03;
  unsigned long __element_selector_76 = (__N >> 6) & 0x03;
  static const unsigned short __permute_selectors[4] = {
#ifdef __LITTLE_ENDIAN__
      0x0908, 0x0B0A, 0x0D0C, 0x0F0E
#else
      0x0607, 0x0405, 0x0203, 0x0001
#endif
  };
  __m64_union __t;
  __vector unsigned long long __a, __p, __r;

#ifdef __LITTLE_ENDIAN__
  __t.as_short[0] = __permute_selectors[__element_selector_10];
  __t.as_short[1] = __permute_selectors[__element_selector_32];
  __t.as_short[2] = __permute_selectors[__element_selector_54];
  __t.as_short[3] = __permute_selectors[__element_selector_76];
#else
  __t.as_short[3] = __permute_selectors[__element_selector_10];
  __t.as_short[2] = __permute_selectors[__element_selector_32];
  __t.as_short[1] = __permute_selectors[__element_selector_54];
  __t.as_short[0] = __permute_selectors[__element_selector_76];
#endif
  __p = vec_splats(__t.as_m64);
  __a = vec_splats(__A);
  __r = vec_perm(__a, __a, (__vector unsigned char)__p);
  return (__m64)((__vector long long)__r)[0];
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pshufw(__m64 __A, int const __N) {
  return _mm_shuffle_pi16(__A, __N);
}

/* Conditionally store byte elements of A into P.  The high bit of each
   byte in the selector N determines whether the corresponding byte from
   A is stored.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_maskmove_si64(__m64 __A, __m64 __N, char *__P) {
  __m64 __hibit = 0x8080808080808080UL;
  __m64 __mask, __tmp;
  __m64 *__p = (__m64 *)__P;

  __tmp = *__p;
  __mask = _mm_cmpeq_pi8((__N & __hibit), __hibit);
  __tmp = (__tmp & (~__mask)) | (__A & __mask);
  *__p = __tmp;
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_maskmovq(__m64 __A, __m64 __N, char *__P) {
  _mm_maskmove_si64(__A, __N, __P);
}

/* Compute the rounded averages of the unsigned 8-bit values in A and B.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_avg_pu8(__m64 __A, __m64 __B) {
  __vector unsigned char __a, __b, __c;

  __a = (__vector unsigned char)vec_splats(__A);
  __b = (__vector unsigned char)vec_splats(__B);
  __c = vec_avg(__a, __b);
  return (__m64)((__vector long long)__c)[0];
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pavgb(__m64 __A, __m64 __B) {
  return _mm_avg_pu8(__A, __B);
}

/* Compute the rounded averages of the unsigned 16-bit values in A and B.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_avg_pu16(__m64 __A, __m64 __B) {
  __vector unsigned short __a, __b, __c;

  __a = (__vector unsigned short)vec_splats(__A);
  __b = (__vector unsigned short)vec_splats(__B);
  __c = vec_avg(__a, __b);
  return (__m64)((__vector long long)__c)[0];
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_pavgw(__m64 __A, __m64 __B) {
  return _mm_avg_pu16(__A, __B);
}

/* Compute the sum of the absolute differences of the unsigned 8-bit
   values in A and B.  Return the value in the lower 16-bit word; the
   upper words are cleared.  */
extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sad_pu8(__m64 __A, __m64 __B) {
  __vector unsigned char __a, __b;
  __vector unsigned char __vmin, __vmax, __vabsdiff;
  __vector signed int __vsum;
  const __vector unsigned int __zero = {0, 0, 0, 0};
  __m64_union __result = {0};

  __a = (__vector unsigned char)(__vector unsigned long long){0UL, __A};
  __b = (__vector unsigned char)(__vector unsigned long long){0UL, __B};
  __vmin = vec_min(__a, __b);
  __vmax = vec_max(__a, __b);
  __vabsdiff = vec_sub(__vmax, __vmin);
  /* Sum four groups of bytes into integers.  */
  __vsum = (__vector signed int)vec_sum4s(__vabsdiff, __zero);
  /* Sum across four integers with integer result.  */
  __vsum = vec_sums(__vsum, (__vector signed int)__zero);
  /* The sum is in the right most 32-bits of the vector result.
     Transfer to a GPR and truncate to 16 bits.  */
  __result.as_short[0] = __vsum[3];
  return __result.as_m64;
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _m_psadbw(__m64 __A, __m64 __B) {
  return _mm_sad_pu8(__A, __B);
}

/* Stores the data in A to the address P without polluting the caches.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_stream_pi(__m64 *__P, __m64 __A) {
  /* Use the data cache block touch for store transient.  */
  __asm__("	dcbtstt	0,%0" : : "b"(__P) : "memory");
  *__P = __A;
}

/* Likewise.  The address must be 16-byte aligned.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_stream_ps(float *__P, __m128 __A) {
  /* Use the data cache block touch for store transient.  */
  __asm__("	dcbtstt	0,%0" : : "b"(__P) : "memory");
  _mm_store_ps(__P, __A);
}

/* Guarantees that every preceding store is globally visible before
   any subsequent store.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sfence(void) {
  /* Generate a light weight sync.  */
  __atomic_thread_fence(__ATOMIC_RELEASE);
}

/* The execution of the next instruction is delayed by an implementation
   specific amount of time.  The instruction does not modify the
   architectural state.  This is after the pop_options pragma because
   it does not require SSE support in the processor--the encoding is a
   nop on processors that do not support it.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_pause(void) {
  /* There is no exact match with this construct, but the following is
     close to the desired effect.  */
#if _ARCH_PWR8
  /* On power8 and later processors we can depend on Program Priority
     (PRI) and associated "very low" PPI setting.  Since we don't know
     what PPI this thread is running at we: 1) save the current PRI
     from the PPR SPR into a local GRP, 2) set the PRI to "very low*
     via the special or 31,31,31 encoding. 3) issue an "isync" to
     insure the PRI change takes effect before we execute any more
     instructions.
     Now we can execute a lwsync (release barrier) while we execute
     this thread at "very low" PRI.  Finally we restore the original
     PRI and continue execution.  */
  unsigned long __PPR;

  __asm__ volatile("	mfppr	%0;"
                   "   or 31,31,31;"
                   "   isync;"
                   "   lwsync;"
                   "   isync;"
                   "   mtppr	%0;"
                   : "=r"(__PPR)
                   :
                   : "memory");
#else
  /* For older processor where we may not even have Program Priority
     controls we can only depend on Heavy Weight Sync.  */
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

/* Transpose the 4x4 matrix composed of row[0-3].  */
#define _MM_TRANSPOSE4_PS(row0, row1, row2, row3)                              \
  do {                                                                         \
    __v4sf __r0 = (row0), __r1 = (row1), __r2 = (row2), __r3 = (row3);         \
    __v4sf __t0 = vec_vmrghw(__r0, __r1);                                      \
    __v4sf __t1 = vec_vmrghw(__r2, __r3);                                      \
    __v4sf __t2 = vec_vmrglw(__r0, __r1);                                      \
    __v4sf __t3 = vec_vmrglw(__r2, __r3);                                      \
    (row0) = (__v4sf)vec_mergeh((__vector long long)__t0,                      \
                                (__vector long long)__t1);                     \
    (row1) = (__v4sf)vec_mergel((__vector long long)__t0,                      \
                                (__vector long long)__t1);                     \
    (row2) = (__v4sf)vec_mergeh((__vector long long)__t2,                      \
                                (__vector long long)__t3);                     \
    (row3) = (__v4sf)vec_mergel((__vector long long)__t2,                      \
                                (__vector long long)__t3);                     \
  } while (0)

/* For backward source compatibility.  */
//# include <emmintrin.h>

#else
#include_next <xmmintrin.h>
#endif /* defined(__powerpc64__) &&                                            \
        *   (defined(__linux__) || defined(__FreeBSD__) || defined(_AIX)) */

#endif /* XMMINTRIN_H_ */
