//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_SET_SYMMETRIC_DIFFERENCE_H
#define _LIBCPP___ALGORITHM_RANGES_SET_SYMMETRIC_DIFFERENCE_H

#include <__algorithm/in_in_out_result.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/make_projected.h>
#include <__algorithm/set_symmetric_difference.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/mergeable.h>
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

template <class _InIter1, class _InIter2, class _OutIter>
using set_symmetric_difference_result = in_in_out_result<_InIter1, _InIter2, _OutIter>;

namespace __set_symmetric_difference {

struct __fn {
  template <input_iterator _InIter1,
            sentinel_for<_InIter1> _Sent1,
            input_iterator _InIter2,
            sentinel_for<_InIter2> _Sent2,
            weakly_incrementable _OutIter,
            class _Comp  = ranges::less,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires mergeable<_InIter1, _InIter2, _OutIter, _Comp, _Proj1, _Proj2>
  _LIBCPP_HIDE_FROM_ABI constexpr set_symmetric_difference_result<_InIter1, _InIter2, _OutIter> operator()(
      _InIter1 __first1,
      _Sent1 __last1,
      _InIter2 __first2,
      _Sent2 __last2,
      _OutIter __result,
      _Comp __comp   = {},
      _Proj1 __proj1 = {},
      _Proj2 __proj2 = {}) const {
    auto __ret = std::__set_symmetric_difference<_RangeAlgPolicy>(
        std::move(__first1),
        std::move(__last1),
        std::move(__first2),
        std::move(__last2),
        std::move(__result),
        ranges::__make_projected_comp(__comp, __proj1, __proj2));
    return {std::move(__ret.__in1_), std::move(__ret.__in2_), std::move(__ret.__out_)};
  }

  template <input_range _Range1,
            input_range _Range2,
            weakly_incrementable _OutIter,
            class _Comp  = ranges::less,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires mergeable<iterator_t<_Range1>, iterator_t<_Range2>, _OutIter, _Comp, _Proj1, _Proj2>
  _LIBCPP_HIDE_FROM_ABI constexpr set_symmetric_difference_result<borrowed_iterator_t<_Range1>,
                                                                  borrowed_iterator_t<_Range2>,
                                                                  _OutIter>
  operator()(_Range1&& __range1,
             _Range2&& __range2,
             _OutIter __result,
             _Comp __comp   = {},
             _Proj1 __proj1 = {},
             _Proj2 __proj2 = {}) const {
    auto __ret = std::__set_symmetric_difference<_RangeAlgPolicy>(
        ranges::begin(__range1),
        ranges::end(__range1),
        ranges::begin(__range2),
        ranges::end(__range2),
        std::move(__result),
        ranges::__make_projected_comp(__comp, __proj1, __proj2));
    return {std::move(__ret.__in1_), std::move(__ret.__in2_), std::move(__ret.__out_)};
  }
};

} // namespace __set_symmetric_difference

inline namespace __cpo {
inline constexpr auto set_symmetric_difference = __set_symmetric_difference::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_SET_SYMMETRIC_DIFFERENCE_H
