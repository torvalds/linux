//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_GEOMETRIC_DISTRIBUTION_H
#define _LIBCPP___RANDOM_GEOMETRIC_DISTRIBUTION_H

#include <__config>
#include <__random/is_valid.h>
#include <__random/negative_binomial_distribution.h>
#include <iosfwd>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _IntType = int>
class _LIBCPP_TEMPLATE_VIS geometric_distribution {
  static_assert(__libcpp_random_is_valid_inttype<_IntType>::value, "IntType must be a supported integer type");

public:
  // types
  typedef _IntType result_type;

  class _LIBCPP_TEMPLATE_VIS param_type {
    double __p_;

  public:
    typedef geometric_distribution distribution_type;

    _LIBCPP_HIDE_FROM_ABI explicit param_type(double __p = 0.5) : __p_(__p) {}

    _LIBCPP_HIDE_FROM_ABI double p() const { return __p_; }

    friend _LIBCPP_HIDE_FROM_ABI bool operator==(const param_type& __x, const param_type& __y) {
      return __x.__p_ == __y.__p_;
    }
    friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const param_type& __x, const param_type& __y) { return !(__x == __y); }
  };

private:
  param_type __p_;

public:
  // constructors and reset functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI geometric_distribution() : geometric_distribution(0.5) {}
  _LIBCPP_HIDE_FROM_ABI explicit geometric_distribution(double __p) : __p_(__p) {}
#else
  _LIBCPP_HIDE_FROM_ABI explicit geometric_distribution(double __p = 0.5) : __p_(__p) {}
#endif
  _LIBCPP_HIDE_FROM_ABI explicit geometric_distribution(const param_type& __p) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI void reset() {}

  // generating functions
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g) {
    return (*this)(__g, __p_);
  }
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g, const param_type& __p) {
    return negative_binomial_distribution<result_type>(1, __p.p())(__g);
  }

  // property functions
  _LIBCPP_HIDE_FROM_ABI double p() const { return __p_.p(); }

  _LIBCPP_HIDE_FROM_ABI param_type param() const { return __p_; }
  _LIBCPP_HIDE_FROM_ABI void param(const param_type& __p) { __p_ = __p; }

  _LIBCPP_HIDE_FROM_ABI result_type min() const { return 0; }
  _LIBCPP_HIDE_FROM_ABI result_type max() const { return numeric_limits<result_type>::max(); }

  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const geometric_distribution& __x, const geometric_distribution& __y) {
    return __x.__p_ == __y.__p_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const geometric_distribution& __x, const geometric_distribution& __y) {
    return !(__x == __y);
  }
};

template <class _CharT, class _Traits, class _IntType>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const geometric_distribution<_IntType>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _OStream;
  __os.flags(_OStream::dec | _OStream::left | _OStream::fixed | _OStream::scientific);
  return __os << __x.p();
}

template <class _CharT, class _Traits, class _IntType>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, geometric_distribution<_IntType>& __x) {
  typedef geometric_distribution<_IntType> _Eng;
  typedef typename _Eng::param_type param_type;
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  double __p;
  __is >> __p;
  if (!__is.fail())
    __x.param(param_type(__p));
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_GEOMETRIC_DISTRIBUTION_H
