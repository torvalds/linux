//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_CAN_EXTRACT_KEY_H
#define _LIBCPP___TYPE_TRAITS_CAN_EXTRACT_KEY_H

#include <__config>
#include <__fwd/pair.h>
#include <__type_traits/conditional.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_same.h>
#include <__type_traits/remove_const.h>
#include <__type_traits/remove_const_ref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// These traits are used in __tree and __hash_table
struct __extract_key_fail_tag {};
struct __extract_key_self_tag {};
struct __extract_key_first_tag {};

template <class _ValTy, class _Key, class _RawValTy = __remove_const_ref_t<_ValTy> >
struct __can_extract_key
    : __conditional_t<_IsSame<_RawValTy, _Key>::value, __extract_key_self_tag, __extract_key_fail_tag> {};

template <class _Pair, class _Key, class _First, class _Second>
struct __can_extract_key<_Pair, _Key, pair<_First, _Second> >
    : __conditional_t<_IsSame<__remove_const_t<_First>, _Key>::value, __extract_key_first_tag, __extract_key_fail_tag> {
};

// __can_extract_map_key uses true_type/false_type instead of the tags.
// It returns true if _Key != _ContainerValueTy (the container is a map not a set)
// and _ValTy == _Key.
template <class _ValTy, class _Key, class _ContainerValueTy, class _RawValTy = __remove_const_ref_t<_ValTy> >
struct __can_extract_map_key : integral_constant<bool, _IsSame<_RawValTy, _Key>::value> {};

// This specialization returns __extract_key_fail_tag for non-map containers
// because _Key == _ContainerValueTy
template <class _ValTy, class _Key, class _RawValTy>
struct __can_extract_map_key<_ValTy, _Key, _Key, _RawValTy> : false_type {};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_CAN_EXTRACT_KEY_H
