//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_MISMATCH_H
#define _LIBCPP___ALGORITHM_RANGES_MISMATCH_H

#include <__algorithm/in_in_result.h>
#include <__algorithm/mismatch.h>
#include <__algorithm/unwrap_range.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/indirectly_comparable.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {

template <class _I1, class _I2>
using mismatch_result = in_in_result<_I1, _I2>;

namespace __mismatch {
struct __fn {
  template <class _I1, class _S1, class _I2, class _S2, class _Pred, class _Proj1, class _Proj2>
  static _LIBCPP_HIDE_FROM_ABI constexpr mismatch_result<_I1, _I2>
  __go(_I1 __first1, _S1 __last1, _I2 __first2, _S2 __last2, _Pred& __pred, _Proj1& __proj1, _Proj2& __proj2) {
    if constexpr (forward_iterator<_I1> && forward_iterator<_I2>) {
      auto __range1 = std::__unwrap_range(__first1, __last1);
      auto __range2 = std::__unwrap_range(__first2, __last2);
      auto __res =
          std::__mismatch(__range1.first, __range1.second, __range2.first, __range2.second, __pred, __proj1, __proj2);
      return {std::__rewrap_range<_S1>(__first1, __res.first), std::__rewrap_range<_S2>(__first2, __res.second)};
    } else {
      auto __res = std::__mismatch(
          std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2), __pred, __proj1, __proj2);
      return {std::move(__res.first), std::move(__res.second)};
    }
  }

  template <input_iterator _I1,
            sentinel_for<_I1> _S1,
            input_iterator _I2,
            sentinel_for<_I2> _S2,
            class _Pred  = ranges::equal_to,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_comparable<_I1, _I2, _Pred, _Proj1, _Proj2>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr mismatch_result<_I1, _I2> operator()(
      _I1 __first1, _S1 __last1, _I2 __first2, _S2 __last2, _Pred __pred = {}, _Proj1 __proj1 = {}, _Proj2 __proj2 = {})
      const {
    return __go(std::move(__first1), __last1, std::move(__first2), __last2, __pred, __proj1, __proj2);
  }

  template <input_range _R1,
            input_range _R2,
            class _Pred  = ranges::equal_to,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_comparable<iterator_t<_R1>, iterator_t<_R2>, _Pred, _Proj1, _Proj2>
  [[nodiscard]]
  _LIBCPP_HIDE_FROM_ABI constexpr mismatch_result<borrowed_iterator_t<_R1>, borrowed_iterator_t<_R2>>
  operator()(_R1&& __r1, _R2&& __r2, _Pred __pred = {}, _Proj1 __proj1 = {}, _Proj2 __proj2 = {}) const {
    return __go(
        ranges::begin(__r1), ranges::end(__r1), ranges::begin(__r2), ranges::end(__r2), __pred, __proj1, __proj2);
  }
};
} // namespace __mismatch

inline namespace __cpo {
constexpr inline auto mismatch = __mismatch::__fn{};
} // namespace __cpo
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_MISMATCH_H
