// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_DATA_H
#define _LIBCPP___ITERATOR_DATA_H

#include <__config>
#include <cstddef>
#include <initializer_list>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

template <class _Cont>
constexpr _LIBCPP_HIDE_FROM_ABI auto data(_Cont& __c) noexcept(noexcept(__c.data())) -> decltype(__c.data()) {
  return __c.data();
}

template <class _Cont>
constexpr _LIBCPP_HIDE_FROM_ABI auto data(const _Cont& __c) noexcept(noexcept(__c.data())) -> decltype(__c.data()) {
  return __c.data();
}

template <class _Tp, size_t _Sz>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp* data(_Tp (&__array)[_Sz]) noexcept {
  return __array;
}

template <class _Ep>
_LIBCPP_HIDE_FROM_ABI constexpr const _Ep* data(initializer_list<_Ep> __il) noexcept {
  return __il.begin();
}

#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_DATA_H
