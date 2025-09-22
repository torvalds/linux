//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_MINMAX_ELEMENT_H
#define _LIBCPP___ALGORITHM_RANGES_MINMAX_ELEMENT_H

#include <__algorithm/min_max_result.h>
#include <__algorithm/minmax_element.h>
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

template <class _T1>
using minmax_element_result = min_max_result<_T1>;

namespace __minmax_element {
struct __fn {
  template <forward_iterator _Ip,
            sentinel_for<_Ip> _Sp,
            class _Proj                                             = identity,
            indirect_strict_weak_order<projected<_Ip, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr ranges::minmax_element_result<_Ip>
  operator()(_Ip __first, _Sp __last, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __ret = std::__minmax_element_impl(std::move(__first), std::move(__last), __comp, __proj);
    return {__ret.first, __ret.second};
  }

  template <forward_range _Rp,
            class _Proj                                                         = identity,
            indirect_strict_weak_order<projected<iterator_t<_Rp>, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr ranges::minmax_element_result<borrowed_iterator_t<_Rp>>
  operator()(_Rp&& __r, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __ret = std::__minmax_element_impl(ranges::begin(__r), ranges::end(__r), __comp, __proj);
    return {__ret.first, __ret.second};
  }
};
} // namespace __minmax_element

inline namespace __cpo {
inline constexpr auto minmax_element = __minmax_element::__fn{};
} // namespace __cpo

} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_MINMAX_H
