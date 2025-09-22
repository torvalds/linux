/*===---- __clang_cuda_cmath.h - Device-side CUDA cmath support ------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __CLANG_CUDA_CMATH_H__
#define __CLANG_CUDA_CMATH_H__
#ifndef __CUDA__
#error "This file is for CUDA compilation only."
#endif

#ifndef __OPENMP_NVPTX__
#include <limits>
#endif

// CUDA lets us use various std math functions on the device side.  This file
// works in concert with __clang_cuda_math_forward_declares.h to make this work.
//
// Specifically, the forward-declares header declares __device__ overloads for
// these functions in the global namespace, then pulls them into namespace std
// with 'using' statements.  Then this file implements those functions, after
// their implementations have been pulled in.
//
// It's important that we declare the functions in the global namespace and pull
// them into namespace std with using statements, as opposed to simply declaring
// these functions in namespace std, because our device functions need to
// overload the standard library functions, which may be declared in the global
// namespace or in std, depending on the degree of conformance of the stdlib
// implementation.  Declaring in the global namespace and pulling into namespace
// std covers all of the known knowns.

#ifdef __OPENMP_NVPTX__
#define __DEVICE__ static constexpr __attribute__((always_inline, nothrow))
#else
#define __DEVICE__ static __device__ __inline__ __attribute__((always_inline))
#endif

__DEVICE__ long long abs(long long __n) { return ::llabs(__n); }
__DEVICE__ long abs(long __n) { return ::labs(__n); }
__DEVICE__ float abs(float __x) { return ::fabsf(__x); }
__DEVICE__ double abs(double __x) { return ::fabs(__x); }
__DEVICE__ float acos(float __x) { return ::acosf(__x); }
__DEVICE__ float asin(float __x) { return ::asinf(__x); }
__DEVICE__ float atan(float __x) { return ::atanf(__x); }
__DEVICE__ float atan2(float __x, float __y) { return ::atan2f(__x, __y); }
__DEVICE__ float ceil(float __x) { return ::ceilf(__x); }
__DEVICE__ float cos(float __x) { return ::cosf(__x); }
__DEVICE__ float cosh(float __x) { return ::coshf(__x); }
__DEVICE__ float exp(float __x) { return ::expf(__x); }
__DEVICE__ float fabs(float __x) { return ::fabsf(__x); }
__DEVICE__ float floor(float __x) { return ::floorf(__x); }
__DEVICE__ float fmod(float __x, float __y) { return ::fmodf(__x, __y); }
__DEVICE__ int fpclassify(float __x) {
  return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL,
                              FP_ZERO, __x);
}
__DEVICE__ int fpclassify(double __x) {
  return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL,
                              FP_ZERO, __x);
}
__DEVICE__ float frexp(float __arg, int *__exp) {
  return ::frexpf(__arg, __exp);
}

// For inscrutable reasons, the CUDA headers define these functions for us on
// Windows.
#if !defined(_MSC_VER) || defined(__OPENMP_NVPTX__)

// For OpenMP we work around some old system headers that have non-conforming
// `isinf(float)` and `isnan(float)` implementations that return an `int`. We do
// this by providing two versions of these functions, differing only in the
// return type. To avoid conflicting definitions we disable implicit base
// function generation. That means we will end up with two specializations, one
// per type, but only one has a base function defined by the system header.
#if defined(__OPENMP_NVPTX__)
#pragma omp begin declare variant match(                                       \
    implementation = {extension(disable_implicit_base)})

// FIXME: We lack an extension to customize the mangling of the variants, e.g.,
//        add a suffix. This means we would clash with the names of the variants
//        (note that we do not create implicit base functions here). To avoid
//        this clash we add a new trait to some of them that is always true
//        (this is LLVM after all ;)). It will only influence the mangled name
//        of the variants inside the inner region and avoid the clash.
#pragma omp begin declare variant match(implementation = {vendor(llvm)})

__DEVICE__ int isinf(float __x) { return ::__isinff(__x); }
__DEVICE__ int isinf(double __x) { return ::__isinf(__x); }
__DEVICE__ int isfinite(float __x) { return ::__finitef(__x); }
__DEVICE__ int isfinite(double __x) { return ::__isfinited(__x); }
__DEVICE__ int isnan(float __x) { return ::__isnanf(__x); }
__DEVICE__ int isnan(double __x) { return ::__isnan(__x); }

#pragma omp end declare variant

#endif

__DEVICE__ bool isinf(float __x) { return ::__isinff(__x); }
__DEVICE__ bool isinf(double __x) { return ::__isinf(__x); }
__DEVICE__ bool isfinite(float __x) { return ::__finitef(__x); }
// For inscrutable reasons, __finite(), the double-precision version of
// __finitef, does not exist when compiling for MacOS.  __isfinited is available
// everywhere and is just as good.
__DEVICE__ bool isfinite(double __x) { return ::__isfinited(__x); }
__DEVICE__ bool isnan(float __x) { return ::__isnanf(__x); }
__DEVICE__ bool isnan(double __x) { return ::__isnan(__x); }

#if defined(__OPENMP_NVPTX__)
#pragma omp end declare variant
#endif

#endif

__DEVICE__ bool isgreater(float __x, float __y) {
  return __builtin_isgreater(__x, __y);
}
__DEVICE__ bool isgreater(double __x, double __y) {
  return __builtin_isgreater(__x, __y);
}
__DEVICE__ bool isgreaterequal(float __x, float __y) {
  return __builtin_isgreaterequal(__x, __y);
}
__DEVICE__ bool isgreaterequal(double __x, double __y) {
  return __builtin_isgreaterequal(__x, __y);
}
__DEVICE__ bool isless(float __x, float __y) {
  return __builtin_isless(__x, __y);
}
__DEVICE__ bool isless(double __x, double __y) {
  return __builtin_isless(__x, __y);
}
__DEVICE__ bool islessequal(float __x, float __y) {
  return __builtin_islessequal(__x, __y);
}
__DEVICE__ bool islessequal(double __x, double __y) {
  return __builtin_islessequal(__x, __y);
}
__DEVICE__ bool islessgreater(float __x, float __y) {
  return __builtin_islessgreater(__x, __y);
}
__DEVICE__ bool islessgreater(double __x, double __y) {
  return __builtin_islessgreater(__x, __y);
}
__DEVICE__ bool isnormal(float __x) { return __builtin_isnormal(__x); }
__DEVICE__ bool isnormal(double __x) { return __builtin_isnormal(__x); }
__DEVICE__ bool isunordered(float __x, float __y) {
  return __builtin_isunordered(__x, __y);
}
__DEVICE__ bool isunordered(double __x, double __y) {
  return __builtin_isunordered(__x, __y);
}
__DEVICE__ float ldexp(float __arg, int __exp) {
  return ::ldexpf(__arg, __exp);
}
__DEVICE__ float log(float __x) { return ::logf(__x); }
__DEVICE__ float log10(float __x) { return ::log10f(__x); }
__DEVICE__ float modf(float __x, float *__iptr) { return ::modff(__x, __iptr); }
__DEVICE__ float pow(float __base, float __exp) {
  return ::powf(__base, __exp);
}
__DEVICE__ float pow(float __base, int __iexp) {
  return ::powif(__base, __iexp);
}
__DEVICE__ double pow(double __base, int __iexp) {
  return ::powi(__base, __iexp);
}
__DEVICE__ bool signbit(float __x) { return ::__signbitf(__x); }
__DEVICE__ bool signbit(double __x) { return ::__signbitd(__x); }
__DEVICE__ float sin(float __x) { return ::sinf(__x); }
__DEVICE__ float sinh(float __x) { return ::sinhf(__x); }
__DEVICE__ float sqrt(float __x) { return ::sqrtf(__x); }
__DEVICE__ float tan(float __x) { return ::tanf(__x); }
__DEVICE__ float tanh(float __x) { return ::tanhf(__x); }

// There was a redefinition error for this this overload in CUDA mode.
// We restrict it to OpenMP mode for now, that is where it is actually needed
// anyway.
#ifdef __OPENMP_NVPTX__
__DEVICE__ float remquo(float __n, float __d, int *__q) {
  return ::remquof(__n, __d, __q);
}
#endif

// Notably missing above is nexttoward.  We omit it because
// libdevice doesn't provide an implementation, and we don't want to be in the
// business of implementing tricky libm functions in this header.

#ifndef __OPENMP_NVPTX__

// Now we've defined everything we promised we'd define in
// __clang_cuda_math_forward_declares.h.  We need to do two additional things to
// fix up our math functions.
//
// 1) Define __device__ overloads for e.g. sin(int).  The CUDA headers define
//    only sin(float) and sin(double), which means that e.g. sin(0) is
//    ambiguous.
//
// 2) Pull the __device__ overloads of "foobarf" math functions into namespace
//    std.  These are defined in the CUDA headers in the global namespace,
//    independent of everything else we've done here.

// We can't use std::enable_if, because we want to be pre-C++11 compatible.  But
// we go ahead and unconditionally define functions that are only available when
// compiling for C++11 to match the behavior of the CUDA headers.
template<bool __B, class __T = void>
struct __clang_cuda_enable_if {};

template <class __T> struct __clang_cuda_enable_if<true, __T> {
  typedef __T type;
};

// Defines an overload of __fn that accepts one integral argument, calls
// __fn((double)x), and returns __retty.
#define __CUDA_CLANG_FN_INTEGER_OVERLOAD_1(__retty, __fn)                      \
  template <typename __T>                                                      \
  __DEVICE__                                                                   \
      typename __clang_cuda_enable_if<std::numeric_limits<__T>::is_integer,    \
                                      __retty>::type                           \
      __fn(__T __x) {                                                          \
    return ::__fn((double)__x);                                                \
  }

// Defines an overload of __fn that accepts one two arithmetic arguments, calls
// __fn((double)x, (double)y), and returns a double.
//
// Note this is different from OVERLOAD_1, which generates an overload that
// accepts only *integral* arguments.
#define __CUDA_CLANG_FN_INTEGER_OVERLOAD_2(__retty, __fn)                      \
  template <typename __T1, typename __T2>                                      \
  __DEVICE__ typename __clang_cuda_enable_if<                                  \
      std::numeric_limits<__T1>::is_specialized &&                             \
          std::numeric_limits<__T2>::is_specialized,                           \
      __retty>::type                                                           \
  __fn(__T1 __x, __T2 __y) {                                                   \
    return __fn((double)__x, (double)__y);                                     \
  }

__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, acos)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, acosh)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, asin)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, asinh)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, atan)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, atan2);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, atanh)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, cbrt)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, ceil)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, copysign);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, cos)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, cosh)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, erf)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, erfc)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, exp)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, exp2)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, expm1)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, fabs)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, fdim);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, floor)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, fmax);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, fmin);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, fmod);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(int, fpclassify)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, hypot);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(int, ilogb)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(bool, isfinite)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(bool, isgreater);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(bool, isgreaterequal);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(bool, isinf);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(bool, isless);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(bool, islessequal);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(bool, islessgreater);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(bool, isnan);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(bool, isnormal)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(bool, isunordered);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, lgamma)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, log)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, log10)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, log1p)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, log2)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, logb)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(long long, llrint)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(long long, llround)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(long, lrint)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(long, lround)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, nearbyint);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, nextafter);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, pow);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_2(double, remainder);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, rint);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, round);
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(bool, signbit)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, sin)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, sinh)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, sqrt)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, tan)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, tanh)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, tgamma)
__CUDA_CLANG_FN_INTEGER_OVERLOAD_1(double, trunc);

#undef __CUDA_CLANG_FN_INTEGER_OVERLOAD_1
#undef __CUDA_CLANG_FN_INTEGER_OVERLOAD_2

// Overloads for functions that don't match the patterns expected by
// __CUDA_CLANG_FN_INTEGER_OVERLOAD_{1,2}.
template <typename __T1, typename __T2, typename __T3>
__DEVICE__ typename __clang_cuda_enable_if<
    std::numeric_limits<__T1>::is_specialized &&
        std::numeric_limits<__T2>::is_specialized &&
        std::numeric_limits<__T3>::is_specialized,
    double>::type
fma(__T1 __x, __T2 __y, __T3 __z) {
  return std::fma((double)__x, (double)__y, (double)__z);
}

template <typename __T>
__DEVICE__ typename __clang_cuda_enable_if<std::numeric_limits<__T>::is_integer,
                                           double>::type
frexp(__T __x, int *__exp) {
  return std::frexp((double)__x, __exp);
}

template <typename __T>
__DEVICE__ typename __clang_cuda_enable_if<std::numeric_limits<__T>::is_integer,
                                           double>::type
ldexp(__T __x, int __exp) {
  return std::ldexp((double)__x, __exp);
}

template <typename __T1, typename __T2>
__DEVICE__ typename __clang_cuda_enable_if<
    std::numeric_limits<__T1>::is_specialized &&
        std::numeric_limits<__T2>::is_specialized,
    double>::type
remquo(__T1 __x, __T2 __y, int *__quo) {
  return std::remquo((double)__x, (double)__y, __quo);
}

template <typename __T>
__DEVICE__ typename __clang_cuda_enable_if<std::numeric_limits<__T>::is_integer,
                                           double>::type
scalbln(__T __x, long __exp) {
  return std::scalbln((double)__x, __exp);
}

template <typename __T>
__DEVICE__ typename __clang_cuda_enable_if<std::numeric_limits<__T>::is_integer,
                                           double>::type
scalbn(__T __x, int __exp) {
  return std::scalbn((double)__x, __exp);
}

// We need to define these overloads in exactly the namespace our standard
// library uses (including the right inline namespace), otherwise they won't be
// picked up by other functions in the standard library (e.g. functions in
// <complex>).  Thus the ugliness below.
#ifdef _LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_NAMESPACE_STD
#else
namespace std {
#ifdef _GLIBCXX_BEGIN_NAMESPACE_VERSION
_GLIBCXX_BEGIN_NAMESPACE_VERSION
#endif
#endif

// Pull the new overloads we defined above into namespace std.
using ::acos;
using ::acosh;
using ::asin;
using ::asinh;
using ::atan;
using ::atan2;
using ::atanh;
using ::cbrt;
using ::ceil;
using ::copysign;
using ::cos;
using ::cosh;
using ::erf;
using ::erfc;
using ::exp;
using ::exp2;
using ::expm1;
using ::fabs;
using ::fdim;
using ::floor;
using ::fma;
using ::fmax;
using ::fmin;
using ::fmod;
using ::fpclassify;
using ::frexp;
using ::hypot;
using ::ilogb;
using ::isfinite;
using ::isgreater;
using ::isgreaterequal;
using ::isless;
using ::islessequal;
using ::islessgreater;
using ::isnormal;
using ::isunordered;
using ::ldexp;
using ::lgamma;
using ::llrint;
using ::llround;
using ::log;
using ::log10;
using ::log1p;
using ::log2;
using ::logb;
using ::lrint;
using ::lround;
using ::nearbyint;
using ::nextafter;
using ::pow;
using ::remainder;
using ::remquo;
using ::rint;
using ::round;
using ::scalbln;
using ::scalbn;
using ::signbit;
using ::sin;
using ::sinh;
using ::sqrt;
using ::tan;
using ::tanh;
using ::tgamma;
using ::trunc;

// Well this is fun: We need to pull these symbols in for libc++, but we can't
// pull them in with libstdc++, because its ::isinf and ::isnan are different
// than its std::isinf and std::isnan.
#ifndef __GLIBCXX__
using ::isinf;
using ::isnan;
#endif

// Finally, pull the "foobarf" functions that CUDA defines in its headers into
// namespace std.
using ::acosf;
using ::acoshf;
using ::asinf;
using ::asinhf;
using ::atan2f;
using ::atanf;
using ::atanhf;
using ::cbrtf;
using ::ceilf;
using ::copysignf;
using ::cosf;
using ::coshf;
using ::erfcf;
using ::erff;
using ::exp2f;
using ::expf;
using ::expm1f;
using ::fabsf;
using ::fdimf;
using ::floorf;
using ::fmaf;
using ::fmaxf;
using ::fminf;
using ::fmodf;
using ::frexpf;
using ::hypotf;
using ::ilogbf;
using ::ldexpf;
using ::lgammaf;
using ::llrintf;
using ::llroundf;
using ::log10f;
using ::log1pf;
using ::log2f;
using ::logbf;
using ::logf;
using ::lrintf;
using ::lroundf;
using ::modff;
using ::nearbyintf;
using ::nextafterf;
using ::powf;
using ::remainderf;
using ::remquof;
using ::rintf;
using ::roundf;
using ::scalblnf;
using ::scalbnf;
using ::sinf;
using ::sinhf;
using ::sqrtf;
using ::tanf;
using ::tanhf;
using ::tgammaf;
using ::truncf;

#ifdef _LIBCPP_END_NAMESPACE_STD
_LIBCPP_END_NAMESPACE_STD
#else
#ifdef _GLIBCXX_BEGIN_NAMESPACE_VERSION
_GLIBCXX_END_NAMESPACE_VERSION
#endif
} // namespace std
#endif

#endif // __OPENMP_NVPTX__

#undef __DEVICE__

#endif
