// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_INCREMENTABLE_TRAITS_H
#define _LIBCPP___ITERATOR_INCREMENTABLE_TRAITS_H

#include <__concepts/arithmetic.h>
#include <__config>
#include <__type_traits/conditional.h>
#include <__type_traits/is_object.h>
#include <__type_traits/is_primary_template.h>
#include <__type_traits/make_signed.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/declval.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [incrementable.traits]
template <class>
struct incrementable_traits {};

template <class _Tp>
  requires is_object_v<_Tp>
struct incrementable_traits<_Tp*> {
  using difference_type = ptrdiff_t;
};

template <class _Ip>
struct incrementable_traits<const _Ip> : incrementable_traits<_Ip> {};

template <class _Tp>
concept __has_member_difference_type = requires { typename _Tp::difference_type; };

template <__has_member_difference_type _Tp>
struct incrementable_traits<_Tp> {
  using difference_type = typename _Tp::difference_type;
};

template <class _Tp>
concept __has_integral_minus = requires(const _Tp& __x, const _Tp& __y) {
  { __x - __y } -> integral;
};

template <__has_integral_minus _Tp>
  requires(!__has_member_difference_type<_Tp>)
struct incrementable_traits<_Tp> {
  using difference_type = make_signed_t<decltype(std::declval<_Tp>() - std::declval<_Tp>())>;
};

template <class>
struct iterator_traits;

// Let `RI` be `remove_cvref_t<I>`. The type `iter_difference_t<I>` denotes
// `incrementable_traits<RI>::difference_type` if `iterator_traits<RI>` names a specialization
// generated from the primary template, and `iterator_traits<RI>::difference_type` otherwise.
template <class _Ip>
using iter_difference_t =
    typename conditional_t<__is_primary_template<iterator_traits<remove_cvref_t<_Ip> > >::value,
                           incrementable_traits<remove_cvref_t<_Ip> >,
                           iterator_traits<remove_cvref_t<_Ip> > >::difference_type;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_INCREMENTABLE_TRAITS_H
