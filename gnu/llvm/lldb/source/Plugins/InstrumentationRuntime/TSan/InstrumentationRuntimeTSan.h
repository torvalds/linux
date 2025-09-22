//===-- InstrumentationRuntimeTSan.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUMENTATIONRUNTIME_TSAN_INSTRUMENTATIONRUNTIMETSAN_H
#define LLDB_SOURCE_PLUGINS_INSTRUMENTATIONRUNTIME_TSAN_INSTRUMENTATIONRUNTIMETSAN_H

#include "lldb/Target/ABI.h"
#include "lldb/Target/InstrumentationRuntime.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class InstrumentationRuntimeTSan : public lldb_private::InstrumentationRuntime {
public:
  ~InstrumentationRuntimeTSan() override;

  static lldb::InstrumentationRuntimeSP
  CreateInstance(const lldb::ProcessSP &process_sp);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "ThreadSanitizer"; }

  static lldb::InstrumentationRuntimeType GetTypeStatic();

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  virtual lldb::InstrumentationRuntimeType GetType() { return GetTypeStatic(); }

  lldb::ThreadCollectionSP
  GetBacktracesFromExtendedStopInfo(StructuredData::ObjectSP info) override;

private:
  InstrumentationRuntimeTSan(const lldb::ProcessSP &process_sp)
      : lldb_private::InstrumentationRuntime(process_sp) {}

  const RegularExpression &GetPatternForRuntimeLibrary() override;

  bool CheckIfRuntimeIsValid(const lldb::ModuleSP module_sp) override;

  void Activate() override;

  void Deactivate();

  static bool NotifyBreakpointHit(void *baton,
                                  StoppointCallbackContext *context,
                                  lldb::user_id_t break_id,
                                  lldb::user_id_t break_loc_id);

  StructuredData::ObjectSP RetrieveReportData(ExecutionContextRef exe_ctx_ref);

  std::string FormatDescription(StructuredData::ObjectSP report);

  std::string GenerateSummary(StructuredData::ObjectSP report);

  lldb::addr_t GetMainRacyAddress(StructuredData::ObjectSP report);

  std::string GetLocationDescription(StructuredData::ObjectSP report,
                                     lldb::addr_t &global_addr,
                                     std::string &global_name,
                                     std::string &filename, uint32_t &line);

  lldb::addr_t GetFirstNonInternalFramePc(StructuredData::ObjectSP trace,
                                          bool skip_one_frame = false);
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_INSTRUMENTATIONRUNTIME_TSAN_INSTRUMENTATIONRUNTIMETSAN_H
