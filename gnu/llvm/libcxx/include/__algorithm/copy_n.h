//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_COPY_N_H
#define _LIBCPP___ALGORITHM_COPY_N_H

#include <__algorithm/copy.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__type_traits/enable_if.h>
#include <__utility/convert_to_integral.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _InputIterator,
          class _Size,
          class _OutputIterator,
          __enable_if_t<__has_input_iterator_category<_InputIterator>::value &&
                            !__has_random_access_iterator_category<_InputIterator>::value,
                        int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
copy_n(_InputIterator __first, _Size __orig_n, _OutputIterator __result) {
  typedef decltype(std::__convert_to_integral(__orig_n)) _IntegralSize;
  _IntegralSize __n = __orig_n;
  if (__n > 0) {
    *__result = *__first;
    ++__result;
    for (--__n; __n > 0; --__n) {
      ++__first;
      *__result = *__first;
      ++__result;
    }
  }
  return __result;
}

template <class _InputIterator,
          class _Size,
          class _OutputIterator,
          __enable_if_t<__has_random_access_iterator_category<_InputIterator>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
copy_n(_InputIterator __first, _Size __orig_n, _OutputIterator __result) {
  typedef typename iterator_traits<_InputIterator>::difference_type difference_type;
  typedef decltype(std::__convert_to_integral(__orig_n)) _IntegralSize;
  _IntegralSize __n = __orig_n;
  return std::copy(__first, __first + difference_type(__n), __result);
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_COPY_N_H
