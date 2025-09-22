//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___COMPARE_COMPARE_THREE_WAY_RESULT_H
#define _LIBCPP___COMPARE_COMPARE_THREE_WAY_RESULT_H

#include <__config>
#include <__type_traits/make_const_lvalue_ref.h>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class, class, class>
struct _LIBCPP_HIDE_FROM_ABI __compare_three_way_result {};

template <class _Tp, class _Up>
struct _LIBCPP_HIDE_FROM_ABI __compare_three_way_result<
    _Tp,
    _Up,
    decltype(std::declval<__make_const_lvalue_ref<_Tp>>() <=> std::declval<__make_const_lvalue_ref<_Up>>(), void())> {
  using type = decltype(std::declval<__make_const_lvalue_ref<_Tp>>() <=> std::declval<__make_const_lvalue_ref<_Up>>());
};

template <class _Tp, class _Up = _Tp>
struct _LIBCPP_TEMPLATE_VIS compare_three_way_result : __compare_three_way_result<_Tp, _Up, void> {};

template <class _Tp, class _Up = _Tp>
using compare_three_way_result_t = typename compare_three_way_result<_Tp, _Up>::type;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___COMPARE_COMPARE_THREE_WAY_RESULT_H
