//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//
// This file reimplements builtins that are normally provided by compiler-rt, which is
// not provided on Windows. This should go away once compiler-rt is shipped on Windows.
//

#include <cmath>
#include <complex>

template <class T>
static std::__complex_t<T> mul_impl(T a, T b, T c, T d) {
  T __ac = a * c;
  T __bd = b * d;
  T __ad = a * d;
  T __bc = b * c;
  T __x  = __ac - __bd;
  T __y  = __ad + __bc;
  if (std::isnan(__x) && std::isnan(__y)) {
    bool recalc = false;
    if (std::isinf(a) || std::isinf(b)) {
      a = std::copysign(std::isinf(a) ? T(1) : T(0), a);
      b = std::copysign(std::isinf(b) ? T(1) : T(0), b);
      if (std::isnan(c))
        c = std::copysign(T(0), c);
      if (std::isnan(d))
        d = std::copysign(T(0), d);
      recalc = true;
    }
    if (std::isinf(c) || std::isinf(d)) {
      c = std::copysign(std::isinf(c) ? T(1) : T(0), c);
      d = std::copysign(std::isinf(d) ? T(1) : T(0), d);
      if (std::isnan(a))
        a = std::copysign(T(0), a);
      if (std::isnan(b))
        b = std::copysign(T(0), b);
      recalc = true;
    }
    if (!recalc && (std::isinf(__ac) || std::isinf(__bd) || std::isinf(__ad) || std::isinf(__bc))) {
      if (std::isnan(a))
        a = std::copysign(T(0), a);
      if (std::isnan(b))
        b = std::copysign(T(0), b);
      if (std::isnan(c))
        c = std::copysign(T(0), c);
      if (std::isnan(d))
        d = std::copysign(T(0), d);
      recalc = true;
    }
    if (recalc) {
      __x = T(INFINITY) * (a * c - b * d);
      __y = T(INFINITY) * (a * d + b * c);
    }
  }
  return {__x, __y};
}

extern "C" _LIBCPP_EXPORTED_FROM_ABI _Complex double __muldc3(double a, double b, double c, double d) {
  return mul_impl(a, b, c, d);
}

extern "C" _LIBCPP_EXPORTED_FROM_ABI _Complex float __mulsc3(float a, float b, float c, float d) {
  return mul_impl(a, b, c, d);
}

template <class T>
std::__complex_t<T> div_impl(T a, T b, T c, T d) {
  int ilogbw = 0;
  T __logbw  = std::logb(std::fmax(std::fabs(c), std::fabs(d)));
  if (std::isfinite(__logbw)) {
    ilogbw = static_cast<int>(__logbw);
    c      = std::scalbn(c, -ilogbw);
    d      = std::scalbn(d, -ilogbw);
  }

  T denom = c * c + d * d;
  T x     = std::scalbn((a * c + b * d) / denom, -ilogbw);
  T y     = std::scalbn((b * c - a * d) / denom, -ilogbw);
  if (std::isnan(x) && std::isnan(y)) {
    if ((denom == T(0)) && (!std::isnan(a) || !std::isnan(b))) {
      x = std::copysign(T(INFINITY), c) * a;
      y = std::copysign(T(INFINITY), c) * b;
    } else if ((std::isinf(a) || std::isinf(b)) && std::isfinite(c) && std::isfinite(d)) {
      a = std::copysign(std::isinf(a) ? T(1) : T(0), a);
      b = std::copysign(std::isinf(b) ? T(1) : T(0), b);
      x = T(INFINITY) * (a * c + b * d);
      y = T(INFINITY) * (b * c - a * d);
    } else if (std::isinf(__logbw) && __logbw > T(0) && std::isfinite(a) && std::isfinite(b)) {
      c = std::copysign(std::isinf(c) ? T(1) : T(0), c);
      d = std::copysign(std::isinf(d) ? T(1) : T(0), d);
      x = T(0) * (a * c + b * d);
      y = T(0) * (b * c - a * d);
    }
  }
  return {x, y};
}

extern "C" _LIBCPP_EXPORTED_FROM_ABI _Complex double __divdc3(double a, double b, double c, double d) {
  return div_impl(a, b, c, d);
}

extern "C" _LIBCPP_EXPORTED_FROM_ABI _Complex float __divsc3(float a, float b, float c, float d) {
  return div_impl(a, b, c, d);
}
