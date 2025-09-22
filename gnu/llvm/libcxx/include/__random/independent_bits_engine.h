//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_INDEPENDENT_BITS_ENGINE_H
#define _LIBCPP___RANDOM_INDEPENDENT_BITS_ENGINE_H

#include <__config>
#include <__fwd/istream.h>
#include <__fwd/ostream.h>
#include <__random/is_seed_sequence.h>
#include <__random/log2.h>
#include <__type_traits/conditional.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_convertible.h>
#include <__utility/move.h>
#include <cstddef>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Engine, size_t __w, class _UIntType>
class _LIBCPP_TEMPLATE_VIS independent_bits_engine {
  template <class _UInt, _UInt _R0, size_t _Wp, size_t _Mp>
  class __get_n {
    static _LIBCPP_CONSTEXPR const size_t _Dt = numeric_limits<_UInt>::digits;
    static _LIBCPP_CONSTEXPR const size_t _Np = _Wp / _Mp + (_Wp % _Mp != 0);
    static _LIBCPP_CONSTEXPR const size_t _W0 = _Wp / _Np;
    static _LIBCPP_CONSTEXPR const _UInt _Y0  = _W0 >= _Dt ? 0 : (_R0 >> _W0) << _W0;

  public:
    static _LIBCPP_CONSTEXPR const size_t value = _R0 - _Y0 > _Y0 / _Np ? _Np + 1 : _Np;
  };

public:
  // types
  typedef _UIntType result_type;

private:
  _Engine __e_;

  static _LIBCPP_CONSTEXPR const result_type _Dt = numeric_limits<result_type>::digits;
  static_assert(0 < __w, "independent_bits_engine invalid parameters");
  static_assert(__w <= _Dt, "independent_bits_engine invalid parameters");

  typedef typename _Engine::result_type _Engine_result_type;
  typedef __conditional_t<sizeof(_Engine_result_type) <= sizeof(result_type), result_type, _Engine_result_type>
      _Working_result_type;
#ifdef _LIBCPP_CXX03_LANG
  static const _Working_result_type _Rp = _Engine::_Max - _Engine::_Min + _Working_result_type(1);
#else
  static _LIBCPP_CONSTEXPR const _Working_result_type _Rp = _Engine::max() - _Engine::min() + _Working_result_type(1);
#endif
  static _LIBCPP_CONSTEXPR const size_t __m                = __log2<_Working_result_type, _Rp>::value;
  static _LIBCPP_CONSTEXPR const size_t __n                = __get_n<_Working_result_type, _Rp, __w, __m>::value;
  static _LIBCPP_CONSTEXPR const size_t __w0               = __w / __n;
  static _LIBCPP_CONSTEXPR const size_t __n0               = __n - __w % __n;
  static _LIBCPP_CONSTEXPR const size_t _WDt               = numeric_limits<_Working_result_type>::digits;
  static _LIBCPP_CONSTEXPR const size_t _EDt               = numeric_limits<_Engine_result_type>::digits;
  static _LIBCPP_CONSTEXPR const _Working_result_type __y0 = __w0 >= _WDt ? 0 : (_Rp >> __w0) << __w0;
  static _LIBCPP_CONSTEXPR const _Working_result_type __y1 = __w0 >= _WDt - 1 ? 0 : (_Rp >> (__w0 + 1)) << (__w0 + 1);
  static _LIBCPP_CONSTEXPR const
      _Engine_result_type __mask0 = __w0 > 0 ? _Engine_result_type(~0) >> (_EDt - __w0) : _Engine_result_type(0);
  static _LIBCPP_CONSTEXPR const _Engine_result_type __mask1 =
      __w0 < _EDt - 1 ? _Engine_result_type(~0) >> (_EDt - (__w0 + 1)) : _Engine_result_type(~0);

public:
  static _LIBCPP_CONSTEXPR const result_type _Min = 0;
  static _LIBCPP_CONSTEXPR const result_type _Max =
      __w == _Dt ? result_type(~0) : (result_type(1) << __w) - result_type(1);
  static_assert(_Min < _Max, "independent_bits_engine invalid parameters");

  // engine characteristics
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type min() { return _Min; }
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type max() { return _Max; }

  // constructors and seeding functions
  _LIBCPP_HIDE_FROM_ABI independent_bits_engine() {}
  _LIBCPP_HIDE_FROM_ABI explicit independent_bits_engine(const _Engine& __e) : __e_(__e) {}
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI explicit independent_bits_engine(_Engine&& __e) : __e_(std::move(__e)) {}
#endif // _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI explicit independent_bits_engine(result_type __sd) : __e_(__sd) {}
  template <
      class _Sseq,
      __enable_if_t<__is_seed_sequence<_Sseq, independent_bits_engine>::value && !is_convertible<_Sseq, _Engine>::value,
                    int> = 0>
  _LIBCPP_HIDE_FROM_ABI explicit independent_bits_engine(_Sseq& __q) : __e_(__q) {}
  _LIBCPP_HIDE_FROM_ABI void seed() { __e_.seed(); }
  _LIBCPP_HIDE_FROM_ABI void seed(result_type __sd) { __e_.seed(__sd); }
  template <class _Sseq, __enable_if_t<__is_seed_sequence<_Sseq, independent_bits_engine>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI void seed(_Sseq& __q) {
    __e_.seed(__q);
  }

  // generating functions
  _LIBCPP_HIDE_FROM_ABI result_type operator()() { return __eval(integral_constant<bool, _Rp != 0>()); }
  _LIBCPP_HIDE_FROM_ABI void discard(unsigned long long __z) {
    for (; __z; --__z)
      operator()();
  }

  // property functions
  _LIBCPP_HIDE_FROM_ABI const _Engine& base() const _NOEXCEPT { return __e_; }

  template <class _Eng, size_t _Wp, class _UInt>
  friend bool operator==(const independent_bits_engine<_Eng, _Wp, _UInt>& __x,
                         const independent_bits_engine<_Eng, _Wp, _UInt>& __y);

  template <class _Eng, size_t _Wp, class _UInt>
  friend bool operator!=(const independent_bits_engine<_Eng, _Wp, _UInt>& __x,
                         const independent_bits_engine<_Eng, _Wp, _UInt>& __y);

  template <class _CharT, class _Traits, class _Eng, size_t _Wp, class _UInt>
  friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const independent_bits_engine<_Eng, _Wp, _UInt>& __x);

  template <class _CharT, class _Traits, class _Eng, size_t _Wp, class _UInt>
  friend basic_istream<_CharT, _Traits>&
  operator>>(basic_istream<_CharT, _Traits>& __is, independent_bits_engine<_Eng, _Wp, _UInt>& __x);

private:
  _LIBCPP_HIDE_FROM_ABI result_type __eval(false_type);
  _LIBCPP_HIDE_FROM_ABI result_type __eval(true_type);

  template <size_t __count,
            __enable_if_t<__count< _Dt, int> = 0> _LIBCPP_HIDE_FROM_ABI static result_type __lshift(result_type __x) {
    return __x << __count;
  }

  template <size_t __count, __enable_if_t<(__count >= _Dt), int> = 0>
  _LIBCPP_HIDE_FROM_ABI static result_type __lshift(result_type) {
    return result_type(0);
  }
};

template <class _Engine, size_t __w, class _UIntType>
inline _UIntType independent_bits_engine<_Engine, __w, _UIntType>::__eval(false_type) {
  return static_cast<result_type>(__e_() & __mask0);
}

template <class _Engine, size_t __w, class _UIntType>
_UIntType independent_bits_engine<_Engine, __w, _UIntType>::__eval(true_type) {
  result_type __sp = 0;
  for (size_t __k = 0; __k < __n0; ++__k) {
    _Engine_result_type __u;
    do {
      __u = __e_() - _Engine::min();
    } while (__u >= __y0);
    __sp = static_cast<result_type>(__lshift<__w0>(__sp) + (__u & __mask0));
  }
  for (size_t __k = __n0; __k < __n; ++__k) {
    _Engine_result_type __u;
    do {
      __u = __e_() - _Engine::min();
    } while (__u >= __y1);
    __sp = static_cast<result_type>(__lshift<__w0 + 1>(__sp) + (__u & __mask1));
  }
  return __sp;
}

template <class _Eng, size_t _Wp, class _UInt>
inline _LIBCPP_HIDE_FROM_ABI bool
operator==(const independent_bits_engine<_Eng, _Wp, _UInt>& __x, const independent_bits_engine<_Eng, _Wp, _UInt>& __y) {
  return __x.base() == __y.base();
}

template <class _Eng, size_t _Wp, class _UInt>
inline _LIBCPP_HIDE_FROM_ABI bool
operator!=(const independent_bits_engine<_Eng, _Wp, _UInt>& __x, const independent_bits_engine<_Eng, _Wp, _UInt>& __y) {
  return !(__x == __y);
}

template <class _CharT, class _Traits, class _Eng, size_t _Wp, class _UInt>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const independent_bits_engine<_Eng, _Wp, _UInt>& __x) {
  return __os << __x.base();
}

template <class _CharT, class _Traits, class _Eng, size_t _Wp, class _UInt>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, independent_bits_engine<_Eng, _Wp, _UInt>& __x) {
  _Eng __e;
  __is >> __e;
  if (!__is.fail())
    __x.__e_ = __e;
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_INDEPENDENT_BITS_ENGINE_H
