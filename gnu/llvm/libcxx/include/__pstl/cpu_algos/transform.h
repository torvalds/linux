//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___PSTL_CPU_ALGOS_TRANSFORM_H
#define _LIBCPP___PSTL_CPU_ALGOS_TRANSFORM_H

#include <__algorithm/transform.h>
#include <__assert>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__pstl/backend_fwd.h>
#include <__pstl/cpu_algos/cpu_traits.h>
#include <__type_traits/is_execution_policy.h>
#include <__utility/move.h>
#include <optional>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD
namespace __pstl {

template <class _Iterator1, class _DifferenceType, class _Iterator2, class _Function>
_LIBCPP_HIDE_FROM_ABI _Iterator2
__simd_transform(_Iterator1 __first1, _DifferenceType __n, _Iterator2 __first2, _Function __f) noexcept {
  _PSTL_PRAGMA_SIMD
  for (_DifferenceType __i = 0; __i < __n; ++__i)
    __f(__first1[__i], __first2[__i]);
  return __first2 + __n;
}

template <class _Iterator1, class _DifferenceType, class _Iterator2, class _Iterator3, class _Function>
_LIBCPP_HIDE_FROM_ABI _Iterator3 __simd_transform(
    _Iterator1 __first1, _DifferenceType __n, _Iterator2 __first2, _Iterator3 __first3, _Function __f) noexcept {
  _PSTL_PRAGMA_SIMD
  for (_DifferenceType __i = 0; __i < __n; ++__i)
    __f(__first1[__i], __first2[__i], __first3[__i]);
  return __first3 + __n;
}

template <class _Backend, class _RawExecutionPolicy>
struct __cpu_parallel_transform {
  template <class _Policy, class _ForwardIterator, class _ForwardOutIterator, class _UnaryOperation>
  _LIBCPP_HIDE_FROM_ABI optional<_ForwardOutIterator>
  operator()(_Policy&& __policy,
             _ForwardIterator __first,
             _ForwardIterator __last,
             _ForwardOutIterator __result,
             _UnaryOperation __op) const noexcept {
    if constexpr (__is_parallel_execution_policy_v<_RawExecutionPolicy> &&
                  __has_random_access_iterator_category_or_concept<_ForwardIterator>::value &&
                  __has_random_access_iterator_category_or_concept<_ForwardOutIterator>::value) {
      __cpu_traits<_Backend>::__for_each(
          __first,
          __last,
          [&__policy, __op, __first, __result](_ForwardIterator __brick_first, _ForwardIterator __brick_last) {
            using _TransformUnseq = __pstl::__transform<_Backend, __remove_parallel_policy_t<_RawExecutionPolicy>>;
            auto __res            = _TransformUnseq()(
                std::__remove_parallel_policy(__policy),
                __brick_first,
                __brick_last,
                __result + (__brick_first - __first),
                __op);
            _LIBCPP_ASSERT_INTERNAL(__res, "unseq/seq should never try to allocate!");
            return *std::move(__res);
          });
      return __result + (__last - __first);
    } else if constexpr (__is_unsequenced_execution_policy_v<_RawExecutionPolicy> &&
                         __has_random_access_iterator_category_or_concept<_ForwardIterator>::value &&
                         __has_random_access_iterator_category_or_concept<_ForwardOutIterator>::value) {
      return __pstl::__simd_transform(
          __first,
          __last - __first,
          __result,
          [&](__iter_reference<_ForwardIterator> __in_value, __iter_reference<_ForwardOutIterator> __out_value) {
            __out_value = __op(__in_value);
          });
    } else {
      return std::transform(__first, __last, __result, __op);
    }
  }
};

template <class _Backend, class _RawExecutionPolicy>
struct __cpu_parallel_transform_binary {
  template <class _Policy,
            class _ForwardIterator1,
            class _ForwardIterator2,
            class _ForwardOutIterator,
            class _BinaryOperation>
  _LIBCPP_HIDE_FROM_ABI optional<_ForwardOutIterator>
  operator()(_Policy&& __policy,
             _ForwardIterator1 __first1,
             _ForwardIterator1 __last1,
             _ForwardIterator2 __first2,
             _ForwardOutIterator __result,
             _BinaryOperation __op) const noexcept {
    if constexpr (__is_parallel_execution_policy_v<_RawExecutionPolicy> &&
                  __has_random_access_iterator_category_or_concept<_ForwardIterator1>::value &&
                  __has_random_access_iterator_category_or_concept<_ForwardIterator2>::value &&
                  __has_random_access_iterator_category_or_concept<_ForwardOutIterator>::value) {
      auto __res = __cpu_traits<_Backend>::__for_each(
          __first1,
          __last1,
          [&__policy, __op, __first1, __first2, __result](
              _ForwardIterator1 __brick_first, _ForwardIterator1 __brick_last) {
            using _TransformBinaryUnseq =
                __pstl::__transform_binary<_Backend, __remove_parallel_policy_t<_RawExecutionPolicy>>;
            return _TransformBinaryUnseq()(
                std::__remove_parallel_policy(__policy),
                __brick_first,
                __brick_last,
                __first2 + (__brick_first - __first1),
                __result + (__brick_first - __first1),
                __op);
          });
      if (!__res)
        return nullopt;
      return __result + (__last1 - __first1);
    } else if constexpr (__is_unsequenced_execution_policy_v<_RawExecutionPolicy> &&
                         __has_random_access_iterator_category_or_concept<_ForwardIterator1>::value &&
                         __has_random_access_iterator_category_or_concept<_ForwardIterator2>::value &&
                         __has_random_access_iterator_category_or_concept<_ForwardOutIterator>::value) {
      return __pstl::__simd_transform(
          __first1,
          __last1 - __first1,
          __first2,
          __result,
          [&](__iter_reference<_ForwardIterator1> __in1,
              __iter_reference<_ForwardIterator2> __in2,
              __iter_reference<_ForwardOutIterator> __out_value) { __out_value = __op(__in1, __in2); });
    } else {
      return std::transform(__first1, __last1, __first2, __result, __op);
    }
  }
};

} // namespace __pstl
_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___PSTL_CPU_ALGOS_TRANSFORM_H
