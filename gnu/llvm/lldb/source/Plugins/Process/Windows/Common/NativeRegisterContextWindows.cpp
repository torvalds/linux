//===-- NativeRegisterContextWindows.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/HostThread.h"
#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/Log.h"

#include "NativeRegisterContextWindows.h"
#include "NativeThreadWindows.h"
#include "ProcessWindowsLog.h"

using namespace lldb;
using namespace lldb_private;

NativeRegisterContextWindows::NativeRegisterContextWindows(
    NativeThreadProtocol &thread, RegisterInfoInterface *reg_info_interface_p)
    : NativeRegisterContextRegisterInfo(thread, reg_info_interface_p) {}

lldb::thread_t NativeRegisterContextWindows::GetThreadHandle() const {
  auto wthread = static_cast<NativeThreadWindows *>(&m_thread);
  return wthread->GetHostThread().GetNativeThread().GetSystemHandle();
}
