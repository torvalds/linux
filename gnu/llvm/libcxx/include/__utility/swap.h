//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_SWAP_H
#define _LIBCPP___UTILITY_SWAP_H

#include <__config>
#include <__type_traits/is_assignable.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_nothrow_assignable.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/is_swappable.h>
#include <__utility/declval.h>
#include <__utility/move.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_CXX03_LANG
template <class _Tp>
using __swap_result_t = __enable_if_t<is_move_constructible<_Tp>::value && is_move_assignable<_Tp>::value>;
#else
template <class>
using __swap_result_t = void;
#endif

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI __swap_result_t<_Tp> _LIBCPP_CONSTEXPR_SINCE_CXX20 swap(_Tp& __x, _Tp& __y)
    _NOEXCEPT_(is_nothrow_move_constructible<_Tp>::value&& is_nothrow_move_assignable<_Tp>::value) {
  _Tp __t(std::move(__x));
  __x = std::move(__y);
  __y = std::move(__t);
}

template <class _Tp, size_t _Np, __enable_if_t<__is_swappable_v<_Tp>, int> >
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void swap(_Tp (&__a)[_Np], _Tp (&__b)[_Np])
    _NOEXCEPT_(__is_nothrow_swappable_v<_Tp>) {
  for (size_t __i = 0; __i != _Np; ++__i) {
    swap(__a[__i], __b[__i]);
  }
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___UTILITY_SWAP_H
