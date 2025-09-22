//===-- windows.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_lldb_windows_h_
#define LLDB_lldb_windows_h_

#define NTDDI_VERSION NTDDI_VISTA
#undef _WIN32_WINNT // undef a previous definition to avoid warning
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#undef NOMINMAX // undef a previous definition to avoid warning
#define NOMINMAX
#include <windows.h>
#undef CreateProcess
#undef GetMessage
#undef GetUserName
#undef LoadImage
#undef Yield
#undef far
#undef near
#undef FAR
#undef NEAR
#define FAR
#define NEAR

#endif // LLDB_lldb_windows_h_
