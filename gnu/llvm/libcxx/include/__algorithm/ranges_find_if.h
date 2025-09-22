//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_FIND_IF_H
#define _LIBCPP___ALGORITHM_RANGES_FIND_IF_H

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

template <class _Ip, class _Sp, class _Pred, class _Proj>
_LIBCPP_HIDE_FROM_ABI constexpr _Ip __find_if_impl(_Ip __first, _Sp __last, _Pred& __pred, _Proj& __proj) {
  for (; __first != __last; ++__first) {
    if (std::invoke(__pred, std::invoke(__proj, *__first)))
      break;
  }
  return __first;
}

namespace __find_if {
struct __fn {
  template <input_iterator _Ip,
            sentinel_for<_Ip> _Sp,
            class _Proj = identity,
            indirect_unary_predicate<projected<_Ip, _Proj>> _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Ip
  operator()(_Ip __first, _Sp __last, _Pred __pred, _Proj __proj = {}) const {
    return ranges::__find_if_impl(std::move(__first), std::move(__last), __pred, __proj);
  }

  template <input_range _Rp, class _Proj = identity, indirect_unary_predicate<projected<iterator_t<_Rp>, _Proj>> _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr borrowed_iterator_t<_Rp>
  operator()(_Rp&& __r, _Pred __pred, _Proj __proj = {}) const {
    return ranges::__find_if_impl(ranges::begin(__r), ranges::end(__r), __pred, __proj);
  }
};
} // namespace __find_if

inline namespace __cpo {
inline constexpr auto find_if = __find_if::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_FIND_IF_H
