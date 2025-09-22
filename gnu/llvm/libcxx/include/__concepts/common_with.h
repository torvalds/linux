//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_COMMON_WITH_H
#define _LIBCPP___CONCEPTS_COMMON_WITH_H

#include <__concepts/common_reference_with.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__type_traits/add_lvalue_reference.h>
#include <__type_traits/common_reference.h>
#include <__type_traits/common_type.h>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concept.common]

// clang-format off
template <class _Tp, class _Up>
concept common_with =
    same_as<common_type_t<_Tp, _Up>, common_type_t<_Up, _Tp>> &&
    requires {
        static_cast<common_type_t<_Tp, _Up>>(std::declval<_Tp>());
        static_cast<common_type_t<_Tp, _Up>>(std::declval<_Up>());
    } &&
    common_reference_with<
        add_lvalue_reference_t<const _Tp>,
        add_lvalue_reference_t<const _Up>> &&
    common_reference_with<
        add_lvalue_reference_t<common_type_t<_Tp, _Up>>,
        common_reference_t<
            add_lvalue_reference_t<const _Tp>,
            add_lvalue_reference_t<const _Up>>>;
// clang-format on

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_COMMON_WITH_H
