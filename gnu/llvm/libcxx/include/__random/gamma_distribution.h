//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_GAMMA_DISTRIBUTION_H
#define _LIBCPP___RANDOM_GAMMA_DISTRIBUTION_H

#include <__config>
#include <__random/exponential_distribution.h>
#include <__random/is_valid.h>
#include <__random/uniform_real_distribution.h>
#include <cmath>
#include <iosfwd>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _RealType = double>
class _LIBCPP_TEMPLATE_VIS gamma_distribution {
  static_assert(__libcpp_random_is_valid_realtype<_RealType>::value,
                "RealType must be a supported floating-point type");

public:
  // types
  typedef _RealType result_type;

  class _LIBCPP_TEMPLATE_VIS param_type {
    result_type __alpha_;
    result_type __beta_;

  public:
    typedef gamma_distribution distribution_type;

    _LIBCPP_HIDE_FROM_ABI explicit param_type(result_type __alpha = 1, result_type __beta = 1)
        : __alpha_(__alpha), __beta_(__beta) {}

    _LIBCPP_HIDE_FROM_ABI result_type alpha() const { return __alpha_; }
    _LIBCPP_HIDE_FROM_ABI result_type beta() const { return __beta_; }

    friend _LIBCPP_HIDE_FROM_ABI bool operator==(const param_type& __x, const param_type& __y) {
      return __x.__alpha_ == __y.__alpha_ && __x.__beta_ == __y.__beta_;
    }
    friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const param_type& __x, const param_type& __y) { return !(__x == __y); }
  };

private:
  param_type __p_;

public:
  // constructors and reset functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI gamma_distribution() : gamma_distribution(1) {}
  _LIBCPP_HIDE_FROM_ABI explicit gamma_distribution(result_type __alpha, result_type __beta = 1)
      : __p_(param_type(__alpha, __beta)) {}
#else
  _LIBCPP_HIDE_FROM_ABI explicit gamma_distribution(result_type __alpha = 1, result_type __beta = 1)
      : __p_(param_type(__alpha, __beta)) {}
#endif
  _LIBCPP_HIDE_FROM_ABI explicit gamma_distribution(const param_type& __p) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI void reset() {}

  // generating functions
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g) {
    return (*this)(__g, __p_);
  }
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g, const param_type& __p);

  // property functions
  _LIBCPP_HIDE_FROM_ABI result_type alpha() const { return __p_.alpha(); }
  _LIBCPP_HIDE_FROM_ABI result_type beta() const { return __p_.beta(); }

  _LIBCPP_HIDE_FROM_ABI param_type param() const { return __p_; }
  _LIBCPP_HIDE_FROM_ABI void param(const param_type& __p) { __p_ = __p; }

  _LIBCPP_HIDE_FROM_ABI result_type min() const { return 0; }
  _LIBCPP_HIDE_FROM_ABI result_type max() const { return numeric_limits<result_type>::infinity(); }

  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const gamma_distribution& __x, const gamma_distribution& __y) {
    return __x.__p_ == __y.__p_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const gamma_distribution& __x, const gamma_distribution& __y) {
    return !(__x == __y);
  }
};

template <class _RealType>
template <class _URNG>
_RealType gamma_distribution<_RealType>::operator()(_URNG& __g, const param_type& __p) {
  static_assert(__libcpp_random_is_valid_urng<_URNG>::value, "");
  result_type __a = __p.alpha();
  uniform_real_distribution<result_type> __gen(0, 1);
  exponential_distribution<result_type> __egen;
  result_type __x;
  if (__a == 1)
    __x = __egen(__g);
  else if (__a > 1) {
    const result_type __b = __a - 1;
    const result_type __c = 3 * __a - result_type(0.75);
    while (true) {
      const result_type __u = __gen(__g);
      const result_type __v = __gen(__g);
      const result_type __w = __u * (1 - __u);
      if (__w != 0) {
        const result_type __y = std::sqrt(__c / __w) * (__u - result_type(0.5));
        __x                   = __b + __y;
        if (__x >= 0) {
          const result_type __z = 64 * __w * __w * __w * __v * __v;
          if (__z <= 1 - 2 * __y * __y / __x)
            break;
          if (std::log(__z) <= 2 * (__b * std::log(__x / __b) - __y))
            break;
        }
      }
    }
  } else // __a < 1
  {
    while (true) {
      const result_type __u  = __gen(__g);
      const result_type __es = __egen(__g);
      if (__u <= 1 - __a) {
        __x = std::pow(__u, 1 / __a);
        if (__x <= __es)
          break;
      } else {
        const result_type __e = -std::log((1 - __u) / __a);
        __x                   = std::pow(1 - __a + __a * __e, 1 / __a);
        if (__x <= __e + __es)
          break;
      }
    }
  }
  return __x * __p.beta();
}

template <class _CharT, class _Traits, class _RT>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const gamma_distribution<_RT>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _OStream;
  __os.flags(_OStream::dec | _OStream::left | _OStream::fixed | _OStream::scientific);
  _CharT __sp = __os.widen(' ');
  __os.fill(__sp);
  __os << __x.alpha() << __sp << __x.beta();
  return __os;
}

template <class _CharT, class _Traits, class _RT>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, gamma_distribution<_RT>& __x) {
  typedef gamma_distribution<_RT> _Eng;
  typedef typename _Eng::result_type result_type;
  typedef typename _Eng::param_type param_type;
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  result_type __alpha;
  result_type __beta;
  __is >> __alpha >> __beta;
  if (!__is.fail())
    __x.param(param_type(__alpha, __beta));
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_GAMMA_DISTRIBUTION_H
