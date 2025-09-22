//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___COROUTINE_COROUTINE_TRAITS_H
#define _LIBCPP___COROUTINE_COROUTINE_TRAITS_H

#include <__config>
#include <__type_traits/void_t.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

// [coroutine.traits]
// [coroutine.traits.primary]
//   The header <coroutine> defined the primary template coroutine_traits such that
// if ArgTypes is a parameter pack of types and if the qualified-id R::promise_type
// is valid and denotes a type ([temp.deduct]), then coroutine_traits<R, ArgTypes...>
// has the following publicly accessible memebr:
//
//    using promise_type = typename R::promise_type;
//
// Otherwise, coroutine_traits<R, ArgTypes...> has no members.
template <class _Tp, class = void>
struct __coroutine_traits_sfinae {};

template <class _Tp>
struct __coroutine_traits_sfinae< _Tp, __void_t<typename _Tp::promise_type> > {
  using promise_type = typename _Tp::promise_type;
};

template <class _Ret, class... _Args>
struct coroutine_traits : public __coroutine_traits_sfinae<_Ret> {};

_LIBCPP_END_NAMESPACE_STD

#endif // __LIBCPP_STD_VER >= 20

#endif // _LIBCPP___COROUTINE_COROUTINE_TRAITS_H
