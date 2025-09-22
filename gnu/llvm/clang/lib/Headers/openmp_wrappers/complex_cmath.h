//===------------------------- __complex_cmath.h --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// std::complex header copied from the libcxx source and simplified for use in
// OpenMP target offload regions.
//
//===----------------------------------------------------------------------===//

#ifndef _OPENMP
#error "This file is for OpenMP compilation only."
#endif

#ifndef __cplusplus
#error "This file is for C++ compilation only."
#endif

#ifndef _LIBCPP_COMPLEX
#define _LIBCPP_COMPLEX

#include <cmath>
#include <type_traits>

#define __DEVICE__ static constexpr __attribute__((nothrow))

namespace std {

// abs

template <class _Tp> __DEVICE__ _Tp abs(const std::complex<_Tp> &__c) {
  return hypot(__c.real(), __c.imag());
}

// arg

template <class _Tp> __DEVICE__ _Tp arg(const std::complex<_Tp> &__c) {
  return atan2(__c.imag(), __c.real());
}

template <class _Tp>
typename enable_if<is_integral<_Tp>::value || is_same<_Tp, double>::value,
                   double>::type
arg(_Tp __re) {
  return atan2(0., __re);
}

template <class _Tp>
typename enable_if<is_same<_Tp, float>::value, float>::type arg(_Tp __re) {
  return atan2f(0.F, __re);
}

// norm

template <class _Tp> __DEVICE__ _Tp norm(const std::complex<_Tp> &__c) {
  if (std::isinf(__c.real()))
    return abs(__c.real());
  if (std::isinf(__c.imag()))
    return abs(__c.imag());
  return __c.real() * __c.real() + __c.imag() * __c.imag();
}

// conj

template <class _Tp> std::complex<_Tp> conj(const std::complex<_Tp> &__c) {
  return std::complex<_Tp>(__c.real(), -__c.imag());
}

// proj

template <class _Tp> std::complex<_Tp> proj(const std::complex<_Tp> &__c) {
  std::complex<_Tp> __r = __c;
  if (std::isinf(__c.real()) || std::isinf(__c.imag()))
    __r = std::complex<_Tp>(INFINITY, copysign(_Tp(0), __c.imag()));
  return __r;
}

// polar

template <class _Tp>
complex<_Tp> polar(const _Tp &__rho, const _Tp &__theta = _Tp()) {
  if (std::isnan(__rho) || signbit(__rho))
    return std::complex<_Tp>(_Tp(NAN), _Tp(NAN));
  if (std::isnan(__theta)) {
    if (std::isinf(__rho))
      return std::complex<_Tp>(__rho, __theta);
    return std::complex<_Tp>(__theta, __theta);
  }
  if (std::isinf(__theta)) {
    if (std::isinf(__rho))
      return std::complex<_Tp>(__rho, _Tp(NAN));
    return std::complex<_Tp>(_Tp(NAN), _Tp(NAN));
  }
  _Tp __x = __rho * cos(__theta);
  if (std::isnan(__x))
    __x = 0;
  _Tp __y = __rho * sin(__theta);
  if (std::isnan(__y))
    __y = 0;
  return std::complex<_Tp>(__x, __y);
}

// log

template <class _Tp> std::complex<_Tp> log(const std::complex<_Tp> &__x) {
  return std::complex<_Tp>(log(abs(__x)), arg(__x));
}

// log10

template <class _Tp> std::complex<_Tp> log10(const std::complex<_Tp> &__x) {
  return log(__x) / log(_Tp(10));
}

// sqrt

template <class _Tp>
__DEVICE__ std::complex<_Tp> sqrt(const std::complex<_Tp> &__x) {
  if (std::isinf(__x.imag()))
    return std::complex<_Tp>(_Tp(INFINITY), __x.imag());
  if (std::isinf(__x.real())) {
    if (__x.real() > _Tp(0))
      return std::complex<_Tp>(__x.real(), std::isnan(__x.imag())
                                               ? __x.imag()
                                               : copysign(_Tp(0), __x.imag()));
    return std::complex<_Tp>(std::isnan(__x.imag()) ? __x.imag() : _Tp(0),
                             copysign(__x.real(), __x.imag()));
  }
  return polar(sqrt(abs(__x)), arg(__x) / _Tp(2));
}

// exp

template <class _Tp>
__DEVICE__ std::complex<_Tp> exp(const std::complex<_Tp> &__x) {
  _Tp __i = __x.imag();
  if (std::isinf(__x.real())) {
    if (__x.real() < _Tp(0)) {
      if (!std::isfinite(__i))
        __i = _Tp(1);
    } else if (__i == 0 || !std::isfinite(__i)) {
      if (std::isinf(__i))
        __i = _Tp(NAN);
      return std::complex<_Tp>(__x.real(), __i);
    }
  } else if (std::isnan(__x.real()) && __x.imag() == 0)
    return __x;
  _Tp __e = exp(__x.real());
  return std::complex<_Tp>(__e * cos(__i), __e * sin(__i));
}

// pow

template <class _Tp>
std::complex<_Tp> pow(const std::complex<_Tp> &__x,
                      const std::complex<_Tp> &__y) {
  return exp(__y * log(__x));
}

// __sqr, computes pow(x, 2)

template <class _Tp> std::complex<_Tp> __sqr(const std::complex<_Tp> &__x) {
  return std::complex<_Tp>((__x.real() - __x.imag()) *
                               (__x.real() + __x.imag()),
                           _Tp(2) * __x.real() * __x.imag());
}

// asinh

template <class _Tp>
__DEVICE__ std::complex<_Tp> asinh(const std::complex<_Tp> &__x) {
  const _Tp __pi(atan2(+0., -0.));
  if (std::isinf(__x.real())) {
    if (std::isnan(__x.imag()))
      return __x;
    if (std::isinf(__x.imag()))
      return std::complex<_Tp>(__x.real(),
                               copysign(__pi * _Tp(0.25), __x.imag()));
    return std::complex<_Tp>(__x.real(), copysign(_Tp(0), __x.imag()));
  }
  if (std::isnan(__x.real())) {
    if (std::isinf(__x.imag()))
      return std::complex<_Tp>(__x.imag(), __x.real());
    if (__x.imag() == 0)
      return __x;
    return std::complex<_Tp>(__x.real(), __x.real());
  }
  if (std::isinf(__x.imag()))
    return std::complex<_Tp>(copysign(__x.imag(), __x.real()),
                             copysign(__pi / _Tp(2), __x.imag()));
  std::complex<_Tp> __z = log(__x + sqrt(__sqr(__x) + _Tp(1)));
  return std::complex<_Tp>(copysign(__z.real(), __x.real()),
                           copysign(__z.imag(), __x.imag()));
}

// acosh

template <class _Tp>
__DEVICE__ std::complex<_Tp> acosh(const std::complex<_Tp> &__x) {
  const _Tp __pi(atan2(+0., -0.));
  if (std::isinf(__x.real())) {
    if (std::isnan(__x.imag()))
      return std::complex<_Tp>(abs(__x.real()), __x.imag());
    if (std::isinf(__x.imag())) {
      if (__x.real() > 0)
        return std::complex<_Tp>(__x.real(),
                                 copysign(__pi * _Tp(0.25), __x.imag()));
      else
        return std::complex<_Tp>(-__x.real(),
                                 copysign(__pi * _Tp(0.75), __x.imag()));
    }
    if (__x.real() < 0)
      return std::complex<_Tp>(-__x.real(), copysign(__pi, __x.imag()));
    return std::complex<_Tp>(__x.real(), copysign(_Tp(0), __x.imag()));
  }
  if (std::isnan(__x.real())) {
    if (std::isinf(__x.imag()))
      return std::complex<_Tp>(abs(__x.imag()), __x.real());
    return std::complex<_Tp>(__x.real(), __x.real());
  }
  if (std::isinf(__x.imag()))
    return std::complex<_Tp>(abs(__x.imag()),
                             copysign(__pi / _Tp(2), __x.imag()));
  std::complex<_Tp> __z = log(__x + sqrt(__sqr(__x) - _Tp(1)));
  return std::complex<_Tp>(copysign(__z.real(), _Tp(0)),
                           copysign(__z.imag(), __x.imag()));
}

// atanh

template <class _Tp>
__DEVICE__ std::complex<_Tp> atanh(const std::complex<_Tp> &__x) {
  const _Tp __pi(atan2(+0., -0.));
  if (std::isinf(__x.imag())) {
    return std::complex<_Tp>(copysign(_Tp(0), __x.real()),
                             copysign(__pi / _Tp(2), __x.imag()));
  }
  if (std::isnan(__x.imag())) {
    if (std::isinf(__x.real()) || __x.real() == 0)
      return std::complex<_Tp>(copysign(_Tp(0), __x.real()), __x.imag());
    return std::complex<_Tp>(__x.imag(), __x.imag());
  }
  if (std::isnan(__x.real())) {
    return std::complex<_Tp>(__x.real(), __x.real());
  }
  if (std::isinf(__x.real())) {
    return std::complex<_Tp>(copysign(_Tp(0), __x.real()),
                             copysign(__pi / _Tp(2), __x.imag()));
  }
  if (abs(__x.real()) == _Tp(1) && __x.imag() == _Tp(0)) {
    return std::complex<_Tp>(copysign(_Tp(INFINITY), __x.real()),
                             copysign(_Tp(0), __x.imag()));
  }
  std::complex<_Tp> __z = log((_Tp(1) + __x) / (_Tp(1) - __x)) / _Tp(2);
  return std::complex<_Tp>(copysign(__z.real(), __x.real()),
                           copysign(__z.imag(), __x.imag()));
}

// sinh

template <class _Tp>
__DEVICE__ std::complex<_Tp> sinh(const std::complex<_Tp> &__x) {
  if (std::isinf(__x.real()) && !std::isfinite(__x.imag()))
    return std::complex<_Tp>(__x.real(), _Tp(NAN));
  if (__x.real() == 0 && !std::isfinite(__x.imag()))
    return std::complex<_Tp>(__x.real(), _Tp(NAN));
  if (__x.imag() == 0 && !std::isfinite(__x.real()))
    return __x;
  return std::complex<_Tp>(sinh(__x.real()) * cos(__x.imag()),
                           cosh(__x.real()) * sin(__x.imag()));
}

// cosh

template <class _Tp>
__DEVICE__ std::complex<_Tp> cosh(const std::complex<_Tp> &__x) {
  if (std::isinf(__x.real()) && !std::isfinite(__x.imag()))
    return std::complex<_Tp>(abs(__x.real()), _Tp(NAN));
  if (__x.real() == 0 && !std::isfinite(__x.imag()))
    return std::complex<_Tp>(_Tp(NAN), __x.real());
  if (__x.real() == 0 && __x.imag() == 0)
    return std::complex<_Tp>(_Tp(1), __x.imag());
  if (__x.imag() == 0 && !std::isfinite(__x.real()))
    return std::complex<_Tp>(abs(__x.real()), __x.imag());
  return std::complex<_Tp>(cosh(__x.real()) * cos(__x.imag()),
                           sinh(__x.real()) * sin(__x.imag()));
}

// tanh

template <class _Tp>
__DEVICE__ std::complex<_Tp> tanh(const std::complex<_Tp> &__x) {
  if (std::isinf(__x.real())) {
    if (!std::isfinite(__x.imag()))
      return std::complex<_Tp>(_Tp(1), _Tp(0));
    return std::complex<_Tp>(_Tp(1),
                             copysign(_Tp(0), sin(_Tp(2) * __x.imag())));
  }
  if (std::isnan(__x.real()) && __x.imag() == 0)
    return __x;
  _Tp __2r(_Tp(2) * __x.real());
  _Tp __2i(_Tp(2) * __x.imag());
  _Tp __d(cosh(__2r) + cos(__2i));
  _Tp __2rsh(sinh(__2r));
  if (std::isinf(__2rsh) && std::isinf(__d))
    return std::complex<_Tp>(__2rsh > _Tp(0) ? _Tp(1) : _Tp(-1),
                             __2i > _Tp(0) ? _Tp(0) : _Tp(-0.));
  return std::complex<_Tp>(__2rsh / __d, sin(__2i) / __d);
}

// asin

template <class _Tp>
__DEVICE__ std::complex<_Tp> asin(const std::complex<_Tp> &__x) {
  std::complex<_Tp> __z = asinh(complex<_Tp>(-__x.imag(), __x.real()));
  return std::complex<_Tp>(__z.imag(), -__z.real());
}

// acos

template <class _Tp>
__DEVICE__ std::complex<_Tp> acos(const std::complex<_Tp> &__x) {
  const _Tp __pi(atan2(+0., -0.));
  if (std::isinf(__x.real())) {
    if (std::isnan(__x.imag()))
      return std::complex<_Tp>(__x.imag(), __x.real());
    if (std::isinf(__x.imag())) {
      if (__x.real() < _Tp(0))
        return std::complex<_Tp>(_Tp(0.75) * __pi, -__x.imag());
      return std::complex<_Tp>(_Tp(0.25) * __pi, -__x.imag());
    }
    if (__x.real() < _Tp(0))
      return std::complex<_Tp>(__pi,
                               signbit(__x.imag()) ? -__x.real() : __x.real());
    return std::complex<_Tp>(_Tp(0),
                             signbit(__x.imag()) ? __x.real() : -__x.real());
  }
  if (std::isnan(__x.real())) {
    if (std::isinf(__x.imag()))
      return std::complex<_Tp>(__x.real(), -__x.imag());
    return std::complex<_Tp>(__x.real(), __x.real());
  }
  if (std::isinf(__x.imag()))
    return std::complex<_Tp>(__pi / _Tp(2), -__x.imag());
  if (__x.real() == 0 && (__x.imag() == 0 || isnan(__x.imag())))
    return std::complex<_Tp>(__pi / _Tp(2), -__x.imag());
  std::complex<_Tp> __z = log(__x + sqrt(__sqr(__x) - _Tp(1)));
  if (signbit(__x.imag()))
    return std::complex<_Tp>(abs(__z.imag()), abs(__z.real()));
  return std::complex<_Tp>(abs(__z.imag()), -abs(__z.real()));
}

// atan

template <class _Tp>
__DEVICE__ std::complex<_Tp> atan(const std::complex<_Tp> &__x) {
  std::complex<_Tp> __z = atanh(complex<_Tp>(-__x.imag(), __x.real()));
  return std::complex<_Tp>(__z.imag(), -__z.real());
}

// sin

template <class _Tp>
__DEVICE__ std::complex<_Tp> sin(const std::complex<_Tp> &__x) {
  std::complex<_Tp> __z = sinh(complex<_Tp>(-__x.imag(), __x.real()));
  return std::complex<_Tp>(__z.imag(), -__z.real());
}

// cos

template <class _Tp> std::complex<_Tp> cos(const std::complex<_Tp> &__x) {
  return cosh(complex<_Tp>(-__x.imag(), __x.real()));
}

// tan

template <class _Tp>
__DEVICE__ std::complex<_Tp> tan(const std::complex<_Tp> &__x) {
  std::complex<_Tp> __z = tanh(complex<_Tp>(-__x.imag(), __x.real()));
  return std::complex<_Tp>(__z.imag(), -__z.real());
}

} // namespace std

#endif
