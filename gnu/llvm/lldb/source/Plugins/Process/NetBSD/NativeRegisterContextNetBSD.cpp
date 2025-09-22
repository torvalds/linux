//===-- NativeRegisterContextNetBSD.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeRegisterContextNetBSD.h"

#include "Plugins/Process/NetBSD/NativeProcessNetBSD.h"

#include "lldb/Host/common/NativeProcessProtocol.h"

using namespace lldb_private;
using namespace lldb_private::process_netbsd;

// clang-format off
#include <sys/types.h>
#include <sys/ptrace.h>
// clang-format on

Status NativeRegisterContextNetBSD::DoRegisterSet(int ptrace_req, void *buf) {
  return NativeProcessNetBSD::PtraceWrapper(ptrace_req, GetProcessPid(), buf,
                                            m_thread.GetID());
}

NativeProcessNetBSD &NativeRegisterContextNetBSD::GetProcess() {
  return static_cast<NativeProcessNetBSD &>(m_thread.GetProcess());
}

::pid_t NativeRegisterContextNetBSD::GetProcessPid() {
  return GetProcess().GetID();
}
