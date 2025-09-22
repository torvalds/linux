//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_SEARCH_N_H
#define _LIBCPP___ALGORITHM_RANGES_SEARCH_N_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/search_n.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__iterator/advance.h>
#include <__iterator/concepts.h>
#include <__iterator/distance.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/indirectly_comparable.h>
#include <__iterator/iterator_traits.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/size.h>
#include <__ranges/subrange.h>
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
namespace __search_n {
struct __fn {
  template <class _Iter1, class _Sent1, class _SizeT, class _Type, class _Pred, class _Proj>
  _LIBCPP_HIDE_FROM_ABI static constexpr subrange<_Iter1> __ranges_search_n_impl(
      _Iter1 __first, _Sent1 __last, _SizeT __count, const _Type& __value, _Pred& __pred, _Proj& __proj) {
    if (__count == 0)
      return {__first, __first};

    if constexpr (sized_sentinel_for<_Sent1, _Iter1>) {
      auto __size = ranges::distance(__first, __last);
      if (__size < __count) {
        ranges::advance(__first, __last);
        return {__first, __first};
      }

      if constexpr (random_access_iterator<_Iter1>) {
        auto __ret = std::__search_n_random_access_impl<_RangeAlgPolicy>(
            __first, __last, __count, __value, __pred, __proj, __size);
        return {std::move(__ret.first), std::move(__ret.second)};
      }
    }

    auto __ret = std::__search_n_forward_impl<_RangeAlgPolicy>(__first, __last, __count, __value, __pred, __proj);
    return {std::move(__ret.first), std::move(__ret.second)};
  }

  template <forward_iterator _Iter,
            sentinel_for<_Iter> _Sent,
            class _Type,
            class _Pred = ranges::equal_to,
            class _Proj = identity>
    requires indirectly_comparable<_Iter, const _Type*, _Pred, _Proj>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr subrange<_Iter>
  operator()(_Iter __first,
             _Sent __last,
             iter_difference_t<_Iter> __count,
             const _Type& __value,
             _Pred __pred = {},
             _Proj __proj = _Proj{}) const {
    return __ranges_search_n_impl(__first, __last, __count, __value, __pred, __proj);
  }

  template <forward_range _Range, class _Type, class _Pred = ranges::equal_to, class _Proj = identity>
    requires indirectly_comparable<iterator_t<_Range>, const _Type*, _Pred, _Proj>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr borrowed_subrange_t<_Range> operator()(
      _Range&& __range, range_difference_t<_Range> __count, const _Type& __value, _Pred __pred = {}, _Proj __proj = {})
      const {
    auto __first = ranges::begin(__range);
    if (__count <= 0)
      return {__first, __first};
    if constexpr (sized_range<_Range>) {
      auto __size1 = ranges::size(__range);
      if (__size1 < static_cast<range_size_t<_Range>>(__count)) {
        ranges::advance(__first, ranges::end(__range));
        return {__first, __first};
      }
    }

    return __ranges_search_n_impl(ranges::begin(__range), ranges::end(__range), __count, __value, __pred, __proj);
  }
};
} // namespace __search_n

inline namespace __cpo {
inline constexpr auto search_n = __search_n::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_SEARCH_N_H
