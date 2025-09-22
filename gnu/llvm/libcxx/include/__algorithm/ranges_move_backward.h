//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_MOVE_BACKWARD_H
#define _LIBCPP___ALGORITHM_RANGES_MOVE_BACKWARD_H

#include <__algorithm/in_out_result.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/move_backward.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iter_move.h>
#include <__iterator/next.h>
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

template <class _InIter, class _OutIter>
using move_backward_result = in_out_result<_InIter, _OutIter>;

namespace __move_backward {
struct __fn {
  template <class _InIter, class _Sent, class _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr static move_backward_result<_InIter, _OutIter>
  __move_backward_impl(_InIter __first, _Sent __last, _OutIter __result) {
    auto __ret = std::__move_backward<_RangeAlgPolicy>(std::move(__first), std::move(__last), std::move(__result));
    return {std::move(__ret.first), std::move(__ret.second)};
  }

  template <bidirectional_iterator _InIter, sentinel_for<_InIter> _Sent, bidirectional_iterator _OutIter>
    requires indirectly_movable<_InIter, _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr move_backward_result<_InIter, _OutIter>
  operator()(_InIter __first, _Sent __last, _OutIter __result) const {
    return __move_backward_impl(std::move(__first), std::move(__last), std::move(__result));
  }

  template <bidirectional_range _Range, bidirectional_iterator _Iter>
    requires indirectly_movable<iterator_t<_Range>, _Iter>
  _LIBCPP_HIDE_FROM_ABI constexpr move_backward_result<borrowed_iterator_t<_Range>, _Iter>
  operator()(_Range&& __range, _Iter __result) const {
    return __move_backward_impl(ranges::begin(__range), ranges::end(__range), std::move(__result));
  }
};
} // namespace __move_backward

inline namespace __cpo {
inline constexpr auto move_backward = __move_backward::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_MOVE_BACKWARD_H
