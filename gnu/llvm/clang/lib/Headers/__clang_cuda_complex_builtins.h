/*===-- __clang_cuda_complex_builtins - CUDA impls of runtime complex fns ---===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __CLANG_CUDA_COMPLEX_BUILTINS
#define __CLANG_CUDA_COMPLEX_BUILTINS

// This header defines __muldc3, __mulsc3, __divdc3, and __divsc3.  These are
// libgcc functions that clang assumes are available when compiling c99 complex
// operations.  (These implementations come from libc++, and have been modified
// to work with CUDA and OpenMP target offloading [in C and C++ mode].)

#pragma push_macro("__DEVICE__")
#if defined(__OPENMP_NVPTX__) || defined(__OPENMP_AMDGCN__)
#pragma omp declare target
#define __DEVICE__ __attribute__((noinline, nothrow, cold, weak))
#else
#define __DEVICE__ __device__ inline
#endif

// To make the algorithms available for C and C++ in CUDA and OpenMP we select
// different but equivalent function versions. TODO: For OpenMP we currently
// select the native builtins as the overload support for templates is lacking.
#if !defined(__OPENMP_NVPTX__) && !defined(__OPENMP_AMDGCN__)
#define _ISNANd std::isnan
#define _ISNANf std::isnan
#define _ISINFd std::isinf
#define _ISINFf std::isinf
#define _ISFINITEd std::isfinite
#define _ISFINITEf std::isfinite
#define _COPYSIGNd std::copysign
#define _COPYSIGNf std::copysign
#define _SCALBNd std::scalbn
#define _SCALBNf std::scalbn
#define _ABSd std::abs
#define _ABSf std::abs
#define _LOGBd std::logb
#define _LOGBf std::logb
// Rather than pulling in std::max from algorithm everytime, use available ::max.
#define _fmaxd max
#define _fmaxf max
#else
#ifdef __AMDGCN__
#define _ISNANd __ocml_isnan_f64
#define _ISNANf __ocml_isnan_f32
#define _ISINFd __ocml_isinf_f64
#define _ISINFf __ocml_isinf_f32
#define _ISFINITEd __ocml_isfinite_f64
#define _ISFINITEf __ocml_isfinite_f32
#define _COPYSIGNd __ocml_copysign_f64
#define _COPYSIGNf __ocml_copysign_f32
#define _SCALBNd __ocml_scalbn_f64
#define _SCALBNf __ocml_scalbn_f32
#define _ABSd __ocml_fabs_f64
#define _ABSf __ocml_fabs_f32
#define _LOGBd __ocml_logb_f64
#define _LOGBf __ocml_logb_f32
#define _fmaxd __ocml_fmax_f64
#define _fmaxf __ocml_fmax_f32
#else
#define _ISNANd __nv_isnand
#define _ISNANf __nv_isnanf
#define _ISINFd __nv_isinfd
#define _ISINFf __nv_isinff
#define _ISFINITEd __nv_isfinited
#define _ISFINITEf __nv_finitef
#define _COPYSIGNd __nv_copysign
#define _COPYSIGNf __nv_copysignf
#define _SCALBNd __nv_scalbn
#define _SCALBNf __nv_scalbnf
#define _ABSd __nv_fabs
#define _ABSf __nv_fabsf
#define _LOGBd __nv_logb
#define _LOGBf __nv_logbf
#define _fmaxd __nv_fmax
#define _fmaxf __nv_fmaxf
#endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif

__DEVICE__ double _Complex __muldc3(double __a, double __b, double __c,
                                    double __d) {
  double __ac = __a * __c;
  double __bd = __b * __d;
  double __ad = __a * __d;
  double __bc = __b * __c;
  double _Complex z;
  __real__(z) = __ac - __bd;
  __imag__(z) = __ad + __bc;
  if (_ISNANd(__real__(z)) && _ISNANd(__imag__(z))) {
    int __recalc = 0;
    if (_ISINFd(__a) || _ISINFd(__b)) {
      __a = _COPYSIGNd(_ISINFd(__a) ? 1 : 0, __a);
      __b = _COPYSIGNd(_ISINFd(__b) ? 1 : 0, __b);
      if (_ISNANd(__c))
        __c = _COPYSIGNd(0, __c);
      if (_ISNANd(__d))
        __d = _COPYSIGNd(0, __d);
      __recalc = 1;
    }
    if (_ISINFd(__c) || _ISINFd(__d)) {
      __c = _COPYSIGNd(_ISINFd(__c) ? 1 : 0, __c);
      __d = _COPYSIGNd(_ISINFd(__d) ? 1 : 0, __d);
      if (_ISNANd(__a))
        __a = _COPYSIGNd(0, __a);
      if (_ISNANd(__b))
        __b = _COPYSIGNd(0, __b);
      __recalc = 1;
    }
    if (!__recalc &&
        (_ISINFd(__ac) || _ISINFd(__bd) || _ISINFd(__ad) || _ISINFd(__bc))) {
      if (_ISNANd(__a))
        __a = _COPYSIGNd(0, __a);
      if (_ISNANd(__b))
        __b = _COPYSIGNd(0, __b);
      if (_ISNANd(__c))
        __c = _COPYSIGNd(0, __c);
      if (_ISNANd(__d))
        __d = _COPYSIGNd(0, __d);
      __recalc = 1;
    }
    if (__recalc) {
      // Can't use std::numeric_limits<double>::infinity() -- that doesn't have
      // a device overload (and isn't constexpr before C++11, naturally).
      __real__(z) = __builtin_huge_val() * (__a * __c - __b * __d);
      __imag__(z) = __builtin_huge_val() * (__a * __d + __b * __c);
    }
  }
  return z;
}

__DEVICE__ float _Complex __mulsc3(float __a, float __b, float __c, float __d) {
  float __ac = __a * __c;
  float __bd = __b * __d;
  float __ad = __a * __d;
  float __bc = __b * __c;
  float _Complex z;
  __real__(z) = __ac - __bd;
  __imag__(z) = __ad + __bc;
  if (_ISNANf(__real__(z)) && _ISNANf(__imag__(z))) {
    int __recalc = 0;
    if (_ISINFf(__a) || _ISINFf(__b)) {
      __a = _COPYSIGNf(_ISINFf(__a) ? 1 : 0, __a);
      __b = _COPYSIGNf(_ISINFf(__b) ? 1 : 0, __b);
      if (_ISNANf(__c))
        __c = _COPYSIGNf(0, __c);
      if (_ISNANf(__d))
        __d = _COPYSIGNf(0, __d);
      __recalc = 1;
    }
    if (_ISINFf(__c) || _ISINFf(__d)) {
      __c = _COPYSIGNf(_ISINFf(__c) ? 1 : 0, __c);
      __d = _COPYSIGNf(_ISINFf(__d) ? 1 : 0, __d);
      if (_ISNANf(__a))
        __a = _COPYSIGNf(0, __a);
      if (_ISNANf(__b))
        __b = _COPYSIGNf(0, __b);
      __recalc = 1;
    }
    if (!__recalc &&
        (_ISINFf(__ac) || _ISINFf(__bd) || _ISINFf(__ad) || _ISINFf(__bc))) {
      if (_ISNANf(__a))
        __a = _COPYSIGNf(0, __a);
      if (_ISNANf(__b))
        __b = _COPYSIGNf(0, __b);
      if (_ISNANf(__c))
        __c = _COPYSIGNf(0, __c);
      if (_ISNANf(__d))
        __d = _COPYSIGNf(0, __d);
      __recalc = 1;
    }
    if (__recalc) {
      __real__(z) = __builtin_huge_valf() * (__a * __c - __b * __d);
      __imag__(z) = __builtin_huge_valf() * (__a * __d + __b * __c);
    }
  }
  return z;
}

__DEVICE__ double _Complex __divdc3(double __a, double __b, double __c,
                                    double __d) {
  int __ilogbw = 0;
  // Can't use std::max, because that's defined in <algorithm>, and we don't
  // want to pull that in for every compile.  The CUDA headers define
  // ::max(float, float) and ::max(double, double), which is sufficient for us.
  double __logbw = _LOGBd(_fmaxd(_ABSd(__c), _ABSd(__d)));
  if (_ISFINITEd(__logbw)) {
    __ilogbw = (int)__logbw;
    __c = _SCALBNd(__c, -__ilogbw);
    __d = _SCALBNd(__d, -__ilogbw);
  }
  double __denom = __c * __c + __d * __d;
  double _Complex z;
  __real__(z) = _SCALBNd((__a * __c + __b * __d) / __denom, -__ilogbw);
  __imag__(z) = _SCALBNd((__b * __c - __a * __d) / __denom, -__ilogbw);
  if (_ISNANd(__real__(z)) && _ISNANd(__imag__(z))) {
    if ((__denom == 0.0) && (!_ISNANd(__a) || !_ISNANd(__b))) {
      __real__(z) = _COPYSIGNd(__builtin_huge_val(), __c) * __a;
      __imag__(z) = _COPYSIGNd(__builtin_huge_val(), __c) * __b;
    } else if ((_ISINFd(__a) || _ISINFd(__b)) && _ISFINITEd(__c) &&
               _ISFINITEd(__d)) {
      __a = _COPYSIGNd(_ISINFd(__a) ? 1.0 : 0.0, __a);
      __b = _COPYSIGNd(_ISINFd(__b) ? 1.0 : 0.0, __b);
      __real__(z) = __builtin_huge_val() * (__a * __c + __b * __d);
      __imag__(z) = __builtin_huge_val() * (__b * __c - __a * __d);
    } else if (_ISINFd(__logbw) && __logbw > 0.0 && _ISFINITEd(__a) &&
               _ISFINITEd(__b)) {
      __c = _COPYSIGNd(_ISINFd(__c) ? 1.0 : 0.0, __c);
      __d = _COPYSIGNd(_ISINFd(__d) ? 1.0 : 0.0, __d);
      __real__(z) = 0.0 * (__a * __c + __b * __d);
      __imag__(z) = 0.0 * (__b * __c - __a * __d);
    }
  }
  return z;
}

__DEVICE__ float _Complex __divsc3(float __a, float __b, float __c, float __d) {
  int __ilogbw = 0;
  float __logbw = _LOGBf(_fmaxf(_ABSf(__c), _ABSf(__d)));
  if (_ISFINITEf(__logbw)) {
    __ilogbw = (int)__logbw;
    __c = _SCALBNf(__c, -__ilogbw);
    __d = _SCALBNf(__d, -__ilogbw);
  }
  float __denom = __c * __c + __d * __d;
  float _Complex z;
  __real__(z) = _SCALBNf((__a * __c + __b * __d) / __denom, -__ilogbw);
  __imag__(z) = _SCALBNf((__b * __c - __a * __d) / __denom, -__ilogbw);
  if (_ISNANf(__real__(z)) && _ISNANf(__imag__(z))) {
    if ((__denom == 0) && (!_ISNANf(__a) || !_ISNANf(__b))) {
      __real__(z) = _COPYSIGNf(__builtin_huge_valf(), __c) * __a;
      __imag__(z) = _COPYSIGNf(__builtin_huge_valf(), __c) * __b;
    } else if ((_ISINFf(__a) || _ISINFf(__b)) && _ISFINITEf(__c) &&
               _ISFINITEf(__d)) {
      __a = _COPYSIGNf(_ISINFf(__a) ? 1 : 0, __a);
      __b = _COPYSIGNf(_ISINFf(__b) ? 1 : 0, __b);
      __real__(z) = __builtin_huge_valf() * (__a * __c + __b * __d);
      __imag__(z) = __builtin_huge_valf() * (__b * __c - __a * __d);
    } else if (_ISINFf(__logbw) && __logbw > 0 && _ISFINITEf(__a) &&
               _ISFINITEf(__b)) {
      __c = _COPYSIGNf(_ISINFf(__c) ? 1 : 0, __c);
      __d = _COPYSIGNf(_ISINFf(__d) ? 1 : 0, __d);
      __real__(z) = 0 * (__a * __c + __b * __d);
      __imag__(z) = 0 * (__b * __c - __a * __d);
    }
  }
  return z;
}

#if defined(__cplusplus)
} // extern "C"
#endif

#undef _ISNANd
#undef _ISNANf
#undef _ISINFd
#undef _ISINFf
#undef _COPYSIGNd
#undef _COPYSIGNf
#undef _ISFINITEd
#undef _ISFINITEf
#undef _SCALBNd
#undef _SCALBNf
#undef _ABSd
#undef _ABSf
#undef _LOGBd
#undef _LOGBf
#undef _fmaxd
#undef _fmaxf

#if defined(__OPENMP_NVPTX__) || defined(__OPENMP_AMDGCN__)
#pragma omp end declare target
#endif

#pragma pop_macro("__DEVICE__")

#endif // __CLANG_CUDA_COMPLEX_BUILTINS
