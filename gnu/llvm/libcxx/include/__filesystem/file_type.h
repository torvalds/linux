// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_FILE_TYPE_H
#define _LIBCPP___FILESYSTEM_FILE_TYPE_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

// On Windows, the library never identifies files as block, character, fifo
// or socket.
enum class file_type : signed char {
  none      = 0,
  not_found = -1,
  regular   = 1,
  directory = 2,
  symlink   = 3,
  block     = 4,
  character = 5,
  fifo      = 6,
  socket    = 7,
  unknown   = 8
};

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___FILESYSTEM_FILE_TYPE_H
