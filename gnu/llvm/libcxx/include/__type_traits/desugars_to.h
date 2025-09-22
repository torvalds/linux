//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_DESUGARS_TO_H
#define _LIBCPP___TYPE_TRAITS_DESUGARS_TO_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// Tags to represent the canonical operations
struct __equal_tag {};
struct __plus_tag {};
struct __less_tag {};

// This class template is used to determine whether an operation "desugars"
// (or boils down) to a given canonical operation.
//
// For example, `std::equal_to<>`, our internal `std::__equal_to` helper and
// `ranges::equal_to` are all just fancy ways of representing a transparent
// equality operation, so they all desugar to `__equal_tag`.
//
// This is useful to optimize some functions in cases where we know e.g. the
// predicate being passed is actually going to call a builtin operator, or has
// some specific semantics.
template <class _CanonicalTag, class _Operation, class... _Args>
inline const bool __desugars_to_v = false;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_DESUGARS_TO_H
