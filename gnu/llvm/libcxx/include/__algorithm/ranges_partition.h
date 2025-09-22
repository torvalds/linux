//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_PARTITION_H
#define _LIBCPP___ALGORITHM_RANGES_PARTITION_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/make_projected.h>
#include <__algorithm/partition.h>
#include <__algorithm/ranges_iterator_concept.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/permutable.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/subrange.h>
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
namespace __partition {

struct __fn {
  template <class _Iter, class _Sent, class _Proj, class _Pred>
  _LIBCPP_HIDE_FROM_ABI static constexpr subrange<__remove_cvref_t<_Iter>>
  __partition_fn_impl(_Iter&& __first, _Sent&& __last, _Pred&& __pred, _Proj&& __proj) {
    auto&& __projected_pred = std::__make_projected(__pred, __proj);
    auto __result           = std::__partition<_RangeAlgPolicy>(
        std::move(__first), std::move(__last), __projected_pred, __iterator_concept<_Iter>());

    return {std::move(__result.first), std::move(__result.second)};
  }

  template <permutable _Iter,
            sentinel_for<_Iter> _Sent,
            class _Proj = identity,
            indirect_unary_predicate<projected<_Iter, _Proj>> _Pred>
  _LIBCPP_HIDE_FROM_ABI constexpr subrange<_Iter>
  operator()(_Iter __first, _Sent __last, _Pred __pred, _Proj __proj = {}) const {
    return __partition_fn_impl(__first, __last, __pred, __proj);
  }

  template <forward_range _Range,
            class _Proj = identity,
            indirect_unary_predicate<projected<iterator_t<_Range>, _Proj>> _Pred>
    requires permutable<iterator_t<_Range>>
  _LIBCPP_HIDE_FROM_ABI constexpr borrowed_subrange_t<_Range>
  operator()(_Range&& __range, _Pred __pred, _Proj __proj = {}) const {
    return __partition_fn_impl(ranges::begin(__range), ranges::end(__range), __pred, __proj);
  }
};

} // namespace __partition

inline namespace __cpo {
inline constexpr auto partition = __partition::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_PARTITION_H
