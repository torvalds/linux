// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_TO_UNDERLYING_H
#define _LIBCPP___UTILITY_TO_UNDERLYING_H

#include <__config>
#include <__type_traits/underlying_type.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_CXX03_LANG
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr typename underlying_type<_Tp>::type __to_underlying(_Tp __val) noexcept {
  return static_cast<typename underlying_type<_Tp>::type>(__val);
}
#endif // !_LIBCPP_CXX03_LANG

#if _LIBCPP_STD_VER >= 23
template <class _Tp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr underlying_type_t<_Tp> to_underlying(_Tp __val) noexcept {
  return std::__to_underlying(__val);
}
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_TO_UNDERLYING_H
