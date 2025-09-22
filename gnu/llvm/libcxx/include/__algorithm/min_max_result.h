// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_MIN_MAX_RESULT_H
#define _LIBCPP___ALGORITHM_MIN_MAX_RESULT_H

#include <__concepts/convertible_to.h>
#include <__config>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {

template <class _T1>
struct min_max_result {
  _LIBCPP_NO_UNIQUE_ADDRESS _T1 min;
  _LIBCPP_NO_UNIQUE_ADDRESS _T1 max;

  template <class _T2>
    requires convertible_to<const _T1&, _T2>
  _LIBCPP_HIDE_FROM_ABI constexpr operator min_max_result<_T2>() const& {
    return {min, max};
  }

  template <class _T2>
    requires convertible_to<_T1, _T2>
  _LIBCPP_HIDE_FROM_ABI constexpr operator min_max_result<_T2>() && {
    return {std::move(min), std::move(max)};
  }
};

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_MIN_MAX_RESULT_H
