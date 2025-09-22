/*===---- __clang_hip_math.h - Device-side HIP math support ----------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __CLANG_HIP_MATH_H__
#define __CLANG_HIP_MATH_H__

#if !defined(__HIP__) && !defined(__OPENMP_AMDGCN__)
#error "This file is for HIP and OpenMP AMDGCN device compilation only."
#endif

#if !defined(__HIPCC_RTC__)
#include <limits.h>
#include <stdint.h>
#ifdef __OPENMP_AMDGCN__
#include <omp.h>
#endif
#endif // !defined(__HIPCC_RTC__)

#pragma push_macro("__DEVICE__")

#ifdef __OPENMP_AMDGCN__
#define __DEVICE__ static inline __attribute__((always_inline, nothrow))
#else
#define __DEVICE__ static __device__ inline __attribute__((always_inline))
#endif

// Device library provides fast low precision and slow full-recision
// implementations for some functions. Which one gets selected depends on
// __CLANG_GPU_APPROX_TRANSCENDENTALS__ which gets defined by clang if
// -ffast-math or -fgpu-approx-transcendentals are in effect.
#pragma push_macro("__FAST_OR_SLOW")
#if defined(__CLANG_GPU_APPROX_TRANSCENDENTALS__)
#define __FAST_OR_SLOW(fast, slow) fast
#else
#define __FAST_OR_SLOW(fast, slow) slow
#endif

// A few functions return bool type starting only in C++11.
#pragma push_macro("__RETURN_TYPE")
#ifdef __OPENMP_AMDGCN__
#define __RETURN_TYPE int
#else
#if defined(__cplusplus)
#define __RETURN_TYPE bool
#else
#define __RETURN_TYPE int
#endif
#endif // __OPENMP_AMDGCN__

#if defined (__cplusplus) && __cplusplus < 201103L
// emulate static_assert on type sizes
template<bool>
struct __compare_result{};
template<>
struct __compare_result<true> {
  static const __device__ bool valid;
};

__DEVICE__
void __suppress_unused_warning(bool b){};
template <unsigned int S, unsigned int T>
__DEVICE__ void __static_assert_equal_size() {
  __suppress_unused_warning(__compare_result<S == T>::valid);
}

#define __static_assert_type_size_equal(A, B) \
  __static_assert_equal_size<A,B>()

#else
#define __static_assert_type_size_equal(A,B) \
  static_assert((A) == (B), "")

#endif

__DEVICE__
uint64_t __make_mantissa_base8(const char *__tagp __attribute__((nonnull))) {
  uint64_t __r = 0;
  while (*__tagp != '\0') {
    char __tmp = *__tagp;

    if (__tmp >= '0' && __tmp <= '7')
      __r = (__r * 8u) + __tmp - '0';
    else
      return 0;

    ++__tagp;
  }

  return __r;
}

__DEVICE__
uint64_t __make_mantissa_base10(const char *__tagp __attribute__((nonnull))) {
  uint64_t __r = 0;
  while (*__tagp != '\0') {
    char __tmp = *__tagp;

    if (__tmp >= '0' && __tmp <= '9')
      __r = (__r * 10u) + __tmp - '0';
    else
      return 0;

    ++__tagp;
  }

  return __r;
}

__DEVICE__
uint64_t __make_mantissa_base16(const char *__tagp __attribute__((nonnull))) {
  uint64_t __r = 0;
  while (*__tagp != '\0') {
    char __tmp = *__tagp;

    if (__tmp >= '0' && __tmp <= '9')
      __r = (__r * 16u) + __tmp - '0';
    else if (__tmp >= 'a' && __tmp <= 'f')
      __r = (__r * 16u) + __tmp - 'a' + 10;
    else if (__tmp >= 'A' && __tmp <= 'F')
      __r = (__r * 16u) + __tmp - 'A' + 10;
    else
      return 0;

    ++__tagp;
  }

  return __r;
}

__DEVICE__
uint64_t __make_mantissa(const char *__tagp __attribute__((nonnull))) {
  if (*__tagp == '0') {
    ++__tagp;

    if (*__tagp == 'x' || *__tagp == 'X')
      return __make_mantissa_base16(__tagp);
    else
      return __make_mantissa_base8(__tagp);
  }

  return __make_mantissa_base10(__tagp);
}

// BEGIN FLOAT

// BEGIN INTRINSICS

__DEVICE__
float __cosf(float __x) { return __ocml_native_cos_f32(__x); }

__DEVICE__
float __exp10f(float __x) {
  const float __log2_10 = 0x1.a934f0p+1f;
  return __builtin_amdgcn_exp2f(__log2_10 * __x);
}

__DEVICE__
float __expf(float __x) {
  const float __log2_e = 0x1.715476p+0;
  return __builtin_amdgcn_exp2f(__log2_e * __x);
}

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
float __fadd_rd(float __x, float __y) { return __ocml_add_rtn_f32(__x, __y); }
__DEVICE__
float __fadd_rn(float __x, float __y) { return __ocml_add_rte_f32(__x, __y); }
__DEVICE__
float __fadd_ru(float __x, float __y) { return __ocml_add_rtp_f32(__x, __y); }
__DEVICE__
float __fadd_rz(float __x, float __y) { return __ocml_add_rtz_f32(__x, __y); }
#else
__DEVICE__
float __fadd_rn(float __x, float __y) { return __x + __y; }
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
float __fdiv_rd(float __x, float __y) { return __ocml_div_rtn_f32(__x, __y); }
__DEVICE__
float __fdiv_rn(float __x, float __y) { return __ocml_div_rte_f32(__x, __y); }
__DEVICE__
float __fdiv_ru(float __x, float __y) { return __ocml_div_rtp_f32(__x, __y); }
__DEVICE__
float __fdiv_rz(float __x, float __y) { return __ocml_div_rtz_f32(__x, __y); }
#else
__DEVICE__
float __fdiv_rn(float __x, float __y) { return __x / __y; }
#endif

__DEVICE__
float __fdividef(float __x, float __y) { return __x / __y; }

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
float __fmaf_rd(float __x, float __y, float __z) {
  return __ocml_fma_rtn_f32(__x, __y, __z);
}
__DEVICE__
float __fmaf_rn(float __x, float __y, float __z) {
  return __ocml_fma_rte_f32(__x, __y, __z);
}
__DEVICE__
float __fmaf_ru(float __x, float __y, float __z) {
  return __ocml_fma_rtp_f32(__x, __y, __z);
}
__DEVICE__
float __fmaf_rz(float __x, float __y, float __z) {
  return __ocml_fma_rtz_f32(__x, __y, __z);
}
#else
__DEVICE__
float __fmaf_rn(float __x, float __y, float __z) {
  return __builtin_fmaf(__x, __y, __z);
}
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
float __fmul_rd(float __x, float __y) { return __ocml_mul_rtn_f32(__x, __y); }
__DEVICE__
float __fmul_rn(float __x, float __y) { return __ocml_mul_rte_f32(__x, __y); }
__DEVICE__
float __fmul_ru(float __x, float __y) { return __ocml_mul_rtp_f32(__x, __y); }
__DEVICE__
float __fmul_rz(float __x, float __y) { return __ocml_mul_rtz_f32(__x, __y); }
#else
__DEVICE__
float __fmul_rn(float __x, float __y) { return __x * __y; }
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
float __frcp_rd(float __x) { return __ocml_div_rtn_f32(1.0f, __x); }
__DEVICE__
float __frcp_rn(float __x) { return __ocml_div_rte_f32(1.0f, __x); }
__DEVICE__
float __frcp_ru(float __x) { return __ocml_div_rtp_f32(1.0f, __x); }
__DEVICE__
float __frcp_rz(float __x) { return __ocml_div_rtz_f32(1.0f, __x); }
#else
__DEVICE__
float __frcp_rn(float __x) { return 1.0f / __x; }
#endif

__DEVICE__
float __frsqrt_rn(float __x) { return __builtin_amdgcn_rsqf(__x); }

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
float __fsqrt_rd(float __x) { return __ocml_sqrt_rtn_f32(__x); }
__DEVICE__
float __fsqrt_rn(float __x) { return __ocml_sqrt_rte_f32(__x); }
__DEVICE__
float __fsqrt_ru(float __x) { return __ocml_sqrt_rtp_f32(__x); }
__DEVICE__
float __fsqrt_rz(float __x) { return __ocml_sqrt_rtz_f32(__x); }
#else
__DEVICE__
float __fsqrt_rn(float __x) { return __ocml_native_sqrt_f32(__x); }
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
float __fsub_rd(float __x, float __y) { return __ocml_sub_rtn_f32(__x, __y); }
__DEVICE__
float __fsub_rn(float __x, float __y) { return __ocml_sub_rte_f32(__x, __y); }
__DEVICE__
float __fsub_ru(float __x, float __y) { return __ocml_sub_rtp_f32(__x, __y); }
__DEVICE__
float __fsub_rz(float __x, float __y) { return __ocml_sub_rtz_f32(__x, __y); }
#else
__DEVICE__
float __fsub_rn(float __x, float __y) { return __x - __y; }
#endif

__DEVICE__
float __log10f(float __x) { return __builtin_log10f(__x); }

__DEVICE__
float __log2f(float __x) { return __builtin_amdgcn_logf(__x); }

__DEVICE__
float __logf(float __x) { return __builtin_logf(__x); }

__DEVICE__
float __powf(float __x, float __y) { return __ocml_pow_f32(__x, __y); }

__DEVICE__
float __saturatef(float __x) { return (__x < 0) ? 0 : ((__x > 1) ? 1 : __x); }

__DEVICE__
void __sincosf(float __x, float *__sinptr, float *__cosptr) {
  *__sinptr = __ocml_native_sin_f32(__x);
  *__cosptr = __ocml_native_cos_f32(__x);
}

__DEVICE__
float __sinf(float __x) { return __ocml_native_sin_f32(__x); }

__DEVICE__
float __tanf(float __x) {
  return __sinf(__x) * __builtin_amdgcn_rcpf(__cosf(__x));
}
// END INTRINSICS

#if defined(__cplusplus)
__DEVICE__
int abs(int __x) {
  return __builtin_abs(__x);
}
__DEVICE__
long labs(long __x) {
  return __builtin_labs(__x);
}
__DEVICE__
long long llabs(long long __x) {
  return __builtin_llabs(__x);
}
#endif

__DEVICE__
float acosf(float __x) { return __ocml_acos_f32(__x); }

__DEVICE__
float acoshf(float __x) { return __ocml_acosh_f32(__x); }

__DEVICE__
float asinf(float __x) { return __ocml_asin_f32(__x); }

__DEVICE__
float asinhf(float __x) { return __ocml_asinh_f32(__x); }

__DEVICE__
float atan2f(float __x, float __y) { return __ocml_atan2_f32(__x, __y); }

__DEVICE__
float atanf(float __x) { return __ocml_atan_f32(__x); }

__DEVICE__
float atanhf(float __x) { return __ocml_atanh_f32(__x); }

__DEVICE__
float cbrtf(float __x) { return __ocml_cbrt_f32(__x); }

__DEVICE__
float ceilf(float __x) { return __builtin_ceilf(__x); }

__DEVICE__
float copysignf(float __x, float __y) { return __builtin_copysignf(__x, __y); }

__DEVICE__
float cosf(float __x) { return __FAST_OR_SLOW(__cosf, __ocml_cos_f32)(__x); }

__DEVICE__
float coshf(float __x) { return __ocml_cosh_f32(__x); }

__DEVICE__
float cospif(float __x) { return __ocml_cospi_f32(__x); }

__DEVICE__
float cyl_bessel_i0f(float __x) { return __ocml_i0_f32(__x); }

__DEVICE__
float cyl_bessel_i1f(float __x) { return __ocml_i1_f32(__x); }

__DEVICE__
float erfcf(float __x) { return __ocml_erfc_f32(__x); }

__DEVICE__
float erfcinvf(float __x) { return __ocml_erfcinv_f32(__x); }

__DEVICE__
float erfcxf(float __x) { return __ocml_erfcx_f32(__x); }

__DEVICE__
float erff(float __x) { return __ocml_erf_f32(__x); }

__DEVICE__
float erfinvf(float __x) { return __ocml_erfinv_f32(__x); }

__DEVICE__
float exp10f(float __x) { return __ocml_exp10_f32(__x); }

__DEVICE__
float exp2f(float __x) { return __builtin_exp2f(__x); }

__DEVICE__
float expf(float __x) { return __builtin_expf(__x); }

__DEVICE__
float expm1f(float __x) { return __ocml_expm1_f32(__x); }

__DEVICE__
float fabsf(float __x) { return __builtin_fabsf(__x); }

__DEVICE__
float fdimf(float __x, float __y) { return __ocml_fdim_f32(__x, __y); }

__DEVICE__
float fdividef(float __x, float __y) { return __x / __y; }

__DEVICE__
float floorf(float __x) { return __builtin_floorf(__x); }

__DEVICE__
float fmaf(float __x, float __y, float __z) {
  return __builtin_fmaf(__x, __y, __z);
}

__DEVICE__
float fmaxf(float __x, float __y) { return __builtin_fmaxf(__x, __y); }

__DEVICE__
float fminf(float __x, float __y) { return __builtin_fminf(__x, __y); }

__DEVICE__
float fmodf(float __x, float __y) { return __ocml_fmod_f32(__x, __y); }

__DEVICE__
float frexpf(float __x, int *__nptr) {
  return __builtin_frexpf(__x, __nptr);
}

__DEVICE__
float hypotf(float __x, float __y) { return __ocml_hypot_f32(__x, __y); }

__DEVICE__
int ilogbf(float __x) { return __ocml_ilogb_f32(__x); }

__DEVICE__
__RETURN_TYPE __finitef(float __x) { return __builtin_isfinite(__x); }

__DEVICE__
__RETURN_TYPE __isinff(float __x) { return __builtin_isinf(__x); }

__DEVICE__
__RETURN_TYPE __isnanf(float __x) { return __builtin_isnan(__x); }

__DEVICE__
float j0f(float __x) { return __ocml_j0_f32(__x); }

__DEVICE__
float j1f(float __x) { return __ocml_j1_f32(__x); }

__DEVICE__
float jnf(int __n, float __x) { // TODO: we could use Ahmes multiplication
                                // and the Miller & Brown algorithm
  //       for linear recurrences to get O(log n) steps, but it's unclear if
  //       it'd be beneficial in this case.
  if (__n == 0)
    return j0f(__x);
  if (__n == 1)
    return j1f(__x);

  float __x0 = j0f(__x);
  float __x1 = j1f(__x);
  for (int __i = 1; __i < __n; ++__i) {
    float __x2 = (2 * __i) / __x * __x1 - __x0;
    __x0 = __x1;
    __x1 = __x2;
  }

  return __x1;
}

__DEVICE__
float ldexpf(float __x, int __e) { return __builtin_amdgcn_ldexpf(__x, __e); }

__DEVICE__
float lgammaf(float __x) { return __ocml_lgamma_f32(__x); }

__DEVICE__
long long int llrintf(float __x) { return __builtin_rintf(__x); }

__DEVICE__
long long int llroundf(float __x) { return __builtin_roundf(__x); }

__DEVICE__
float log10f(float __x) { return __builtin_log10f(__x); }

__DEVICE__
float log1pf(float __x) { return __ocml_log1p_f32(__x); }

__DEVICE__
float log2f(float __x) { return __FAST_OR_SLOW(__log2f, __ocml_log2_f32)(__x); }

__DEVICE__
float logbf(float __x) { return __ocml_logb_f32(__x); }

__DEVICE__
float logf(float __x) { return __FAST_OR_SLOW(__logf, __ocml_log_f32)(__x); }

__DEVICE__
long int lrintf(float __x) { return __builtin_rintf(__x); }

__DEVICE__
long int lroundf(float __x) { return __builtin_roundf(__x); }

__DEVICE__
float modff(float __x, float *__iptr) {
  float __tmp;
#ifdef __OPENMP_AMDGCN__
#pragma omp allocate(__tmp) allocator(omp_thread_mem_alloc)
#endif
  float __r =
      __ocml_modf_f32(__x, (__attribute__((address_space(5))) float *)&__tmp);
  *__iptr = __tmp;
  return __r;
}

__DEVICE__
float nanf(const char *__tagp __attribute__((nonnull))) {
  union {
    float val;
    struct ieee_float {
      unsigned int mantissa : 22;
      unsigned int quiet : 1;
      unsigned int exponent : 8;
      unsigned int sign : 1;
    } bits;
  } __tmp;
  __static_assert_type_size_equal(sizeof(__tmp.val), sizeof(__tmp.bits));

  __tmp.bits.sign = 0u;
  __tmp.bits.exponent = ~0u;
  __tmp.bits.quiet = 1u;
  __tmp.bits.mantissa = __make_mantissa(__tagp);

  return __tmp.val;
}

__DEVICE__
float nearbyintf(float __x) { return __builtin_nearbyintf(__x); }

__DEVICE__
float nextafterf(float __x, float __y) {
  return __ocml_nextafter_f32(__x, __y);
}

__DEVICE__
float norm3df(float __x, float __y, float __z) {
  return __ocml_len3_f32(__x, __y, __z);
}

__DEVICE__
float norm4df(float __x, float __y, float __z, float __w) {
  return __ocml_len4_f32(__x, __y, __z, __w);
}

__DEVICE__
float normcdff(float __x) { return __ocml_ncdf_f32(__x); }

__DEVICE__
float normcdfinvf(float __x) { return __ocml_ncdfinv_f32(__x); }

__DEVICE__
float normf(int __dim,
            const float *__a) { // TODO: placeholder until OCML adds support.
  float __r = 0;
  while (__dim--) {
    __r += __a[0] * __a[0];
    ++__a;
  }

  return __builtin_sqrtf(__r);
}

__DEVICE__
float powf(float __x, float __y) { return __ocml_pow_f32(__x, __y); }

__DEVICE__
float powif(float __x, int __y) { return __ocml_pown_f32(__x, __y); }

__DEVICE__
float rcbrtf(float __x) { return __ocml_rcbrt_f32(__x); }

__DEVICE__
float remainderf(float __x, float __y) {
  return __ocml_remainder_f32(__x, __y);
}

__DEVICE__
float remquof(float __x, float __y, int *__quo) {
  int __tmp;
#ifdef __OPENMP_AMDGCN__
#pragma omp allocate(__tmp) allocator(omp_thread_mem_alloc)
#endif
  float __r = __ocml_remquo_f32(
      __x, __y, (__attribute__((address_space(5))) int *)&__tmp);
  *__quo = __tmp;

  return __r;
}

__DEVICE__
float rhypotf(float __x, float __y) { return __ocml_rhypot_f32(__x, __y); }

__DEVICE__
float rintf(float __x) { return __builtin_rintf(__x); }

__DEVICE__
float rnorm3df(float __x, float __y, float __z) {
  return __ocml_rlen3_f32(__x, __y, __z);
}

__DEVICE__
float rnorm4df(float __x, float __y, float __z, float __w) {
  return __ocml_rlen4_f32(__x, __y, __z, __w);
}

__DEVICE__
float rnormf(int __dim,
             const float *__a) { // TODO: placeholder until OCML adds support.
  float __r = 0;
  while (__dim--) {
    __r += __a[0] * __a[0];
    ++__a;
  }

  return __ocml_rsqrt_f32(__r);
}

__DEVICE__
float roundf(float __x) { return __builtin_roundf(__x); }

__DEVICE__
float rsqrtf(float __x) { return __ocml_rsqrt_f32(__x); }

__DEVICE__
float scalblnf(float __x, long int __n) {
  return (__n < INT_MAX) ? __builtin_amdgcn_ldexpf(__x, __n)
                         : __ocml_scalb_f32(__x, __n);
}

__DEVICE__
float scalbnf(float __x, int __n) { return __builtin_amdgcn_ldexpf(__x, __n); }

__DEVICE__
__RETURN_TYPE __signbitf(float __x) { return __builtin_signbitf(__x); }

__DEVICE__
void sincosf(float __x, float *__sinptr, float *__cosptr) {
  float __tmp;
#ifdef __OPENMP_AMDGCN__
#pragma omp allocate(__tmp) allocator(omp_thread_mem_alloc)
#endif
#ifdef __CLANG_CUDA_APPROX_TRANSCENDENTALS__
  __sincosf(__x, __sinptr, __cosptr);
#else
  *__sinptr =
      __ocml_sincos_f32(__x, (__attribute__((address_space(5))) float *)&__tmp);
  *__cosptr = __tmp;
#endif
}

__DEVICE__
void sincospif(float __x, float *__sinptr, float *__cosptr) {
  float __tmp;
#ifdef __OPENMP_AMDGCN__
#pragma omp allocate(__tmp) allocator(omp_thread_mem_alloc)
#endif
  *__sinptr = __ocml_sincospi_f32(
      __x, (__attribute__((address_space(5))) float *)&__tmp);
  *__cosptr = __tmp;
}

__DEVICE__
float sinf(float __x) { return __FAST_OR_SLOW(__sinf, __ocml_sin_f32)(__x); }

__DEVICE__
float sinhf(float __x) { return __ocml_sinh_f32(__x); }

__DEVICE__
float sinpif(float __x) { return __ocml_sinpi_f32(__x); }

__DEVICE__
float sqrtf(float __x) { return __builtin_sqrtf(__x); }

__DEVICE__
float tanf(float __x) { return __ocml_tan_f32(__x); }

__DEVICE__
float tanhf(float __x) { return __ocml_tanh_f32(__x); }

__DEVICE__
float tgammaf(float __x) { return __ocml_tgamma_f32(__x); }

__DEVICE__
float truncf(float __x) { return __builtin_truncf(__x); }

__DEVICE__
float y0f(float __x) { return __ocml_y0_f32(__x); }

__DEVICE__
float y1f(float __x) { return __ocml_y1_f32(__x); }

__DEVICE__
float ynf(int __n, float __x) { // TODO: we could use Ahmes multiplication
                                // and the Miller & Brown algorithm
  //       for linear recurrences to get O(log n) steps, but it's unclear if
  //       it'd be beneficial in this case. Placeholder until OCML adds
  //       support.
  if (__n == 0)
    return y0f(__x);
  if (__n == 1)
    return y1f(__x);

  float __x0 = y0f(__x);
  float __x1 = y1f(__x);
  for (int __i = 1; __i < __n; ++__i) {
    float __x2 = (2 * __i) / __x * __x1 - __x0;
    __x0 = __x1;
    __x1 = __x2;
  }

  return __x1;
}


// END FLOAT

// BEGIN DOUBLE
__DEVICE__
double acos(double __x) { return __ocml_acos_f64(__x); }

__DEVICE__
double acosh(double __x) { return __ocml_acosh_f64(__x); }

__DEVICE__
double asin(double __x) { return __ocml_asin_f64(__x); }

__DEVICE__
double asinh(double __x) { return __ocml_asinh_f64(__x); }

__DEVICE__
double atan(double __x) { return __ocml_atan_f64(__x); }

__DEVICE__
double atan2(double __x, double __y) { return __ocml_atan2_f64(__x, __y); }

__DEVICE__
double atanh(double __x) { return __ocml_atanh_f64(__x); }

__DEVICE__
double cbrt(double __x) { return __ocml_cbrt_f64(__x); }

__DEVICE__
double ceil(double __x) { return __builtin_ceil(__x); }

__DEVICE__
double copysign(double __x, double __y) {
  return __builtin_copysign(__x, __y);
}

__DEVICE__
double cos(double __x) { return __ocml_cos_f64(__x); }

__DEVICE__
double cosh(double __x) { return __ocml_cosh_f64(__x); }

__DEVICE__
double cospi(double __x) { return __ocml_cospi_f64(__x); }

__DEVICE__
double cyl_bessel_i0(double __x) { return __ocml_i0_f64(__x); }

__DEVICE__
double cyl_bessel_i1(double __x) { return __ocml_i1_f64(__x); }

__DEVICE__
double erf(double __x) { return __ocml_erf_f64(__x); }

__DEVICE__
double erfc(double __x) { return __ocml_erfc_f64(__x); }

__DEVICE__
double erfcinv(double __x) { return __ocml_erfcinv_f64(__x); }

__DEVICE__
double erfcx(double __x) { return __ocml_erfcx_f64(__x); }

__DEVICE__
double erfinv(double __x) { return __ocml_erfinv_f64(__x); }

__DEVICE__
double exp(double __x) { return __ocml_exp_f64(__x); }

__DEVICE__
double exp10(double __x) { return __ocml_exp10_f64(__x); }

__DEVICE__
double exp2(double __x) { return __ocml_exp2_f64(__x); }

__DEVICE__
double expm1(double __x) { return __ocml_expm1_f64(__x); }

__DEVICE__
double fabs(double __x) { return __builtin_fabs(__x); }

__DEVICE__
double fdim(double __x, double __y) { return __ocml_fdim_f64(__x, __y); }

__DEVICE__
double floor(double __x) { return __builtin_floor(__x); }

__DEVICE__
double fma(double __x, double __y, double __z) {
  return __builtin_fma(__x, __y, __z);
}

__DEVICE__
double fmax(double __x, double __y) { return __builtin_fmax(__x, __y); }

__DEVICE__
double fmin(double __x, double __y) { return __builtin_fmin(__x, __y); }

__DEVICE__
double fmod(double __x, double __y) { return __ocml_fmod_f64(__x, __y); }

__DEVICE__
double frexp(double __x, int *__nptr) {
  return __builtin_frexp(__x, __nptr);
}

__DEVICE__
double hypot(double __x, double __y) { return __ocml_hypot_f64(__x, __y); }

__DEVICE__
int ilogb(double __x) { return __ocml_ilogb_f64(__x); }

__DEVICE__
__RETURN_TYPE __finite(double __x) { return __builtin_isfinite(__x); }

__DEVICE__
__RETURN_TYPE __isinf(double __x) { return __builtin_isinf(__x); }

__DEVICE__
__RETURN_TYPE __isnan(double __x) { return __builtin_isnan(__x); }

__DEVICE__
double j0(double __x) { return __ocml_j0_f64(__x); }

__DEVICE__
double j1(double __x) { return __ocml_j1_f64(__x); }

__DEVICE__
double jn(int __n, double __x) { // TODO: we could use Ahmes multiplication
                                 // and the Miller & Brown algorithm
  //       for linear recurrences to get O(log n) steps, but it's unclear if
  //       it'd be beneficial in this case. Placeholder until OCML adds
  //       support.
  if (__n == 0)
    return j0(__x);
  if (__n == 1)
    return j1(__x);

  double __x0 = j0(__x);
  double __x1 = j1(__x);
  for (int __i = 1; __i < __n; ++__i) {
    double __x2 = (2 * __i) / __x * __x1 - __x0;
    __x0 = __x1;
    __x1 = __x2;
  }
  return __x1;
}

__DEVICE__
double ldexp(double __x, int __e) { return __builtin_amdgcn_ldexp(__x, __e); }

__DEVICE__
double lgamma(double __x) { return __ocml_lgamma_f64(__x); }

__DEVICE__
long long int llrint(double __x) { return __builtin_rint(__x); }

__DEVICE__
long long int llround(double __x) { return __builtin_round(__x); }

__DEVICE__
double log(double __x) { return __ocml_log_f64(__x); }

__DEVICE__
double log10(double __x) { return __ocml_log10_f64(__x); }

__DEVICE__
double log1p(double __x) { return __ocml_log1p_f64(__x); }

__DEVICE__
double log2(double __x) { return __ocml_log2_f64(__x); }

__DEVICE__
double logb(double __x) { return __ocml_logb_f64(__x); }

__DEVICE__
long int lrint(double __x) { return __builtin_rint(__x); }

__DEVICE__
long int lround(double __x) { return __builtin_round(__x); }

__DEVICE__
double modf(double __x, double *__iptr) {
  double __tmp;
#ifdef __OPENMP_AMDGCN__
#pragma omp allocate(__tmp) allocator(omp_thread_mem_alloc)
#endif
  double __r =
      __ocml_modf_f64(__x, (__attribute__((address_space(5))) double *)&__tmp);
  *__iptr = __tmp;

  return __r;
}

__DEVICE__
double nan(const char *__tagp) {
#if !_WIN32
  union {
    double val;
    struct ieee_double {
      uint64_t mantissa : 51;
      uint32_t quiet : 1;
      uint32_t exponent : 11;
      uint32_t sign : 1;
    } bits;
  } __tmp;
  __static_assert_type_size_equal(sizeof(__tmp.val), sizeof(__tmp.bits));

  __tmp.bits.sign = 0u;
  __tmp.bits.exponent = ~0u;
  __tmp.bits.quiet = 1u;
  __tmp.bits.mantissa = __make_mantissa(__tagp);

  return __tmp.val;
#else
  __static_assert_type_size_equal(sizeof(uint64_t), sizeof(double));
  uint64_t __val = __make_mantissa(__tagp);
  __val |= 0xFFF << 51;
  return *reinterpret_cast<double *>(&__val);
#endif
}

__DEVICE__
double nearbyint(double __x) { return __builtin_nearbyint(__x); }

__DEVICE__
double nextafter(double __x, double __y) {
  return __ocml_nextafter_f64(__x, __y);
}

__DEVICE__
double norm(int __dim,
            const double *__a) { // TODO: placeholder until OCML adds support.
  double __r = 0;
  while (__dim--) {
    __r += __a[0] * __a[0];
    ++__a;
  }

  return __builtin_sqrt(__r);
}

__DEVICE__
double norm3d(double __x, double __y, double __z) {
  return __ocml_len3_f64(__x, __y, __z);
}

__DEVICE__
double norm4d(double __x, double __y, double __z, double __w) {
  return __ocml_len4_f64(__x, __y, __z, __w);
}

__DEVICE__
double normcdf(double __x) { return __ocml_ncdf_f64(__x); }

__DEVICE__
double normcdfinv(double __x) { return __ocml_ncdfinv_f64(__x); }

__DEVICE__
double pow(double __x, double __y) { return __ocml_pow_f64(__x, __y); }

__DEVICE__
double powi(double __x, int __y) { return __ocml_pown_f64(__x, __y); }

__DEVICE__
double rcbrt(double __x) { return __ocml_rcbrt_f64(__x); }

__DEVICE__
double remainder(double __x, double __y) {
  return __ocml_remainder_f64(__x, __y);
}

__DEVICE__
double remquo(double __x, double __y, int *__quo) {
  int __tmp;
#ifdef __OPENMP_AMDGCN__
#pragma omp allocate(__tmp) allocator(omp_thread_mem_alloc)
#endif
  double __r = __ocml_remquo_f64(
      __x, __y, (__attribute__((address_space(5))) int *)&__tmp);
  *__quo = __tmp;

  return __r;
}

__DEVICE__
double rhypot(double __x, double __y) { return __ocml_rhypot_f64(__x, __y); }

__DEVICE__
double rint(double __x) { return __builtin_rint(__x); }

__DEVICE__
double rnorm(int __dim,
             const double *__a) { // TODO: placeholder until OCML adds support.
  double __r = 0;
  while (__dim--) {
    __r += __a[0] * __a[0];
    ++__a;
  }

  return __ocml_rsqrt_f64(__r);
}

__DEVICE__
double rnorm3d(double __x, double __y, double __z) {
  return __ocml_rlen3_f64(__x, __y, __z);
}

__DEVICE__
double rnorm4d(double __x, double __y, double __z, double __w) {
  return __ocml_rlen4_f64(__x, __y, __z, __w);
}

__DEVICE__
double round(double __x) { return __builtin_round(__x); }

__DEVICE__
double rsqrt(double __x) { return __ocml_rsqrt_f64(__x); }

__DEVICE__
double scalbln(double __x, long int __n) {
  return (__n < INT_MAX) ? __builtin_amdgcn_ldexp(__x, __n)
                         : __ocml_scalb_f64(__x, __n);
}
__DEVICE__
double scalbn(double __x, int __n) { return __builtin_amdgcn_ldexp(__x, __n); }

__DEVICE__
__RETURN_TYPE __signbit(double __x) { return __builtin_signbit(__x); }

__DEVICE__
double sin(double __x) { return __ocml_sin_f64(__x); }

__DEVICE__
void sincos(double __x, double *__sinptr, double *__cosptr) {
  double __tmp;
#ifdef __OPENMP_AMDGCN__
#pragma omp allocate(__tmp) allocator(omp_thread_mem_alloc)
#endif
  *__sinptr = __ocml_sincos_f64(
      __x, (__attribute__((address_space(5))) double *)&__tmp);
  *__cosptr = __tmp;
}

__DEVICE__
void sincospi(double __x, double *__sinptr, double *__cosptr) {
  double __tmp;
#ifdef __OPENMP_AMDGCN__
#pragma omp allocate(__tmp) allocator(omp_thread_mem_alloc)
#endif
  *__sinptr = __ocml_sincospi_f64(
      __x, (__attribute__((address_space(5))) double *)&__tmp);
  *__cosptr = __tmp;
}

__DEVICE__
double sinh(double __x) { return __ocml_sinh_f64(__x); }

__DEVICE__
double sinpi(double __x) { return __ocml_sinpi_f64(__x); }

__DEVICE__
double sqrt(double __x) { return __builtin_sqrt(__x); }

__DEVICE__
double tan(double __x) { return __ocml_tan_f64(__x); }

__DEVICE__
double tanh(double __x) { return __ocml_tanh_f64(__x); }

__DEVICE__
double tgamma(double __x) { return __ocml_tgamma_f64(__x); }

__DEVICE__
double trunc(double __x) { return __builtin_trunc(__x); }

__DEVICE__
double y0(double __x) { return __ocml_y0_f64(__x); }

__DEVICE__
double y1(double __x) { return __ocml_y1_f64(__x); }

__DEVICE__
double yn(int __n, double __x) { // TODO: we could use Ahmes multiplication
                                 // and the Miller & Brown algorithm
  //       for linear recurrences to get O(log n) steps, but it's unclear if
  //       it'd be beneficial in this case. Placeholder until OCML adds
  //       support.
  if (__n == 0)
    return y0(__x);
  if (__n == 1)
    return y1(__x);

  double __x0 = y0(__x);
  double __x1 = y1(__x);
  for (int __i = 1; __i < __n; ++__i) {
    double __x2 = (2 * __i) / __x * __x1 - __x0;
    __x0 = __x1;
    __x1 = __x2;
  }

  return __x1;
}

// BEGIN INTRINSICS
#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
double __dadd_rd(double __x, double __y) {
  return __ocml_add_rtn_f64(__x, __y);
}
__DEVICE__
double __dadd_rn(double __x, double __y) {
  return __ocml_add_rte_f64(__x, __y);
}
__DEVICE__
double __dadd_ru(double __x, double __y) {
  return __ocml_add_rtp_f64(__x, __y);
}
__DEVICE__
double __dadd_rz(double __x, double __y) {
  return __ocml_add_rtz_f64(__x, __y);
}
#else
__DEVICE__
double __dadd_rn(double __x, double __y) { return __x + __y; }
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
double __ddiv_rd(double __x, double __y) {
  return __ocml_div_rtn_f64(__x, __y);
}
__DEVICE__
double __ddiv_rn(double __x, double __y) {
  return __ocml_div_rte_f64(__x, __y);
}
__DEVICE__
double __ddiv_ru(double __x, double __y) {
  return __ocml_div_rtp_f64(__x, __y);
}
__DEVICE__
double __ddiv_rz(double __x, double __y) {
  return __ocml_div_rtz_f64(__x, __y);
}
#else
__DEVICE__
double __ddiv_rn(double __x, double __y) { return __x / __y; }
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
double __dmul_rd(double __x, double __y) {
  return __ocml_mul_rtn_f64(__x, __y);
}
__DEVICE__
double __dmul_rn(double __x, double __y) {
  return __ocml_mul_rte_f64(__x, __y);
}
__DEVICE__
double __dmul_ru(double __x, double __y) {
  return __ocml_mul_rtp_f64(__x, __y);
}
__DEVICE__
double __dmul_rz(double __x, double __y) {
  return __ocml_mul_rtz_f64(__x, __y);
}
#else
__DEVICE__
double __dmul_rn(double __x, double __y) { return __x * __y; }
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
double __drcp_rd(double __x) { return __ocml_div_rtn_f64(1.0, __x); }
__DEVICE__
double __drcp_rn(double __x) { return __ocml_div_rte_f64(1.0, __x); }
__DEVICE__
double __drcp_ru(double __x) { return __ocml_div_rtp_f64(1.0, __x); }
__DEVICE__
double __drcp_rz(double __x) { return __ocml_div_rtz_f64(1.0, __x); }
#else
__DEVICE__
double __drcp_rn(double __x) { return 1.0 / __x; }
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
double __dsqrt_rd(double __x) { return __ocml_sqrt_rtn_f64(__x); }
__DEVICE__
double __dsqrt_rn(double __x) { return __ocml_sqrt_rte_f64(__x); }
__DEVICE__
double __dsqrt_ru(double __x) { return __ocml_sqrt_rtp_f64(__x); }
__DEVICE__
double __dsqrt_rz(double __x) { return __ocml_sqrt_rtz_f64(__x); }
#else
__DEVICE__
double __dsqrt_rn(double __x) { return __builtin_sqrt(__x); }
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
double __dsub_rd(double __x, double __y) {
  return __ocml_sub_rtn_f64(__x, __y);
}
__DEVICE__
double __dsub_rn(double __x, double __y) {
  return __ocml_sub_rte_f64(__x, __y);
}
__DEVICE__
double __dsub_ru(double __x, double __y) {
  return __ocml_sub_rtp_f64(__x, __y);
}
__DEVICE__
double __dsub_rz(double __x, double __y) {
  return __ocml_sub_rtz_f64(__x, __y);
}
#else
__DEVICE__
double __dsub_rn(double __x, double __y) { return __x - __y; }
#endif

#if defined OCML_BASIC_ROUNDED_OPERATIONS
__DEVICE__
double __fma_rd(double __x, double __y, double __z) {
  return __ocml_fma_rtn_f64(__x, __y, __z);
}
__DEVICE__
double __fma_rn(double __x, double __y, double __z) {
  return __ocml_fma_rte_f64(__x, __y, __z);
}
__DEVICE__
double __fma_ru(double __x, double __y, double __z) {
  return __ocml_fma_rtp_f64(__x, __y, __z);
}
__DEVICE__
double __fma_rz(double __x, double __y, double __z) {
  return __ocml_fma_rtz_f64(__x, __y, __z);
}
#else
__DEVICE__
double __fma_rn(double __x, double __y, double __z) {
  return __builtin_fma(__x, __y, __z);
}
#endif
// END INTRINSICS
// END DOUBLE

// C only macros
#if !defined(__cplusplus) && __STDC_VERSION__ >= 201112L
#define isfinite(__x) _Generic((__x), float : __finitef, double : __finite)(__x)
#define isinf(__x) _Generic((__x), float : __isinff, double : __isinf)(__x)
#define isnan(__x) _Generic((__x), float : __isnanf, double : __isnan)(__x)
#define signbit(__x)                                                           \
  _Generic((__x), float : __signbitf, double : __signbit)(__x)
#endif // !defined(__cplusplus) && __STDC_VERSION__ >= 201112L

#if defined(__cplusplus)
template <class T> __DEVICE__ T min(T __arg1, T __arg2) {
  return (__arg1 < __arg2) ? __arg1 : __arg2;
}

template <class T> __DEVICE__ T max(T __arg1, T __arg2) {
  return (__arg1 > __arg2) ? __arg1 : __arg2;
}

__DEVICE__ int min(int __arg1, int __arg2) {
  return (__arg1 < __arg2) ? __arg1 : __arg2;
}
__DEVICE__ int max(int __arg1, int __arg2) {
  return (__arg1 > __arg2) ? __arg1 : __arg2;
}

__DEVICE__
float max(float __x, float __y) { return __builtin_fmaxf(__x, __y); }

__DEVICE__
double max(double __x, double __y) { return __builtin_fmax(__x, __y); }

__DEVICE__
float min(float __x, float __y) { return __builtin_fminf(__x, __y); }

__DEVICE__
double min(double __x, double __y) { return __builtin_fmin(__x, __y); }

#if !defined(__HIPCC_RTC__) && !defined(__OPENMP_AMDGCN__)
__host__ inline static int min(int __arg1, int __arg2) {
  return __arg1 < __arg2 ? __arg1 : __arg2;
}

__host__ inline static int max(int __arg1, int __arg2) {
  return __arg1 > __arg2 ? __arg1 : __arg2;
}
#endif // !defined(__HIPCC_RTC__) && !defined(__OPENMP_AMDGCN__)
#endif

#pragma pop_macro("__DEVICE__")
#pragma pop_macro("__RETURN_TYPE")
#pragma pop_macro("__FAST_OR_SLOW")

#endif // __CLANG_HIP_MATH_H__
