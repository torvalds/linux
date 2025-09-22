// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_IN_IN_RESULT_H
#define _LIBCPP___ALGORITHM_IN_IN_RESULT_H

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

template <class _InIter1, class _InIter2>
struct in_in_result {
  _LIBCPP_NO_UNIQUE_ADDRESS _InIter1 in1;
  _LIBCPP_NO_UNIQUE_ADDRESS _InIter2 in2;

  template <class _InIter3, class _InIter4>
    requires convertible_to<const _InIter1&, _InIter3> && convertible_to<const _InIter2&, _InIter4>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_in_result<_InIter3, _InIter4>() const& {
    return {in1, in2};
  }

  template <class _InIter3, class _InIter4>
    requires convertible_to<_InIter1, _InIter3> && convertible_to<_InIter2, _InIter4>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_in_result<_InIter3, _InIter4>() && {
    return {std::move(in1), std::move(in2)};
  }
};

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_IN_IN_RESULT_H
