//===-- CommandObjectProcess.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectProcess.h"
#include "CommandObjectBreakpoint.h"
#include "CommandObjectTrace.h"
#include "CommandOptionsProcessAttach.h"
#include "CommandOptionsProcessLaunch.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointIDList.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointName.h"
#include "lldb/Breakpoint/BreakpointSite.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupPythonClassWithDict.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/ScriptedMetadata.h"
#include "lldb/Utility/State.h"

#include "llvm/ADT/ScopeExit.h"

#include <bitset>
#include <optional>

using namespace lldb;
using namespace lldb_private;

class CommandObjectProcessLaunchOrAttach : public CommandObjectParsed {
public:
  CommandObjectProcessLaunchOrAttach(CommandInterpreter &interpreter,
                                     const char *name, const char *help,
                                     const char *syntax, uint32_t flags,
                                     const char *new_process_action)
      : CommandObjectParsed(interpreter, name, help, syntax, flags),
        m_new_process_action(new_process_action) {}

  ~CommandObjectProcessLaunchOrAttach() override = default;

protected:
  bool StopProcessIfNecessary(Process *process, StateType &state,
                              CommandReturnObject &result) {
    state = eStateInvalid;
    if (process) {
      state = process->GetState();

      if (process->IsAlive() && state != eStateConnected) {
        std::string message;
        if (process->GetState() == eStateAttaching)
          message =
              llvm::formatv("There is a pending attach, abort it and {0}?",
                            m_new_process_action);
        else if (process->GetShouldDetach())
          message = llvm::formatv(
              "There is a running process, detach from it and {0}?",
              m_new_process_action);
        else
          message =
              llvm::formatv("There is a running process, kill it and {0}?",
                            m_new_process_action);

        if (!m_interpreter.Confirm(message, true)) {
          result.SetStatus(eReturnStatusFailed);
          return false;
        } else {
          if (process->GetShouldDetach()) {
            bool keep_stopped = false;
            Status detach_error(process->Detach(keep_stopped));
            if (detach_error.Success()) {
              result.SetStatus(eReturnStatusSuccessFinishResult);
              process = nullptr;
            } else {
              result.AppendErrorWithFormat(
                  "Failed to detach from process: %s\n",
                  detach_error.AsCString());
            }
          } else {
            Status destroy_error(process->Destroy(false));
            if (destroy_error.Success()) {
              result.SetStatus(eReturnStatusSuccessFinishResult);
              process = nullptr;
            } else {
              result.AppendErrorWithFormat("Failed to kill process: %s\n",
                                           destroy_error.AsCString());
            }
          }
        }
      }
    }
    return result.Succeeded();
  }

  std::string m_new_process_action;
};

// CommandObjectProcessLaunch
#pragma mark CommandObjectProcessLaunch
class CommandObjectProcessLaunch : public CommandObjectProcessLaunchOrAttach {
public:
  CommandObjectProcessLaunch(CommandInterpreter &interpreter)
      : CommandObjectProcessLaunchOrAttach(
            interpreter, "process launch",
            "Launch the executable in the debugger.", nullptr,
            eCommandRequiresTarget, "restart"),

        m_class_options("scripted process", true, 'C', 'k', 'v', 0) {
    m_all_options.Append(&m_options);
    m_all_options.Append(&m_class_options, LLDB_OPT_SET_1 | LLDB_OPT_SET_2,
                         LLDB_OPT_SET_ALL);
    m_all_options.Finalize();

    AddSimpleArgumentList(eArgTypeRunArgs, eArgRepeatOptional);
  }

  ~CommandObjectProcessLaunch() override = default;

  Options *GetOptions() override { return &m_all_options; }

  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override {
    // No repeat for "process launch"...
    return std::string("");
  }

protected:
  void DoExecute(Args &launch_args, CommandReturnObject &result) override {
    Debugger &debugger = GetDebugger();
    Target *target = debugger.GetSelectedTarget().get();
    // If our listener is nullptr, users aren't allows to launch
    ModuleSP exe_module_sp = target->GetExecutableModule();

    // If the target already has an executable module, then use that.  If it
    // doesn't then someone must be trying to launch using a path that will
    // make sense to the remote stub, but doesn't exist on the local host.
    // In that case use the ExecutableFile that was set in the target's
    // ProcessLaunchInfo.
    if (exe_module_sp == nullptr && !target->GetProcessLaunchInfo().GetExecutableFile()) {
      result.AppendError("no file in target, create a debug target using the "
                         "'target create' command");
      return;
    }

    StateType state = eStateInvalid;

    if (!StopProcessIfNecessary(m_exe_ctx.GetProcessPtr(), state, result))
      return;

    // Determine whether we will disable ASLR or leave it in the default state
    // (i.e. enabled if the platform supports it). First check if the process
    // launch options explicitly turn on/off
    // disabling ASLR.  If so, use that setting;
    // otherwise, use the 'settings target.disable-aslr' setting.
    bool disable_aslr = false;
    if (m_options.disable_aslr != eLazyBoolCalculate) {
      // The user specified an explicit setting on the process launch line.
      // Use it.
      disable_aslr = (m_options.disable_aslr == eLazyBoolYes);
    } else {
      // The user did not explicitly specify whether to disable ASLR.  Fall
      // back to the target.disable-aslr setting.
      disable_aslr = target->GetDisableASLR();
    }

    if (!m_class_options.GetName().empty()) {
      m_options.launch_info.SetProcessPluginName("ScriptedProcess");
      ScriptedMetadataSP metadata_sp = std::make_shared<ScriptedMetadata>(
          m_class_options.GetName(), m_class_options.GetStructuredData());
      m_options.launch_info.SetScriptedMetadata(metadata_sp);
      target->SetProcessLaunchInfo(m_options.launch_info);
    }

    if (disable_aslr)
      m_options.launch_info.GetFlags().Set(eLaunchFlagDisableASLR);
    else
      m_options.launch_info.GetFlags().Clear(eLaunchFlagDisableASLR);

    if (target->GetInheritTCC())
      m_options.launch_info.GetFlags().Set(eLaunchFlagInheritTCCFromParent);

    if (target->GetDetachOnError())
      m_options.launch_info.GetFlags().Set(eLaunchFlagDetachOnError);

    if (target->GetDisableSTDIO())
      m_options.launch_info.GetFlags().Set(eLaunchFlagDisableSTDIO);

    // Merge the launch info environment with the target environment.
    Environment target_env = target->GetEnvironment();
    m_options.launch_info.GetEnvironment().insert(target_env.begin(),
                                                  target_env.end());

    llvm::StringRef target_settings_argv0 = target->GetArg0();

    if (!target_settings_argv0.empty()) {
      m_options.launch_info.GetArguments().AppendArgument(
          target_settings_argv0);
      if (exe_module_sp)
        m_options.launch_info.SetExecutableFile(
            exe_module_sp->GetPlatformFileSpec(), false);
      else
        m_options.launch_info.SetExecutableFile(target->GetProcessLaunchInfo().GetExecutableFile(), false);
    } else {
      if (exe_module_sp)
        m_options.launch_info.SetExecutableFile(
            exe_module_sp->GetPlatformFileSpec(), true);
      else
        m_options.launch_info.SetExecutableFile(target->GetProcessLaunchInfo().GetExecutableFile(), true);
    }

    if (launch_args.GetArgumentCount() == 0) {
      m_options.launch_info.GetArguments().AppendArguments(
          target->GetProcessLaunchInfo().GetArguments());
    } else {
      m_options.launch_info.GetArguments().AppendArguments(launch_args);
      // Save the arguments for subsequent runs in the current target.
      target->SetRunArguments(launch_args);
    }

    StreamString stream;
    Status error = target->Launch(m_options.launch_info, &stream);

    if (error.Success()) {
      ProcessSP process_sp(target->GetProcessSP());
      if (process_sp) {
        // There is a race condition where this thread will return up the call
        // stack to the main command handler and show an (lldb) prompt before
        // HandlePrivateEvent (from PrivateStateThread) has a chance to call
        // PushProcessIOHandler().
        process_sp->SyncIOHandler(0, std::chrono::seconds(2));

        // If we didn't have a local executable, then we wouldn't have had an
        // executable module before launch.
        if (!exe_module_sp)
          exe_module_sp = target->GetExecutableModule();
        if (!exe_module_sp) {
          result.AppendWarning("Could not get executable module after launch.");
        } else {

          const char *archname =
              exe_module_sp->GetArchitecture().GetArchitectureName();
          result.AppendMessageWithFormat(
              "Process %" PRIu64 " launched: '%s' (%s)\n", process_sp->GetID(),
              exe_module_sp->GetFileSpec().GetPath().c_str(), archname);
        }
        result.SetStatus(eReturnStatusSuccessFinishResult);
        // This message will refer to an event that happened after the process
        // launched.
        llvm::StringRef data = stream.GetString();
        if (!data.empty())
          result.AppendMessage(data);
        result.SetDidChangeProcessState(true);
      } else {
        result.AppendError(
            "no error returned from Target::Launch, and target has no process");
      }
    } else {
      result.AppendError(error.AsCString());
    }
  }

  CommandOptionsProcessLaunch m_options;
  OptionGroupPythonClassWithDict m_class_options;
  OptionGroupOptions m_all_options;
};

#define LLDB_OPTIONS_process_attach
#include "CommandOptions.inc"

#pragma mark CommandObjectProcessAttach
class CommandObjectProcessAttach : public CommandObjectProcessLaunchOrAttach {
public:
  CommandObjectProcessAttach(CommandInterpreter &interpreter)
      : CommandObjectProcessLaunchOrAttach(
            interpreter, "process attach", "Attach to a process.",
            "process attach <cmd-options>", 0, "attach"),
        m_class_options("scripted process", true, 'C', 'k', 'v', 0) {
    m_all_options.Append(&m_options);
    m_all_options.Append(&m_class_options, LLDB_OPT_SET_1 | LLDB_OPT_SET_2,
                         LLDB_OPT_SET_ALL);
    m_all_options.Finalize();
  }

  ~CommandObjectProcessAttach() override = default;

  Options *GetOptions() override { return &m_all_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());

    Target *target = GetDebugger().GetSelectedTarget().get();
    // N.B. The attach should be synchronous.  It doesn't help much to get the
    // prompt back between initiating the attach and the target actually
    // stopping.  So even if the interpreter is set to be asynchronous, we wait
    // for the stop ourselves here.

    StateType state = eStateInvalid;
    Process *process = m_exe_ctx.GetProcessPtr();

    if (!StopProcessIfNecessary(process, state, result))
      return;

    if (target == nullptr) {
      // If there isn't a current target create one.
      TargetSP new_target_sp;
      Status error;

      error = GetDebugger().GetTargetList().CreateTarget(
          GetDebugger(), "", "", eLoadDependentsNo,
          nullptr, // No platform options
          new_target_sp);
      target = new_target_sp.get();
      if (target == nullptr || error.Fail()) {
        result.AppendError(error.AsCString("Error creating target"));
        return;
      }
    }

    if (!m_class_options.GetName().empty()) {
      m_options.attach_info.SetProcessPluginName("ScriptedProcess");
      ScriptedMetadataSP metadata_sp = std::make_shared<ScriptedMetadata>(
          m_class_options.GetName(), m_class_options.GetStructuredData());
      m_options.attach_info.SetScriptedMetadata(metadata_sp);
    }

    // Record the old executable module, we want to issue a warning if the
    // process of attaching changed the current executable (like somebody said
    // "file foo" then attached to a PID whose executable was bar.)

    ModuleSP old_exec_module_sp = target->GetExecutableModule();
    ArchSpec old_arch_spec = target->GetArchitecture();

    StreamString stream;
    ProcessSP process_sp;
    const auto error = target->Attach(m_options.attach_info, &stream);
    if (error.Success()) {
      process_sp = target->GetProcessSP();
      if (process_sp) {
        result.AppendMessage(stream.GetString());
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
        result.SetDidChangeProcessState(true);
      } else {
        result.AppendError(
            "no error returned from Target::Attach, and target has no process");
      }
    } else {
      result.AppendErrorWithFormat("attach failed: %s\n", error.AsCString());
    }

    if (!result.Succeeded())
      return;

    // Okay, we're done.  Last step is to warn if the executable module has
    // changed:
    char new_path[PATH_MAX];
    ModuleSP new_exec_module_sp(target->GetExecutableModule());
    if (!old_exec_module_sp) {
      // We might not have a module if we attached to a raw pid...
      if (new_exec_module_sp) {
        new_exec_module_sp->GetFileSpec().GetPath(new_path, PATH_MAX);
        result.AppendMessageWithFormat("Executable module set to \"%s\".\n",
                                       new_path);
      }
    } else if (old_exec_module_sp->GetFileSpec() !=
               new_exec_module_sp->GetFileSpec()) {
      char old_path[PATH_MAX];

      old_exec_module_sp->GetFileSpec().GetPath(old_path, PATH_MAX);
      new_exec_module_sp->GetFileSpec().GetPath(new_path, PATH_MAX);

      result.AppendWarningWithFormat(
          "Executable module changed from \"%s\" to \"%s\".\n", old_path,
          new_path);
    }

    if (!old_arch_spec.IsValid()) {
      result.AppendMessageWithFormat(
          "Architecture set to: %s.\n",
          target->GetArchitecture().GetTriple().getTriple().c_str());
    } else if (!old_arch_spec.IsExactMatch(target->GetArchitecture())) {
      result.AppendWarningWithFormat(
          "Architecture changed from %s to %s.\n",
          old_arch_spec.GetTriple().getTriple().c_str(),
          target->GetArchitecture().GetTriple().getTriple().c_str());
    }

    // This supports the use-case scenario of immediately continuing the
    // process once attached.
    if (m_options.attach_info.GetContinueOnceAttached()) {
      // We have made a process but haven't told the interpreter about it yet,
      // so CheckRequirements will fail for "process continue".  Set the override
      // here:
      ExecutionContext exe_ctx(process_sp);
      m_interpreter.HandleCommand("process continue", eLazyBoolNo, exe_ctx, result);
    }
  }

  CommandOptionsProcessAttach m_options;
  OptionGroupPythonClassWithDict m_class_options;
  OptionGroupOptions m_all_options;
};

// CommandObjectProcessContinue

#define LLDB_OPTIONS_process_continue
#include "CommandOptions.inc"

#pragma mark CommandObjectProcessContinue

class CommandObjectProcessContinue : public CommandObjectParsed {
public:
  CommandObjectProcessContinue(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process continue",
            "Continue execution of all threads in the current process.",
            "process continue",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectProcessContinue() override = default;

protected:
  class CommandOptions : public Options {
  public:
    CommandOptions() {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *exe_ctx) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 'i':
        if (option_arg.getAsInteger(0, m_ignore))
          error.SetErrorStringWithFormat(
              "invalid value for ignore option: \"%s\", should be a number.",
              option_arg.str().c_str());
        break;
      case 'b':
        m_run_to_bkpt_args.AppendArgument(option_arg);
        m_any_bkpts_specified = true;
      break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_ignore = 0;
      m_run_to_bkpt_args.Clear();
      m_any_bkpts_specified = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_process_continue_options);
    }

    uint32_t m_ignore = 0;
    Args m_run_to_bkpt_args;
    bool m_any_bkpts_specified = false;
  };

  void DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    bool synchronous_execution = m_interpreter.GetSynchronous();
    StateType state = process->GetState();
    if (state == eStateStopped) {
      if (m_options.m_ignore > 0) {
        ThreadSP sel_thread_sp(GetDefaultThread()->shared_from_this());
        if (sel_thread_sp) {
          StopInfoSP stop_info_sp = sel_thread_sp->GetStopInfo();
          if (stop_info_sp &&
              stop_info_sp->GetStopReason() == eStopReasonBreakpoint) {
            lldb::break_id_t bp_site_id =
                (lldb::break_id_t)stop_info_sp->GetValue();
            BreakpointSiteSP bp_site_sp(
                process->GetBreakpointSiteList().FindByID(bp_site_id));
            if (bp_site_sp) {
              const size_t num_owners = bp_site_sp->GetNumberOfConstituents();
              for (size_t i = 0; i < num_owners; i++) {
                Breakpoint &bp_ref =
                    bp_site_sp->GetConstituentAtIndex(i)->GetBreakpoint();
                if (!bp_ref.IsInternal()) {
                  bp_ref.SetIgnoreCount(m_options.m_ignore);
                }
              }
            }
          }
        }
      }

      Target *target = m_exe_ctx.GetTargetPtr();
      BreakpointIDList run_to_bkpt_ids;
      // Don't pass an empty run_to_breakpoint list, as Verify will look for the
      // default breakpoint.
      if (m_options.m_run_to_bkpt_args.GetArgumentCount() > 0)
        CommandObjectMultiwordBreakpoint::VerifyBreakpointOrLocationIDs(
            m_options.m_run_to_bkpt_args, target, result, &run_to_bkpt_ids,
            BreakpointName::Permissions::disablePerm);
      if (!result.Succeeded()) {
        return;
      }
      result.Clear();
      if (m_options.m_any_bkpts_specified && run_to_bkpt_ids.GetSize() == 0) {
        result.AppendError("continue-to breakpoints did not specify any actual "
                           "breakpoints or locations");
        return;
      }

      // First figure out which breakpoints & locations were specified by the
      // user:
      size_t num_run_to_bkpt_ids = run_to_bkpt_ids.GetSize();
      std::vector<break_id_t> bkpts_disabled;
      std::vector<BreakpointID> locs_disabled;
      if (num_run_to_bkpt_ids != 0) {
        // Go through the ID's specified, and separate the breakpoints from are
        // the breakpoint.location specifications since the latter require
        // special handling.  We also figure out whether there's at least one
        // specifier in the set that is enabled.
        BreakpointList &bkpt_list = target->GetBreakpointList();
        std::unordered_set<break_id_t> bkpts_seen;
        std::unordered_set<break_id_t> bkpts_with_locs_seen;
        BreakpointIDList with_locs;
        bool any_enabled = false;

        for (size_t idx = 0; idx < num_run_to_bkpt_ids; idx++) {
          BreakpointID bkpt_id = run_to_bkpt_ids.GetBreakpointIDAtIndex(idx);
          break_id_t bp_id = bkpt_id.GetBreakpointID();
          break_id_t loc_id = bkpt_id.GetLocationID();
          BreakpointSP bp_sp
              = bkpt_list.FindBreakpointByID(bp_id);
          // Note, VerifyBreakpointOrLocationIDs checks for existence, so we
          // don't need to do it again here.
          if (bp_sp->IsEnabled()) {
            if (loc_id == LLDB_INVALID_BREAK_ID) {
              // A breakpoint (without location) was specified.  Make sure that
              // at least one of the locations is enabled.
              size_t num_locations = bp_sp->GetNumLocations();
              for (size_t loc_idx = 0; loc_idx < num_locations; loc_idx++) {
                BreakpointLocationSP loc_sp
                    = bp_sp->GetLocationAtIndex(loc_idx);
                if (loc_sp->IsEnabled()) {
                  any_enabled = true;
                  break;
                }
              }
            } else {
              // A location was specified, check if it was enabled:
              BreakpointLocationSP loc_sp = bp_sp->FindLocationByID(loc_id);
              if (loc_sp->IsEnabled())
                any_enabled = true;
            }

            // Then sort the bp & bp.loc entries for later use:
            if (bkpt_id.GetLocationID() == LLDB_INVALID_BREAK_ID)
              bkpts_seen.insert(bkpt_id.GetBreakpointID());
            else {
              bkpts_with_locs_seen.insert(bkpt_id.GetBreakpointID());
              with_locs.AddBreakpointID(bkpt_id);
            }
          }
        }
        // Do all the error checking here so once we start disabling we don't
        // have to back out half-way through.

        // Make sure at least one of the specified breakpoints is enabled.
        if (!any_enabled) {
          result.AppendError("at least one of the continue-to breakpoints must "
                             "be enabled.");
          return;
        }

        // Also, if you specify BOTH a breakpoint and one of it's locations,
        // we flag that as an error, since it won't do what you expect, the
        // breakpoint directive will mean "run to all locations", which is not
        // what the location directive means...
        for (break_id_t bp_id : bkpts_with_locs_seen) {
          if (bkpts_seen.count(bp_id)) {
            result.AppendErrorWithFormatv("can't specify both a breakpoint and "
                               "one of its locations: {0}", bp_id);
          }
        }

        // Now go through the breakpoints in the target, disabling all the ones
        // that the user didn't mention:
        for (BreakpointSP bp_sp : bkpt_list.Breakpoints()) {
          break_id_t bp_id = bp_sp->GetID();
          // Handle the case where no locations were specified.  Note we don't
          // have to worry about the case where a breakpoint and one of its
          // locations are both in the lists, we've already disallowed that.
          if (!bkpts_with_locs_seen.count(bp_id)) {
            if (!bkpts_seen.count(bp_id) && bp_sp->IsEnabled()) {
              bkpts_disabled.push_back(bp_id);
              bp_sp->SetEnabled(false);
            }
            continue;
          }
          // Next, handle the case where a location was specified:
          // Run through all the locations of this breakpoint and disable
          // the ones that aren't on our "with locations" BreakpointID list:
          size_t num_locations = bp_sp->GetNumLocations();
          BreakpointID tmp_id(bp_id, LLDB_INVALID_BREAK_ID);
          for (size_t loc_idx = 0; loc_idx < num_locations; loc_idx++) {
            BreakpointLocationSP loc_sp = bp_sp->GetLocationAtIndex(loc_idx);
            tmp_id.SetBreakpointLocationID(loc_idx);
            if (!with_locs.Contains(tmp_id) && loc_sp->IsEnabled()) {
              locs_disabled.push_back(tmp_id);
              loc_sp->SetEnabled(false);
            }
          }
        }
      }

      { // Scope for thread list mutex:
        std::lock_guard<std::recursive_mutex> guard(
            process->GetThreadList().GetMutex());
        const uint32_t num_threads = process->GetThreadList().GetSize();

        // Set the actions that the threads should each take when resuming
        for (uint32_t idx = 0; idx < num_threads; ++idx) {
          const bool override_suspend = false;
          process->GetThreadList().GetThreadAtIndex(idx)->SetResumeState(
              eStateRunning, override_suspend);
        }
      }

      const uint32_t iohandler_id = process->GetIOHandlerID();

      StreamString stream;
      Status error;
      // For now we can only do -b with synchronous:
      bool old_sync = GetDebugger().GetAsyncExecution();

      if (run_to_bkpt_ids.GetSize() != 0) {
        GetDebugger().SetAsyncExecution(false);
        synchronous_execution = true;
      }
      if (synchronous_execution)
        error = process->ResumeSynchronous(&stream);
      else
        error = process->Resume();

      if (run_to_bkpt_ids.GetSize() != 0) {
        GetDebugger().SetAsyncExecution(old_sync);
      }

      // Now re-enable the breakpoints we disabled:
      BreakpointList &bkpt_list = target->GetBreakpointList();
      for (break_id_t bp_id : bkpts_disabled) {
        BreakpointSP bp_sp = bkpt_list.FindBreakpointByID(bp_id);
        if (bp_sp)
          bp_sp->SetEnabled(true);
      }
      for (const BreakpointID &bkpt_id : locs_disabled) {
        BreakpointSP bp_sp
            = bkpt_list.FindBreakpointByID(bkpt_id.GetBreakpointID());
        if (bp_sp) {
          BreakpointLocationSP loc_sp
              = bp_sp->FindLocationByID(bkpt_id.GetLocationID());
          if (loc_sp)
            loc_sp->SetEnabled(true);
        }
      }

      if (error.Success()) {
        // There is a race condition where this thread will return up the call
        // stack to the main command handler and show an (lldb) prompt before
        // HandlePrivateEvent (from PrivateStateThread) has a chance to call
        // PushProcessIOHandler().
        process->SyncIOHandler(iohandler_id, std::chrono::seconds(2));

        result.AppendMessageWithFormat("Process %" PRIu64 " resuming\n",
                                       process->GetID());
        if (synchronous_execution) {
          // If any state changed events had anything to say, add that to the
          // result
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
    } else {
      result.AppendErrorWithFormat(
          "Process cannot be continued from its current state (%s).\n",
          StateAsCString(state));
    }
  }

  Options *GetOptions() override { return &m_options; }

  CommandOptions m_options;
};

// CommandObjectProcessDetach
#define LLDB_OPTIONS_process_detach
#include "CommandOptions.inc"

#pragma mark CommandObjectProcessDetach

class CommandObjectProcessDetach : public CommandObjectParsed {
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
      case 's':
        bool tmp_result;
        bool success;
        tmp_result = OptionArgParser::ToBoolean(option_arg, false, &success);
        if (!success)
          error.SetErrorStringWithFormat("invalid boolean option: \"%s\"",
                                         option_arg.str().c_str());
        else {
          if (tmp_result)
            m_keep_stopped = eLazyBoolYes;
          else
            m_keep_stopped = eLazyBoolNo;
        }
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_keep_stopped = eLazyBoolCalculate;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_process_detach_options);
    }

    // Instance variables to hold the values for command options.
    LazyBool m_keep_stopped;
  };

  CommandObjectProcessDetach(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process detach",
                            "Detach from the current target process.",
                            "process detach",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectProcessDetach() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    // FIXME: This will be a Command Option:
    bool keep_stopped;
    if (m_options.m_keep_stopped == eLazyBoolCalculate) {
      // Check the process default:
      keep_stopped = process->GetDetachKeepsStopped();
    } else if (m_options.m_keep_stopped == eLazyBoolYes)
      keep_stopped = true;
    else
      keep_stopped = false;

    Status error(process->Detach(keep_stopped));
    if (error.Success()) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendErrorWithFormat("Detach failed: %s\n", error.AsCString());
    }
  }

  CommandOptions m_options;
};

// CommandObjectProcessConnect
#define LLDB_OPTIONS_process_connect
#include "CommandOptions.inc"

#pragma mark CommandObjectProcessConnect

class CommandObjectProcessConnect : public CommandObjectParsed {
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
      case 'p':
        plugin_name.assign(std::string(option_arg));
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      plugin_name.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_process_connect_options);
    }

    // Instance variables to hold the values for command options.

    std::string plugin_name;
  };

  CommandObjectProcessConnect(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process connect",
                            "Connect to a remote debug service.",
                            "process connect <remote-url>", 0) {
    AddSimpleArgumentList(eArgTypeConnectURL);
  }

  ~CommandObjectProcessConnect() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat(
          "'%s' takes exactly one argument:\nUsage: %s\n", m_cmd_name.c_str(),
          m_cmd_syntax.c_str());
      return;
    }

    Process *process = m_exe_ctx.GetProcessPtr();
    if (process && process->IsAlive()) {
      result.AppendErrorWithFormat(
          "Process %" PRIu64
          " is currently being debugged, kill the process before connecting.\n",
          process->GetID());
      return;
    }

    const char *plugin_name = nullptr;
    if (!m_options.plugin_name.empty())
      plugin_name = m_options.plugin_name.c_str();

    Status error;
    Debugger &debugger = GetDebugger();
    PlatformSP platform_sp = m_interpreter.GetPlatform(true);
    ProcessSP process_sp =
        debugger.GetAsyncExecution()
            ? platform_sp->ConnectProcess(
                  command.GetArgumentAtIndex(0), plugin_name, debugger,
                  debugger.GetSelectedTarget().get(), error)
            : platform_sp->ConnectProcessSynchronous(
                  command.GetArgumentAtIndex(0), plugin_name, debugger,
                  result.GetOutputStream(), debugger.GetSelectedTarget().get(),
                  error);
    if (error.Fail() || process_sp == nullptr) {
      result.AppendError(error.AsCString("Error connecting to the process"));
    }
  }

  CommandOptions m_options;
};

// CommandObjectProcessPlugin
#pragma mark CommandObjectProcessPlugin

class CommandObjectProcessPlugin : public CommandObjectProxy {
public:
  CommandObjectProcessPlugin(CommandInterpreter &interpreter)
      : CommandObjectProxy(
            interpreter, "process plugin",
            "Send a custom command to the current target process plug-in.",
            "process plugin <args>", 0) {}

  ~CommandObjectProcessPlugin() override = default;

  CommandObject *GetProxyCommandObject() override {
    Process *process = m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process)
      return process->GetPluginCommandObject();
    return nullptr;
  }
};

// CommandObjectProcessLoad
#define LLDB_OPTIONS_process_load
#include "CommandOptions.inc"

#pragma mark CommandObjectProcessLoad

class CommandObjectProcessLoad : public CommandObjectParsed {
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
      ArchSpec arch =
          execution_context->GetProcessPtr()->GetSystemArchitecture();
      switch (short_option) {
      case 'i':
        do_install = true;
        if (!option_arg.empty())
          install_path.SetFile(option_arg, arch.GetTriple());
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      do_install = false;
      install_path.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_process_load_options);
    }

    // Instance variables to hold the values for command options.
    bool do_install;
    FileSpec install_path;
  };

  CommandObjectProcessLoad(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process load",
                            "Load a shared library into the current process.",
                            "process load <filename> [<filename> ...]",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypePath, eArgRepeatPlus);
  }

  ~CommandObjectProcessLoad() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (!m_exe_ctx.HasProcessScope())
      return;
    CommandObject::HandleArgumentCompletion(request, opt_element_vector);
  }

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();

    for (auto &entry : command.entries()) {
      Status error;
      PlatformSP platform = process->GetTarget().GetPlatform();
      llvm::StringRef image_path = entry.ref();
      uint32_t image_token = LLDB_INVALID_IMAGE_TOKEN;

      if (!m_options.do_install) {
        FileSpec image_spec(image_path);
        platform->ResolveRemotePath(image_spec, image_spec);
        image_token =
            platform->LoadImage(process, FileSpec(), image_spec, error);
      } else if (m_options.install_path) {
        FileSpec image_spec(image_path);
        FileSystem::Instance().Resolve(image_spec);
        platform->ResolveRemotePath(m_options.install_path,
                                    m_options.install_path);
        image_token = platform->LoadImage(process, image_spec,
                                          m_options.install_path, error);
      } else {
        FileSpec image_spec(image_path);
        FileSystem::Instance().Resolve(image_spec);
        image_token =
            platform->LoadImage(process, image_spec, FileSpec(), error);
      }

      if (image_token != LLDB_INVALID_IMAGE_TOKEN) {
        result.AppendMessageWithFormat(
            "Loading \"%s\"...ok\nImage %u loaded.\n", image_path.str().c_str(),
            image_token);
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendErrorWithFormat("failed to load '%s': %s",
                                     image_path.str().c_str(),
                                     error.AsCString());
      }
    }
  }

  CommandOptions m_options;
};

// CommandObjectProcessUnload
#pragma mark CommandObjectProcessUnload

class CommandObjectProcessUnload : public CommandObjectParsed {
public:
  CommandObjectProcessUnload(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process unload",
            "Unload a shared library from the current process using the index "
            "returned by a previous call to \"process load\".",
            "process unload <index>",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypeUnsignedInteger);
  }

  ~CommandObjectProcessUnload() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {

    if (request.GetCursorIndex() || !m_exe_ctx.HasProcessScope())
      return;

    Process *process = m_exe_ctx.GetProcessPtr();

    const std::vector<lldb::addr_t> &tokens = process->GetImageTokens();
    const size_t token_num = tokens.size();
    for (size_t i = 0; i < token_num; ++i) {
      if (tokens[i] == LLDB_INVALID_IMAGE_TOKEN)
        continue;
      request.TryCompleteCurrentArg(std::to_string(i));
    }
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();

    for (auto &entry : command.entries()) {
      uint32_t image_token;
      if (entry.ref().getAsInteger(0, image_token)) {
        result.AppendErrorWithFormat("invalid image index argument '%s'",
                                     entry.ref().str().c_str());
        break;
      } else {
        Status error(process->GetTarget().GetPlatform()->UnloadImage(
            process, image_token));
        if (error.Success()) {
          result.AppendMessageWithFormat(
              "Unloading shared library with index %u...ok\n", image_token);
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat("failed to unload image: %s",
                                       error.AsCString());
          break;
        }
      }
    }
  }
};

// CommandObjectProcessSignal
#pragma mark CommandObjectProcessSignal

class CommandObjectProcessSignal : public CommandObjectParsed {
public:
  CommandObjectProcessSignal(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process signal",
            "Send a UNIX signal to the current target process.", nullptr,
            eCommandRequiresProcess | eCommandTryTargetAPILock) {
    AddSimpleArgumentList(eArgTypeUnixSignal);
  }

  ~CommandObjectProcessSignal() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (!m_exe_ctx.HasProcessScope() || request.GetCursorIndex() != 0)
      return;

    UnixSignalsSP signals = m_exe_ctx.GetProcessPtr()->GetUnixSignals();
    int signo = signals->GetFirstSignalNumber();
    while (signo != LLDB_INVALID_SIGNAL_NUMBER) {
      request.TryCompleteCurrentArg(signals->GetSignalAsStringRef(signo));
      signo = signals->GetNextSignalNumber(signo);
    }
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();

    if (command.GetArgumentCount() == 1) {
      int signo = LLDB_INVALID_SIGNAL_NUMBER;

      const char *signal_name = command.GetArgumentAtIndex(0);
      if (::isxdigit(signal_name[0])) {
        if (!llvm::to_integer(signal_name, signo))
          signo = LLDB_INVALID_SIGNAL_NUMBER;
      } else
        signo = process->GetUnixSignals()->GetSignalNumberFromName(signal_name);

      if (signo == LLDB_INVALID_SIGNAL_NUMBER) {
        result.AppendErrorWithFormat("Invalid signal argument '%s'.\n",
                                     command.GetArgumentAtIndex(0));
      } else {
        Status error(process->Signal(signo));
        if (error.Success()) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat("Failed to send signal %i: %s\n", signo,
                                       error.AsCString());
        }
      }
    } else {
      result.AppendErrorWithFormat(
          "'%s' takes exactly one signal number argument:\nUsage: %s\n",
          m_cmd_name.c_str(), m_cmd_syntax.c_str());
    }
  }
};

// CommandObjectProcessInterrupt
#pragma mark CommandObjectProcessInterrupt

class CommandObjectProcessInterrupt : public CommandObjectParsed {
public:
  CommandObjectProcessInterrupt(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process interrupt",
                            "Interrupt the current target process.",
                            "process interrupt",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectProcessInterrupt() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process to halt");
      return;
    }

    bool clear_thread_plans = true;
    Status error(process->Halt(clear_thread_plans));
    if (error.Success()) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendErrorWithFormat("Failed to halt process: %s\n",
                                   error.AsCString());
    }
  }
};

// CommandObjectProcessKill
#pragma mark CommandObjectProcessKill

class CommandObjectProcessKill : public CommandObjectParsed {
public:
  CommandObjectProcessKill(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process kill",
                            "Terminate the current target process.",
                            "process kill",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectProcessKill() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process to kill");
      return;
    }

    Status error(process->Destroy(true));
    if (error.Success()) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendErrorWithFormat("Failed to kill process: %s\n",
                                   error.AsCString());
    }
  }
};

#define LLDB_OPTIONS_process_save_core
#include "CommandOptions.inc"

class CommandObjectProcessSaveCore : public CommandObjectParsed {
public:
  CommandObjectProcessSaveCore(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process save-core",
            "Save the current process as a core file using an "
            "appropriate file type.",
            "process save-core [-s corefile-style -p plugin-name] FILE",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched) {
    AddSimpleArgumentList(eArgTypePath);
  }

  ~CommandObjectProcessSaveCore() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_process_save_core_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      const int short_option = m_getopt_table[option_idx].val;
      Status error;

      switch (short_option) {
      case 'p':
        error = m_core_dump_options.SetPluginName(option_arg.data());
        break;
      case 's':
        m_core_dump_options.SetStyle(
            (lldb::SaveCoreStyle)OptionArgParser::ToOptionEnum(
                option_arg, GetDefinitions()[option_idx].enum_values,
                eSaveCoreUnspecified, error));
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return {};
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_core_dump_options.Clear();
    }

    // Instance variables to hold the values for command options.
    SaveCoreOptions m_core_dump_options;
  };

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    ProcessSP process_sp = m_exe_ctx.GetProcessSP();
    if (process_sp) {
      if (command.GetArgumentCount() == 1) {
        FileSpec output_file(command.GetArgumentAtIndex(0));
        FileSystem::Instance().Resolve(output_file);
        auto &core_dump_options = m_options.m_core_dump_options;
        core_dump_options.SetOutputFile(output_file);
        Status error = PluginManager::SaveCore(process_sp, core_dump_options);
        if (error.Success()) {
          if (core_dump_options.GetStyle() ==
                  SaveCoreStyle::eSaveCoreDirtyOnly ||
              core_dump_options.GetStyle() ==
                  SaveCoreStyle::eSaveCoreStackOnly) {
            result.AppendMessageWithFormat(
                "\nModified-memory or stack-memory only corefile "
                "created.  This corefile may \n"
                "not show library/framework/app binaries "
                "on a different system, or when \n"
                "those binaries have "
                "been updated/modified. Copies are not included\n"
                "in this corefile.  Use --style full to include all "
                "process memory.\n");
          }
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat(
              "Failed to save core file for process: %s\n", error.AsCString());
        }
      } else {
        result.AppendErrorWithFormat("'%s' takes one arguments:\nUsage: %s\n",
                                     m_cmd_name.c_str(), m_cmd_syntax.c_str());
      }
    } else {
      result.AppendError("invalid process");
    }
  }

  CommandOptions m_options;
};

// CommandObjectProcessStatus
#pragma mark CommandObjectProcessStatus
#define LLDB_OPTIONS_process_status
#include "CommandOptions.inc"

class CommandObjectProcessStatus : public CommandObjectParsed {
public:
  CommandObjectProcessStatus(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process status",
            "Show status and stop location for the current target process.",
            "process status",
            eCommandRequiresProcess | eCommandTryTargetAPILock) {}

  ~CommandObjectProcessStatus() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
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
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_process_status_options);
    }

    // Instance variables to hold the values for command options.
    bool m_verbose = false;
  };

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Stream &strm = result.GetOutputStream();
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    // No need to check "process" for validity as eCommandRequiresProcess
    // ensures it is valid
    Process *process = m_exe_ctx.GetProcessPtr();
    const bool only_threads_with_stop_reason = true;
    const uint32_t start_frame = 0;
    const uint32_t num_frames = 1;
    const uint32_t num_frames_with_source = 1;
    const bool stop_format = true;
    process->GetStatus(strm);
    process->GetThreadStatus(strm, only_threads_with_stop_reason, start_frame,
                             num_frames, num_frames_with_source, stop_format);

    if (m_options.m_verbose) {
      addr_t code_mask = process->GetCodeAddressMask();
      addr_t data_mask = process->GetDataAddressMask();
      if (code_mask != LLDB_INVALID_ADDRESS_MASK) {
        int bits = std::bitset<64>(~code_mask).count();
        result.AppendMessageWithFormat(
            "Addressable code address mask: 0x%" PRIx64 "\n", code_mask);
        result.AppendMessageWithFormat(
            "Addressable data address mask: 0x%" PRIx64 "\n", data_mask);
        result.AppendMessageWithFormat(
            "Number of bits used in addressing (code): %d\n", bits);
      }

      PlatformSP platform_sp = process->GetTarget().GetPlatform();
      if (!platform_sp) {
        result.AppendError("Couldn'retrieve the target's platform");
        return;
      }

      auto expected_crash_info =
          platform_sp->FetchExtendedCrashInformation(*process);

      if (!expected_crash_info) {
        result.AppendError(llvm::toString(expected_crash_info.takeError()));
        return;
      }

      StructuredData::DictionarySP crash_info_sp = *expected_crash_info;

      if (crash_info_sp) {
        strm.EOL();
        strm.PutCString("Extended Crash Information:\n");
        crash_info_sp->GetDescription(strm);
      }
    }
  }

private:
  CommandOptions m_options;
};

// CommandObjectProcessHandle
#define LLDB_OPTIONS_process_handle
#include "CommandOptions.inc"

#pragma mark CommandObjectProcessHandle

class CommandObjectProcessHandle : public CommandObjectParsed {
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
      case 'c':
        do_clear = true;
        break;
      case 'd':
        dummy = true;
        break;
      case 's':
        stop = std::string(option_arg);
        break;
      case 'n':
        notify = std::string(option_arg);
        break;
      case 'p':
        pass = std::string(option_arg);
        break;
      case 't':
        only_target_values = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      stop.clear();
      notify.clear();
      pass.clear();
      only_target_values = false;
      do_clear = false;
      dummy = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_process_handle_options);
    }

    // Instance variables to hold the values for command options.

    std::string stop;
    std::string notify;
    std::string pass;
    bool only_target_values = false;
    bool do_clear = false;
    bool dummy = false;
  };

  CommandObjectProcessHandle(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process handle",
                            "Manage LLDB handling of OS signals for the "
                            "current target process.  Defaults to showing "
                            "current policy.",
                            nullptr) {
    SetHelpLong("\nIf no signals are specified but one or more actions are, "
                "and there is a live process, update them all.  If no action "
                "is specified, list the current values.\n"
                "If you specify actions with no target (e.g. in an init file) "
                "or in a target with no process "
                "the values will get copied into subsequent targets, but "
                "lldb won't be able to spell-check the options since it can't "
                "know which signal set will later be in force."
                "\nYou can see the signal modifications held by the target"
                "by passing the -t option."
                "\nYou can also clear the target modification for a signal"
                "by passing the -c option");
    AddSimpleArgumentList(eArgTypeUnixSignal, eArgRepeatStar);
  }

  ~CommandObjectProcessHandle() override = default;

  Options *GetOptions() override { return &m_options; }

  void PrintSignalHeader(Stream &str) {
    str.Printf("NAME         PASS   STOP   NOTIFY\n");
    str.Printf("===========  =====  =====  ======\n");
  }

  void PrintSignal(Stream &str, int32_t signo, llvm::StringRef sig_name,
                   const UnixSignalsSP &signals_sp) {
    bool stop;
    bool suppress;
    bool notify;

    str.Format("{0, -11}  ", sig_name);
    if (signals_sp->GetSignalInfo(signo, suppress, stop, notify)) {
      bool pass = !suppress;
      str.Printf("%s  %s  %s", (pass ? "true " : "false"),
                 (stop ? "true " : "false"), (notify ? "true " : "false"));
    }
    str.Printf("\n");
  }

  void PrintSignalInformation(Stream &str, Args &signal_args,
                              int num_valid_signals,
                              const UnixSignalsSP &signals_sp) {
    PrintSignalHeader(str);

    if (num_valid_signals > 0) {
      size_t num_args = signal_args.GetArgumentCount();
      for (size_t i = 0; i < num_args; ++i) {
        int32_t signo = signals_sp->GetSignalNumberFromName(
            signal_args.GetArgumentAtIndex(i));
        if (signo != LLDB_INVALID_SIGNAL_NUMBER)
          PrintSignal(str, signo, signal_args.GetArgumentAtIndex(i),
                      signals_sp);
      }
    } else // Print info for ALL signals
    {
      int32_t signo = signals_sp->GetFirstSignalNumber();
      while (signo != LLDB_INVALID_SIGNAL_NUMBER) {
        PrintSignal(str, signo, signals_sp->GetSignalAsStringRef(signo),
                    signals_sp);
        signo = signals_sp->GetNextSignalNumber(signo);
      }
    }
  }

protected:
  void DoExecute(Args &signal_args, CommandReturnObject &result) override {
    Target &target = GetSelectedOrDummyTarget();

    // Any signals that are being set should be added to the Target's
    // DummySignals so they will get applied on rerun, etc.
    // If we have a process, however, we can do a more accurate job of vetting
    // the user's options.
    ProcessSP process_sp = target.GetProcessSP();

    std::optional<bool> stop_action = {};
    std::optional<bool> pass_action = {};
    std::optional<bool> notify_action = {};

    if (!m_options.stop.empty()) {
      bool success = false;
      bool value = OptionArgParser::ToBoolean(m_options.stop, false, &success);
      if (!success) {
        result.AppendError(
            "Invalid argument for command option --stop; must be "
            "true or false.\n");
        return;
      }

      stop_action = value;
    }

    if (!m_options.pass.empty()) {
      bool success = false;
      bool value = OptionArgParser::ToBoolean(m_options.pass, false, &success);
      if (!success) {
        result.AppendError(
            "Invalid argument for command option --pass; must be "
            "true or false.\n");
        return;
      }
      pass_action = value;
    }

    if (!m_options.notify.empty()) {
      bool success = false;
      bool value =
          OptionArgParser::ToBoolean(m_options.notify, false, &success);
      if (!success) {
        result.AppendError("Invalid argument for command option --notify; must "
                           "be true or false.\n");
        return;
      }
      notify_action = value;
    }

    if (!m_options.notify.empty() && !notify_action.has_value()) {
    }

    bool no_actions = (!stop_action.has_value() && !pass_action.has_value() &&
                       !notify_action.has_value());
    if (m_options.only_target_values && !no_actions) {
      result.AppendError("-t is for reporting, not setting, target values.");
      return;
    }

    size_t num_args = signal_args.GetArgumentCount();
    UnixSignalsSP signals_sp;
    if (process_sp)
      signals_sp = process_sp->GetUnixSignals();

    int num_signals_set = 0;

    // If we were just asked to print the target values, do that here and
    // return:
    if (m_options.only_target_values) {
      target.PrintDummySignals(result.GetOutputStream(), signal_args);
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return;
    }

    // This handles clearing values:
    if (m_options.do_clear) {
      target.ClearDummySignals(signal_args);
      if (m_options.dummy)
        GetDummyTarget().ClearDummySignals(signal_args);
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return;
    }

    // This rest handles setting values:
    if (num_args > 0) {
      for (const auto &arg : signal_args) {
        // Do the process first.  If we have a process we can catch
        // invalid signal names, which we do here.
        if (signals_sp) {
          int32_t signo = signals_sp->GetSignalNumberFromName(arg.c_str());
          if (signo != LLDB_INVALID_SIGNAL_NUMBER) {
            if (stop_action.has_value())
              signals_sp->SetShouldStop(signo, *stop_action);
            if (pass_action.has_value()) {
              bool suppress = !*pass_action;
              signals_sp->SetShouldSuppress(signo, suppress);
            }
            if (notify_action.has_value())
              signals_sp->SetShouldNotify(signo, *notify_action);
            ++num_signals_set;
          } else {
            result.AppendErrorWithFormat("Invalid signal name '%s'\n",
                                          arg.c_str());
            continue;
          }
        } else {
          // If there's no process we can't check, so we just set them all.
          // But since the map signal name -> signal number across all platforms
          // is not 1-1, we can't sensibly set signal actions by number before
          // we have a process.  Check that here:
          int32_t signo;
          if (llvm::to_integer(arg.c_str(), signo)) {
            result.AppendErrorWithFormat("Can't set signal handling by signal "
                                         "number with no process");
            return;
          }
         num_signals_set = num_args;
        }
        auto set_lazy_bool = [](std::optional<bool> action) -> LazyBool {
          if (!action.has_value())
            return eLazyBoolCalculate;
          return (*action) ? eLazyBoolYes : eLazyBoolNo;
        };

        // If there were no actions, we're just listing, don't add the dummy:
        if (!no_actions)
          target.AddDummySignal(arg.ref(), set_lazy_bool(pass_action),
                                set_lazy_bool(notify_action),
                                set_lazy_bool(stop_action));
      }
    } else {
      // No signal specified, if any command options were specified, update ALL
      // signals.  But we can't do this without a process since we don't know
      // all the possible signals that might be valid for this target.
      if ((notify_action.has_value() || stop_action.has_value() ||
           pass_action.has_value()) &&
          process_sp) {
        if (m_interpreter.Confirm(
                "Do you really want to update all the signals?", false)) {
          int32_t signo = signals_sp->GetFirstSignalNumber();
          while (signo != LLDB_INVALID_SIGNAL_NUMBER) {
            if (notify_action.has_value())
              signals_sp->SetShouldNotify(signo, *notify_action);
            if (stop_action.has_value())
              signals_sp->SetShouldStop(signo, *stop_action);
            if (pass_action.has_value()) {
              bool suppress = !*pass_action;
              signals_sp->SetShouldSuppress(signo, suppress);
            }
            signo = signals_sp->GetNextSignalNumber(signo);
          }
        }
      }
    }

    if (signals_sp)
      PrintSignalInformation(result.GetOutputStream(), signal_args,
                             num_signals_set, signals_sp);
    else
      target.PrintDummySignals(result.GetOutputStream(),
          signal_args);

    if (num_signals_set > 0)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else
      result.SetStatus(eReturnStatusFailed);
  }

  CommandOptions m_options;
};

// Next are the subcommands of CommandObjectMultiwordProcessTrace

// CommandObjectProcessTraceStart
class CommandObjectProcessTraceStart : public CommandObjectTraceProxy {
public:
  CommandObjectProcessTraceStart(CommandInterpreter &interpreter)
      : CommandObjectTraceProxy(
            /*live_debug_session_only*/ true, interpreter,
            "process trace start",
            "Start tracing this process with the corresponding trace "
            "plug-in.",
            "process trace start [<trace-options>]") {}

protected:
  lldb::CommandObjectSP GetDelegateCommand(Trace &trace) override {
    return trace.GetProcessTraceStartCommand(m_interpreter);
  }
};

// CommandObjectProcessTraceStop
class CommandObjectProcessTraceStop : public CommandObjectParsed {
public:
  CommandObjectProcessTraceStop(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process trace stop",
                            "Stop tracing this process. This does not affect "
                            "traces started with the "
                            "\"thread trace start\" command.",
                            "process trace stop",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused |
                                eCommandProcessMustBeTraced) {}

  ~CommandObjectProcessTraceStop() override = default;

  void DoExecute(Args &command, CommandReturnObject &result) override {
    ProcessSP process_sp = m_exe_ctx.GetProcessSP();

    TraceSP trace_sp = process_sp->GetTarget().GetTrace();

    if (llvm::Error err = trace_sp->Stop())
      result.AppendError(toString(std::move(err)));
    else
      result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// CommandObjectMultiwordProcessTrace
class CommandObjectMultiwordProcessTrace : public CommandObjectMultiword {
public:
  CommandObjectMultiwordProcessTrace(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "trace", "Commands for tracing the current process.",
            "process trace <subcommand> [<subcommand objects>]") {
    LoadSubCommand("start", CommandObjectSP(new CommandObjectProcessTraceStart(
                                interpreter)));
    LoadSubCommand("stop", CommandObjectSP(
                               new CommandObjectProcessTraceStop(interpreter)));
  }

  ~CommandObjectMultiwordProcessTrace() override = default;
};

// CommandObjectMultiwordProcess

CommandObjectMultiwordProcess::CommandObjectMultiwordProcess(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(
          interpreter, "process",
          "Commands for interacting with processes on the current platform.",
          "process <subcommand> [<subcommand-options>]") {
  LoadSubCommand("attach",
                 CommandObjectSP(new CommandObjectProcessAttach(interpreter)));
  LoadSubCommand("launch",
                 CommandObjectSP(new CommandObjectProcessLaunch(interpreter)));
  LoadSubCommand("continue", CommandObjectSP(new CommandObjectProcessContinue(
                                 interpreter)));
  LoadSubCommand("connect",
                 CommandObjectSP(new CommandObjectProcessConnect(interpreter)));
  LoadSubCommand("detach",
                 CommandObjectSP(new CommandObjectProcessDetach(interpreter)));
  LoadSubCommand("load",
                 CommandObjectSP(new CommandObjectProcessLoad(interpreter)));
  LoadSubCommand("unload",
                 CommandObjectSP(new CommandObjectProcessUnload(interpreter)));
  LoadSubCommand("signal",
                 CommandObjectSP(new CommandObjectProcessSignal(interpreter)));
  LoadSubCommand("handle",
                 CommandObjectSP(new CommandObjectProcessHandle(interpreter)));
  LoadSubCommand("status",
                 CommandObjectSP(new CommandObjectProcessStatus(interpreter)));
  LoadSubCommand("interrupt", CommandObjectSP(new CommandObjectProcessInterrupt(
                                  interpreter)));
  LoadSubCommand("kill",
                 CommandObjectSP(new CommandObjectProcessKill(interpreter)));
  LoadSubCommand("plugin",
                 CommandObjectSP(new CommandObjectProcessPlugin(interpreter)));
  LoadSubCommand("save-core", CommandObjectSP(new CommandObjectProcessSaveCore(
                                  interpreter)));
  LoadSubCommand(
      "trace",
      CommandObjectSP(new CommandObjectMultiwordProcessTrace(interpreter)));
}

CommandObjectMultiwordProcess::~CommandObjectMultiwordProcess() = default;
