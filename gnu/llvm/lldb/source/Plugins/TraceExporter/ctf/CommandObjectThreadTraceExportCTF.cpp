//===-- CommandObjectThreadTraceExportCTF.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectThreadTraceExportCTF.h"

#include "../common/TraceHTR.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Trace.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::ctf;
using namespace llvm;

// CommandObjectThreadTraceExportCTF

#define LLDB_OPTIONS_thread_trace_export_ctf
#include "TraceExporterCTFCommandOptions.inc"

Status CommandObjectThreadTraceExportCTF::CommandOptions::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;
  const int short_option = m_getopt_table[option_idx].val;

  switch (short_option) {
  case 'f': {
    m_file.assign(std::string(option_arg));
    break;
  }
  case 't': {
    int64_t thread_index;
    if (option_arg.empty() || option_arg.getAsInteger(0, thread_index) ||
        thread_index < 0)
      error.SetErrorStringWithFormat("invalid integer value for option '%s'",
                                     option_arg.str().c_str());
    else
      m_thread_index = thread_index;
    break;
  }
  default:
    llvm_unreachable("Unimplemented option");
  }
  return error;
}

void CommandObjectThreadTraceExportCTF::CommandOptions::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_file.clear();
  m_thread_index = std::nullopt;
}

llvm::ArrayRef<OptionDefinition>
CommandObjectThreadTraceExportCTF::CommandOptions::GetDefinitions() {
  return llvm::ArrayRef(g_thread_trace_export_ctf_options);
}

void CommandObjectThreadTraceExportCTF::DoExecute(Args &command,
                                                  CommandReturnObject &result) {
  const TraceSP &trace_sp = m_exe_ctx.GetTargetSP()->GetTrace();
  Process *process = m_exe_ctx.GetProcessPtr();
  Thread *thread = m_options.m_thread_index
                       ? process->GetThreadList()
                             .FindThreadByIndexID(*m_options.m_thread_index)
                             .get()
                       : GetDefaultThread();

  if (thread == nullptr) {
    const uint32_t num_threads = process->GetThreadList().GetSize();
    size_t tid = m_options.m_thread_index.value_or(LLDB_INVALID_THREAD_ID);
    result.AppendErrorWithFormatv(
        "Thread index {0} is out of range (valid values are 1 - {1}).\n", tid,
        num_threads);
  } else {
    auto do_work = [&]() -> Error {
      Expected<TraceCursorSP> cursor = trace_sp->CreateNewCursor(*thread);
      if (!cursor)
        return cursor.takeError();
      TraceHTR htr(*thread, **cursor);
      htr.ExecutePasses();
      return htr.Export(m_options.m_file);
    };

    if (llvm::Error err = do_work()) {
      result.AppendErrorWithFormat("%s\n", toString(std::move(err)).c_str());
    }
  }
}
