//===-- PosixApi.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_POSIXAPI_H
#define LLDB_HOST_POSIXAPI_H

// This file defines platform specific functions, macros, and types necessary
// to provide a minimum level of compatibility across all platforms to rely on
// various posix api functionality.

#if defined(_WIN32)
#include "lldb/Host/windows/PosixApi.h"
#else
#include <unistd.h>
#include <csignal>
#endif

#endif
