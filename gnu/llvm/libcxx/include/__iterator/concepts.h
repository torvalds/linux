// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_CONCEPTS_H
#define _LIBCPP___ITERATOR_CONCEPTS_H

#include <__concepts/arithmetic.h>
#include <__concepts/assignable.h>
#include <__concepts/common_reference_with.h>
#include <__concepts/constructible.h>
#include <__concepts/copyable.h>
#include <__concepts/derived_from.h>
#include <__concepts/equality_comparable.h>
#include <__concepts/invocable.h>
#include <__concepts/movable.h>
#include <__concepts/predicate.h>
#include <__concepts/regular.h>
#include <__concepts/relation.h>
#include <__concepts/same_as.h>
#include <__concepts/semiregular.h>
#include <__concepts/totally_ordered.h>
#include <__config>
#include <__functional/invoke.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iter_move.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/readable_traits.h>
#include <__memory/pointer_traits.h>
#include <__type_traits/add_pointer.h>
#include <__type_traits/common_reference.h>
#include <__type_traits/is_pointer.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [iterator.concept.readable]
template <class _In>
concept __indirectly_readable_impl =
    requires(const _In __i) {
      typename iter_value_t<_In>;
      typename iter_reference_t<_In>;
      typename iter_rvalue_reference_t<_In>;
      { *__i } -> same_as<iter_reference_t<_In>>;
      { ranges::iter_move(__i) } -> same_as<iter_rvalue_reference_t<_In>>;
    } && common_reference_with<iter_reference_t<_In>&&, iter_value_t<_In>&> &&
    common_reference_with<iter_reference_t<_In>&&, iter_rvalue_reference_t<_In>&&> &&
    common_reference_with<iter_rvalue_reference_t<_In>&&, const iter_value_t<_In>&>;

template <class _In>
concept indirectly_readable = __indirectly_readable_impl<remove_cvref_t<_In>>;

template <indirectly_readable _Tp>
using iter_common_reference_t = common_reference_t<iter_reference_t<_Tp>, iter_value_t<_Tp>&>;

// [iterator.concept.writable]
template <class _Out, class _Tp>
concept indirectly_writable = requires(_Out&& __o, _Tp&& __t) {
  *__o                                             = std::forward<_Tp>(__t); // not required to be equality-preserving
  *std::forward<_Out>(__o)                         = std::forward<_Tp>(__t); // not required to be equality-preserving
  const_cast<const iter_reference_t<_Out>&&>(*__o) = std::forward<_Tp>(__t); // not required to be equality-preserving
  const_cast<const iter_reference_t<_Out>&&>(*std::forward<_Out>(__o)) =
      std::forward<_Tp>(__t); // not required to be equality-preserving
};

// [iterator.concept.winc]
template <class _Tp>
concept __integer_like = integral<_Tp> && !same_as<_Tp, bool>;

template <class _Tp>
concept __signed_integer_like = signed_integral<_Tp>;

template <class _Ip>
concept weakly_incrementable =
    // TODO: remove this once the clang bug is fixed (bugs.llvm.org/PR48173).
    !same_as<_Ip, bool> && // Currently, clang does not handle bool correctly.
    movable<_Ip> && requires(_Ip __i) {
      typename iter_difference_t<_Ip>;
      requires __signed_integer_like<iter_difference_t<_Ip>>;
      { ++__i } -> same_as<_Ip&>; // not required to be equality-preserving
      __i++;                      // not required to be equality-preserving
    };

// [iterator.concept.inc]
template <class _Ip>
concept incrementable = regular<_Ip> && weakly_incrementable<_Ip> && requires(_Ip __i) {
  { __i++ } -> same_as<_Ip>;
};

// [iterator.concept.iterator]
template <class _Ip>
concept input_or_output_iterator = requires(_Ip __i) {
  { *__i } -> __can_reference;
} && weakly_incrementable<_Ip>;

// [iterator.concept.sentinel]
template <class _Sp, class _Ip>
concept sentinel_for = semiregular<_Sp> && input_or_output_iterator<_Ip> && __weakly_equality_comparable_with<_Sp, _Ip>;

template <class, class>
inline constexpr bool disable_sized_sentinel_for = false;

template <class _Sp, class _Ip>
concept sized_sentinel_for =
    sentinel_for<_Sp, _Ip> && !disable_sized_sentinel_for<remove_cv_t<_Sp>, remove_cv_t<_Ip>> &&
    requires(const _Ip& __i, const _Sp& __s) {
      { __s - __i } -> same_as<iter_difference_t<_Ip>>;
      { __i - __s } -> same_as<iter_difference_t<_Ip>>;
    };

// [iterator.concept.input]
template <class _Ip>
concept input_iterator = input_or_output_iterator<_Ip> && indirectly_readable<_Ip> && requires {
  typename _ITER_CONCEPT<_Ip>;
} && derived_from<_ITER_CONCEPT<_Ip>, input_iterator_tag>;

// [iterator.concept.output]
template <class _Ip, class _Tp>
concept output_iterator =
    input_or_output_iterator<_Ip> && indirectly_writable<_Ip, _Tp> && requires(_Ip __it, _Tp&& __t) {
      *__it++ = std::forward<_Tp>(__t); // not required to be equality-preserving
    };

// [iterator.concept.forward]
template <class _Ip>
concept forward_iterator =
    input_iterator<_Ip> && derived_from<_ITER_CONCEPT<_Ip>, forward_iterator_tag> && incrementable<_Ip> &&
    sentinel_for<_Ip, _Ip>;

// [iterator.concept.bidir]
template <class _Ip>
concept bidirectional_iterator =
    forward_iterator<_Ip> && derived_from<_ITER_CONCEPT<_Ip>, bidirectional_iterator_tag> && requires(_Ip __i) {
      { --__i } -> same_as<_Ip&>;
      { __i-- } -> same_as<_Ip>;
    };

template <class _Ip>
concept random_access_iterator =
    bidirectional_iterator<_Ip> && derived_from<_ITER_CONCEPT<_Ip>, random_access_iterator_tag> &&
    totally_ordered<_Ip> && sized_sentinel_for<_Ip, _Ip> &&
    requires(_Ip __i, const _Ip __j, const iter_difference_t<_Ip> __n) {
      { __i += __n } -> same_as<_Ip&>;
      { __j + __n } -> same_as<_Ip>;
      { __n + __j } -> same_as<_Ip>;
      { __i -= __n } -> same_as<_Ip&>;
      { __j - __n } -> same_as<_Ip>;
      { __j[__n] } -> same_as<iter_reference_t<_Ip>>;
    };

template <class _Ip>
concept contiguous_iterator =
    random_access_iterator<_Ip> && derived_from<_ITER_CONCEPT<_Ip>, contiguous_iterator_tag> &&
    is_lvalue_reference_v<iter_reference_t<_Ip>> && same_as<iter_value_t<_Ip>, remove_cvref_t<iter_reference_t<_Ip>>> &&
    requires(const _Ip& __i) {
      { std::to_address(__i) } -> same_as<add_pointer_t<iter_reference_t<_Ip>>>;
    };

template <class _Ip>
concept __has_arrow = input_iterator<_Ip> && (is_pointer_v<_Ip> || requires(_Ip __i) { __i.operator->(); });

// [indirectcallable.indirectinvocable]
template <class _Fp, class _It>
concept indirectly_unary_invocable =
    indirectly_readable<_It> && copy_constructible<_Fp> && invocable<_Fp&, iter_value_t<_It>&> &&
    invocable<_Fp&, iter_reference_t<_It>> &&
    common_reference_with< invoke_result_t<_Fp&, iter_value_t<_It>&>, invoke_result_t<_Fp&, iter_reference_t<_It>>>;

template <class _Fp, class _It>
concept indirectly_regular_unary_invocable =
    indirectly_readable<_It> && copy_constructible<_Fp> && regular_invocable<_Fp&, iter_value_t<_It>&> &&
    regular_invocable<_Fp&, iter_reference_t<_It>> &&
    common_reference_with< invoke_result_t<_Fp&, iter_value_t<_It>&>, invoke_result_t<_Fp&, iter_reference_t<_It>>>;

template <class _Fp, class _It>
concept indirect_unary_predicate =
    indirectly_readable<_It> && copy_constructible<_Fp> && predicate<_Fp&, iter_value_t<_It>&> &&
    predicate<_Fp&, iter_reference_t<_It>>;

template <class _Fp, class _It1, class _It2>
concept indirect_binary_predicate =
    indirectly_readable<_It1> && indirectly_readable<_It2> && copy_constructible<_Fp> &&
    predicate<_Fp&, iter_value_t<_It1>&, iter_value_t<_It2>&> &&
    predicate<_Fp&, iter_value_t<_It1>&, iter_reference_t<_It2>> &&
    predicate<_Fp&, iter_reference_t<_It1>, iter_value_t<_It2>&> &&
    predicate<_Fp&, iter_reference_t<_It1>, iter_reference_t<_It2>>;

template <class _Fp, class _It1, class _It2 = _It1>
concept indirect_equivalence_relation =
    indirectly_readable<_It1> && indirectly_readable<_It2> && copy_constructible<_Fp> &&
    equivalence_relation<_Fp&, iter_value_t<_It1>&, iter_value_t<_It2>&> &&
    equivalence_relation<_Fp&, iter_value_t<_It1>&, iter_reference_t<_It2>> &&
    equivalence_relation<_Fp&, iter_reference_t<_It1>, iter_value_t<_It2>&> &&
    equivalence_relation<_Fp&, iter_reference_t<_It1>, iter_reference_t<_It2>>;

template <class _Fp, class _It1, class _It2 = _It1>
concept indirect_strict_weak_order =
    indirectly_readable<_It1> && indirectly_readable<_It2> && copy_constructible<_Fp> &&
    strict_weak_order<_Fp&, iter_value_t<_It1>&, iter_value_t<_It2>&> &&
    strict_weak_order<_Fp&, iter_value_t<_It1>&, iter_reference_t<_It2>> &&
    strict_weak_order<_Fp&, iter_reference_t<_It1>, iter_value_t<_It2>&> &&
    strict_weak_order<_Fp&, iter_reference_t<_It1>, iter_reference_t<_It2>>;

template <class _Fp, class... _Its>
  requires(indirectly_readable<_Its> && ...) && invocable<_Fp, iter_reference_t<_Its>...>
using indirect_result_t = invoke_result_t<_Fp, iter_reference_t<_Its>...>;

template <class _In, class _Out>
concept indirectly_movable = indirectly_readable<_In> && indirectly_writable<_Out, iter_rvalue_reference_t<_In>>;

template <class _In, class _Out>
concept indirectly_movable_storable =
    indirectly_movable<_In, _Out> && indirectly_writable<_Out, iter_value_t<_In>> && movable<iter_value_t<_In>> &&
    constructible_from<iter_value_t<_In>, iter_rvalue_reference_t<_In>> &&
    assignable_from<iter_value_t<_In>&, iter_rvalue_reference_t<_In>>;

template <class _In, class _Out>
concept indirectly_copyable = indirectly_readable<_In> && indirectly_writable<_Out, iter_reference_t<_In>>;

template <class _In, class _Out>
concept indirectly_copyable_storable =
    indirectly_copyable<_In, _Out> && indirectly_writable<_Out, iter_value_t<_In>&> &&
    indirectly_writable<_Out, const iter_value_t<_In>&> && indirectly_writable<_Out, iter_value_t<_In>&&> &&
    indirectly_writable<_Out, const iter_value_t<_In>&&> && copyable<iter_value_t<_In>> &&
    constructible_from<iter_value_t<_In>, iter_reference_t<_In>> &&
    assignable_from<iter_value_t<_In>&, iter_reference_t<_In>>;

// Note: indirectly_swappable is located in iter_swap.h to prevent a dependency cycle
// (both iter_swap and indirectly_swappable require indirectly_readable).

#endif // _LIBCPP_STD_VER >= 20

template <class _Tp>
using __has_random_access_iterator_category_or_concept
#if _LIBCPP_STD_VER >= 20
    = integral_constant<bool, random_access_iterator<_Tp>>;
#else  // _LIBCPP_STD_VER < 20
    = __has_random_access_iterator_category<_Tp>;
#endif // _LIBCPP_STD_VER

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_CONCEPTS_H
