//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_NOTHROW_CONVERTIBLE_H
#define _LIBCPP___TYPE_TRAITS_IS_NOTHROW_CONVERTIBLE_H

#include <__config>
#include <__type_traits/conjunction.h>
#include <__type_traits/disjunction.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_void.h>
#include <__type_traits/lazy.h>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

#  if __has_builtin(__is_nothrow_convertible)

template <class _Tp, class _Up>
struct is_nothrow_convertible : bool_constant<__is_nothrow_convertible(_Tp, _Up)> {};

template <class _Tp, class _Up>
inline constexpr bool is_nothrow_convertible_v = __is_nothrow_convertible(_Tp, _Up);

#  else // __has_builtin(__is_nothrow_convertible)

template <typename _Tp>
void __test_noexcept(_Tp) noexcept;

template <typename _Fm, typename _To>
bool_constant<noexcept(std::__test_noexcept<_To>(std::declval<_Fm>()))> __is_nothrow_convertible_test();

template <typename _Fm, typename _To>
struct __is_nothrow_convertible_helper : decltype(__is_nothrow_convertible_test<_Fm, _To>()) {};

template <typename _Fm, typename _To>
struct is_nothrow_convertible
    : _Or<_And<is_void<_To>, is_void<_Fm>>,
          _Lazy<_And, is_convertible<_Fm, _To>, __is_nothrow_convertible_helper<_Fm, _To> > >::type {};

template <typename _Fm, typename _To>
inline constexpr bool is_nothrow_convertible_v = is_nothrow_convertible<_Fm, _To>::value;

#  endif // __has_builtin(__is_nothrow_convertible)

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_NOTHROW_CONVERTIBLE_H
