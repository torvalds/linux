//===-- ProcessPOSIXLog.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProcessPOSIXLog.h"

#include "llvm/Support/Threading.h"

using namespace lldb_private;

static constexpr Log::Category g_categories[] = {
    {{"break"}, {"log breakpoints"}, POSIXLog::Breakpoints},
    {{"memory"}, {"log memory reads and writes"}, POSIXLog::Memory},
    {{"process"}, {"log process events and activities"}, POSIXLog::Process},
    {{"ptrace"}, {"log all calls to ptrace"}, POSIXLog::Ptrace},
    {{"registers"}, {"log register read/writes"}, POSIXLog::Registers},
    {{"thread"}, {"log thread events and activities"}, POSIXLog::Thread},
    {{"watch"}, {"log watchpoint related activities"}, POSIXLog::Watchpoints},
};

static Log::Channel g_channel(g_categories, POSIXLog::Process);

template <> Log::Channel &lldb_private::LogChannelFor<POSIXLog>() {
  return g_channel;
}

void ProcessPOSIXLog::Initialize() {
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() { Log::Register("posix", g_channel); });
}
