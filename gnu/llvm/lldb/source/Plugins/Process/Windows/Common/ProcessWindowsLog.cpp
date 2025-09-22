//===-- ProcessWindowsLog.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProcessWindowsLog.h"

using namespace lldb_private;

static constexpr Log::Category g_categories[] = {
    {{"break"}, {"log breakpoints"}, WindowsLog::Breakpoints},
    {{"event"}, {"log low level debugger events"}, WindowsLog::Event},
    {{"exception"}, {"log exception information"}, WindowsLog::Exception},
    {{"memory"}, {"log memory reads and writes"}, WindowsLog::Memory},
    {{"process"}, {"log process events and activities"}, WindowsLog::Process},
    {{"registers"}, {"log register read/writes"}, WindowsLog::Registers},
    {{"step"}, {"log step related activities"}, WindowsLog::Step},
    {{"thread"}, {"log thread events and activities"}, WindowsLog::Thread},
};

static Log::Channel g_channel(g_categories, WindowsLog::Process);

template <> Log::Channel &lldb_private::LogChannelFor<WindowsLog>() {
  return g_channel;
}

void ProcessWindowsLog::Initialize() {
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() { Log::Register("windows", g_channel); });
}

void ProcessWindowsLog::Terminate() {}









