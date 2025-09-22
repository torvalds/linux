//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_PARTITION_POINT_H
#define _LIBCPP___ALGORITHM_RANGES_PARTITION_POINT_H

#include <__algorithm/half_positive.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/concepts.h>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __partition_point {

struct __fn {
  // TODO(ranges): delegate to the classic algorithm.
  template <class _Iter, class _Sent, class _Proj, class _Pred>
  _LIBCPP_HIDE_FROM_ABI constexpr static _Iter
  __partition_point_fn_impl(_Iter&& __first, _Sent&& __last, _Pred& __pred, _Proj& __proj) {
    auto __len = ranges::distance(__first, __last);

    while (__len != 0) {
      auto __half_len = std::__half_positive(__len);
      auto __mid      = ranges::next(__first, __half_len);

      if (std::invoke(__pred, std::invoke(__proj, *__mid))) {
        __first = ++__mid;
        __len -= __half_len + 1;

      } else {
        __len = __half_len;
      }
    }

    return __first;
  }

  template <forward_iterator _Iter,
            sentinel_for<_Iter> _Sent,
            class _Proj = identity,
            indirect_unary_predicate<projected<_Iter, _Proj>> _Pred>
  _LIBCPP_HIDE_FROM_ABI constexpr _Iter operator()(_Iter __first, _Sent __last, _Pred __pred, _Proj __proj = {}) const {
    return __partition_point_fn_impl(std::move(__first), std::move(__last), __pred, __proj);
  }

  template <forward_range _Range,
            class _Proj = identity,
            indirect_unary_predicate<projected<iterator_t<_Range>, _Proj>> _Pred>
  _LIBCPP_HIDE_FROM_ABI constexpr borrowed_iterator_t<_Range>
  operator()(_Range&& __range, _Pred __pred, _Proj __proj = {}) const {
    return __partition_point_fn_impl(ranges::begin(__range), ranges::end(__range), __pred, __proj);
  }
};

} // namespace __partition_point

inline namespace __cpo {
inline constexpr auto partition_point = __partition_point::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_PARTITION_POINT_H
