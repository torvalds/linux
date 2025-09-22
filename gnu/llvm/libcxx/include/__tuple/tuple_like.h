//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TUPLE_TUPLE_LIKE_H
#define _LIBCPP___TUPLE_TUPLE_LIKE_H

#include <__config>
#include <__fwd/subrange.h>
#include <__tuple/tuple_like_no_subrange.h>
#include <__tuple/tuple_size.h>
#include <__type_traits/remove_cvref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _Tp>
inline constexpr bool __is_ranges_subrange_v = false;

template <class _Iter, class _Sent, ranges::subrange_kind _Kind>
inline constexpr bool __is_ranges_subrange_v<ranges::subrange<_Iter, _Sent, _Kind>> = true;

template <class _Tp>
concept __tuple_like = __tuple_like_no_subrange<_Tp> || __is_ranges_subrange_v<remove_cvref_t<_Tp>>;

// As of writing this comment every use of `pair-like` in the standard excludes `ranges::subrange`, so
// you most likely want to use `__pair_like_no_subrange` if you're looking for `pair-like`.

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TUPLE_TUPLE_LIKE_H
