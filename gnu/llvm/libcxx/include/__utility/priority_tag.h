//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_PRIORITY_TAG_H
#define _LIBCPP___UTILITY_PRIORITY_TAG_H

#include <__config>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <size_t _Ip>
struct __priority_tag : __priority_tag<_Ip - 1> {};
template <>
struct __priority_tag<0> {};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_PRIORITY_TAG_H
