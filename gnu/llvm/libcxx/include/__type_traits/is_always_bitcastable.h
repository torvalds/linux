//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_ALWAYS_BITCASTABLE_H
#define _LIBCPP___TYPE_TRAITS_IS_ALWAYS_BITCASTABLE_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_object.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_trivially_copyable.h>
#include <__type_traits/remove_cv.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// Checks whether an object of type `From` can always be bit-cast to an object of type `To` and represent a valid value
// of type `To`. In other words, `From` and `To` have the same value representation and the set of values of `From` is
// a subset of the set of values of `To`.
//
// Note that types that cannot be assigned to each other using built-in assignment (e.g. arrays) might still be
// considered bit-castable.
template <class _From, class _To>
struct __is_always_bitcastable {
  using _UnqualFrom = __remove_cv_t<_From>;
  using _UnqualTo   = __remove_cv_t<_To>;

  // clang-format off
  static const bool value =
      // First, the simple case -- `From` and `To` are the same object type.
      (is_same<_UnqualFrom, _UnqualTo>::value && is_trivially_copyable<_UnqualFrom>::value) ||

      // Beyond the simple case, we say that one type is "always bit-castable" to another if:
      // - (1) `From` and `To` have the same value representation, and in addition every possible value of `From` has
      //   a corresponding value in the `To` type (in other words, the set of values of `To` is a superset of the set of
      //   values of `From`);
      // - (2) When the corresponding values are not the same value (as, for example, between an unsigned and a signed
      //   integer, where a large positive value of the unsigned integer corresponds to a negative value in the signed
      //   integer type), the value of `To` that results from a bitwise copy of `From` is the same what would be
      //   produced by the built-in assignment (if it were defined for the two types, to which there are minor
      //   exceptions, e.g. built-in arrays).
      //
      // In practice, that means:
      // - all integral types (except `bool`, see below) -- that is, character types and `int` types, both signed and
      //   unsigned...
      // - as well as arrays of such types...
      // - ...that have the same size.
      //
      // Other trivially-copyable types can't be validly bit-cast outside of their own type:
      // - floating-point types normally have different sizes and thus aren't bit-castable between each other (fails
      // #1);
      // - integral types and floating-point types use different representations, so for example bit-casting an integral
      //   `1` to `float` results in a very small less-than-one value, unlike built-in assignment that produces `1.0`
      //   (fails #2);
      // - booleans normally use only a single bit of their object representation; bit-casting an integer to a boolean
      //   will result in a boolean object with an incorrect representation, which is undefined behavior (fails #2).
      //   Bit-casting from a boolean into an integer, however, is valid;
      // - enumeration types may have different ranges of possible values (fails #1);
      // - for pointers, it is not guaranteed that pointers to different types use the same set of values to represent
      //   addresses, and the conversion results are explicitly unspecified for types with different alignments
      //   (fails #1);
      // - for structs and unions it is impossible to determine whether the set of values of one of them is a subset of
      //   the other (fails #1);
      // - there is no need to consider `nullptr_t` for practical purposes.
      (
        sizeof(_From) == sizeof(_To) &&
        is_integral<_From>::value &&
        is_integral<_To>::value &&
        !is_same<_UnqualTo, bool>::value
      );
  // clang-format on
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_ALWAYS_BITCASTABLE_H
