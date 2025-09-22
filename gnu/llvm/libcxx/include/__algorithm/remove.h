//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_REMOVE_H
#define _LIBCPP___ALGORITHM_REMOVE_H

#include <__algorithm/find.h>
#include <__algorithm/find_if.h>
#include <__config>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _ForwardIterator, class _Tp>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
remove(_ForwardIterator __first, _ForwardIterator __last, const _Tp& __value) {
  __first = std::find(__first, __last, __value);
  if (__first != __last) {
    _ForwardIterator __i = __first;
    while (++__i != __last) {
      if (!(*__i == __value)) {
        *__first = std::move(*__i);
        ++__first;
      }
    }
  }
  return __first;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_REMOVE_H
