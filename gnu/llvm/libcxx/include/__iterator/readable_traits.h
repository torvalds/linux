// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_READABLE_TRAITS_H
#define _LIBCPP___ITERATOR_READABLE_TRAITS_H

#include <__concepts/same_as.h>
#include <__config>
#include <__type_traits/conditional.h>
#include <__type_traits/is_array.h>
#include <__type_traits/is_object.h>
#include <__type_traits/is_primary_template.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/remove_cvref.h>
#include <__type_traits/remove_extent.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [readable.traits]
template <class>
struct __cond_value_type {};

template <class _Tp>
  requires is_object_v<_Tp>
struct __cond_value_type<_Tp> {
  using value_type = remove_cv_t<_Tp>;
};

template <class _Tp>
concept __has_member_value_type = requires { typename _Tp::value_type; };

template <class _Tp>
concept __has_member_element_type = requires { typename _Tp::element_type; };

template <class>
struct indirectly_readable_traits {};

template <class _Ip>
  requires is_array_v<_Ip>
struct indirectly_readable_traits<_Ip> {
  using value_type = remove_cv_t<remove_extent_t<_Ip>>;
};

template <class _Ip>
struct indirectly_readable_traits<const _Ip> : indirectly_readable_traits<_Ip> {};

template <class _Tp>
struct indirectly_readable_traits<_Tp*> : __cond_value_type<_Tp> {};

template <__has_member_value_type _Tp>
struct indirectly_readable_traits<_Tp> : __cond_value_type<typename _Tp::value_type> {};

template <__has_member_element_type _Tp>
struct indirectly_readable_traits<_Tp> : __cond_value_type<typename _Tp::element_type> {};

template <__has_member_value_type _Tp>
  requires __has_member_element_type<_Tp>
struct indirectly_readable_traits<_Tp> {};

template <__has_member_value_type _Tp>
  requires __has_member_element_type<_Tp> &&
           same_as<remove_cv_t<typename _Tp::element_type>, remove_cv_t<typename _Tp::value_type>>
struct indirectly_readable_traits<_Tp> : __cond_value_type<typename _Tp::value_type> {};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_READABLE_TRAITS_H
