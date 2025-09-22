// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_PROJECTED_H
#define _LIBCPP___ITERATOR_PROJECTED_H

#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h> // iter_difference_t
#include <__type_traits/remove_cvref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _It, class _Proj>
struct __projected_impl {
  struct __type {
    using value_type = remove_cvref_t<indirect_result_t<_Proj&, _It>>;
    indirect_result_t<_Proj&, _It> operator*() const; // not defined
  };
};

template <weakly_incrementable _It, class _Proj>
struct __projected_impl<_It, _Proj> {
  struct __type {
    using value_type      = remove_cvref_t<indirect_result_t<_Proj&, _It>>;
    using difference_type = iter_difference_t<_It>;
    indirect_result_t<_Proj&, _It> operator*() const; // not defined
  };
};

// Note that we implement std::projected in a way that satisfies P2538R1 even in standard
// modes before C++26 to avoid breaking the ABI between standard modes (even though ABI
// breaks with std::projected are expected to have essentially no impact).
template <indirectly_readable _It, indirectly_regular_unary_invocable<_It> _Proj>
using projected = typename __projected_impl<_It, _Proj>::__type;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_PROJECTED_H
