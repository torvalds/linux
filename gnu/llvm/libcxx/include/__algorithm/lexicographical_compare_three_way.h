//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_LEXICOGRAPHICAL_COMPARE_THREE_WAY_H
#define _LIBCPP___ALGORITHM_LEXICOGRAPHICAL_COMPARE_THREE_WAY_H

#include <__algorithm/min.h>
#include <__algorithm/three_way_comp_ref_type.h>
#include <__compare/compare_three_way.h>
#include <__compare/ordering.h>
#include <__concepts/arithmetic.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__type_traits/common_type.h>
#include <__type_traits/is_constructible.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// Fast path for random access iterators which computes the number of loop iterations up-front and
// then skips the iterator comparisons inside the loop.
template <class _InputIterator1, class _InputIterator2, class _Cmp>
_LIBCPP_HIDE_FROM_ABI constexpr auto __lexicographical_compare_three_way_fast_path(
    _InputIterator1 __first1, _InputIterator1 __last1, _InputIterator2 __first2, _InputIterator2 __last2, _Cmp& __comp)
    -> decltype(__comp(*__first1, *__first2)) {
  static_assert(
      signed_integral<__iter_diff_t<_InputIterator1>>, "Using a non-integral difference_type is undefined behavior.");
  static_assert(
      signed_integral<__iter_diff_t<_InputIterator2>>, "Using a non-integral difference_type is undefined behavior.");

  using _Len1   = __iter_diff_t<_InputIterator1>;
  using _Len2   = __iter_diff_t<_InputIterator2>;
  using _Common = common_type_t<_Len1, _Len2>;

  _Len1 __len1      = __last1 - __first1;
  _Len2 __len2      = __last2 - __first2;
  _Common __min_len = std::min<_Common>(__len1, __len2);

  for (_Common __i = 0; __i < __min_len; ++__i) {
    auto __c = __comp(*__first1, *__first2);
    if (__c != 0) {
      return __c;
    }
    ++__first1;
    ++__first2;
  }

  return __len1 <=> __len2;
}

// Unoptimized implementation which compares the iterators against the end in every loop iteration
template <class _InputIterator1, class _InputIterator2, class _Cmp>
_LIBCPP_HIDE_FROM_ABI constexpr auto __lexicographical_compare_three_way_slow_path(
    _InputIterator1 __first1, _InputIterator1 __last1, _InputIterator2 __first2, _InputIterator2 __last2, _Cmp& __comp)
    -> decltype(__comp(*__first1, *__first2)) {
  while (true) {
    bool __exhausted1 = __first1 == __last1;
    bool __exhausted2 = __first2 == __last2;

    if (__exhausted1 || __exhausted2) {
      if (!__exhausted1)
        return strong_ordering::greater;
      if (!__exhausted2)
        return strong_ordering::less;
      return strong_ordering::equal;
    }

    auto __c = __comp(*__first1, *__first2);
    if (__c != 0) {
      return __c;
    }

    ++__first1;
    ++__first2;
  }
}

template <class _InputIterator1, class _InputIterator2, class _Cmp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto lexicographical_compare_three_way(
    _InputIterator1 __first1, _InputIterator1 __last1, _InputIterator2 __first2, _InputIterator2 __last2, _Cmp __comp)
    -> decltype(__comp(*__first1, *__first2)) {
  static_assert(__comparison_category<decltype(__comp(*__first1, *__first2))>,
                "The comparator passed to lexicographical_compare_three_way must return a comparison category type.");
  static_assert(std::is_copy_constructible_v<_InputIterator1>, "Iterators must be copy constructible.");
  static_assert(std::is_copy_constructible_v<_InputIterator2>, "Iterators must be copy constructible.");
  __three_way_comp_ref_type<_Cmp> __wrapped_comp_ref(__comp);
  if constexpr (__has_random_access_iterator_category<_InputIterator1>::value &&
                __has_random_access_iterator_category<_InputIterator2>::value) {
    return std::__lexicographical_compare_three_way_fast_path(
        std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2), __wrapped_comp_ref);
  } else {
    // Unoptimized implementation which compares the iterators against the end in every loop iteration
    return std::__lexicographical_compare_three_way_slow_path(
        std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2), __wrapped_comp_ref);
  }
}

template <class _InputIterator1, class _InputIterator2>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto lexicographical_compare_three_way(
    _InputIterator1 __first1, _InputIterator1 __last1, _InputIterator2 __first2, _InputIterator2 __last2) {
  return std::lexicographical_compare_three_way(
      std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2), std::compare_three_way());
}

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_LEXICOGRAPHICAL_COMPARE_THREE_WAY_H
