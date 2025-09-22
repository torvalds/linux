//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_COPY_BACKWARD_H
#define _LIBCPP___ALGORITHM_RANGES_COPY_BACKWARD_H

#include <__algorithm/copy_backward.h>
#include <__algorithm/in_out_result.h>
#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__iterator/concepts.h>
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

template <class _Ip, class _Op>
using copy_backward_result = in_out_result<_Ip, _Op>;

namespace __copy_backward {
struct __fn {
  template <bidirectional_iterator _InIter1, sentinel_for<_InIter1> _Sent1, bidirectional_iterator _InIter2>
    requires indirectly_copyable<_InIter1, _InIter2>
  _LIBCPP_HIDE_FROM_ABI constexpr copy_backward_result<_InIter1, _InIter2>
  operator()(_InIter1 __first, _Sent1 __last, _InIter2 __result) const {
    auto __ret = std::__copy_backward<_RangeAlgPolicy>(std::move(__first), std::move(__last), std::move(__result));
    return {std::move(__ret.first), std::move(__ret.second)};
  }

  template <bidirectional_range _Range, bidirectional_iterator _Iter>
    requires indirectly_copyable<iterator_t<_Range>, _Iter>
  _LIBCPP_HIDE_FROM_ABI constexpr copy_backward_result<borrowed_iterator_t<_Range>, _Iter>
  operator()(_Range&& __r, _Iter __result) const {
    auto __ret = std::__copy_backward<_RangeAlgPolicy>(ranges::begin(__r), ranges::end(__r), std::move(__result));
    return {std::move(__ret.first), std::move(__ret.second)};
  }
};
} // namespace __copy_backward

inline namespace __cpo {
inline constexpr auto copy_backward = __copy_backward::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_COPY_BACKWARD_H
