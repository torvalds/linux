//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_EQUAL_RANGE_H
#define _LIBCPP___ALGORITHM_EQUAL_RANGE_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__algorithm/half_positive.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/lower_bound.h>
#include <__algorithm/upper_bound.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/advance.h>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__type_traits/is_callable.h>
#include <__type_traits/is_constructible.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _AlgPolicy, class _Compare, class _Iter, class _Sent, class _Tp, class _Proj>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_Iter, _Iter>
__equal_range(_Iter __first, _Sent __last, const _Tp& __value, _Compare&& __comp, _Proj&& __proj) {
  auto __len  = _IterOps<_AlgPolicy>::distance(__first, __last);
  _Iter __end = _IterOps<_AlgPolicy>::next(__first, __last);
  while (__len != 0) {
    auto __half_len = std::__half_positive(__len);
    _Iter __mid     = _IterOps<_AlgPolicy>::next(__first, __half_len);
    if (std::__invoke(__comp, std::__invoke(__proj, *__mid), __value)) {
      __first = ++__mid;
      __len -= __half_len + 1;
    } else if (std::__invoke(__comp, __value, std::__invoke(__proj, *__mid))) {
      __end = __mid;
      __len = __half_len;
    } else {
      _Iter __mp1 = __mid;
      return pair<_Iter, _Iter>(std::__lower_bound<_AlgPolicy>(__first, __mid, __value, __comp, __proj),
                                std::__upper_bound<_AlgPolicy>(++__mp1, __end, __value, __comp, __proj));
    }
  }
  return pair<_Iter, _Iter>(__first, __first);
}

template <class _ForwardIterator, class _Tp, class _Compare>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_ForwardIterator, _ForwardIterator>
equal_range(_ForwardIterator __first, _ForwardIterator __last, const _Tp& __value, _Compare __comp) {
  static_assert(__is_callable<_Compare, decltype(*__first), const _Tp&>::value, "The comparator has to be callable");
  static_assert(is_copy_constructible<_ForwardIterator>::value, "Iterator has to be copy constructible");
  return std::__equal_range<_ClassicAlgPolicy>(
      std::move(__first),
      std::move(__last),
      __value,
      static_cast<__comp_ref_type<_Compare> >(__comp),
      std::__identity());
}

template <class _ForwardIterator, class _Tp>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_ForwardIterator, _ForwardIterator>
equal_range(_ForwardIterator __first, _ForwardIterator __last, const _Tp& __value) {
  return std::equal_range(std::move(__first), std::move(__last), __value, __less<>());
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_EQUAL_RANGE_H
