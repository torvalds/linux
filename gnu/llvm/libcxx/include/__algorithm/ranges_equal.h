//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_EQUAL_H
#define _LIBCPP___ALGORITHM_RANGES_EQUAL_H

#include <__algorithm/equal.h>
#include <__algorithm/unwrap_range.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/distance.h>
#include <__iterator/indirectly_comparable.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __equal {
struct __fn {
  template <input_iterator _Iter1,
            sentinel_for<_Iter1> _Sent1,
            input_iterator _Iter2,
            sentinel_for<_Iter2> _Sent2,
            class _Pred  = ranges::equal_to,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_comparable<_Iter1, _Iter2, _Pred, _Proj1, _Proj2>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool operator()(
      _Iter1 __first1,
      _Sent1 __last1,
      _Iter2 __first2,
      _Sent2 __last2,
      _Pred __pred   = {},
      _Proj1 __proj1 = {},
      _Proj2 __proj2 = {}) const {
    if constexpr (sized_sentinel_for<_Sent1, _Iter1> && sized_sentinel_for<_Sent2, _Iter2>) {
      if (__last1 - __first1 != __last2 - __first2)
        return false;
    }
    auto __unwrapped1 = std::__unwrap_range(std::move(__first1), std::move(__last1));
    auto __unwrapped2 = std::__unwrap_range(std::move(__first2), std::move(__last2));
    return std::__equal_impl(
        std::move(__unwrapped1.first),
        std::move(__unwrapped1.second),
        std::move(__unwrapped2.first),
        std::move(__unwrapped2.second),
        __pred,
        __proj1,
        __proj2);
  }

  template <input_range _Range1,
            input_range _Range2,
            class _Pred  = ranges::equal_to,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_comparable<iterator_t<_Range1>, iterator_t<_Range2>, _Pred, _Proj1, _Proj2>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool operator()(
      _Range1&& __range1, _Range2&& __range2, _Pred __pred = {}, _Proj1 __proj1 = {}, _Proj2 __proj2 = {}) const {
    if constexpr (sized_range<_Range1> && sized_range<_Range2>) {
      if (ranges::distance(__range1) != ranges::distance(__range2))
        return false;
    }
    auto __unwrapped1 = std::__unwrap_range(ranges::begin(__range1), ranges::end(__range1));
    auto __unwrapped2 = std::__unwrap_range(ranges::begin(__range2), ranges::end(__range2));
    return std::__equal_impl(
        std::move(__unwrapped1.first),
        std::move(__unwrapped1.second),
        std::move(__unwrapped2.first),
        std::move(__unwrapped2.second),
        __pred,
        __proj1,
        __proj2);
    return false;
  }
};
} // namespace __equal

inline namespace __cpo {
inline constexpr auto equal = __equal::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_EQUAL_H
