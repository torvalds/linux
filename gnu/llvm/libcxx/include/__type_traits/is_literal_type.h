//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_LITERAL_TYPE
#define _LIBCPP___TYPE_TRAITS_IS_LITERAL_TYPE

#include <__config>
#include <__type_traits/integral_constant.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS)
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS
_LIBCPP_DEPRECATED_IN_CXX17 is_literal_type : public integral_constant<bool, __is_literal_type(_Tp)> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
_LIBCPP_DEPRECATED_IN_CXX17 inline constexpr bool is_literal_type_v = __is_literal_type(_Tp);
#  endif // _LIBCPP_STD_VER >= 17
#endif   // _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_LITERAL_TYPE
