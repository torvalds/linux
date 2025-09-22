//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_VALID_EXPANSION_H
#define _LIBCPP___TYPE_TRAITS_IS_VALID_EXPANSION_H

#include <__config>
#include <__type_traits/integral_constant.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <template <class...> class _Templ, class... _Args, class = _Templ<_Args...> >
true_type __sfinae_test_impl(int);
template <template <class...> class, class...>
false_type __sfinae_test_impl(...);

template <template <class...> class _Templ, class... _Args>
using _IsValidExpansion _LIBCPP_NODEBUG = decltype(std::__sfinae_test_impl<_Templ, _Args...>(0));

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_VALID_EXPANSION_H
