//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_DERIVED_FROM_H
#define _LIBCPP___CONCEPTS_DERIVED_FROM_H

#include <__config>
#include <__type_traits/is_base_of.h>
#include <__type_traits/is_convertible.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concept.derived]

template <class _Dp, class _Bp>
concept derived_from = is_base_of_v<_Bp, _Dp> && is_convertible_v<const volatile _Dp*, const volatile _Bp*>;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_DERIVED_FROM_H
