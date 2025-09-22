//===-- InstrumentationRuntimeASan.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InstrumentationRuntimeASan.h"

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

LLDB_PLUGIN_DEFINE(InstrumentationRuntimeASan)

lldb::InstrumentationRuntimeSP
InstrumentationRuntimeASan::CreateInstance(const lldb::ProcessSP &process_sp) {
  return InstrumentationRuntimeSP(new InstrumentationRuntimeASan(process_sp));
}

void InstrumentationRuntimeASan::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "AddressSanitizer instrumentation runtime plugin.",
      CreateInstance, GetTypeStatic);
}

void InstrumentationRuntimeASan::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb::InstrumentationRuntimeType InstrumentationRuntimeASan::GetTypeStatic() {
  return eInstrumentationRuntimeTypeAddressSanitizer;
}

InstrumentationRuntimeASan::~InstrumentationRuntimeASan() { Deactivate(); }

const RegularExpression &
InstrumentationRuntimeASan::GetPatternForRuntimeLibrary() {
  // FIXME: This shouldn't include the "dylib" suffix.
  static RegularExpression regex(
      llvm::StringRef("libclang_rt.asan_(.*)_dynamic\\.dylib"));
  return regex;
}

bool InstrumentationRuntimeASan::CheckIfRuntimeIsValid(
    const lldb::ModuleSP module_sp) {
  const Symbol *symbol = module_sp->FindFirstSymbolWithNameAndType(
      ConstString("__asan_get_alloc_stack"), lldb::eSymbolTypeAny);

  return symbol != nullptr;
}

bool InstrumentationRuntimeASan::NotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton)
    return false;

  InstrumentationRuntimeASan *const instance =
      static_cast<InstrumentationRuntimeASan *>(baton);

  ProcessSP process_sp = instance->GetProcessSP();

  return ReportRetriever::NotifyBreakpointHit(process_sp, context, break_id,
                                              break_loc_id);
}

void InstrumentationRuntimeASan::Activate() {
  if (IsActive())
    return;

  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return;

  Breakpoint *breakpoint = ReportRetriever::SetupBreakpoint(
      GetRuntimeModuleSP(), process_sp, ConstString("_ZN6__asanL7AsanDieEv"));

  if (!breakpoint)
    return;

  const bool sync = false;

  breakpoint->SetCallback(InstrumentationRuntimeASan::NotifyBreakpointHit, this,
                          sync);
  breakpoint->SetBreakpointKind("address-sanitizer-report");
  SetBreakpointID(breakpoint->GetID());

  SetActive(true);
}

void InstrumentationRuntimeASan::Deactivate() {
  SetActive(false);

  if (GetBreakpointID() == LLDB_INVALID_BREAK_ID)
    return;

  if (ProcessSP process_sp = GetProcessSP()) {
    process_sp->GetTarget().RemoveBreakpointByID(GetBreakpointID());
    SetBreakpointID(LLDB_INVALID_BREAK_ID);
  }
}
