//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_EQUALITY_COMPARABLE_H
#define _LIBCPP___TYPE_TRAITS_IS_EQUALITY_COMPARABLE_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_signed.h>
#include <__type_traits/is_void.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, class _Up, class = void>
struct __is_equality_comparable : false_type {};

template <class _Tp, class _Up>
struct __is_equality_comparable<_Tp, _Up, __void_t<decltype(std::declval<_Tp>() == std::declval<_Up>())> > : true_type {
};

// A type is_trivially_equality_comparable if the expression `a == b` is equivalent to `std::memcmp(&a, &b, sizeof(T))`
// (with `a` and `b` being of type `T`). For the case where we compare two object of the same type, we can use
// __is_trivially_equality_comparable. We have special-casing for pointers which point to the same type ignoring
// cv-qualifications and comparing to void-pointers.
//
// The following types are not trivially equality comparable:
// floating-point types: different bit-patterns can compare equal. (e.g 0.0 and -0.0)
// enums: The user is allowed to specialize operator== for enums
// pointers that don't have the same type (ignoring cv-qualifiers): pointers to virtual bases are equality comparable,
//   but don't have the same bit-pattern. An exception to this is comparing to a void-pointer. There the bit-pattern is
//   always compared.
// objects with padding bytes: since objects with padding bytes may compare equal, even though their object
//   representation may not be equivalent.

template <class _Tp, class _Up, class = void>
struct __libcpp_is_trivially_equality_comparable_impl : false_type {};

template <class _Tp>
struct __libcpp_is_trivially_equality_comparable_impl<_Tp, _Tp>
#if __has_builtin(__is_trivially_equality_comparable)
    : integral_constant<bool, __is_trivially_equality_comparable(_Tp) && __is_equality_comparable<_Tp, _Tp>::value> {
};
#else
    : is_integral<_Tp> {
};
#endif // __has_builtin(__is_trivially_equality_comparable)

template <class _Tp, class _Up>
struct __libcpp_is_trivially_equality_comparable_impl<
    _Tp,
    _Up,
    __enable_if_t<is_integral<_Tp>::value && is_integral<_Up>::value && !is_same<_Tp, _Up>::value &&
                  is_signed<_Tp>::value == is_signed<_Up>::value && sizeof(_Tp) == sizeof(_Up)> > : true_type {};

template <class _Tp>
struct __libcpp_is_trivially_equality_comparable_impl<_Tp*, _Tp*> : true_type {};

// TODO: Use is_pointer_inverconvertible_base_of
template <class _Tp, class _Up>
struct __libcpp_is_trivially_equality_comparable_impl<_Tp*, _Up*>
    : integral_constant<
          bool,
          __is_equality_comparable<_Tp*, _Up*>::value &&
              (is_same<__remove_cv_t<_Tp>, __remove_cv_t<_Up> >::value || is_void<_Tp>::value || is_void<_Up>::value)> {
};

template <class _Tp, class _Up>
using __libcpp_is_trivially_equality_comparable =
    __libcpp_is_trivially_equality_comparable_impl<__remove_cv_t<_Tp>, __remove_cv_t<_Up> >;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_EQUALITY_COMPARABLE_H
