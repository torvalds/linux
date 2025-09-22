//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_COPY_IF_H
#define _LIBCPP___ALGORITHM_RANGES_COPY_IF_H

#include <__algorithm/in_out_result.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
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

template <class _Ip, class _Op>
using copy_if_result = in_out_result<_Ip, _Op>;

namespace __copy_if {
struct __fn {
  template <class _InIter, class _Sent, class _OutIter, class _Proj, class _Pred>
  _LIBCPP_HIDE_FROM_ABI static constexpr copy_if_result<_InIter, _OutIter>
  __copy_if_impl(_InIter __first, _Sent __last, _OutIter __result, _Pred& __pred, _Proj& __proj) {
    for (; __first != __last; ++__first) {
      if (std::invoke(__pred, std::invoke(__proj, *__first))) {
        *__result = *__first;
        ++__result;
      }
    }
    return {std::move(__first), std::move(__result)};
  }

  template <input_iterator _Iter,
            sentinel_for<_Iter> _Sent,
            weakly_incrementable _OutIter,
            class _Proj = identity,
            indirect_unary_predicate<projected<_Iter, _Proj>> _Pred>
    requires indirectly_copyable<_Iter, _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr copy_if_result<_Iter, _OutIter>
  operator()(_Iter __first, _Sent __last, _OutIter __result, _Pred __pred, _Proj __proj = {}) const {
    return __copy_if_impl(std::move(__first), std::move(__last), std::move(__result), __pred, __proj);
  }

  template <input_range _Range,
            weakly_incrementable _OutIter,
            class _Proj = identity,
            indirect_unary_predicate<projected<iterator_t<_Range>, _Proj>> _Pred>
    requires indirectly_copyable<iterator_t<_Range>, _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr copy_if_result<borrowed_iterator_t<_Range>, _OutIter>
  operator()(_Range&& __r, _OutIter __result, _Pred __pred, _Proj __proj = {}) const {
    return __copy_if_impl(ranges::begin(__r), ranges::end(__r), std::move(__result), __pred, __proj);
  }
};
} // namespace __copy_if

inline namespace __cpo {
inline constexpr auto copy_if = __copy_if::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_COPY_IF_H
