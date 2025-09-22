//===-- CommandObjectThreadTraceExportCTF.h -------------------*- C++ //-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_COMMANDOBJECTTHREADTRACEEXPORTCTF_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_COMMANDOBJECTTHREADTRACEEXPORTCTF_H

#include "TraceExporterCTF.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include <optional>

namespace lldb_private {
namespace ctf {

class CommandObjectThreadTraceExportCTF : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override;

    void OptionParsingStarting(ExecutionContext *execution_context) override;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

    std::optional<size_t> m_thread_index;
    std::string m_file;
  };

  CommandObjectThreadTraceExportCTF(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread trace export ctf",
            "Export a given thread's trace to Chrome Trace Format",
            "thread trace export ctf [<ctf-options>]",
            lldb::eCommandRequiresProcess | lldb::eCommandTryTargetAPILock |
                lldb::eCommandProcessMustBeLaunched |
                lldb::eCommandProcessMustBePaused |
                lldb::eCommandProcessMustBeTraced),
        m_options() {}

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override;

  CommandOptions m_options;
};

} // namespace ctf
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_COMMANDOBJECTTHREADTRACEEXPORTCTF_H
