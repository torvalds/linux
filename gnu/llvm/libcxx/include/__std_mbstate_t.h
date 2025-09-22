// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STD_MBSTATE_T_H
#define _LIBCPP___STD_MBSTATE_T_H

#include <__config>
#include <__mbstate_t.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

// The goal of this header is to provide std::mbstate_t without requiring all
// of <cuchar> or <cwchar>.

_LIBCPP_BEGIN_NAMESPACE_STD

using ::mbstate_t _LIBCPP_USING_IF_EXISTS;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___STD_MBSTATE_T_H
