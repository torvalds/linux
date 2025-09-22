//===-- InstrumentationRuntimeMainThreadChecker.cpp -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InstrumentationRuntimeMainThreadChecker.h"

#include "Plugins/Process/Utility/HistoryThread.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/InstrumentationRuntimeStopInfo.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegularExpression.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(InstrumentationRuntimeMainThreadChecker)

InstrumentationRuntimeMainThreadChecker::
    ~InstrumentationRuntimeMainThreadChecker() {
  Deactivate();
}

lldb::InstrumentationRuntimeSP
InstrumentationRuntimeMainThreadChecker::CreateInstance(
    const lldb::ProcessSP &process_sp) {
  return InstrumentationRuntimeSP(
      new InstrumentationRuntimeMainThreadChecker(process_sp));
}

void InstrumentationRuntimeMainThreadChecker::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(),
      "MainThreadChecker instrumentation runtime plugin.", CreateInstance,
      GetTypeStatic);
}

void InstrumentationRuntimeMainThreadChecker::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb::InstrumentationRuntimeType
InstrumentationRuntimeMainThreadChecker::GetTypeStatic() {
  return eInstrumentationRuntimeTypeMainThreadChecker;
}

const RegularExpression &
InstrumentationRuntimeMainThreadChecker::GetPatternForRuntimeLibrary() {
  static RegularExpression regex(llvm::StringRef("libMainThreadChecker.dylib"));
  return regex;
}

bool InstrumentationRuntimeMainThreadChecker::CheckIfRuntimeIsValid(
    const lldb::ModuleSP module_sp) {
  static ConstString test_sym("__main_thread_checker_on_report");
  const Symbol *symbol =
      module_sp->FindFirstSymbolWithNameAndType(test_sym, lldb::eSymbolTypeAny);
  return symbol != nullptr;
}

StructuredData::ObjectSP
InstrumentationRuntimeMainThreadChecker::RetrieveReportData(
    ExecutionContextRef exe_ctx_ref) {
  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return StructuredData::ObjectSP();

  ThreadSP thread_sp = exe_ctx_ref.GetThreadSP();
  StackFrameSP frame_sp =
      thread_sp->GetSelectedFrame(DoNoSelectMostRelevantFrame);
  ModuleSP runtime_module_sp = GetRuntimeModuleSP();
  Target &target = process_sp->GetTarget();

  if (!frame_sp)
    return StructuredData::ObjectSP();

  RegisterContextSP regctx_sp = frame_sp->GetRegisterContext();
  if (!regctx_sp)
    return StructuredData::ObjectSP();

  const RegisterInfo *reginfo = regctx_sp->GetRegisterInfoByName("arg1");
  if (!reginfo)
    return StructuredData::ObjectSP();

  uint64_t apiname_ptr = regctx_sp->ReadRegisterAsUnsigned(reginfo, 0);
  if (!apiname_ptr)
    return StructuredData::ObjectSP();

  std::string apiName;
  Status read_error;
  target.ReadCStringFromMemory(apiname_ptr, apiName, read_error);
  if (read_error.Fail())
    return StructuredData::ObjectSP();

  std::string className;
  std::string selector;
  if (apiName.substr(0, 2) == "-[") {
    size_t spacePos = apiName.find(' ');
    if (spacePos != std::string::npos) {
      className = apiName.substr(2, spacePos - 2);
      selector = apiName.substr(spacePos + 1, apiName.length() - spacePos - 2);
    }
  }

  // Gather the PCs of the user frames in the backtrace.
  StructuredData::Array *trace = new StructuredData::Array();
  auto trace_sp = StructuredData::ObjectSP(trace);
  StackFrameSP responsible_frame;
  for (unsigned I = 0; I < thread_sp->GetStackFrameCount(); ++I) {
    StackFrameSP frame = thread_sp->GetStackFrameAtIndex(I);
    Address addr = frame->GetFrameCodeAddressForSymbolication();
    if (addr.GetModule() == runtime_module_sp) // Skip PCs from the runtime.
      continue;

    // The first non-runtime frame is responsible for the bug.
    if (!responsible_frame)
      responsible_frame = frame;

    lldb::addr_t PC = addr.GetLoadAddress(&target);
    trace->AddIntegerItem(PC);
  }

  auto *d = new StructuredData::Dictionary();
  auto dict_sp = StructuredData::ObjectSP(d);
  d->AddStringItem("instrumentation_class", "MainThreadChecker");
  d->AddStringItem("api_name", apiName);
  d->AddStringItem("class_name", className);
  d->AddStringItem("selector", selector);
  d->AddStringItem("description",
                   apiName + " must be used from main thread only");
  d->AddIntegerItem("tid", thread_sp->GetIndexID());
  d->AddItem("trace", trace_sp);
  return dict_sp;
}

bool InstrumentationRuntimeMainThreadChecker::NotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton)
    return false; ///< false => resume execution.

  InstrumentationRuntimeMainThreadChecker *const instance =
      static_cast<InstrumentationRuntimeMainThreadChecker *>(baton);

  ProcessSP process_sp = instance->GetProcessSP();
  ThreadSP thread_sp = context->exe_ctx_ref.GetThreadSP();
  if (!process_sp || !thread_sp ||
      process_sp != context->exe_ctx_ref.GetProcessSP())
    return false;

  if (process_sp->GetModIDRef().IsLastResumeForUserExpression())
    return false;

  StructuredData::ObjectSP report =
      instance->RetrieveReportData(context->exe_ctx_ref);

  if (report) {
    std::string description = std::string(report->GetAsDictionary()
                                              ->GetValueForKey("description")
                                              ->GetAsString()
                                              ->GetValue());
    thread_sp->SetStopInfo(
        InstrumentationRuntimeStopInfo::CreateStopReasonWithInstrumentationData(
            *thread_sp, description, report));
    return true;
  }

  return false;
}

void InstrumentationRuntimeMainThreadChecker::Activate() {
  if (IsActive())
    return;

  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return;

  ModuleSP runtime_module_sp = GetRuntimeModuleSP();

  ConstString symbol_name("__main_thread_checker_on_report");
  const Symbol *symbol = runtime_module_sp->FindFirstSymbolWithNameAndType(
      symbol_name, eSymbolTypeCode);

  if (symbol == nullptr)
    return;

  if (!symbol->ValueIsAddress() || !symbol->GetAddressRef().IsValid())
    return;

  Target &target = process_sp->GetTarget();
  addr_t symbol_address = symbol->GetAddressRef().GetOpcodeLoadAddress(&target);

  if (symbol_address == LLDB_INVALID_ADDRESS)
    return;

  Breakpoint *breakpoint =
      process_sp->GetTarget()
          .CreateBreakpoint(symbol_address, /*internal=*/true,
                            /*hardware=*/false)
          .get();
  const bool sync = false;
  breakpoint->SetCallback(
      InstrumentationRuntimeMainThreadChecker::NotifyBreakpointHit, this, sync);
  breakpoint->SetBreakpointKind("main-thread-checker-report");
  SetBreakpointID(breakpoint->GetID());

  SetActive(true);
}

void InstrumentationRuntimeMainThreadChecker::Deactivate() {
  SetActive(false);

  auto BID = GetBreakpointID();
  if (BID == LLDB_INVALID_BREAK_ID)
    return;

  if (ProcessSP process_sp = GetProcessSP()) {
    process_sp->GetTarget().RemoveBreakpointByID(BID);
    SetBreakpointID(LLDB_INVALID_BREAK_ID);
  }
}

lldb::ThreadCollectionSP
InstrumentationRuntimeMainThreadChecker::GetBacktracesFromExtendedStopInfo(
    StructuredData::ObjectSP info) {
  ThreadCollectionSP threads;
  threads = std::make_shared<ThreadCollection>();

  ProcessSP process_sp = GetProcessSP();

  if (info->GetObjectForDotSeparatedPath("instrumentation_class")
          ->GetStringValue() != "MainThreadChecker")
    return threads;

  std::vector<lldb::addr_t> PCs;
  auto trace = info->GetObjectForDotSeparatedPath("trace")->GetAsArray();
  trace->ForEach([&PCs](StructuredData::Object *PC) -> bool {
    PCs.push_back(PC->GetUnsignedIntegerValue());
    return true;
  });

  if (PCs.empty())
    return threads;

  StructuredData::ObjectSP thread_id_obj =
      info->GetObjectForDotSeparatedPath("tid");
  tid_t tid = thread_id_obj ? thread_id_obj->GetUnsignedIntegerValue() : 0;

  // We gather symbolication addresses above, so no need for HistoryThread to
  // try to infer the call addresses.
  bool pcs_are_call_addresses = true;
  ThreadSP new_thread_sp = std::make_shared<HistoryThread>(
      *process_sp, tid, PCs, pcs_are_call_addresses);

  // Save this in the Process' ExtendedThreadList so a strong pointer retains
  // the object
  process_sp->GetExtendedThreadList().AddThread(new_thread_sp);
  threads->AddThread(new_thread_sp);

  return threads;
}
