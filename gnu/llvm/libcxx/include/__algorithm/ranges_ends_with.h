//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_ENDS_WITH_H
#define _LIBCPP___ALGORITHM_RANGES_ENDS_WITH_H

#include <__algorithm/ranges_equal.h>
#include <__algorithm/ranges_starts_with.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__functional/reference_wrapper.h>
#include <__iterator/advance.h>
#include <__iterator/concepts.h>
#include <__iterator/distance.h>
#include <__iterator/indirectly_comparable.h>
#include <__iterator/reverse_iterator.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 23

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __ends_with {
struct __fn {
  template <class _Iter1, class _Sent1, class _Iter2, class _Sent2, class _Pred, class _Proj1, class _Proj2>
  _LIBCPP_HIDE_FROM_ABI static constexpr bool __ends_with_fn_impl_bidirectional(
      _Iter1 __first1,
      _Sent1 __last1,
      _Iter2 __first2,
      _Sent2 __last2,
      _Pred& __pred,
      _Proj1& __proj1,
      _Proj2& __proj2) {
    auto __rbegin1 = std::make_reverse_iterator(__last1);
    auto __rend1   = std::make_reverse_iterator(__first1);
    auto __rbegin2 = std::make_reverse_iterator(__last2);
    auto __rend2   = std::make_reverse_iterator(__first2);
    return ranges::starts_with(
        __rbegin1, __rend1, __rbegin2, __rend2, std::ref(__pred), std::ref(__proj1), std::ref(__proj2));
  }

  template <class _Iter1, class _Sent1, class _Iter2, class _Sent2, class _Pred, class _Proj1, class _Proj2>
  _LIBCPP_HIDE_FROM_ABI static constexpr bool __ends_with_fn_impl(
      _Iter1 __first1,
      _Sent1 __last1,
      _Iter2 __first2,
      _Sent2 __last2,
      _Pred& __pred,
      _Proj1& __proj1,
      _Proj2& __proj2) {
    if constexpr (std::bidirectional_iterator<_Sent1> && std::bidirectional_iterator<_Sent2> &&
                  (!std::random_access_iterator<_Sent1>) && (!std::random_access_iterator<_Sent2>)) {
      return __ends_with_fn_impl_bidirectional(__first1, __last1, __first2, __last2, __pred, __proj1, __proj2);

    } else {
      auto __n1 = ranges::distance(__first1, __last1);
      auto __n2 = ranges::distance(__first2, __last2);
      if (__n2 == 0)
        return true;
      if (__n2 > __n1)
        return false;

      return __ends_with_fn_impl_with_offset(
          std::move(__first1),
          std::move(__last1),
          std::move(__first2),
          std::move(__last2),
          __pred,
          __proj1,
          __proj2,
          __n1 - __n2);
    }
  }

  template <class _Iter1,
            class _Sent1,
            class _Iter2,
            class _Sent2,
            class _Pred,
            class _Proj1,
            class _Proj2,
            class _Offset>
  static _LIBCPP_HIDE_FROM_ABI constexpr bool __ends_with_fn_impl_with_offset(
      _Iter1 __first1,
      _Sent1 __last1,
      _Iter2 __first2,
      _Sent2 __last2,
      _Pred& __pred,
      _Proj1& __proj1,
      _Proj2& __proj2,
      _Offset __offset) {
    if constexpr (std::bidirectional_iterator<_Sent1> && std::bidirectional_iterator<_Sent2> &&
                  !std::random_access_iterator<_Sent1> && !std::random_access_iterator<_Sent2>) {
      return __ends_with_fn_impl_bidirectional(
          std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2), __pred, __proj1, __proj2);

    } else {
      ranges::advance(__first1, __offset);
      return ranges::equal(
          std::move(__first1),
          std::move(__last1),
          std::move(__first2),
          std::move(__last2),
          std::ref(__pred),
          std::ref(__proj1),
          std::ref(__proj2));
    }
  }

  template <input_iterator _Iter1,
            sentinel_for<_Iter1> _Sent1,
            input_iterator _Iter2,
            sentinel_for<_Iter2> _Sent2,
            class _Pred  = ranges::equal_to,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires(forward_iterator<_Iter1> || sized_sentinel_for<_Sent1, _Iter1>) &&
            (forward_iterator<_Iter2> || sized_sentinel_for<_Sent2, _Iter2>) &&
            indirectly_comparable<_Iter1, _Iter2, _Pred, _Proj1, _Proj2>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool operator()(
      _Iter1 __first1,
      _Sent1 __last1,
      _Iter2 __first2,
      _Sent2 __last2,
      _Pred __pred   = {},
      _Proj1 __proj1 = {},
      _Proj2 __proj2 = {}) const {
    return __ends_with_fn_impl(
        std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2), __pred, __proj1, __proj2);
  }

  template <input_range _Range1,
            input_range _Range2,
            class _Pred  = ranges::equal_to,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires(forward_range<_Range1> || sized_range<_Range1>) && (forward_range<_Range2> || sized_range<_Range2>) &&
            indirectly_comparable<iterator_t<_Range1>, iterator_t<_Range2>, _Pred, _Proj1, _Proj2>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool operator()(
      _Range1&& __range1, _Range2&& __range2, _Pred __pred = {}, _Proj1 __proj1 = {}, _Proj2 __proj2 = {}) const {
    if constexpr (sized_range<_Range1> && sized_range<_Range2>) {
      auto __n1 = ranges::size(__range1);
      auto __n2 = ranges::size(__range2);
      if (__n2 == 0)
        return true;
      if (__n2 > __n1)
        return false;
      auto __offset = __n1 - __n2;

      return __ends_with_fn_impl_with_offset(
          ranges::begin(__range1),
          ranges::end(__range1),
          ranges::begin(__range2),
          ranges::end(__range2),
          __pred,
          __proj1,
          __proj2,
          __offset);

    } else {
      return __ends_with_fn_impl(
          ranges::begin(__range1),
          ranges::end(__range1),
          ranges::begin(__range2),
          ranges::end(__range2),
          __pred,
          __proj1,
          __proj2);
    }
  }
};
} // namespace __ends_with

inline namespace __cpo {
inline constexpr auto ends_with = __ends_with::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_ENDS_WITH_H
