// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_PERMUTABLE_H
#define _LIBCPP___ITERATOR_PERMUTABLE_H

#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iter_swap.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _Iterator>
concept permutable =
    forward_iterator<_Iterator> && indirectly_movable_storable<_Iterator, _Iterator> &&
    indirectly_swappable<_Iterator, _Iterator>;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_PERMUTABLE_H
