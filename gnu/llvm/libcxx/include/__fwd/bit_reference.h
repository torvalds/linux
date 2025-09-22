//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_BIT_REFERENCE_H
#define _LIBCPP___FWD_BIT_REFERENCE_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Cp, bool _IsConst, typename _Cp::__storage_type = 0>
class __bit_iterator;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_BIT_REFERENCE_H
