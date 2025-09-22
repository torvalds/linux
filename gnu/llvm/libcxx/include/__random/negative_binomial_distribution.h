//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_NEGATIVE_BINOMIAL_DISTRIBUTION_H
#define _LIBCPP___RANDOM_NEGATIVE_BINOMIAL_DISTRIBUTION_H

#include <__assert>
#include <__config>
#include <__random/bernoulli_distribution.h>
#include <__random/gamma_distribution.h>
#include <__random/is_valid.h>
#include <__random/poisson_distribution.h>
#include <iosfwd>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _IntType = int>
class _LIBCPP_TEMPLATE_VIS negative_binomial_distribution {
  static_assert(__libcpp_random_is_valid_inttype<_IntType>::value, "IntType must be a supported integer type");

public:
  // types
  typedef _IntType result_type;

  class _LIBCPP_TEMPLATE_VIS param_type {
    result_type __k_;
    double __p_;

  public:
    typedef negative_binomial_distribution distribution_type;

    _LIBCPP_HIDE_FROM_ABI explicit param_type(result_type __k = 1, double __p = 0.5) : __k_(__k), __p_(__p) {}

    _LIBCPP_HIDE_FROM_ABI result_type k() const { return __k_; }
    _LIBCPP_HIDE_FROM_ABI double p() const { return __p_; }

    friend _LIBCPP_HIDE_FROM_ABI bool operator==(const param_type& __x, const param_type& __y) {
      return __x.__k_ == __y.__k_ && __x.__p_ == __y.__p_;
    }
    friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const param_type& __x, const param_type& __y) { return !(__x == __y); }
  };

private:
  param_type __p_;

public:
  // constructor and reset functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI negative_binomial_distribution() : negative_binomial_distribution(1) {}
  _LIBCPP_HIDE_FROM_ABI explicit negative_binomial_distribution(result_type __k, double __p = 0.5) : __p_(__k, __p) {}
#else
  _LIBCPP_HIDE_FROM_ABI explicit negative_binomial_distribution(result_type __k = 1, double __p = 0.5)
      : __p_(__k, __p) {}
#endif
  _LIBCPP_HIDE_FROM_ABI explicit negative_binomial_distribution(const param_type& __p) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI void reset() {}

  // generating functions
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g) {
    return (*this)(__g, __p_);
  }
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g, const param_type& __p);

  // property functions
  _LIBCPP_HIDE_FROM_ABI result_type k() const { return __p_.k(); }
  _LIBCPP_HIDE_FROM_ABI double p() const { return __p_.p(); }

  _LIBCPP_HIDE_FROM_ABI param_type param() const { return __p_; }
  _LIBCPP_HIDE_FROM_ABI void param(const param_type& __p) { __p_ = __p; }

  _LIBCPP_HIDE_FROM_ABI result_type min() const { return 0; }
  _LIBCPP_HIDE_FROM_ABI result_type max() const { return numeric_limits<result_type>::max(); }

  friend _LIBCPP_HIDE_FROM_ABI bool
  operator==(const negative_binomial_distribution& __x, const negative_binomial_distribution& __y) {
    return __x.__p_ == __y.__p_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool
  operator!=(const negative_binomial_distribution& __x, const negative_binomial_distribution& __y) {
    return !(__x == __y);
  }
};

template <class _IntType>
template <class _URNG>
_IntType negative_binomial_distribution<_IntType>::operator()(_URNG& __urng, const param_type& __pr) {
  static_assert(__libcpp_random_is_valid_urng<_URNG>::value, "");
  result_type __k = __pr.k();
  double __p      = __pr.p();
  // When the number of bits in _IntType is small, we are too likely to
  // overflow __f below to use this technique.
  if (__k <= 21 * __p && sizeof(_IntType) > 1) {
    bernoulli_distribution __gen(__p);
    result_type __f = 0;
    result_type __s = 0;
    while (__s < __k) {
      if (__gen(__urng))
        ++__s;
      else
        ++__f;
    }
    _LIBCPP_ASSERT_INTERNAL(__f >= 0,
                            "std::negative_binomial_distribution should never produce negative values. "
                            "This is almost certainly a signed integer overflow issue on __f.");
    return __f;
  }
  return poisson_distribution<result_type>(gamma_distribution<double>(__k, (1 - __p) / __p)(__urng))(__urng);
}

template <class _CharT, class _Traits, class _IntType>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const negative_binomial_distribution<_IntType>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _OStream;
  __os.flags(_OStream::dec | _OStream::left | _OStream::fixed | _OStream::scientific);
  _CharT __sp = __os.widen(' ');
  __os.fill(__sp);
  return __os << __x.k() << __sp << __x.p();
}

template <class _CharT, class _Traits, class _IntType>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, negative_binomial_distribution<_IntType>& __x) {
  typedef negative_binomial_distribution<_IntType> _Eng;
  typedef typename _Eng::result_type result_type;
  typedef typename _Eng::param_type param_type;
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  result_type __k;
  double __p;
  __is >> __k >> __p;
  if (!__is.fail())
    __x.param(param_type(__k, __p));
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_NEGATIVE_BINOMIAL_DISTRIBUTION_H
