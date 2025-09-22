//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_SAMPLE_H
#define _LIBCPP___ALGORITHM_SAMPLE_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/min.h>
#include <__assert>
#include <__config>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__random/uniform_int_distribution.h>
#include <__type_traits/common_type.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _AlgPolicy,
          class _PopulationIterator,
          class _PopulationSentinel,
          class _SampleIterator,
          class _Distance,
          class _UniformRandomNumberGenerator>
_LIBCPP_HIDE_FROM_ABI _SampleIterator __sample(
    _PopulationIterator __first,
    _PopulationSentinel __last,
    _SampleIterator __output_iter,
    _Distance __n,
    _UniformRandomNumberGenerator& __g,
    input_iterator_tag) {
  _Distance __k = 0;
  for (; __first != __last && __k < __n; ++__first, (void)++__k)
    __output_iter[__k] = *__first;
  _Distance __sz = __k;
  for (; __first != __last; ++__first, (void)++__k) {
    _Distance __r = uniform_int_distribution<_Distance>(0, __k)(__g);
    if (__r < __sz)
      __output_iter[__r] = *__first;
  }
  return __output_iter + std::min(__n, __k);
}

template <class _AlgPolicy,
          class _PopulationIterator,
          class _PopulationSentinel,
          class _SampleIterator,
          class _Distance,
          class _UniformRandomNumberGenerator>
_LIBCPP_HIDE_FROM_ABI _SampleIterator __sample(
    _PopulationIterator __first,
    _PopulationSentinel __last,
    _SampleIterator __output_iter,
    _Distance __n,
    _UniformRandomNumberGenerator& __g,
    forward_iterator_tag) {
  _Distance __unsampled_sz = _IterOps<_AlgPolicy>::distance(__first, __last);
  for (__n = std::min(__n, __unsampled_sz); __n != 0; ++__first) {
    _Distance __r = uniform_int_distribution<_Distance>(0, --__unsampled_sz)(__g);
    if (__r < __n) {
      *__output_iter++ = *__first;
      --__n;
    }
  }
  return __output_iter;
}

template <class _AlgPolicy,
          class _PopulationIterator,
          class _PopulationSentinel,
          class _SampleIterator,
          class _Distance,
          class _UniformRandomNumberGenerator>
_LIBCPP_HIDE_FROM_ABI _SampleIterator __sample(
    _PopulationIterator __first,
    _PopulationSentinel __last,
    _SampleIterator __output_iter,
    _Distance __n,
    _UniformRandomNumberGenerator& __g) {
  _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__n >= 0, "N must be a positive number.");

  using _PopIterCategory = typename _IterOps<_AlgPolicy>::template __iterator_category<_PopulationIterator>;
  using _Difference      = typename _IterOps<_AlgPolicy>::template __difference_type<_PopulationIterator>;
  using _CommonType      = typename common_type<_Distance, _Difference>::type;

  return std::__sample<_AlgPolicy>(
      std::move(__first), std::move(__last), std::move(__output_iter), _CommonType(__n), __g, _PopIterCategory());
}

#if _LIBCPP_STD_VER >= 17
template <class _PopulationIterator, class _SampleIterator, class _Distance, class _UniformRandomNumberGenerator>
inline _LIBCPP_HIDE_FROM_ABI _SampleIterator
sample(_PopulationIterator __first,
       _PopulationIterator __last,
       _SampleIterator __output_iter,
       _Distance __n,
       _UniformRandomNumberGenerator&& __g) {
  static_assert(__has_forward_iterator_category<_PopulationIterator>::value ||
                    __has_random_access_iterator_category<_SampleIterator>::value,
                "SampleIterator must meet the requirements of RandomAccessIterator");

  return std::__sample<_ClassicAlgPolicy>(std::move(__first), std::move(__last), std::move(__output_iter), __n, __g);
}

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_SAMPLE_H
