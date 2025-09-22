//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_POISSON_DISTRIBUTION_H
#define _LIBCPP___RANDOM_POISSON_DISTRIBUTION_H

#include <__config>
#include <__random/clamp_to_integral.h>
#include <__random/exponential_distribution.h>
#include <__random/is_valid.h>
#include <__random/normal_distribution.h>
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

template <class _IntType = int>
class _LIBCPP_TEMPLATE_VIS poisson_distribution {
  static_assert(__libcpp_random_is_valid_inttype<_IntType>::value, "IntType must be a supported integer type");

public:
  // types
  typedef _IntType result_type;

  class _LIBCPP_TEMPLATE_VIS param_type {
    double __mean_;
    double __s_;
    double __d_;
    double __l_;
    double __omega_;
    double __c0_;
    double __c1_;
    double __c2_;
    double __c3_;
    double __c_;

  public:
    typedef poisson_distribution distribution_type;

    _LIBCPP_HIDE_FROM_ABI explicit param_type(double __mean = 1.0);

    _LIBCPP_HIDE_FROM_ABI double mean() const { return __mean_; }

    friend _LIBCPP_HIDE_FROM_ABI bool operator==(const param_type& __x, const param_type& __y) {
      return __x.__mean_ == __y.__mean_;
    }
    friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const param_type& __x, const param_type& __y) { return !(__x == __y); }

    friend class poisson_distribution;
  };

private:
  param_type __p_;

public:
  // constructors and reset functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI poisson_distribution() : poisson_distribution(1.0) {}
  _LIBCPP_HIDE_FROM_ABI explicit poisson_distribution(double __mean) : __p_(__mean) {}
#else
  _LIBCPP_HIDE_FROM_ABI explicit poisson_distribution(double __mean = 1.0) : __p_(__mean) {}
#endif
  _LIBCPP_HIDE_FROM_ABI explicit poisson_distribution(const param_type& __p) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI void reset() {}

  // generating functions
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g) {
    return (*this)(__g, __p_);
  }
  template <class _URNG>
  _LIBCPP_HIDE_FROM_ABI result_type operator()(_URNG& __g, const param_type& __p);

  // property functions
  _LIBCPP_HIDE_FROM_ABI double mean() const { return __p_.mean(); }

  _LIBCPP_HIDE_FROM_ABI param_type param() const { return __p_; }
  _LIBCPP_HIDE_FROM_ABI void param(const param_type& __p) { __p_ = __p; }

  _LIBCPP_HIDE_FROM_ABI result_type min() const { return 0; }
  _LIBCPP_HIDE_FROM_ABI result_type max() const { return numeric_limits<result_type>::max(); }

  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const poisson_distribution& __x, const poisson_distribution& __y) {
    return __x.__p_ == __y.__p_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const poisson_distribution& __x, const poisson_distribution& __y) {
    return !(__x == __y);
  }
};

template <class _IntType>
poisson_distribution<_IntType>::param_type::param_type(double __mean)
    // According to the standard `inf` is a valid input, but it causes the
    // distribution to hang, so we replace it with the maximum representable
    // mean.
    : __mean_(isinf(__mean) ? numeric_limits<double>::max() : __mean) {
  if (__mean_ < 10) {
    __s_     = 0;
    __d_     = 0;
    __l_     = std::exp(-__mean_);
    __omega_ = 0;
    __c3_    = 0;
    __c2_    = 0;
    __c1_    = 0;
    __c0_    = 0;
    __c_     = 0;
  } else {
    __s_        = std::sqrt(__mean_);
    __d_        = 6 * __mean_ * __mean_;
    __l_        = std::trunc(__mean_ - 1.1484);
    __omega_    = .3989423 / __s_;
    double __b1 = .4166667E-1 / __mean_;
    double __b2 = .3 * __b1 * __b1;
    __c3_       = .1428571 * __b1 * __b2;
    __c2_       = __b2 - 15. * __c3_;
    __c1_       = __b1 - 6. * __b2 + 45. * __c3_;
    __c0_       = 1. - __b1 + 3. * __b2 - 15. * __c3_;
    __c_        = .1069 / __mean_;
  }
}

template <class _IntType>
template <class _URNG>
_IntType poisson_distribution<_IntType>::operator()(_URNG& __urng, const param_type& __pr) {
  static_assert(__libcpp_random_is_valid_urng<_URNG>::value, "");
  double __tx;
  uniform_real_distribution<double> __urd;
  if (__pr.__mean_ < 10) {
    __tx = 0;
    for (double __p = __urd(__urng); __p > __pr.__l_; ++__tx)
      __p *= __urd(__urng);
  } else {
    double __difmuk;
    double __g = __pr.__mean_ + __pr.__s_ * normal_distribution<double>()(__urng);
    double __u;
    if (__g > 0) {
      __tx = std::trunc(__g);
      if (__tx >= __pr.__l_)
        return std::__clamp_to_integral<result_type>(__tx);
      __difmuk = __pr.__mean_ - __tx;
      __u      = __urd(__urng);
      if (__pr.__d_ * __u >= __difmuk * __difmuk * __difmuk)
        return std::__clamp_to_integral<result_type>(__tx);
    }
    exponential_distribution<double> __edist;
    for (bool __using_exp_dist = false; true; __using_exp_dist = true) {
      double __e;
      if (__using_exp_dist || __g <= 0) {
        double __t;
        do {
          __e = __edist(__urng);
          __u = __urd(__urng);
          __u += __u - 1;
          __t = 1.8 + (__u < 0 ? -__e : __e);
        } while (__t <= -.6744);
        __tx             = std::trunc(__pr.__mean_ + __pr.__s_ * __t);
        __difmuk         = __pr.__mean_ - __tx;
        __using_exp_dist = true;
      }
      double __px;
      double __py;
      if (__tx < 10 && __tx >= 0) {
        const double __fac[] = {1, 1, 2, 6, 24, 120, 720, 5040, 40320, 362880};
        __px                 = -__pr.__mean_;
        __py                 = std::pow(__pr.__mean_, (double)__tx) / __fac[static_cast<int>(__tx)];
      } else {
        double __del = .8333333E-1 / __tx;
        __del -= 4.8 * __del * __del * __del;
        double __v = __difmuk / __tx;
        if (std::abs(__v) > 0.25)
          __px = __tx * std::log(1 + __v) - __difmuk - __del;
        else
          __px = __tx * __v * __v *
                     (((((((.1250060 * __v + -.1384794) * __v + .1421878) * __v + -.1661269) * __v + .2000118) * __v +
                        -.2500068) *
                           __v +
                       .3333333) *
                          __v +
                      -.5) -
                 __del;
        __py = .3989423 / std::sqrt(__tx);
      }
      double __r  = (0.5 - __difmuk) / __pr.__s_;
      double __r2 = __r * __r;
      double __fx = -0.5 * __r2;
      double __fy = __pr.__omega_ * (((__pr.__c3_ * __r2 + __pr.__c2_) * __r2 + __pr.__c1_) * __r2 + __pr.__c0_);
      if (__using_exp_dist) {
        if (__pr.__c_ * std::abs(__u) <= __py * std::exp(__px + __e) - __fy * std::exp(__fx + __e))
          break;
      } else {
        if (__fy - __u * __fy <= __py * std::exp(__px - __fx))
          break;
      }
    }
  }
  return std::__clamp_to_integral<result_type>(__tx);
}

template <class _CharT, class _Traits, class _IntType>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const poisson_distribution<_IntType>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _OStream;
  __os.flags(_OStream::dec | _OStream::left | _OStream::fixed | _OStream::scientific);
  return __os << __x.mean();
}

template <class _CharT, class _Traits, class _IntType>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, poisson_distribution<_IntType>& __x) {
  typedef poisson_distribution<_IntType> _Eng;
  typedef typename _Eng::param_type param_type;
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  double __mean;
  __is >> __mean;
  if (!__is.fail())
    __x.param(param_type(__mean));
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_POISSON_DISTRIBUTION_H
