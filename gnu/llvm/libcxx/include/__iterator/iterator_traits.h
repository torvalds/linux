// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_ITERATOR_TRAITS_H
#define _LIBCPP___ITERATOR_ITERATOR_TRAITS_H

#include <__concepts/arithmetic.h>
#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__concepts/copyable.h>
#include <__concepts/equality_comparable.h>
#include <__concepts/same_as.h>
#include <__concepts/totally_ordered.h>
#include <__config>
#include <__fwd/pair.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/readable_traits.h>
#include <__type_traits/common_reference.h>
#include <__type_traits/conditional.h>
#include <__type_traits/disjunction.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_object.h>
#include <__type_traits/is_primary_template.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/is_valid_expansion.h>
#include <__type_traits/remove_const.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/remove_cvref.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _Tp>
using __with_reference = _Tp&;

template <class _Tp>
concept __can_reference = requires { typename __with_reference<_Tp>; };

template <class _Tp>
concept __dereferenceable = requires(_Tp& __t) {
  { *__t } -> __can_reference; // not required to be equality-preserving
};

// [iterator.traits]
template <__dereferenceable _Tp>
using iter_reference_t = decltype(*std::declval<_Tp&>());

#endif // _LIBCPP_STD_VER >= 20

template <class _Iter>
struct _LIBCPP_TEMPLATE_VIS iterator_traits;

struct _LIBCPP_TEMPLATE_VIS input_iterator_tag {};
struct _LIBCPP_TEMPLATE_VIS output_iterator_tag {};
struct _LIBCPP_TEMPLATE_VIS forward_iterator_tag : public input_iterator_tag {};
struct _LIBCPP_TEMPLATE_VIS bidirectional_iterator_tag : public forward_iterator_tag {};
struct _LIBCPP_TEMPLATE_VIS random_access_iterator_tag : public bidirectional_iterator_tag {};
#if _LIBCPP_STD_VER >= 20
struct _LIBCPP_TEMPLATE_VIS contiguous_iterator_tag : public random_access_iterator_tag {};
#endif

template <class _Iter>
struct __iter_traits_cache {
  using type = _If< __is_primary_template<iterator_traits<_Iter> >::value, _Iter, iterator_traits<_Iter> >;
};
template <class _Iter>
using _ITER_TRAITS = typename __iter_traits_cache<_Iter>::type;

struct __iter_concept_concept_test {
  template <class _Iter>
  using _Apply = typename _ITER_TRAITS<_Iter>::iterator_concept;
};
struct __iter_concept_category_test {
  template <class _Iter>
  using _Apply = typename _ITER_TRAITS<_Iter>::iterator_category;
};
struct __iter_concept_random_fallback {
  template <class _Iter>
  using _Apply = __enable_if_t< __is_primary_template<iterator_traits<_Iter> >::value, random_access_iterator_tag >;
};

template <class _Iter, class _Tester>
struct __test_iter_concept : _IsValidExpansion<_Tester::template _Apply, _Iter>, _Tester {};

template <class _Iter>
struct __iter_concept_cache {
  using type = _Or< __test_iter_concept<_Iter, __iter_concept_concept_test>,
                    __test_iter_concept<_Iter, __iter_concept_category_test>,
                    __test_iter_concept<_Iter, __iter_concept_random_fallback> >;
};

template <class _Iter>
using _ITER_CONCEPT = typename __iter_concept_cache<_Iter>::type::template _Apply<_Iter>;

template <class _Tp>
struct __has_iterator_typedefs {
private:
  template <class _Up>
  static false_type __test(...);
  template <class _Up>
  static true_type
  __test(__void_t<typename _Up::iterator_category>* = nullptr,
         __void_t<typename _Up::difference_type>*   = nullptr,
         __void_t<typename _Up::value_type>*        = nullptr,
         __void_t<typename _Up::reference>*         = nullptr,
         __void_t<typename _Up::pointer>*           = nullptr);

public:
  static const bool value = decltype(__test<_Tp>(nullptr, nullptr, nullptr, nullptr, nullptr))::value;
};

template <class _Tp>
struct __has_iterator_category {
private:
  template <class _Up>
  static false_type __test(...);
  template <class _Up>
  static true_type __test(typename _Up::iterator_category* = nullptr);

public:
  static const bool value = decltype(__test<_Tp>(nullptr))::value;
};

template <class _Tp>
struct __has_iterator_concept {
private:
  template <class _Up>
  static false_type __test(...);
  template <class _Up>
  static true_type __test(typename _Up::iterator_concept* = nullptr);

public:
  static const bool value = decltype(__test<_Tp>(nullptr))::value;
};

#if _LIBCPP_STD_VER >= 20

// The `cpp17-*-iterator` exposition-only concepts have very similar names to the `Cpp17*Iterator` named requirements
// from `[iterator.cpp17]`. To avoid confusion between the two, the exposition-only concepts have been banished to
// a "detail" namespace indicating they have a niche use-case.
namespace __iterator_traits_detail {
template <class _Ip>
concept __cpp17_iterator = requires(_Ip __i) {
  { *__i } -> __can_reference;
  { ++__i } -> same_as<_Ip&>;
  { *__i++ } -> __can_reference;
} && copyable<_Ip>;

template <class _Ip>
concept __cpp17_input_iterator = __cpp17_iterator<_Ip> && equality_comparable<_Ip> && requires(_Ip __i) {
  typename incrementable_traits<_Ip>::difference_type;
  typename indirectly_readable_traits<_Ip>::value_type;
  typename common_reference_t<iter_reference_t<_Ip>&&, typename indirectly_readable_traits<_Ip>::value_type&>;
  typename common_reference_t<decltype(*__i++)&&, typename indirectly_readable_traits<_Ip>::value_type&>;
  requires signed_integral<typename incrementable_traits<_Ip>::difference_type>;
};

template <class _Ip>
concept __cpp17_forward_iterator =
    __cpp17_input_iterator<_Ip> && constructible_from<_Ip> && is_reference_v<iter_reference_t<_Ip>> &&
    same_as<remove_cvref_t<iter_reference_t<_Ip>>, typename indirectly_readable_traits<_Ip>::value_type> &&
    requires(_Ip __i) {
      { __i++ } -> convertible_to<_Ip const&>;
      { *__i++ } -> same_as<iter_reference_t<_Ip>>;
    };

template <class _Ip>
concept __cpp17_bidirectional_iterator = __cpp17_forward_iterator<_Ip> && requires(_Ip __i) {
  { --__i } -> same_as<_Ip&>;
  { __i-- } -> convertible_to<_Ip const&>;
  { *__i-- } -> same_as<iter_reference_t<_Ip>>;
};

template <class _Ip>
concept __cpp17_random_access_iterator =
    __cpp17_bidirectional_iterator<_Ip> && totally_ordered<_Ip> &&
    requires(_Ip __i, typename incrementable_traits<_Ip>::difference_type __n) {
      { __i += __n } -> same_as<_Ip&>;
      { __i -= __n } -> same_as<_Ip&>;
      { __i + __n } -> same_as<_Ip>;
      { __n + __i } -> same_as<_Ip>;
      { __i - __n } -> same_as<_Ip>;
      { __i - __i } -> same_as<decltype(__n)>; // NOLINT(misc-redundant-expression) ; This is llvm.org/PR54114
      { __i[__n] } -> convertible_to<iter_reference_t<_Ip>>;
    };
} // namespace __iterator_traits_detail

template <class _Ip>
concept __has_member_reference = requires { typename _Ip::reference; };

template <class _Ip>
concept __has_member_pointer = requires { typename _Ip::pointer; };

template <class _Ip>
concept __has_member_iterator_category = requires { typename _Ip::iterator_category; };

template <class _Ip>
concept __specifies_members = requires {
  typename _Ip::value_type;
  typename _Ip::difference_type;
  requires __has_member_reference<_Ip>;
  requires __has_member_iterator_category<_Ip>;
};

template <class>
struct __iterator_traits_member_pointer_or_void {
  using type = void;
};

template <__has_member_pointer _Tp>
struct __iterator_traits_member_pointer_or_void<_Tp> {
  using type = typename _Tp::pointer;
};

template <class _Tp>
concept __cpp17_iterator_missing_members = !__specifies_members<_Tp> && __iterator_traits_detail::__cpp17_iterator<_Tp>;

template <class _Tp>
concept __cpp17_input_iterator_missing_members =
    __cpp17_iterator_missing_members<_Tp> && __iterator_traits_detail::__cpp17_input_iterator<_Tp>;

// Otherwise, `pointer` names `void`.
template <class>
struct __iterator_traits_member_pointer_or_arrow_or_void {
  using type = void;
};

// [iterator.traits]/3.2.1
// If the qualified-id `I::pointer` is valid and denotes a type, `pointer` names that type.
template <__has_member_pointer _Ip>
struct __iterator_traits_member_pointer_or_arrow_or_void<_Ip> {
  using type = typename _Ip::pointer;
};

// Otherwise, if `decltype(declval<I&>().operator->())` is well-formed, then `pointer` names that
// type.
template <class _Ip>
  requires requires(_Ip& __i) { __i.operator->(); } && (!__has_member_pointer<_Ip>)
struct __iterator_traits_member_pointer_or_arrow_or_void<_Ip> {
  using type = decltype(std::declval<_Ip&>().operator->());
};

// Otherwise, `reference` names `iter-reference-t<I>`.
template <class _Ip>
struct __iterator_traits_member_reference {
  using type = iter_reference_t<_Ip>;
};

// [iterator.traits]/3.2.2
// If the qualified-id `I::reference` is valid and denotes a type, `reference` names that type.
template <__has_member_reference _Ip>
struct __iterator_traits_member_reference<_Ip> {
  using type = typename _Ip::reference;
};

// [iterator.traits]/3.2.3.4
// input_iterator_tag
template <class _Ip>
struct __deduce_iterator_category {
  using type = input_iterator_tag;
};

// [iterator.traits]/3.2.3.1
// `random_access_iterator_tag` if `I` satisfies `cpp17-random-access-iterator`, or otherwise
template <__iterator_traits_detail::__cpp17_random_access_iterator _Ip>
struct __deduce_iterator_category<_Ip> {
  using type = random_access_iterator_tag;
};

// [iterator.traits]/3.2.3.2
// `bidirectional_iterator_tag` if `I` satisfies `cpp17-bidirectional-iterator`, or otherwise
template <__iterator_traits_detail::__cpp17_bidirectional_iterator _Ip>
struct __deduce_iterator_category<_Ip> {
  using type = bidirectional_iterator_tag;
};

// [iterator.traits]/3.2.3.3
// `forward_iterator_tag` if `I` satisfies `cpp17-forward-iterator`, or otherwise
template <__iterator_traits_detail::__cpp17_forward_iterator _Ip>
struct __deduce_iterator_category<_Ip> {
  using type = forward_iterator_tag;
};

template <class _Ip>
struct __iterator_traits_iterator_category : __deduce_iterator_category<_Ip> {};

// [iterator.traits]/3.2.3
// If the qualified-id `I::iterator-category` is valid and denotes a type, `iterator-category` names
// that type.
template <__has_member_iterator_category _Ip>
struct __iterator_traits_iterator_category<_Ip> {
  using type = typename _Ip::iterator_category;
};

// otherwise, it names void.
template <class>
struct __iterator_traits_difference_type {
  using type = void;
};

// If the qualified-id `incrementable_traits<I>::difference_type` is valid and denotes a type, then
// `difference_type` names that type;
template <class _Ip>
  requires requires { typename incrementable_traits<_Ip>::difference_type; }
struct __iterator_traits_difference_type<_Ip> {
  using type = typename incrementable_traits<_Ip>::difference_type;
};

// [iterator.traits]/3.4
// Otherwise, `iterator_traits<I>` has no members by any of the above names.
template <class>
struct __iterator_traits {};

// [iterator.traits]/3.1
// If `I` has valid ([temp.deduct]) member types `difference-type`, `value-type`, `reference`, and
// `iterator-category`, then `iterator-traits<I>` has the following publicly accessible members:
template <__specifies_members _Ip>
struct __iterator_traits<_Ip> {
  using iterator_category = typename _Ip::iterator_category;
  using value_type        = typename _Ip::value_type;
  using difference_type   = typename _Ip::difference_type;
  using pointer           = typename __iterator_traits_member_pointer_or_void<_Ip>::type;
  using reference         = typename _Ip::reference;
};

// [iterator.traits]/3.2
// Otherwise, if `I` satisfies the exposition-only concept `cpp17-input-iterator`,
// `iterator-traits<I>` has the following publicly accessible members:
template <__cpp17_input_iterator_missing_members _Ip>
struct __iterator_traits<_Ip> {
  using iterator_category = typename __iterator_traits_iterator_category<_Ip>::type;
  using value_type        = typename indirectly_readable_traits<_Ip>::value_type;
  using difference_type   = typename incrementable_traits<_Ip>::difference_type;
  using pointer           = typename __iterator_traits_member_pointer_or_arrow_or_void<_Ip>::type;
  using reference         = typename __iterator_traits_member_reference<_Ip>::type;
};

// Otherwise, if `I` satisfies the exposition-only concept `cpp17-iterator`, then
// `iterator_traits<I>` has the following publicly accessible members:
template <__cpp17_iterator_missing_members _Ip>
struct __iterator_traits<_Ip> {
  using iterator_category = output_iterator_tag;
  using value_type        = void;
  using difference_type   = typename __iterator_traits_difference_type<_Ip>::type;
  using pointer           = void;
  using reference         = void;
};

template <class _Ip>
struct iterator_traits : __iterator_traits<_Ip> {
  using __primary_template = iterator_traits;
};

#else  // _LIBCPP_STD_VER >= 20

template <class _Iter, bool>
struct __iterator_traits {};

template <class _Iter, bool>
struct __iterator_traits_impl {};

template <class _Iter>
struct __iterator_traits_impl<_Iter, true> {
  typedef typename _Iter::difference_type difference_type;
  typedef typename _Iter::value_type value_type;
  typedef typename _Iter::pointer pointer;
  typedef typename _Iter::reference reference;
  typedef typename _Iter::iterator_category iterator_category;
};

template <class _Iter>
struct __iterator_traits<_Iter, true>
    : __iterator_traits_impl< _Iter,
                              is_convertible<typename _Iter::iterator_category, input_iterator_tag>::value ||
                                  is_convertible<typename _Iter::iterator_category, output_iterator_tag>::value > {};

// iterator_traits<Iterator> will only have the nested types if Iterator::iterator_category
//    exists.  Else iterator_traits<Iterator> will be an empty class.  This is a
//    conforming extension which allows some programs to compile and behave as
//    the client expects instead of failing at compile time.

template <class _Iter>
struct _LIBCPP_TEMPLATE_VIS iterator_traits : __iterator_traits<_Iter, __has_iterator_typedefs<_Iter>::value> {
  using __primary_template = iterator_traits;
};
#endif // _LIBCPP_STD_VER >= 20

template <class _Tp>
#if _LIBCPP_STD_VER >= 20
  requires is_object_v<_Tp>
#endif
struct _LIBCPP_TEMPLATE_VIS iterator_traits<_Tp*> {
  typedef ptrdiff_t difference_type;
  typedef __remove_cv_t<_Tp> value_type;
  typedef _Tp* pointer;
  typedef _Tp& reference;
  typedef random_access_iterator_tag iterator_category;
#if _LIBCPP_STD_VER >= 20
  typedef contiguous_iterator_tag iterator_concept;
#endif
};

template <class _Tp, class _Up, bool = __has_iterator_category<iterator_traits<_Tp> >::value>
struct __has_iterator_category_convertible_to : is_convertible<typename iterator_traits<_Tp>::iterator_category, _Up> {
};

template <class _Tp, class _Up>
struct __has_iterator_category_convertible_to<_Tp, _Up, false> : false_type {};

template <class _Tp, class _Up, bool = __has_iterator_concept<_Tp>::value>
struct __has_iterator_concept_convertible_to : is_convertible<typename _Tp::iterator_concept, _Up> {};

template <class _Tp, class _Up>
struct __has_iterator_concept_convertible_to<_Tp, _Up, false> : false_type {};

template <class _Tp>
using __has_input_iterator_category = __has_iterator_category_convertible_to<_Tp, input_iterator_tag>;

template <class _Tp>
using __has_forward_iterator_category = __has_iterator_category_convertible_to<_Tp, forward_iterator_tag>;

template <class _Tp>
using __has_bidirectional_iterator_category = __has_iterator_category_convertible_to<_Tp, bidirectional_iterator_tag>;

template <class _Tp>
using __has_random_access_iterator_category = __has_iterator_category_convertible_to<_Tp, random_access_iterator_tag>;

// __libcpp_is_contiguous_iterator determines if an iterator is known by
// libc++ to be contiguous, either because it advertises itself as such
// (in C++20) or because it is a pointer type or a known trivial wrapper
// around a (possibly fancy) pointer type, such as __wrap_iter<T*>.
// Such iterators receive special "contiguous" optimizations in
// std::copy and std::sort.
//
#if _LIBCPP_STD_VER >= 20
template <class _Tp>
struct __libcpp_is_contiguous_iterator
    : _Or< __has_iterator_category_convertible_to<_Tp, contiguous_iterator_tag>,
           __has_iterator_concept_convertible_to<_Tp, contiguous_iterator_tag> > {};
#else
template <class _Tp>
struct __libcpp_is_contiguous_iterator : false_type {};
#endif

// Any native pointer which is an iterator is also a contiguous iterator.
template <class _Up>
struct __libcpp_is_contiguous_iterator<_Up*> : true_type {};

template <class _Iter>
class __wrap_iter;

template <class _Tp>
using __has_exactly_input_iterator_category =
    integral_constant<bool,
                      __has_iterator_category_convertible_to<_Tp, input_iterator_tag>::value &&
                          !__has_iterator_category_convertible_to<_Tp, forward_iterator_tag>::value>;

template <class _Tp>
using __has_exactly_forward_iterator_category =
    integral_constant<bool,
                      __has_iterator_category_convertible_to<_Tp, forward_iterator_tag>::value &&
                          !__has_iterator_category_convertible_to<_Tp, bidirectional_iterator_tag>::value>;

template <class _Tp>
using __has_exactly_bidirectional_iterator_category =
    integral_constant<bool,
                      __has_iterator_category_convertible_to<_Tp, bidirectional_iterator_tag>::value &&
                          !__has_iterator_category_convertible_to<_Tp, random_access_iterator_tag>::value>;

template <class _InputIterator>
using __iter_value_type = typename iterator_traits<_InputIterator>::value_type;

template <class _InputIterator>
using __iter_key_type = __remove_const_t<typename iterator_traits<_InputIterator>::value_type::first_type>;

template <class _InputIterator>
using __iter_mapped_type = typename iterator_traits<_InputIterator>::value_type::second_type;

template <class _InputIterator>
using __iter_to_alloc_type =
    pair<const typename iterator_traits<_InputIterator>::value_type::first_type,
         typename iterator_traits<_InputIterator>::value_type::second_type>;

template <class _Iter>
using __iterator_category_type = typename iterator_traits<_Iter>::iterator_category;

template <class _Iter>
using __iterator_pointer_type = typename iterator_traits<_Iter>::pointer;

template <class _Iter>
using __iter_diff_t = typename iterator_traits<_Iter>::difference_type;

template <class _Iter>
using __iter_reference = typename iterator_traits<_Iter>::reference;

#if _LIBCPP_STD_VER >= 20

// [readable.traits]

// Let `RI` be `remove_cvref_t<I>`. The type `iter_value_t<I>` denotes
// `indirectly_readable_traits<RI>::value_type` if `iterator_traits<RI>` names a specialization
// generated from the primary template, and `iterator_traits<RI>::value_type` otherwise.
// This has to be in this file and not readable_traits.h to break the include cycle between the two.
template <class _Ip>
using iter_value_t =
    typename conditional_t<__is_primary_template<iterator_traits<remove_cvref_t<_Ip> > >::value,
                           indirectly_readable_traits<remove_cvref_t<_Ip> >,
                           iterator_traits<remove_cvref_t<_Ip> > >::value_type;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_ITERATOR_TRAITS_H
