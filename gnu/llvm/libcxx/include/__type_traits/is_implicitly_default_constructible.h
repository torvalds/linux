//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_IMPLICITLY_DEFAULT_CONSTRUCTIBLE_H
#define _LIBCPP___TYPE_TRAITS_IS_IMPLICITLY_DEFAULT_CONSTRUCTIBLE_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_constructible.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_CXX03_LANG
// First of all, we can't implement this check in C++03 mode because the {}
// default initialization syntax isn't valid.
// Second, we implement the trait in a funny manner with two defaulted template
// arguments to workaround Clang's PR43454.
template <class _Tp>
void __test_implicit_default_constructible(_Tp);

template <class _Tp, class = void, class = typename is_default_constructible<_Tp>::type>
struct __is_implicitly_default_constructible : false_type {};

template <class _Tp>
struct __is_implicitly_default_constructible<_Tp,
                                             decltype(std::__test_implicit_default_constructible<_Tp const&>({})),
                                             true_type> : true_type {};

template <class _Tp>
struct __is_implicitly_default_constructible<_Tp,
                                             decltype(std::__test_implicit_default_constructible<_Tp const&>({})),
                                             false_type> : false_type {};
#endif // !C++03

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_IMPLICITLY_DEFAULT_CONSTRUCTIBLE_H
