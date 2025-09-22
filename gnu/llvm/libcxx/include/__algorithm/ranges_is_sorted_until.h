//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP__ALGORITHM_RANGES_IS_SORTED_UNTIL_H
#define _LIBCPP__ALGORITHM_RANGES_IS_SORTED_UNTIL_H

#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/projected.h>
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

template <class _Iter, class _Sent, class _Proj, class _Comp>
_LIBCPP_HIDE_FROM_ABI constexpr _Iter
__is_sorted_until_impl(_Iter __first, _Sent __last, _Comp& __comp, _Proj& __proj) {
  if (__first == __last)
    return __first;
  auto __i = __first;
  while (++__i != __last) {
    if (std::invoke(__comp, std::invoke(__proj, *__i), std::invoke(__proj, *__first)))
      return __i;
    __first = __i;
  }
  return __i;
}

namespace __is_sorted_until {
struct __fn {
  template <forward_iterator _Iter,
            sentinel_for<_Iter> _Sent,
            class _Proj                                               = identity,
            indirect_strict_weak_order<projected<_Iter, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Iter
  operator()(_Iter __first, _Sent __last, _Comp __comp = {}, _Proj __proj = {}) const {
    return ranges::__is_sorted_until_impl(std::move(__first), std::move(__last), __comp, __proj);
  }

  template <forward_range _Range,
            class _Proj                                                            = identity,
            indirect_strict_weak_order<projected<iterator_t<_Range>, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr borrowed_iterator_t<_Range>
  operator()(_Range&& __range, _Comp __comp = {}, _Proj __proj = {}) const {
    return ranges::__is_sorted_until_impl(ranges::begin(__range), ranges::end(__range), __comp, __proj);
  }
};
} // namespace __is_sorted_until

inline namespace __cpo {
inline constexpr auto is_sorted_until = __is_sorted_until::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP__ALGORITHM_RANGES_IS_SORTED_UNTIL_H
