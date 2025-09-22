//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_PARTIAL_SORT_COPY_H
#define _LIBCPP___ALGORITHM_RANGES_PARTIAL_SORT_COPY_H

#include <__algorithm/in_out_result.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/make_projected.h>
#include <__algorithm/partial_sort_copy.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/projected.h>
#include <__iterator/sortable.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {

template <class _InIter, class _OutIter>
using partial_sort_copy_result = in_out_result<_InIter, _OutIter>;

namespace __partial_sort_copy {

struct __fn {
  template <input_iterator _Iter1,
            sentinel_for<_Iter1> _Sent1,
            random_access_iterator _Iter2,
            sentinel_for<_Iter2> _Sent2,
            class _Comp  = ranges::less,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_copyable<_Iter1, _Iter2> && sortable<_Iter2, _Comp, _Proj2> &&
             indirect_strict_weak_order<_Comp, projected<_Iter1, _Proj1>, projected<_Iter2, _Proj2>>
  _LIBCPP_HIDE_FROM_ABI constexpr partial_sort_copy_result<_Iter1, _Iter2> operator()(
      _Iter1 __first,
      _Sent1 __last,
      _Iter2 __result_first,
      _Sent2 __result_last,
      _Comp __comp   = {},
      _Proj1 __proj1 = {},
      _Proj2 __proj2 = {}) const {
    auto __result = std::__partial_sort_copy<_RangeAlgPolicy>(
        std::move(__first),
        std::move(__last),
        std::move(__result_first),
        std::move(__result_last),
        __comp,
        __proj1,
        __proj2);
    return {std::move(__result.first), std::move(__result.second)};
  }

  template <input_range _Range1,
            random_access_range _Range2,
            class _Comp  = ranges::less,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_copyable<iterator_t<_Range1>, iterator_t<_Range2>> &&
             sortable<iterator_t<_Range2>, _Comp, _Proj2> &&
             indirect_strict_weak_order<_Comp,
                                        projected<iterator_t<_Range1>, _Proj1>,
                                        projected<iterator_t<_Range2>, _Proj2>>
  _LIBCPP_HIDE_FROM_ABI constexpr partial_sort_copy_result<borrowed_iterator_t<_Range1>, borrowed_iterator_t<_Range2>>
  operator()(
      _Range1&& __range, _Range2&& __result_range, _Comp __comp = {}, _Proj1 __proj1 = {}, _Proj2 __proj2 = {}) const {
    auto __result = std::__partial_sort_copy<_RangeAlgPolicy>(
        ranges::begin(__range),
        ranges::end(__range),
        ranges::begin(__result_range),
        ranges::end(__result_range),
        __comp,
        __proj1,
        __proj2);
    return {std::move(__result.first), std::move(__result.second)};
  }
};

} // namespace __partial_sort_copy

inline namespace __cpo {
inline constexpr auto partial_sort_copy = __partial_sort_copy::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_PARTIAL_SORT_COPY_H
