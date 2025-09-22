//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___PSTL_CPU_ALGOS_ANY_OF_H
#define _LIBCPP___PSTL_CPU_ALGOS_ANY_OF_H

#include <__algorithm/any_of.h>
#include <__assert>
#include <__atomic/atomic.h>
#include <__atomic/memory_order.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__pstl/backend_fwd.h>
#include <__pstl/cpu_algos/cpu_traits.h>
#include <__type_traits/is_execution_policy.h>
#include <__utility/move.h>
#include <__utility/pair.h>
#include <cstdint>
#include <optional>

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD
namespace __pstl {

template <class _Backend, class _Index, class _Brick>
_LIBCPP_HIDE_FROM_ABI optional<bool> __parallel_or(_Index __first, _Index __last, _Brick __f) {
  std::atomic<bool> __found(false);
  auto __ret = __cpu_traits<_Backend>::__for_each(__first, __last, [__f, &__found](_Index __i, _Index __j) {
    if (!__found.load(std::memory_order_relaxed) && __f(__i, __j)) {
      __found.store(true, std::memory_order_relaxed);
      __cpu_traits<_Backend>::__cancel_execution();
    }
  });
  if (!__ret)
    return nullopt;
  return static_cast<bool>(__found);
}

// TODO: check whether __simd_first() can be used here
template <class _Index, class _DifferenceType, class _Pred>
_LIBCPP_HIDE_FROM_ABI bool __simd_or(_Index __first, _DifferenceType __n, _Pred __pred) noexcept {
  _DifferenceType __block_size = 4 < __n ? 4 : __n;
  const _Index __last          = __first + __n;
  while (__last != __first) {
    int32_t __flag = 1;
    _PSTL_PRAGMA_SIMD_REDUCTION(& : __flag)
    for (_DifferenceType __i = 0; __i < __block_size; ++__i)
      if (__pred(*(__first + __i)))
        __flag = 0;
    if (!__flag)
      return true;

    __first += __block_size;
    if (__last - __first >= __block_size << 1) {
      // Double the block _Size.  Any unnecessary iterations can be amortized against work done so far.
      __block_size <<= 1;
    } else {
      __block_size = __last - __first;
    }
  }
  return false;
}

template <class _Backend, class _RawExecutionPolicy>
struct __cpu_parallel_any_of {
  template <class _Policy, class _ForwardIterator, class _Predicate>
  _LIBCPP_HIDE_FROM_ABI optional<bool>
  operator()(_Policy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) const noexcept {
    if constexpr (__is_parallel_execution_policy_v<_RawExecutionPolicy> &&
                  __has_random_access_iterator_category_or_concept<_ForwardIterator>::value) {
      return __pstl::__parallel_or<_Backend>(
          __first, __last, [&__policy, &__pred](_ForwardIterator __brick_first, _ForwardIterator __brick_last) {
            using _AnyOfUnseq = __pstl::__any_of<_Backend, __remove_parallel_policy_t<_RawExecutionPolicy>>;
            auto __res = _AnyOfUnseq()(std::__remove_parallel_policy(__policy), __brick_first, __brick_last, __pred);
            _LIBCPP_ASSERT_INTERNAL(__res, "unseq/seq should never try to allocate!");
            return *std::move(__res);
          });
    } else if constexpr (__is_unsequenced_execution_policy_v<_RawExecutionPolicy> &&
                         __has_random_access_iterator_category_or_concept<_ForwardIterator>::value) {
      return __pstl::__simd_or(__first, __last - __first, __pred);
    } else {
      return std::any_of(__first, __last, __pred);
    }
  }
};

} // namespace __pstl
_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___PSTL_CPU_ALGOS_ANY_OF_H
