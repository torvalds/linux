//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_REVERSE_COPY_H
#define _LIBCPP___ALGORITHM_RANGES_REVERSE_COPY_H

#include <__algorithm/in_out_result.h>
#include <__algorithm/ranges_copy.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/next.h>
#include <__iterator/reverse_iterator.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__ranges/subrange.h>
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
using reverse_copy_result = in_out_result<_InIter, _OutIter>;

namespace __reverse_copy {
struct __fn {
  template <bidirectional_iterator _InIter, sentinel_for<_InIter> _Sent, weakly_incrementable _OutIter>
    requires indirectly_copyable<_InIter, _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr reverse_copy_result<_InIter, _OutIter>
  operator()(_InIter __first, _Sent __last, _OutIter __result) const {
    return (*this)(subrange(std::move(__first), std::move(__last)), std::move(__result));
  }

  template <bidirectional_range _Range, weakly_incrementable _OutIter>
    requires indirectly_copyable<iterator_t<_Range>, _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr reverse_copy_result<borrowed_iterator_t<_Range>, _OutIter>
  operator()(_Range&& __range, _OutIter __result) const {
    auto __ret = ranges::copy(std::__reverse_range(__range), std::move(__result));
    return {ranges::next(ranges::begin(__range), ranges::end(__range)), std::move(__ret.out)};
  }
};
} // namespace __reverse_copy

inline namespace __cpo {
inline constexpr auto reverse_copy = __reverse_copy::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_REVERSE_COPY_H
