//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_SHIFT_LEFT_H
#define _LIBCPP___ALGORITHM_SHIFT_LEFT_H

#include <__algorithm/move.h>
#include <__config>
#include <__iterator/iterator_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _ForwardIterator>
inline _LIBCPP_HIDE_FROM_ABI constexpr _ForwardIterator
shift_left(_ForwardIterator __first,
           _ForwardIterator __last,
           typename iterator_traits<_ForwardIterator>::difference_type __n) {
  if (__n == 0) {
    return __last;
  }

  _ForwardIterator __m = __first;
  if constexpr (__has_random_access_iterator_category<_ForwardIterator>::value) {
    if (__n >= __last - __first) {
      return __first;
    }
    __m += __n;
  } else {
    for (; __n > 0; --__n) {
      if (__m == __last) {
        return __first;
      }
      ++__m;
    }
  }
  return std::move(__m, __last, __first);
}

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_SHIFT_LEFT_H
