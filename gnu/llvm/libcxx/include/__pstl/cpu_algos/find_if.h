//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___PSTL_CPU_ALGOS_FIND_IF_H
#define _LIBCPP___PSTL_CPU_ALGOS_FIND_IF_H

#include <__algorithm/find_if.h>
#include <__assert>
#include <__atomic/atomic.h>
#include <__config>
#include <__functional/operations.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__pstl/backend_fwd.h>
#include <__pstl/cpu_algos/cpu_traits.h>
#include <__type_traits/is_execution_policy.h>
#include <__utility/move.h>
#include <__utility/pair.h>
#include <cstddef>
#include <optional>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD
namespace __pstl {

template <class _Backend, class _Index, class _Brick, class _Compare>
_LIBCPP_HIDE_FROM_ABI optional<_Index>
__parallel_find(_Index __first, _Index __last, _Brick __f, _Compare __comp, bool __b_first) {
  typedef typename std::iterator_traits<_Index>::difference_type _DifferenceType;
  const _DifferenceType __n      = __last - __first;
  _DifferenceType __initial_dist = __b_first ? __n : -1;
  std::atomic<_DifferenceType> __extremum(__initial_dist);
  // TODO: find out what is better here: parallel_for or parallel_reduce
  auto __res =
      __cpu_traits<_Backend>::__for_each(__first, __last, [__comp, __f, __first, &__extremum](_Index __i, _Index __j) {
        // See "Reducing Contention Through Priority Updates", PPoPP '13, for discussion of
        // why using a shared variable scales fairly well in this situation.
        if (__comp(__i - __first, __extremum)) {
          _Index __result = __f(__i, __j);
          // If not '__last' returned then we found what we want so put this to extremum
          if (__result != __j) {
            const _DifferenceType __k = __result - __first;
            for (_DifferenceType __old = __extremum; __comp(__k, __old); __old = __extremum) {
              __extremum.compare_exchange_weak(__old, __k);
            }
          }
        }
      });
  if (!__res)
    return nullopt;
  return __extremum.load() != __initial_dist ? __first + __extremum.load() : __last;
}

template <class _Backend, class _Index, class _DifferenceType, class _Compare>
_LIBCPP_HIDE_FROM_ABI _Index
__simd_first(_Index __first, _DifferenceType __begin, _DifferenceType __end, _Compare __comp) noexcept {
  // Experiments show good block sizes like this
  const _DifferenceType __block_size                                                = 8;
  alignas(__cpu_traits<_Backend>::__lane_size) _DifferenceType __lane[__block_size] = {0};
  while (__end - __begin >= __block_size) {
    _DifferenceType __found = 0;
    _PSTL_PRAGMA_SIMD_REDUCTION(| : __found) for (_DifferenceType __i = __begin; __i < __begin + __block_size; ++__i) {
      const _DifferenceType __t = __comp(__first, __i);
      __lane[__i - __begin]     = __t;
      __found |= __t;
    }
    if (__found) {
      _DifferenceType __i;
      // This will vectorize
      for (__i = 0; __i < __block_size; ++__i) {
        if (__lane[__i]) {
          break;
        }
      }
      return __first + __begin + __i;
    }
    __begin += __block_size;
  }

  // Keep remainder scalar
  while (__begin != __end) {
    if (__comp(__first, __begin)) {
      return __first + __begin;
    }
    ++__begin;
  }
  return __first + __end;
}

template <class _Backend, class _RawExecutionPolicy>
struct __cpu_parallel_find_if {
  template <class _Policy, class _ForwardIterator, class _Predicate>
  _LIBCPP_HIDE_FROM_ABI optional<_ForwardIterator>
  operator()(_Policy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) const noexcept {
    if constexpr (__is_parallel_execution_policy_v<_RawExecutionPolicy> &&
                  __has_random_access_iterator_category_or_concept<_ForwardIterator>::value) {
      return __pstl::__parallel_find<_Backend>(
          __first,
          __last,
          [&__policy, &__pred](_ForwardIterator __brick_first, _ForwardIterator __brick_last) {
            using _FindIfUnseq = __pstl::__find_if<_Backend, __remove_parallel_policy_t<_RawExecutionPolicy>>;
            auto __res = _FindIfUnseq()(std::__remove_parallel_policy(__policy), __brick_first, __brick_last, __pred);
            _LIBCPP_ASSERT_INTERNAL(__res, "unseq/seq should never try to allocate!");
            return *std::move(__res);
          },
          less<>{},
          true);
    } else if constexpr (__is_unsequenced_execution_policy_v<_RawExecutionPolicy> &&
                         __has_random_access_iterator_category_or_concept<_ForwardIterator>::value) {
      using __diff_t = __iter_diff_t<_ForwardIterator>;
      return __pstl::__simd_first<_Backend>(
          __first, __diff_t(0), __last - __first, [&__pred](_ForwardIterator __iter, __diff_t __i) {
            return __pred(__iter[__i]);
          });
    } else {
      return std::find_if(__first, __last, __pred);
    }
  }
};

} // namespace __pstl
_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___PSTL_CPU_ALGOS_FIND_IF_H
