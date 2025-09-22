//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_RELATION_H
#define _LIBCPP___CONCEPTS_RELATION_H

#include <__concepts/predicate.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concept.relation]

template <class _Rp, class _Tp, class _Up>
concept relation =
    predicate<_Rp, _Tp, _Tp> && predicate<_Rp, _Up, _Up> && predicate<_Rp, _Tp, _Up> && predicate<_Rp, _Up, _Tp>;

// [concept.equiv]

template <class _Rp, class _Tp, class _Up>
concept equivalence_relation = relation<_Rp, _Tp, _Up>;

// [concept.strictweakorder]

template <class _Rp, class _Tp, class _Up>
concept strict_weak_order = relation<_Rp, _Tp, _Up>;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_RELATION_H
