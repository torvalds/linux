//===-- InstrumentationRuntimeASanLibsanitizers.cpp -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InstrumentationRuntimeASanLibsanitizers.h"

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/RegularExpression.h"

#include "Plugins/InstrumentationRuntime/Utility/ReportRetriever.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(InstrumentationRuntimeASanLibsanitizers)

lldb::InstrumentationRuntimeSP
InstrumentationRuntimeASanLibsanitizers::CreateInstance(
    const lldb::ProcessSP &process_sp) {
  return InstrumentationRuntimeSP(
      new InstrumentationRuntimeASanLibsanitizers(process_sp));
}

void InstrumentationRuntimeASanLibsanitizers::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(),
      "AddressSanitizer instrumentation runtime plugin for Libsanitizers.",
      CreateInstance, GetTypeStatic);
}

void InstrumentationRuntimeASanLibsanitizers::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb::InstrumentationRuntimeType
InstrumentationRuntimeASanLibsanitizers::GetTypeStatic() {
  return eInstrumentationRuntimeTypeLibsanitizersAsan;
}

InstrumentationRuntimeASanLibsanitizers::
    ~InstrumentationRuntimeASanLibsanitizers() {
  Deactivate();
}

const RegularExpression &
InstrumentationRuntimeASanLibsanitizers::GetPatternForRuntimeLibrary() {
  static RegularExpression regex(
      llvm::StringRef("libsystem_sanitizers\\.dylib"));
  return regex;
}

bool InstrumentationRuntimeASanLibsanitizers::CheckIfRuntimeIsValid(
    const lldb::ModuleSP module_sp) {
  const Symbol *symbol = module_sp->FindFirstSymbolWithNameAndType(
      ConstString("__asan_abi_init"), lldb::eSymbolTypeAny);

  return symbol != nullptr;
}

bool InstrumentationRuntimeASanLibsanitizers::NotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton)
    return false;

  InstrumentationRuntimeASanLibsanitizers *const instance =
      static_cast<InstrumentationRuntimeASanLibsanitizers *>(baton);

  ProcessSP process_sp = instance->GetProcessSP();

  return ReportRetriever::NotifyBreakpointHit(process_sp, context, break_id,
                                              break_loc_id);
}

void InstrumentationRuntimeASanLibsanitizers::Activate() {
  if (IsActive())
    return;

  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return;

  lldb::ModuleSP module_sp = GetRuntimeModuleSP();

  Breakpoint *breakpoint = ReportRetriever::SetupBreakpoint(
      module_sp, process_sp, ConstString("sanitizers_address_on_report"));

  if (!breakpoint) {
    breakpoint = ReportRetriever::SetupBreakpoint(
        module_sp, process_sp,
        ConstString("_Z22raise_sanitizers_error23sanitizer_error_context"));
  }

  if (!breakpoint)
    return;

  const bool sync = false;

  breakpoint->SetCallback(
      InstrumentationRuntimeASanLibsanitizers::NotifyBreakpointHit, this, sync);
  breakpoint->SetBreakpointKind("address-sanitizer-report");
  SetBreakpointID(breakpoint->GetID());

  SetActive(true);
}

void InstrumentationRuntimeASanLibsanitizers::Deactivate() {
  SetActive(false);

  if (GetBreakpointID() == LLDB_INVALID_BREAK_ID)
    return;

  if (ProcessSP process_sp = GetProcessSP()) {
    process_sp->GetTarget().RemoveBreakpointByID(GetBreakpointID());
    SetBreakpointID(LLDB_INVALID_BREAK_ID);
  }
}
