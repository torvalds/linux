//===-- CommandObjectTraceStartIntelPT.h ----------------------*- C++ //-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_COMMANDOBJECTTRACESTARTINTELPT_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_COMMANDOBJECTTRACESTARTINTELPT_H

#include "../../../../source/Commands/CommandObjectTrace.h"
#include "TraceIntelPT.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include <optional>

namespace lldb_private {
namespace trace_intel_pt {

class CommandObjectThreadTraceStartIntelPT
    : public CommandObjectMultipleThreads {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override;

    void OptionParsingStarting(ExecutionContext *execution_context) override;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

    uint64_t m_ipt_trace_size;
    bool m_enable_tsc;
    std::optional<uint64_t> m_psb_period;
  };

  CommandObjectThreadTraceStartIntelPT(TraceIntelPT &trace,
                                       CommandInterpreter &interpreter)
      : CommandObjectMultipleThreads(
            interpreter, "thread trace start",
            "Start tracing one or more threads with intel-pt. "
            "Defaults to the current thread. Thread indices can be "
            "specified as arguments.\n Use the thread-index \"all\" to trace "
            "all threads including future threads.",
            "thread trace start [<thread-index> <thread-index> ...] "
            "[<intel-pt-options>]",
            lldb::eCommandRequiresProcess | lldb::eCommandTryTargetAPILock |
                lldb::eCommandProcessMustBeLaunched |
                lldb::eCommandProcessMustBePaused),
        m_trace(trace), m_options() {}

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecuteOnThreads(Args &command, CommandReturnObject &result,
                          llvm::ArrayRef<lldb::tid_t> tids) override;

  TraceIntelPT &m_trace;
  CommandOptions m_options;
};

class CommandObjectProcessTraceStartIntelPT : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override;

    void OptionParsingStarting(ExecutionContext *execution_context) override;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

    uint64_t m_ipt_trace_size;
    uint64_t m_process_buffer_size_limit;
    bool m_enable_tsc;
    std::optional<uint64_t> m_psb_period;
    bool m_per_cpu_tracing;
    bool m_disable_cgroup_filtering;
  };

  CommandObjectProcessTraceStartIntelPT(TraceIntelPT &trace,
                                        CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process trace start",
            "Start tracing this process with intel-pt, including future "
            "threads. If --per-cpu-tracing is not provided, this traces each "
            "thread independently, thus using a trace buffer per thread. "
            "Threads traced with the \"thread trace start\" command are left "
            "unaffected ant not retraced. This is the recommended option "
            "unless the number of threads is huge. If --per-cpu-tracing is "
            "passed, each cpu core is traced instead of each thread, which "
            "uses a fixed number of trace buffers, but might result in less "
            "data available for less frequent threads.",
            "process trace start [<intel-pt-options>]",
            lldb::eCommandRequiresProcess | lldb::eCommandTryTargetAPILock |
                lldb::eCommandProcessMustBeLaunched |
                lldb::eCommandProcessMustBePaused),
        m_trace(trace), m_options() {}

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override;

  TraceIntelPT &m_trace;
  CommandOptions m_options;
};

namespace ParsingUtils {
/// Convert an integral size expression like 12KiB or 4MB into bytes. The units
/// are taken loosely to help users input sizes into LLDB, e.g. KiB and KB are
/// considered the same (2^20 bytes) for simplicity.
///
/// \param[in] size_expression
///     String expression which is an integral number plus a unit that can be
///     lower or upper case. Supported units: K, KB and KiB for 2^10 bytes; M,
///     MB and MiB for 2^20 bytes; and B for bytes. A single integral number is
///     considered bytes.
/// \return
///   The converted number of bytes or \a std::nullopt if the expression is
///   invalid.
std::optional<uint64_t>
ParseUserFriendlySizeExpression(llvm::StringRef size_expression);
} // namespace ParsingUtils

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_COMMANDOBJECTTRACESTARTINTELPT_H
