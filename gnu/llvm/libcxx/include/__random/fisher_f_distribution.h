//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_FISHER_F_DISTRIBUTION_H
#define _LIBCPP___RANDOM_FISHER_F_DISTRIBUTION_H

#include <__config>
#include <__random/gamma_distribution.h>
#include <__random/is_valid.h>
#include <iosfwd>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _RealType = double>
class _LIBCPP_TEMPLATE_VIS fisher_f_distribution {
  static_assert(__libcpp_random_is_valid_realtype<_RealType>::value,
                "RealType must be a supported floating-point type");

public:
  // types
  typedef _RealType result_type;

  class _LIBCPP_TEMPLATE_VIS param_type {
    result_type __m_;
    result_type __n_;

  public:
    typedef fisher_f_distribution distribution_type;

    _LIBCPP_HIDE_FROM_ABI explicit param_type(result_type __m = 1, result_type __n = 1) : __m_(__m), __n_(__n) {}

    _LIBCPP_HIDE_FROM_ABI result_type m() const { return __m_; }
    _LIBCPP_HIDE_FROM_ABI result_type n() const { return __n_; }

    friend _LIBCPP_HIDE_FROM_ABI bool operator==(const param_type& __x, const param_type& __y) {
      return __x.__m_ == __y.__m_ && __x.__n_ == __y.__n_;
    }
    friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const param_type& __x, const param_type& __y) { return !(__x == __y); }
  };

private:
  param_type __p_;

public:
  // constructor and reset functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI fisher_f_distribution() : fisher_f_distribution(1) {}
  _LIBCPP_HIDE_FROM_ABI explicit fisher_f_distribution(result_type __m, result_type __n = 1)
      : __p_(param_type(__m, __n)) {}
#else
  _LIBCPP_HIDE_FROM_ABI explicit fisher_f_distribution(result_type __m = 1, result_type __n = 1)
      : __p_(param_type(__m, __n)) {}
#endif
  _LIBCPP_HIDE_FROM_ABI explicit fisher_f_distribution(const param_type& __p) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI void reset() {}

  // generating functions
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g) {
    return (*this)(__g, __p_);
  }
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g, const param_type& __p);

  // property functions
  _LIBCPP_HIDE_FROM_ABI result_type m() const { return __p_.m(); }
  _LIBCPP_HIDE_FROM_ABI result_type n() const { return __p_.n(); }

  _LIBCPP_HIDE_FROM_ABI param_type param() const { return __p_; }
  _LIBCPP_HIDE_FROM_ABI void param(const param_type& __p) { __p_ = __p; }

  _LIBCPP_HIDE_FROM_ABI result_type min() const { return 0; }
  _LIBCPP_HIDE_FROM_ABI result_type max() const { return numeric_limits<result_type>::infinity(); }

  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const fisher_f_distribution& __x, const fisher_f_distribution& __y) {
    return __x.__p_ == __y.__p_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const fisher_f_distribution& __x, const fisher_f_distribution& __y) {
    return !(__x == __y);
  }
};

template <class _RealType>
template <class _URNG>
_RealType fisher_f_distribution<_RealType>::operator()(_URNG& __g, const param_type& __p) {
  static_assert(__libcpp_random_is_valid_urng<_URNG>::value, "");
  gamma_distribution<result_type> __gdm(__p.m() * result_type(.5));
  gamma_distribution<result_type> __gdn(__p.n() * result_type(.5));
  return __p.n() * __gdm(__g) / (__p.m() * __gdn(__g));
}

template <class _CharT, class _Traits, class _RT>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const fisher_f_distribution<_RT>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _OStream;
  __os.flags(_OStream::dec | _OStream::left | _OStream::fixed | _OStream::scientific);
  _CharT __sp = __os.widen(' ');
  __os.fill(__sp);
  __os << __x.m() << __sp << __x.n();
  return __os;
}

template <class _CharT, class _Traits, class _RT>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, fisher_f_distribution<_RT>& __x) {
  typedef fisher_f_distribution<_RT> _Eng;
  typedef typename _Eng::result_type result_type;
  typedef typename _Eng::param_type param_type;
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  result_type __m;
  result_type __n;
  __is >> __m >> __n;
  if (!__is.fail())
    __x.param(param_type(__m, __n));
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_FISHER_F_DISTRIBUTION_H
