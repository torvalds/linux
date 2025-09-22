// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_INDIRECTLY_COMPARABLE_H
#define _LIBCPP___ITERATOR_INDIRECTLY_COMPARABLE_H

#include <__config>
#include <__functional/identity.h>
#include <__iterator/concepts.h>
#include <__iterator/projected.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _I1, class _I2, class _Rp, class _P1 = identity, class _P2 = identity>
concept indirectly_comparable = indirect_binary_predicate<_Rp, projected<_I1, _P1>, projected<_I2, _P2>>;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_INDIRECTLY_COMPARABLE_H
