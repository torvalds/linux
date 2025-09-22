// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_FOR_EACH_H
#define _LIBCPP___ALGORITHM_FOR_EACH_H

#include <__algorithm/for_each_segment.h>
#include <__config>
#include <__iterator/segmented_iterator.h>
#include <__ranges/movable_box.h>
#include <__type_traits/enable_if.h>
#include <__utility/in_place.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _InputIterator, class _Function>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Function
for_each(_InputIterator __first, _InputIterator __last, _Function __f) {
  for (; __first != __last; ++__first)
    __f(*__first);
  return __f;
}

// __movable_box is available in C++20, but is actually a copyable-box, so optimization is only correct in C++23
#if _LIBCPP_STD_VER >= 23
template <class _SegmentedIterator, class _Function>
  requires __is_segmented_iterator<_SegmentedIterator>::value
_LIBCPP_HIDE_FROM_ABI constexpr _Function
for_each(_SegmentedIterator __first, _SegmentedIterator __last, _Function __func) {
  ranges::__movable_box<_Function> __wrapped_func(in_place, std::move(__func));
  std::__for_each_segment(__first, __last, [&](auto __lfirst, auto __llast) {
    __wrapped_func =
        ranges::__movable_box<_Function>(in_place, std::for_each(__lfirst, __llast, std::move(*__wrapped_func)));
  });
  return std::move(*__wrapped_func);
}
#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_FOR_EACH_H
