// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_ISTREAM_ITERATOR_H
#define _LIBCPP___ITERATOR_ISTREAM_ITERATOR_H

#include <__config>
#include <__fwd/istream.h>
#include <__fwd/string.h>
#include <__iterator/default_sentinel.h>
#include <__iterator/iterator.h>
#include <__iterator/iterator_traits.h>
#include <__memory/addressof.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
template <class _Tp, class _CharT = char, class _Traits = char_traits<_CharT>, class _Distance = ptrdiff_t>
class _LIBCPP_TEMPLATE_VIS istream_iterator
#if _LIBCPP_STD_VER <= 14 || !defined(_LIBCPP_ABI_NO_ITERATOR_BASES)
    : public iterator<input_iterator_tag, _Tp, _Distance, const _Tp*, const _Tp&>
#endif
{
  _LIBCPP_SUPPRESS_DEPRECATED_POP

public:
  typedef input_iterator_tag iterator_category;
  typedef _Tp value_type;
  typedef _Distance difference_type;
  typedef const _Tp* pointer;
  typedef const _Tp& reference;
  typedef _CharT char_type;
  typedef _Traits traits_type;
  typedef basic_istream<_CharT, _Traits> istream_type;

private:
  istream_type* __in_stream_;
  _Tp __value_;

public:
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR istream_iterator() : __in_stream_(nullptr), __value_() {}
#if _LIBCPP_STD_VER >= 20
  _LIBCPP_HIDE_FROM_ABI constexpr istream_iterator(default_sentinel_t) : istream_iterator() {}
#endif // _LIBCPP_STD_VER >= 20
  _LIBCPP_HIDE_FROM_ABI istream_iterator(istream_type& __s) : __in_stream_(std::addressof(__s)) {
    if (!(*__in_stream_ >> __value_))
      __in_stream_ = nullptr;
  }

  _LIBCPP_HIDE_FROM_ABI const _Tp& operator*() const { return __value_; }
  _LIBCPP_HIDE_FROM_ABI const _Tp* operator->() const { return std::addressof((operator*())); }
  _LIBCPP_HIDE_FROM_ABI istream_iterator& operator++() {
    if (!(*__in_stream_ >> __value_))
      __in_stream_ = nullptr;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI istream_iterator operator++(int) {
    istream_iterator __t(*this);
    ++(*this);
    return __t;
  }

  template <class _Up, class _CharU, class _TraitsU, class _DistanceU>
  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const istream_iterator<_Up, _CharU, _TraitsU, _DistanceU>& __x,
                                               const istream_iterator<_Up, _CharU, _TraitsU, _DistanceU>& __y);

#if _LIBCPP_STD_VER >= 20
  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const istream_iterator& __i, default_sentinel_t) {
    return __i.__in_stream_ == nullptr;
  }
#endif // _LIBCPP_STD_VER >= 20
};

template <class _Tp, class _CharT, class _Traits, class _Distance>
inline _LIBCPP_HIDE_FROM_ABI bool operator==(const istream_iterator<_Tp, _CharT, _Traits, _Distance>& __x,
                                             const istream_iterator<_Tp, _CharT, _Traits, _Distance>& __y) {
  return __x.__in_stream_ == __y.__in_stream_;
}

#if _LIBCPP_STD_VER <= 17
template <class _Tp, class _CharT, class _Traits, class _Distance>
inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const istream_iterator<_Tp, _CharT, _Traits, _Distance>& __x,
                                             const istream_iterator<_Tp, _CharT, _Traits, _Distance>& __y) {
  return !(__x == __y);
}
#endif // _LIBCPP_STD_VER <= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_ISTREAM_ITERATOR_H
