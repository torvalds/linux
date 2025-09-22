// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_ADDRESSOF_H
#define _LIBCPP___MEMORY_ADDRESSOF_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX17 _LIBCPP_NO_CFI _LIBCPP_HIDE_FROM_ABI _Tp* addressof(_Tp& __x) _NOEXCEPT {
  return __builtin_addressof(__x);
}

#if defined(_LIBCPP_HAS_OBJC_ARC) && !defined(_LIBCPP_PREDEFINED_OBJC_ARC_ADDRESSOF)
// Objective-C++ Automatic Reference Counting uses qualified pointers
// that require special addressof() signatures. When
// _LIBCPP_PREDEFINED_OBJC_ARC_ADDRESSOF is defined, the compiler
// itself is providing these definitions. Otherwise, we provide them.
template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI __strong _Tp* addressof(__strong _Tp& __x) _NOEXCEPT {
  return &__x;
}

#  ifdef _LIBCPP_HAS_OBJC_ARC_WEAK
template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI __weak _Tp* addressof(__weak _Tp& __x) _NOEXCEPT {
  return &__x;
}
#  endif

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI __autoreleasing _Tp* addressof(__autoreleasing _Tp& __x) _NOEXCEPT {
  return &__x;
}

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI __unsafe_unretained _Tp* addressof(__unsafe_unretained _Tp& __x) _NOEXCEPT {
  return &__x;
}
#endif

#if !defined(_LIBCPP_CXX03_LANG)
template <class _Tp>
_Tp* addressof(const _Tp&&) noexcept = delete;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_ADDRESSOF_H
