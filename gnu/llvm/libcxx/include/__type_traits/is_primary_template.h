//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_PRIMARY_TEMPLATE_H
#define _LIBCPP___TYPE_TRAITS_IS_PRIMARY_TEMPLATE_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_valid_expansion.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
using __test_for_primary_template = __enable_if_t<_IsSame<_Tp, typename _Tp::__primary_template>::value>;

template <class _Tp>
using __is_primary_template = _IsValidExpansion<__test_for_primary_template, _Tp>;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_PRIMARY_TEMPLATE_H
