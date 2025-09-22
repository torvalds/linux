// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_IN_FUN_RESULT_H
#define _LIBCPP___ALGORITHM_IN_FUN_RESULT_H

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
template <class _InIter1, class _Func1>
struct in_fun_result {
  _LIBCPP_NO_UNIQUE_ADDRESS _InIter1 in;
  _LIBCPP_NO_UNIQUE_ADDRESS _Func1 fun;

  template <class _InIter2, class _Func2>
    requires convertible_to<const _InIter1&, _InIter2> && convertible_to<const _Func1&, _Func2>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_fun_result<_InIter2, _Func2>() const& {
    return {in, fun};
  }

  template <class _InIter2, class _Func2>
    requires convertible_to<_InIter1, _InIter2> && convertible_to<_Func1, _Func2>
  _LIBCPP_HIDE_FROM_ABI constexpr operator in_fun_result<_InIter2, _Func2>() && {
    return {std::move(in), std::move(fun)};
  }
};
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_IN_FUN_RESULT_H
