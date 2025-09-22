//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_EQUAL_RANGE_H
#define _LIBCPP___ALGORITHM_RANGES_EQUAL_RANGE_H

#include <__algorithm/equal_range.h>
#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
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
namespace __equal_range {

struct __fn {
  template <forward_iterator _Iter,
            sentinel_for<_Iter> _Sent,
            class _Tp,
            class _Proj                                                           = identity,
            indirect_strict_weak_order<const _Tp*, projected<_Iter, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr subrange<_Iter>
  operator()(_Iter __first, _Sent __last, const _Tp& __value, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __ret = std::__equal_range<_RangeAlgPolicy>(std::move(__first), std::move(__last), __value, __comp, __proj);
    return {std::move(__ret.first), std::move(__ret.second)};
  }

  template <forward_range _Range,
            class _Tp,
            class _Proj                                                                        = identity,
            indirect_strict_weak_order<const _Tp*, projected<iterator_t<_Range>, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr borrowed_subrange_t<_Range>
  operator()(_Range&& __range, const _Tp& __value, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __ret =
        std::__equal_range<_RangeAlgPolicy>(ranges::begin(__range), ranges::end(__range), __value, __comp, __proj);
    return {std::move(__ret.first), std::move(__ret.second)};
  }
};

} // namespace __equal_range

inline namespace __cpo {
inline constexpr auto equal_range = __equal_range::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_EQUAL_RANGE_H
