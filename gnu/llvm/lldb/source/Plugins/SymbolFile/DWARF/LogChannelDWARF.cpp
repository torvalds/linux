//===-- LogChannelDWARF.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LogChannelDWARF.h"

using namespace lldb_private;

static constexpr Log::Category g_categories[] = {
    {{"comp"},
     {"log struct/union/class type completions"},
     DWARFLog::TypeCompletion},
    {{"info"}, {"log the parsing of .debug_info"}, DWARFLog::DebugInfo},
    {{"line"}, {"log the parsing of .debug_line"}, DWARFLog::DebugLine},
    {{"lookups"},
     {"log any lookups that happen by name, regex, or address"},
     DWARFLog::Lookups},
    {{"map"},
     {"log insertions of object files into DWARF debug maps"},
     DWARFLog::DebugMap},
    {{"split"}, {"log split DWARF related activities"}, DWARFLog::SplitDwarf},
};

static Log::Channel g_channel(g_categories, DWARFLog::DebugInfo);

template <> Log::Channel &lldb_private::LogChannelFor<DWARFLog>() {
  return g_channel;
}

void LogChannelDWARF::Initialize() {
  Log::Register("dwarf", g_channel);
}

void LogChannelDWARF::Terminate() { Log::Unregister("dwarf"); }
