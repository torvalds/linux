//===-- CommandObjectTraceStartIntelPT.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectTraceStartIntelPT.h"
#include "TraceIntelPT.h"
#include "TraceIntelPTConstants.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Trace.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::trace_intel_pt;
using namespace llvm;

// CommandObjectThreadTraceStartIntelPT

#define LLDB_OPTIONS_thread_trace_start_intel_pt
#include "TraceIntelPTCommandOptions.inc"

Status CommandObjectThreadTraceStartIntelPT::CommandOptions::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;
  const int short_option = m_getopt_table[option_idx].val;

  switch (short_option) {
  case 's': {
    if (std::optional<uint64_t> bytes =
            ParsingUtils::ParseUserFriendlySizeExpression(option_arg))
      m_ipt_trace_size = *bytes;
    else
      error.SetErrorStringWithFormat("invalid bytes expression for '%s'",
                                     option_arg.str().c_str());
    break;
  }
  case 't': {
    m_enable_tsc = true;
    break;
  }
  case 'p': {
    int64_t psb_period;
    if (option_arg.empty() || option_arg.getAsInteger(0, psb_period) ||
        psb_period < 0)
      error.SetErrorStringWithFormat("invalid integer value for option '%s'",
                                     option_arg.str().c_str());
    else
      m_psb_period = psb_period;
    break;
  }
  default:
    llvm_unreachable("Unimplemented option");
  }
  return error;
}

void CommandObjectThreadTraceStartIntelPT::CommandOptions::
    OptionParsingStarting(ExecutionContext *execution_context) {
  m_ipt_trace_size = kDefaultIptTraceSize;
  m_enable_tsc = kDefaultEnableTscValue;
  m_psb_period = kDefaultPsbPeriod;
}

llvm::ArrayRef<OptionDefinition>
CommandObjectThreadTraceStartIntelPT::CommandOptions::GetDefinitions() {
  return llvm::ArrayRef(g_thread_trace_start_intel_pt_options);
}

bool CommandObjectThreadTraceStartIntelPT::DoExecuteOnThreads(
    Args &command, CommandReturnObject &result,
    llvm::ArrayRef<lldb::tid_t> tids) {
  if (Error err = m_trace.Start(tids, m_options.m_ipt_trace_size,
                                m_options.m_enable_tsc, m_options.m_psb_period))
    result.SetError(Status(std::move(err)));
  else
    result.SetStatus(eReturnStatusSuccessFinishResult);

  return result.Succeeded();
}

/// CommandObjectProcessTraceStartIntelPT

#define LLDB_OPTIONS_process_trace_start_intel_pt
#include "TraceIntelPTCommandOptions.inc"

Status CommandObjectProcessTraceStartIntelPT::CommandOptions::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;
  const int short_option = m_getopt_table[option_idx].val;

  switch (short_option) {
  case 's': {
    if (std::optional<uint64_t> bytes =
            ParsingUtils::ParseUserFriendlySizeExpression(option_arg))
      m_ipt_trace_size = *bytes;
    else
      error.SetErrorStringWithFormat("invalid bytes expression for '%s'",
                                     option_arg.str().c_str());
    break;
  }
  case 'l': {
    if (std::optional<uint64_t> bytes =
            ParsingUtils::ParseUserFriendlySizeExpression(option_arg))
      m_process_buffer_size_limit = *bytes;
    else
      error.SetErrorStringWithFormat("invalid bytes expression for '%s'",
                                     option_arg.str().c_str());
    break;
  }
  case 't': {
    m_enable_tsc = true;
    break;
  }
  case 'c': {
    m_per_cpu_tracing = true;
    break;
  }
  case 'd': {
    m_disable_cgroup_filtering = true;
    break;
  }
  case 'p': {
    int64_t psb_period;
    if (option_arg.empty() || option_arg.getAsInteger(0, psb_period) ||
        psb_period < 0)
      error.SetErrorStringWithFormat("invalid integer value for option '%s'",
                                     option_arg.str().c_str());
    else
      m_psb_period = psb_period;
    break;
  }
  default:
    llvm_unreachable("Unimplemented option");
  }
  return error;
}

void CommandObjectProcessTraceStartIntelPT::CommandOptions::
    OptionParsingStarting(ExecutionContext *execution_context) {
  m_ipt_trace_size = kDefaultIptTraceSize;
  m_process_buffer_size_limit = kDefaultProcessBufferSizeLimit;
  m_enable_tsc = kDefaultEnableTscValue;
  m_psb_period = kDefaultPsbPeriod;
  m_per_cpu_tracing = kDefaultPerCpuTracing;
  m_disable_cgroup_filtering = kDefaultDisableCgroupFiltering;
}

llvm::ArrayRef<OptionDefinition>
CommandObjectProcessTraceStartIntelPT::CommandOptions::GetDefinitions() {
  return llvm::ArrayRef(g_process_trace_start_intel_pt_options);
}

void CommandObjectProcessTraceStartIntelPT::DoExecute(
    Args &command, CommandReturnObject &result) {
  if (Error err = m_trace.Start(
          m_options.m_ipt_trace_size, m_options.m_process_buffer_size_limit,
          m_options.m_enable_tsc, m_options.m_psb_period,
          m_options.m_per_cpu_tracing, m_options.m_disable_cgroup_filtering))
    result.SetError(Status(std::move(err)));
  else
    result.SetStatus(eReturnStatusSuccessFinishResult);
}

std::optional<uint64_t>
ParsingUtils::ParseUserFriendlySizeExpression(llvm::StringRef size_expression) {
  if (size_expression.empty()) {
    return std::nullopt;
  }
  const uint64_t kBytesMultiplier = 1;
  const uint64_t kKibiBytesMultiplier = 1024;
  const uint64_t kMebiBytesMultiplier = 1024 * 1024;

  DenseMap<StringRef, uint64_t> multipliers = {
      {"mib", kMebiBytesMultiplier}, {"mb", kMebiBytesMultiplier},
      {"m", kMebiBytesMultiplier},   {"kib", kKibiBytesMultiplier},
      {"kb", kKibiBytesMultiplier},  {"k", kKibiBytesMultiplier},
      {"b", kBytesMultiplier},       {"", kBytesMultiplier}};

  const auto non_digit_index = size_expression.find_first_not_of("0123456789");
  if (non_digit_index == 0) { // expression starts from from non-digit char.
    return std::nullopt;
  }

  const llvm::StringRef number_part =
      non_digit_index == llvm::StringRef::npos
          ? size_expression
          : size_expression.substr(0, non_digit_index);
  uint64_t parsed_number;
  if (number_part.getAsInteger(10, parsed_number)) {
    return std::nullopt;
  }

  if (non_digit_index != llvm::StringRef::npos) { // if expression has units.
    const auto multiplier = size_expression.substr(non_digit_index).lower();

    auto it = multipliers.find(multiplier);
    if (it == multipliers.end())
      return std::nullopt;

    return parsed_number * it->second;
  } else {
    return parsed_number;
  }
}
