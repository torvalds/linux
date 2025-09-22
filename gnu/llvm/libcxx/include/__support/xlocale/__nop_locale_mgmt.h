// -*- C++ -*-
//===-----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___SUPPORT_XLOCALE_NOP_LOCALE_MGMT_H
#define _LIBCPP___SUPPORT_XLOCALE_NOP_LOCALE_MGMT_H

#include <__config>

// Patch over lack of extended locale support
typedef void* locale_t;

inline _LIBCPP_HIDE_FROM_ABI locale_t duplocale(locale_t) { return NULL; }

inline _LIBCPP_HIDE_FROM_ABI void freelocale(locale_t) {}

inline _LIBCPP_HIDE_FROM_ABI locale_t newlocale(int, const char*, locale_t) { return NULL; }

inline _LIBCPP_HIDE_FROM_ABI locale_t uselocale(locale_t) { return NULL; }

#define LC_COLLATE_MASK (1 << LC_COLLATE)
#define LC_CTYPE_MASK (1 << LC_CTYPE)
#define LC_MESSAGES_MASK (1 << LC_MESSAGES)
#define LC_MONETARY_MASK (1 << LC_MONETARY)
#define LC_NUMERIC_MASK (1 << LC_NUMERIC)
#define LC_TIME_MASK (1 << LC_TIME)
#define LC_ALL_MASK                                                                                                    \
  (LC_COLLATE_MASK | LC_CTYPE_MASK | LC_MONETARY_MASK | LC_NUMERIC_MASK | LC_TIME_MASK | LC_MESSAGES_MASK)

#endif // _LIBCPP___SUPPORT_XLOCALE_NOP_LOCALE_MGMT_H
