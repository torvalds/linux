//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___COMPARE_THREE_WAY_COMPARABLE_H
#define _LIBCPP___COMPARE_THREE_WAY_COMPARABLE_H

#include <__compare/common_comparison_category.h>
#include <__compare/ordering.h>
#include <__concepts/common_reference_with.h>
#include <__concepts/equality_comparable.h>
#include <__concepts/same_as.h>
#include <__concepts/totally_ordered.h>
#include <__config>
#include <__type_traits/common_reference.h>
#include <__type_traits/make_const_lvalue_ref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _Tp, class _Cat>
concept __compares_as = same_as<common_comparison_category_t<_Tp, _Cat>, _Cat>;

template <class _Tp, class _Cat = partial_ordering>
concept three_way_comparable =
    __weakly_equality_comparable_with<_Tp, _Tp> && __partially_ordered_with<_Tp, _Tp> &&
    requires(__make_const_lvalue_ref<_Tp> __a, __make_const_lvalue_ref<_Tp> __b) {
      { __a <=> __b } -> __compares_as<_Cat>;
    };

template <class _Tp, class _Up, class _Cat = partial_ordering>
concept three_way_comparable_with =
    three_way_comparable<_Tp, _Cat> && three_way_comparable<_Up, _Cat> &&
    common_reference_with<__make_const_lvalue_ref<_Tp>, __make_const_lvalue_ref<_Up>> &&
    three_way_comparable<common_reference_t<__make_const_lvalue_ref<_Tp>, __make_const_lvalue_ref<_Up>>, _Cat> &&
    __weakly_equality_comparable_with<_Tp, _Up> && __partially_ordered_with<_Tp, _Up> &&
    requires(__make_const_lvalue_ref<_Tp> __t, __make_const_lvalue_ref<_Up> __u) {
      { __t <=> __u } -> __compares_as<_Cat>;
      { __u <=> __t } -> __compares_as<_Cat>;
    };

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___COMPARE_THREE_WAY_COMPARABLE_H
