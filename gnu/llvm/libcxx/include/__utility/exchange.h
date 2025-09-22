//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_EXCHANGE_H
#define _LIBCPP___UTILITY_EXCHANGE_H

#include <__config>
#include <__type_traits/is_nothrow_assignable.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__utility/forward.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 14
template <class _T1, class _T2 = _T1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _T1 exchange(_T1& __obj, _T2&& __new_value) noexcept(
    is_nothrow_move_constructible<_T1>::value && is_nothrow_assignable<_T1&, _T2>::value) {
  _T1 __old_value = std::move(__obj);
  __obj           = std::forward<_T2>(__new_value);
  return __old_value;
}
#endif // _LIBCPP_STD_VER >= 14

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___UTILITY_EXCHANGE_H
