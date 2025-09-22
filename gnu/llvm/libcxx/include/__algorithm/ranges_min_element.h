//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_MIN_ELEMENT_H
#define _LIBCPP___ALGORITHM_RANGES_MIN_ELEMENT_H

#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {

// TODO(ranges): `ranges::min_element` can now simply delegate to `std::__min_element`.
template <class _Ip, class _Sp, class _Proj, class _Comp>
_LIBCPP_HIDE_FROM_ABI constexpr _Ip __min_element_impl(_Ip __first, _Sp __last, _Comp& __comp, _Proj& __proj) {
  if (__first == __last)
    return __first;

  _Ip __i = __first;
  while (++__i != __last)
    if (std::invoke(__comp, std::invoke(__proj, *__i), std::invoke(__proj, *__first)))
      __first = __i;
  return __first;
}

namespace __min_element {
struct __fn {
  template <forward_iterator _Ip,
            sentinel_for<_Ip> _Sp,
            class _Proj                                             = identity,
            indirect_strict_weak_order<projected<_Ip, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Ip
  operator()(_Ip __first, _Sp __last, _Comp __comp = {}, _Proj __proj = {}) const {
    return ranges::__min_element_impl(__first, __last, __comp, __proj);
  }

  template <forward_range _Rp,
            class _Proj                                                         = identity,
            indirect_strict_weak_order<projected<iterator_t<_Rp>, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr borrowed_iterator_t<_Rp>
  operator()(_Rp&& __r, _Comp __comp = {}, _Proj __proj = {}) const {
    return ranges::__min_element_impl(ranges::begin(__r), ranges::end(__r), __comp, __proj);
  }
};
} // namespace __min_element

inline namespace __cpo {
inline constexpr auto min_element = __min_element::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_MIN_ELEMENT_H
