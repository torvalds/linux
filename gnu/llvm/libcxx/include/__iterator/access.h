// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_ACCESS_H
#define _LIBCPP___ITERATOR_ACCESS_H

#include <__config>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, size_t _Np>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _Tp* begin(_Tp (&__array)[_Np]) _NOEXCEPT {
  return __array;
}

template <class _Tp, size_t _Np>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _Tp* end(_Tp (&__array)[_Np]) _NOEXCEPT {
  return __array + _Np;
}

#if !defined(_LIBCPP_CXX03_LANG)

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 auto begin(_Cp& __c) -> decltype(__c.begin()) {
  return __c.begin();
}

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 auto begin(const _Cp& __c) -> decltype(__c.begin()) {
  return __c.begin();
}

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 auto end(_Cp& __c) -> decltype(__c.end()) {
  return __c.end();
}

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 auto end(const _Cp& __c) -> decltype(__c.end()) {
  return __c.end();
}

#  if _LIBCPP_STD_VER >= 14

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI constexpr auto
cbegin(const _Cp& __c) noexcept(noexcept(std::begin(__c))) -> decltype(std::begin(__c)) {
  return std::begin(__c);
}

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI constexpr auto cend(const _Cp& __c) noexcept(noexcept(std::end(__c))) -> decltype(std::end(__c)) {
  return std::end(__c);
}

#  endif

#else // defined(_LIBCPP_CXX03_LANG)

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI typename _Cp::iterator begin(_Cp& __c) {
  return __c.begin();
}

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI typename _Cp::const_iterator begin(const _Cp& __c) {
  return __c.begin();
}

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI typename _Cp::iterator end(_Cp& __c) {
  return __c.end();
}

template <class _Cp>
_LIBCPP_HIDE_FROM_ABI typename _Cp::const_iterator end(const _Cp& __c) {
  return __c.end();
}

#endif // !defined(_LIBCPP_CXX03_LANG)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_ACCESS_H
