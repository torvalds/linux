// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_IN_OUT_OUT_RESULT_H
#define _LIBCPP___ALGORITHM_IN_OUT_OUT_RESULT_H

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
template <class _InIter1, class _OutIter1, class _OutIter2>
struct in_out_out_result {
  _LIBCPP_NO_UNIQUE_ADDRESS _InIter1 in;
  _LIBCPP_NO_UNIQUE_ADDRESS _OutIter1 out1;
  _LIBCPP_NO_UNIQUE_ADDRESS _OutIter2 out2;

  template <class _InIter2, class _OutIter3, class _OutIter4>
    requires convertible_to<const _InIter1&, _InIter2> && convertible_to<const _OutIter1&, _OutIter3> &&
             convertible_to<const _OutIter2&, _OutIter4>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_out_out_result<_InIter2, _OutIter3, _OutIter4>() const& {
    return {in, out1, out2};
  }

  template <class _InIter2, class _OutIter3, class _OutIter4>
    requires convertible_to<_InIter1, _InIter2> && convertible_to<_OutIter1, _OutIter3> &&
             convertible_to<_OutIter2, _OutIter4>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_out_out_result<_InIter2, _OutIter3, _OutIter4>() && {
    return {std::move(in), std::move(out1), std::move(out2)};
  }
};
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_IN_OUT_OUT_RESULT_H
