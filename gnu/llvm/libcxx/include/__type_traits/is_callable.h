//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_CALLABLE_H
#define _LIBCPP___TYPE_TRAITS_IS_CALLABLE_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Func, class... _Args, class = decltype(std::declval<_Func>()(std::declval<_Args>()...))>
true_type __is_callable_helper(int);
template <class...>
false_type __is_callable_helper(...);

template <class _Func, class... _Args>
struct __is_callable : decltype(std::__is_callable_helper<_Func, _Args...>(0)) {};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_CALLABLE_H
