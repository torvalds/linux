// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef _LIBCPP___EXPECTED_UNEXPECTED_H
#define _LIBCPP___EXPECTED_UNEXPECTED_H

#include <__config>
#include <__type_traits/conjunction.h>
#include <__type_traits/is_array.h>
#include <__type_traits/is_const.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/is_object.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_swappable.h>
#include <__type_traits/is_volatile.h>
#include <__type_traits/negation.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/forward.h>
#include <__utility/in_place.h>
#include <__utility/move.h>
#include <__utility/swap.h>
#include <initializer_list>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 23

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Err>
class unexpected;

template <class _Tp>
struct __is_std_unexpected : false_type {};

template <class _Err>
struct __is_std_unexpected<unexpected<_Err>> : true_type {};

template <class _Tp>
using __valid_std_unexpected = _BoolConstant< //
    is_object_v<_Tp> &&                       //
    !is_array_v<_Tp> &&                       //
    !__is_std_unexpected<_Tp>::value &&       //
    !is_const_v<_Tp> &&                       //
    !is_volatile_v<_Tp>                       //
    >;

template <class _Err>
class unexpected {
  static_assert(__valid_std_unexpected<_Err>::value,
                "[expected.un.general] states a program that instantiates std::unexpected for a non-object type, an "
                "array type, a specialization of unexpected, or a cv-qualified type is ill-formed.");

public:
  _LIBCPP_HIDE_FROM_ABI constexpr unexpected(const unexpected&) = default;
  _LIBCPP_HIDE_FROM_ABI constexpr unexpected(unexpected&&)      = default;

  template <class _Error = _Err>
    requires(!is_same_v<remove_cvref_t<_Error>, unexpected> && //
             !is_same_v<remove_cvref_t<_Error>, in_place_t> && //
             is_constructible_v<_Err, _Error>)
  _LIBCPP_HIDE_FROM_ABI constexpr explicit unexpected(_Error&& __error) //
      noexcept(is_nothrow_constructible_v<_Err, _Error>)                // strengthened
      : __unex_(std::forward<_Error>(__error)) {}

  template <class... _Args>
    requires is_constructible_v<_Err, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit unexpected(in_place_t, _Args&&... __args) //
      noexcept(is_nothrow_constructible_v<_Err, _Args...>)                           // strengthened
      : __unex_(std::forward<_Args>(__args)...) {}

  template <class _Up, class... _Args>
    requires is_constructible_v<_Err, initializer_list<_Up>&, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit unexpected(in_place_t, initializer_list<_Up> __il, _Args&&... __args) //
      noexcept(is_nothrow_constructible_v<_Err, initializer_list<_Up>&, _Args...>) // strengthened
      : __unex_(__il, std::forward<_Args>(__args)...) {}

  _LIBCPP_HIDE_FROM_ABI constexpr unexpected& operator=(const unexpected&) = default;
  _LIBCPP_HIDE_FROM_ABI constexpr unexpected& operator=(unexpected&&)      = default;

  _LIBCPP_HIDE_FROM_ABI constexpr const _Err& error() const& noexcept { return __unex_; }
  _LIBCPP_HIDE_FROM_ABI constexpr _Err& error() & noexcept { return __unex_; }
  _LIBCPP_HIDE_FROM_ABI constexpr const _Err&& error() const&& noexcept { return std::move(__unex_); }
  _LIBCPP_HIDE_FROM_ABI constexpr _Err&& error() && noexcept { return std::move(__unex_); }

  _LIBCPP_HIDE_FROM_ABI constexpr void swap(unexpected& __other) noexcept(is_nothrow_swappable_v<_Err>) {
    static_assert(is_swappable_v<_Err>, "unexpected::swap requires is_swappable_v<E> to be true");
    using std::swap;
    swap(__unex_, __other.__unex_);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr void swap(unexpected& __x, unexpected& __y) noexcept(noexcept(__x.swap(__y)))
    requires is_swappable_v<_Err>
  {
    __x.swap(__y);
  }

  template <class _Err2>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const unexpected& __x, const unexpected<_Err2>& __y) {
    return __x.__unex_ == __y.__unex_;
  }

private:
  _Err __unex_;
};

template <class _Err>
unexpected(_Err) -> unexpected<_Err>;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_POP_MACROS

#endif // _LIBCPP___EXPECTED_UNEXPECTED_H
