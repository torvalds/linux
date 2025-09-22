//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_PARTIAL_SORT_H
#define _LIBCPP___ALGORITHM_RANGES_PARTIAL_SORT_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/make_projected.h>
#include <__algorithm/partial_sort.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__iterator/projected.h>
#include <__iterator/sortable.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__utility/forward.h>
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
namespace __partial_sort {

struct __fn {
  template <class _Iter, class _Sent, class _Comp, class _Proj>
  _LIBCPP_HIDE_FROM_ABI constexpr static _Iter
  __partial_sort_fn_impl(_Iter __first, _Iter __middle, _Sent __last, _Comp& __comp, _Proj& __proj) {
    auto&& __projected_comp = std::__make_projected(__comp, __proj);
    return std::__partial_sort<_RangeAlgPolicy>(std::move(__first), std::move(__middle), __last, __projected_comp);
  }

  template <random_access_iterator _Iter, sentinel_for<_Iter> _Sent, class _Comp = ranges::less, class _Proj = identity>
    requires sortable<_Iter, _Comp, _Proj>
  _LIBCPP_HIDE_FROM_ABI constexpr _Iter
  operator()(_Iter __first, _Iter __middle, _Sent __last, _Comp __comp = {}, _Proj __proj = {}) const {
    return __partial_sort_fn_impl(std::move(__first), std::move(__middle), std::move(__last), __comp, __proj);
  }

  template <random_access_range _Range, class _Comp = ranges::less, class _Proj = identity>
    requires sortable<iterator_t<_Range>, _Comp, _Proj>
  _LIBCPP_HIDE_FROM_ABI constexpr borrowed_iterator_t<_Range>
  operator()(_Range&& __r, iterator_t<_Range> __middle, _Comp __comp = {}, _Proj __proj = {}) const {
    return __partial_sort_fn_impl(ranges::begin(__r), std::move(__middle), ranges::end(__r), __comp, __proj);
  }
};

} // namespace __partial_sort

inline namespace __cpo {
inline constexpr auto partial_sort = __partial_sort::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_PARTIAL_SORT_H
