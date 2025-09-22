//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_PIECEWISE_CONSTRUCT_H
#define _LIBCPP___UTILITY_PIECEWISE_CONSTRUCT_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

struct _LIBCPP_TEMPLATE_VIS piecewise_construct_t {
  explicit piecewise_construct_t() = default;
};

#if _LIBCPP_STD_VER >= 17
inline constexpr piecewise_construct_t piecewise_construct = piecewise_construct_t();
#elif !defined(_LIBCPP_CXX03_LANG)
constexpr piecewise_construct_t piecewise_construct = piecewise_construct_t();
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_PIECEWISE_CONSTRUCT_H
