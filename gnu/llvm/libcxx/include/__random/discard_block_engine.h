//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_DISCARD_BLOCK_ENGINE_H
#define _LIBCPP___RANDOM_DISCARD_BLOCK_ENGINE_H

#include <__config>
#include <__random/is_seed_sequence.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_convertible.h>
#include <__utility/move.h>
#include <cstddef>
#include <iosfwd>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Engine, size_t __p, size_t __r>
class _LIBCPP_TEMPLATE_VIS discard_block_engine {
  _Engine __e_;
  int __n_;

  static_assert(0 < __r, "discard_block_engine invalid parameters");
  static_assert(__r <= __p, "discard_block_engine invalid parameters");
#ifndef _LIBCPP_CXX03_LANG // numeric_limits::max() is not constexpr in C++03
  static_assert(__r <= numeric_limits<int>::max(), "discard_block_engine invalid parameters");
#endif

public:
  // types
  typedef typename _Engine::result_type result_type;

  // engine characteristics
  static _LIBCPP_CONSTEXPR const size_t block_size = __p;
  static _LIBCPP_CONSTEXPR const size_t used_block = __r;

#ifdef _LIBCPP_CXX03_LANG
  static const result_type _Min = _Engine::_Min;
  static const result_type _Max = _Engine::_Max;
#else
  static constexpr result_type _Min = _Engine::min();
  static constexpr result_type _Max = _Engine::max();
#endif

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type min() { return _Engine::min(); }
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type max() { return _Engine::max(); }

  // constructors and seeding functions
  _LIBCPP_HIDE_FROM_ABI discard_block_engine() : __n_(0) {}
  _LIBCPP_HIDE_FROM_ABI explicit discard_block_engine(const _Engine& __e) : __e_(__e), __n_(0) {}
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI explicit discard_block_engine(_Engine&& __e) : __e_(std::move(__e)), __n_(0) {}
#endif // _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI explicit discard_block_engine(result_type __sd) : __e_(__sd), __n_(0) {}
  template <
      class _Sseq,
      __enable_if_t<__is_seed_sequence<_Sseq, discard_block_engine>::value && !is_convertible<_Sseq, _Engine>::value,
                    int> = 0>
  _LIBCPP_HIDE_FROM_ABI explicit discard_block_engine(_Sseq& __q) : __e_(__q), __n_(0) {}
  _LIBCPP_HIDE_FROM_ABI void seed() {
    __e_.seed();
    __n_ = 0;
  }
  _LIBCPP_HIDE_FROM_ABI void seed(result_type __sd) {
    __e_.seed(__sd);
    __n_ = 0;
  }
  template <class _Sseq, __enable_if_t<__is_seed_sequence<_Sseq, discard_block_engine>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI void seed(_Sseq& __q) {
    __e_.seed(__q);
    __n_ = 0;
  }

  // generating functions
  _LIBCPP_HIDE_FROM_ABI result_type operator()();
  _LIBCPP_HIDE_FROM_ABI void discard(unsigned long long __z) {
    for (; __z; --__z)
      operator()();
  }

  // property functions
  _LIBCPP_HIDE_FROM_ABI const _Engine& base() const _NOEXCEPT { return __e_; }

  template <class _Eng, size_t _Pp, size_t _Rp>
  friend bool
  operator==(const discard_block_engine<_Eng, _Pp, _Rp>& __x, const discard_block_engine<_Eng, _Pp, _Rp>& __y);

  template <class _Eng, size_t _Pp, size_t _Rp>
  friend bool
  operator!=(const discard_block_engine<_Eng, _Pp, _Rp>& __x, const discard_block_engine<_Eng, _Pp, _Rp>& __y);

  template <class _CharT, class _Traits, class _Eng, size_t _Pp, size_t _Rp>
  friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const discard_block_engine<_Eng, _Pp, _Rp>& __x);

  template <class _CharT, class _Traits, class _Eng, size_t _Pp, size_t _Rp>
  friend basic_istream<_CharT, _Traits>&
  operator>>(basic_istream<_CharT, _Traits>& __is, discard_block_engine<_Eng, _Pp, _Rp>& __x);
};

template <class _Engine, size_t __p, size_t __r>
_LIBCPP_CONSTEXPR const size_t discard_block_engine<_Engine, __p, __r>::block_size;

template <class _Engine, size_t __p, size_t __r>
_LIBCPP_CONSTEXPR const size_t discard_block_engine<_Engine, __p, __r>::used_block;

template <class _Engine, size_t __p, size_t __r>
typename discard_block_engine<_Engine, __p, __r>::result_type discard_block_engine<_Engine, __p, __r>::operator()() {
  if (__n_ >= static_cast<int>(__r)) {
    __e_.discard(__p - __r);
    __n_ = 0;
  }
  ++__n_;
  return __e_();
}

template <class _Eng, size_t _Pp, size_t _Rp>
inline _LIBCPP_HIDE_FROM_ABI bool
operator==(const discard_block_engine<_Eng, _Pp, _Rp>& __x, const discard_block_engine<_Eng, _Pp, _Rp>& __y) {
  return __x.__n_ == __y.__n_ && __x.__e_ == __y.__e_;
}

template <class _Eng, size_t _Pp, size_t _Rp>
inline _LIBCPP_HIDE_FROM_ABI bool
operator!=(const discard_block_engine<_Eng, _Pp, _Rp>& __x, const discard_block_engine<_Eng, _Pp, _Rp>& __y) {
  return !(__x == __y);
}

template <class _CharT, class _Traits, class _Eng, size_t _Pp, size_t _Rp>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const discard_block_engine<_Eng, _Pp, _Rp>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _Ostream;
  __os.flags(_Ostream::dec | _Ostream::left);
  _CharT __sp = __os.widen(' ');
  __os.fill(__sp);
  return __os << __x.__e_ << __sp << __x.__n_;
}

template <class _CharT, class _Traits, class _Eng, size_t _Pp, size_t _Rp>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, discard_block_engine<_Eng, _Pp, _Rp>& __x) {
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  _Eng __e;
  int __n;
  __is >> __e >> __n;
  if (!__is.fail()) {
    __x.__e_ = __e;
    __x.__n_ = __n;
  }
  return __is;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_DISCARD_BLOCK_ENGINE_H
