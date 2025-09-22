//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_COUNT_IF_H
#define _LIBCPP___ALGORITHM_RANGES_COUNT_IF_H

#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/projected.h>
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
template <class _Iter, class _Sent, class _Proj, class _Pred>
_LIBCPP_HIDE_FROM_ABI constexpr iter_difference_t<_Iter>
__count_if_impl(_Iter __first, _Sent __last, _Pred& __pred, _Proj& __proj) {
  iter_difference_t<_Iter> __counter(0);
  for (; __first != __last; ++__first) {
    if (std::invoke(__pred, std::invoke(__proj, *__first)))
      ++__counter;
  }
  return __counter;
}

namespace __count_if {
struct __fn {
  template <input_iterator _Iter,
            sentinel_for<_Iter> _Sent,
            class _Proj = identity,
            indirect_unary_predicate<projected<_Iter, _Proj>> _Predicate>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr iter_difference_t<_Iter>
  operator()(_Iter __first, _Sent __last, _Predicate __pred, _Proj __proj = {}) const {
    return ranges::__count_if_impl(std::move(__first), std::move(__last), __pred, __proj);
  }

  template <input_range _Range,
            class _Proj = identity,
            indirect_unary_predicate<projected<iterator_t<_Range>, _Proj>> _Predicate>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr range_difference_t<_Range>
  operator()(_Range&& __r, _Predicate __pred, _Proj __proj = {}) const {
    return ranges::__count_if_impl(ranges::begin(__r), ranges::end(__r), __pred, __proj);
  }
};
} // namespace __count_if

inline namespace __cpo {
inline constexpr auto count_if = __count_if::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_COUNT_IF_H
