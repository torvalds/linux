// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_ISTREAM_VIEW_H
#define _LIBCPP___RANGES_ISTREAM_VIEW_H

#include <__concepts/constructible.h>
#include <__concepts/derived_from.h>
#include <__concepts/movable.h>
#include <__config>
#include <__fwd/istream.h>
#include <__fwd/string.h>
#include <__iterator/default_sentinel.h>
#include <__iterator/iterator_traits.h>
#include <__memory/addressof.h>
#include <__ranges/view_interface.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/forward.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {

template <class _Val, class _CharT, class _Traits>
concept __stream_extractable = requires(basic_istream<_CharT, _Traits>& __is, _Val& __t) { __is >> __t; };

template <movable _Val, class _CharT, class _Traits = char_traits<_CharT>>
  requires default_initializable<_Val> && __stream_extractable<_Val, _CharT, _Traits>
class basic_istream_view : public view_interface<basic_istream_view<_Val, _CharT, _Traits>> {
  class __iterator;

public:
  _LIBCPP_HIDE_FROM_ABI constexpr explicit basic_istream_view(basic_istream<_CharT, _Traits>& __stream)
      : __stream_(std::addressof(__stream)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() {
    *__stream_ >> __value_;
    return __iterator{*this};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr default_sentinel_t end() const noexcept { return default_sentinel; }

private:
  basic_istream<_CharT, _Traits>* __stream_;
  _LIBCPP_NO_UNIQUE_ADDRESS _Val __value_ = _Val();
};

template <movable _Val, class _CharT, class _Traits>
  requires default_initializable<_Val> && __stream_extractable<_Val, _CharT, _Traits>
class basic_istream_view<_Val, _CharT, _Traits>::__iterator {
public:
  using iterator_concept = input_iterator_tag;
  using difference_type  = ptrdiff_t;
  using value_type       = _Val;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __iterator(basic_istream_view<_Val, _CharT, _Traits>& __parent) noexcept
      : __parent_(std::addressof(__parent)) {}

  __iterator(const __iterator&)                  = delete;
  _LIBCPP_HIDE_FROM_ABI __iterator(__iterator&&) = default;

  __iterator& operator=(const __iterator&)                  = delete;
  _LIBCPP_HIDE_FROM_ABI __iterator& operator=(__iterator&&) = default;

  _LIBCPP_HIDE_FROM_ABI __iterator& operator++() {
    *__parent_->__stream_ >> __parent_->__value_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI void operator++(int) { ++*this; }

  _LIBCPP_HIDE_FROM_ABI _Val& operator*() const { return __parent_->__value_; }

  _LIBCPP_HIDE_FROM_ABI friend bool operator==(const __iterator& __x, default_sentinel_t) {
    return !*__x.__get_parent_stream();
  }

private:
  basic_istream_view<_Val, _CharT, _Traits>* __parent_;

  _LIBCPP_HIDE_FROM_ABI constexpr basic_istream<_CharT, _Traits>* __get_parent_stream() const {
    return __parent_->__stream_;
  }
};

template <class _Val>
using istream_view = basic_istream_view<_Val, char>;

#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <class _Val>
using wistream_view = basic_istream_view<_Val, wchar_t>;
#  endif

namespace views {
namespace __istream {

// clang-format off
template <class _Tp>
struct __fn {
  template <class _Up, class _UnCVRef = remove_cvref_t<_Up>>
    requires derived_from<_UnCVRef, basic_istream<typename _UnCVRef::char_type,
                                                  typename _UnCVRef::traits_type>>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Up&& __u) const
    noexcept(noexcept(basic_istream_view<_Tp, typename _UnCVRef::char_type,
                                              typename _UnCVRef::traits_type>(std::forward<_Up>(__u))))
    -> decltype(      basic_istream_view<_Tp, typename _UnCVRef::char_type,
                                              typename _UnCVRef::traits_type>(std::forward<_Up>(__u)))
    {   return        basic_istream_view<_Tp, typename _UnCVRef::char_type,
                                              typename _UnCVRef::traits_type>(std::forward<_Up>(__u));
    }
};
// clang-format on

} // namespace __istream

inline namespace __cpo {
template <class _Tp>
inline constexpr auto istream = __istream::__fn<_Tp>{};
} // namespace __cpo
} // namespace views

} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___RANGES_ISTREAM_VIEW_H
