//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_SEARCH_H
#define _LIBCPP___ALGORITHM_RANGES_SEARCH_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/search.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__iterator/advance.h>
#include <__iterator/concepts.h>
#include <__iterator/distance.h>
#include <__iterator/indirectly_comparable.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/size.h>
#include <__ranges/subrange.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __search {
struct __fn {
  template <class _Iter1, class _Sent1, class _Iter2, class _Sent2, class _Pred, class _Proj1, class _Proj2>
  _LIBCPP_HIDE_FROM_ABI static constexpr subrange<_Iter1> __ranges_search_impl(
      _Iter1 __first1,
      _Sent1 __last1,
      _Iter2 __first2,
      _Sent2 __last2,
      _Pred& __pred,
      _Proj1& __proj1,
      _Proj2& __proj2) {
    if constexpr (sized_sentinel_for<_Sent2, _Iter2>) {
      auto __size2 = ranges::distance(__first2, __last2);
      if (__size2 == 0)
        return {__first1, __first1};

      if constexpr (sized_sentinel_for<_Sent1, _Iter1>) {
        auto __size1 = ranges::distance(__first1, __last1);
        if (__size1 < __size2) {
          ranges::advance(__first1, __last1);
          return {__first1, __first1};
        }

        if constexpr (random_access_iterator<_Iter1> && random_access_iterator<_Iter2>) {
          auto __ret = std::__search_random_access_impl<_RangeAlgPolicy>(
              __first1, __last1, __first2, __last2, __pred, __proj1, __proj2, __size1, __size2);
          return {__ret.first, __ret.second};
        }
      }
    }

    auto __ret =
        std::__search_forward_impl<_RangeAlgPolicy>(__first1, __last1, __first2, __last2, __pred, __proj1, __proj2);
    return {__ret.first, __ret.second};
  }

  template <forward_iterator _Iter1,
            sentinel_for<_Iter1> _Sent1,
            forward_iterator _Iter2,
            sentinel_for<_Iter2> _Sent2,
            class _Pred  = ranges::equal_to,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_comparable<_Iter1, _Iter2, _Pred, _Proj1, _Proj2>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr subrange<_Iter1> operator()(
      _Iter1 __first1,
      _Sent1 __last1,
      _Iter2 __first2,
      _Sent2 __last2,
      _Pred __pred   = {},
      _Proj1 __proj1 = {},
      _Proj2 __proj2 = {}) const {
    return __ranges_search_impl(__first1, __last1, __first2, __last2, __pred, __proj1, __proj2);
  }

  template <forward_range _Range1,
            forward_range _Range2,
            class _Pred  = ranges::equal_to,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_comparable<iterator_t<_Range1>, iterator_t<_Range2>, _Pred, _Proj1, _Proj2>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr borrowed_subrange_t<_Range1> operator()(
      _Range1&& __range1, _Range2&& __range2, _Pred __pred = {}, _Proj1 __proj1 = {}, _Proj2 __proj2 = {}) const {
    auto __first1 = ranges::begin(__range1);
    if constexpr (sized_range<_Range2>) {
      auto __size2 = ranges::size(__range2);
      if (__size2 == 0)
        return {__first1, __first1};
      if constexpr (sized_range<_Range1>) {
        auto __size1 = ranges::size(__range1);
        if (__size1 < __size2) {
          ranges::advance(__first1, ranges::end(__range1));
          return {__first1, __first1};
        }
      }
    }

    return __ranges_search_impl(
        ranges::begin(__range1),
        ranges::end(__range1),
        ranges::begin(__range2),
        ranges::end(__range2),
        __pred,
        __proj1,
        __proj2);
  }
};
} // namespace __search

inline namespace __cpo {
inline constexpr auto search = __search::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___ALGORITHM_RANGES_SEARCH_H
