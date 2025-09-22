//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___PSTL_CPU_ALGOS_FILL_H
#define _LIBCPP___PSTL_CPU_ALGOS_FILL_H

#include <__algorithm/fill.h>
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

template <class _Index, class _DifferenceType, class _Tp>
_LIBCPP_HIDE_FROM_ABI _Index __simd_fill_n(_Index __first, _DifferenceType __n, const _Tp& __value) noexcept {
  _PSTL_USE_NONTEMPORAL_STORES_IF_ALLOWED
  _PSTL_PRAGMA_SIMD
  for (_DifferenceType __i = 0; __i < __n; ++__i)
    __first[__i] = __value;
  return __first + __n;
}

template <class _Backend, class _RawExecutionPolicy>
struct __cpu_parallel_fill {
  template <class _Policy, class _ForwardIterator, class _Tp>
  _LIBCPP_HIDE_FROM_ABI optional<__empty>
  operator()(_Policy&& __policy, _ForwardIterator __first, _ForwardIterator __last, const _Tp& __value) const noexcept {
    if constexpr (__is_parallel_execution_policy_v<_RawExecutionPolicy> &&
                  __has_random_access_iterator_category_or_concept<_ForwardIterator>::value) {
      return __cpu_traits<_Backend>::__for_each(
          __first, __last, [&__policy, &__value](_ForwardIterator __brick_first, _ForwardIterator __brick_last) {
            using _FillUnseq = __pstl::__fill<_Backend, __remove_parallel_policy_t<_RawExecutionPolicy>>;
            [[maybe_unused]] auto __res =
                _FillUnseq()(std::__remove_parallel_policy(__policy), __brick_first, __brick_last, __value);
            _LIBCPP_ASSERT_INTERNAL(__res, "unseq/seq should never try to allocate!");
          });
    } else if constexpr (__is_unsequenced_execution_policy_v<_RawExecutionPolicy> &&
                         __has_random_access_iterator_category_or_concept<_ForwardIterator>::value) {
      __pstl::__simd_fill_n(__first, __last - __first, __value);
      return __empty{};
    } else {
      std::fill(__first, __last, __value);
      return __empty{};
    }
  }
};

} // namespace __pstl
_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___PSTL_CPU_ALGOS_FILL_H
