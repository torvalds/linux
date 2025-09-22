//===-- ProcessGDBRemoteLog.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_PROCESSGDBREMOTELOG_H
#define LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_PROCESSGDBREMOTELOG_H

#include "lldb/Utility/Log.h"
#include "llvm/ADT/BitmaskEnum.h"

namespace lldb_private {
namespace process_gdb_remote {

enum class GDBRLog : Log::MaskType {
  Async = Log::ChannelFlag<0>,
  Breakpoints = Log::ChannelFlag<1>,
  Comm = Log::ChannelFlag<2>,
  Memory = Log::ChannelFlag<3>,          // Log memory reads/writes calls
  MemoryDataLong = Log::ChannelFlag<4>,  // Log all memory reads/writes bytes
  MemoryDataShort = Log::ChannelFlag<5>, // Log short memory reads/writes bytes
  Packets = Log::ChannelFlag<6>,
  Process = Log::ChannelFlag<7>,
  Step = Log::ChannelFlag<8>,
  Thread = Log::ChannelFlag<9>,
  Watchpoints = Log::ChannelFlag<10>,
  LLVM_MARK_AS_BITMASK_ENUM(Watchpoints)
};
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

class ProcessGDBRemoteLog {
public:
  static void Initialize();
};

} // namespace process_gdb_remote

template <> Log::Channel &LogChannelFor<process_gdb_remote::GDBRLog>();

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_PROCESSGDBREMOTELOG_H
