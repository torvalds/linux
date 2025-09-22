//===-- LLDBUtils.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LLDBUtils.h"
#include "DAP.h"

#include <mutex>

namespace lldb_dap {

bool RunLLDBCommands(llvm::StringRef prefix,
                     const llvm::ArrayRef<std::string> &commands,
                     llvm::raw_ostream &strm, bool parse_command_directives) {
  if (commands.empty())
    return true;

  bool did_print_prefix = false;

  lldb::SBCommandInterpreter interp = g_dap.debugger.GetCommandInterpreter();
  for (llvm::StringRef command : commands) {
    lldb::SBCommandReturnObject result;
    bool quiet_on_success = false;
    bool check_error = false;

    while (parse_command_directives) {
      if (command.starts_with("?")) {
        command = command.drop_front();
        quiet_on_success = true;
      } else if (command.starts_with("!")) {
        command = command.drop_front();
        check_error = true;
      } else {
        break;
      }
    }

    {
      // Prevent simultaneous calls to HandleCommand, e.g. EventThreadFunction
      // may asynchronously call RunExitCommands when we are already calling
      // RunTerminateCommands.
      static std::mutex handle_command_mutex;
      std::lock_guard<std::mutex> locker(handle_command_mutex);
      interp.HandleCommand(command.str().c_str(), result);
    }

    const bool got_error = !result.Succeeded();
    // The if statement below is assuming we always print out `!` prefixed
    // lines. The only time we don't print is when we have `quiet_on_success ==
    // true` and we don't have an error.
    if (quiet_on_success ? got_error : true) {
      if (!did_print_prefix && !prefix.empty()) {
        strm << prefix << "\n";
        did_print_prefix = true;
      }
      strm << "(lldb) " << command << "\n";
      auto output_len = result.GetOutputSize();
      if (output_len) {
        const char *output = result.GetOutput();
        strm << output;
      }
      auto error_len = result.GetErrorSize();
      if (error_len) {
        const char *error = result.GetError();
        strm << error;
      }
    }
    if (check_error && got_error)
      return false; // Stop running commands.
  }
  return true;
}

std::string RunLLDBCommands(llvm::StringRef prefix,
                            const llvm::ArrayRef<std::string> &commands,
                            bool &required_command_failed,
                            bool parse_command_directives) {
  required_command_failed = false;
  std::string s;
  llvm::raw_string_ostream strm(s);
  required_command_failed =
      !RunLLDBCommands(prefix, commands, strm, parse_command_directives);
  strm.flush();
  return s;
}

std::string
RunLLDBCommandsVerbatim(llvm::StringRef prefix,
                        const llvm::ArrayRef<std::string> &commands) {
  bool required_command_failed = false;
  return RunLLDBCommands(prefix, commands, required_command_failed,
                         /*parse_command_directives=*/false);
}

bool ThreadHasStopReason(lldb::SBThread &thread) {
  switch (thread.GetStopReason()) {
  case lldb::eStopReasonTrace:
  case lldb::eStopReasonPlanComplete:
  case lldb::eStopReasonBreakpoint:
  case lldb::eStopReasonWatchpoint:
  case lldb::eStopReasonInstrumentation:
  case lldb::eStopReasonSignal:
  case lldb::eStopReasonException:
  case lldb::eStopReasonExec:
  case lldb::eStopReasonProcessorTrace:
  case lldb::eStopReasonFork:
  case lldb::eStopReasonVFork:
  case lldb::eStopReasonVForkDone:
    return true;
  case lldb::eStopReasonThreadExiting:
  case lldb::eStopReasonInvalid:
  case lldb::eStopReasonNone:
    break;
  }
  return false;
}

static uint32_t constexpr THREAD_INDEX_SHIFT = 19;

uint32_t GetLLDBThreadIndexID(uint64_t dap_frame_id) {
  return dap_frame_id >> THREAD_INDEX_SHIFT;
}

uint32_t GetLLDBFrameID(uint64_t dap_frame_id) {
  return dap_frame_id & ((1u << THREAD_INDEX_SHIFT) - 1);
}

int64_t MakeDAPFrameID(lldb::SBFrame &frame) {
  return ((int64_t)frame.GetThread().GetIndexID() << THREAD_INDEX_SHIFT) |
         frame.GetFrameID();
}

} // namespace lldb_dap
