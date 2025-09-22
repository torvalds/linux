//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_MERGE_H
#define _LIBCPP___ALGORITHM_RANGES_MERGE_H

#include <__algorithm/in_in_out_result.h>
#include <__algorithm/ranges_copy.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/mergeable.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__type_traits/remove_cvref.h>
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
using merge_result = in_in_out_result<_InIter1, _InIter2, _OutIter>;

namespace __merge {

template < class _InIter1,
           class _Sent1,
           class _InIter2,
           class _Sent2,
           class _OutIter,
           class _Comp,
           class _Proj1,
           class _Proj2>
_LIBCPP_HIDE_FROM_ABI constexpr merge_result<__remove_cvref_t<_InIter1>,
                                             __remove_cvref_t<_InIter2>,
                                             __remove_cvref_t<_OutIter>>
__merge_impl(_InIter1&& __first1,
             _Sent1&& __last1,
             _InIter2&& __first2,
             _Sent2&& __last2,
             _OutIter&& __result,
             _Comp&& __comp,
             _Proj1&& __proj1,
             _Proj2&& __proj2) {
  for (; __first1 != __last1 && __first2 != __last2; ++__result) {
    if (std::invoke(__comp, std::invoke(__proj2, *__first2), std::invoke(__proj1, *__first1))) {
      *__result = *__first2;
      ++__first2;
    } else {
      *__result = *__first1;
      ++__first1;
    }
  }
  auto __ret1 = ranges::copy(std::move(__first1), std::move(__last1), std::move(__result));
  auto __ret2 = ranges::copy(std::move(__first2), std::move(__last2), std::move(__ret1.out));
  return {std::move(__ret1.in), std::move(__ret2.in), std::move(__ret2.out)};
}

struct __fn {
  template <input_iterator _InIter1,
            sentinel_for<_InIter1> _Sent1,
            input_iterator _InIter2,
            sentinel_for<_InIter2> _Sent2,
            weakly_incrementable _OutIter,
            class _Comp  = less,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires mergeable<_InIter1, _InIter2, _OutIter, _Comp, _Proj1, _Proj2>
  _LIBCPP_HIDE_FROM_ABI constexpr merge_result<_InIter1, _InIter2, _OutIter> operator()(
      _InIter1 __first1,
      _Sent1 __last1,
      _InIter2 __first2,
      _Sent2 __last2,
      _OutIter __result,
      _Comp __comp   = {},
      _Proj1 __proj1 = {},
      _Proj2 __proj2 = {}) const {
    return __merge::__merge_impl(__first1, __last1, __first2, __last2, __result, __comp, __proj1, __proj2);
  }

  template <input_range _Range1,
            input_range _Range2,
            weakly_incrementable _OutIter,
            class _Comp  = less,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires mergeable<iterator_t<_Range1>, iterator_t<_Range2>, _OutIter, _Comp, _Proj1, _Proj2>
  _LIBCPP_HIDE_FROM_ABI constexpr merge_result<borrowed_iterator_t<_Range1>, borrowed_iterator_t<_Range2>, _OutIter>
  operator()(_Range1&& __range1,
             _Range2&& __range2,
             _OutIter __result,
             _Comp __comp   = {},
             _Proj1 __proj1 = {},
             _Proj2 __proj2 = {}) const {
    return __merge::__merge_impl(
        ranges::begin(__range1),
        ranges::end(__range1),
        ranges::begin(__range2),
        ranges::end(__range2),
        __result,
        __comp,
        __proj1,
        __proj2);
  }
};

} // namespace __merge

inline namespace __cpo {
inline constexpr auto merge = __merge::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_MERGE_H
