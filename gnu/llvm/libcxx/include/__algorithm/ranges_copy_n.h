//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_COPY_N_H
#define _LIBCPP___ALGORITHM_RANGES_COPY_N_H

#include <__algorithm/copy.h>
#include <__algorithm/in_out_result.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/ranges_copy.h>
#include <__config>
#include <__functional/identity.h>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/unreachable_sentinel.h>
#include <__iterator/wrap_iter.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {

template <class _Ip, class _Op>
using copy_n_result = in_out_result<_Ip, _Op>;

namespace __copy_n {
struct __fn {
  template <class _InIter, class _DiffType, class _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr static copy_n_result<_InIter, _OutIter>
  __go(_InIter __first, _DiffType __n, _OutIter __result) {
    while (__n != 0) {
      *__result = *__first;
      ++__first;
      ++__result;
      --__n;
    }
    return {std::move(__first), std::move(__result)};
  }

  template <random_access_iterator _InIter, class _DiffType, random_access_iterator _OutIter>
  _LIBCPP_HIDE_FROM_ABI constexpr static copy_n_result<_InIter, _OutIter>
  __go(_InIter __first, _DiffType __n, _OutIter __result) {
    auto __ret = std::__copy<_RangeAlgPolicy>(__first, __first + __n, __result);
    return {__ret.first, __ret.second};
  }

  template <input_iterator _Ip, weakly_incrementable _Op>
    requires indirectly_copyable<_Ip, _Op>
  _LIBCPP_HIDE_FROM_ABI constexpr copy_n_result<_Ip, _Op>
  operator()(_Ip __first, iter_difference_t<_Ip> __n, _Op __result) const {
    return __go(std::move(__first), __n, std::move(__result));
  }
};
} // namespace __copy_n

inline namespace __cpo {
inline constexpr auto copy_n = __copy_n::__fn{};
} // namespace __cpo
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_COPY_N_H
