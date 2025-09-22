//===-- NativeRegisterContextFreeBSD.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeRegisterContextFreeBSD.h"

#include "Plugins/Process/FreeBSD/NativeProcessFreeBSD.h"

#include "lldb/Host/common/NativeProcessProtocol.h"

using namespace lldb_private;
using namespace lldb_private::process_freebsd;

// clang-format off
#include <sys/types.h>
#include <sys/ptrace.h>
// clang-format on

NativeProcessFreeBSD &NativeRegisterContextFreeBSD::GetProcess() {
  return static_cast<NativeProcessFreeBSD &>(m_thread.GetProcess());
}

::pid_t NativeRegisterContextFreeBSD::GetProcessPid() {
  return GetProcess().GetID();
}
