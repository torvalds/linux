//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_TOTALLY_ORDERED_H
#define _LIBCPP___CONCEPTS_TOTALLY_ORDERED_H

#include <__concepts/boolean_testable.h>
#include <__concepts/equality_comparable.h>
#include <__config>
#include <__type_traits/common_reference.h>
#include <__type_traits/make_const_lvalue_ref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concept.totallyordered]

template <class _Tp, class _Up>
concept __partially_ordered_with = requires(__make_const_lvalue_ref<_Tp> __t, __make_const_lvalue_ref<_Up> __u) {
  { __t < __u } -> __boolean_testable;
  { __t > __u } -> __boolean_testable;
  { __t <= __u } -> __boolean_testable;
  { __t >= __u } -> __boolean_testable;
  { __u < __t } -> __boolean_testable;
  { __u > __t } -> __boolean_testable;
  { __u <= __t } -> __boolean_testable;
  { __u >= __t } -> __boolean_testable;
};

template <class _Tp>
concept totally_ordered = equality_comparable<_Tp> && __partially_ordered_with<_Tp, _Tp>;

// clang-format off
template <class _Tp, class _Up>
concept totally_ordered_with =
    totally_ordered<_Tp> && totally_ordered<_Up> &&
    equality_comparable_with<_Tp, _Up> &&
    totally_ordered<
        common_reference_t<
            __make_const_lvalue_ref<_Tp>,
            __make_const_lvalue_ref<_Up>>> &&
    __partially_ordered_with<_Tp, _Up>;
// clang-format on

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_TOTALLY_ORDERED_H
