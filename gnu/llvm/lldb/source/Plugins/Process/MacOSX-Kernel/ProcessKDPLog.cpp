//===-- ProcessKDPLog.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProcessKDPLog.h"

using namespace lldb_private;

static constexpr Log::Category g_categories[] = {
    {{"async"}, {"log asynchronous activity"}, KDPLog::Async},
    {{"break"}, {"log breakpoints"}, KDPLog::Breakpoints},
    {{"comm"}, {"log communication activity"}, KDPLog::Comm},
    {{"data-long"},
     {"log memory bytes for memory reads and writes for all transactions"},
     KDPLog::MemoryDataLong},
    {{"data-short"},
     {"log memory bytes for memory reads and writes for short transactions "
      "only"},
     KDPLog::MemoryDataShort},
    {{"memory"}, {"log memory reads and writes"}, KDPLog::Memory},
    {{"packets"}, {"log gdb remote packets"}, KDPLog::Packets},
    {{"process"}, {"log process events and activities"}, KDPLog::Process},
    {{"step"}, {"log step related activities"}, KDPLog::Step},
    {{"thread"}, {"log thread events and activities"}, KDPLog::Thread},
    {{"watch"}, {"log watchpoint related activities"}, KDPLog::Watchpoints},
};

static Log::Channel g_channel(g_categories, KDPLog::Packets);

template <> Log::Channel &lldb_private::LogChannelFor<KDPLog>() {
  return g_channel;
}

void ProcessKDPLog::Initialize() { Log::Register("kdp-remote", g_channel); }
