// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_UNREACHABLE_SENTINEL_H
#define _LIBCPP___ITERATOR_UNREACHABLE_SENTINEL_H

#include <__config>
#include <__iterator/concepts.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

struct unreachable_sentinel_t {
  template <weakly_incrementable _Iter>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(unreachable_sentinel_t, const _Iter&) noexcept {
    return false;
  }
};

inline constexpr unreachable_sentinel_t unreachable_sentinel{};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_UNREACHABLE_SENTINEL_H
