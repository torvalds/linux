/*===---- emmintrin.h - Implementation of SSE2 intrinsics on PowerPC -------===
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

   Since X86 SSE2 intrinsics mainly handles __m128i and __m128d type,
   PowerPC VMX/VSX ISA is a good match for vector float SIMD operations.
   However scalar float operations in vector (XMM) registers require
   the POWER8 VSX ISA (2.07) level. There are differences for data
   format and placement of float scalars in the vector register, which
   require extra steps to match SSE2 scalar float semantics on POWER.

   It should be noted that there's much difference between X86_64's
   MXSCR and PowerISA's FPSCR/VSCR registers. It's recommended to use
   portable <fenv.h> instead of access MXSCR directly.

   Most SSE2 scalar float intrinsic operations can be performed more
   efficiently as C language float scalar operations or optimized to
   use vector SIMD operations. We recommend this for new applications.
*/
#error                                                                         \
    "Please read comment above.  Use -DNO_WARN_X86_INTRINSICS to disable this error."
#endif

#ifndef EMMINTRIN_H_
#define EMMINTRIN_H_

#if defined(__powerpc64__) &&                                                  \
    (defined(__linux__) || defined(__FreeBSD__) || defined(_AIX))

#include <altivec.h>

/* We need definitions from the SSE header files.  */
#include <xmmintrin.h>

/* SSE2 */
typedef __vector double __v2df;
typedef __vector float __v4f;
typedef __vector long long __v2di;
typedef __vector unsigned long long __v2du;
typedef __vector int __v4si;
typedef __vector unsigned int __v4su;
typedef __vector short __v8hi;
typedef __vector unsigned short __v8hu;
typedef __vector signed char __v16qi;
typedef __vector unsigned char __v16qu;

/* The Intel API is flexible enough that we must allow aliasing with other
   vector types, and their scalar components.  */
typedef long long __m128i __attribute__((__vector_size__(16), __may_alias__));
typedef double __m128d __attribute__((__vector_size__(16), __may_alias__));

/* Unaligned version of the same types.  */
typedef long long __m128i_u
    __attribute__((__vector_size__(16), __may_alias__, __aligned__(1)));
typedef double __m128d_u
    __attribute__((__vector_size__(16), __may_alias__, __aligned__(1)));

/* Define two value permute mask.  */
#define _MM_SHUFFLE2(x, y) (((x) << 1) | (y))

/* Create a vector with element 0 as F and the rest zero.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_sd(double __F) {
  return __extension__(__m128d){__F, 0.0};
}

/* Create a vector with both elements equal to F.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set1_pd(double __F) {
  return __extension__(__m128d){__F, __F};
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_pd1(double __F) {
  return _mm_set1_pd(__F);
}

/* Create a vector with the lower value X and upper value W.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_pd(double __W, double __X) {
  return __extension__(__m128d){__X, __W};
}

/* Create a vector with the lower value W and upper value X.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_setr_pd(double __W, double __X) {
  return __extension__(__m128d){__W, __X};
}

/* Create an undefined vector.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_undefined_pd(void) {
  __m128d __Y = __Y;
  return __Y;
}

/* Create a vector of zeros.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_setzero_pd(void) {
  return (__m128d)vec_splats(0);
}

/* Sets the low DPFP value of A from the low value of B.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_move_sd(__m128d __A, __m128d __B) {
  __v2df __result = (__v2df)__A;
  __result[0] = ((__v2df)__B)[0];
  return (__m128d)__result;
}

/* Load two DPFP values from P.  The address must be 16-byte aligned.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_load_pd(double const *__P) {
  return ((__m128d)vec_ld(0, (__v16qu *)__P));
}

/* Load two DPFP values from P.  The address need not be 16-byte aligned.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadu_pd(double const *__P) {
  return (vec_vsx_ld(0, __P));
}

/* Create a vector with all two elements equal to *P.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_load1_pd(double const *__P) {
  return (vec_splats(*__P));
}

/* Create a vector with element 0 as *P and the rest zero.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_load_sd(double const *__P) {
  return _mm_set_sd(*__P);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_load_pd1(double const *__P) {
  return _mm_load1_pd(__P);
}

/* Load two DPFP values in reverse order.  The address must be aligned.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadr_pd(double const *__P) {
  __v2df __tmp = _mm_load_pd(__P);
  return (__m128d)vec_xxpermdi(__tmp, __tmp, 2);
}

/* Store two DPFP values.  The address must be 16-byte aligned.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_store_pd(double *__P, __m128d __A) {
  vec_st((__v16qu)__A, 0, (__v16qu *)__P);
}

/* Store two DPFP values.  The address need not be 16-byte aligned.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storeu_pd(double *__P, __m128d __A) {
  *(__m128d_u *)__P = __A;
}

/* Stores the lower DPFP value.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_store_sd(double *__P, __m128d __A) {
  *__P = ((__v2df)__A)[0];
}

extern __inline double
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsd_f64(__m128d __A) {
  return ((__v2df)__A)[0];
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storel_pd(double *__P, __m128d __A) {
  _mm_store_sd(__P, __A);
}

/* Stores the upper DPFP value.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storeh_pd(double *__P, __m128d __A) {
  *__P = ((__v2df)__A)[1];
}
/* Store the lower DPFP value across two words.
   The address must be 16-byte aligned.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_store1_pd(double *__P, __m128d __A) {
  _mm_store_pd(__P, vec_splat(__A, 0));
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_store_pd1(double *__P, __m128d __A) {
  _mm_store1_pd(__P, __A);
}

/* Store two DPFP values in reverse order.  The address must be aligned.  */
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storer_pd(double *__P, __m128d __A) {
  _mm_store_pd(__P, vec_xxpermdi(__A, __A, 2));
}

/* Intel intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi128_si64(__m128i __A) {
  return ((__v2di)__A)[0];
}

/* Microsoft intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi128_si64x(__m128i __A) {
  return ((__v2di)__A)[0];
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_add_pd(__m128d __A, __m128d __B) {
  return (__m128d)((__v2df)__A + (__v2df)__B);
}

/* Add the lower double-precision (64-bit) floating-point element in
   a and b, store the result in the lower element of dst, and copy
   the upper element from a to the upper element of dst. */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_add_sd(__m128d __A, __m128d __B) {
  __A[0] = __A[0] + __B[0];
  return (__A);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sub_pd(__m128d __A, __m128d __B) {
  return (__m128d)((__v2df)__A - (__v2df)__B);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sub_sd(__m128d __A, __m128d __B) {
  __A[0] = __A[0] - __B[0];
  return (__A);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mul_pd(__m128d __A, __m128d __B) {
  return (__m128d)((__v2df)__A * (__v2df)__B);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mul_sd(__m128d __A, __m128d __B) {
  __A[0] = __A[0] * __B[0];
  return (__A);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_div_pd(__m128d __A, __m128d __B) {
  return (__m128d)((__v2df)__A / (__v2df)__B);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_div_sd(__m128d __A, __m128d __B) {
  __A[0] = __A[0] / __B[0];
  return (__A);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sqrt_pd(__m128d __A) {
  return (vec_sqrt(__A));
}

/* Return pair {sqrt (B[0]), A[1]}.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sqrt_sd(__m128d __A, __m128d __B) {
  __v2df __c;
  __c = vec_sqrt((__v2df)_mm_set1_pd(__B[0]));
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_pd(__m128d __A, __m128d __B) {
  return (vec_min(__A, __B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  __c = vec_min(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_pd(__m128d __A, __m128d __B) {
  return (vec_max(__A, __B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  __c = vec_max(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpeq_pd(__m128d __A, __m128d __B) {
  return ((__m128d)vec_cmpeq((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmplt_pd(__m128d __A, __m128d __B) {
  return ((__m128d)vec_cmplt((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmple_pd(__m128d __A, __m128d __B) {
  return ((__m128d)vec_cmple((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpgt_pd(__m128d __A, __m128d __B) {
  return ((__m128d)vec_cmpgt((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpge_pd(__m128d __A, __m128d __B) {
  return ((__m128d)vec_cmpge((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpneq_pd(__m128d __A, __m128d __B) {
  __v2df __temp = (__v2df)vec_cmpeq((__v2df)__A, (__v2df)__B);
  return ((__m128d)vec_nor(__temp, __temp));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnlt_pd(__m128d __A, __m128d __B) {
  return ((__m128d)vec_cmpge((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnle_pd(__m128d __A, __m128d __B) {
  return ((__m128d)vec_cmpgt((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpngt_pd(__m128d __A, __m128d __B) {
  return ((__m128d)vec_cmple((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnge_pd(__m128d __A, __m128d __B) {
  return ((__m128d)vec_cmplt((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpord_pd(__m128d __A, __m128d __B) {
  __v2du __c, __d;
  /* Compare against self will return false (0's) if NAN.  */
  __c = (__v2du)vec_cmpeq(__A, __A);
  __d = (__v2du)vec_cmpeq(__B, __B);
  /* A != NAN and B != NAN.  */
  return ((__m128d)vec_and(__c, __d));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpunord_pd(__m128d __A, __m128d __B) {
#if _ARCH_PWR8
  __v2du __c, __d;
  /* Compare against self will return false (0's) if NAN.  */
  __c = (__v2du)vec_cmpeq((__v2df)__A, (__v2df)__A);
  __d = (__v2du)vec_cmpeq((__v2df)__B, (__v2df)__B);
  /* A == NAN OR B == NAN converts too:
     NOT(A != NAN) OR NOT(B != NAN).  */
  __c = vec_nor(__c, __c);
  return ((__m128d)vec_orc(__c, __d));
#else
  __v2du __c, __d;
  /* Compare against self will return false (0's) if NAN.  */
  __c = (__v2du)vec_cmpeq((__v2df)__A, (__v2df)__A);
  __d = (__v2du)vec_cmpeq((__v2df)__B, (__v2df)__B);
  /* Convert the true ('1's) is NAN.  */
  __c = vec_nor(__c, __c);
  __d = vec_nor(__d, __d);
  return ((__m128d)vec_or(__c, __d));
#endif
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpeq_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  /* PowerISA VSX does not allow partial (for just lower double)
     results. So to insure we don't generate spurious exceptions
     (from the upper double values) we splat the lower double
     before we do the operation. */
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  __c = (__v2df)vec_cmpeq(__a, __b);
  /* Then we merge the lower double result with the original upper
     double from __A.  */
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmplt_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  __c = (__v2df)vec_cmplt(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmple_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  __c = (__v2df)vec_cmple(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpgt_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  __c = (__v2df)vec_cmpgt(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpge_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  __c = (__v2df)vec_cmpge(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpneq_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  __c = (__v2df)vec_cmpeq(__a, __b);
  __c = vec_nor(__c, __c);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnlt_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  /* Not less than is just greater than or equal.  */
  __c = (__v2df)vec_cmpge(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnle_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  /* Not less than or equal is just greater than.  */
  __c = (__v2df)vec_cmpge(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpngt_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  /* Not greater than is just less than or equal.  */
  __c = (__v2df)vec_cmple(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpnge_sd(__m128d __A, __m128d __B) {
  __v2df __a, __b, __c;
  __a = vec_splats(__A[0]);
  __b = vec_splats(__B[0]);
  /* Not greater than or equal is just less than.  */
  __c = (__v2df)vec_cmplt(__a, __b);
  return (__m128d)_mm_setr_pd(__c[0], __A[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpord_sd(__m128d __A, __m128d __B) {
  __v2df __r;
  __r = (__v2df)_mm_cmpord_pd(vec_splats(__A[0]), vec_splats(__B[0]));
  return (__m128d)_mm_setr_pd(__r[0], ((__v2df)__A)[1]);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpunord_sd(__m128d __A, __m128d __B) {
  __v2df __r;
  __r = _mm_cmpunord_pd(vec_splats(__A[0]), vec_splats(__B[0]));
  return (__m128d)_mm_setr_pd(__r[0], __A[1]);
}

/* FIXME
   The __mm_comi??_sd and __mm_ucomi??_sd implementations below are
   exactly the same because GCC for PowerPC only generates unordered
   compares (scalar and vector).
   Technically __mm_comieq_sp et all should be using the ordered
   compare and signal for QNaNs.  The __mm_ucomieq_sd et all should
   be OK.   */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comieq_sd(__m128d __A, __m128d __B) {
  return (__A[0] == __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comilt_sd(__m128d __A, __m128d __B) {
  return (__A[0] < __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comile_sd(__m128d __A, __m128d __B) {
  return (__A[0] <= __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comigt_sd(__m128d __A, __m128d __B) {
  return (__A[0] > __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comige_sd(__m128d __A, __m128d __B) {
  return (__A[0] >= __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_comineq_sd(__m128d __A, __m128d __B) {
  return (__A[0] != __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomieq_sd(__m128d __A, __m128d __B) {
  return (__A[0] == __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomilt_sd(__m128d __A, __m128d __B) {
  return (__A[0] < __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomile_sd(__m128d __A, __m128d __B) {
  return (__A[0] <= __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomigt_sd(__m128d __A, __m128d __B) {
  return (__A[0] > __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomige_sd(__m128d __A, __m128d __B) {
  return (__A[0] >= __B[0]);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_ucomineq_sd(__m128d __A, __m128d __B) {
  return (__A[0] != __B[0]);
}

/* Create a vector of Qi, where i is the element number.  */
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_epi64x(long long __q1, long long __q0) {
  return __extension__(__m128i)(__v2di){__q0, __q1};
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_epi64(__m64 __q1, __m64 __q0) {
  return _mm_set_epi64x((long long)__q1, (long long)__q0);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_epi32(int __q3, int __q2, int __q1, int __q0) {
  return __extension__(__m128i)(__v4si){__q0, __q1, __q2, __q3};
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_epi16(short __q7, short __q6, short __q5, short __q4, short __q3,
                  short __q2, short __q1, short __q0) {
  return __extension__(__m128i)(__v8hi){__q0, __q1, __q2, __q3,
                                        __q4, __q5, __q6, __q7};
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set_epi8(char __q15, char __q14, char __q13, char __q12, char __q11,
                 char __q10, char __q09, char __q08, char __q07, char __q06,
                 char __q05, char __q04, char __q03, char __q02, char __q01,
                 char __q00) {
  return __extension__(__m128i)(__v16qi){
      __q00, __q01, __q02, __q03, __q04, __q05, __q06, __q07,
      __q08, __q09, __q10, __q11, __q12, __q13, __q14, __q15};
}

/* Set all of the elements of the vector to A.  */
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set1_epi64x(long long __A) {
  return _mm_set_epi64x(__A, __A);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set1_epi64(__m64 __A) {
  return _mm_set_epi64(__A, __A);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set1_epi32(int __A) {
  return _mm_set_epi32(__A, __A, __A, __A);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set1_epi16(short __A) {
  return _mm_set_epi16(__A, __A, __A, __A, __A, __A, __A, __A);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_set1_epi8(char __A) {
  return _mm_set_epi8(__A, __A, __A, __A, __A, __A, __A, __A, __A, __A, __A,
                      __A, __A, __A, __A, __A);
}

/* Create a vector of Qi, where i is the element number.
   The parameter order is reversed from the _mm_set_epi* functions.  */
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_setr_epi64(__m64 __q0, __m64 __q1) {
  return _mm_set_epi64(__q1, __q0);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_setr_epi32(int __q0, int __q1, int __q2, int __q3) {
  return _mm_set_epi32(__q3, __q2, __q1, __q0);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_setr_epi16(short __q0, short __q1, short __q2, short __q3, short __q4,
                   short __q5, short __q6, short __q7) {
  return _mm_set_epi16(__q7, __q6, __q5, __q4, __q3, __q2, __q1, __q0);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_setr_epi8(char __q00, char __q01, char __q02, char __q03, char __q04,
                  char __q05, char __q06, char __q07, char __q08, char __q09,
                  char __q10, char __q11, char __q12, char __q13, char __q14,
                  char __q15) {
  return _mm_set_epi8(__q15, __q14, __q13, __q12, __q11, __q10, __q09, __q08,
                      __q07, __q06, __q05, __q04, __q03, __q02, __q01, __q00);
}

/* Create a vector with element 0 as *P and the rest zero.  */
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_load_si128(__m128i const *__P) {
  return *__P;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadu_si128(__m128i_u const *__P) {
  return (__m128i)(vec_vsx_ld(0, (signed int const *)__P));
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadl_epi64(__m128i_u const *__P) {
  return _mm_set_epi64((__m64)0LL, *(__m64 *)__P);
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_store_si128(__m128i *__P, __m128i __B) {
  vec_st((__v16qu)__B, 0, (__v16qu *)__P);
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storeu_si128(__m128i_u *__P, __m128i __B) {
  *__P = __B;
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_storel_epi64(__m128i_u *__P, __m128i __B) {
  *(long long *)__P = ((__v2di)__B)[0];
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_movepi64_pi64(__m128i_u __B) {
  return (__m64)((__v2di)__B)[0];
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_movpi64_epi64(__m64 __A) {
  return _mm_set_epi64((__m64)0LL, __A);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_move_epi64(__m128i __A) {
  return _mm_set_epi64((__m64)0LL, (__m64)__A[0]);
}

/* Create an undefined vector.  */
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_undefined_si128(void) {
  __m128i __Y = __Y;
  return __Y;
}

/* Create a vector of zeros.  */
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_setzero_si128(void) {
  return __extension__(__m128i)(__v4si){0, 0, 0, 0};
}

#ifdef _ARCH_PWR8
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepi32_pd(__m128i __A) {
  __v2di __val;
  /* For LE need to generate Vector Unpack Low Signed Word.
     Which is generated from unpackh.  */
  __val = (__v2di)vec_unpackh((__v4si)__A);

  return (__m128d)vec_ctf(__val, 0);
}
#endif

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtepi32_ps(__m128i __A) {
  return ((__m128)vec_ctf((__v4si)__A, 0));
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtpd_epi32(__m128d __A) {
  __v2df __rounded = vec_rint(__A);
  __v4si __result, __temp;
  const __v4si __vzero = {0, 0, 0, 0};

  /* VSX Vector truncate Double-Precision to integer and Convert to
   Signed Integer Word format with Saturate.  */
  __asm__("xvcvdpsxws %x0,%x1" : "=wa"(__temp) : "wa"(__rounded) :);

#ifdef _ARCH_PWR8
#ifdef __LITTLE_ENDIAN__
  __temp = vec_mergeo(__temp, __temp);
#else
  __temp = vec_mergee(__temp, __temp);
#endif
  __result = (__v4si)vec_vpkudum((__vector long long)__temp,
                                 (__vector long long)__vzero);
#else
  {
    const __v16qu __pkperm = {0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b,
                              0x14, 0x15, 0x16, 0x17, 0x1c, 0x1d, 0x1e, 0x1f};
    __result = (__v4si)vec_perm((__v16qu)__temp, (__v16qu)__vzero, __pkperm);
  }
#endif
  return (__m128i)__result;
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtpd_pi32(__m128d __A) {
  __m128i __result = _mm_cvtpd_epi32(__A);

  return (__m64)__result[0];
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtpd_ps(__m128d __A) {
  __v4sf __result;
  __v4si __temp;
  const __v4si __vzero = {0, 0, 0, 0};

  __asm__("xvcvdpsp %x0,%x1" : "=wa"(__temp) : "wa"(__A) :);

#ifdef _ARCH_PWR8
#ifdef __LITTLE_ENDIAN__
  __temp = vec_mergeo(__temp, __temp);
#else
  __temp = vec_mergee(__temp, __temp);
#endif
  __result = (__v4sf)vec_vpkudum((__vector long long)__temp,
                                 (__vector long long)__vzero);
#else
  {
    const __v16qu __pkperm = {0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b,
                              0x14, 0x15, 0x16, 0x17, 0x1c, 0x1d, 0x1e, 0x1f};
    __result = (__v4sf)vec_perm((__v16qu)__temp, (__v16qu)__vzero, __pkperm);
  }
#endif
  return ((__m128)__result);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttpd_epi32(__m128d __A) {
  __v4si __result;
  __v4si __temp;
  const __v4si __vzero = {0, 0, 0, 0};

  /* VSX Vector truncate Double-Precision to integer and Convert to
   Signed Integer Word format with Saturate.  */
  __asm__("xvcvdpsxws %x0,%x1" : "=wa"(__temp) : "wa"(__A) :);

#ifdef _ARCH_PWR8
#ifdef __LITTLE_ENDIAN__
  __temp = vec_mergeo(__temp, __temp);
#else
  __temp = vec_mergee(__temp, __temp);
#endif
  __result = (__v4si)vec_vpkudum((__vector long long)__temp,
                                 (__vector long long)__vzero);
#else
  {
    const __v16qu __pkperm = {0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b,
                              0x14, 0x15, 0x16, 0x17, 0x1c, 0x1d, 0x1e, 0x1f};
    __result = (__v4si)vec_perm((__v16qu)__temp, (__v16qu)__vzero, __pkperm);
  }
#endif

  return ((__m128i)__result);
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttpd_pi32(__m128d __A) {
  __m128i __result = _mm_cvttpd_epi32(__A);

  return (__m64)__result[0];
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi128_si32(__m128i __A) {
  return ((__v4si)__A)[0];
}

#ifdef _ARCH_PWR8
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtpi32_pd(__m64 __A) {
  __v4si __temp;
  __v2di __tmp2;
  __v4f __result;

  __temp = (__v4si)vec_splats(__A);
  __tmp2 = (__v2di)vec_unpackl(__temp);
  __result = vec_ctf((__vector signed long long)__tmp2, 0);
  return (__m128d)__result;
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtps_epi32(__m128 __A) {
  __v4sf __rounded;
  __v4si __result;

  __rounded = vec_rint((__v4sf)__A);
  __result = vec_cts(__rounded, 0);
  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttps_epi32(__m128 __A) {
  __v4si __result;

  __result = vec_cts((__v4sf)__A, 0);
  return (__m128i)__result;
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtps_pd(__m128 __A) {
  /* Check if vec_doubleh is defined by <altivec.h>. If so use that. */
#ifdef vec_doubleh
  return (__m128d)vec_doubleh((__v4sf)__A);
#else
  /* Otherwise the compiler is not current and so need to generate the
     equivalent code.  */
  __v4sf __a = (__v4sf)__A;
  __v4sf __temp;
  __v2df __result;
#ifdef __LITTLE_ENDIAN__
  /* The input float values are in elements {[0], [1]} but the convert
     instruction needs them in elements {[1], [3]}, So we use two
     shift left double vector word immediates to get the elements
     lined up.  */
  __temp = __builtin_vsx_xxsldwi(__a, __a, 3);
  __temp = __builtin_vsx_xxsldwi(__a, __temp, 2);
#else
  /* The input float values are in elements {[0], [1]} but the convert
     instruction needs them in elements {[0], [2]}, So we use two
     shift left double vector word immediates to get the elements
     lined up.  */
  __temp = vec_vmrghw(__a, __a);
#endif
  __asm__(" xvcvspdp %x0,%x1" : "=wa"(__result) : "wa"(__temp) :);
  return (__m128d)__result;
#endif
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsd_si32(__m128d __A) {
  __v2df __rounded = vec_rint((__v2df)__A);
  int __result = ((__v2df)__rounded)[0];

  return __result;
}
/* Intel intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsd_si64(__m128d __A) {
  __v2df __rounded = vec_rint((__v2df)__A);
  long long __result = ((__v2df)__rounded)[0];

  return __result;
}

/* Microsoft intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsd_si64x(__m128d __A) {
  return _mm_cvtsd_si64((__v2df)__A);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttsd_si32(__m128d __A) {
  int __result = ((__v2df)__A)[0];

  return __result;
}

/* Intel intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttsd_si64(__m128d __A) {
  long long __result = ((__v2df)__A)[0];

  return __result;
}

/* Microsoft intrinsic.  */
extern __inline long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvttsd_si64x(__m128d __A) {
  return _mm_cvttsd_si64(__A);
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsd_ss(__m128 __A, __m128d __B) {
  __v4sf __result = (__v4sf)__A;

#ifdef __LITTLE_ENDIAN__
  __v4sf __temp_s;
  /* Copy double element[0] to element [1] for conversion.  */
  __v2df __temp_b = vec_splat((__v2df)__B, 0);

  /* Pre-rotate __A left 3 (logically right 1) elements.  */
  __result = __builtin_vsx_xxsldwi(__result, __result, 3);
  /* Convert double to single float scalar in a vector.  */
  __asm__("xscvdpsp %x0,%x1" : "=wa"(__temp_s) : "wa"(__temp_b) :);
  /* Shift the resulting scalar into vector element [0].  */
  __result = __builtin_vsx_xxsldwi(__result, __temp_s, 1);
#else
  __result[0] = ((__v2df)__B)[0];
#endif
  return (__m128)__result;
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi32_sd(__m128d __A, int __B) {
  __v2df __result = (__v2df)__A;
  double __db = __B;
  __result[0] = __db;
  return (__m128d)__result;
}

/* Intel intrinsic.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi64_sd(__m128d __A, long long __B) {
  __v2df __result = (__v2df)__A;
  double __db = __B;
  __result[0] = __db;
  return (__m128d)__result;
}

/* Microsoft intrinsic.  */
extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi64x_sd(__m128d __A, long long __B) {
  return _mm_cvtsi64_sd(__A, __B);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtss_sd(__m128d __A, __m128 __B) {
#ifdef __LITTLE_ENDIAN__
  /* Use splat to move element [0] into position for the convert. */
  __v4sf __temp = vec_splat((__v4sf)__B, 0);
  __v2df __res;
  /* Convert single float scalar to double in a vector.  */
  __asm__("xscvspdp %x0,%x1" : "=wa"(__res) : "wa"(__temp) :);
  return (__m128d)vec_mergel(__res, (__v2df)__A);
#else
  __v2df __res = (__v2df)__A;
  __res[0] = ((__v4sf)__B)[0];
  return (__m128d)__res;
#endif
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_shuffle_pd(__m128d __A, __m128d __B, const int __mask) {
  __vector double __result;
  const int __litmsk = __mask & 0x3;

  if (__litmsk == 0)
    __result = vec_mergeh(__A, __B);
#if __GNUC__ < 6
  else if (__litmsk == 1)
    __result = vec_xxpermdi(__B, __A, 2);
  else if (__litmsk == 2)
    __result = vec_xxpermdi(__B, __A, 1);
#else
  else if (__litmsk == 1)
    __result = vec_xxpermdi(__A, __B, 2);
  else if (__litmsk == 2)
    __result = vec_xxpermdi(__A, __B, 1);
#endif
  else
    __result = vec_mergel(__A, __B);

  return __result;
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpackhi_pd(__m128d __A, __m128d __B) {
  return (__m128d)vec_mergel((__v2df)__A, (__v2df)__B);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpacklo_pd(__m128d __A, __m128d __B) {
  return (__m128d)vec_mergeh((__v2df)__A, (__v2df)__B);
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadh_pd(__m128d __A, double const *__B) {
  __v2df __result = (__v2df)__A;
  __result[1] = *__B;
  return (__m128d)__result;
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_loadl_pd(__m128d __A, double const *__B) {
  __v2df __result = (__v2df)__A;
  __result[0] = *__B;
  return (__m128d)__result;
}

#ifdef _ARCH_PWR8
/* Intrinsic functions that require PowerISA 2.07 minimum.  */

/* Creates a 2-bit mask from the most significant bits of the DPFP values.  */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_movemask_pd(__m128d __A) {
#ifdef _ARCH_PWR10
  return vec_extractm((__v2du)__A);
#else
  __vector unsigned long long __result;
  static const __vector unsigned int __perm_mask = {
#ifdef __LITTLE_ENDIAN__
      0x80800040, 0x80808080, 0x80808080, 0x80808080
#else
      0x80808080, 0x80808080, 0x80808080, 0x80804000
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

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_packs_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_packs((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_packs_epi32(__m128i __A, __m128i __B) {
  return (__m128i)vec_packs((__v4si)__A, (__v4si)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_packus_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_packsu((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpackhi_epi8(__m128i __A, __m128i __B) {
  return (__m128i)vec_mergel((__v16qu)__A, (__v16qu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpackhi_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_mergel((__v8hu)__A, (__v8hu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpackhi_epi32(__m128i __A, __m128i __B) {
  return (__m128i)vec_mergel((__v4su)__A, (__v4su)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpackhi_epi64(__m128i __A, __m128i __B) {
  return (__m128i)vec_mergel((__vector long long)__A, (__vector long long)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpacklo_epi8(__m128i __A, __m128i __B) {
  return (__m128i)vec_mergeh((__v16qu)__A, (__v16qu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpacklo_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_mergeh((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpacklo_epi32(__m128i __A, __m128i __B) {
  return (__m128i)vec_mergeh((__v4si)__A, (__v4si)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_unpacklo_epi64(__m128i __A, __m128i __B) {
  return (__m128i)vec_mergeh((__vector long long)__A, (__vector long long)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_add_epi8(__m128i __A, __m128i __B) {
  return (__m128i)((__v16qu)__A + (__v16qu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_add_epi16(__m128i __A, __m128i __B) {
  return (__m128i)((__v8hu)__A + (__v8hu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_add_epi32(__m128i __A, __m128i __B) {
  return (__m128i)((__v4su)__A + (__v4su)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_add_epi64(__m128i __A, __m128i __B) {
  return (__m128i)((__v2du)__A + (__v2du)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_adds_epi8(__m128i __A, __m128i __B) {
  return (__m128i)vec_adds((__v16qi)__A, (__v16qi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_adds_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_adds((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_adds_epu8(__m128i __A, __m128i __B) {
  return (__m128i)vec_adds((__v16qu)__A, (__v16qu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_adds_epu16(__m128i __A, __m128i __B) {
  return (__m128i)vec_adds((__v8hu)__A, (__v8hu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sub_epi8(__m128i __A, __m128i __B) {
  return (__m128i)((__v16qu)__A - (__v16qu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sub_epi16(__m128i __A, __m128i __B) {
  return (__m128i)((__v8hu)__A - (__v8hu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sub_epi32(__m128i __A, __m128i __B) {
  return (__m128i)((__v4su)__A - (__v4su)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sub_epi64(__m128i __A, __m128i __B) {
  return (__m128i)((__v2du)__A - (__v2du)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_subs_epi8(__m128i __A, __m128i __B) {
  return (__m128i)vec_subs((__v16qi)__A, (__v16qi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_subs_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_subs((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_subs_epu8(__m128i __A, __m128i __B) {
  return (__m128i)vec_subs((__v16qu)__A, (__v16qu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_subs_epu16(__m128i __A, __m128i __B) {
  return (__m128i)vec_subs((__v8hu)__A, (__v8hu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_madd_epi16(__m128i __A, __m128i __B) {
  __vector signed int __zero = {0, 0, 0, 0};

  return (__m128i)vec_vmsumshm((__v8hi)__A, (__v8hi)__B, __zero);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mulhi_epi16(__m128i __A, __m128i __B) {
  __vector signed int __w0, __w1;

  __vector unsigned char __xform1 = {
#ifdef __LITTLE_ENDIAN__
      0x02, 0x03, 0x12, 0x13, 0x06, 0x07, 0x16, 0x17, 0x0A,
      0x0B, 0x1A, 0x1B, 0x0E, 0x0F, 0x1E, 0x1F
#else
      0x00, 0x01, 0x10, 0x11, 0x04, 0x05, 0x14, 0x15, 0x08,
      0x09, 0x18, 0x19, 0x0C, 0x0D, 0x1C, 0x1D
#endif
  };

  __w0 = vec_vmulesh((__v8hi)__A, (__v8hi)__B);
  __w1 = vec_vmulosh((__v8hi)__A, (__v8hi)__B);
  return (__m128i)vec_perm(__w0, __w1, __xform1);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mullo_epi16(__m128i __A, __m128i __B) {
  return (__m128i)((__v8hi)__A * (__v8hi)__B);
}

extern __inline __m64
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mul_su32(__m64 __A, __m64 __B) {
  unsigned int __a = __A;
  unsigned int __b = __B;

  return ((__m64)__a * (__m64)__b);
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mul_epu32(__m128i __A, __m128i __B) {
#if __GNUC__ < 8
  __v2du __result;

#ifdef __LITTLE_ENDIAN__
  /* VMX Vector Multiply Odd Unsigned Word.  */
  __asm__("vmulouw %0,%1,%2" : "=v"(__result) : "v"(__A), "v"(__B) :);
#else
  /* VMX Vector Multiply Even Unsigned Word.  */
  __asm__("vmuleuw %0,%1,%2" : "=v"(__result) : "v"(__A), "v"(__B) :);
#endif
  return (__m128i)__result;
#else
  return (__m128i)vec_mule((__v4su)__A, (__v4su)__B);
#endif
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_slli_epi16(__m128i __A, int __B) {
  __v8hu __lshift;
  __v8hi __result = {0, 0, 0, 0, 0, 0, 0, 0};

  if (__B >= 0 && __B < 16) {
    if (__builtin_constant_p(__B))
      __lshift = (__v8hu)vec_splat_s16(__B);
    else
      __lshift = vec_splats((unsigned short)__B);

    __result = vec_sl((__v8hi)__A, __lshift);
  }

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_slli_epi32(__m128i __A, int __B) {
  __v4su __lshift;
  __v4si __result = {0, 0, 0, 0};

  if (__B >= 0 && __B < 32) {
    if (__builtin_constant_p(__B) && __B < 16)
      __lshift = (__v4su)vec_splat_s32(__B);
    else
      __lshift = vec_splats((unsigned int)__B);

    __result = vec_sl((__v4si)__A, __lshift);
  }

  return (__m128i)__result;
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_slli_epi64(__m128i __A, int __B) {
  __v2du __lshift;
  __v2di __result = {0, 0};

  if (__B >= 0 && __B < 64) {
    if (__builtin_constant_p(__B) && __B < 16)
      __lshift = (__v2du)vec_splat_s32(__B);
    else
      __lshift = (__v2du)vec_splats((unsigned int)__B);

    __result = vec_sl((__v2di)__A, __lshift);
  }

  return (__m128i)__result;
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_srai_epi16(__m128i __A, int __B) {
  __v8hu __rshift = {15, 15, 15, 15, 15, 15, 15, 15};
  __v8hi __result;

  if (__B < 16) {
    if (__builtin_constant_p(__B))
      __rshift = (__v8hu)vec_splat_s16(__B);
    else
      __rshift = vec_splats((unsigned short)__B);
  }
  __result = vec_sra((__v8hi)__A, __rshift);

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_srai_epi32(__m128i __A, int __B) {
  __v4su __rshift = {31, 31, 31, 31};
  __v4si __result;

  if (__B < 32) {
    if (__builtin_constant_p(__B)) {
      if (__B < 16)
        __rshift = (__v4su)vec_splat_s32(__B);
      else
        __rshift = (__v4su)vec_splats((unsigned int)__B);
    } else
      __rshift = vec_splats((unsigned int)__B);
  }
  __result = vec_sra((__v4si)__A, __rshift);

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_bslli_si128(__m128i __A, const int __N) {
  __v16qu __result;
  const __v16qu __zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  if (__N < 16)
    __result = vec_sld((__v16qu)__A, __zeros, __N);
  else
    __result = __zeros;

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_bsrli_si128(__m128i __A, const int __N) {
  __v16qu __result;
  const __v16qu __zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  if (__N < 16)
#ifdef __LITTLE_ENDIAN__
    if (__builtin_constant_p(__N))
      /* Would like to use Vector Shift Left Double by Octet
         Immediate here to use the immediate form and avoid
         load of __N * 8 value into a separate VR.  */
      __result = vec_sld(__zeros, (__v16qu)__A, (16 - __N));
    else
#endif
    {
      __v16qu __shift = vec_splats((unsigned char)(__N * 8));
#ifdef __LITTLE_ENDIAN__
      __result = vec_sro((__v16qu)__A, __shift);
#else
    __result = vec_slo((__v16qu)__A, __shift);
#endif
    }
  else
    __result = __zeros;

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_srli_si128(__m128i __A, const int __N) {
  return _mm_bsrli_si128(__A, __N);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_slli_si128(__m128i __A, const int _imm5) {
  __v16qu __result;
  const __v16qu __zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  if (_imm5 < 16)
#ifdef __LITTLE_ENDIAN__
    __result = vec_sld((__v16qu)__A, __zeros, _imm5);
#else
    __result = vec_sld(__zeros, (__v16qu)__A, (16 - _imm5));
#endif
  else
    __result = __zeros;

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))

    _mm_srli_epi16(__m128i __A, int __B) {
  __v8hu __rshift;
  __v8hi __result = {0, 0, 0, 0, 0, 0, 0, 0};

  if (__B < 16) {
    if (__builtin_constant_p(__B))
      __rshift = (__v8hu)vec_splat_s16(__B);
    else
      __rshift = vec_splats((unsigned short)__B);

    __result = vec_sr((__v8hi)__A, __rshift);
  }

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_srli_epi32(__m128i __A, int __B) {
  __v4su __rshift;
  __v4si __result = {0, 0, 0, 0};

  if (__B < 32) {
    if (__builtin_constant_p(__B)) {
      if (__B < 16)
        __rshift = (__v4su)vec_splat_s32(__B);
      else
        __rshift = (__v4su)vec_splats((unsigned int)__B);
    } else
      __rshift = vec_splats((unsigned int)__B);

    __result = vec_sr((__v4si)__A, __rshift);
  }

  return (__m128i)__result;
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_srli_epi64(__m128i __A, int __B) {
  __v2du __rshift;
  __v2di __result = {0, 0};

  if (__B < 64) {
    if (__builtin_constant_p(__B)) {
      if (__B < 16)
        __rshift = (__v2du)vec_splat_s32(__B);
      else
        __rshift = (__v2du)vec_splats((unsigned long long)__B);
    } else
      __rshift = (__v2du)vec_splats((unsigned int)__B);

    __result = vec_sr((__v2di)__A, __rshift);
  }

  return (__m128i)__result;
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sll_epi16(__m128i __A, __m128i __B) {
  __v8hu __lshift;
  __vector __bool short __shmask;
  const __v8hu __shmax = {15, 15, 15, 15, 15, 15, 15, 15};
  __v8hu __result;

#ifdef __LITTLE_ENDIAN__
  __lshift = vec_splat((__v8hu)__B, 0);
#else
  __lshift = vec_splat((__v8hu)__B, 3);
#endif
  __shmask = vec_cmple(__lshift, __shmax);
  __result = vec_sl((__v8hu)__A, __lshift);
  __result = vec_sel((__v8hu)__shmask, __result, __shmask);

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sll_epi32(__m128i __A, __m128i __B) {
  __v4su __lshift;
  __vector __bool int __shmask;
  const __v4su __shmax = {32, 32, 32, 32};
  __v4su __result;
#ifdef __LITTLE_ENDIAN__
  __lshift = vec_splat((__v4su)__B, 0);
#else
  __lshift = vec_splat((__v4su)__B, 1);
#endif
  __shmask = vec_cmplt(__lshift, __shmax);
  __result = vec_sl((__v4su)__A, __lshift);
  __result = vec_sel((__v4su)__shmask, __result, __shmask);

  return (__m128i)__result;
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sll_epi64(__m128i __A, __m128i __B) {
  __v2du __lshift;
  __vector __bool long long __shmask;
  const __v2du __shmax = {64, 64};
  __v2du __result;

  __lshift = vec_splat((__v2du)__B, 0);
  __shmask = vec_cmplt(__lshift, __shmax);
  __result = vec_sl((__v2du)__A, __lshift);
  __result = vec_sel((__v2du)__shmask, __result, __shmask);

  return (__m128i)__result;
}
#endif

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sra_epi16(__m128i __A, __m128i __B) {
  const __v8hu __rshmax = {15, 15, 15, 15, 15, 15, 15, 15};
  __v8hu __rshift;
  __v8hi __result;

#ifdef __LITTLE_ENDIAN__
  __rshift = vec_splat((__v8hu)__B, 0);
#else
  __rshift = vec_splat((__v8hu)__B, 3);
#endif
  __rshift = vec_min(__rshift, __rshmax);
  __result = vec_sra((__v8hi)__A, __rshift);

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sra_epi32(__m128i __A, __m128i __B) {
  const __v4su __rshmax = {31, 31, 31, 31};
  __v4su __rshift;
  __v4si __result;

#ifdef __LITTLE_ENDIAN__
  __rshift = vec_splat((__v4su)__B, 0);
#else
  __rshift = vec_splat((__v4su)__B, 1);
#endif
  __rshift = vec_min(__rshift, __rshmax);
  __result = vec_sra((__v4si)__A, __rshift);

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_srl_epi16(__m128i __A, __m128i __B) {
  __v8hu __rshift;
  __vector __bool short __shmask;
  const __v8hu __shmax = {15, 15, 15, 15, 15, 15, 15, 15};
  __v8hu __result;

#ifdef __LITTLE_ENDIAN__
  __rshift = vec_splat((__v8hu)__B, 0);
#else
  __rshift = vec_splat((__v8hu)__B, 3);
#endif
  __shmask = vec_cmple(__rshift, __shmax);
  __result = vec_sr((__v8hu)__A, __rshift);
  __result = vec_sel((__v8hu)__shmask, __result, __shmask);

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_srl_epi32(__m128i __A, __m128i __B) {
  __v4su __rshift;
  __vector __bool int __shmask;
  const __v4su __shmax = {32, 32, 32, 32};
  __v4su __result;

#ifdef __LITTLE_ENDIAN__
  __rshift = vec_splat((__v4su)__B, 0);
#else
  __rshift = vec_splat((__v4su)__B, 1);
#endif
  __shmask = vec_cmplt(__rshift, __shmax);
  __result = vec_sr((__v4su)__A, __rshift);
  __result = vec_sel((__v4su)__shmask, __result, __shmask);

  return (__m128i)__result;
}

#ifdef _ARCH_PWR8
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_srl_epi64(__m128i __A, __m128i __B) {
  __v2du __rshift;
  __vector __bool long long __shmask;
  const __v2du __shmax = {64, 64};
  __v2du __result;

  __rshift = vec_splat((__v2du)__B, 0);
  __shmask = vec_cmplt(__rshift, __shmax);
  __result = vec_sr((__v2du)__A, __rshift);
  __result = vec_sel((__v2du)__shmask, __result, __shmask);

  return (__m128i)__result;
}
#endif

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_and_pd(__m128d __A, __m128d __B) {
  return (vec_and((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_andnot_pd(__m128d __A, __m128d __B) {
  return (vec_andc((__v2df)__B, (__v2df)__A));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_or_pd(__m128d __A, __m128d __B) {
  return (vec_or((__v2df)__A, (__v2df)__B));
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_xor_pd(__m128d __A, __m128d __B) {
  return (vec_xor((__v2df)__A, (__v2df)__B));
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_and_si128(__m128i __A, __m128i __B) {
  return (__m128i)vec_and((__v2di)__A, (__v2di)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_andnot_si128(__m128i __A, __m128i __B) {
  return (__m128i)vec_andc((__v2di)__B, (__v2di)__A);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_or_si128(__m128i __A, __m128i __B) {
  return (__m128i)vec_or((__v2di)__A, (__v2di)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_xor_si128(__m128i __A, __m128i __B) {
  return (__m128i)vec_xor((__v2di)__A, (__v2di)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpeq_epi8(__m128i __A, __m128i __B) {
  return (__m128i)vec_cmpeq((__v16qi)__A, (__v16qi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpeq_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_cmpeq((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpeq_epi32(__m128i __A, __m128i __B) {
  return (__m128i)vec_cmpeq((__v4si)__A, (__v4si)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmplt_epi8(__m128i __A, __m128i __B) {
  return (__m128i)vec_cmplt((__v16qi)__A, (__v16qi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmplt_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_cmplt((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmplt_epi32(__m128i __A, __m128i __B) {
  return (__m128i)vec_cmplt((__v4si)__A, (__v4si)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpgt_epi8(__m128i __A, __m128i __B) {
  return (__m128i)vec_cmpgt((__v16qi)__A, (__v16qi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpgt_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_cmpgt((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cmpgt_epi32(__m128i __A, __m128i __B) {
  return (__m128i)vec_cmpgt((__v4si)__A, (__v4si)__B);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_extract_epi16(__m128i const __A, int const __N) {
  return (unsigned short)((__v8hi)__A)[__N & 7];
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_insert_epi16(__m128i const __A, int const __D, int const __N) {
  __v8hi __result = (__v8hi)__A;

  __result[(__N & 7)] = __D;

  return (__m128i)__result;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_max((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_max_epu8(__m128i __A, __m128i __B) {
  return (__m128i)vec_max((__v16qu)__A, (__v16qu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_epi16(__m128i __A, __m128i __B) {
  return (__m128i)vec_min((__v8hi)__A, (__v8hi)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_min_epu8(__m128i __A, __m128i __B) {
  return (__m128i)vec_min((__v16qu)__A, (__v16qu)__B);
}

#ifdef _ARCH_PWR8
/* Intrinsic functions that require PowerISA 2.07 minimum.  */

/* Return a mask created from the most significant bit of each 8-bit
   element in A.  */
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_movemask_epi8(__m128i __A) {
#ifdef _ARCH_PWR10
  return vec_extractm((__v16qu)__A);
#else
  __vector unsigned long long __result;
  static const __vector unsigned char __perm_mask = {
      0x78, 0x70, 0x68, 0x60, 0x58, 0x50, 0x48, 0x40,
      0x38, 0x30, 0x28, 0x20, 0x18, 0x10, 0x08, 0x00};

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

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mulhi_epu16(__m128i __A, __m128i __B) {
  __v4su __w0, __w1;
  __v16qu __xform1 = {
#ifdef __LITTLE_ENDIAN__
      0x02, 0x03, 0x12, 0x13, 0x06, 0x07, 0x16, 0x17, 0x0A,
      0x0B, 0x1A, 0x1B, 0x0E, 0x0F, 0x1E, 0x1F
#else
      0x00, 0x01, 0x10, 0x11, 0x04, 0x05, 0x14, 0x15, 0x08,
      0x09, 0x18, 0x19, 0x0C, 0x0D, 0x1C, 0x1D
#endif
  };

  __w0 = vec_vmuleuh((__v8hu)__A, (__v8hu)__B);
  __w1 = vec_vmulouh((__v8hu)__A, (__v8hu)__B);
  return (__m128i)vec_perm(__w0, __w1, __xform1);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_shufflehi_epi16(__m128i __A, const int __mask) {
  unsigned long __element_selector_98 = __mask & 0x03;
  unsigned long __element_selector_BA = (__mask >> 2) & 0x03;
  unsigned long __element_selector_DC = (__mask >> 4) & 0x03;
  unsigned long __element_selector_FE = (__mask >> 6) & 0x03;
  static const unsigned short __permute_selectors[4] = {
#ifdef __LITTLE_ENDIAN__
      0x0908, 0x0B0A, 0x0D0C, 0x0F0E
#else
      0x0809, 0x0A0B, 0x0C0D, 0x0E0F
#endif
  };
  __v2du __pmask =
#ifdef __LITTLE_ENDIAN__
      {0x1716151413121110UL, 0UL};
#else
      {0x1011121314151617UL, 0UL};
#endif
  __m64_union __t;
  __v2du __a, __r;

  __t.as_short[0] = __permute_selectors[__element_selector_98];
  __t.as_short[1] = __permute_selectors[__element_selector_BA];
  __t.as_short[2] = __permute_selectors[__element_selector_DC];
  __t.as_short[3] = __permute_selectors[__element_selector_FE];
  __pmask[1] = __t.as_m64;
  __a = (__v2du)__A;
  __r = vec_perm(__a, __a, (__vector unsigned char)__pmask);
  return (__m128i)__r;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_shufflelo_epi16(__m128i __A, const int __mask) {
  unsigned long __element_selector_10 = __mask & 0x03;
  unsigned long __element_selector_32 = (__mask >> 2) & 0x03;
  unsigned long __element_selector_54 = (__mask >> 4) & 0x03;
  unsigned long __element_selector_76 = (__mask >> 6) & 0x03;
  static const unsigned short __permute_selectors[4] = {
#ifdef __LITTLE_ENDIAN__
      0x0100, 0x0302, 0x0504, 0x0706
#else
      0x0001, 0x0203, 0x0405, 0x0607
#endif
  };
  __v2du __pmask =
#ifdef __LITTLE_ENDIAN__
      {0UL, 0x1f1e1d1c1b1a1918UL};
#else
      {0UL, 0x18191a1b1c1d1e1fUL};
#endif
  __m64_union __t;
  __v2du __a, __r;
  __t.as_short[0] = __permute_selectors[__element_selector_10];
  __t.as_short[1] = __permute_selectors[__element_selector_32];
  __t.as_short[2] = __permute_selectors[__element_selector_54];
  __t.as_short[3] = __permute_selectors[__element_selector_76];
  __pmask[0] = __t.as_m64;
  __a = (__v2du)__A;
  __r = vec_perm(__a, __a, (__vector unsigned char)__pmask);
  return (__m128i)__r;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_shuffle_epi32(__m128i __A, const int __mask) {
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
  __v4su __t;

  __t[0] = __permute_selectors[__element_selector_10];
  __t[1] = __permute_selectors[__element_selector_32];
  __t[2] = __permute_selectors[__element_selector_54] + 0x10101010;
  __t[3] = __permute_selectors[__element_selector_76] + 0x10101010;
  return (__m128i)vec_perm((__v4si)__A, (__v4si)__A,
                           (__vector unsigned char)__t);
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_maskmoveu_si128(__m128i __A, __m128i __B, char *__C) {
  __v2du __hibit = {0x7f7f7f7f7f7f7f7fUL, 0x7f7f7f7f7f7f7f7fUL};
  __v16qu __mask, __tmp;
  __m128i_u *__p = (__m128i_u *)__C;

  __tmp = (__v16qu)_mm_loadu_si128(__p);
  __mask = (__v16qu)vec_cmpgt((__v16qu)__B, (__v16qu)__hibit);
  __tmp = vec_sel(__tmp, (__v16qu)__A, __mask);
  _mm_storeu_si128(__p, (__m128i)__tmp);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_avg_epu8(__m128i __A, __m128i __B) {
  return (__m128i)vec_avg((__v16qu)__A, (__v16qu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_avg_epu16(__m128i __A, __m128i __B) {
  return (__m128i)vec_avg((__v8hu)__A, (__v8hu)__B);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sad_epu8(__m128i __A, __m128i __B) {
  __v16qu __a, __b;
  __v16qu __vabsdiff;
  __v4si __vsum;
  const __v4su __zero = {0, 0, 0, 0};
  __v4si __result;

  __a = (__v16qu)__A;
  __b = (__v16qu)__B;
#ifndef _ARCH_PWR9
  __v16qu __vmin = vec_min(__a, __b);
  __v16qu __vmax = vec_max(__a, __b);
  __vabsdiff = vec_sub(__vmax, __vmin);
#else
  __vabsdiff = vec_absd(__a, __b);
#endif
  /* Sum four groups of bytes into integers.  */
  __vsum = (__vector signed int)vec_sum4s(__vabsdiff, __zero);
#ifdef __LITTLE_ENDIAN__
  /* Sum across four integers with two integer results.  */
  __asm__("vsum2sws %0,%1,%2" : "=v"(__result) : "v"(__vsum), "v"(__zero));
  /* Note: vec_sum2s could be used here, but on little-endian, vector
     shifts are added that are not needed for this use-case.
     A vector shift to correctly position the 32-bit integer results
     (currently at [0] and [2]) to [1] and [3] would then need to be
     swapped back again since the desired results are two 64-bit
     integers ([1]|[0] and [3]|[2]).  Thus, no shift is performed.  */
#else
  /* Sum across four integers with two integer results.  */
  __result = vec_sum2s(__vsum, (__vector signed int)__zero);
  /* Rotate the sums into the correct position.  */
  __result = vec_sld(__result, __result, 6);
#endif
  return (__m128i)__result;
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_stream_si32(int *__A, int __B) {
  /* Use the data cache block touch for store transient.  */
  __asm__("dcbtstt 0,%0" : : "b"(__A) : "memory");
  *__A = __B;
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_stream_si64(long long int *__A, long long int __B) {
  /* Use the data cache block touch for store transient.  */
  __asm__("	dcbtstt	0,%0" : : "b"(__A) : "memory");
  *__A = __B;
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_stream_si128(__m128i *__A, __m128i __B) {
  /* Use the data cache block touch for store transient.  */
  __asm__("dcbtstt 0,%0" : : "b"(__A) : "memory");
  *__A = __B;
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_stream_pd(double *__A, __m128d __B) {
  /* Use the data cache block touch for store transient.  */
  __asm__("dcbtstt 0,%0" : : "b"(__A) : "memory");
  *(__m128d *)__A = __B;
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_clflush(void const *__A) {
  /* Use the data cache block flush.  */
  __asm__("dcbf 0,%0" : : "b"(__A) : "memory");
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_lfence(void) {
  /* Use light weight sync for load to load ordering.  */
  __atomic_thread_fence(__ATOMIC_RELEASE);
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_mfence(void) {
  /* Use heavy weight sync for any to any ordering.  */
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi32_si128(int __A) {
  return _mm_set_epi32(0, 0, 0, __A);
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi64_si128(long long __A) {
  return __extension__(__m128i)(__v2di){__A, 0LL};
}

/* Microsoft intrinsic.  */
extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_cvtsi64x_si128(long long __A) {
  return __extension__(__m128i)(__v2di){__A, 0LL};
}

/* Casts between various SP, DP, INT vector types.  Note that these do no
   conversion of values, they just change the type.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castpd_ps(__m128d __A) {
  return (__m128)__A;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castpd_si128(__m128d __A) {
  return (__m128i)__A;
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castps_pd(__m128 __A) {
  return (__m128d)__A;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castps_si128(__m128 __A) {
  return (__m128i)__A;
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castsi128_ps(__m128i __A) {
  return (__m128)__A;
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castsi128_pd(__m128i __A) {
  return (__m128d)__A;
}

#else
#include_next <emmintrin.h>
#endif /* defined(__powerpc64__) &&                                            \
        *   (defined(__linux__) || defined(__FreeBSD__) || defined(_AIX)) */

#endif /* EMMINTRIN_H_ */
