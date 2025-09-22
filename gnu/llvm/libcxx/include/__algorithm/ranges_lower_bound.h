//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_LOWER_BOUND_H
#define _LIBCPP___ALGORITHM_RANGES_LOWER_BOUND_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/lower_bound.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/advance.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {

namespace __lower_bound {
struct __fn {
  template <forward_iterator _Iter,
            sentinel_for<_Iter> _Sent,
            class _Type,
            class _Proj                                                             = identity,
            indirect_strict_weak_order<const _Type*, projected<_Iter, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Iter
  operator()(_Iter __first, _Sent __last, const _Type& __value, _Comp __comp = {}, _Proj __proj = {}) const {
    return std::__lower_bound<_RangeAlgPolicy>(__first, __last, __value, __comp, __proj);
  }

  template <forward_range _Range,
            class _Type,
            class _Proj                                                                          = identity,
            indirect_strict_weak_order<const _Type*, projected<iterator_t<_Range>, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr borrowed_iterator_t<_Range>
  operator()(_Range&& __r, const _Type& __value, _Comp __comp = {}, _Proj __proj = {}) const {
    return std::__lower_bound<_RangeAlgPolicy>(ranges::begin(__r), ranges::end(__r), __value, __comp, __proj);
  }
};
} // namespace __lower_bound

inline namespace __cpo {
inline constexpr auto lower_bound = __lower_bound::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_LOWER_BOUND_H
