//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_LEXICOGRAPHICAL_COMPARE_H
#define _LIBCPP___ALGORITHM_RANGES_LEXICOGRAPHICAL_COMPARE_H

#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
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
namespace __lexicographical_compare {
struct __fn {
  template <class _Iter1, class _Sent1, class _Iter2, class _Sent2, class _Proj1, class _Proj2, class _Comp>
  _LIBCPP_HIDE_FROM_ABI constexpr static bool __lexicographical_compare_impl(
      _Iter1 __first1,
      _Sent1 __last1,
      _Iter2 __first2,
      _Sent2 __last2,
      _Comp& __comp,
      _Proj1& __proj1,
      _Proj2& __proj2) {
    while (__first2 != __last2) {
      if (__first1 == __last1 || std::invoke(__comp, std::invoke(__proj1, *__first1), std::invoke(__proj2, *__first2)))
        return true;
      if (std::invoke(__comp, std::invoke(__proj2, *__first2), std::invoke(__proj1, *__first1)))
        return false;
      ++__first1;
      ++__first2;
    }
    return false;
  }

  template <input_iterator _Iter1,
            sentinel_for<_Iter1> _Sent1,
            input_iterator _Iter2,
            sentinel_for<_Iter2> _Sent2,
            class _Proj1                                                                           = identity,
            class _Proj2                                                                           = identity,
            indirect_strict_weak_order<projected<_Iter1, _Proj1>, projected<_Iter2, _Proj2>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool operator()(
      _Iter1 __first1,
      _Sent1 __last1,
      _Iter2 __first2,
      _Sent2 __last2,
      _Comp __comp   = {},
      _Proj1 __proj1 = {},
      _Proj2 __proj2 = {}) const {
    return __lexicographical_compare_impl(
        std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2), __comp, __proj1, __proj2);
  }

  template <input_range _Range1,
            input_range _Range2,
            class _Proj1 = identity,
            class _Proj2 = identity,
            indirect_strict_weak_order<projected<iterator_t<_Range1>, _Proj1>, projected<iterator_t<_Range2>, _Proj2>>
                _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool operator()(
      _Range1&& __range1, _Range2&& __range2, _Comp __comp = {}, _Proj1 __proj1 = {}, _Proj2 __proj2 = {}) const {
    return __lexicographical_compare_impl(
        ranges::begin(__range1),
        ranges::end(__range1),
        ranges::begin(__range2),
        ranges::end(__range2),
        __comp,
        __proj1,
        __proj2);
  }
};
} // namespace __lexicographical_compare

inline namespace __cpo {
inline constexpr auto lexicographical_compare = __lexicographical_compare::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_LEXICOGRAPHICAL_COMPARE_H
