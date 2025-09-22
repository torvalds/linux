//===-- ReportRetriever.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Process.h"

#ifndef LLDB_SOURCE_PLUGINS_INSTRUMENTATIONRUNTIME_UTILITY_REPORTRETRIEVER_H
#define LLDB_SOURCE_PLUGINS_INSTRUMENTATIONRUNTIME_UTILITY_REPORTRETRIEVER_H

namespace lldb_private {

class ReportRetriever {
private:
  static StructuredData::ObjectSP
  RetrieveReportData(const lldb::ProcessSP process_sp);

  static std::string FormatDescription(StructuredData::ObjectSP report);

public:
  static bool NotifyBreakpointHit(lldb::ProcessSP process_sp,
                                  StoppointCallbackContext *context,
                                  lldb::user_id_t break_id,
                                  lldb::user_id_t break_loc_id);

  static Breakpoint *SetupBreakpoint(lldb::ModuleSP, lldb::ProcessSP,
                                     ConstString);
};
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_INSTRUMENTATIONRUNTIME_UTILITY_REPORTRETRIEVER_H
