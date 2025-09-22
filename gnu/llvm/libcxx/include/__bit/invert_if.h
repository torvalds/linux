//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___BIT_INVERT_IF_H
#define _LIBCPP___BIT_INVERT_IF_H

#include <__concepts/arithmetic.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <bool _Invert, class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp __invert_if(_Tp __v) {
  if (_Invert)
    return ~__v;
  return __v;
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___BIT_INVERT_IF_H
