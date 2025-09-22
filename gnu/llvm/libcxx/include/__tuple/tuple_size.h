//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TUPLE_TUPLE_SIZE_H
#define _LIBCPP___TUPLE_TUPLE_SIZE_H

#include <__config>
#include <__fwd/tuple.h>
#include <__tuple/tuple_types.h>
#include <__type_traits/is_const.h>
#include <__type_traits/is_volatile.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS tuple_size;

#if !defined(_LIBCPP_CXX03_LANG)
template <class _Tp, class...>
using __enable_if_tuple_size_imp = _Tp;

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS tuple_size<__enable_if_tuple_size_imp< const _Tp,
                                                                   __enable_if_t<!is_volatile<_Tp>::value>,
                                                                   integral_constant<size_t, sizeof(tuple_size<_Tp>)>>>
    : public integral_constant<size_t, tuple_size<_Tp>::value> {};

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS tuple_size<__enable_if_tuple_size_imp< volatile _Tp,
                                                                   __enable_if_t<!is_const<_Tp>::value>,
                                                                   integral_constant<size_t, sizeof(tuple_size<_Tp>)>>>
    : public integral_constant<size_t, tuple_size<_Tp>::value> {};

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS
tuple_size<__enable_if_tuple_size_imp<const volatile _Tp, integral_constant<size_t, sizeof(tuple_size<_Tp>)>>>
    : public integral_constant<size_t, tuple_size<_Tp>::value> {};

#else
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS tuple_size<const _Tp> : public tuple_size<_Tp> {};
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS tuple_size<volatile _Tp> : public tuple_size<_Tp> {};
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS tuple_size<const volatile _Tp> : public tuple_size<_Tp> {};
#endif

#ifndef _LIBCPP_CXX03_LANG

template <class... _Tp>
struct _LIBCPP_TEMPLATE_VIS tuple_size<tuple<_Tp...> > : public integral_constant<size_t, sizeof...(_Tp)> {};

template <class... _Tp>
struct _LIBCPP_TEMPLATE_VIS tuple_size<__tuple_types<_Tp...> > : public integral_constant<size_t, sizeof...(_Tp)> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr size_t tuple_size_v = tuple_size<_Tp>::value;
#  endif

#endif // _LIBCPP_CXX03_LANG

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TUPLE_TUPLE_SIZE_H
