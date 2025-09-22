//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_SWAPPABLE_H
#define _LIBCPP___TYPE_TRAITS_IS_SWAPPABLE_H

#include <__config>
#include <__type_traits/add_lvalue_reference.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_assignable.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_nothrow_assignable.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, class _Up, class = void>
inline const bool __is_swappable_with_v = false;

template <class _Tp>
inline const bool __is_swappable_v = __is_swappable_with_v<_Tp&, _Tp&>;

template <class _Tp, class _Up, bool = __is_swappable_with_v<_Tp, _Up> >
inline const bool __is_nothrow_swappable_with_v = false;

template <class _Tp>
inline const bool __is_nothrow_swappable_v = __is_nothrow_swappable_with_v<_Tp&, _Tp&>;

#ifndef _LIBCPP_CXX03_LANG
template <class _Tp>
using __swap_result_t = __enable_if_t<is_move_constructible<_Tp>::value && is_move_assignable<_Tp>::value>;
#else
template <class>
using __swap_result_t = void;
#endif

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 __swap_result_t<_Tp> swap(_Tp& __x, _Tp& __y)
    _NOEXCEPT_(is_nothrow_move_constructible<_Tp>::value&& is_nothrow_move_assignable<_Tp>::value);

template <class _Tp, size_t _Np, __enable_if_t<__is_swappable_v<_Tp>, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI
_LIBCPP_CONSTEXPR_SINCE_CXX20 void swap(_Tp (&__a)[_Np], _Tp (&__b)[_Np]) _NOEXCEPT_(__is_nothrow_swappable_v<_Tp>);

// ALL generic swap overloads MUST already have a declaration available at this point.

template <class _Tp, class _Up>
inline const bool __is_swappable_with_v<_Tp,
                                        _Up,
                                        __void_t<decltype(swap(std::declval<_Tp>(), std::declval<_Up>())),
                                                 decltype(swap(std::declval<_Up>(), std::declval<_Tp>()))> > = true;

#ifndef _LIBCPP_CXX03_LANG // C++03 doesn't have noexcept, so things are never nothrow swappable
template <class _Tp, class _Up>
inline const bool __is_nothrow_swappable_with_v<_Tp, _Up, true> =
    noexcept(swap(std::declval<_Tp>(), std::declval<_Up>())) &&
    noexcept(swap(std::declval<_Up>(), std::declval<_Tp>()));
#endif

#if _LIBCPP_STD_VER >= 17

template <class _Tp, class _Up>
inline constexpr bool is_swappable_with_v = __is_swappable_with_v<_Tp, _Up>;

template <class _Tp, class _Up>
struct _LIBCPP_TEMPLATE_VIS is_swappable_with : bool_constant<is_swappable_with_v<_Tp, _Up>> {};

template <class _Tp>
inline constexpr bool is_swappable_v =
    is_swappable_with_v<__add_lvalue_reference_t<_Tp>, __add_lvalue_reference_t<_Tp>>;

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_swappable : bool_constant<is_swappable_v<_Tp>> {};

template <class _Tp, class _Up>
inline constexpr bool is_nothrow_swappable_with_v = __is_nothrow_swappable_with_v<_Tp, _Up>;

template <class _Tp, class _Up>
struct _LIBCPP_TEMPLATE_VIS is_nothrow_swappable_with : bool_constant<is_nothrow_swappable_with_v<_Tp, _Up>> {};

template <class _Tp>
inline constexpr bool is_nothrow_swappable_v =
    is_nothrow_swappable_with_v<__add_lvalue_reference_t<_Tp>, __add_lvalue_reference_t<_Tp>>;

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_nothrow_swappable : bool_constant<is_nothrow_swappable_v<_Tp>> {};

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_SWAPPABLE_H
