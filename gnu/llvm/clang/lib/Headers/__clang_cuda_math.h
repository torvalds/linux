/*===---- __clang_cuda_math.h - Device-side CUDA math support --------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __CLANG_CUDA_MATH_H__
#define __CLANG_CUDA_MATH_H__
#ifndef __CUDA__
#error "This file is for CUDA compilation only."
#endif

#ifndef __OPENMP_NVPTX__
#if CUDA_VERSION < 9000
#error This file is intended to be used with CUDA-9+ only.
#endif
#endif

// __DEVICE__ is a helper macro with common set of attributes for the wrappers
// we implement in this file. We need static in order to avoid emitting unused
// functions and __forceinline__ helps inlining these wrappers at -O1.
#pragma push_macro("__DEVICE__")
#ifdef __OPENMP_NVPTX__
#if defined(__cplusplus)
#define __DEVICE__ static constexpr __attribute__((always_inline, nothrow))
#else
#define __DEVICE__ static __attribute__((always_inline, nothrow))
#endif
#else
#define __DEVICE__ static __device__ __forceinline__
#endif

// Specialized version of __DEVICE__ for functions with void return type. Needed
// because the OpenMP overlay requires constexpr functions here but prior to
// c++14 void return functions could not be constexpr.
#pragma push_macro("__DEVICE_VOID__")
#if defined(__OPENMP_NVPTX__) && defined(__cplusplus) && __cplusplus < 201402L
#define __DEVICE_VOID__ static __attribute__((always_inline, nothrow))
#else
#define __DEVICE_VOID__ __DEVICE__
#endif

// libdevice provides fast low precision and slow full-recision implementations
// for some functions. Which one gets selected depends on
// __CLANG_CUDA_APPROX_TRANSCENDENTALS__ which gets defined by clang if
// -ffast-math or -fgpu-approx-transcendentals are in effect.
#pragma push_macro("__FAST_OR_SLOW")
#if defined(__CLANG_GPU_APPROX_TRANSCENDENTALS__)
#define __FAST_OR_SLOW(fast, slow) fast
#else
#define __FAST_OR_SLOW(fast, slow) slow
#endif

__DEVICE__ int abs(int __a) { return __nv_abs(__a); }
__DEVICE__ double fabs(double __a) { return __nv_fabs(__a); }
__DEVICE__ double acos(double __a) { return __nv_acos(__a); }
__DEVICE__ float acosf(float __a) { return __nv_acosf(__a); }
__DEVICE__ double acosh(double __a) { return __nv_acosh(__a); }
__DEVICE__ float acoshf(float __a) { return __nv_acoshf(__a); }
__DEVICE__ double asin(double __a) { return __nv_asin(__a); }
__DEVICE__ float asinf(float __a) { return __nv_asinf(__a); }
__DEVICE__ double asinh(double __a) { return __nv_asinh(__a); }
__DEVICE__ float asinhf(float __a) { return __nv_asinhf(__a); }
__DEVICE__ double atan(double __a) { return __nv_atan(__a); }
__DEVICE__ double atan2(double __a, double __b) { return __nv_atan2(__a, __b); }
__DEVICE__ float atan2f(float __a, float __b) { return __nv_atan2f(__a, __b); }
__DEVICE__ float atanf(float __a) { return __nv_atanf(__a); }
__DEVICE__ double atanh(double __a) { return __nv_atanh(__a); }
__DEVICE__ float atanhf(float __a) { return __nv_atanhf(__a); }
__DEVICE__ double cbrt(double __a) { return __nv_cbrt(__a); }
__DEVICE__ float cbrtf(float __a) { return __nv_cbrtf(__a); }
__DEVICE__ double ceil(double __a) { return __nv_ceil(__a); }
__DEVICE__ float ceilf(float __a) { return __nv_ceilf(__a); }
__DEVICE__ double copysign(double __a, double __b) {
  return __nv_copysign(__a, __b);
}
__DEVICE__ float copysignf(float __a, float __b) {
  return __nv_copysignf(__a, __b);
}
__DEVICE__ double cos(double __a) { return __nv_cos(__a); }
__DEVICE__ float cosf(float __a) {
  return __FAST_OR_SLOW(__nv_fast_cosf, __nv_cosf)(__a);
}
__DEVICE__ double cosh(double __a) { return __nv_cosh(__a); }
__DEVICE__ float coshf(float __a) { return __nv_coshf(__a); }
__DEVICE__ double cospi(double __a) { return __nv_cospi(__a); }
__DEVICE__ float cospif(float __a) { return __nv_cospif(__a); }
__DEVICE__ double cyl_bessel_i0(double __a) { return __nv_cyl_bessel_i0(__a); }
__DEVICE__ float cyl_bessel_i0f(float __a) { return __nv_cyl_bessel_i0f(__a); }
__DEVICE__ double cyl_bessel_i1(double __a) { return __nv_cyl_bessel_i1(__a); }
__DEVICE__ float cyl_bessel_i1f(float __a) { return __nv_cyl_bessel_i1f(__a); }
__DEVICE__ double erf(double __a) { return __nv_erf(__a); }
__DEVICE__ double erfc(double __a) { return __nv_erfc(__a); }
__DEVICE__ float erfcf(float __a) { return __nv_erfcf(__a); }
__DEVICE__ double erfcinv(double __a) { return __nv_erfcinv(__a); }
__DEVICE__ float erfcinvf(float __a) { return __nv_erfcinvf(__a); }
__DEVICE__ double erfcx(double __a) { return __nv_erfcx(__a); }
__DEVICE__ float erfcxf(float __a) { return __nv_erfcxf(__a); }
__DEVICE__ float erff(float __a) { return __nv_erff(__a); }
__DEVICE__ double erfinv(double __a) { return __nv_erfinv(__a); }
__DEVICE__ float erfinvf(float __a) { return __nv_erfinvf(__a); }
__DEVICE__ double exp(double __a) { return __nv_exp(__a); }
__DEVICE__ double exp10(double __a) { return __nv_exp10(__a); }
__DEVICE__ float exp10f(float __a) { return __nv_exp10f(__a); }
__DEVICE__ double exp2(double __a) { return __nv_exp2(__a); }
__DEVICE__ float exp2f(float __a) { return __nv_exp2f(__a); }
__DEVICE__ float expf(float __a) { return __nv_expf(__a); }
__DEVICE__ double expm1(double __a) { return __nv_expm1(__a); }
__DEVICE__ float expm1f(float __a) { return __nv_expm1f(__a); }
__DEVICE__ float fabsf(float __a) { return __nv_fabsf(__a); }
__DEVICE__ double fdim(double __a, double __b) { return __nv_fdim(__a, __b); }
__DEVICE__ float fdimf(float __a, float __b) { return __nv_fdimf(__a, __b); }
__DEVICE__ double fdivide(double __a, double __b) { return __a / __b; }
__DEVICE__ float fdividef(float __a, float __b) {
#if __FAST_MATH__ && !__CUDA_PREC_DIV
  return __nv_fast_fdividef(__a, __b);
#else
  return __a / __b;
#endif
}
__DEVICE__ double floor(double __f) { return __nv_floor(__f); }
__DEVICE__ float floorf(float __f) { return __nv_floorf(__f); }
__DEVICE__ double fma(double __a, double __b, double __c) {
  return __nv_fma(__a, __b, __c);
}
__DEVICE__ float fmaf(float __a, float __b, float __c) {
  return __nv_fmaf(__a, __b, __c);
}
__DEVICE__ double fmax(double __a, double __b) { return __nv_fmax(__a, __b); }
__DEVICE__ float fmaxf(float __a, float __b) { return __nv_fmaxf(__a, __b); }
__DEVICE__ double fmin(double __a, double __b) { return __nv_fmin(__a, __b); }
__DEVICE__ float fminf(float __a, float __b) { return __nv_fminf(__a, __b); }
__DEVICE__ double fmod(double __a, double __b) { return __nv_fmod(__a, __b); }
__DEVICE__ float fmodf(float __a, float __b) { return __nv_fmodf(__a, __b); }
__DEVICE__ double frexp(double __a, int *__b) { return __nv_frexp(__a, __b); }
__DEVICE__ float frexpf(float __a, int *__b) { return __nv_frexpf(__a, __b); }
__DEVICE__ double hypot(double __a, double __b) { return __nv_hypot(__a, __b); }
__DEVICE__ float hypotf(float __a, float __b) { return __nv_hypotf(__a, __b); }
__DEVICE__ int ilogb(double __a) { return __nv_ilogb(__a); }
__DEVICE__ int ilogbf(float __a) { return __nv_ilogbf(__a); }
__DEVICE__ double j0(double __a) { return __nv_j0(__a); }
__DEVICE__ float j0f(float __a) { return __nv_j0f(__a); }
__DEVICE__ double j1(double __a) { return __nv_j1(__a); }
__DEVICE__ float j1f(float __a) { return __nv_j1f(__a); }
__DEVICE__ double jn(int __n, double __a) { return __nv_jn(__n, __a); }
__DEVICE__ float jnf(int __n, float __a) { return __nv_jnf(__n, __a); }
#if defined(__LP64__) || defined(_WIN64)
__DEVICE__ long labs(long __a) { return __nv_llabs(__a); };
#else
__DEVICE__ long labs(long __a) { return __nv_abs(__a); };
#endif
__DEVICE__ double ldexp(double __a, int __b) { return __nv_ldexp(__a, __b); }
__DEVICE__ float ldexpf(float __a, int __b) { return __nv_ldexpf(__a, __b); }
__DEVICE__ double lgamma(double __a) { return __nv_lgamma(__a); }
__DEVICE__ float lgammaf(float __a) { return __nv_lgammaf(__a); }
__DEVICE__ long long llabs(long long __a) { return __nv_llabs(__a); }
__DEVICE__ long long llmax(long long __a, long long __b) {
  return __nv_llmax(__a, __b);
}
__DEVICE__ long long llmin(long long __a, long long __b) {
  return __nv_llmin(__a, __b);
}
__DEVICE__ long long llrint(double __a) { return __nv_llrint(__a); }
__DEVICE__ long long llrintf(float __a) { return __nv_llrintf(__a); }
__DEVICE__ long long llround(double __a) { return __nv_llround(__a); }
__DEVICE__ long long llroundf(float __a) { return __nv_llroundf(__a); }
__DEVICE__ double round(double __a) { return __nv_round(__a); }
__DEVICE__ float roundf(float __a) { return __nv_roundf(__a); }
__DEVICE__ double log(double __a) { return __nv_log(__a); }
__DEVICE__ double log10(double __a) { return __nv_log10(__a); }
__DEVICE__ float log10f(float __a) { return __nv_log10f(__a); }
__DEVICE__ double log1p(double __a) { return __nv_log1p(__a); }
__DEVICE__ float log1pf(float __a) { return __nv_log1pf(__a); }
__DEVICE__ double log2(double __a) { return __nv_log2(__a); }
__DEVICE__ float log2f(float __a) {
  return __FAST_OR_SLOW(__nv_fast_log2f, __nv_log2f)(__a);
}
__DEVICE__ double logb(double __a) { return __nv_logb(__a); }
__DEVICE__ float logbf(float __a) { return __nv_logbf(__a); }
__DEVICE__ float logf(float __a) {
  return __FAST_OR_SLOW(__nv_fast_logf, __nv_logf)(__a);
}
#if defined(__LP64__) || defined(_WIN64)
__DEVICE__ long lrint(double __a) { return llrint(__a); }
__DEVICE__ long lrintf(float __a) { return __float2ll_rn(__a); }
__DEVICE__ long lround(double __a) { return llround(__a); }
__DEVICE__ long lroundf(float __a) { return llroundf(__a); }
#else
__DEVICE__ long lrint(double __a) { return (long)rint(__a); }
__DEVICE__ long lrintf(float __a) { return __float2int_rn(__a); }
__DEVICE__ long lround(double __a) { return round(__a); }
__DEVICE__ long lroundf(float __a) { return roundf(__a); }
#endif
__DEVICE__ int max(int __a, int __b) { return __nv_max(__a, __b); }
__DEVICE__ int min(int __a, int __b) { return __nv_min(__a, __b); }
__DEVICE__ double modf(double __a, double *__b) { return __nv_modf(__a, __b); }
__DEVICE__ float modff(float __a, float *__b) { return __nv_modff(__a, __b); }
__DEVICE__ double nearbyint(double __a) { return __builtin_nearbyint(__a); }
__DEVICE__ float nearbyintf(float __a) { return __builtin_nearbyintf(__a); }
__DEVICE__ double nextafter(double __a, double __b) {
  return __nv_nextafter(__a, __b);
}
__DEVICE__ float nextafterf(float __a, float __b) {
  return __nv_nextafterf(__a, __b);
}
__DEVICE__ double norm(int __dim, const double *__t) {
  return __nv_norm(__dim, __t);
}
__DEVICE__ double norm3d(double __a, double __b, double __c) {
  return __nv_norm3d(__a, __b, __c);
}
__DEVICE__ float norm3df(float __a, float __b, float __c) {
  return __nv_norm3df(__a, __b, __c);
}
__DEVICE__ double norm4d(double __a, double __b, double __c, double __d) {
  return __nv_norm4d(__a, __b, __c, __d);
}
__DEVICE__ float norm4df(float __a, float __b, float __c, float __d) {
  return __nv_norm4df(__a, __b, __c, __d);
}
__DEVICE__ double normcdf(double __a) { return __nv_normcdf(__a); }
__DEVICE__ float normcdff(float __a) { return __nv_normcdff(__a); }
__DEVICE__ double normcdfinv(double __a) { return __nv_normcdfinv(__a); }
__DEVICE__ float normcdfinvf(float __a) { return __nv_normcdfinvf(__a); }
__DEVICE__ float normf(int __dim, const float *__t) {
  return __nv_normf(__dim, __t);
}
__DEVICE__ double pow(double __a, double __b) { return __nv_pow(__a, __b); }
__DEVICE__ float powf(float __a, float __b) { return __nv_powf(__a, __b); }
__DEVICE__ double powi(double __a, int __b) { return __nv_powi(__a, __b); }
__DEVICE__ float powif(float __a, int __b) { return __nv_powif(__a, __b); }
__DEVICE__ double rcbrt(double __a) { return __nv_rcbrt(__a); }
__DEVICE__ float rcbrtf(float __a) { return __nv_rcbrtf(__a); }
__DEVICE__ double remainder(double __a, double __b) {
  return __nv_remainder(__a, __b);
}
__DEVICE__ float remainderf(float __a, float __b) {
  return __nv_remainderf(__a, __b);
}
__DEVICE__ double remquo(double __a, double __b, int *__c) {
  return __nv_remquo(__a, __b, __c);
}
__DEVICE__ float remquof(float __a, float __b, int *__c) {
  return __nv_remquof(__a, __b, __c);
}
__DEVICE__ double rhypot(double __a, double __b) {
  return __nv_rhypot(__a, __b);
}
__DEVICE__ float rhypotf(float __a, float __b) {
  return __nv_rhypotf(__a, __b);
}
// __nv_rint* in libdevice is buggy and produces incorrect results.
__DEVICE__ double rint(double __a) { return __builtin_rint(__a); }
__DEVICE__ float rintf(float __a) { return __builtin_rintf(__a); }
__DEVICE__ double rnorm(int __a, const double *__b) {
  return __nv_rnorm(__a, __b);
}
__DEVICE__ double rnorm3d(double __a, double __b, double __c) {
  return __nv_rnorm3d(__a, __b, __c);
}
__DEVICE__ float rnorm3df(float __a, float __b, float __c) {
  return __nv_rnorm3df(__a, __b, __c);
}
__DEVICE__ double rnorm4d(double __a, double __b, double __c, double __d) {
  return __nv_rnorm4d(__a, __b, __c, __d);
}
__DEVICE__ float rnorm4df(float __a, float __b, float __c, float __d) {
  return __nv_rnorm4df(__a, __b, __c, __d);
}
__DEVICE__ float rnormf(int __dim, const float *__t) {
  return __nv_rnormf(__dim, __t);
}
__DEVICE__ double rsqrt(double __a) { return __nv_rsqrt(__a); }
__DEVICE__ float rsqrtf(float __a) { return __nv_rsqrtf(__a); }
__DEVICE__ double scalbn(double __a, int __b) { return __nv_scalbn(__a, __b); }
__DEVICE__ float scalbnf(float __a, int __b) { return __nv_scalbnf(__a, __b); }
__DEVICE__ double scalbln(double __a, long __b) {
  if (__b > INT_MAX)
    return __a > 0 ? HUGE_VAL : -HUGE_VAL;
  if (__b < INT_MIN)
    return __a > 0 ? 0.0 : -0.0;
  return scalbn(__a, (int)__b);
}
__DEVICE__ float scalblnf(float __a, long __b) {
  if (__b > INT_MAX)
    return __a > 0 ? HUGE_VALF : -HUGE_VALF;
  if (__b < INT_MIN)
    return __a > 0 ? 0.f : -0.f;
  return scalbnf(__a, (int)__b);
}
__DEVICE__ double sin(double __a) { return __nv_sin(__a); }
__DEVICE_VOID__ void sincos(double __a, double *__s, double *__c) {
  return __nv_sincos(__a, __s, __c);
}
__DEVICE_VOID__ void sincosf(float __a, float *__s, float *__c) {
  return __FAST_OR_SLOW(__nv_fast_sincosf, __nv_sincosf)(__a, __s, __c);
}
__DEVICE_VOID__ void sincospi(double __a, double *__s, double *__c) {
  return __nv_sincospi(__a, __s, __c);
}
__DEVICE_VOID__ void sincospif(float __a, float *__s, float *__c) {
  return __nv_sincospif(__a, __s, __c);
}
__DEVICE__ float sinf(float __a) {
  return __FAST_OR_SLOW(__nv_fast_sinf, __nv_sinf)(__a);
}
__DEVICE__ double sinh(double __a) { return __nv_sinh(__a); }
__DEVICE__ float sinhf(float __a) { return __nv_sinhf(__a); }
__DEVICE__ double sinpi(double __a) { return __nv_sinpi(__a); }
__DEVICE__ float sinpif(float __a) { return __nv_sinpif(__a); }
__DEVICE__ double sqrt(double __a) { return __nv_sqrt(__a); }
__DEVICE__ float sqrtf(float __a) { return __nv_sqrtf(__a); }
__DEVICE__ double tan(double __a) { return __nv_tan(__a); }
__DEVICE__ float tanf(float __a) { return __nv_tanf(__a); }
__DEVICE__ double tanh(double __a) { return __nv_tanh(__a); }
__DEVICE__ float tanhf(float __a) { return __nv_tanhf(__a); }
__DEVICE__ double tgamma(double __a) { return __nv_tgamma(__a); }
__DEVICE__ float tgammaf(float __a) { return __nv_tgammaf(__a); }
__DEVICE__ double trunc(double __a) { return __nv_trunc(__a); }
__DEVICE__ float truncf(float __a) { return __nv_truncf(__a); }
__DEVICE__ unsigned long long ullmax(unsigned long long __a,
                                     unsigned long long __b) {
  return __nv_ullmax(__a, __b);
}
__DEVICE__ unsigned long long ullmin(unsigned long long __a,
                                     unsigned long long __b) {
  return __nv_ullmin(__a, __b);
}
__DEVICE__ unsigned int umax(unsigned int __a, unsigned int __b) {
  return __nv_umax(__a, __b);
}
__DEVICE__ unsigned int umin(unsigned int __a, unsigned int __b) {
  return __nv_umin(__a, __b);
}
__DEVICE__ double y0(double __a) { return __nv_y0(__a); }
__DEVICE__ float y0f(float __a) { return __nv_y0f(__a); }
__DEVICE__ double y1(double __a) { return __nv_y1(__a); }
__DEVICE__ float y1f(float __a) { return __nv_y1f(__a); }
__DEVICE__ double yn(int __a, double __b) { return __nv_yn(__a, __b); }
__DEVICE__ float ynf(int __a, float __b) { return __nv_ynf(__a, __b); }

#pragma pop_macro("__DEVICE__")
#pragma pop_macro("__DEVICE_VOID__")
#pragma pop_macro("__FAST_OR_SLOW")

#endif // __CLANG_CUDA_MATH_H__
