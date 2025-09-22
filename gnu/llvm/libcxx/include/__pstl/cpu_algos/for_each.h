//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___PSTL_CPU_ALGOS_FOR_EACH_H
#define _LIBCPP___PSTL_CPU_ALGOS_FOR_EACH_H

#include <__algorithm/for_each.h>
#include <__assert>
#include <__config>
#include <__iterator/concepts.h>
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

template <class _Iterator, class _DifferenceType, class _Function>
_LIBCPP_HIDE_FROM_ABI _Iterator __simd_for_each(_Iterator __first, _DifferenceType __n, _Function __f) noexcept {
  _PSTL_PRAGMA_SIMD
  for (_DifferenceType __i = 0; __i < __n; ++__i)
    __f(__first[__i]);

  return __first + __n;
}

template <class _Backend, class _RawExecutionPolicy>
struct __cpu_parallel_for_each {
  template <class _Policy, class _ForwardIterator, class _Function>
  _LIBCPP_HIDE_FROM_ABI optional<__empty>
  operator()(_Policy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Function __func) const noexcept {
    if constexpr (__is_parallel_execution_policy_v<_RawExecutionPolicy> &&
                  __has_random_access_iterator_category_or_concept<_ForwardIterator>::value) {
      return __cpu_traits<_Backend>::__for_each(
          __first, __last, [&__policy, __func](_ForwardIterator __brick_first, _ForwardIterator __brick_last) {
            using _ForEachUnseq = __pstl::__for_each<_Backend, __remove_parallel_policy_t<_RawExecutionPolicy>>;
            [[maybe_unused]] auto __res =
                _ForEachUnseq()(std::__remove_parallel_policy(__policy), __brick_first, __brick_last, __func);
            _LIBCPP_ASSERT_INTERNAL(__res, "unseq/seq should never try to allocate!");
          });
    } else if constexpr (__is_unsequenced_execution_policy_v<_RawExecutionPolicy> &&
                         __has_random_access_iterator_category_or_concept<_ForwardIterator>::value) {
      __pstl::__simd_for_each(__first, __last - __first, __func);
      return __empty{};
    } else {
      std::for_each(__first, __last, __func);
      return __empty{};
    }
  }
};

} // namespace __pstl
_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___PSTL_CPU_ALGOS_FOR_EACH_H
