// -*- C++ -*- C forwarding header.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003
// Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

//
// ISO C++ 14882: 26.5  C library
//

/** @file cmath
 *  This is a Standard C++ Library file.  You should @c #include this file
 *  in your programs, rather than any of the "*.h" implementation files.
 *
 *  This is the C++ version of the Standard C Library header @c math.h,
 *  and its contents are (mostly) the same as that header, but are all
 *  contained in the namespace @c std.
 */

#ifndef _CPP_CMATH
#define _CPP_CMATH 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <bits/cpp_type_traits.h>

#include <math.h>

// Get rid of those macros defined in <math.h> in lieu of real functions.
#undef abs
#undef div
#undef acos
#undef asin
#undef atan
#undef atan2
#undef ceil
#undef cos
#undef cosh
#undef exp
#undef fabs
#undef floor
#undef fmod
#undef frexp
#undef ldexp
#undef log
#undef log10
#undef modf
#undef pow
#undef sin
#undef sinh
#undef sqrt
#undef tan
#undef tanh

// ...and in the darkness bind them...
namespace __gnu_cxx
{
  namespace  __c99_binding
  {
#if defined(_GLIBCPP_USE_C99_FLOAT_TRANSCENDENTALS_CHECK) || \
    defined(_GLIBCPP_USE_C99_FLOAT_TRANSCENDENTALS_DYNAMIC)
    extern "C" float (acosf)(float);
    extern "C" float (asinf)(float);
    extern "C" float (atanf)(float);
    extern "C" float (atan2f)(float, float);
    extern "C" float (ceilf)(float);
    extern "C" float (coshf)(float);
    extern "C" float (expf)(float);
    extern "C" float (floorf)(float);
    extern "C" float (fmodf)(float, float);
    extern "C" float (frexpf)(float, int*);
    extern "C" float (ldexpf)(float, int);
    extern "C" float (logf)(float);
    extern "C" float (log10f)(float);
    extern "C" float (modff)(float, float*);
    extern "C" float (powf)(float, float);
    extern "C" float (sinhf)(float);
    extern "C" float (tanf)(float);
    extern "C" float (tanhf)(float);
#endif
#if !defined(_GLIBCPP_USE_C99_FLOAT_TRANSCENDENTALS_DYNAMIC)
#if defined(_GLIBCPP_HAVE_ACOSF)
    using ::acosf;
#endif
#if defined(_GLIBCPP_HAVE_ASINF)
    using ::asinf;
#endif
#if defined(_GLIBCPP_HAVE_ATANF)
    using ::atanf;
#endif
#if defined(_GLIBCPP_HAVE_ATAN2F)
    using ::atan2f;
#endif
#if defined(_GLIBCPP_HAVE_CEILF)
    using ::ceilf;
#endif
#if defined(_GLIBCPP_HAVE_COSHF)
    using ::coshf;
#endif
#if defined(_GLIBCPP_HAVE_EXPF)
    using ::expf;
#endif
#if defined(_GLIBCPP_HAVE_FLOORF)
    using ::floorf;
#endif
#if defined(_GLIBCPP_HAVE_FMODF)
    using ::fmodf;
#endif
#if defined(_GLIBCPP_HAVE_FREXPF)
    using ::frexpf;
#endif
#if defined(_GLIBCPP_HAVE_LDEXPF)
    using ::ldexpf;
#endif
#if defined(_GLIBCPP_HAVE_LOGF)
    using ::logf;
#endif
#if defined(_GLIBCPP_HAVE_LOG10F)
    using ::log10f;
#endif
#if defined(_GLIBCPP_HAVE_MODFF)
    using ::modff;
#endif
#if defined(_GLIBCPP_HAVE_POWF)
    using ::powf;
#endif
#if defined(_GLIBCPP_HAVE_SINHF)
    using ::sinhf;
#endif
#if defined(_GLIBCPP_HAVE_TANF)
    using ::tanf;
#endif
#if defined(_GLIBCPP_HAVE_TANHF)
    using ::tanhf;
#endif
#endif /* _GLIBCPP_USE_C99_FLOAT_TRANSCENDENTALS_DYNAMIC */
  }
}

namespace std 
{
  // Forward declaration of a helper function.  This really should be
  // an `exported' forward declaration.
  template<typename _Tp> _Tp __cmath_power(_Tp, unsigned int);

  inline double
  abs(double __x)
  { return __builtin_fabs(__x); }

  inline float
  abs(float __x)
  { return __builtin_fabsf(__x); }

  inline long double
  abs(long double __x)
  { return __builtin_fabsl(__x); }

#if defined(_GLIBCPP_HAVE_ACOSF)
  inline float 
  acos(float __x) { return __gnu_cxx::__c99_binding::acosf(__x); }
#else
  inline float 
  acos(float __x) { return ::acos(static_cast<double>(__x)); }
#endif

  using ::acos;
  
#if defined(_GLIBCPP_HAVE_ACOSL)
  inline long double 
  acos(long double __x) { return ::acosl(__x); }
#else
  inline long double 
  acos(long double __x) { return ::acos(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    acos(_Tp __x)
    {
      return ::acos(static_cast<double>(__x));
    }
  
  using ::asin;

#if defined(_GLIBCPP_HAVE_ASINF)
  inline float 
  asin(float __x) { return __gnu_cxx::__c99_binding::asinf(__x); }
#else
  inline float 
  asin(float __x) { return ::asin(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_ASINL)
  inline long double 
  asin(long double __x) { return ::asinl(__x); }
#else
  inline long double 
  asin(long double __x) { return ::asin(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    asin(_Tp __x)
    { return ::asin(static_cast<double>(__x)); }

  using ::atan;

#if defined(_GLIBCPP_HAVE_ATANF)
  inline float 
  atan(float __x) { return __gnu_cxx::__c99_binding::atanf(__x); }
#else
  inline float 
  atan(float __x) { return ::atan(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_ATANL)
  inline long double 
  atan(long double __x) { return ::atanl(__x); }
#else
  inline long double 
  atan(long double __x) { return ::atan(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    atan(_Tp __x)
    { return ::atan(static_cast<double>(__x)); }
  
  using ::atan2;

#if defined(_GLIBCPP_HAVE_ATAN2F)
  inline float 
  atan2(float __y, float __x) { return __gnu_cxx::__c99_binding::atan2f(__y, __x); }
#else
  inline float 
  atan2(float __y, float __x)
  { return ::atan2(static_cast<double>(__y), static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_ATAN2L)
  inline long double 
  atan2(long double __y, long double __x) { return ::atan2l(__y, __x); }
#else
  inline long double 
  atan2(long double __y, long double __x) 
  { return ::atan2(static_cast<double>(__y), static_cast<double>(__x)); }
#endif

  template<typename _Tp, typename _Up>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type
                                        && __is_integer<_Up>::_M_type>::_M_type
    atan2(_Tp __x, _Up __y)
    { return ::atan2(static_cast<double>(__x), static_cast<double>(__y)); }

  using ::ceil;

#if defined(_GLIBCPP_HAVE_CEILF)
  inline float 
  ceil(float __x) { return __gnu_cxx::__c99_binding::ceilf(__x); }
#else
  inline float 
  ceil(float __x) { return ::ceil(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_CEILL)
  inline long double 
  ceil(long double __x) { return ::ceill(__x); }
#else
  inline long double 
  ceil(long double __x) { return ::ceil(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    ceil(_Tp __x)
    { return ::ceil(static_cast<double>(__x)); }
  
  using ::cos;

  inline float
  cos(float __x)
  { return __builtin_cosf(__x); }

  inline long double
  cos(long double __x)
  { return __builtin_cosl(__x); }

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    cos(_Tp __x)
    { return __builtin_cos(__x); }

  using ::cosh;

#if defined(_GLIBCPP_HAVE_COSHF)
  inline float 
  cosh(float __x) { return __gnu_cxx::__c99_binding::coshf(__x); }
#else
  inline float 
  cosh(float __x) { return ::cosh(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_COSHL)
  inline long double 
  cosh(long double __x) { return ::coshl(__x); }
#else
  inline long double 
  cosh(long double __x) { return ::cosh(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    cosh(_Tp __x)
    { return ::cosh(static_cast<double>(__x)); }

  using ::exp;

#if defined(_GLIBCPP_HAVE_EXPF)
  inline float 
  exp(float __x) { return __gnu_cxx::__c99_binding::expf(__x); }
#else
  inline float 
  exp(float __x) { return ::exp(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_EXPL)
  inline long double 
  exp(long double __x) { return ::expl(__x); }
#else
  inline long double 
  exp(long double __x) { return ::exp(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    exp(_Tp __x)
    { return ::exp(static_cast<double>(__x)); }
  
  using ::fabs;

  inline float
  fabs(float __x)
  { return __builtin_fabsf(__x); }

  inline long double
  fabs(long double __x)
  { return __builtin_fabsl(__x); }

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    fabs(_Tp __x)
    { return __builtin_fabs(__x); }

  using ::floor;

#if defined(_GLIBCPP_HAVE_FLOORF)
  inline float 
  floor(float __x) { return __gnu_cxx::__c99_binding::floorf(__x); }
#else
  inline float 
  floor(float __x) { return ::floor(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_FLOORL)
  inline long double 
  floor(long double __x) { return ::floorl(__x); }
#else
  inline long double 
  floor(long double __x) { return ::floor(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    floor(_Tp __x)
    { return ::floor(static_cast<double>(__x)); }
  
  using ::fmod;

#if defined(_GLIBCPP_HAVE_FMODF)
  inline float 
  fmod(float __x, float __y) { return __gnu_cxx::__c99_binding::fmodf(__x, __y); }
#else
  inline float 
  fmod(float __x, float __y)
  { return ::fmod(static_cast<double>(__x), static_cast<double>(__y)); }
#endif

#if defined(_GLIBCPP_HAVE_FMODL)
  inline long double 
  fmod(long double __x, long double __y) { return ::fmodl(__x, __y); }
#else
  inline long double 
  fmod(long double __x, long double __y) 
  { return ::fmod(static_cast<double>(__x), static_cast<double>(__y)); }
#endif

  using ::frexp;

#if defined(_GLIBCPP_HAVE_FREXPF)
  inline float 
  frexp(float __x, int* __exp) { return __gnu_cxx::__c99_binding::frexpf(__x, __exp); }
#else
  inline float 
  frexp(float __x, int* __exp) { return ::frexp(__x, __exp); }
#endif

#if defined(_GLIBCPP_HAVE_FREXPL)
  inline long double 
  frexp(long double __x, int* __exp) { return ::frexpl(__x, __exp); }
#else
  inline long double 
  frexp(long double __x, int* __exp) 
  { return ::frexp(static_cast<double>(__x), __exp); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    frexp(_Tp __x, int* __exp)
    { return ::frexp(static_cast<double>(__x), __exp); }
  
  using ::ldexp;

#if defined(_GLIBCPP_HAVE_LDEXPF)
  inline float 
  ldexp(float __x, int __exp) { return __gnu_cxx::__c99_binding::ldexpf(__x, __exp); }
#else
  inline float 
  ldexp(float __x, int __exp)
  { return ::ldexp(static_cast<double>(__x), __exp); }
#endif

#if defined(_GLIBCPP_HAVE_LDEXPL)
  inline long double 
  ldexp(long double __x, int __exp) { return ::ldexpl(__x, __exp); }
#else
  inline long double 
  ldexp(long double __x, int __exp) 
  { return ::ldexp(static_cast<double>(__x), __exp); }
#endif

  template<typename _Tp>
  inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
  ldexp(_Tp __x, int __exp)
  { return ::ldexp(static_cast<double>(__x), __exp); }

  using ::log;

#if defined(_GLIBCPP_HAVE_LOGF)
  inline float 
  log(float __x) { return __gnu_cxx::__c99_binding::logf(__x); }
#else
  inline float log(float __x)
  { return ::log(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_LOGL)
  inline long double 
  log(long double __x) { return ::logl(__x); }
#else
  inline long double 
  log(long double __x) { return ::log(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    log(_Tp __x)
    { return ::log(static_cast<double>(__x)); }
  
  using ::log10;

#if defined(_GLIBCPP_HAVE_LOG10F)
  inline float 
  log10(float __x) { return __gnu_cxx::__c99_binding::log10f(__x); }
#else
  inline float 
  log10(float __x) { return ::log10(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_LOG10L)
  inline long double 
  log10(long double __x) { return ::log10l(__x); }
#else
  inline long double 
  log10(long double __x) { return ::log10(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    log10(_Tp __x)
    { return ::log10(static_cast<double>(__x)); }
  
  using ::modf;

#if defined(_GLIBCPP_HAVE_MODFF)
  inline float 
  modf(float __x, float* __iptr) { return __gnu_cxx::__c99_binding::modff(__x, __iptr); }
#else
  inline float 
  modf(float __x, float* __iptr)
  {
    double __tmp;
    double __res = ::modf(static_cast<double>(__x), &__tmp);
    *__iptr = static_cast<float>(__tmp);
    return __res;
  }
#endif

#if defined(_GLIBCPP_HAVE_MODFL)
  inline long double 
  modf(long double __x, long double* __iptr) { return ::modfl(__x, __iptr); }
#else
  inline long double 
  modf(long double __x, long double* __iptr) 
  { 
    double __tmp;
    double __res = ::modf(static_cast<double>(__x), &__tmp);
    * __iptr = static_cast<long double>(__tmp);
    return __res;
  }
#endif

  template<typename _Tp>
    inline _Tp
    __pow_helper(_Tp __x, int __n)
    {
      return __n < 0
        ? _Tp(1)/__cmath_power(__x, -__n)
        : __cmath_power(__x, __n);
    }

  using ::pow;

#if defined(_GLIBCPP_HAVE_POWF)
  inline float 
  pow(float __x, float __y) { return __gnu_cxx::__c99_binding::powf(__x, __y); }
#else
  inline float 
  pow(float __x, float __y)
  { return ::pow(static_cast<double>(__x), static_cast<double>(__y)); }
#endif

#if defined(_GLIBCPP_HAVE_POWL)
  inline long double 
  pow(long double __x, long double __y) { return ::powl(__x, __y); }
#else
  inline long double 
  pow(long double __x, long double __y) 
  { return ::pow(static_cast<double>(__x), static_cast<double>(__y)); }
#endif

  inline double 
  pow(double __x, int __i)
  { return __pow_helper(__x, __i); }

  inline float 
  pow(float __x, int __n)
  { return __pow_helper(__x, __n); }

  inline long double 
  pow(long double __x, int __n)
  { return __pow_helper(__x, __n); }

  using ::sin;

  inline float
  sin(float __x)
  { return __builtin_sinf(__x); }

  inline long double
  sin(long double __x)
  { return __builtin_sinl(__x); }

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    sin(_Tp __x)
    { return __builtin_sin(__x); }

  using ::sinh;

#if defined(_GLIBCPP_HAVE_SINHF)
  inline float 
  sinh(float __x) { return __gnu_cxx::__c99_binding::sinhf(__x); }
#else
  inline float 
  sinh(float __x) { return ::sinh(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_SINHL)
  inline long double 
  sinh(long double __x) { return ::sinhl(__x); }
#else
  inline long double 
  sinh(long double __x) { return ::sinh(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    sinh(_Tp __x)
    { return ::sinh(static_cast<_Tp>(__x)); }
  
  using ::sqrt;

  inline float
  sqrt(float __x)
  { return __builtin_sqrtf(__x); }

  inline long double
  sqrt(long double __x)
  { return __builtin_sqrtl(__x); }

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    sqrt(_Tp __x)
    { return __builtin_sqrt(__x); }
  
  using ::tan;

#if defined(_GLIBCPP_HAVE_TANF)
  inline float 
  tan(float __x) { return __gnu_cxx::__c99_binding::tanf(__x); }
#else
  inline float 
  tan(float __x) { return ::tan(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_TANL)
  inline long double 
  tan(long double __x) { return ::tanl(__x); }
#else
  inline long double 
  tan(long double __x) { return ::tan(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    tan(_Tp __x)
    { return ::tan(static_cast<double>(__x)); }
  
  using ::tanh;

#if defined(_GLIBCPP_HAVE_TANHF)
  inline float 
  tanh(float __x) { return __gnu_cxx::__c99_binding::tanhf(__x); }
#else
  inline float 
  tanh(float __x) { return ::tanh(static_cast<double>(__x)); }
#endif

#if defined(_GLIBCPP_HAVE_TANHL)
  inline long double 
  tanh(long double __x) { return ::tanhl(__x); }
#else
  inline long double 
  tanh(long double __x) { return ::tanh(static_cast<double>(__x)); }
#endif

  template<typename _Tp>
    inline typename __enable_if<double, __is_integer<_Tp>::_M_type>::_M_type
    tanh(_Tp __x)
    { return ::tanh(static_cast<double>(__x)); }
} 


#if defined(_GLIBCPP_USE_C99)
#if !defined(_GLIBCPP_USE_C99_FP_MACROS_DYNAMIC)
// These are possible macros imported from C99-land. For strict
// conformance, remove possible C99-injected names from the global
// namespace, and sequester them in the __gnu_cxx extension namespace. 
namespace __gnu_cxx
{
  template<typename _Tp>
    int 
    __capture_fpclassify(_Tp __f) { return fpclassify(__f); }

  template<typename _Tp>
    int 
    __capture_isfinite(_Tp __f) { return isfinite(__f); }

  template<typename _Tp>
    int 
    __capture_isinf(_Tp __f) { return isinf(__f); }

  template<typename _Tp>
    int 
    __capture_isnan(_Tp __f) { return isnan(__f); }

  template<typename _Tp>
    int 
    __capture_isnormal(_Tp __f) { return isnormal(__f); }

  template<typename _Tp>
    int 
    __capture_signbit(_Tp __f) { return signbit(__f); }

  template<typename _Tp>
    int 
    __capture_isgreater(_Tp __f1, _Tp __f2)
    { return isgreater(__f1, __f2); }

  template<typename _Tp>
     int 
     __capture_isgreaterequal(_Tp __f1, _Tp __f2) 
     { return isgreaterequal(__f1, __f2); }

  template<typename _Tp>
     int 
     __capture_isless(_Tp __f1, _Tp __f2) { return isless(__f1, __f2); }

  template<typename _Tp>
     int 
     __capture_islessequal(_Tp __f1, _Tp __f2) 
     { return islessequal(__f1, __f2); }

  template<typename _Tp>
     int 
     __capture_islessgreater(_Tp __f1, _Tp __f2) 
     { return islessgreater(__f1, __f2); }

  template<typename _Tp>
     int 
     __capture_isunordered(_Tp __f1, _Tp __f2) 
     { return isunordered(__f1, __f2); }
} 

#undef fpclassify
#undef isfinite
#undef isinf
#undef isnan
#undef isnormal
#undef signbit
#undef isgreater
#undef isgreaterequal
#undef isless
#undef islessequal
#undef islessgreater
#undef isunordered

namespace __gnu_cxx
{
  template<typename _Tp>
    int
    fpclassify(_Tp __f) { return __capture_fpclassify(__f); }

  template<typename _Tp>
    int
    isfinite(_Tp __f) { return __capture_isfinite(__f); }

  template<typename _Tp>
    int 
    isinf(_Tp __f) { return __capture_isinf(__f); }

  template<typename _Tp>
    int 
    isnan(_Tp __f) { return __capture_isnan(__f); }

  template<typename _Tp>
    int 
    isnormal(_Tp __f) { return __capture_isnormal(__f); }

  template<typename _Tp>
    int 
    signbit(_Tp __f) { return __capture_signbit(__f); }

  template<typename _Tp>
    int 
    isgreater(_Tp __f1, _Tp __f2) { return __capture_isgreater(__f1, __f2); }

  template<typename _Tp>
    int 
    isgreaterequal(_Tp __f1, _Tp __f2) 
    { return __capture_isgreaterequal(__f1, __f2); }

  template<typename _Tp>
    int 
    isless(_Tp __f1, _Tp __f2) { return __capture_isless(__f1, __f2); }

  template<typename _Tp>
    int 
    islessequal(_Tp __f1, _Tp __f2) 
    { return __capture_islessequal(__f1, __f2); }

  template<typename _Tp>
    int 
    islessgreater(_Tp __f1, _Tp __f2) 
    { return __capture_islessgreater(__f1, __f2); }

  template<typename _Tp>
    int 
    isunordered(_Tp __f1, _Tp __f2) 
    { return __capture_isunordered(__f1, __f2); }
}

namespace std
{
  using __gnu_cxx::fpclassify;
  using __gnu_cxx::isfinite;
  using __gnu_cxx::isinf;
  using __gnu_cxx::isnan;
  using __gnu_cxx::isnormal;
  using __gnu_cxx::signbit;
  using __gnu_cxx::isgreater;
  using __gnu_cxx::isgreaterequal;
  using __gnu_cxx::isless;
  using __gnu_cxx::islessequal;
  using __gnu_cxx::islessgreater;
  using __gnu_cxx::isunordered;
}
#endif /* _GLIBCPP_USE_C99_FP_MACROS_DYNAMIC */
#endif
  
#ifdef _GLIBCPP_NO_TEMPLATE_EXPORT
#  define export
#  include <bits/cmath.tcc>
#endif

#endif
