//===---------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_QUEUE_H
#define _LIBCPP___FWD_QUEUE_H

#include <__config>
#include <__functional/operations.h>
#include <__fwd/deque.h>
#include <__fwd/vector.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, class _Container = deque<_Tp> >
class _LIBCPP_TEMPLATE_VIS queue;

template <class _Tp, class _Container = vector<_Tp>, class _Compare = less<typename _Container::value_type> >
class _LIBCPP_TEMPLATE_VIS priority_queue;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_QUEUE_H
