// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_MATH_H
#  define _LIBCPP_MATH_H

/*
    math.h synopsis

Macros:

    HUGE_VAL
    HUGE_VALF               // C99
    HUGE_VALL               // C99
    INFINITY                // C99
    NAN                     // C99
    FP_INFINITE             // C99
    FP_NAN                  // C99
    FP_NORMAL               // C99
    FP_SUBNORMAL            // C99
    FP_ZERO                 // C99
    FP_FAST_FMA             // C99
    FP_FAST_FMAF            // C99
    FP_FAST_FMAL            // C99
    FP_ILOGB0               // C99
    FP_ILOGBNAN             // C99
    MATH_ERRNO              // C99
    MATH_ERREXCEPT          // C99
    math_errhandling        // C99

Types:

    float_t                 // C99
    double_t                // C99

// C90

floating_point abs(floating_point x);

floating_point acos (arithmetic x);
float          acosf(float x);
long double    acosl(long double x);

floating_point asin (arithmetic x);
float          asinf(float x);
long double    asinl(long double x);

floating_point atan (arithmetic x);
float          atanf(float x);
long double    atanl(long double x);

floating_point atan2 (arithmetic y, arithmetic x);
float          atan2f(float y, float x);
long double    atan2l(long double y, long double x);

floating_point ceil (arithmetic x);
float          ceilf(float x);
long double    ceill(long double x);

floating_point cos (arithmetic x);
float          cosf(float x);
long double    cosl(long double x);

floating_point cosh (arithmetic x);
float          coshf(float x);
long double    coshl(long double x);

floating_point exp (arithmetic x);
float          expf(float x);
long double    expl(long double x);

floating_point fabs (arithmetic x);
float          fabsf(float x);
long double    fabsl(long double x);

floating_point floor (arithmetic x);
float          floorf(float x);
long double    floorl(long double x);

floating_point fmod (arithmetic x, arithmetic y);
float          fmodf(float x, float y);
long double    fmodl(long double x, long double y);

floating_point frexp (arithmetic value, int* exp);
float          frexpf(float value, int* exp);
long double    frexpl(long double value, int* exp);

floating_point ldexp (arithmetic value, int exp);
float          ldexpf(float value, int exp);
long double    ldexpl(long double value, int exp);

floating_point log (arithmetic x);
float          logf(float x);
long double    logl(long double x);

floating_point log10 (arithmetic x);
float          log10f(float x);
long double    log10l(long double x);

floating_point modf (floating_point value, floating_point* iptr);
float          modff(float value, float* iptr);
long double    modfl(long double value, long double* iptr);

floating_point pow (arithmetic x, arithmetic y);
float          powf(float x, float y);
long double    powl(long double x, long double y);

floating_point sin (arithmetic x);
float          sinf(float x);
long double    sinl(long double x);

floating_point sinh (arithmetic x);
float          sinhf(float x);
long double    sinhl(long double x);

floating_point sqrt (arithmetic x);
float          sqrtf(float x);
long double    sqrtl(long double x);

floating_point tan (arithmetic x);
float          tanf(float x);
long double    tanl(long double x);

floating_point tanh (arithmetic x);
float          tanhf(float x);
long double    tanhl(long double x);

//  C99

bool signbit(arithmetic x);

int fpclassify(arithmetic x);

bool isfinite(arithmetic x);
bool isinf(arithmetic x);
bool isnan(arithmetic x);
bool isnormal(arithmetic x);

bool isgreater(arithmetic x, arithmetic y);
bool isgreaterequal(arithmetic x, arithmetic y);
bool isless(arithmetic x, arithmetic y);
bool islessequal(arithmetic x, arithmetic y);
bool islessgreater(arithmetic x, arithmetic y);
bool isunordered(arithmetic x, arithmetic y);

floating_point acosh (arithmetic x);
float          acoshf(float x);
long double    acoshl(long double x);

floating_point asinh (arithmetic x);
float          asinhf(float x);
long double    asinhl(long double x);

floating_point atanh (arithmetic x);
float          atanhf(float x);
long double    atanhl(long double x);

floating_point cbrt (arithmetic x);
float          cbrtf(float x);
long double    cbrtl(long double x);

floating_point copysign (arithmetic x, arithmetic y);
float          copysignf(float x, float y);
long double    copysignl(long double x, long double y);

floating_point erf (arithmetic x);
float          erff(float x);
long double    erfl(long double x);

floating_point erfc (arithmetic x);
float          erfcf(float x);
long double    erfcl(long double x);

floating_point exp2 (arithmetic x);
float          exp2f(float x);
long double    exp2l(long double x);

floating_point expm1 (arithmetic x);
float          expm1f(float x);
long double    expm1l(long double x);

floating_point fdim (arithmetic x, arithmetic y);
float          fdimf(float x, float y);
long double    fdiml(long double x, long double y);

floating_point fma (arithmetic x, arithmetic y, arithmetic z);
float          fmaf(float x, float y, float z);
long double    fmal(long double x, long double y, long double z);

floating_point fmax (arithmetic x, arithmetic y);
float          fmaxf(float x, float y);
long double    fmaxl(long double x, long double y);

floating_point fmin (arithmetic x, arithmetic y);
float          fminf(float x, float y);
long double    fminl(long double x, long double y);

floating_point hypot (arithmetic x, arithmetic y);
float          hypotf(float x, float y);
long double    hypotl(long double x, long double y);

int ilogb (arithmetic x);
int ilogbf(float x);
int ilogbl(long double x);

floating_point lgamma (arithmetic x);
float          lgammaf(float x);
long double    lgammal(long double x);

long long llrint (arithmetic x);
long long llrintf(float x);
long long llrintl(long double x);

long long llround (arithmetic x);
long long llroundf(float x);
long long llroundl(long double x);

floating_point log1p (arithmetic x);
float          log1pf(float x);
long double    log1pl(long double x);

floating_point log2 (arithmetic x);
float          log2f(float x);
long double    log2l(long double x);

floating_point logb (arithmetic x);
float          logbf(float x);
long double    logbl(long double x);

long lrint (arithmetic x);
long lrintf(float x);
long lrintl(long double x);

long lround (arithmetic x);
long lroundf(float x);
long lroundl(long double x);

double      nan (const char* str);
float       nanf(const char* str);
long double nanl(const char* str);

floating_point nearbyint (arithmetic x);
float          nearbyintf(float x);
long double    nearbyintl(long double x);

floating_point nextafter (arithmetic x, arithmetic y);
float          nextafterf(float x, float y);
long double    nextafterl(long double x, long double y);

floating_point nexttoward (arithmetic x, long double y);
float          nexttowardf(float x, long double y);
long double    nexttowardl(long double x, long double y);

floating_point remainder (arithmetic x, arithmetic y);
float          remainderf(float x, float y);
long double    remainderl(long double x, long double y);

floating_point remquo (arithmetic x, arithmetic y, int* pquo);
float          remquof(float x, float y, int* pquo);
long double    remquol(long double x, long double y, int* pquo);

floating_point rint (arithmetic x);
float          rintf(float x);
long double    rintl(long double x);

floating_point round (arithmetic x);
float          roundf(float x);
long double    roundl(long double x);

floating_point scalbln (arithmetic x, long ex);
float          scalblnf(float x, long ex);
long double    scalblnl(long double x, long ex);

floating_point scalbn (arithmetic x, int ex);
float          scalbnf(float x, int ex);
long double    scalbnl(long double x, int ex);

floating_point tgamma (arithmetic x);
float          tgammaf(float x);
long double    tgammal(long double x);

floating_point trunc (arithmetic x);
float          truncf(float x);
long double    truncl(long double x);

*/

#  include <__config>

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

#  if __has_include_next(<math.h>)
#    include_next <math.h>
#  endif

#  ifdef __cplusplus

// We support including .h headers inside 'extern "C"' contexts, so switch
// back to C++ linkage before including these C++ headers.
extern "C++" {

#    ifdef fpclassify
#      undef fpclassify
#    endif

#    ifdef signbit
#      undef signbit
#    endif

#    ifdef isfinite
#      undef isfinite
#    endif

#    ifdef isinf
#      undef isinf
#    endif

#    ifdef isnan
#      undef isnan
#    endif

#    ifdef isnormal
#      undef isnormal
#    endif

#    ifdef isgreater
#      undef isgreater
#    endif

#    ifdef isgreaterequal
#      undef isgreaterequal
#    endif

#    ifdef isless
#      undef isless
#    endif

#    ifdef islessequal
#      undef islessequal
#    endif

#    ifdef islessgreater
#      undef islessgreater
#    endif

#    ifdef isunordered
#      undef isunordered
#    endif

#    include <__math/abs.h>
#    include <__math/copysign.h>
#    include <__math/error_functions.h>
#    include <__math/exponential_functions.h>
#    include <__math/fdim.h>
#    include <__math/fma.h>
#    include <__math/gamma.h>
#    include <__math/hyperbolic_functions.h>
#    include <__math/hypot.h>
#    include <__math/inverse_hyperbolic_functions.h>
#    include <__math/inverse_trigonometric_functions.h>
#    include <__math/logarithms.h>
#    include <__math/min_max.h>
#    include <__math/modulo.h>
#    include <__math/remainder.h>
#    include <__math/roots.h>
#    include <__math/rounding_functions.h>
#    include <__math/traits.h>
#    include <__math/trigonometric_functions.h>
#    include <__type_traits/enable_if.h>
#    include <__type_traits/is_floating_point.h>
#    include <__type_traits/is_integral.h>
#    include <stdlib.h>

// fpclassify relies on implementation-defined constants, so we can't move it to a detail header
_LIBCPP_BEGIN_NAMESPACE_STD

namespace __math {

// fpclassify

// template on non-double overloads to make them weaker than same overloads from MSVC runtime
template <class = int>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI int fpclassify(float __x) _NOEXCEPT {
  return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, __x);
}

template <class = int>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI int fpclassify(double __x) _NOEXCEPT {
  return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, __x);
}

template <class = int>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI int fpclassify(long double __x) _NOEXCEPT {
  return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, __x);
}

template <class _A1, std::__enable_if_t<std::is_integral<_A1>::value, int> = 0>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI int fpclassify(_A1 __x) _NOEXCEPT {
  return __x == 0 ? FP_ZERO : FP_NORMAL;
}

} // namespace __math

_LIBCPP_END_NAMESPACE_STD

using std::__math::fpclassify;
using std::__math::signbit;

// The MSVC runtime already provides these functions as templates
#    ifndef _LIBCPP_MSVCRT
using std::__math::isfinite;
using std::__math::isgreater;
using std::__math::isgreaterequal;
using std::__math::isinf;
using std::__math::isless;
using std::__math::islessequal;
using std::__math::islessgreater;
using std::__math::isnan;
using std::__math::isnormal;
using std::__math::isunordered;
#    endif // _LIBCPP_MSVCRT

// abs
//
// handled in stdlib.h

// div
//
// handled in stdlib.h

// We have to provide double overloads for <math.h> to work on platforms that don't provide the full set of math
// functions. To make the overload set work with multiple functions that take the same arguments, we make our overloads
// templates. Functions are preferred over function templates during overload resolution, which means that our overload
// will only be selected when the C library doesn't provide one.

using std::__math::acos;
using std::__math::acosh;
using std::__math::asin;
using std::__math::asinh;
using std::__math::atan;
using std::__math::atan2;
using std::__math::atanh;
using std::__math::cbrt;
using std::__math::ceil;
using std::__math::copysign;
using std::__math::cos;
using std::__math::cosh;
using std::__math::erf;
using std::__math::erfc;
using std::__math::exp;
using std::__math::exp2;
using std::__math::expm1;
using std::__math::fabs;
using std::__math::fdim;
using std::__math::floor;
using std::__math::fma;
using std::__math::fmax;
using std::__math::fmin;
using std::__math::fmod;
using std::__math::frexp;
using std::__math::hypot;
using std::__math::ilogb;
using std::__math::ldexp;
using std::__math::lgamma;
using std::__math::llrint;
using std::__math::llround;
using std::__math::log;
using std::__math::log10;
using std::__math::log1p;
using std::__math::log2;
using std::__math::logb;
using std::__math::lrint;
using std::__math::lround;
using std::__math::modf;
using std::__math::nearbyint;
using std::__math::nextafter;
using std::__math::nexttoward;
using std::__math::pow;
using std::__math::remainder;
using std::__math::remquo;
using std::__math::rint;
using std::__math::round;
using std::__math::scalbln;
using std::__math::scalbn;
using std::__math::signbit;
using std::__math::sin;
using std::__math::sinh;
using std::__math::sqrt;
using std::__math::tan;
using std::__math::tanh;
using std::__math::tgamma;
using std::__math::trunc;

} // extern "C++"

#  endif // __cplusplus

#else // _LIBCPP_MATH_H

// This include lives outside the header guard in order to support an MSVC
// extension which allows users to do:
//
// #define _USE_MATH_DEFINES
// #include <math.h>
//
// and receive the definitions of mathematical constants, even if <math.h>
// has previously been included.
#  if defined(_LIBCPP_MSVCRT) && defined(_USE_MATH_DEFINES)
#    include_next <math.h>
#  endif

#endif // _LIBCPP_MATH_H
