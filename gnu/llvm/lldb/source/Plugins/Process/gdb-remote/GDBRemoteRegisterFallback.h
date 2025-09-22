//===-- GDBRemoteRegisterFallback.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTEREGISTERFALLBACK_H
#define LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTEREGISTERFALLBACK_H

#include <vector>

#include "lldb/Target/DynamicRegisterInfo.h"
#include "lldb/Utility/ArchSpec.h"

namespace lldb_private {
namespace process_gdb_remote {

std::vector<DynamicRegisterInfo::Register>
GetFallbackRegisters(const ArchSpec &arch_to_use);

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTEREGISTERFALLBACK_H
