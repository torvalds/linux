//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_NEXT_PERMUTATION_H
#define _LIBCPP___ALGORITHM_RANGES_NEXT_PERMUTATION_H

#include <__algorithm/in_found_result.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/make_projected.h>
#include <__algorithm/next_permutation.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
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

template <class _InIter>
using next_permutation_result = in_found_result<_InIter>;

namespace __next_permutation {

struct __fn {
  template <bidirectional_iterator _Iter, sentinel_for<_Iter> _Sent, class _Comp = ranges::less, class _Proj = identity>
    requires sortable<_Iter, _Comp, _Proj>
  _LIBCPP_HIDE_FROM_ABI constexpr next_permutation_result<_Iter>
  operator()(_Iter __first, _Sent __last, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __result = std::__next_permutation<_RangeAlgPolicy>(
        std::move(__first), std::move(__last), std::__make_projected(__comp, __proj));
    return {std::move(__result.first), std::move(__result.second)};
  }

  template <bidirectional_range _Range, class _Comp = ranges::less, class _Proj = identity>
    requires sortable<iterator_t<_Range>, _Comp, _Proj>
  _LIBCPP_HIDE_FROM_ABI constexpr next_permutation_result<borrowed_iterator_t<_Range>>
  operator()(_Range&& __range, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __result = std::__next_permutation<_RangeAlgPolicy>(
        ranges::begin(__range), ranges::end(__range), std::__make_projected(__comp, __proj));
    return {std::move(__result.first), std::move(__result.second)};
  }
};

} // namespace __next_permutation

inline namespace __cpo {
constexpr inline auto next_permutation = __next_permutation::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_NEXT_PERMUTATION_H
