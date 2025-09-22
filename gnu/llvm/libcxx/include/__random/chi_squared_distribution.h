//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_CHI_SQUARED_DISTRIBUTION_H
#define _LIBCPP___RANDOM_CHI_SQUARED_DISTRIBUTION_H

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
class _LIBCPP_TEMPLATE_VIS chi_squared_distribution {
  static_assert(__libcpp_random_is_valid_realtype<_RealType>::value,
                "RealType must be a supported floating-point type");

public:
  // types
  typedef _RealType result_type;

  class _LIBCPP_TEMPLATE_VIS param_type {
    result_type __n_;

  public:
    typedef chi_squared_distribution distribution_type;

    _LIBCPP_HIDE_FROM_ABI explicit param_type(result_type __n = 1) : __n_(__n) {}

    _LIBCPP_HIDE_FROM_ABI result_type n() const { return __n_; }

    friend _LIBCPP_HIDE_FROM_ABI bool operator==(const param_type& __x, const param_type& __y) {
      return __x.__n_ == __y.__n_;
    }
    friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const param_type& __x, const param_type& __y) { return !(__x == __y); }
  };

private:
  param_type __p_;

public:
  // constructor and reset functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI chi_squared_distribution() : chi_squared_distribution(1) {}
  _LIBCPP_HIDE_FROM_ABI explicit chi_squared_distribution(result_type __n) : __p_(param_type(__n)) {}
#else
  _LIBCPP_HIDE_FROM_ABI explicit chi_squared_distribution(result_type __n = 1) : __p_(param_type(__n)) {}
#endif
  _LIBCPP_HIDE_FROM_ABI explicit chi_squared_distribution(const param_type& __p) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI void reset() {}

  // generating functions
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g) {
    return (*this)(__g, __p_);
  }
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g, const param_type& __p) {
    return gamma_distribution<result_type>(__p.n() / 2, 2)(__g);
  }

  // property functions
  _LIBCPP_HIDE_FROM_ABI result_type n() const { return __p_.n(); }

  _LIBCPP_HIDE_FROM_ABI param_type param() const { return __p_; }
  _LIBCPP_HIDE_FROM_ABI void param(const param_type& __p) { __p_ = __p; }

  _LIBCPP_HIDE_FROM_ABI result_type min() const { return 0; }
  _LIBCPP_HIDE_FROM_ABI result_type max() const { return numeric_limits<result_type>::infinity(); }

  friend _LIBCPP_HIDE_FROM_ABI bool
  operator==(const chi_squared_distribution& __x, const chi_squared_distribution& __y) {
    return __x.__p_ == __y.__p_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool
  operator!=(const chi_squared_distribution& __x, const chi_squared_distribution& __y) {
    return !(__x == __y);
  }
};

template <class _CharT, class _Traits, class _RT>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const chi_squared_distribution<_RT>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _OStream;
  __os.flags(_OStream::dec | _OStream::left | _OStream::fixed | _OStream::scientific);
  __os << __x.n();
  return __os;
}

template <class _CharT, class _Traits, class _RT>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, chi_squared_distribution<_RT>& __x) {
  typedef chi_squared_distribution<_RT> _Eng;
  typedef typename _Eng::result_type result_type;
  typedef typename _Eng::param_type param_type;
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  result_type __n;
  __is >> __n;
  if (!__is.fail())
    __x.param(param_type(__n));
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_CHI_SQUARED_DISTRIBUTION_H
