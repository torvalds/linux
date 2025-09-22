//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_PIECEWISE_LINEAR_DISTRIBUTION_H
#define _LIBCPP___RANDOM_PIECEWISE_LINEAR_DISTRIBUTION_H

#include <__algorithm/upper_bound.h>
#include <__config>
#include <__random/is_valid.h>
#include <__random/uniform_real_distribution.h>
#include <cmath>
#include <iosfwd>
#include <vector>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _RealType = double>
class _LIBCPP_TEMPLATE_VIS piecewise_linear_distribution {
  static_assert(__libcpp_random_is_valid_realtype<_RealType>::value,
                "RealType must be a supported floating-point type");

public:
  // types
  typedef _RealType result_type;

  class _LIBCPP_TEMPLATE_VIS param_type {
    vector<result_type> __b_;
    vector<result_type> __densities_;
    vector<result_type> __areas_;

  public:
    typedef piecewise_linear_distribution distribution_type;

    _LIBCPP_HIDE_FROM_ABI param_type();
    template <class _InputIteratorB, class _InputIteratorW>
    _LIBCPP_HIDE_FROM_ABI param_type(_InputIteratorB __f_b, _InputIteratorB __l_b, _InputIteratorW __f_w);
#ifndef _LIBCPP_CXX03_LANG
    template <class _UnaryOperation>
    _LIBCPP_HIDE_FROM_ABI param_type(initializer_list<result_type> __bl, _UnaryOperation __fw);
#endif // _LIBCPP_CXX03_LANG
    template <class _UnaryOperation>
    _LIBCPP_HIDE_FROM_ABI param_type(size_t __nw, result_type __xmin, result_type __xmax, _UnaryOperation __fw);
    _LIBCPP_HIDE_FROM_ABI param_type(param_type const&) = default;
    _LIBCPP_HIDE_FROM_ABI param_type& operator=(const param_type& __rhs);

    _LIBCPP_HIDE_FROM_ABI vector<result_type> intervals() const { return __b_; }
    _LIBCPP_HIDE_FROM_ABI vector<result_type> densities() const { return __densities_; }

    friend _LIBCPP_HIDE_FROM_ABI bool operator==(const param_type& __x, const param_type& __y) {
      return __x.__densities_ == __y.__densities_ && __x.__b_ == __y.__b_;
    }
    friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const param_type& __x, const param_type& __y) { return !(__x == __y); }

  private:
    _LIBCPP_HIDE_FROM_ABI void __init();

    friend class piecewise_linear_distribution;

    template <class _CharT, class _Traits, class _RT>
    friend basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __os, const piecewise_linear_distribution<_RT>& __x);

    template <class _CharT, class _Traits, class _RT>
    friend basic_istream<_CharT, _Traits>&
    operator>>(basic_istream<_CharT, _Traits>& __is, piecewise_linear_distribution<_RT>& __x);
  };

private:
  param_type __p_;

public:
  // constructor and reset functions
  _LIBCPP_HIDE_FROM_ABI piecewise_linear_distribution() {}
  template <class _InputIteratorB, class _InputIteratorW>
  _LIBCPP_HIDE_FROM_ABI
  piecewise_linear_distribution(_InputIteratorB __f_b, _InputIteratorB __l_b, _InputIteratorW __f_w)
      : __p_(__f_b, __l_b, __f_w) {}

#ifndef _LIBCPP_CXX03_LANG
  template <class _UnaryOperation>
  _LIBCPP_HIDE_FROM_ABI piecewise_linear_distribution(initializer_list<result_type> __bl, _UnaryOperation __fw)
      : __p_(__bl, __fw) {}
#endif // _LIBCPP_CXX03_LANG

  template <class _UnaryOperation>
  _LIBCPP_HIDE_FROM_ABI
  piecewise_linear_distribution(size_t __nw, result_type __xmin, result_type __xmax, _UnaryOperation __fw)
      : __p_(__nw, __xmin, __xmax, __fw) {}

  _LIBCPP_HIDE_FROM_ABI explicit piecewise_linear_distribution(const param_type& __p) : __p_(__p) {}

  _LIBCPP_HIDE_FROM_ABI void reset() {}

  // generating functions
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g) {
    return (*this)(__g, __p_);
  }
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g, const param_type& __p);

  // property functions
  _LIBCPP_HIDE_FROM_ABI vector<result_type> intervals() const { return __p_.intervals(); }
  _LIBCPP_HIDE_FROM_ABI vector<result_type> densities() const { return __p_.densities(); }

  _LIBCPP_HIDE_FROM_ABI param_type param() const { return __p_; }
  _LIBCPP_HIDE_FROM_ABI void param(const param_type& __p) { __p_ = __p; }

  _LIBCPP_HIDE_FROM_ABI result_type min() const { return __p_.__b_.front(); }
  _LIBCPP_HIDE_FROM_ABI result_type max() const { return __p_.__b_.back(); }

  friend _LIBCPP_HIDE_FROM_ABI bool
  operator==(const piecewise_linear_distribution& __x, const piecewise_linear_distribution& __y) {
    return __x.__p_ == __y.__p_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool
  operator!=(const piecewise_linear_distribution& __x, const piecewise_linear_distribution& __y) {
    return !(__x == __y);
  }

  template <class _CharT, class _Traits, class _RT>
  friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const piecewise_linear_distribution<_RT>& __x);

  template <class _CharT, class _Traits, class _RT>
  friend basic_istream<_CharT, _Traits>&
  operator>>(basic_istream<_CharT, _Traits>& __is, piecewise_linear_distribution<_RT>& __x);
};

template <class _RealType>
typename piecewise_linear_distribution<_RealType>::param_type&
piecewise_linear_distribution<_RealType>::param_type::operator=(const param_type& __rhs) {
  //  These can throw
  __b_.reserve(__rhs.__b_.size());
  __densities_.reserve(__rhs.__densities_.size());
  __areas_.reserve(__rhs.__areas_.size());

  //  These can not throw
  __b_         = __rhs.__b_;
  __densities_ = __rhs.__densities_;
  __areas_     = __rhs.__areas_;
  return *this;
}

template <class _RealType>
void piecewise_linear_distribution<_RealType>::param_type::__init() {
  __areas_.assign(__densities_.size() - 1, result_type());
  result_type __sp = 0;
  for (size_t __i = 0; __i < __areas_.size(); ++__i) {
    __areas_[__i] = (__densities_[__i + 1] + __densities_[__i]) * (__b_[__i + 1] - __b_[__i]) * .5;
    __sp += __areas_[__i];
  }
  for (size_t __i = __areas_.size(); __i > 1;) {
    --__i;
    __areas_[__i] = __areas_[__i - 1] / __sp;
  }
  __areas_[0] = 0;
  for (size_t __i = 1; __i < __areas_.size(); ++__i)
    __areas_[__i] += __areas_[__i - 1];
  for (size_t __i = 0; __i < __densities_.size(); ++__i)
    __densities_[__i] /= __sp;
}

template <class _RealType>
piecewise_linear_distribution<_RealType>::param_type::param_type() : __b_(2), __densities_(2, 1.0), __areas_(1, 0.0) {
  __b_[1] = 1;
}

template <class _RealType>
template <class _InputIteratorB, class _InputIteratorW>
piecewise_linear_distribution<_RealType>::param_type::param_type(
    _InputIteratorB __f_b, _InputIteratorB __l_b, _InputIteratorW __f_w)
    : __b_(__f_b, __l_b) {
  if (__b_.size() < 2) {
    __b_.resize(2);
    __b_[0] = 0;
    __b_[1] = 1;
    __densities_.assign(2, 1.0);
    __areas_.assign(1, 0.0);
  } else {
    __densities_.reserve(__b_.size());
    for (size_t __i = 0; __i < __b_.size(); ++__i, ++__f_w)
      __densities_.push_back(*__f_w);
    __init();
  }
}

#ifndef _LIBCPP_CXX03_LANG

template <class _RealType>
template <class _UnaryOperation>
piecewise_linear_distribution<_RealType>::param_type::param_type(
    initializer_list<result_type> __bl, _UnaryOperation __fw)
    : __b_(__bl.begin(), __bl.end()) {
  if (__b_.size() < 2) {
    __b_.resize(2);
    __b_[0] = 0;
    __b_[1] = 1;
    __densities_.assign(2, 1.0);
    __areas_.assign(1, 0.0);
  } else {
    __densities_.reserve(__b_.size());
    for (size_t __i = 0; __i < __b_.size(); ++__i)
      __densities_.push_back(__fw(__b_[__i]));
    __init();
  }
}

#endif // _LIBCPP_CXX03_LANG

template <class _RealType>
template <class _UnaryOperation>
piecewise_linear_distribution<_RealType>::param_type::param_type(
    size_t __nw, result_type __xmin, result_type __xmax, _UnaryOperation __fw)
    : __b_(__nw == 0 ? 2 : __nw + 1) {
  size_t __n      = __b_.size() - 1;
  result_type __d = (__xmax - __xmin) / __n;
  __densities_.reserve(__b_.size());
  for (size_t __i = 0; __i < __n; ++__i) {
    __b_[__i] = __xmin + __i * __d;
    __densities_.push_back(__fw(__b_[__i]));
  }
  __b_[__n] = __xmax;
  __densities_.push_back(__fw(__b_[__n]));
  __init();
}

template <class _RealType>
template <class _URNG>
_RealType piecewise_linear_distribution<_RealType>::operator()(_URNG& __g, const param_type& __p) {
  static_assert(__libcpp_random_is_valid_urng<_URNG>::value, "");
  typedef uniform_real_distribution<result_type> _Gen;
  result_type __u = _Gen()(__g);
  ptrdiff_t __k   = std::upper_bound(__p.__areas_.begin(), __p.__areas_.end(), __u) - __p.__areas_.begin() - 1;
  __u -= __p.__areas_[__k];
  const result_type __dk     = __p.__densities_[__k];
  const result_type __dk1    = __p.__densities_[__k + 1];
  const result_type __deltad = __dk1 - __dk;
  const result_type __bk     = __p.__b_[__k];
  if (__deltad == 0)
    return __u / __dk + __bk;
  const result_type __bk1    = __p.__b_[__k + 1];
  const result_type __deltab = __bk1 - __bk;
  return (__bk * __dk1 - __bk1 * __dk + std::sqrt(__deltab * (__deltab * __dk * __dk + 2 * __deltad * __u))) / __deltad;
}

template <class _CharT, class _Traits, class _RT>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const piecewise_linear_distribution<_RT>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _OStream;
  __os.flags(_OStream::dec | _OStream::left | _OStream::fixed | _OStream::scientific);
  _CharT __sp = __os.widen(' ');
  __os.fill(__sp);
  size_t __n = __x.__p_.__b_.size();
  __os << __n;
  for (size_t __i = 0; __i < __n; ++__i)
    __os << __sp << __x.__p_.__b_[__i];
  __n = __x.__p_.__densities_.size();
  __os << __sp << __n;
  for (size_t __i = 0; __i < __n; ++__i)
    __os << __sp << __x.__p_.__densities_[__i];
  __n = __x.__p_.__areas_.size();
  __os << __sp << __n;
  for (size_t __i = 0; __i < __n; ++__i)
    __os << __sp << __x.__p_.__areas_[__i];
  return __os;
}

template <class _CharT, class _Traits, class _RT>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, piecewise_linear_distribution<_RT>& __x) {
  typedef piecewise_linear_distribution<_RT> _Eng;
  typedef typename _Eng::result_type result_type;
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  size_t __n;
  __is >> __n;
  vector<result_type> __b(__n);
  for (size_t __i = 0; __i < __n; ++__i)
    __is >> __b[__i];
  __is >> __n;
  vector<result_type> __densities(__n);
  for (size_t __i = 0; __i < __n; ++__i)
    __is >> __densities[__i];
  __is >> __n;
  vector<result_type> __areas(__n);
  for (size_t __i = 0; __i < __n; ++__i)
    __is >> __areas[__i];
  if (!__is.fail()) {
    swap(__x.__p_.__b_, __b);
    swap(__x.__p_.__densities_, __densities);
    swap(__x.__p_.__areas_, __areas);
  }
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_PIECEWISE_LINEAR_DISTRIBUTION_H
