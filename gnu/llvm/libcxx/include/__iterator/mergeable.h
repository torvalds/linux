// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_MERGEABLE_H
#define _LIBCPP___ITERATOR_MERGEABLE_H

#include <__config>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/projected.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _Input1,
          class _Input2,
          class _Output,
          class _Comp  = ranges::less,
          class _Proj1 = identity,
          class _Proj2 = identity>
concept mergeable =
    input_iterator<_Input1> && input_iterator<_Input2> && weakly_incrementable<_Output> &&
    indirectly_copyable<_Input1, _Output> && indirectly_copyable<_Input2, _Output> &&
    indirect_strict_weak_order<_Comp, projected<_Input1, _Proj1>, projected<_Input2, _Proj2>>;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_MERGEABLE_H
