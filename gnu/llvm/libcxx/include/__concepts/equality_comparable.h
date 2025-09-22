//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_EQUALITY_COMPARABLE_H
#define _LIBCPP___CONCEPTS_EQUALITY_COMPARABLE_H

#include <__concepts/boolean_testable.h>
#include <__concepts/common_reference_with.h>
#include <__config>
#include <__type_traits/common_reference.h>
#include <__type_traits/make_const_lvalue_ref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concept.equalitycomparable]

template <class _Tp, class _Up>
concept __weakly_equality_comparable_with =
    requires(__make_const_lvalue_ref<_Tp> __t, __make_const_lvalue_ref<_Up> __u) {
      { __t == __u } -> __boolean_testable;
      { __t != __u } -> __boolean_testable;
      { __u == __t } -> __boolean_testable;
      { __u != __t } -> __boolean_testable;
    };

template <class _Tp>
concept equality_comparable = __weakly_equality_comparable_with<_Tp, _Tp>;

// clang-format off
template <class _Tp, class _Up>
concept equality_comparable_with =
    equality_comparable<_Tp> && equality_comparable<_Up> &&
    common_reference_with<__make_const_lvalue_ref<_Tp>, __make_const_lvalue_ref<_Up>> &&
    equality_comparable<
        common_reference_t<
            __make_const_lvalue_ref<_Tp>,
            __make_const_lvalue_ref<_Up>>> &&
    __weakly_equality_comparable_with<_Tp, _Up>;
// clang-format on

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_EQUALITY_COMPARABLE_H
