//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_IS_HEAP_H
#define _LIBCPP___ALGORITHM_RANGES_IS_HEAP_H

#include <__algorithm/is_heap_until.h>
#include <__algorithm/make_projected.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
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
namespace __is_heap {

struct __fn {
  template <class _Iter, class _Sent, class _Proj, class _Comp>
  _LIBCPP_HIDE_FROM_ABI constexpr static bool
  __is_heap_fn_impl(_Iter __first, _Sent __last, _Comp& __comp, _Proj& __proj) {
    auto __last_iter        = ranges::next(__first, __last);
    auto&& __projected_comp = std::__make_projected(__comp, __proj);

    auto __result = std::__is_heap_until(std::move(__first), std::move(__last_iter), __projected_comp);
    return __result == __last;
  }

  template <random_access_iterator _Iter,
            sentinel_for<_Iter> _Sent,
            class _Proj                                               = identity,
            indirect_strict_weak_order<projected<_Iter, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool
  operator()(_Iter __first, _Sent __last, _Comp __comp = {}, _Proj __proj = {}) const {
    return __is_heap_fn_impl(std::move(__first), std::move(__last), __comp, __proj);
  }

  template <random_access_range _Range,
            class _Proj                                                            = identity,
            indirect_strict_weak_order<projected<iterator_t<_Range>, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool
  operator()(_Range&& __range, _Comp __comp = {}, _Proj __proj = {}) const {
    return __is_heap_fn_impl(ranges::begin(__range), ranges::end(__range), __comp, __proj);
  }
};

} // namespace __is_heap

inline namespace __cpo {
inline constexpr auto is_heap = __is_heap::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_IS_HEAP_H
