//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_UNIQUE_H
#define _LIBCPP___ALGORITHM_UNIQUE_H

#include <__algorithm/adjacent_find.h>
#include <__algorithm/comp.h>
#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// unique

template <class _AlgPolicy, class _Iter, class _Sent, class _BinaryPredicate>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 std::pair<_Iter, _Iter>
__unique(_Iter __first, _Sent __last, _BinaryPredicate&& __pred) {
  __first = std::__adjacent_find(__first, __last, __pred);
  if (__first != __last) {
    // ...  a  a  ?  ...
    //      f     i
    _Iter __i = __first;
    for (++__i; ++__i != __last;)
      if (!__pred(*__first, *__i))
        *++__first = _IterOps<_AlgPolicy>::__iter_move(__i);
    ++__first;
    return std::pair<_Iter, _Iter>(std::move(__first), std::move(__i));
  }
  return std::pair<_Iter, _Iter>(__first, __first);
}

template <class _ForwardIterator, class _BinaryPredicate>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
unique(_ForwardIterator __first, _ForwardIterator __last, _BinaryPredicate __pred) {
  return std::__unique<_ClassicAlgPolicy>(std::move(__first), std::move(__last), __pred).first;
}

template <class _ForwardIterator>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
unique(_ForwardIterator __first, _ForwardIterator __last) {
  return std::unique(__first, __last, __equal_to());
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_UNIQUE_H
