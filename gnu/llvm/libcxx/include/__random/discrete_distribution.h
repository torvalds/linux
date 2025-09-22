//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_DISCRETE_DISTRIBUTION_H
#define _LIBCPP___RANDOM_DISCRETE_DISTRIBUTION_H

#include <__algorithm/upper_bound.h>
#include <__config>
#include <__random/is_valid.h>
#include <__random/uniform_real_distribution.h>
#include <cstddef>
#include <iosfwd>
#include <numeric>
#include <vector>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _IntType = int>
class _LIBCPP_TEMPLATE_VIS discrete_distribution {
  static_assert(__libcpp_random_is_valid_inttype<_IntType>::value, "IntType must be a supported integer type");

public:
  // types
  typedef _IntType result_type;

  class _LIBCPP_TEMPLATE_VIS param_type {
    vector<double> __p_;

  public:
    typedef discrete_distribution distribution_type;

    _LIBCPP_HIDE_FROM_ABI param_type() {}
    template <class _InputIterator>
    _LIBCPP_HIDE_FROM_ABI param_type(_InputIterator __f, _InputIterator __l) : __p_(__f, __l) {
      __init();
    }
#ifndef _LIBCPP_CXX03_LANG
    _LIBCPP_HIDE_FROM_ABI param_type(initializer_list<double> __wl) : __p_(__wl.begin(), __wl.end()) { __init(); }
#endif // _LIBCPP_CXX03_LANG
    template <class _UnaryOperation>
    _LIBCPP_HIDE_FROM_ABI param_type(size_t __nw, double __xmin, double __xmax, _UnaryOperation __fw);

    _LIBCPP_HIDE_FROM_ABI vector<double> probabilities() const;

    friend _LIBCPP_HIDE_FROM_ABI bool operator==(const param_type& __x, const param_type& __y) {
      return __x.__p_ == __y.__p_;
    }
    friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const param_type& __x, const param_type& __y) { return !(__x == __y); }

  private:
    _LIBCPP_HIDE_FROM_ABI void __init();

    friend class discrete_distribution;

    template <class _CharT, class _Traits, class _IT>
    friend basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __os, const discrete_distribution<_IT>& __x);

    template <class _CharT, class _Traits, class _IT>
    friend basic_istream<_CharT, _Traits>&
    operator>>(basic_istream<_CharT, _Traits>& __is, discrete_distribution<_IT>& __x);
  };

private:
  param_type __p_;

public:
  // constructor and reset functions
  _LIBCPP_HIDE_FROM_ABI discrete_distribution() {}
  template <class _InputIterator>
  _LIBCPP_HIDE_FROM_ABI discrete_distribution(_InputIterator __f, _InputIterator __l) : __p_(__f, __l) {}
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI discrete_distribution(initializer_list<double> __wl) : __p_(__wl) {}
#endif // _LIBCPP_CXX03_LANG
  template <class _UnaryOperation>
  _LIBCPP_HIDE_FROM_ABI discrete_distribution(size_t __nw, double __xmin, double __xmax, _UnaryOperation __fw)
      : __p_(__nw, __xmin, __xmax, __fw) {}
  _LIBCPP_HIDE_FROM_ABI explicit discrete_distribution(const param_type& __p) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI void reset() {}

  // generating functions
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g) {
    return (*this)(__g, __p_);
  }
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g, const param_type& __p);

  // property functions
  _LIBCPP_HIDE_FROM_ABI vector<double> probabilities() const { return __p_.probabilities(); }

  _LIBCPP_HIDE_FROM_ABI param_type param() const { return __p_; }
  _LIBCPP_HIDE_FROM_ABI void param(const param_type& __p) { __p_ = __p; }

  _LIBCPP_HIDE_FROM_ABI result_type min() const { return 0; }
  _LIBCPP_HIDE_FROM_ABI result_type max() const { return __p_.__p_.size(); }

  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const discrete_distribution& __x, const discrete_distribution& __y) {
    return __x.__p_ == __y.__p_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const discrete_distribution& __x, const discrete_distribution& __y) {
    return !(__x == __y);
  }

  template <class _CharT, class _Traits, class _IT>
  friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const discrete_distribution<_IT>& __x);

  template <class _CharT, class _Traits, class _IT>
  friend basic_istream<_CharT, _Traits>&
  operator>>(basic_istream<_CharT, _Traits>& __is, discrete_distribution<_IT>& __x);
};

template <class _IntType>
template <class _UnaryOperation>
discrete_distribution<_IntType>::param_type::param_type(
    size_t __nw, double __xmin, double __xmax, _UnaryOperation __fw) {
  if (__nw > 1) {
    __p_.reserve(__nw - 1);
    double __d  = (__xmax - __xmin) / __nw;
    double __d2 = __d / 2;
    for (size_t __k = 0; __k < __nw; ++__k)
      __p_.push_back(__fw(__xmin + __k * __d + __d2));
    __init();
  }
}

template <class _IntType>
void discrete_distribution<_IntType>::param_type::__init() {
  if (!__p_.empty()) {
    if (__p_.size() > 1) {
      double __s = std::accumulate(__p_.begin(), __p_.end(), 0.0);
      for (vector<double>::iterator __i = __p_.begin(), __e = __p_.end(); __i < __e; ++__i)
        *__i /= __s;
      vector<double> __t(__p_.size() - 1);
      std::partial_sum(__p_.begin(), __p_.end() - 1, __t.begin());
      swap(__p_, __t);
    } else {
      __p_.clear();
      __p_.shrink_to_fit();
    }
  }
}

template <class _IntType>
vector<double> discrete_distribution<_IntType>::param_type::probabilities() const {
  size_t __n = __p_.size();
  vector<double> __p(__n + 1);
  std::adjacent_difference(__p_.begin(), __p_.end(), __p.begin());
  if (__n > 0)
    __p[__n] = 1 - __p_[__n - 1];
  else
    __p[0] = 1;
  return __p;
}

template <class _IntType>
template <class _URNG>
_IntType discrete_distribution<_IntType>::operator()(_URNG& __g, const param_type& __p) {
  static_assert(__libcpp_random_is_valid_urng<_URNG>::value, "");
  uniform_real_distribution<double> __gen;
  return static_cast<_IntType>(std::upper_bound(__p.__p_.begin(), __p.__p_.end(), __gen(__g)) - __p.__p_.begin());
}

template <class _CharT, class _Traits, class _IT>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const discrete_distribution<_IT>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _OStream;
  __os.flags(_OStream::dec | _OStream::left | _OStream::fixed | _OStream::scientific);
  _CharT __sp = __os.widen(' ');
  __os.fill(__sp);
  size_t __n = __x.__p_.__p_.size();
  __os << __n;
  for (size_t __i = 0; __i < __n; ++__i)
    __os << __sp << __x.__p_.__p_[__i];
  return __os;
}

template <class _CharT, class _Traits, class _IT>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, discrete_distribution<_IT>& __x) {
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  size_t __n;
  __is >> __n;
  vector<double> __p(__n);
  for (size_t __i = 0; __i < __n; ++__i)
    __is >> __p[__i];
  if (!__is.fail())
    swap(__x.__p_.__p_, __p);
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_DISCRETE_DISTRIBUTION_H
