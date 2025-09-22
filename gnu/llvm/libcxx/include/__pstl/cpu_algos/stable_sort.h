//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___PSTL_CPU_ALGOS_STABLE_SORT_H
#define _LIBCPP___PSTL_CPU_ALGOS_STABLE_SORT_H

#include <__algorithm/stable_sort.h>
#include <__config>
#include <__pstl/backend_fwd.h>
#include <__pstl/cpu_algos/cpu_traits.h>
#include <__type_traits/is_execution_policy.h>
#include <__utility/empty.h>
#include <optional>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD
namespace __pstl {

template <class _Backend, class _RawExecutionPolicy>
struct __cpu_parallel_stable_sort {
  template <class _Policy, class _RandomAccessIterator, class _Comp>
  _LIBCPP_HIDE_FROM_ABI optional<__empty>
  operator()(_Policy&&, _RandomAccessIterator __first, _RandomAccessIterator __last, _Comp __comp) const noexcept {
    if constexpr (__is_parallel_execution_policy_v<_RawExecutionPolicy>) {
      return __cpu_traits<_Backend>::__stable_sort(
          __first, __last, __comp, [](_RandomAccessIterator __g_first, _RandomAccessIterator __g_last, _Comp __g_comp) {
            std::stable_sort(__g_first, __g_last, __g_comp);
          });
    } else {
      std::stable_sort(__first, __last, __comp);
      return __empty{};
    }
  }
};

} // namespace __pstl
_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___PSTL_CPU_ALGOS_STABLE_SORT_H
