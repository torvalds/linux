//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_ROTATE_COPY_H
#define _LIBCPP___ALGORITHM_RANGES_ROTATE_COPY_H

#include <__algorithm/in_out_result.h>
#include <__algorithm/ranges_copy.h>
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

template <class _InIter, class _OutIter>
using rotate_copy_result = in_out_result<_InIter, _OutIter>;

namespace __rotate_copy {
struct __fn {
  template <forward_iterator _InIter, sentinel_for<_InIter> _Sent, weakly_incrementable _OutIter>
    requires indirectly_copyable<_InIter, _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr rotate_copy_result<_InIter, _OutIter>
  operator()(_InIter __first, _InIter __middle, _Sent __last, _OutIter __result) const {
    auto __res1 = ranges::copy(__middle, __last, std::move(__result));
    auto __res2 = ranges::copy(__first, __middle, std::move(__res1.out));
    return {std::move(__res1.in), std::move(__res2.out)};
  }

  template <forward_range _Range, weakly_incrementable _OutIter>
    requires indirectly_copyable<iterator_t<_Range>, _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr rotate_copy_result<borrowed_iterator_t<_Range>, _OutIter>
  operator()(_Range&& __range, iterator_t<_Range> __middle, _OutIter __result) const {
    return (*this)(ranges::begin(__range), std::move(__middle), ranges::end(__range), std::move(__result));
  }
};
} // namespace __rotate_copy

inline namespace __cpo {
inline constexpr auto rotate_copy = __rotate_copy::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_ROTATE_COPY_H
