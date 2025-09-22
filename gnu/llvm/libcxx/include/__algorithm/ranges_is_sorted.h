//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP__ALGORITHM_RANGES_IS_SORTED_H
#define _LIBCPP__ALGORITHM_RANGES_IS_SORTED_H

#include <__algorithm/ranges_is_sorted_until.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __is_sorted {
struct __fn {
  template <forward_iterator _Iter,
            sentinel_for<_Iter> _Sent,
            class _Proj                                               = identity,
            indirect_strict_weak_order<projected<_Iter, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool
  operator()(_Iter __first, _Sent __last, _Comp __comp = {}, _Proj __proj = {}) const {
    return ranges::__is_sorted_until_impl(std::move(__first), __last, __comp, __proj) == __last;
  }

  template <forward_range _Range,
            class _Proj                                                            = identity,
            indirect_strict_weak_order<projected<iterator_t<_Range>, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool
  operator()(_Range&& __range, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __last = ranges::end(__range);
    return ranges::__is_sorted_until_impl(ranges::begin(__range), __last, __comp, __proj) == __last;
  }
};
} // namespace __is_sorted

inline namespace __cpo {
inline constexpr auto is_sorted = __is_sorted::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP__ALGORITHM_RANGES_IS_SORTED_H
