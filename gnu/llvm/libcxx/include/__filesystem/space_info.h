// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_SPACE_INFO_H
#define _LIBCPP___FILESYSTEM_SPACE_INFO_H

#include <__config>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

struct _LIBCPP_EXPORTED_FROM_ABI space_info {
  uintmax_t capacity;
  uintmax_t free;
  uintmax_t available;

#  if _LIBCPP_STD_VER >= 20
  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const space_info&, const space_info&) = default;
#  endif
};

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___FILESYSTEM_SPACE_INFO_H
