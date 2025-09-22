// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_IN_OUT_RESULT_H
#define _LIBCPP___ALGORITHM_IN_OUT_RESULT_H

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

template <class _InIter1, class _OutIter1>
struct in_out_result {
  _LIBCPP_NO_UNIQUE_ADDRESS _InIter1 in;
  _LIBCPP_NO_UNIQUE_ADDRESS _OutIter1 out;

  template <class _InIter2, class _OutIter2>
    requires convertible_to<const _InIter1&, _InIter2> && convertible_to<const _OutIter1&, _OutIter2>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_out_result<_InIter2, _OutIter2>() const& {
    return {in, out};
  }

  template <class _InIter2, class _OutIter2>
    requires convertible_to<_InIter1, _InIter2> && convertible_to<_OutIter1, _OutIter2>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_out_result<_InIter2, _OutIter2>() && {
    return {std::move(in), std::move(out)};
  }
};

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_IN_OUT_RESULT_H
