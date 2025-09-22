//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_FILL_N_H
#define _LIBCPP___ALGORITHM_RANGES_FILL_N_H

#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __fill_n {
struct __fn {
  template <class _Type, output_iterator<const _Type&> _Iter>
  _LIBCPP_HIDE_FROM_ABI constexpr _Iter
  operator()(_Iter __first, iter_difference_t<_Iter> __n, const _Type& __value) const {
    for (; __n != 0; --__n) {
      *__first = __value;
      ++__first;
    }
    return __first;
  }
};
} // namespace __fill_n

inline namespace __cpo {
inline constexpr auto fill_n = __fill_n::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_FILL_N_H
