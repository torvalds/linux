//===-- InstrumentationRuntimeASanLibsanitizers.h ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUMENTATIONRUNTIME_ASANLIBSANITIZERS_INSTRUMENTATIONRUNTIMEASANLIBSANITIZERS_H
#define LLDB_SOURCE_PLUGINS_INSTRUMENTATIONRUNTIME_ASANLIBSANITIZERS_INSTRUMENTATIONRUNTIMEASANLIBSANITIZERS_H

#include "lldb/Target/InstrumentationRuntime.h"

class InstrumentationRuntimeASanLibsanitizers
    : public lldb_private::InstrumentationRuntime {
public:
  ~InstrumentationRuntimeASanLibsanitizers() override;

  static lldb::InstrumentationRuntimeSP
  CreateInstance(const lldb::ProcessSP &process_sp);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "Libsanitizers-ASan"; }

  static lldb::InstrumentationRuntimeType GetTypeStatic();

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  virtual lldb::InstrumentationRuntimeType GetType() { return GetTypeStatic(); }

private:
  InstrumentationRuntimeASanLibsanitizers(const lldb::ProcessSP &process_sp)
      : lldb_private::InstrumentationRuntime(process_sp) {}

  const lldb_private::RegularExpression &GetPatternForRuntimeLibrary() override;

  bool CheckIfRuntimeIsValid(const lldb::ModuleSP module_sp) override;

  void Activate() override;

  void Deactivate();

  static bool
  NotifyBreakpointHit(void *baton,
                      lldb_private::StoppointCallbackContext *context,
                      lldb::user_id_t break_id, lldb::user_id_t break_loc_id);
};

#endif // LLDB_SOURCE_PLUGINS_INSTRUMENTATIONRUNTIME_ASANLIBSANITIZERS_INSTRUMENTATIONRUNTIMEASANLIBSANITIZERS_H
