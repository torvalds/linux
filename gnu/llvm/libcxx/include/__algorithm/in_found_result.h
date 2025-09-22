// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_IN_FOUND_RESULT_H
#define _LIBCPP___ALGORITHM_IN_FOUND_RESULT_H

#include <__concepts/convertible_to.h>
#include <__config>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
template <class _InIter1>
struct in_found_result {
  _LIBCPP_NO_UNIQUE_ADDRESS _InIter1 in;
  bool found;

  template <class _InIter2>
    requires convertible_to<const _InIter1&, _InIter2>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_found_result<_InIter2>() const& {
    return {in, found};
  }

  template <class _InIter2>
    requires convertible_to<_InIter1, _InIter2>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_found_result<_InIter2>() && {
    return {std::move(in), found};
  }
};
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_IN_FOUND_RESULT_H
