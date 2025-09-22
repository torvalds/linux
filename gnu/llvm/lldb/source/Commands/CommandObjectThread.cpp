//===-- CommandObjectThread.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectThread.h"

#include <memory>
#include <optional>
#include <sstream>

#include "CommandObjectThreadUtil.h"
#include "CommandObjectTrace.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupPythonClassWithDict.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanStepInRange.h"
#include "lldb/Target/Trace.h"
#include "lldb/Target/TraceDumper.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

// CommandObjectThreadBacktrace
#define LLDB_OPTIONS_thread_backtrace
#include "CommandOptions.inc"

class CommandObjectThreadBacktrace : public CommandObjectIterateOverThreads {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'c':
        if (option_arg.getAsInteger(0, m_count)) {
          m_count = UINT32_MAX;
          error.SetErrorStringWithFormat(
              "invalid integer value for option '%c': %s", short_option,
              option_arg.data());
        }
        // A count of 0 means all frames.
        if (m_count == 0)
          m_count = UINT32_MAX;
        break;
      case 's':
        if (option_arg.getAsInteger(0, m_start))
          error.SetErrorStringWithFormat(
              "invalid integer value for option '%c': %s", short_option,
              option_arg.data());
        break;
      case 'e': {
        bool success;
        m_extended_backtrace =
            OptionArgParser::ToBoolean(option_arg, false, &success);
        if (!success)
          error.SetErrorStringWithFormat(
              "invalid boolean value for option '%c': %s", short_option,
              option_arg.data());
      } break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_count = UINT32_MAX;
      m_start = 0;
      m_extended_backtrace = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_backtrace_options);
    }

    // Instance variables to hold the values for command options.
    uint32_t m_count;
    uint32_t m_start;
    bool m_extended_backtrace;
  };

  CommandObjectThreadBacktrace(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread backtrace",
            "Show backtraces of thread call stacks.  Defaults to the current "
            "thread, thread indexes can be specified as arguments.\n"
            "Use the thread-index \"all\" to see all threads.\n"
            "Use the thread-index \"unique\" to see threads grouped by unique "
            "call stacks.\n"
            "Use 'settings set frame-format' to customize the printing of "
            "frames in the backtrace and 'settings set thread-format' to "
            "customize the thread header.",
            nullptr,
            eCommandRequiresProcess | eCommandRequiresThread |
                eCommandTryTargetAPILock | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused) {}

  ~CommandObjectThreadBacktrace() override = default;

  Options *GetOptions() override { return &m_options; }

  std::optional<std::string> GetRepeatCommand(Args &current_args,
                                              uint32_t index) override {
    llvm::StringRef count_opt("--count");
    llvm::StringRef start_opt("--start");

    // If no "count" was provided, we are dumping the entire backtrace, so
    // there isn't a repeat command.  So we search for the count option in
    // the args, and if we find it, we make a copy and insert or modify the
    // start option's value to start count indices greater.

    Args copy_args(current_args);
    size_t num_entries = copy_args.GetArgumentCount();
    // These two point at the index of the option value if found.
    size_t count_idx = 0;
    size_t start_idx = 0;
    size_t count_val = 0;
    size_t start_val = 0;

    for (size_t idx = 0; idx < num_entries; idx++) {
      llvm::StringRef arg_string = copy_args[idx].ref();
      if (arg_string == "-c" || count_opt.starts_with(arg_string)) {
        idx++;
        if (idx == num_entries)
          return std::nullopt;
        count_idx = idx;
        if (copy_args[idx].ref().getAsInteger(0, count_val))
          return std::nullopt;
      } else if (arg_string == "-s" || start_opt.starts_with(arg_string)) {
        idx++;
        if (idx == num_entries)
          return std::nullopt;
        start_idx = idx;
        if (copy_args[idx].ref().getAsInteger(0, start_val))
          return std::nullopt;
      }
    }
    if (count_idx == 0)
      return std::nullopt;

    std::string new_start_val = llvm::formatv("{0}", start_val + count_val);
    if (start_idx == 0) {
      copy_args.AppendArgument(start_opt);
      copy_args.AppendArgument(new_start_val);
    } else {
      copy_args.ReplaceArgumentAtIndex(start_idx, new_start_val);
    }
    std::string repeat_command;
    if (!copy_args.GetQuotedCommandString(repeat_command))
      return std::nullopt;
    return repeat_command;
  }

protected:
  void DoExtendedBacktrace(Thread *thread, CommandReturnObject &result) {
    SystemRuntime *runtime = thread->GetProcess()->GetSystemRuntime();
    if (runtime) {
      Stream &strm = result.GetOutputStream();
      const std::vector<ConstString> &types =
          runtime->GetExtendedBacktraceTypes();
      for (auto type : types) {
        ThreadSP ext_thread_sp = runtime->GetExtendedBacktraceThread(
            thread->shared_from_this(), type);
        if (ext_thread_sp && ext_thread_sp->IsValid()) {
          const uint32_t num_frames_with_source = 0;
          const bool stop_format = false;
          strm.PutChar('\n');
          if (ext_thread_sp->GetStatus(strm, m_options.m_start,
                                       m_options.m_count,
                                       num_frames_with_source, stop_format)) {
            DoExtendedBacktrace(ext_thread_sp.get(), result);
          }
        }
      }
    }
  }

  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    ThreadSP thread_sp =
        m_exe_ctx.GetProcessPtr()->GetThreadList().FindThreadByID(tid);
    if (!thread_sp) {
      result.AppendErrorWithFormat(
          "thread disappeared while computing backtraces: 0x%" PRIx64 "\n",
          tid);
      return false;
    }

    Thread *thread = thread_sp.get();

    Stream &strm = result.GetOutputStream();

    // Only dump stack info if we processing unique stacks.
    const bool only_stacks = m_unique_stacks;

    // Don't show source context when doing backtraces.
    const uint32_t num_frames_with_source = 0;
    const bool stop_format = true;
    if (!thread->GetStatus(strm, m_options.m_start, m_options.m_count,
                           num_frames_with_source, stop_format, only_stacks)) {
      result.AppendErrorWithFormat(
          "error displaying backtrace for thread: \"0x%4.4x\"\n",
          thread->GetIndexID());
      return false;
    }
    if (m_options.m_extended_backtrace) {
      if (!INTERRUPT_REQUESTED(GetDebugger(),
                               "Interrupt skipped extended backtrace")) {
        DoExtendedBacktrace(thread, result);
      }
    }

    return true;
  }

  CommandOptions m_options;
};

enum StepScope { eStepScopeSource, eStepScopeInstruction };

#define LLDB_OPTIONS_thread_step_scope
#include "CommandOptions.inc"

class ThreadStepScopeOptionGroup : public OptionGroup {
public:
  ThreadStepScopeOptionGroup() {
    // Keep default values of all options in one place: OptionParsingStarting
    // ()
    OptionParsingStarting(nullptr);
  }

  ~ThreadStepScopeOptionGroup() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    return llvm::ArrayRef(g_thread_step_scope_options);
  }

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                        ExecutionContext *execution_context) override {
    Status error;
    const int short_option =
        g_thread_step_scope_options[option_idx].short_option;

    switch (short_option) {
    case 'a': {
      bool success;
      bool avoid_no_debug =
          OptionArgParser::ToBoolean(option_arg, true, &success);
      if (!success)
        error.SetErrorStringWithFormat(
            "invalid boolean value for option '%c': %s", short_option,
            option_arg.data());
      else {
        m_step_in_avoid_no_debug = avoid_no_debug ? eLazyBoolYes : eLazyBoolNo;
      }
    } break;

    case 'A': {
      bool success;
      bool avoid_no_debug =
          OptionArgParser::ToBoolean(option_arg, true, &success);
      if (!success)
        error.SetErrorStringWithFormat(
            "invalid boolean value for option '%c': %s", short_option,
            option_arg.data());
      else {
        m_step_out_avoid_no_debug = avoid_no_debug ? eLazyBoolYes : eLazyBoolNo;
      }
    } break;

    case 'c':
      if (option_arg.getAsInteger(0, m_step_count))
        error.SetErrorStringWithFormat(
            "invalid integer value for option '%c': %s", short_option,
            option_arg.data());
      break;

    case 'm': {
      auto enum_values = GetDefinitions()[option_idx].enum_values;
      m_run_mode = (lldb::RunMode)OptionArgParser::ToOptionEnum(
          option_arg, enum_values, eOnlyDuringStepping, error);
    } break;

    case 'e':
      if (option_arg == "block") {
        m_end_line_is_block_end = true;
        break;
      }
      if (option_arg.getAsInteger(0, m_end_line))
        error.SetErrorStringWithFormat("invalid end line number '%s'",
                                       option_arg.str().c_str());
      break;

    case 'r':
      m_avoid_regexp.clear();
      m_avoid_regexp.assign(std::string(option_arg));
      break;

    case 't':
      m_step_in_target.clear();
      m_step_in_target.assign(std::string(option_arg));
      break;

    default:
      llvm_unreachable("Unimplemented option");
    }
    return error;
  }

  void OptionParsingStarting(ExecutionContext *execution_context) override {
    m_step_in_avoid_no_debug = eLazyBoolCalculate;
    m_step_out_avoid_no_debug = eLazyBoolCalculate;
    m_run_mode = eOnlyDuringStepping;

    // Check if we are in Non-Stop mode
    TargetSP target_sp =
        execution_context ? execution_context->GetTargetSP() : TargetSP();
    ProcessSP process_sp =
        execution_context ? execution_context->GetProcessSP() : ProcessSP();
    if (process_sp && process_sp->GetSteppingRunsAllThreads())
      m_run_mode = eAllThreads;

    m_avoid_regexp.clear();
    m_step_in_target.clear();
    m_step_count = 1;
    m_end_line = LLDB_INVALID_LINE_NUMBER;
    m_end_line_is_block_end = false;
  }

  // Instance variables to hold the values for command options.
  LazyBool m_step_in_avoid_no_debug;
  LazyBool m_step_out_avoid_no_debug;
  RunMode m_run_mode;
  std::string m_avoid_regexp;
  std::string m_step_in_target;
  uint32_t m_step_count;
  uint32_t m_end_line;
  bool m_end_line_is_block_end;
};

class CommandObjectThreadStepWithTypeAndScope : public CommandObjectParsed {
public:
  CommandObjectThreadStepWithTypeAndScope(CommandInterpreter &interpreter,
                                          const char *name, const char *help,
                                          const char *syntax,
                                          StepType step_type,
                                          StepScope step_scope)
      : CommandObjectParsed(interpreter, name, help, syntax,
                            eCommandRequiresProcess | eCommandRequiresThread |
                                eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused),
        m_step_type(step_type), m_step_scope(step_scope),
        m_class_options("scripted step") {
    AddSimpleArgumentList(eArgTypeThreadIndex, eArgRepeatOptional);

    if (step_type == eStepTypeScripted) {
      m_all_options.Append(&m_class_options, LLDB_OPT_SET_1 | LLDB_OPT_SET_2,
                           LLDB_OPT_SET_1);
    }
    m_all_options.Append(&m_options);
    m_all_options.Finalize();
  }

  ~CommandObjectThreadStepWithTypeAndScope() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex())
      return;
    CommandObject::HandleArgumentCompletion(request, opt_element_vector);
  }

  Options *GetOptions() override { return &m_all_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    bool synchronous_execution = m_interpreter.GetSynchronous();

    const uint32_t num_threads = process->GetThreadList().GetSize();
    Thread *thread = nullptr;

    if (command.GetArgumentCount() == 0) {
      thread = GetDefaultThread();

      if (thread == nullptr) {
        result.AppendError("no selected thread in process");
        return;
      }
    } else {
      const char *thread_idx_cstr = command.GetArgumentAtIndex(0);
      uint32_t step_thread_idx;

      if (!llvm::to_integer(thread_idx_cstr, step_thread_idx)) {
        result.AppendErrorWithFormat("invalid thread index '%s'.\n",
                                     thread_idx_cstr);
        return;
      }
      thread =
          process->GetThreadList().FindThreadByIndexID(step_thread_idx).get();
      if (thread == nullptr) {
        result.AppendErrorWithFormat(
            "Thread index %u is out of range (valid values are 0 - %u).\n",
            step_thread_idx, num_threads);
        return;
      }
    }

    if (m_step_type == eStepTypeScripted) {
      if (m_class_options.GetName().empty()) {
        result.AppendErrorWithFormat("empty class name for scripted step.");
        return;
      } else if (!GetDebugger().GetScriptInterpreter()->CheckObjectExists(
                     m_class_options.GetName().c_str())) {
        result.AppendErrorWithFormat(
            "class for scripted step: \"%s\" does not exist.",
            m_class_options.GetName().c_str());
        return;
      }
    }

    if (m_options.m_end_line != LLDB_INVALID_LINE_NUMBER &&
        m_step_type != eStepTypeInto) {
      result.AppendErrorWithFormat(
          "end line option is only valid for step into");
      return;
    }

    const bool abort_other_plans = false;
    const lldb::RunMode stop_other_threads = m_options.m_run_mode;

    // This is a bit unfortunate, but not all the commands in this command
    // object support only while stepping, so I use the bool for them.
    bool bool_stop_other_threads;
    if (m_options.m_run_mode == eAllThreads)
      bool_stop_other_threads = false;
    else if (m_options.m_run_mode == eOnlyDuringStepping)
      bool_stop_other_threads = (m_step_type != eStepTypeOut);
    else
      bool_stop_other_threads = true;

    ThreadPlanSP new_plan_sp;
    Status new_plan_status;

    if (m_step_type == eStepTypeInto) {
      StackFrame *frame = thread->GetStackFrameAtIndex(0).get();
      assert(frame != nullptr);

      if (frame->HasDebugInformation()) {
        AddressRange range;
        SymbolContext sc = frame->GetSymbolContext(eSymbolContextEverything);
        if (m_options.m_end_line != LLDB_INVALID_LINE_NUMBER) {
          Status error;
          if (!sc.GetAddressRangeFromHereToEndLine(m_options.m_end_line, range,
                                                   error)) {
            result.AppendErrorWithFormat("invalid end-line option: %s.",
                                         error.AsCString());
            return;
          }
        } else if (m_options.m_end_line_is_block_end) {
          Status error;
          Block *block = frame->GetSymbolContext(eSymbolContextBlock).block;
          if (!block) {
            result.AppendErrorWithFormat("Could not find the current block.");
            return;
          }

          AddressRange block_range;
          Address pc_address = frame->GetFrameCodeAddress();
          block->GetRangeContainingAddress(pc_address, block_range);
          if (!block_range.GetBaseAddress().IsValid()) {
            result.AppendErrorWithFormat(
                "Could not find the current block address.");
            return;
          }
          lldb::addr_t pc_offset_in_block =
              pc_address.GetFileAddress() -
              block_range.GetBaseAddress().GetFileAddress();
          lldb::addr_t range_length =
              block_range.GetByteSize() - pc_offset_in_block;
          range = AddressRange(pc_address, range_length);
        } else {
          range = sc.line_entry.range;
        }

        new_plan_sp = thread->QueueThreadPlanForStepInRange(
            abort_other_plans, range,
            frame->GetSymbolContext(eSymbolContextEverything),
            m_options.m_step_in_target.c_str(), stop_other_threads,
            new_plan_status, m_options.m_step_in_avoid_no_debug,
            m_options.m_step_out_avoid_no_debug);

        if (new_plan_sp && !m_options.m_avoid_regexp.empty()) {
          ThreadPlanStepInRange *step_in_range_plan =
              static_cast<ThreadPlanStepInRange *>(new_plan_sp.get());
          step_in_range_plan->SetAvoidRegexp(m_options.m_avoid_regexp.c_str());
        }
      } else
        new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
            false, abort_other_plans, bool_stop_other_threads, new_plan_status);
    } else if (m_step_type == eStepTypeOver) {
      StackFrame *frame = thread->GetStackFrameAtIndex(0).get();

      if (frame->HasDebugInformation())
        new_plan_sp = thread->QueueThreadPlanForStepOverRange(
            abort_other_plans,
            frame->GetSymbolContext(eSymbolContextEverything).line_entry,
            frame->GetSymbolContext(eSymbolContextEverything),
            stop_other_threads, new_plan_status,
            m_options.m_step_out_avoid_no_debug);
      else
        new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
            true, abort_other_plans, bool_stop_other_threads, new_plan_status);
    } else if (m_step_type == eStepTypeTrace) {
      new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
          false, abort_other_plans, bool_stop_other_threads, new_plan_status);
    } else if (m_step_type == eStepTypeTraceOver) {
      new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
          true, abort_other_plans, bool_stop_other_threads, new_plan_status);
    } else if (m_step_type == eStepTypeOut) {
      new_plan_sp = thread->QueueThreadPlanForStepOut(
          abort_other_plans, nullptr, false, bool_stop_other_threads, eVoteYes,
          eVoteNoOpinion,
          thread->GetSelectedFrameIndex(DoNoSelectMostRelevantFrame),
          new_plan_status, m_options.m_step_out_avoid_no_debug);
    } else if (m_step_type == eStepTypeScripted) {
      new_plan_sp = thread->QueueThreadPlanForStepScripted(
          abort_other_plans, m_class_options.GetName().c_str(),
          m_class_options.GetStructuredData(), bool_stop_other_threads,
          new_plan_status);
    } else {
      result.AppendError("step type is not supported");
      return;
    }

    // If we got a new plan, then set it to be a controlling plan (User level
    // Plans should be controlling plans so that they can be interruptible).
    // Then resume the process.

    if (new_plan_sp) {
      new_plan_sp->SetIsControllingPlan(true);
      new_plan_sp->SetOkayToDiscard(false);

      if (m_options.m_step_count > 1) {
        if (!new_plan_sp->SetIterationCount(m_options.m_step_count)) {
          result.AppendWarning(
              "step operation does not support iteration count.");
        }
      }

      process->GetThreadList().SetSelectedThreadByID(thread->GetID());

      const uint32_t iohandler_id = process->GetIOHandlerID();

      StreamString stream;
      Status error;
      if (synchronous_execution)
        error = process->ResumeSynchronous(&stream);
      else
        error = process->Resume();

      if (!error.Success()) {
        result.AppendMessage(error.AsCString());
        return;
      }

      // There is a race condition where this thread will return up the call
      // stack to the main command handler and show an (lldb) prompt before
      // HandlePrivateEvent (from PrivateStateThread) has a chance to call
      // PushProcessIOHandler().
      process->SyncIOHandler(iohandler_id, std::chrono::seconds(2));

      if (synchronous_execution) {
        // If any state changed events had anything to say, add that to the
        // result
        if (stream.GetSize() > 0)
          result.AppendMessage(stream.GetString());

        process->GetThreadList().SetSelectedThreadByID(thread->GetID());
        result.SetDidChangeProcessState(true);
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
      } else {
        result.SetStatus(eReturnStatusSuccessContinuingNoResult);
      }
    } else {
      result.SetError(new_plan_status);
    }
  }

  StepType m_step_type;
  StepScope m_step_scope;
  ThreadStepScopeOptionGroup m_options;
  OptionGroupPythonClassWithDict m_class_options;
  OptionGroupOptions m_all_options;
};

// CommandObjectThreadContinue

class CommandObjectThreadContinue : public CommandObjectParsed {
public:
  CommandObjectThreadContinue(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread continue",
            "Continue execution of the current target process.  One "
            "or more threads may be specified, by default all "
            "threads continue.",
            nullptr,
            eCommandRequiresThread | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypeThreadIndex, eArgRepeatPlus);
  }

  ~CommandObjectThreadContinue() override = default;

  void DoExecute(Args &command, CommandReturnObject &result) override {
    bool synchronous_execution = m_interpreter.GetSynchronous();

    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process exists. Cannot continue");
      return;
    }

    StateType state = process->GetState();
    if ((state == eStateCrashed) || (state == eStateStopped) ||
        (state == eStateSuspended)) {
      const size_t argc = command.GetArgumentCount();
      if (argc > 0) {
        // These two lines appear at the beginning of both blocks in this
        // if..else, but that is because we need to release the lock before
        // calling process->Resume below.
        std::lock_guard<std::recursive_mutex> guard(
            process->GetThreadList().GetMutex());
        const uint32_t num_threads = process->GetThreadList().GetSize();
        std::vector<Thread *> resume_threads;
        for (auto &entry : command.entries()) {
          uint32_t thread_idx;
          if (entry.ref().getAsInteger(0, thread_idx)) {
            result.AppendErrorWithFormat(
                "invalid thread index argument: \"%s\".\n", entry.c_str());
            return;
          }
          Thread *thread =
              process->GetThreadList().FindThreadByIndexID(thread_idx).get();

          if (thread) {
            resume_threads.push_back(thread);
          } else {
            result.AppendErrorWithFormat("invalid thread index %u.\n",
                                         thread_idx);
            return;
          }
        }

        if (resume_threads.empty()) {
          result.AppendError("no valid thread indexes were specified");
          return;
        } else {
          if (resume_threads.size() == 1)
            result.AppendMessageWithFormat("Resuming thread: ");
          else
            result.AppendMessageWithFormat("Resuming threads: ");

          for (uint32_t idx = 0; idx < num_threads; ++idx) {
            Thread *thread =
                process->GetThreadList().GetThreadAtIndex(idx).get();
            std::vector<Thread *>::iterator this_thread_pos =
                find(resume_threads.begin(), resume_threads.end(), thread);

            if (this_thread_pos != resume_threads.end()) {
              resume_threads.erase(this_thread_pos);
              if (!resume_threads.empty())
                result.AppendMessageWithFormat("%u, ", thread->GetIndexID());
              else
                result.AppendMessageWithFormat("%u ", thread->GetIndexID());

              const bool override_suspend = true;
              thread->SetResumeState(eStateRunning, override_suspend);
            } else {
              thread->SetResumeState(eStateSuspended);
            }
          }
          result.AppendMessageWithFormat("in process %" PRIu64 "\n",
                                         process->GetID());
        }
      } else {
        // These two lines appear at the beginning of both blocks in this
        // if..else, but that is because we need to release the lock before
        // calling process->Resume below.
        std::lock_guard<std::recursive_mutex> guard(
            process->GetThreadList().GetMutex());
        const uint32_t num_threads = process->GetThreadList().GetSize();
        Thread *current_thread = GetDefaultThread();
        if (current_thread == nullptr) {
          result.AppendError("the process doesn't have a current thread");
          return;
        }
        // Set the actions that the threads should each take when resuming
        for (uint32_t idx = 0; idx < num_threads; ++idx) {
          Thread *thread = process->GetThreadList().GetThreadAtIndex(idx).get();
          if (thread == current_thread) {
            result.AppendMessageWithFormat("Resuming thread 0x%4.4" PRIx64
                                           " in process %" PRIu64 "\n",
                                           thread->GetID(), process->GetID());
            const bool override_suspend = true;
            thread->SetResumeState(eStateRunning, override_suspend);
          } else {
            thread->SetResumeState(eStateSuspended);
          }
        }
      }

      StreamString stream;
      Status error;
      if (synchronous_execution)
        error = process->ResumeSynchronous(&stream);
      else
        error = process->Resume();

      // We should not be holding the thread list lock when we do this.
      if (error.Success()) {
        result.AppendMessageWithFormat("Process %" PRIu64 " resuming\n",
                                       process->GetID());
        if (synchronous_execution) {
          // If any state changed events had anything to say, add that to the
          // result
          if (stream.GetSize() > 0)
            result.AppendMessage(stream.GetString());

          result.SetDidChangeProcessState(true);
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        } else {
          result.SetStatus(eReturnStatusSuccessContinuingNoResult);
        }
      } else {
        result.AppendErrorWithFormat("Failed to resume process: %s\n",
                                     error.AsCString());
      }
    } else {
      result.AppendErrorWithFormat(
          "Process cannot be continued from its current state (%s).\n",
          StateAsCString(state));
    }
  }
};

// CommandObjectThreadUntil

#define LLDB_OPTIONS_thread_until
#include "CommandOptions.inc"

class CommandObjectThreadUntil : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    uint32_t m_thread_idx = LLDB_INVALID_THREAD_ID;
    uint32_t m_frame_idx = LLDB_INVALID_FRAME_ID;

    CommandOptions() {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a': {
        lldb::addr_t tmp_addr = OptionArgParser::ToAddress(
            execution_context, option_arg, LLDB_INVALID_ADDRESS, &error);
        if (error.Success())
          m_until_addrs.push_back(tmp_addr);
      } break;
      case 't':
        if (option_arg.getAsInteger(0, m_thread_idx)) {
          m_thread_idx = LLDB_INVALID_INDEX32;
          error.SetErrorStringWithFormat("invalid thread index '%s'",
                                         option_arg.str().c_str());
        }
        break;
      case 'f':
        if (option_arg.getAsInteger(0, m_frame_idx)) {
          m_frame_idx = LLDB_INVALID_FRAME_ID;
          error.SetErrorStringWithFormat("invalid frame index '%s'",
                                         option_arg.str().c_str());
        }
        break;
      case 'm': {
        auto enum_values = GetDefinitions()[option_idx].enum_values;
        lldb::RunMode run_mode = (lldb::RunMode)OptionArgParser::ToOptionEnum(
            option_arg, enum_values, eOnlyDuringStepping, error);

        if (error.Success()) {
          if (run_mode == eAllThreads)
            m_stop_others = false;
          else
            m_stop_others = true;
        }
      } break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_thread_idx = LLDB_INVALID_THREAD_ID;
      m_frame_idx = 0;
      m_stop_others = false;
      m_until_addrs.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_until_options);
    }

    uint32_t m_step_thread_idx = LLDB_INVALID_THREAD_ID;
    bool m_stop_others = false;
    std::vector<lldb::addr_t> m_until_addrs;

    // Instance variables to hold the values for command options.
  };

  CommandObjectThreadUntil(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread until",
            "Continue until a line number or address is reached by the "
            "current or specified thread.  Stops when returning from "
            "the current function as a safety measure.  "
            "The target line number(s) are given as arguments, and if more "
            "than one"
            " is provided, stepping will stop when the first one is hit.",
            nullptr,
            eCommandRequiresThread | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypeLineNum);
  }

  ~CommandObjectThreadUntil() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    bool synchronous_execution = m_interpreter.GetSynchronous();

    Target *target = &GetSelectedTarget();

    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("need a valid process to step");
    } else {
      Thread *thread = nullptr;
      std::vector<uint32_t> line_numbers;

      if (command.GetArgumentCount() >= 1) {
        size_t num_args = command.GetArgumentCount();
        for (size_t i = 0; i < num_args; i++) {
          uint32_t line_number;
          if (!llvm::to_integer(command.GetArgumentAtIndex(i), line_number)) {
            result.AppendErrorWithFormat("invalid line number: '%s'.\n",
                                         command.GetArgumentAtIndex(i));
            return;
          } else
            line_numbers.push_back(line_number);
        }
      } else if (m_options.m_until_addrs.empty()) {
        result.AppendErrorWithFormat("No line number or address provided:\n%s",
                                     GetSyntax().str().c_str());
        return;
      }

      if (m_options.m_thread_idx == LLDB_INVALID_THREAD_ID) {
        thread = GetDefaultThread();
      } else {
        thread = process->GetThreadList()
                     .FindThreadByIndexID(m_options.m_thread_idx)
                     .get();
      }

      if (thread == nullptr) {
        const uint32_t num_threads = process->GetThreadList().GetSize();
        result.AppendErrorWithFormat(
            "Thread index %u is out of range (valid values are 0 - %u).\n",
            m_options.m_thread_idx, num_threads);
        return;
      }

      const bool abort_other_plans = false;

      StackFrame *frame =
          thread->GetStackFrameAtIndex(m_options.m_frame_idx).get();
      if (frame == nullptr) {
        result.AppendErrorWithFormat(
            "Frame index %u is out of range for thread id %" PRIu64 ".\n",
            m_options.m_frame_idx, thread->GetID());
        return;
      }

      ThreadPlanSP new_plan_sp;
      Status new_plan_status;

      if (frame->HasDebugInformation()) {
        // Finally we got here...  Translate the given line number to a bunch
        // of addresses:
        SymbolContext sc(frame->GetSymbolContext(eSymbolContextCompUnit));
        LineTable *line_table = nullptr;
        if (sc.comp_unit)
          line_table = sc.comp_unit->GetLineTable();

        if (line_table == nullptr) {
          result.AppendErrorWithFormat("Failed to resolve the line table for "
                                       "frame %u of thread id %" PRIu64 ".\n",
                                       m_options.m_frame_idx, thread->GetID());
          return;
        }

        LineEntry function_start;
        uint32_t index_ptr = 0, end_ptr = UINT32_MAX;
        std::vector<addr_t> address_list;

        // Find the beginning & end index of the function, but first make
        // sure it is valid:
        if (!sc.function) {
          result.AppendErrorWithFormat("Have debug information but no "
                                       "function info - can't get until range.");
          return;
        }

        AddressRange fun_addr_range = sc.function->GetAddressRange();
        Address fun_start_addr = fun_addr_range.GetBaseAddress();
        line_table->FindLineEntryByAddress(fun_start_addr, function_start,
                                           &index_ptr);

        Address fun_end_addr(fun_start_addr.GetSection(),
                             fun_start_addr.GetOffset() +
                                 fun_addr_range.GetByteSize());

        bool all_in_function = true;

        line_table->FindLineEntryByAddress(fun_end_addr, function_start,
                                           &end_ptr);

        // Since not all source lines will contribute code, check if we are
        // setting the breakpoint on the exact line number or the nearest
        // subsequent line number and set breakpoints at all the line table
        // entries of the chosen line number (exact or nearest subsequent).
        for (uint32_t line_number : line_numbers) {
          LineEntry line_entry;
          bool exact = false;
          uint32_t start_idx_ptr = index_ptr;
          start_idx_ptr = sc.comp_unit->FindLineEntry(
              index_ptr, line_number, nullptr, exact, &line_entry);
          if (start_idx_ptr != UINT32_MAX)
            line_number = line_entry.line;
          exact = true;
          start_idx_ptr = index_ptr;
          while (start_idx_ptr <= end_ptr) {
            start_idx_ptr = sc.comp_unit->FindLineEntry(
                start_idx_ptr, line_number, nullptr, exact, &line_entry);
            if (start_idx_ptr == UINT32_MAX)
              break;

            addr_t address =
                line_entry.range.GetBaseAddress().GetLoadAddress(target);
            if (address != LLDB_INVALID_ADDRESS) {
              if (fun_addr_range.ContainsLoadAddress(address, target))
                address_list.push_back(address);
              else
                all_in_function = false;
            }
            start_idx_ptr++;
          }
        }

        for (lldb::addr_t address : m_options.m_until_addrs) {
          if (fun_addr_range.ContainsLoadAddress(address, target))
            address_list.push_back(address);
          else
            all_in_function = false;
        }

        if (address_list.empty()) {
          if (all_in_function)
            result.AppendErrorWithFormat(
                "No line entries matching until target.\n");
          else
            result.AppendErrorWithFormat(
                "Until target outside of the current function.\n");

          return;
        }

        new_plan_sp = thread->QueueThreadPlanForStepUntil(
            abort_other_plans, &address_list.front(), address_list.size(),
            m_options.m_stop_others, m_options.m_frame_idx, new_plan_status);
        if (new_plan_sp) {
          // User level plans should be controlling plans so they can be
          // interrupted
          // (e.g. by hitting a breakpoint) and other plans executed by the
          // user (stepping around the breakpoint) and then a "continue" will
          // resume the original plan.
          new_plan_sp->SetIsControllingPlan(true);
          new_plan_sp->SetOkayToDiscard(false);
        } else {
          result.SetError(new_plan_status);
          return;
        }
      } else {
        result.AppendErrorWithFormat("Frame index %u of thread id %" PRIu64
                                     " has no debug information.\n",
                                     m_options.m_frame_idx, thread->GetID());
        return;
      }

      if (!process->GetThreadList().SetSelectedThreadByID(thread->GetID())) {
        result.AppendErrorWithFormat(
            "Failed to set the selected thread to thread id %" PRIu64 ".\n",
            thread->GetID());
        return;
      }

      StreamString stream;
      Status error;
      if (synchronous_execution)
        error = process->ResumeSynchronous(&stream);
      else
        error = process->Resume();

      if (error.Success()) {
        result.AppendMessageWithFormat("Process %" PRIu64 " resuming\n",
                                       process->GetID());
        if (synchronous_execution) {
          // If any state changed events had anything to say, add that to the
          // result
          if (stream.GetSize() > 0)
            result.AppendMessage(stream.GetString());

          result.SetDidChangeProcessState(true);
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        } else {
          result.SetStatus(eReturnStatusSuccessContinuingNoResult);
        }
      } else {
        result.AppendErrorWithFormat("Failed to resume process: %s.\n",
                                     error.AsCString());
      }
    }
  }

  CommandOptions m_options;
};

// CommandObjectThreadSelect

#define LLDB_OPTIONS_thread_select
#include "CommandOptions.inc"

class CommandObjectThreadSelect : public CommandObjectParsed {
public:
  class OptionGroupThreadSelect : public OptionGroup {
  public:
    OptionGroupThreadSelect() { OptionParsingStarting(nullptr); }

    ~OptionGroupThreadSelect() override = default;

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_thread_id = LLDB_INVALID_THREAD_ID;
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      const int short_option = g_thread_select_options[option_idx].short_option;
      switch (short_option) {
      case 't': {
        if (option_arg.getAsInteger(0, m_thread_id)) {
          m_thread_id = LLDB_INVALID_THREAD_ID;
          return Status("Invalid thread ID: '%s'.", option_arg.str().c_str());
        }
        break;
      }

      default:
        llvm_unreachable("Unimplemented option");
      }

      return {};
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_select_options);
    }

    lldb::tid_t m_thread_id;
  };

  CommandObjectThreadSelect(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "thread select",
                            "Change the currently selected thread.",
                            "thread select <thread-index> (or -t <thread-id>)",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    CommandArgumentEntry arg;
    CommandArgumentData thread_idx_arg;

    // Define the first (and only) variant of this arg.
    thread_idx_arg.arg_type = eArgTypeThreadIndex;
    thread_idx_arg.arg_repetition = eArgRepeatPlain;
    thread_idx_arg.arg_opt_set_association = LLDB_OPT_SET_1;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(thread_idx_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);

    m_option_group.Append(&m_options, LLDB_OPT_SET_ALL, LLDB_OPT_SET_2);
    m_option_group.Finalize();
  }

  ~CommandObjectThreadSelect() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex())
      return;

    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eThreadIndexCompletion, request,
        nullptr);
  }

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process");
      return;
    } else if (m_options.m_thread_id == LLDB_INVALID_THREAD_ID &&
               command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat(
          "'%s' takes exactly one thread index argument, or a thread ID "
          "option:\nUsage: %s\n",
          m_cmd_name.c_str(), m_cmd_syntax.c_str());
      return;
    } else if (m_options.m_thread_id != LLDB_INVALID_THREAD_ID &&
               command.GetArgumentCount() != 0) {
      result.AppendErrorWithFormat("'%s' cannot take both a thread ID option "
                                   "and a thread index argument:\nUsage: %s\n",
                                   m_cmd_name.c_str(), m_cmd_syntax.c_str());
      return;
    }

    Thread *new_thread = nullptr;
    if (command.GetArgumentCount() == 1) {
      uint32_t index_id;
      if (!llvm::to_integer(command.GetArgumentAtIndex(0), index_id)) {
        result.AppendErrorWithFormat("Invalid thread index '%s'",
                                     command.GetArgumentAtIndex(0));
        return;
      }
      new_thread = process->GetThreadList().FindThreadByIndexID(index_id).get();
      if (new_thread == nullptr) {
        result.AppendErrorWithFormat("Invalid thread index #%s.\n",
                                     command.GetArgumentAtIndex(0));
        return;
      }
    } else {
      new_thread =
          process->GetThreadList().FindThreadByID(m_options.m_thread_id).get();
      if (new_thread == nullptr) {
        result.AppendErrorWithFormat("Invalid thread ID %" PRIu64 ".\n",
                                     m_options.m_thread_id);
        return;
      }
    }

    process->GetThreadList().SetSelectedThreadByID(new_thread->GetID(), true);
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }

  OptionGroupThreadSelect m_options;
  OptionGroupOptions m_option_group;
};

// CommandObjectThreadList

class CommandObjectThreadList : public CommandObjectParsed {
public:
  CommandObjectThreadList(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread list",
            "Show a summary of each thread in the current target process.  "
            "Use 'settings set thread-format' to customize the individual "
            "thread listings.",
            "thread list",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectThreadList() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Stream &strm = result.GetOutputStream();
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    Process *process = m_exe_ctx.GetProcessPtr();
    const bool only_threads_with_stop_reason = false;
    const uint32_t start_frame = 0;
    const uint32_t num_frames = 0;
    const uint32_t num_frames_with_source = 0;
    process->GetStatus(strm);
    process->GetThreadStatus(strm, only_threads_with_stop_reason, start_frame,
                             num_frames, num_frames_with_source, false);
  }
};

// CommandObjectThreadInfo
#define LLDB_OPTIONS_thread_info
#include "CommandOptions.inc"

class CommandObjectThreadInfo : public CommandObjectIterateOverThreads {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_json_thread = false;
      m_json_stopinfo = false;
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      const int short_option = m_getopt_table[option_idx].val;
      Status error;

      switch (short_option) {
      case 'j':
        m_json_thread = true;
        break;

      case 's':
        m_json_stopinfo = true;
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_info_options);
    }

    bool m_json_thread;
    bool m_json_stopinfo;
  };

  CommandObjectThreadInfo(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread info",
            "Show an extended summary of one or "
            "more threads.  Defaults to the "
            "current thread.",
            "thread info",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {
    m_add_return = false;
  }

  ~CommandObjectThreadInfo() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eThreadIndexCompletion, request,
        nullptr);
  }

  Options *GetOptions() override { return &m_options; }

  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    ThreadSP thread_sp =
        m_exe_ctx.GetProcessPtr()->GetThreadList().FindThreadByID(tid);
    if (!thread_sp) {
      result.AppendErrorWithFormat("thread no longer exists: 0x%" PRIx64 "\n",
                                   tid);
      return false;
    }

    Thread *thread = thread_sp.get();

    Stream &strm = result.GetOutputStream();
    if (!thread->GetDescription(strm, eDescriptionLevelFull,
                                m_options.m_json_thread,
                                m_options.m_json_stopinfo)) {
      result.AppendErrorWithFormat("error displaying info for thread: \"%d\"\n",
                                   thread->GetIndexID());
      return false;
    }
    return true;
  }

  CommandOptions m_options;
};

// CommandObjectThreadException

class CommandObjectThreadException : public CommandObjectIterateOverThreads {
public:
  CommandObjectThreadException(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread exception",
            "Display the current exception object for a thread. Defaults to "
            "the current thread.",
            "thread exception",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectThreadException() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eThreadIndexCompletion, request,
        nullptr);
  }

  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    ThreadSP thread_sp =
        m_exe_ctx.GetProcessPtr()->GetThreadList().FindThreadByID(tid);
    if (!thread_sp) {
      result.AppendErrorWithFormat("thread no longer exists: 0x%" PRIx64 "\n",
                                   tid);
      return false;
    }

    Stream &strm = result.GetOutputStream();
    ValueObjectSP exception_object_sp = thread_sp->GetCurrentException();
    if (exception_object_sp) {
      if (llvm::Error error = exception_object_sp->Dump(strm)) {
        result.AppendError(toString(std::move(error)));
        return false;
      }
    }

    ThreadSP exception_thread_sp = thread_sp->GetCurrentExceptionBacktrace();
    if (exception_thread_sp && exception_thread_sp->IsValid()) {
      const uint32_t num_frames_with_source = 0;
      const bool stop_format = false;
      exception_thread_sp->GetStatus(strm, 0, UINT32_MAX,
                                     num_frames_with_source, stop_format);
    }

    return true;
  }
};

class CommandObjectThreadSiginfo : public CommandObjectIterateOverThreads {
public:
  CommandObjectThreadSiginfo(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread siginfo",
            "Display the current siginfo object for a thread. Defaults to "
            "the current thread.",
            "thread siginfo",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectThreadSiginfo() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eThreadIndexCompletion, request,
        nullptr);
  }

  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    ThreadSP thread_sp =
        m_exe_ctx.GetProcessPtr()->GetThreadList().FindThreadByID(tid);
    if (!thread_sp) {
      result.AppendErrorWithFormat("thread no longer exists: 0x%" PRIx64 "\n",
                                   tid);
      return false;
    }

    Stream &strm = result.GetOutputStream();
    if (!thread_sp->GetDescription(strm, eDescriptionLevelFull, false, false)) {
      result.AppendErrorWithFormat("error displaying info for thread: \"%d\"\n",
                                   thread_sp->GetIndexID());
      return false;
    }
    ValueObjectSP exception_object_sp = thread_sp->GetSiginfoValue();
    if (exception_object_sp) {
      if (llvm::Error error = exception_object_sp->Dump(strm)) {
        result.AppendError(toString(std::move(error)));
        return false;
      }
    } else
      strm.Printf("(no siginfo)\n");
    strm.PutChar('\n');

    return true;
  }
};

// CommandObjectThreadReturn
#define LLDB_OPTIONS_thread_return
#include "CommandOptions.inc"

class CommandObjectThreadReturn : public CommandObjectRaw {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'x': {
        bool success;
        bool tmp_value =
            OptionArgParser::ToBoolean(option_arg, false, &success);
        if (success)
          m_from_expression = tmp_value;
        else {
          error.SetErrorStringWithFormat(
              "invalid boolean value '%s' for 'x' option",
              option_arg.str().c_str());
        }
      } break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_from_expression = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_return_options);
    }

    bool m_from_expression = false;

    // Instance variables to hold the values for command options.
  };

  CommandObjectThreadReturn(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "thread return",
                         "Prematurely return from a stack frame, "
                         "short-circuiting execution of newer frames "
                         "and optionally yielding a specified value.  Defaults "
                         "to the exiting the current stack "
                         "frame.",
                         "thread return",
                         eCommandRequiresFrame | eCommandTryTargetAPILock |
                             eCommandProcessMustBeLaunched |
                             eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypeExpression, eArgRepeatOptional);
  }

  ~CommandObjectThreadReturn() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    // I am going to handle this by hand, because I don't want you to have to
    // say:
    // "thread return -- -5".
    if (command.starts_with("-x")) {
      if (command.size() != 2U)
        result.AppendWarning("Return values ignored when returning from user "
                             "called expressions");

      Thread *thread = m_exe_ctx.GetThreadPtr();
      Status error;
      error = thread->UnwindInnermostExpression();
      if (!error.Success()) {
        result.AppendErrorWithFormat("Unwinding expression failed - %s.",
                                     error.AsCString());
      } else {
        bool success =
            thread->SetSelectedFrameByIndexNoisily(0, result.GetOutputStream());
        if (success) {
          m_exe_ctx.SetFrameSP(
              thread->GetSelectedFrame(DoNoSelectMostRelevantFrame));
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat(
              "Could not select 0th frame after unwinding expression.");
        }
      }
      return;
    }

    ValueObjectSP return_valobj_sp;

    StackFrameSP frame_sp = m_exe_ctx.GetFrameSP();
    uint32_t frame_idx = frame_sp->GetFrameIndex();

    if (frame_sp->IsInlined()) {
      result.AppendError("Don't know how to return from inlined frames.");
      return;
    }

    if (!command.empty()) {
      Target *target = m_exe_ctx.GetTargetPtr();
      EvaluateExpressionOptions options;

      options.SetUnwindOnError(true);
      options.SetUseDynamic(eNoDynamicValues);

      ExpressionResults exe_results = eExpressionSetupError;
      exe_results = target->EvaluateExpression(command, frame_sp.get(),
                                               return_valobj_sp, options);
      if (exe_results != eExpressionCompleted) {
        if (return_valobj_sp)
          result.AppendErrorWithFormat(
              "Error evaluating result expression: %s",
              return_valobj_sp->GetError().AsCString());
        else
          result.AppendErrorWithFormat(
              "Unknown error evaluating result expression.");
        return;
      }
    }

    Status error;
    ThreadSP thread_sp = m_exe_ctx.GetThreadSP();
    const bool broadcast = true;
    error = thread_sp->ReturnFromFrame(frame_sp, return_valobj_sp, broadcast);
    if (!error.Success()) {
      result.AppendErrorWithFormat(
          "Error returning from frame %d of thread %d: %s.", frame_idx,
          thread_sp->GetIndexID(), error.AsCString());
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }

  CommandOptions m_options;
};

// CommandObjectThreadJump
#define LLDB_OPTIONS_thread_jump
#include "CommandOptions.inc"

class CommandObjectThreadJump : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_filenames.Clear();
      m_line_num = 0;
      m_line_offset = 0;
      m_load_addr = LLDB_INVALID_ADDRESS;
      m_force = false;
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      const int short_option = m_getopt_table[option_idx].val;
      Status error;

      switch (short_option) {
      case 'f':
        m_filenames.AppendIfUnique(FileSpec(option_arg));
        if (m_filenames.GetSize() > 1)
          return Status("only one source file expected.");
        break;
      case 'l':
        if (option_arg.getAsInteger(0, m_line_num))
          return Status("invalid line number: '%s'.", option_arg.str().c_str());
        break;
      case 'b':
        if (option_arg.getAsInteger(0, m_line_offset))
          return Status("invalid line offset: '%s'.", option_arg.str().c_str());
        break;
      case 'a':
        m_load_addr = OptionArgParser::ToAddress(execution_context, option_arg,
                                                 LLDB_INVALID_ADDRESS, &error);
        break;
      case 'r':
        m_force = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_jump_options);
    }

    FileSpecList m_filenames;
    uint32_t m_line_num;
    int32_t m_line_offset;
    lldb::addr_t m_load_addr;
    bool m_force;
  };

  CommandObjectThreadJump(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread jump",
            "Sets the program counter to a new address.", "thread jump",
            eCommandRequiresFrame | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectThreadJump() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    RegisterContext *reg_ctx = m_exe_ctx.GetRegisterContext();
    StackFrame *frame = m_exe_ctx.GetFramePtr();
    Thread *thread = m_exe_ctx.GetThreadPtr();
    Target *target = m_exe_ctx.GetTargetPtr();
    const SymbolContext &sym_ctx =
        frame->GetSymbolContext(eSymbolContextLineEntry);

    if (m_options.m_load_addr != LLDB_INVALID_ADDRESS) {
      // Use this address directly.
      Address dest = Address(m_options.m_load_addr);

      lldb::addr_t callAddr = dest.GetCallableLoadAddress(target);
      if (callAddr == LLDB_INVALID_ADDRESS) {
        result.AppendErrorWithFormat("Invalid destination address.");
        return;
      }

      if (!reg_ctx->SetPC(callAddr)) {
        result.AppendErrorWithFormat("Error changing PC value for thread %d.",
                                     thread->GetIndexID());
        return;
      }
    } else {
      // Pick either the absolute line, or work out a relative one.
      int32_t line = (int32_t)m_options.m_line_num;
      if (line == 0)
        line = sym_ctx.line_entry.line + m_options.m_line_offset;

      // Try the current file, but override if asked.
      FileSpec file = sym_ctx.line_entry.GetFile();
      if (m_options.m_filenames.GetSize() == 1)
        file = m_options.m_filenames.GetFileSpecAtIndex(0);

      if (!file) {
        result.AppendErrorWithFormat(
            "No source file available for the current location.");
        return;
      }

      std::string warnings;
      Status err = thread->JumpToLine(file, line, m_options.m_force, &warnings);

      if (err.Fail()) {
        result.SetError(err);
        return;
      }

      if (!warnings.empty())
        result.AppendWarning(warnings.c_str());
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }

  CommandOptions m_options;
};

// Next are the subcommands of CommandObjectMultiwordThreadPlan

// CommandObjectThreadPlanList
#define LLDB_OPTIONS_thread_plan_list
#include "CommandOptions.inc"

class CommandObjectThreadPlanList : public CommandObjectIterateOverThreads {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'i':
        m_internal = true;
        break;
      case 't':
        lldb::tid_t tid;
        if (option_arg.getAsInteger(0, tid))
          return Status("invalid tid: '%s'.", option_arg.str().c_str());
        m_tids.push_back(tid);
        break;
      case 'u':
        m_unreported = false;
        break;
      case 'v':
        m_verbose = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return {};
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_verbose = false;
      m_internal = false;
      m_unreported = true; // The variable is "skip unreported" and we want to
                           // skip unreported by default.
      m_tids.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_plan_list_options);
    }

    // Instance variables to hold the values for command options.
    bool m_verbose;
    bool m_internal;
    bool m_unreported;
    std::vector<lldb::tid_t> m_tids;
  };

  CommandObjectThreadPlanList(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread plan list",
            "Show thread plans for one or more threads.  If no threads are "
            "specified, show the "
            "current thread.  Use the thread-index \"all\" to see all threads.",
            nullptr,
            eCommandRequiresProcess | eCommandRequiresThread |
                eCommandTryTargetAPILock | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused) {}

  ~CommandObjectThreadPlanList() override = default;

  Options *GetOptions() override { return &m_options; }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    // If we are reporting all threads, dispatch to the Process to do that:
    if (command.GetArgumentCount() == 0 && m_options.m_tids.empty()) {
      Stream &strm = result.GetOutputStream();
      DescriptionLevel desc_level = m_options.m_verbose
                                        ? eDescriptionLevelVerbose
                                        : eDescriptionLevelFull;
      m_exe_ctx.GetProcessPtr()->DumpThreadPlans(
          strm, desc_level, m_options.m_internal, true, m_options.m_unreported);
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return;
    } else {
      // Do any TID's that the user may have specified as TID, then do any
      // Thread Indexes...
      if (!m_options.m_tids.empty()) {
        Process *process = m_exe_ctx.GetProcessPtr();
        StreamString tmp_strm;
        for (lldb::tid_t tid : m_options.m_tids) {
          bool success = process->DumpThreadPlansForTID(
              tmp_strm, tid, eDescriptionLevelFull, m_options.m_internal,
              true /* condense_trivial */, m_options.m_unreported);
          // If we didn't find a TID, stop here and return an error.
          if (!success) {
            result.AppendError("Error dumping plans:");
            result.AppendError(tmp_strm.GetString());
            return;
          }
          // Otherwise, add our data to the output:
          result.GetOutputStream() << tmp_strm.GetString();
        }
      }
      return CommandObjectIterateOverThreads::DoExecute(command, result);
    }
  }

protected:
  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    // If we have already handled this from a -t option, skip it here.
    if (llvm::is_contained(m_options.m_tids, tid))
      return true;

    Process *process = m_exe_ctx.GetProcessPtr();

    Stream &strm = result.GetOutputStream();
    DescriptionLevel desc_level = eDescriptionLevelFull;
    if (m_options.m_verbose)
      desc_level = eDescriptionLevelVerbose;

    process->DumpThreadPlansForTID(strm, tid, desc_level, m_options.m_internal,
                                   true /* condense_trivial */,
                                   m_options.m_unreported);
    return true;
  }

  CommandOptions m_options;
};

class CommandObjectThreadPlanDiscard : public CommandObjectParsed {
public:
  CommandObjectThreadPlanDiscard(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "thread plan discard",
                            "Discards thread plans up to and including the "
                            "specified index (see 'thread plan list'.)  "
                            "Only user visible plans can be discarded.",
                            nullptr,
                            eCommandRequiresProcess | eCommandRequiresThread |
                                eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypeUnsignedInteger);
  }

  ~CommandObjectThreadPlanDiscard() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (!m_exe_ctx.HasThreadScope() || request.GetCursorIndex())
      return;

    m_exe_ctx.GetThreadPtr()->AutoCompleteThreadPlans(request);
  }

  void DoExecute(Args &args, CommandReturnObject &result) override {
    Thread *thread = m_exe_ctx.GetThreadPtr();
    if (args.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat("Too many arguments, expected one - the "
                                   "thread plan index - but got %zu.",
                                   args.GetArgumentCount());
      return;
    }

    uint32_t thread_plan_idx;
    if (!llvm::to_integer(args.GetArgumentAtIndex(0), thread_plan_idx)) {
      result.AppendErrorWithFormat(
          "Invalid thread index: \"%s\" - should be unsigned int.",
          args.GetArgumentAtIndex(0));
      return;
    }

    if (thread_plan_idx == 0) {
      result.AppendErrorWithFormat(
          "You wouldn't really want me to discard the base thread plan.");
      return;
    }

    if (thread->DiscardUserThreadPlansUpToIndex(thread_plan_idx)) {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendErrorWithFormat(
          "Could not find User thread plan with index %s.",
          args.GetArgumentAtIndex(0));
    }
  }
};

class CommandObjectThreadPlanPrune : public CommandObjectParsed {
public:
  CommandObjectThreadPlanPrune(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "thread plan prune",
                            "Removes any thread plans associated with "
                            "currently unreported threads.  "
                            "Specify one or more TID's to remove, or if no "
                            "TID's are provides, remove threads for all "
                            "unreported threads",
                            nullptr,
                            eCommandRequiresProcess |
                                eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypeThreadID, eArgRepeatStar);
  }

  ~CommandObjectThreadPlanPrune() override = default;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();

    if (args.GetArgumentCount() == 0) {
      process->PruneThreadPlans();
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return;
    }

    const size_t num_args = args.GetArgumentCount();

    std::lock_guard<std::recursive_mutex> guard(
        process->GetThreadList().GetMutex());

    for (size_t i = 0; i < num_args; i++) {
      lldb::tid_t tid;
      if (!llvm::to_integer(args.GetArgumentAtIndex(i), tid)) {
        result.AppendErrorWithFormat("invalid thread specification: \"%s\"\n",
                                     args.GetArgumentAtIndex(i));
        return;
      }
      if (!process->PruneThreadPlansForTID(tid)) {
        result.AppendErrorWithFormat("Could not find unreported tid: \"%s\"\n",
                                     args.GetArgumentAtIndex(i));
        return;
      }
    }
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }
};

// CommandObjectMultiwordThreadPlan

class CommandObjectMultiwordThreadPlan : public CommandObjectMultiword {
public:
  CommandObjectMultiwordThreadPlan(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "plan",
            "Commands for managing thread plans that control execution.",
            "thread plan <subcommand> [<subcommand objects]") {
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectThreadPlanList(interpreter)));
    LoadSubCommand(
        "discard",
        CommandObjectSP(new CommandObjectThreadPlanDiscard(interpreter)));
    LoadSubCommand(
        "prune",
        CommandObjectSP(new CommandObjectThreadPlanPrune(interpreter)));
  }

  ~CommandObjectMultiwordThreadPlan() override = default;
};

// Next are the subcommands of CommandObjectMultiwordTrace

// CommandObjectTraceExport

class CommandObjectTraceExport : public CommandObjectMultiword {
public:
  CommandObjectTraceExport(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "trace thread export",
            "Commands for exporting traces of the threads in the current "
            "process to different formats.",
            "thread trace export <export-plugin> [<subcommand objects>]") {

    unsigned i = 0;
    for (llvm::StringRef plugin_name =
             PluginManager::GetTraceExporterPluginNameAtIndex(i);
         !plugin_name.empty();
         plugin_name = PluginManager::GetTraceExporterPluginNameAtIndex(i++)) {
      if (ThreadTraceExportCommandCreator command_creator =
              PluginManager::GetThreadTraceExportCommandCreatorAtIndex(i)) {
        LoadSubCommand(plugin_name, command_creator(interpreter));
      }
    }
  }
};

// CommandObjectTraceStart

class CommandObjectTraceStart : public CommandObjectTraceProxy {
public:
  CommandObjectTraceStart(CommandInterpreter &interpreter)
      : CommandObjectTraceProxy(
            /*live_debug_session_only=*/true, interpreter, "thread trace start",
            "Start tracing threads with the corresponding trace "
            "plug-in for the current process.",
            "thread trace start [<trace-options>]") {}

protected:
  lldb::CommandObjectSP GetDelegateCommand(Trace &trace) override {
    return trace.GetThreadTraceStartCommand(m_interpreter);
  }
};

// CommandObjectTraceStop

class CommandObjectTraceStop : public CommandObjectMultipleThreads {
public:
  CommandObjectTraceStop(CommandInterpreter &interpreter)
      : CommandObjectMultipleThreads(
            interpreter, "thread trace stop",
            "Stop tracing threads, including the ones traced with the "
            "\"process trace start\" command."
            "Defaults to the current thread. Thread indices can be "
            "specified as arguments.\n Use the thread-index \"all\" to stop "
            "tracing "
            "for all existing threads.",
            "thread trace stop [<thread-index> <thread-index> ...]",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused |
                eCommandProcessMustBeTraced) {}

  ~CommandObjectTraceStop() override = default;

  bool DoExecuteOnThreads(Args &command, CommandReturnObject &result,
                          llvm::ArrayRef<lldb::tid_t> tids) override {
    ProcessSP process_sp = m_exe_ctx.GetProcessSP();

    TraceSP trace_sp = process_sp->GetTarget().GetTrace();

    if (llvm::Error err = trace_sp->Stop(tids))
      result.AppendError(toString(std::move(err)));
    else
      result.SetStatus(eReturnStatusSuccessFinishResult);

    return result.Succeeded();
  }
};

static ThreadSP GetSingleThreadFromArgs(ExecutionContext &exe_ctx, Args &args,
                                        CommandReturnObject &result) {
  if (args.GetArgumentCount() == 0)
    return exe_ctx.GetThreadSP();

  const char *arg = args.GetArgumentAtIndex(0);
  uint32_t thread_idx;

  if (!llvm::to_integer(arg, thread_idx)) {
    result.AppendErrorWithFormat("invalid thread specification: \"%s\"\n", arg);
    return nullptr;
  }
  ThreadSP thread_sp =
      exe_ctx.GetProcessRef().GetThreadList().FindThreadByIndexID(thread_idx);
  if (!thread_sp)
    result.AppendErrorWithFormat("no thread with index: \"%s\"\n", arg);
  return thread_sp;
}

// CommandObjectTraceDumpFunctionCalls
#define LLDB_OPTIONS_thread_trace_dump_function_calls
#include "CommandOptions.inc"

class CommandObjectTraceDumpFunctionCalls : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'j': {
        m_dumper_options.json = true;
        break;
      }
      case 'J': {
        m_dumper_options.json = true;
        m_dumper_options.pretty_print_json = true;
        break;
      }
      case 'F': {
        m_output_file.emplace(option_arg);
        break;
      }
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_dumper_options = {};
      m_output_file = std::nullopt;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_trace_dump_function_calls_options);
    }

    static const size_t kDefaultCount = 20;

    // Instance variables to hold the values for command options.
    TraceDumperOptions m_dumper_options;
    std::optional<FileSpec> m_output_file;
  };

  CommandObjectTraceDumpFunctionCalls(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread trace dump function-calls",
            "Dump the traced function-calls for one thread. If no "
            "thread is specified, the current thread is used.",
            nullptr,
            eCommandRequiresProcess | eCommandRequiresThread |
                eCommandTryTargetAPILock | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused | eCommandProcessMustBeTraced) {
    AddSimpleArgumentList(eArgTypeThreadIndex, eArgRepeatOptional);
  }

  ~CommandObjectTraceDumpFunctionCalls() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    ThreadSP thread_sp = GetSingleThreadFromArgs(m_exe_ctx, args, result);
    if (!thread_sp) {
      result.AppendError("invalid thread\n");
      return;
    }

    llvm::Expected<TraceCursorSP> cursor_or_error =
        m_exe_ctx.GetTargetSP()->GetTrace()->CreateNewCursor(*thread_sp);

    if (!cursor_or_error) {
      result.AppendError(llvm::toString(cursor_or_error.takeError()));
      return;
    }
    TraceCursorSP &cursor_sp = *cursor_or_error;

    std::optional<StreamFile> out_file;
    if (m_options.m_output_file) {
      out_file.emplace(m_options.m_output_file->GetPath().c_str(),
                       File::eOpenOptionWriteOnly | File::eOpenOptionCanCreate |
                           File::eOpenOptionTruncate);
    }

    m_options.m_dumper_options.forwards = true;

    TraceDumper dumper(std::move(cursor_sp),
                       out_file ? *out_file : result.GetOutputStream(),
                       m_options.m_dumper_options);

    dumper.DumpFunctionCalls();
  }

  CommandOptions m_options;
};

// CommandObjectTraceDumpInstructions
#define LLDB_OPTIONS_thread_trace_dump_instructions
#include "CommandOptions.inc"

class CommandObjectTraceDumpInstructions : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'c': {
        int32_t count;
        if (option_arg.empty() || option_arg.getAsInteger(0, count) ||
            count < 0)
          error.SetErrorStringWithFormat(
              "invalid integer value for option '%s'",
              option_arg.str().c_str());
        else
          m_count = count;
        break;
      }
      case 'a': {
        m_count = std::numeric_limits<decltype(m_count)>::max();
        break;
      }
      case 's': {
        int32_t skip;
        if (option_arg.empty() || option_arg.getAsInteger(0, skip) || skip < 0)
          error.SetErrorStringWithFormat(
              "invalid integer value for option '%s'",
              option_arg.str().c_str());
        else
          m_dumper_options.skip = skip;
        break;
      }
      case 'i': {
        uint64_t id;
        if (option_arg.empty() || option_arg.getAsInteger(0, id))
          error.SetErrorStringWithFormat(
              "invalid integer value for option '%s'",
              option_arg.str().c_str());
        else
          m_dumper_options.id = id;
        break;
      }
      case 'F': {
        m_output_file.emplace(option_arg);
        break;
      }
      case 'r': {
        m_dumper_options.raw = true;
        break;
      }
      case 'f': {
        m_dumper_options.forwards = true;
        break;
      }
      case 'k': {
        m_dumper_options.show_control_flow_kind = true;
        break;
      }
      case 't': {
        m_dumper_options.show_timestamps = true;
        break;
      }
      case 'e': {
        m_dumper_options.show_events = true;
        break;
      }
      case 'j': {
        m_dumper_options.json = true;
        break;
      }
      case 'J': {
        m_dumper_options.pretty_print_json = true;
        m_dumper_options.json = true;
        break;
      }
      case 'E': {
        m_dumper_options.only_events = true;
        m_dumper_options.show_events = true;
        break;
      }
      case 'C': {
        m_continue = true;
        break;
      }
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_count = kDefaultCount;
      m_continue = false;
      m_output_file = std::nullopt;
      m_dumper_options = {};
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_trace_dump_instructions_options);
    }

    static const size_t kDefaultCount = 20;

    // Instance variables to hold the values for command options.
    size_t m_count;
    size_t m_continue;
    std::optional<FileSpec> m_output_file;
    TraceDumperOptions m_dumper_options;
  };

  CommandObjectTraceDumpInstructions(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread trace dump instructions",
            "Dump the traced instructions for one thread. If no "
            "thread is specified, show the current thread.",
            nullptr,
            eCommandRequiresProcess | eCommandRequiresThread |
                eCommandTryTargetAPILock | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused | eCommandProcessMustBeTraced) {
    AddSimpleArgumentList(eArgTypeThreadIndex, eArgRepeatOptional);
  }

  ~CommandObjectTraceDumpInstructions() override = default;

  Options *GetOptions() override { return &m_options; }

  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override {
    std::string cmd;
    current_command_args.GetCommandString(cmd);
    if (cmd.find(" --continue") == std::string::npos)
      cmd += " --continue";
    return cmd;
  }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    ThreadSP thread_sp = GetSingleThreadFromArgs(m_exe_ctx, args, result);
    if (!thread_sp) {
      result.AppendError("invalid thread\n");
      return;
    }

    if (m_options.m_continue && m_last_id) {
      // We set up the options to continue one instruction past where
      // the previous iteration stopped.
      m_options.m_dumper_options.skip = 1;
      m_options.m_dumper_options.id = m_last_id;
    }

    llvm::Expected<TraceCursorSP> cursor_or_error =
        m_exe_ctx.GetTargetSP()->GetTrace()->CreateNewCursor(*thread_sp);

    if (!cursor_or_error) {
      result.AppendError(llvm::toString(cursor_or_error.takeError()));
      return;
    }
    TraceCursorSP &cursor_sp = *cursor_or_error;

    if (m_options.m_dumper_options.id &&
        !cursor_sp->HasId(*m_options.m_dumper_options.id)) {
      result.AppendError("invalid instruction id\n");
      return;
    }

    std::optional<StreamFile> out_file;
    if (m_options.m_output_file) {
      out_file.emplace(m_options.m_output_file->GetPath().c_str(),
                       File::eOpenOptionWriteOnly | File::eOpenOptionCanCreate |
                           File::eOpenOptionTruncate);
    }

    if (m_options.m_continue && !m_last_id) {
      // We need to stop processing data when we already ran out of instructions
      // in a previous command. We can fake this by setting the cursor past the
      // end of the trace.
      cursor_sp->Seek(1, lldb::eTraceCursorSeekTypeEnd);
    }

    TraceDumper dumper(std::move(cursor_sp),
                       out_file ? *out_file : result.GetOutputStream(),
                       m_options.m_dumper_options);

    m_last_id = dumper.DumpInstructions(m_options.m_count);
  }

  CommandOptions m_options;
  // Last traversed id used to continue a repeat command. std::nullopt means
  // that all the trace has been consumed.
  std::optional<lldb::user_id_t> m_last_id;
};

// CommandObjectTraceDumpInfo
#define LLDB_OPTIONS_thread_trace_dump_info
#include "CommandOptions.inc"

class CommandObjectTraceDumpInfo : public CommandObjectIterateOverThreads {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'v': {
        m_verbose = true;
        break;
      }
      case 'j': {
        m_json = true;
        break;
      }
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_verbose = false;
      m_json = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_thread_trace_dump_info_options);
    }

    // Instance variables to hold the values for command options.
    bool m_verbose;
    bool m_json;
  };

  CommandObjectTraceDumpInfo(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread trace dump info",
            "Dump the traced information for one or more threads.  If no "
            "threads are specified, show the current thread. Use the "
            "thread-index \"all\" to see all threads.",
            nullptr,
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused |
                eCommandProcessMustBeTraced) {}

  ~CommandObjectTraceDumpInfo() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    const TraceSP &trace_sp = m_exe_ctx.GetTargetSP()->GetTrace();
    ThreadSP thread_sp =
        m_exe_ctx.GetProcessPtr()->GetThreadList().FindThreadByID(tid);
    trace_sp->DumpTraceInfo(*thread_sp, result.GetOutputStream(),
                            m_options.m_verbose, m_options.m_json);
    return true;
  }

  CommandOptions m_options;
};

// CommandObjectMultiwordTraceDump
class CommandObjectMultiwordTraceDump : public CommandObjectMultiword {
public:
  CommandObjectMultiwordTraceDump(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "dump",
            "Commands for displaying trace information of the threads "
            "in the current process.",
            "thread trace dump <subcommand> [<subcommand objects>]") {
    LoadSubCommand(
        "instructions",
        CommandObjectSP(new CommandObjectTraceDumpInstructions(interpreter)));
    LoadSubCommand(
        "function-calls",
        CommandObjectSP(new CommandObjectTraceDumpFunctionCalls(interpreter)));
    LoadSubCommand(
        "info", CommandObjectSP(new CommandObjectTraceDumpInfo(interpreter)));
  }
  ~CommandObjectMultiwordTraceDump() override = default;
};

// CommandObjectMultiwordTrace
class CommandObjectMultiwordTrace : public CommandObjectMultiword {
public:
  CommandObjectMultiwordTrace(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "trace",
            "Commands for operating on traces of the threads in the current "
            "process.",
            "thread trace <subcommand> [<subcommand objects>]") {
    LoadSubCommand("dump", CommandObjectSP(new CommandObjectMultiwordTraceDump(
                               interpreter)));
    LoadSubCommand("start",
                   CommandObjectSP(new CommandObjectTraceStart(interpreter)));
    LoadSubCommand("stop",
                   CommandObjectSP(new CommandObjectTraceStop(interpreter)));
    LoadSubCommand("export",
                   CommandObjectSP(new CommandObjectTraceExport(interpreter)));
  }

  ~CommandObjectMultiwordTrace() override = default;
};

// CommandObjectMultiwordThread

CommandObjectMultiwordThread::CommandObjectMultiwordThread(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "thread",
                             "Commands for operating on "
                             "one or more threads in "
                             "the current process.",
                             "thread <subcommand> [<subcommand-options>]") {
  LoadSubCommand("backtrace", CommandObjectSP(new CommandObjectThreadBacktrace(
                                  interpreter)));
  LoadSubCommand("continue",
                 CommandObjectSP(new CommandObjectThreadContinue(interpreter)));
  LoadSubCommand("list",
                 CommandObjectSP(new CommandObjectThreadList(interpreter)));
  LoadSubCommand("return",
                 CommandObjectSP(new CommandObjectThreadReturn(interpreter)));
  LoadSubCommand("jump",
                 CommandObjectSP(new CommandObjectThreadJump(interpreter)));
  LoadSubCommand("select",
                 CommandObjectSP(new CommandObjectThreadSelect(interpreter)));
  LoadSubCommand("until",
                 CommandObjectSP(new CommandObjectThreadUntil(interpreter)));
  LoadSubCommand("info",
                 CommandObjectSP(new CommandObjectThreadInfo(interpreter)));
  LoadSubCommand("exception", CommandObjectSP(new CommandObjectThreadException(
                                  interpreter)));
  LoadSubCommand("siginfo",
                 CommandObjectSP(new CommandObjectThreadSiginfo(interpreter)));
  LoadSubCommand("step-in",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-in",
                     "Source level single step, stepping into calls.  Defaults "
                     "to current thread unless specified.",
                     nullptr, eStepTypeInto, eStepScopeSource)));

  LoadSubCommand("step-out",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-out",
                     "Finish executing the current stack frame and stop after "
                     "returning.  Defaults to current thread unless specified.",
                     nullptr, eStepTypeOut, eStepScopeSource)));

  LoadSubCommand("step-over",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-over",
                     "Source level single step, stepping over calls.  Defaults "
                     "to current thread unless specified.",
                     nullptr, eStepTypeOver, eStepScopeSource)));

  LoadSubCommand("step-inst",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-inst",
                     "Instruction level single step, stepping into calls.  "
                     "Defaults to current thread unless specified.",
                     nullptr, eStepTypeTrace, eStepScopeInstruction)));

  LoadSubCommand("step-inst-over",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-inst-over",
                     "Instruction level single step, stepping over calls.  "
                     "Defaults to current thread unless specified.",
                     nullptr, eStepTypeTraceOver, eStepScopeInstruction)));

  LoadSubCommand(
      "step-scripted",
      CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
          interpreter, "thread step-scripted",
          "Step as instructed by the script class passed in the -C option.  "
          "You can also specify a dictionary of key (-k) and value (-v) pairs "
          "that will be used to populate an SBStructuredData Dictionary, which "
          "will be passed to the constructor of the class implementing the "
          "scripted step.  See the Python Reference for more details.",
          nullptr, eStepTypeScripted, eStepScopeSource)));

  LoadSubCommand("plan", CommandObjectSP(new CommandObjectMultiwordThreadPlan(
                             interpreter)));
  LoadSubCommand("trace",
                 CommandObjectSP(new CommandObjectMultiwordTrace(interpreter)));
}

CommandObjectMultiwordThread::~CommandObjectMultiwordThread() = default;
