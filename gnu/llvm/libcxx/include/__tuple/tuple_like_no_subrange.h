//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TUPLE_TUPLE_LIKE_NO_SUBRANGE_H
#define _LIBCPP___TUPLE_TUPLE_LIKE_NO_SUBRANGE_H

#include <__config>
#include <__fwd/array.h>
#include <__fwd/complex.h>
#include <__fwd/pair.h>
#include <__fwd/tuple.h>
#include <__tuple/tuple_size.h>
#include <__type_traits/remove_cvref.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _Tp>
inline constexpr bool __tuple_like_no_subrange_impl = false;

template <class... _Tp>
inline constexpr bool __tuple_like_no_subrange_impl<tuple<_Tp...>> = true;

template <class _T1, class _T2>
inline constexpr bool __tuple_like_no_subrange_impl<pair<_T1, _T2>> = true;

template <class _Tp, size_t _Size>
inline constexpr bool __tuple_like_no_subrange_impl<array<_Tp, _Size>> = true;

#  if _LIBCPP_STD_VER >= 26

template <class _Tp>
inline constexpr bool __tuple_like_no_subrange_impl<complex<_Tp>> = true;

#  endif

template <class _Tp>
concept __tuple_like_no_subrange = __tuple_like_no_subrange_impl<remove_cvref_t<_Tp>>;

// This is equivalent to the exposition-only type trait `pair-like`, except that it is false for specializations of
// `ranges::subrange`. This is more useful than the pair-like concept in the standard because every use of `pair-like`
// excludes `ranges::subrange`.
template <class _Tp>
concept __pair_like_no_subrange = __tuple_like_no_subrange<_Tp> && tuple_size<remove_cvref_t<_Tp>>::value == 2;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TUPLE_TUPLE_LIKE_NO_SUBRANGE_H
