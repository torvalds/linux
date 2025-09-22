//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_AS_CONST_H
#define _LIBCPP___UTILITY_AS_CONST_H

#include <__config>
#include <__type_traits/add_const.h>
#include <__utility/forward.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17
template <class _Tp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr add_const_t<_Tp>& as_const(_Tp& __t) noexcept {
  return __t;
}

template <class _Tp>
void as_const(const _Tp&&) = delete;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_AS_CONST_H
