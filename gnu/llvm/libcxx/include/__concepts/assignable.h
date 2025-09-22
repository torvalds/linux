//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_ASSIGNABLE_H
#define _LIBCPP___CONCEPTS_ASSIGNABLE_H

#include <__concepts/common_reference_with.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__type_traits/is_reference.h>
#include <__type_traits/make_const_lvalue_ref.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concept.assignable]

template <class _Lhs, class _Rhs>
concept assignable_from =
    is_lvalue_reference_v<_Lhs> &&
    common_reference_with<__make_const_lvalue_ref<_Lhs>, __make_const_lvalue_ref<_Rhs>> &&
    requires(_Lhs __lhs, _Rhs&& __rhs) {
      { __lhs = std::forward<_Rhs>(__rhs) } -> same_as<_Lhs>;
    };

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_ASSIGNABLE_H
