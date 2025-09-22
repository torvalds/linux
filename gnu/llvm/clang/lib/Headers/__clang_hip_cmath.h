/*===---- __clang_hip_cmath.h - HIP cmath decls -----------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __CLANG_HIP_CMATH_H__
#define __CLANG_HIP_CMATH_H__

#if !defined(__HIP__) && !defined(__OPENMP_AMDGCN__)
#error "This file is for HIP and OpenMP AMDGCN device compilation only."
#endif

#if !defined(__HIPCC_RTC__)
#if defined(__cplusplus)
#include <limits>
#include <type_traits>
#include <utility>
#endif
#include <limits.h>
#include <stdint.h>
#endif // !defined(__HIPCC_RTC__)

#pragma push_macro("__DEVICE__")
#pragma push_macro("__CONSTEXPR__")
#ifdef __OPENMP_AMDGCN__
#define __DEVICE__ static __attribute__((always_inline, nothrow))
#define __CONSTEXPR__ constexpr
#else
#define __DEVICE__ static __device__ inline __attribute__((always_inline))
#define __CONSTEXPR__
#endif // __OPENMP_AMDGCN__

// Start with functions that cannot be defined by DEF macros below.
#if defined(__cplusplus)
#if defined __OPENMP_AMDGCN__
__DEVICE__ __CONSTEXPR__ float fabs(float __x) { return ::fabsf(__x); }
__DEVICE__ __CONSTEXPR__ float sin(float __x) { return ::sinf(__x); }
__DEVICE__ __CONSTEXPR__ float cos(float __x) { return ::cosf(__x); }
#endif
__DEVICE__ __CONSTEXPR__ double abs(double __x) { return ::fabs(__x); }
__DEVICE__ __CONSTEXPR__ float abs(float __x) { return ::fabsf(__x); }
__DEVICE__ __CONSTEXPR__ long long abs(long long __n) { return ::llabs(__n); }
__DEVICE__ __CONSTEXPR__ long abs(long __n) { return ::labs(__n); }
__DEVICE__ __CONSTEXPR__ float fma(float __x, float __y, float __z) {
  return ::fmaf(__x, __y, __z);
}
#if !defined(__HIPCC_RTC__)
// The value returned by fpclassify is platform dependent, therefore it is not
// supported by hipRTC.
__DEVICE__ __CONSTEXPR__ int fpclassify(float __x) {
  return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL,
                              FP_ZERO, __x);
}
__DEVICE__ __CONSTEXPR__ int fpclassify(double __x) {
  return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL,
                              FP_ZERO, __x);
}
#endif // !defined(__HIPCC_RTC__)

__DEVICE__ __CONSTEXPR__ float frexp(float __arg, int *__exp) {
  return ::frexpf(__arg, __exp);
}

#if defined(__OPENMP_AMDGCN__)
// For OpenMP we work around some old system headers that have non-conforming
// `isinf(float)` and `isnan(float)` implementations that return an `int`. We do
// this by providing two versions of these functions, differing only in the
// return type. To avoid conflicting definitions we disable implicit base
// function generation. That means we will end up with two specializations, one
// per type, but only one has a base function defined by the system header.
#pragma omp begin declare variant match(                                       \
    implementation = {extension(disable_implicit_base)})

// FIXME: We lack an extension to customize the mangling of the variants, e.g.,
//        add a suffix. This means we would clash with the names of the variants
//        (note that we do not create implicit base functions here). To avoid
//        this clash we add a new trait to some of them that is always true
//        (this is LLVM after all ;)). It will only influence the mangled name
//        of the variants inside the inner region and avoid the clash.
#pragma omp begin declare variant match(implementation = {vendor(llvm)})

__DEVICE__ __CONSTEXPR__ int isinf(float __x) { return ::__isinff(__x); }
__DEVICE__ __CONSTEXPR__ int isinf(double __x) { return ::__isinf(__x); }
__DEVICE__ __CONSTEXPR__ int isfinite(float __x) { return ::__finitef(__x); }
__DEVICE__ __CONSTEXPR__ int isfinite(double __x) { return ::__finite(__x); }
__DEVICE__ __CONSTEXPR__ int isnan(float __x) { return ::__isnanf(__x); }
__DEVICE__ __CONSTEXPR__ int isnan(double __x) { return ::__isnan(__x); }

#pragma omp end declare variant
#endif // defined(__OPENMP_AMDGCN__)

__DEVICE__ __CONSTEXPR__ bool isinf(float __x) { return ::__isinff(__x); }
__DEVICE__ __CONSTEXPR__ bool isinf(double __x) { return ::__isinf(__x); }
__DEVICE__ __CONSTEXPR__ bool isfinite(float __x) { return ::__finitef(__x); }
__DEVICE__ __CONSTEXPR__ bool isfinite(double __x) { return ::__finite(__x); }
__DEVICE__ __CONSTEXPR__ bool isnan(float __x) { return ::__isnanf(__x); }
__DEVICE__ __CONSTEXPR__ bool isnan(double __x) { return ::__isnan(__x); }

#if defined(__OPENMP_AMDGCN__)
#pragma omp end declare variant
#endif // defined(__OPENMP_AMDGCN__)

__DEVICE__ __CONSTEXPR__ bool isgreater(float __x, float __y) {
  return __builtin_isgreater(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool isgreater(double __x, double __y) {
  return __builtin_isgreater(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool isgreaterequal(float __x, float __y) {
  return __builtin_isgreaterequal(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool isgreaterequal(double __x, double __y) {
  return __builtin_isgreaterequal(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool isless(float __x, float __y) {
  return __builtin_isless(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool isless(double __x, double __y) {
  return __builtin_isless(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool islessequal(float __x, float __y) {
  return __builtin_islessequal(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool islessequal(double __x, double __y) {
  return __builtin_islessequal(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool islessgreater(float __x, float __y) {
  return __builtin_islessgreater(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool islessgreater(double __x, double __y) {
  return __builtin_islessgreater(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool isnormal(float __x) {
  return __builtin_isnormal(__x);
}
__DEVICE__ __CONSTEXPR__ bool isnormal(double __x) {
  return __builtin_isnormal(__x);
}
__DEVICE__ __CONSTEXPR__ bool isunordered(float __x, float __y) {
  return __builtin_isunordered(__x, __y);
}
__DEVICE__ __CONSTEXPR__ bool isunordered(double __x, double __y) {
  return __builtin_isunordered(__x, __y);
}
__DEVICE__ __CONSTEXPR__ float modf(float __x, float *__iptr) {
  return ::modff(__x, __iptr);
}
__DEVICE__ __CONSTEXPR__ float pow(float __base, int __iexp) {
  return ::powif(__base, __iexp);
}
__DEVICE__ __CONSTEXPR__ double pow(double __base, int __iexp) {
  return ::powi(__base, __iexp);
}
__DEVICE__ __CONSTEXPR__ float remquo(float __x, float __y, int *__quo) {
  return ::remquof(__x, __y, __quo);
}
__DEVICE__ __CONSTEXPR__ float scalbln(float __x, long int __n) {
  return ::scalblnf(__x, __n);
}
__DEVICE__ __CONSTEXPR__ bool signbit(float __x) { return ::__signbitf(__x); }
__DEVICE__ __CONSTEXPR__ bool signbit(double __x) { return ::__signbit(__x); }

// Notably missing above is nexttoward.  We omit it because
// ocml doesn't provide an implementation, and we don't want to be in the
// business of implementing tricky libm functions in this header.

// Other functions.
__DEVICE__ __CONSTEXPR__ _Float16 fma(_Float16 __x, _Float16 __y,
                                      _Float16 __z) {
  return __builtin_fmaf16(__x, __y, __z);
}
__DEVICE__ __CONSTEXPR__ _Float16 pow(_Float16 __base, int __iexp) {
  return __ocml_pown_f16(__base, __iexp);
}

#ifndef __OPENMP_AMDGCN__
// BEGIN DEF_FUN and HIP_OVERLOAD

// BEGIN DEF_FUN

#pragma push_macro("__DEF_FUN1")
#pragma push_macro("__DEF_FUN2")
#pragma push_macro("__DEF_FUN2_FI")

// Define cmath functions with float argument and returns __retty.
#define __DEF_FUN1(__retty, __func)                                            \
  __DEVICE__ __CONSTEXPR__ __retty __func(float __x) { return __func##f(__x); }

// Define cmath functions with two float arguments and returns __retty.
#define __DEF_FUN2(__retty, __func)                                            \
  __DEVICE__ __CONSTEXPR__ __retty __func(float __x, float __y) {              \
    return __func##f(__x, __y);                                                \
  }

// Define cmath functions with a float and an int argument and returns __retty.
#define __DEF_FUN2_FI(__retty, __func)                                         \
  __DEVICE__ __CONSTEXPR__ __retty __func(float __x, int __y) {                \
    return __func##f(__x, __y);                                                \
  }

__DEF_FUN1(float, acos)
__DEF_FUN1(float, acosh)
__DEF_FUN1(float, asin)
__DEF_FUN1(float, asinh)
__DEF_FUN1(float, atan)
__DEF_FUN2(float, atan2)
__DEF_FUN1(float, atanh)
__DEF_FUN1(float, cbrt)
__DEF_FUN1(float, ceil)
__DEF_FUN2(float, copysign)
__DEF_FUN1(float, cos)
__DEF_FUN1(float, cosh)
__DEF_FUN1(float, erf)
__DEF_FUN1(float, erfc)
__DEF_FUN1(float, exp)
__DEF_FUN1(float, exp2)
__DEF_FUN1(float, expm1)
__DEF_FUN1(float, fabs)
__DEF_FUN2(float, fdim)
__DEF_FUN1(float, floor)
__DEF_FUN2(float, fmax)
__DEF_FUN2(float, fmin)
__DEF_FUN2(float, fmod)
__DEF_FUN2(float, hypot)
__DEF_FUN1(int, ilogb)
__DEF_FUN2_FI(float, ldexp)
__DEF_FUN1(float, lgamma)
__DEF_FUN1(float, log)
__DEF_FUN1(float, log10)
__DEF_FUN1(float, log1p)
__DEF_FUN1(float, log2)
__DEF_FUN1(float, logb)
__DEF_FUN1(long long, llrint)
__DEF_FUN1(long long, llround)
__DEF_FUN1(long, lrint)
__DEF_FUN1(long, lround)
__DEF_FUN1(float, nearbyint)
__DEF_FUN2(float, nextafter)
__DEF_FUN2(float, pow)
__DEF_FUN2(float, remainder)
__DEF_FUN1(float, rint)
__DEF_FUN1(float, round)
__DEF_FUN2_FI(float, scalbn)
__DEF_FUN1(float, sin)
__DEF_FUN1(float, sinh)
__DEF_FUN1(float, sqrt)
__DEF_FUN1(float, tan)
__DEF_FUN1(float, tanh)
__DEF_FUN1(float, tgamma)
__DEF_FUN1(float, trunc)

#pragma pop_macro("__DEF_FUN1")
#pragma pop_macro("__DEF_FUN2")
#pragma pop_macro("__DEF_FUN2_FI")

// END DEF_FUN

// BEGIN HIP_OVERLOAD

#pragma push_macro("__HIP_OVERLOAD1")
#pragma push_macro("__HIP_OVERLOAD2")

// __hip_enable_if::type is a type function which returns __T if __B is true.
template <bool __B, class __T = void> struct __hip_enable_if {};

template <class __T> struct __hip_enable_if<true, __T> { typedef __T type; };

namespace __hip {
template <class _Tp> struct is_integral {
  enum { value = 0 };
};
template <> struct is_integral<bool> {
  enum { value = 1 };
};
template <> struct is_integral<char> {
  enum { value = 1 };
};
template <> struct is_integral<signed char> {
  enum { value = 1 };
};
template <> struct is_integral<unsigned char> {
  enum { value = 1 };
};
template <> struct is_integral<wchar_t> {
  enum { value = 1 };
};
template <> struct is_integral<short> {
  enum { value = 1 };
};
template <> struct is_integral<unsigned short> {
  enum { value = 1 };
};
template <> struct is_integral<int> {
  enum { value = 1 };
};
template <> struct is_integral<unsigned int> {
  enum { value = 1 };
};
template <> struct is_integral<long> {
  enum { value = 1 };
};
template <> struct is_integral<unsigned long> {
  enum { value = 1 };
};
template <> struct is_integral<long long> {
  enum { value = 1 };
};
template <> struct is_integral<unsigned long long> {
  enum { value = 1 };
};

// ToDo: specializes is_arithmetic<_Float16>
template <class _Tp> struct is_arithmetic {
  enum { value = 0 };
};
template <> struct is_arithmetic<bool> {
  enum { value = 1 };
};
template <> struct is_arithmetic<char> {
  enum { value = 1 };
};
template <> struct is_arithmetic<signed char> {
  enum { value = 1 };
};
template <> struct is_arithmetic<unsigned char> {
  enum { value = 1 };
};
template <> struct is_arithmetic<wchar_t> {
  enum { value = 1 };
};
template <> struct is_arithmetic<short> {
  enum { value = 1 };
};
template <> struct is_arithmetic<unsigned short> {
  enum { value = 1 };
};
template <> struct is_arithmetic<int> {
  enum { value = 1 };
};
template <> struct is_arithmetic<unsigned int> {
  enum { value = 1 };
};
template <> struct is_arithmetic<long> {
  enum { value = 1 };
};
template <> struct is_arithmetic<unsigned long> {
  enum { value = 1 };
};
template <> struct is_arithmetic<long long> {
  enum { value = 1 };
};
template <> struct is_arithmetic<unsigned long long> {
  enum { value = 1 };
};
template <> struct is_arithmetic<float> {
  enum { value = 1 };
};
template <> struct is_arithmetic<double> {
  enum { value = 1 };
};

struct true_type {
  static const __constant__ bool value = true;
};
struct false_type {
  static const __constant__ bool value = false;
};

template <typename __T, typename __U> struct is_same : public false_type {};
template <typename __T> struct is_same<__T, __T> : public true_type {};

template <typename __T> struct add_rvalue_reference { typedef __T &&type; };

template <typename __T> typename add_rvalue_reference<__T>::type declval();

// decltype is only available in C++11 and above.
#if __cplusplus >= 201103L
// __hip_promote
template <class _Tp> struct __numeric_type {
  static void __test(...);
  static _Float16 __test(_Float16);
  static float __test(float);
  static double __test(char);
  static double __test(int);
  static double __test(unsigned);
  static double __test(long);
  static double __test(unsigned long);
  static double __test(long long);
  static double __test(unsigned long long);
  static double __test(double);
  // No support for long double, use double instead.
  static double __test(long double);

  typedef decltype(__test(declval<_Tp>())) type;
  static const bool value = !is_same<type, void>::value;
};

template <> struct __numeric_type<void> { static const bool value = true; };

template <class _A1, class _A2 = void, class _A3 = void,
          bool = __numeric_type<_A1>::value &&__numeric_type<_A2>::value
              &&__numeric_type<_A3>::value>
class __promote_imp {
public:
  static const bool value = false;
};

template <class _A1, class _A2, class _A3>
class __promote_imp<_A1, _A2, _A3, true> {
private:
  typedef typename __promote_imp<_A1>::type __type1;
  typedef typename __promote_imp<_A2>::type __type2;
  typedef typename __promote_imp<_A3>::type __type3;

public:
  typedef decltype(__type1() + __type2() + __type3()) type;
  static const bool value = true;
};

template <class _A1, class _A2> class __promote_imp<_A1, _A2, void, true> {
private:
  typedef typename __promote_imp<_A1>::type __type1;
  typedef typename __promote_imp<_A2>::type __type2;

public:
  typedef decltype(__type1() + __type2()) type;
  static const bool value = true;
};

template <class _A1> class __promote_imp<_A1, void, void, true> {
public:
  typedef typename __numeric_type<_A1>::type type;
  static const bool value = true;
};

template <class _A1, class _A2 = void, class _A3 = void>
class __promote : public __promote_imp<_A1, _A2, _A3> {};
#endif //__cplusplus >= 201103L
} // namespace __hip

// __HIP_OVERLOAD1 is used to resolve function calls with integer argument to
// avoid compilation error due to ambibuity. e.g. floor(5) is resolved with
// floor(double).
#define __HIP_OVERLOAD1(__retty, __fn)                                         \
  template <typename __T>                                                      \
  __DEVICE__ __CONSTEXPR__                                                     \
      typename __hip_enable_if<__hip::is_integral<__T>::value, __retty>::type  \
      __fn(__T __x) {                                                          \
    return ::__fn((double)__x);                                                \
  }

// __HIP_OVERLOAD2 is used to resolve function calls with mixed float/double
// or integer argument to avoid compilation error due to ambibuity. e.g.
// max(5.0f, 6.0) is resolved with max(double, double).
#if __cplusplus >= 201103L
#define __HIP_OVERLOAD2(__retty, __fn)                                         \
  template <typename __T1, typename __T2>                                      \
  __DEVICE__ __CONSTEXPR__ typename __hip_enable_if<                           \
      __hip::is_arithmetic<__T1>::value && __hip::is_arithmetic<__T2>::value,  \
      typename __hip::__promote<__T1, __T2>::type>::type                       \
  __fn(__T1 __x, __T2 __y) {                                                   \
    typedef typename __hip::__promote<__T1, __T2>::type __result_type;         \
    return __fn((__result_type)__x, (__result_type)__y);                       \
  }
#else
#define __HIP_OVERLOAD2(__retty, __fn)                                         \
  template <typename __T1, typename __T2>                                      \
  __DEVICE__ __CONSTEXPR__                                                     \
      typename __hip_enable_if<__hip::is_arithmetic<__T1>::value &&            \
                                   __hip::is_arithmetic<__T2>::value,          \
                               __retty>::type                                  \
      __fn(__T1 __x, __T2 __y) {                                               \
    return __fn((double)__x, (double)__y);                                     \
  }
#endif

__HIP_OVERLOAD1(double, acos)
__HIP_OVERLOAD1(double, acosh)
__HIP_OVERLOAD1(double, asin)
__HIP_OVERLOAD1(double, asinh)
__HIP_OVERLOAD1(double, atan)
__HIP_OVERLOAD2(double, atan2)
__HIP_OVERLOAD1(double, atanh)
__HIP_OVERLOAD1(double, cbrt)
__HIP_OVERLOAD1(double, ceil)
__HIP_OVERLOAD2(double, copysign)
__HIP_OVERLOAD1(double, cos)
__HIP_OVERLOAD1(double, cosh)
__HIP_OVERLOAD1(double, erf)
__HIP_OVERLOAD1(double, erfc)
__HIP_OVERLOAD1(double, exp)
__HIP_OVERLOAD1(double, exp2)
__HIP_OVERLOAD1(double, expm1)
__HIP_OVERLOAD1(double, fabs)
__HIP_OVERLOAD2(double, fdim)
__HIP_OVERLOAD1(double, floor)
__HIP_OVERLOAD2(double, fmax)
__HIP_OVERLOAD2(double, fmin)
__HIP_OVERLOAD2(double, fmod)
#if !defined(__HIPCC_RTC__)
__HIP_OVERLOAD1(int, fpclassify)
#endif // !defined(__HIPCC_RTC__)
__HIP_OVERLOAD2(double, hypot)
__HIP_OVERLOAD1(int, ilogb)
__HIP_OVERLOAD1(bool, isfinite)
__HIP_OVERLOAD2(bool, isgreater)
__HIP_OVERLOAD2(bool, isgreaterequal)
__HIP_OVERLOAD1(bool, isinf)
__HIP_OVERLOAD2(bool, isless)
__HIP_OVERLOAD2(bool, islessequal)
__HIP_OVERLOAD2(bool, islessgreater)
__HIP_OVERLOAD1(bool, isnan)
__HIP_OVERLOAD1(bool, isnormal)
__HIP_OVERLOAD2(bool, isunordered)
__HIP_OVERLOAD1(double, lgamma)
__HIP_OVERLOAD1(double, log)
__HIP_OVERLOAD1(double, log10)
__HIP_OVERLOAD1(double, log1p)
__HIP_OVERLOAD1(double, log2)
__HIP_OVERLOAD1(double, logb)
__HIP_OVERLOAD1(long long, llrint)
__HIP_OVERLOAD1(long long, llround)
__HIP_OVERLOAD1(long, lrint)
__HIP_OVERLOAD1(long, lround)
__HIP_OVERLOAD1(double, nearbyint)
__HIP_OVERLOAD2(double, nextafter)
__HIP_OVERLOAD2(double, pow)
__HIP_OVERLOAD2(double, remainder)
__HIP_OVERLOAD1(double, rint)
__HIP_OVERLOAD1(double, round)
__HIP_OVERLOAD1(bool, signbit)
__HIP_OVERLOAD1(double, sin)
__HIP_OVERLOAD1(double, sinh)
__HIP_OVERLOAD1(double, sqrt)
__HIP_OVERLOAD1(double, tan)
__HIP_OVERLOAD1(double, tanh)
__HIP_OVERLOAD1(double, tgamma)
__HIP_OVERLOAD1(double, trunc)

// Overload these but don't add them to std, they are not part of cmath.
__HIP_OVERLOAD2(double, max)
__HIP_OVERLOAD2(double, min)

// Additional Overloads that don't quite match HIP_OVERLOAD.
#if __cplusplus >= 201103L
template <typename __T1, typename __T2, typename __T3>
__DEVICE__ __CONSTEXPR__ typename __hip_enable_if<
    __hip::is_arithmetic<__T1>::value && __hip::is_arithmetic<__T2>::value &&
        __hip::is_arithmetic<__T3>::value,
    typename __hip::__promote<__T1, __T2, __T3>::type>::type
fma(__T1 __x, __T2 __y, __T3 __z) {
  typedef typename __hip::__promote<__T1, __T2, __T3>::type __result_type;
  return ::fma((__result_type)__x, (__result_type)__y, (__result_type)__z);
}
#else
template <typename __T1, typename __T2, typename __T3>
__DEVICE__ __CONSTEXPR__
    typename __hip_enable_if<__hip::is_arithmetic<__T1>::value &&
                                 __hip::is_arithmetic<__T2>::value &&
                                 __hip::is_arithmetic<__T3>::value,
                             double>::type
    fma(__T1 __x, __T2 __y, __T3 __z) {
  return ::fma((double)__x, (double)__y, (double)__z);
}
#endif

template <typename __T>
__DEVICE__ __CONSTEXPR__
    typename __hip_enable_if<__hip::is_integral<__T>::value, double>::type
    frexp(__T __x, int *__exp) {
  return ::frexp((double)__x, __exp);
}

template <typename __T>
__DEVICE__ __CONSTEXPR__
    typename __hip_enable_if<__hip::is_integral<__T>::value, double>::type
    ldexp(__T __x, int __exp) {
  return ::ldexp((double)__x, __exp);
}

template <typename __T>
__DEVICE__ __CONSTEXPR__
    typename __hip_enable_if<__hip::is_integral<__T>::value, double>::type
    modf(__T __x, double *__exp) {
  return ::modf((double)__x, __exp);
}

#if __cplusplus >= 201103L
template <typename __T1, typename __T2>
__DEVICE__ __CONSTEXPR__
    typename __hip_enable_if<__hip::is_arithmetic<__T1>::value &&
                                 __hip::is_arithmetic<__T2>::value,
                             typename __hip::__promote<__T1, __T2>::type>::type
    remquo(__T1 __x, __T2 __y, int *__quo) {
  typedef typename __hip::__promote<__T1, __T2>::type __result_type;
  return ::remquo((__result_type)__x, (__result_type)__y, __quo);
}
#else
template <typename __T1, typename __T2>
__DEVICE__ __CONSTEXPR__
    typename __hip_enable_if<__hip::is_arithmetic<__T1>::value &&
                                 __hip::is_arithmetic<__T2>::value,
                             double>::type
    remquo(__T1 __x, __T2 __y, int *__quo) {
  return ::remquo((double)__x, (double)__y, __quo);
}
#endif

template <typename __T>
__DEVICE__ __CONSTEXPR__
    typename __hip_enable_if<__hip::is_integral<__T>::value, double>::type
    scalbln(__T __x, long int __exp) {
  return ::scalbln((double)__x, __exp);
}

template <typename __T>
__DEVICE__ __CONSTEXPR__
    typename __hip_enable_if<__hip::is_integral<__T>::value, double>::type
    scalbn(__T __x, int __exp) {
  return ::scalbn((double)__x, __exp);
}

#pragma pop_macro("__HIP_OVERLOAD1")
#pragma pop_macro("__HIP_OVERLOAD2")

// END HIP_OVERLOAD

// END DEF_FUN and HIP_OVERLOAD

#endif // ifndef __OPENMP_AMDGCN__
#endif // defined(__cplusplus)

#ifndef __OPENMP_AMDGCN__
// Define these overloads inside the namespace our standard library uses.
#if !defined(__HIPCC_RTC__)
#ifdef _LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_NAMESPACE_STD
#else
namespace std {
#ifdef _GLIBCXX_BEGIN_NAMESPACE_VERSION
_GLIBCXX_BEGIN_NAMESPACE_VERSION
#endif // _GLIBCXX_BEGIN_NAMESPACE_VERSION
#endif // _LIBCPP_BEGIN_NAMESPACE_STD

// Pull the new overloads we defined above into namespace std.
// using ::abs; - This may be considered for C++.
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
using ::modf;
// using ::nan; - This may be considered for C++.
// using ::nanf; - This may be considered for C++.
// using ::nanl; - This is not yet defined.
using ::nearbyint;
using ::nextafter;
// using ::nexttoward; - Omit this since we do not have a definition.
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

// Finally, pull the "foobarf" functions that HIP defines into std.
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
// using ::nexttowardf; - Omit this since we do not have a definition.
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
#endif // _GLIBCXX_BEGIN_NAMESPACE_VERSION
} // namespace std
#endif // _LIBCPP_END_NAMESPACE_STD
#endif // !defined(__HIPCC_RTC__)

// Define device-side math functions from <ymath.h> on MSVC.
#if !defined(__HIPCC_RTC__)
#if defined(_MSC_VER)

// Before VS2019, `<ymath.h>` is also included in `<limits>` and other headers.
// But, from VS2019, it's only included in `<complex>`. Need to include
// `<ymath.h>` here to ensure C functions declared there won't be markded as
// `__host__` and `__device__` through `<complex>` wrapper.
#include <ymath.h>

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)
__DEVICE__ __CONSTEXPR__ __attribute__((overloadable)) double _Cosh(double x,
                                                                    double y) {
  return cosh(x) * y;
}
__DEVICE__ __CONSTEXPR__ __attribute__((overloadable)) float _FCosh(float x,
                                                                    float y) {
  return coshf(x) * y;
}
__DEVICE__ __CONSTEXPR__ __attribute__((overloadable)) short _Dtest(double *p) {
  return fpclassify(*p);
}
__DEVICE__ __CONSTEXPR__ __attribute__((overloadable)) short _FDtest(float *p) {
  return fpclassify(*p);
}
__DEVICE__ __CONSTEXPR__ __attribute__((overloadable)) double _Sinh(double x,
                                                                    double y) {
  return sinh(x) * y;
}
__DEVICE__ __CONSTEXPR__ __attribute__((overloadable)) float _FSinh(float x,
                                                                    float y) {
  return sinhf(x) * y;
}
#if defined(__cplusplus)
}
#endif // defined(__cplusplus)
#endif // defined(_MSC_VER)
#endif // !defined(__HIPCC_RTC__)
#endif // ifndef __OPENMP_AMDGCN__

#pragma pop_macro("__DEVICE__")
#pragma pop_macro("__CONSTEXPR__")

#endif // __CLANG_HIP_CMATH_H__
