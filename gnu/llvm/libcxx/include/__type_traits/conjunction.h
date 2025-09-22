//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_CONJUNCTION_H
#define _LIBCPP___TYPE_TRAITS_CONJUNCTION_H

#include <__config>
#include <__type_traits/conditional.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_same.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class...>
using __expand_to_true = true_type;

template <class... _Pred>
__expand_to_true<__enable_if_t<_Pred::value>...> __and_helper(int);

template <class...>
false_type __and_helper(...);

// _And always performs lazy evaluation of its arguments.
//
// However, `_And<_Pred...>` itself will evaluate its result immediately (without having to
// be instantiated) since it is an alias, unlike `conjunction<_Pred...>`, which is a struct.
// If you want to defer the evaluation of `_And<_Pred...>` itself, use `_Lazy<_And, _Pred...>`.
template <class... _Pred>
using _And _LIBCPP_NODEBUG = decltype(std::__and_helper<_Pred...>(0));

template <bool... _Preds>
struct __all_dummy;

template <bool... _Pred>
struct __all : _IsSame<__all_dummy<_Pred...>, __all_dummy<((void)_Pred, true)...> > {};

#if _LIBCPP_STD_VER >= 17

template <class...>
struct conjunction : true_type {};

template <class _Arg>
struct conjunction<_Arg> : _Arg {};

template <class _Arg, class... _Args>
struct conjunction<_Arg, _Args...> : conditional_t<!bool(_Arg::value), _Arg, conjunction<_Args...>> {};

template <class... _Args>
inline constexpr bool conjunction_v = conjunction<_Args...>::value;

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_CONJUNCTION_H
