// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H
#define _LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H

#include <__config>
#include <__iterator/iterator.h>
#include <__iterator/iterator_traits.h>
#include <cstddef>
#include <iosfwd> // for forward declaration of basic_streambuf

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
template <class _CharT, class _Traits>
class _LIBCPP_TEMPLATE_VIS ostreambuf_iterator
#if _LIBCPP_STD_VER <= 14 || !defined(_LIBCPP_ABI_NO_ITERATOR_BASES)
    : public iterator<output_iterator_tag, void, void, void, void>
#endif
{
  _LIBCPP_SUPPRESS_DEPRECATED_POP

public:
  typedef output_iterator_tag iterator_category;
  typedef void value_type;
#if _LIBCPP_STD_VER >= 20
  typedef ptrdiff_t difference_type;
#else
  typedef void difference_type;
#endif
  typedef void pointer;
  typedef void reference;
  typedef _CharT char_type;
  typedef _Traits traits_type;
  typedef basic_streambuf<_CharT, _Traits> streambuf_type;
  typedef basic_ostream<_CharT, _Traits> ostream_type;

private:
  streambuf_type* __sbuf_;

public:
  _LIBCPP_HIDE_FROM_ABI ostreambuf_iterator(ostream_type& __s) _NOEXCEPT : __sbuf_(__s.rdbuf()) {}
  _LIBCPP_HIDE_FROM_ABI ostreambuf_iterator(streambuf_type* __s) _NOEXCEPT : __sbuf_(__s) {}
  _LIBCPP_HIDE_FROM_ABI ostreambuf_iterator& operator=(_CharT __c) {
    if (__sbuf_ && traits_type::eq_int_type(__sbuf_->sputc(__c), traits_type::eof()))
      __sbuf_ = nullptr;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI ostreambuf_iterator& operator*() { return *this; }
  _LIBCPP_HIDE_FROM_ABI ostreambuf_iterator& operator++() { return *this; }
  _LIBCPP_HIDE_FROM_ABI ostreambuf_iterator& operator++(int) { return *this; }
  _LIBCPP_HIDE_FROM_ABI bool failed() const _NOEXCEPT { return __sbuf_ == nullptr; }

  template <class _Ch, class _Tr>
  friend _LIBCPP_HIDE_FROM_ABI ostreambuf_iterator<_Ch, _Tr> __pad_and_output(
      ostreambuf_iterator<_Ch, _Tr> __s, const _Ch* __ob, const _Ch* __op, const _Ch* __oe, ios_base& __iob, _Ch __fl);
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H
