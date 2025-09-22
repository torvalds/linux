//===-- ProcessGDBRemoteLog.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProcessGDBRemoteLog.h"
#include "ProcessGDBRemote.h"
#include "llvm/Support/Threading.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

static constexpr Log::Category g_categories[] = {
    {{"async"}, {"log asynchronous activity"}, GDBRLog::Async},
    {{"break"}, {"log breakpoints"}, GDBRLog::Breakpoints},
    {{"comm"}, {"log communication activity"}, GDBRLog::Comm},
    {{"packets"}, {"log gdb remote packets"}, GDBRLog::Packets},
    {{"memory"}, {"log memory reads and writes"}, GDBRLog::Memory},
    {{"data-short"},
     {"log memory bytes for memory reads and writes for short transactions "
      "only"},
     GDBRLog::MemoryDataShort},
    {{"data-long"},
     {"log memory bytes for memory reads and writes for all transactions"},
     GDBRLog::MemoryDataLong},
    {{"process"}, {"log process events and activities"}, GDBRLog::Process},
    {{"step"}, {"log step related activities"}, GDBRLog::Step},
    {{"thread"}, {"log thread events and activities"}, GDBRLog::Thread},
    {{"watch"}, {"log watchpoint related activities"}, GDBRLog::Watchpoints},
};

static Log::Channel g_channel(g_categories, GDBRLog::Packets);

template <> Log::Channel &lldb_private::LogChannelFor<GDBRLog>() {
  return g_channel;
}

void ProcessGDBRemoteLog::Initialize() {
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    Log::Register("gdb-remote", g_channel);
  });
}
