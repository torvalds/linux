//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_UPPER_BOUND_H
#define _LIBCPP___ALGORITHM_UPPER_BOUND_H

#include <__algorithm/comp.h>
#include <__algorithm/half_positive.h>
#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/advance.h>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/is_constructible.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _AlgPolicy, class _Compare, class _Iter, class _Sent, class _Tp, class _Proj>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Iter
__upper_bound(_Iter __first, _Sent __last, const _Tp& __value, _Compare&& __comp, _Proj&& __proj) {
  auto __len = _IterOps<_AlgPolicy>::distance(__first, __last);
  while (__len != 0) {
    auto __half_len = std::__half_positive(__len);
    auto __mid      = _IterOps<_AlgPolicy>::next(__first, __half_len);
    if (std::__invoke(__comp, __value, std::__invoke(__proj, *__mid)))
      __len = __half_len;
    else {
      __first = ++__mid;
      __len -= __half_len + 1;
    }
  }
  return __first;
}

template <class _ForwardIterator, class _Tp, class _Compare>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
upper_bound(_ForwardIterator __first, _ForwardIterator __last, const _Tp& __value, _Compare __comp) {
  static_assert(is_copy_constructible<_ForwardIterator>::value, "Iterator has to be copy constructible");
  return std::__upper_bound<_ClassicAlgPolicy>(
      std::move(__first), std::move(__last), __value, std::move(__comp), std::__identity());
}

template <class _ForwardIterator, class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
upper_bound(_ForwardIterator __first, _ForwardIterator __last, const _Tp& __value) {
  return std::upper_bound(std::move(__first), std::move(__last), __value, __less<>());
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_UPPER_BOUND_H
