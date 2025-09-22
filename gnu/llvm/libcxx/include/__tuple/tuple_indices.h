//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TUPLE_MAKE_TUPLE_INDICES_H
#define _LIBCPP___TUPLE_MAKE_TUPLE_INDICES_H

#include <__config>
#include <__utility/integer_sequence.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#ifndef _LIBCPP_CXX03_LANG

_LIBCPP_BEGIN_NAMESPACE_STD

template <size_t...>
struct __tuple_indices {};

template <size_t _Ep, size_t _Sp = 0>
struct __make_tuple_indices {
  static_assert(_Sp <= _Ep, "__make_tuple_indices input error");
  typedef __make_indices_imp<_Ep, _Sp> type;
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_CXX03_LANG

#endif // _LIBCPP___TUPLE_MAKE_TUPLE_INDICES_H
