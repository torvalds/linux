// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_AUTO_CAST_H
#define _LIBCPP___UTILITY_AUTO_CAST_H

#include <__config>
#include <__type_traits/decay.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#define _LIBCPP_AUTO_CAST(expr) static_cast<::std::__decay_t<decltype((expr))> >(expr)

#endif // _LIBCPP___UTILITY_AUTO_CAST_H
