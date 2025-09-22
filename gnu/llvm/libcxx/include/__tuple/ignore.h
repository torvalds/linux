//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TUPLE_IGNORE_H
#define _LIBCPP___TUPLE_IGNORE_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#ifndef _LIBCPP_CXX03_LANG

_LIBCPP_BEGIN_NAMESPACE_STD

struct __ignore_type {
  template <class _Tp>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 const __ignore_type& operator=(const _Tp&) const noexcept {
    return *this;
  }
};

#  if _LIBCPP_STD_VER >= 17
inline constexpr __ignore_type ignore;
#  else
constexpr __ignore_type ignore;
#  endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_CXX03_LANG

#endif // _LIBCPP___TUPLE_IGNORE_H
