// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H
#define _LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H

#include <__config>
#include <__fwd/istream.h>
#include <__fwd/streambuf.h>
#include <__iterator/default_sentinel.h>
#include <__iterator/iterator.h>
#include <__iterator/iterator_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
template <class _CharT, class _Traits>
class _LIBCPP_TEMPLATE_VIS istreambuf_iterator
#if _LIBCPP_STD_VER <= 14 || !defined(_LIBCPP_ABI_NO_ITERATOR_BASES)
    : public iterator<input_iterator_tag, _CharT, typename _Traits::off_type, _CharT*, _CharT>
#endif
{
  _LIBCPP_SUPPRESS_DEPRECATED_POP

public:
  typedef input_iterator_tag iterator_category;
  typedef _CharT value_type;
  typedef typename _Traits::off_type difference_type;
  typedef _CharT* pointer;
  typedef _CharT reference;
  typedef _CharT char_type;
  typedef _Traits traits_type;
  typedef typename _Traits::int_type int_type;
  typedef basic_streambuf<_CharT, _Traits> streambuf_type;
  typedef basic_istream<_CharT, _Traits> istream_type;

private:
  mutable streambuf_type* __sbuf_;

  class __proxy {
    char_type __keep_;
    streambuf_type* __sbuf_;
    _LIBCPP_HIDE_FROM_ABI explicit __proxy(char_type __c, streambuf_type* __s) : __keep_(__c), __sbuf_(__s) {}
    friend class istreambuf_iterator;

  public:
    _LIBCPP_HIDE_FROM_ABI char_type operator*() const { return __keep_; }
  };

  _LIBCPP_HIDE_FROM_ABI bool __test_for_eof() const {
    if (__sbuf_ && traits_type::eq_int_type(__sbuf_->sgetc(), traits_type::eof()))
      __sbuf_ = nullptr;
    return __sbuf_ == nullptr;
  }

public:
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR istreambuf_iterator() _NOEXCEPT : __sbuf_(nullptr) {}
#if _LIBCPP_STD_VER >= 20
  _LIBCPP_HIDE_FROM_ABI constexpr istreambuf_iterator(default_sentinel_t) noexcept : istreambuf_iterator() {}
#endif // _LIBCPP_STD_VER >= 20
  _LIBCPP_HIDE_FROM_ABI istreambuf_iterator(istream_type& __s) _NOEXCEPT : __sbuf_(__s.rdbuf()) {}
  _LIBCPP_HIDE_FROM_ABI istreambuf_iterator(streambuf_type* __s) _NOEXCEPT : __sbuf_(__s) {}
  _LIBCPP_HIDE_FROM_ABI istreambuf_iterator(const __proxy& __p) _NOEXCEPT : __sbuf_(__p.__sbuf_) {}

  _LIBCPP_HIDE_FROM_ABI char_type operator*() const { return static_cast<char_type>(__sbuf_->sgetc()); }
  _LIBCPP_HIDE_FROM_ABI istreambuf_iterator& operator++() {
    __sbuf_->sbumpc();
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI __proxy operator++(int) { return __proxy(__sbuf_->sbumpc(), __sbuf_); }

  _LIBCPP_HIDE_FROM_ABI bool equal(const istreambuf_iterator& __b) const {
    return __test_for_eof() == __b.__test_for_eof();
  }

#if _LIBCPP_STD_VER >= 20
  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const istreambuf_iterator& __i, default_sentinel_t) {
    return __i.__test_for_eof();
  }
#endif // _LIBCPP_STD_VER >= 20
};

template <class _CharT, class _Traits>
inline _LIBCPP_HIDE_FROM_ABI bool
operator==(const istreambuf_iterator<_CharT, _Traits>& __a, const istreambuf_iterator<_CharT, _Traits>& __b) {
  return __a.equal(__b);
}

#if _LIBCPP_STD_VER <= 17
template <class _CharT, class _Traits>
inline _LIBCPP_HIDE_FROM_ABI bool
operator!=(const istreambuf_iterator<_CharT, _Traits>& __a, const istreambuf_iterator<_CharT, _Traits>& __b) {
  return !__a.equal(__b);
}
#endif // _LIBCPP_STD_VER <= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H
