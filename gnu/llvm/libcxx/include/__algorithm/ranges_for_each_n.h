//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_FOR_EACH_N_H
#define _LIBCPP___ALGORITHM_RANGES_FOR_EACH_N_H

#include <__algorithm/in_fun_result.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/projected.h>
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

template <class _Iter, class _Func>
using for_each_n_result = in_fun_result<_Iter, _Func>;

namespace __for_each_n {
struct __fn {
  template <input_iterator _Iter, class _Proj = identity, indirectly_unary_invocable<projected<_Iter, _Proj>> _Func>
  _LIBCPP_HIDE_FROM_ABI constexpr for_each_n_result<_Iter, _Func>
  operator()(_Iter __first, iter_difference_t<_Iter> __count, _Func __func, _Proj __proj = {}) const {
    while (__count-- > 0) {
      std::invoke(__func, std::invoke(__proj, *__first));
      ++__first;
    }
    return {std::move(__first), std::move(__func)};
  }
};
} // namespace __for_each_n

inline namespace __cpo {
inline constexpr auto for_each_n = __for_each_n::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_FOR_EACH_N_H
