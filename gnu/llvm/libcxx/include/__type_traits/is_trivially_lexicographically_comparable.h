//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_TRIVIALLY_LEXICOGRAPHICALLY_COMPARABLE_H
#define _LIBCPP___TYPE_TRAITS_IS_TRIVIALLY_LEXICOGRAPHICALLY_COMPARABLE_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_unsigned.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// A type is_trivially_lexicographically_comparable if the expression `a <=> b` (or their pre-C++20 equivalents) is
// equivalent to `std::memcmp(&a, &b, sizeof(T))` (with `a` and `b` being of type `T`). There is currently no builtin to
// tell us whether that's the case for arbitrary types, so we can only do this for known types. Specifically, these are
// currently unsigned integer types with a sizeof(T) == 1.
//
// bool is trivially lexicographically comparable, because e.g. false <=> true is valid code. Furthermore, the standard
// says that [basic.fundamental] "Type bool is a distinct type that has the same object representation, value
// representation, and alignment requirements as an implementation-defined unsigned integer type. The values of type
// bool are true and false."
// This means that bool has to be unsigned and has exactly two values. This means that having anything other than the
// `true` or `false` value representations in a bool is UB.
//
// The following types are not trivially lexicographically comparable:
// signed integer types: `char(-1) < char(1)`, but memcmp compares `unsigned char`s
// unsigned integer types with sizeof(T) > 1: depending on the endianness, the LSB might be the first byte to be
//                                            compared. This means that when comparing unsigned(129) and unsigned(2)
//                                            using memcmp(), the result would be that 2 > 129.
//                                            TODO: Do we want to enable this on big-endian systems?

template <class _Tp, class _Up>
struct __libcpp_is_trivially_lexicographically_comparable
    : integral_constant<bool,
                        is_same<__remove_cv_t<_Tp>, __remove_cv_t<_Up> >::value && sizeof(_Tp) == 1 &&
                            is_unsigned<_Tp>::value> {};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_TRIVIALLY_LEXICOGRAPHICALLY_COMPARABLE_H
