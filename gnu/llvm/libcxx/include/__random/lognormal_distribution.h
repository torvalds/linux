//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_LOGNORMAL_DISTRIBUTION_H
#define _LIBCPP___RANDOM_LOGNORMAL_DISTRIBUTION_H

#include <__config>
#include <__random/is_valid.h>
#include <__random/normal_distribution.h>
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
class _LIBCPP_TEMPLATE_VIS lognormal_distribution {
  static_assert(__libcpp_random_is_valid_realtype<_RealType>::value,
                "RealType must be a supported floating-point type");

public:
  // types
  typedef _RealType result_type;

  class _LIBCPP_TEMPLATE_VIS param_type {
    result_type __m_;
    result_type __s_;

  public:
    typedef lognormal_distribution distribution_type;

    _LIBCPP_HIDE_FROM_ABI explicit param_type(result_type __m = 0, result_type __s = 1) : __m_(__m), __s_(__s) {}

    _LIBCPP_HIDE_FROM_ABI result_type m() const { return __m_; }
    _LIBCPP_HIDE_FROM_ABI result_type s() const { return __s_; }

    friend _LIBCPP_HIDE_FROM_ABI bool operator==(const param_type& __x, const param_type& __y) {
      return __x.__m_ == __y.__m_ && __x.__s_ == __y.__s_;
    }
    friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const param_type& __x, const param_type& __y) { return !(__x == __y); }
  };

private:
  normal_distribution<result_type> __nd_;

public:
  // constructor and reset functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI lognormal_distribution() : lognormal_distribution(0) {}
  _LIBCPP_HIDE_FROM_ABI explicit lognormal_distribution(result_type __m, result_type __s = 1) : __nd_(__m, __s) {}
#else
  _LIBCPP_HIDE_FROM_ABI explicit lognormal_distribution(result_type __m = 0, result_type __s = 1) : __nd_(__m, __s) {}
#endif
  _LIBCPP_HIDE_FROM_ABI explicit lognormal_distribution(const param_type& __p) : __nd_(__p.m(), __p.s()) {}
  _LIBCPP_HIDE_FROM_ABI void reset() { __nd_.reset(); }

  // generating functions
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g) {
    return std::exp(__nd_(__g));
  }

  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g, const param_type& __p) {
    typename normal_distribution<result_type>::param_type __pn(__p.m(), __p.s());
    return std::exp(__nd_(__g, __pn));
  }

  // property functions
  _LIBCPP_HIDE_FROM_ABI result_type m() const { return __nd_.mean(); }
  _LIBCPP_HIDE_FROM_ABI result_type s() const { return __nd_.stddev(); }

  _LIBCPP_HIDE_FROM_ABI param_type param() const { return param_type(__nd_.mean(), __nd_.stddev()); }
  _LIBCPP_HIDE_FROM_ABI void param(const param_type& __p) {
    typename normal_distribution<result_type>::param_type __pn(__p.m(), __p.s());
    __nd_.param(__pn);
  }

  _LIBCPP_HIDE_FROM_ABI result_type min() const { return 0; }
  _LIBCPP_HIDE_FROM_ABI result_type max() const { return numeric_limits<result_type>::infinity(); }

  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const lognormal_distribution& __x, const lognormal_distribution& __y) {
    return __x.__nd_ == __y.__nd_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const lognormal_distribution& __x, const lognormal_distribution& __y) {
    return !(__x == __y);
  }

  template <class _CharT, class _Traits, class _RT>
  friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const lognormal_distribution<_RT>& __x);

  template <class _CharT, class _Traits, class _RT>
  friend basic_istream<_CharT, _Traits>&
  operator>>(basic_istream<_CharT, _Traits>& __is, lognormal_distribution<_RT>& __x);
};

template <class _CharT, class _Traits, class _RT>
inline _LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const lognormal_distribution<_RT>& __x) {
  return __os << __x.__nd_;
}

template <class _CharT, class _Traits, class _RT>
inline _LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, lognormal_distribution<_RT>& __x) {
  return __is >> __x.__nd_;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_LOGNORMAL_DISTRIBUTION_H
