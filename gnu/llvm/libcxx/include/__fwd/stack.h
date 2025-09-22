//===---------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_STACK_H
#define _LIBCPP___FWD_STACK_H

#include <__config>
#include <__fwd/deque.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, class _Container = deque<_Tp> >
class _LIBCPP_TEMPLATE_VIS stack;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_STACK_H
