//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_IN_PLACE_H
#define _LIBCPP___UTILITY_IN_PLACE_H

#include <__config>
#include <__type_traits/remove_cvref.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

struct _LIBCPP_EXPORTED_FROM_ABI in_place_t {
  explicit in_place_t() = default;
};
inline constexpr in_place_t in_place{};

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS in_place_type_t {
  _LIBCPP_HIDE_FROM_ABI explicit in_place_type_t() = default;
};
template <class _Tp>
inline constexpr in_place_type_t<_Tp> in_place_type{};

template <size_t _Idx>
struct _LIBCPP_TEMPLATE_VIS in_place_index_t {
  _LIBCPP_HIDE_FROM_ABI explicit in_place_index_t() = default;
};
template <size_t _Idx>
inline constexpr in_place_index_t<_Idx> in_place_index{};

template <class _Tp>
struct __is_inplace_type_imp : false_type {};
template <class _Tp>
struct __is_inplace_type_imp<in_place_type_t<_Tp>> : true_type {};

template <class _Tp>
using __is_inplace_type = __is_inplace_type_imp<__remove_cvref_t<_Tp>>;

template <class _Tp>
struct __is_inplace_index_imp : false_type {};
template <size_t _Idx>
struct __is_inplace_index_imp<in_place_index_t<_Idx>> : true_type {};

template <class _Tp>
using __is_inplace_index = __is_inplace_index_imp<__remove_cvref_t<_Tp>>;

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_IN_PLACE_H
