//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_SWAP_RANGES_H
#define _LIBCPP___ALGORITHM_RANGES_SWAP_RANGES_H

#include <__algorithm/in_in_result.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/swap_ranges.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iter_swap.h>
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

template <class _I1, class _I2>
using swap_ranges_result = in_in_result<_I1, _I2>;

namespace __swap_ranges {
struct __fn {
  template <input_iterator _I1, sentinel_for<_I1> _S1, input_iterator _I2, sentinel_for<_I2> _S2>
    requires indirectly_swappable<_I1, _I2>
  _LIBCPP_HIDE_FROM_ABI constexpr swap_ranges_result<_I1, _I2>
  operator()(_I1 __first1, _S1 __last1, _I2 __first2, _S2 __last2) const {
    auto __ret = std::__swap_ranges<_RangeAlgPolicy>(
        std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2));
    return {std::move(__ret.first), std::move(__ret.second)};
  }

  template <input_range _R1, input_range _R2>
    requires indirectly_swappable<iterator_t<_R1>, iterator_t<_R2>>
  _LIBCPP_HIDE_FROM_ABI constexpr swap_ranges_result<borrowed_iterator_t<_R1>, borrowed_iterator_t<_R2>>
  operator()(_R1&& __r1, _R2&& __r2) const {
    return operator()(ranges::begin(__r1), ranges::end(__r1), ranges::begin(__r2), ranges::end(__r2));
  }
};
} // namespace __swap_ranges

inline namespace __cpo {
inline constexpr auto swap_ranges = __swap_ranges::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_SWAP_RANGES_H
