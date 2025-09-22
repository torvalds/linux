// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_FILE_TIME_TYPE_H
#define _LIBCPP___FILESYSTEM_FILE_TIME_TYPE_H

#include <__chrono/file_clock.h>
#include <__chrono/time_point.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

typedef chrono::time_point<_FilesystemClock> file_time_type;

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___FILESYSTEM_FILE_TIME_TYPE_H
