//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_SUBTRACT_WITH_CARRY_ENGINE_H
#define _LIBCPP___RANDOM_SUBTRACT_WITH_CARRY_ENGINE_H

#include <__algorithm/equal.h>
#include <__algorithm/min.h>
#include <__config>
#include <__random/is_seed_sequence.h>
#include <__random/linear_congruential_engine.h>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _UIntType, size_t __w, size_t __s, size_t __r>
class _LIBCPP_TEMPLATE_VIS subtract_with_carry_engine;

template <class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
_LIBCPP_HIDE_FROM_ABI bool operator==(const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x,
                                      const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __y);

template <class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
_LIBCPP_HIDE_FROM_ABI bool operator!=(const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x,
                                      const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __y);

template <class _CharT, class _Traits, class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x);

template <class _CharT, class _Traits, class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x);

template <class _UIntType, size_t __w, size_t __s, size_t __r>
class _LIBCPP_TEMPLATE_VIS subtract_with_carry_engine {
public:
  // types
  typedef _UIntType result_type;

private:
  result_type __x_[__r];
  result_type __c_;
  size_t __i_;

  static _LIBCPP_CONSTEXPR const result_type _Dt = numeric_limits<result_type>::digits;
  static_assert(0 < __w, "subtract_with_carry_engine invalid parameters");
  static_assert(__w <= _Dt, "subtract_with_carry_engine invalid parameters");
  static_assert(0 < __s, "subtract_with_carry_engine invalid parameters");
  static_assert(__s < __r, "subtract_with_carry_engine invalid parameters");

public:
  static _LIBCPP_CONSTEXPR const result_type _Min = 0;
  static _LIBCPP_CONSTEXPR const result_type _Max =
      __w == _Dt ? result_type(~0) : (result_type(1) << __w) - result_type(1);
  static_assert(_Min < _Max, "subtract_with_carry_engine invalid parameters");

  // engine characteristics
  static _LIBCPP_CONSTEXPR const size_t word_size = __w;
  static _LIBCPP_CONSTEXPR const size_t short_lag = __s;
  static _LIBCPP_CONSTEXPR const size_t long_lag  = __r;
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type min() { return _Min; }
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type max() { return _Max; }
  static _LIBCPP_CONSTEXPR const result_type default_seed = 19780503u;

  // constructors and seeding functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI subtract_with_carry_engine() : subtract_with_carry_engine(default_seed) {}
  _LIBCPP_HIDE_FROM_ABI explicit subtract_with_carry_engine(result_type __sd) { seed(__sd); }
#else
  _LIBCPP_HIDE_FROM_ABI explicit subtract_with_carry_engine(result_type __sd = default_seed) { seed(__sd); }
#endif
  template <class _Sseq, __enable_if_t<__is_seed_sequence<_Sseq, subtract_with_carry_engine>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI explicit subtract_with_carry_engine(_Sseq& __q) {
    seed(__q);
  }
  _LIBCPP_HIDE_FROM_ABI void seed(result_type __sd = default_seed) {
    seed(__sd, integral_constant<unsigned, 1 + (__w - 1) / 32>());
  }
  template <class _Sseq, __enable_if_t<__is_seed_sequence<_Sseq, subtract_with_carry_engine>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI void seed(_Sseq& __q) {
    __seed(__q, integral_constant<unsigned, 1 + (__w - 1) / 32>());
  }

  // generating functions
  _LIBCPP_HIDE_FROM_ABI result_type operator()();
  _LIBCPP_HIDE_FROM_ABI void discard(unsigned long long __z) {
    for (; __z; --__z)
      operator()();
  }

  template <class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
  friend bool operator==(const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x,
                         const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __y);

  template <class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
  friend bool operator!=(const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x,
                         const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __y);

  template <class _CharT, class _Traits, class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
  friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x);

  template <class _CharT, class _Traits, class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
  friend basic_istream<_CharT, _Traits>&
  operator>>(basic_istream<_CharT, _Traits>& __is, subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x);

private:
  _LIBCPP_HIDE_FROM_ABI void seed(result_type __sd, integral_constant<unsigned, 1>);
  _LIBCPP_HIDE_FROM_ABI void seed(result_type __sd, integral_constant<unsigned, 2>);
  template <class _Sseq>
  _LIBCPP_HIDE_FROM_ABI void __seed(_Sseq& __q, integral_constant<unsigned, 1>);
  template <class _Sseq>
  _LIBCPP_HIDE_FROM_ABI void __seed(_Sseq& __q, integral_constant<unsigned, 2>);
};

template <class _UIntType, size_t __w, size_t __s, size_t __r>
_LIBCPP_CONSTEXPR const size_t subtract_with_carry_engine<_UIntType, __w, __s, __r>::word_size;

template <class _UIntType, size_t __w, size_t __s, size_t __r>
_LIBCPP_CONSTEXPR const size_t subtract_with_carry_engine<_UIntType, __w, __s, __r>::short_lag;

template <class _UIntType, size_t __w, size_t __s, size_t __r>
_LIBCPP_CONSTEXPR const size_t subtract_with_carry_engine<_UIntType, __w, __s, __r>::long_lag;

template <class _UIntType, size_t __w, size_t __s, size_t __r>
_LIBCPP_CONSTEXPR const typename subtract_with_carry_engine<_UIntType, __w, __s, __r>::result_type
    subtract_with_carry_engine<_UIntType, __w, __s, __r>::default_seed;

template <class _UIntType, size_t __w, size_t __s, size_t __r>
void subtract_with_carry_engine<_UIntType, __w, __s, __r>::seed(result_type __sd, integral_constant<unsigned, 1>) {
  linear_congruential_engine<result_type, 40014u, 0u, 2147483563u> __e(__sd == 0u ? default_seed : __sd);
  for (size_t __i = 0; __i < __r; ++__i)
    __x_[__i] = static_cast<result_type>(__e() & _Max);
  __c_ = __x_[__r - 1] == 0;
  __i_ = 0;
}

template <class _UIntType, size_t __w, size_t __s, size_t __r>
void subtract_with_carry_engine<_UIntType, __w, __s, __r>::seed(result_type __sd, integral_constant<unsigned, 2>) {
  linear_congruential_engine<result_type, 40014u, 0u, 2147483563u> __e(__sd == 0u ? default_seed : __sd);
  for (size_t __i = 0; __i < __r; ++__i) {
    result_type __e0 = __e();
    __x_[__i]        = static_cast<result_type>((__e0 + ((uint64_t)__e() << 32)) & _Max);
  }
  __c_ = __x_[__r - 1] == 0;
  __i_ = 0;
}

template <class _UIntType, size_t __w, size_t __s, size_t __r>
template <class _Sseq>
void subtract_with_carry_engine<_UIntType, __w, __s, __r>::__seed(_Sseq& __q, integral_constant<unsigned, 1>) {
  const unsigned __k = 1;
  uint32_t __ar[__r * __k];
  __q.generate(__ar, __ar + __r * __k);
  for (size_t __i = 0; __i < __r; ++__i)
    __x_[__i] = static_cast<result_type>(__ar[__i] & _Max);
  __c_ = __x_[__r - 1] == 0;
  __i_ = 0;
}

template <class _UIntType, size_t __w, size_t __s, size_t __r>
template <class _Sseq>
void subtract_with_carry_engine<_UIntType, __w, __s, __r>::__seed(_Sseq& __q, integral_constant<unsigned, 2>) {
  const unsigned __k = 2;
  uint32_t __ar[__r * __k];
  __q.generate(__ar, __ar + __r * __k);
  for (size_t __i = 0; __i < __r; ++__i)
    __x_[__i] = static_cast<result_type>((__ar[2 * __i] + ((uint64_t)__ar[2 * __i + 1] << 32)) & _Max);
  __c_ = __x_[__r - 1] == 0;
  __i_ = 0;
}

template <class _UIntType, size_t __w, size_t __s, size_t __r>
_UIntType subtract_with_carry_engine<_UIntType, __w, __s, __r>::operator()() {
  const result_type& __xs = __x_[(__i_ + (__r - __s)) % __r];
  result_type& __xr       = __x_[__i_];
  result_type __new_c     = __c_ == 0 ? __xs < __xr : __xs != 0 ? __xs <= __xr : 1;
  __xr                    = (__xs - __xr - __c_) & _Max;
  __c_                    = __new_c;
  __i_                    = (__i_ + 1) % __r;
  return __xr;
}

template <class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
_LIBCPP_HIDE_FROM_ABI bool operator==(const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x,
                                      const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __y) {
  if (__x.__c_ != __y.__c_)
    return false;
  if (__x.__i_ == __y.__i_)
    return std::equal(__x.__x_, __x.__x_ + _Rp, __y.__x_);
  if (__x.__i_ == 0 || __y.__i_ == 0) {
    size_t __j = std::min(_Rp - __x.__i_, _Rp - __y.__i_);
    if (!std::equal(__x.__x_ + __x.__i_, __x.__x_ + __x.__i_ + __j, __y.__x_ + __y.__i_))
      return false;
    if (__x.__i_ == 0)
      return std::equal(__x.__x_ + __j, __x.__x_ + _Rp, __y.__x_);
    return std::equal(__x.__x_, __x.__x_ + (_Rp - __j), __y.__x_ + __j);
  }
  if (__x.__i_ < __y.__i_) {
    size_t __j = _Rp - __y.__i_;
    if (!std::equal(__x.__x_ + __x.__i_, __x.__x_ + (__x.__i_ + __j), __y.__x_ + __y.__i_))
      return false;
    if (!std::equal(__x.__x_ + (__x.__i_ + __j), __x.__x_ + _Rp, __y.__x_))
      return false;
    return std::equal(__x.__x_, __x.__x_ + __x.__i_, __y.__x_ + (_Rp - (__x.__i_ + __j)));
  }
  size_t __j = _Rp - __x.__i_;
  if (!std::equal(__y.__x_ + __y.__i_, __y.__x_ + (__y.__i_ + __j), __x.__x_ + __x.__i_))
    return false;
  if (!std::equal(__y.__x_ + (__y.__i_ + __j), __y.__x_ + _Rp, __x.__x_))
    return false;
  return std::equal(__y.__x_, __y.__x_ + __y.__i_, __x.__x_ + (_Rp - (__y.__i_ + __j)));
}

template <class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x,
                                             const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __y) {
  return !(__x == __y);
}

template <class _CharT, class _Traits, class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _Ostream;
  __os.flags(_Ostream::dec | _Ostream::left);
  _CharT __sp = __os.widen(' ');
  __os.fill(__sp);
  __os << __x.__x_[__x.__i_];
  for (size_t __j = __x.__i_ + 1; __j < _Rp; ++__j)
    __os << __sp << __x.__x_[__j];
  for (size_t __j = 0; __j < __x.__i_; ++__j)
    __os << __sp << __x.__x_[__j];
  __os << __sp << __x.__c_;
  return __os;
}

template <class _CharT, class _Traits, class _UInt, size_t _Wp, size_t _Sp, size_t _Rp>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, subtract_with_carry_engine<_UInt, _Wp, _Sp, _Rp>& __x) {
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  _UInt __t[_Rp + 1];
  for (size_t __i = 0; __i < _Rp + 1; ++__i)
    __is >> __t[__i];
  if (!__is.fail()) {
    for (size_t __i = 0; __i < _Rp; ++__i)
      __x.__x_[__i] = __t[__i];
    __x.__c_ = __t[_Rp];
    __x.__i_ = 0;
  }
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_SUBTRACT_WITH_CARRY_ENGINE_H
