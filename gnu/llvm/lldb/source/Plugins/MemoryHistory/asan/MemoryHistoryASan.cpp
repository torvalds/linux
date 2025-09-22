//===-- MemoryHistoryASan.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MemoryHistoryASan.h"

#include "lldb/Target/MemoryHistory.h"

#include "Plugins/Process/Utility/HistoryThread.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/lldb-private.h"

#include <sstream>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(MemoryHistoryASan)

MemoryHistorySP MemoryHistoryASan::CreateInstance(const ProcessSP &process_sp) {
  if (!process_sp.get())
    return nullptr;

  Target &target = process_sp->GetTarget();

  for (ModuleSP module_sp : target.GetImages().Modules()) {
    const Symbol *symbol = module_sp->FindFirstSymbolWithNameAndType(
        ConstString("__asan_get_alloc_stack"), lldb::eSymbolTypeAny);

    if (symbol != nullptr)
      return MemoryHistorySP(new MemoryHistoryASan(process_sp));
  }

  return MemoryHistorySP();
}

void MemoryHistoryASan::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "ASan memory history provider.", CreateInstance);
}

void MemoryHistoryASan::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

MemoryHistoryASan::MemoryHistoryASan(const ProcessSP &process_sp) {
  if (process_sp)
    m_process_wp = process_sp;
}

const char *memory_history_asan_command_prefix = R"(
    extern "C"
    {
        size_t __asan_get_alloc_stack(void *addr, void **trace, size_t size, int *thread_id);
        size_t __asan_get_free_stack(void *addr, void **trace, size_t size, int *thread_id);
    }
)";

const char *memory_history_asan_command_format =
    R"(
    struct {
        void *alloc_trace[256];
        size_t alloc_count;
        int alloc_tid;

        void *free_trace[256];
        size_t free_count;
        int free_tid;
    } t;

    t.alloc_count = __asan_get_alloc_stack((void *)0x%)" PRIx64
    R"(, t.alloc_trace, 256, &t.alloc_tid);
    t.free_count = __asan_get_free_stack((void *)0x%)" PRIx64
    R"(, t.free_trace, 256, &t.free_tid);

    t;
)";

static void CreateHistoryThreadFromValueObject(ProcessSP process_sp,
                                               ValueObjectSP return_value_sp,
                                               const char *type,
                                               const char *thread_name,
                                               HistoryThreads &result) {
  std::string count_path = "." + std::string(type) + "_count";
  std::string tid_path = "." + std::string(type) + "_tid";
  std::string trace_path = "." + std::string(type) + "_trace";

  ValueObjectSP count_sp =
      return_value_sp->GetValueForExpressionPath(count_path.c_str());
  ValueObjectSP tid_sp =
      return_value_sp->GetValueForExpressionPath(tid_path.c_str());

  if (!count_sp || !tid_sp)
    return;

  int count = count_sp->GetValueAsUnsigned(0);
  tid_t tid = tid_sp->GetValueAsUnsigned(0) + 1;

  if (count <= 0)
    return;

  ValueObjectSP trace_sp =
      return_value_sp->GetValueForExpressionPath(trace_path.c_str());

  if (!trace_sp)
    return;

  std::vector<lldb::addr_t> pcs;
  for (int i = 0; i < count; i++) {
    addr_t pc = trace_sp->GetChildAtIndex(i)->GetValueAsUnsigned(0);
    if (pc == 0 || pc == 1 || pc == LLDB_INVALID_ADDRESS)
      continue;
    pcs.push_back(pc);
  }

  // The ASAN runtime already massages the return addresses into call
  // addresses, we don't want LLDB's unwinder to try to locate the previous
  // instruction again as this might lead to us reporting a different line.
  bool pcs_are_call_addresses = true;
  HistoryThread *history_thread =
      new HistoryThread(*process_sp, tid, pcs, pcs_are_call_addresses);
  ThreadSP new_thread_sp(history_thread);
  std::ostringstream thread_name_with_number;
  thread_name_with_number << thread_name << " Thread " << tid;
  history_thread->SetThreadName(thread_name_with_number.str().c_str());
  // Save this in the Process' ExtendedThreadList so a strong pointer retains
  // the object
  process_sp->GetExtendedThreadList().AddThread(new_thread_sp);
  result.push_back(new_thread_sp);
}

HistoryThreads MemoryHistoryASan::GetHistoryThreads(lldb::addr_t address) {
  HistoryThreads result;

  ProcessSP process_sp = m_process_wp.lock();
  if (!process_sp)
    return result;

  ThreadSP thread_sp =
      process_sp->GetThreadList().GetExpressionExecutionThread();
  if (!thread_sp)
    return result;

  StackFrameSP frame_sp =
      thread_sp->GetSelectedFrame(DoNoSelectMostRelevantFrame);
  if (!frame_sp)
    return result;

  ExecutionContext exe_ctx(frame_sp);
  ValueObjectSP return_value_sp;
  StreamString expr;
  Status eval_error;
  expr.Printf(memory_history_asan_command_format, address, address);

  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetTryAllThreads(true);
  options.SetStopOthers(true);
  options.SetIgnoreBreakpoints(true);
  options.SetTimeout(process_sp->GetUtilityExpressionTimeout());
  options.SetPrefix(memory_history_asan_command_prefix);
  options.SetAutoApplyFixIts(false);
  options.SetLanguage(eLanguageTypeObjC_plus_plus);

  ExpressionResults expr_result = UserExpression::Evaluate(
      exe_ctx, options, expr.GetString(), "", return_value_sp, eval_error);
  if (expr_result != eExpressionCompleted) {
    StreamString ss;
    ss << "cannot evaluate AddressSanitizer expression:\n";
    ss << eval_error.AsCString();
    Debugger::ReportWarning(ss.GetString().str(),
                            process_sp->GetTarget().GetDebugger().GetID());
    return result;
  }

  if (!return_value_sp)
    return result;

  CreateHistoryThreadFromValueObject(process_sp, return_value_sp, "free",
                                     "Memory deallocated by", result);
  CreateHistoryThreadFromValueObject(process_sp, return_value_sp, "alloc",
                                     "Memory allocated by", result);

  return result;
}
