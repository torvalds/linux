//===-- CommandObjectTarget.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectTarget.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/DataFormatters/ValueObjectPrinter.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupArchitecture.h"
#include "lldb/Interpreter/OptionGroupBoolean.h"
#include "lldb/Interpreter/OptionGroupFile.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionGroupPlatform.h"
#include "lldb/Interpreter/OptionGroupPythonClassWithDict.h"
#include "lldb/Interpreter/OptionGroupString.h"
#include "lldb/Interpreter/OptionGroupUInt64.h"
#include "lldb/Interpreter/OptionGroupUUID.h"
#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"
#include "lldb/Interpreter/OptionGroupVariable.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/FuncUnwinders.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/Timer.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-enumerations.h"

#include "clang/CodeGen/ObjectFilePCHContainerOperations.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatAdapters.h"


using namespace lldb;
using namespace lldb_private;

static void DumpTargetInfo(uint32_t target_idx, Target *target,
                           const char *prefix_cstr,
                           bool show_stopped_process_status, Stream &strm) {
  const ArchSpec &target_arch = target->GetArchitecture();

  Module *exe_module = target->GetExecutableModulePointer();
  char exe_path[PATH_MAX];
  bool exe_valid = false;
  if (exe_module)
    exe_valid = exe_module->GetFileSpec().GetPath(exe_path, sizeof(exe_path));

  if (!exe_valid)
    ::strcpy(exe_path, "<none>");

  std::string formatted_label = "";
  const std::string &label = target->GetLabel();
  if (!label.empty()) {
    formatted_label = " (" + label + ")";
  }

  strm.Printf("%starget #%u%s: %s", prefix_cstr ? prefix_cstr : "", target_idx,
              formatted_label.data(), exe_path);

  uint32_t properties = 0;
  if (target_arch.IsValid()) {
    strm.Printf("%sarch=", properties++ > 0 ? ", " : " ( ");
    target_arch.DumpTriple(strm.AsRawOstream());
    properties++;
  }
  PlatformSP platform_sp(target->GetPlatform());
  if (platform_sp)
    strm.Format("{0}platform={1}", properties++ > 0 ? ", " : " ( ",
                platform_sp->GetName());

  ProcessSP process_sp(target->GetProcessSP());
  bool show_process_status = false;
  if (process_sp) {
    lldb::pid_t pid = process_sp->GetID();
    StateType state = process_sp->GetState();
    if (show_stopped_process_status)
      show_process_status = StateIsStoppedState(state, true);
    const char *state_cstr = StateAsCString(state);
    if (pid != LLDB_INVALID_PROCESS_ID)
      strm.Printf("%spid=%" PRIu64, properties++ > 0 ? ", " : " ( ", pid);
    strm.Printf("%sstate=%s", properties++ > 0 ? ", " : " ( ", state_cstr);
  }
  if (properties > 0)
    strm.PutCString(" )\n");
  else
    strm.EOL();
  if (show_process_status) {
    const bool only_threads_with_stop_reason = true;
    const uint32_t start_frame = 0;
    const uint32_t num_frames = 1;
    const uint32_t num_frames_with_source = 1;
    const bool stop_format = false;
    process_sp->GetStatus(strm);
    process_sp->GetThreadStatus(strm, only_threads_with_stop_reason,
                                start_frame, num_frames, num_frames_with_source,
                                stop_format);
  }
}

static uint32_t DumpTargetList(TargetList &target_list,
                               bool show_stopped_process_status, Stream &strm) {
  const uint32_t num_targets = target_list.GetNumTargets();
  if (num_targets) {
    TargetSP selected_target_sp(target_list.GetSelectedTarget());
    strm.PutCString("Current targets:\n");
    for (uint32_t i = 0; i < num_targets; ++i) {
      TargetSP target_sp(target_list.GetTargetAtIndex(i));
      if (target_sp) {
        bool is_selected = target_sp.get() == selected_target_sp.get();
        DumpTargetInfo(i, target_sp.get(), is_selected ? "* " : "  ",
                       show_stopped_process_status, strm);
      }
    }
  }
  return num_targets;
}

#define LLDB_OPTIONS_target_dependents
#include "CommandOptions.inc"

class OptionGroupDependents : public OptionGroup {
public:
  OptionGroupDependents() = default;

  ~OptionGroupDependents() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    return llvm::ArrayRef(g_target_dependents_options);
  }

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override {
    Status error;

    // For compatibility no value means don't load dependents.
    if (option_value.empty()) {
      m_load_dependent_files = eLoadDependentsNo;
      return error;
    }

    const char short_option =
        g_target_dependents_options[option_idx].short_option;
    if (short_option == 'd') {
      LoadDependentFiles tmp_load_dependents;
      tmp_load_dependents = (LoadDependentFiles)OptionArgParser::ToOptionEnum(
          option_value, g_target_dependents_options[option_idx].enum_values, 0,
          error);
      if (error.Success())
        m_load_dependent_files = tmp_load_dependents;
    } else {
      error.SetErrorStringWithFormat("unrecognized short option '%c'",
                                     short_option);
    }

    return error;
  }

  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override {
    m_load_dependent_files = eLoadDependentsDefault;
  }

  LoadDependentFiles m_load_dependent_files;

private:
  OptionGroupDependents(const OptionGroupDependents &) = delete;
  const OptionGroupDependents &
  operator=(const OptionGroupDependents &) = delete;
};

#pragma mark CommandObjectTargetCreate

class CommandObjectTargetCreate : public CommandObjectParsed {
public:
  CommandObjectTargetCreate(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target create",
            "Create a target using the argument as the main executable.",
            nullptr),
        m_platform_options(true), // Include the --platform option.
        m_core_file(LLDB_OPT_SET_1, false, "core", 'c', 0, eArgTypeFilename,
                    "Fullpath to a core file to use for this target."),
        m_label(LLDB_OPT_SET_1, false, "label", 'l', 0, eArgTypeName,
                "Optional name for this target.", nullptr),
        m_symbol_file(LLDB_OPT_SET_1, false, "symfile", 's', 0,
                      eArgTypeFilename,
                      "Fullpath to a stand alone debug "
                      "symbols file for when debug symbols "
                      "are not in the executable."),
        m_remote_file(
            LLDB_OPT_SET_1, false, "remote-file", 'r', 0, eArgTypeFilename,
            "Fullpath to the file on the remote host if debugging remotely.") {

    AddSimpleArgumentList(eArgTypeFilename);

    m_option_group.Append(&m_arch_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_platform_options, LLDB_OPT_SET_ALL, 1);
    m_option_group.Append(&m_core_file, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_label, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_symbol_file, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_remote_file, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_add_dependents, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectTargetCreate() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    FileSpec core_file(m_core_file.GetOptionValue().GetCurrentValue());
    FileSpec remote_file(m_remote_file.GetOptionValue().GetCurrentValue());

    if (core_file) {
      auto file = FileSystem::Instance().Open(
          core_file, lldb_private::File::eOpenOptionReadOnly);

      if (!file) {
        result.AppendErrorWithFormatv("Cannot open '{0}': {1}.",
                                      core_file.GetPath(),
                                      llvm::toString(file.takeError()));
        return;
      }
    }

    if (argc == 1 || core_file || remote_file) {
      FileSpec symfile(m_symbol_file.GetOptionValue().GetCurrentValue());
      if (symfile) {
        auto file = FileSystem::Instance().Open(
            symfile, lldb_private::File::eOpenOptionReadOnly);

        if (!file) {
          result.AppendErrorWithFormatv("Cannot open '{0}': {1}.",
                                        symfile.GetPath(),
                                        llvm::toString(file.takeError()));
          return;
        }
      }

      const char *file_path = command.GetArgumentAtIndex(0);
      LLDB_SCOPED_TIMERF("(lldb) target create '%s'", file_path);

      bool must_set_platform_path = false;

      Debugger &debugger = GetDebugger();

      TargetSP target_sp;
      llvm::StringRef arch_cstr = m_arch_option.GetArchitectureName();
      Status error(debugger.GetTargetList().CreateTarget(
          debugger, file_path, arch_cstr,
          m_add_dependents.m_load_dependent_files, &m_platform_options,
          target_sp));

      if (!target_sp) {
        result.AppendError(error.AsCString());
        return;
      }

      const llvm::StringRef label =
          m_label.GetOptionValue().GetCurrentValueAsRef();
      if (!label.empty()) {
        if (auto E = target_sp->SetLabel(label))
          result.SetError(std::move(E));
        return;
      }

      auto on_error = llvm::make_scope_exit(
          [&target_list = debugger.GetTargetList(), &target_sp]() {
            target_list.DeleteTarget(target_sp);
          });

      // Only get the platform after we create the target because we might
      // have switched platforms depending on what the arguments were to
      // CreateTarget() we can't rely on the selected platform.

      PlatformSP platform_sp = target_sp->GetPlatform();

      FileSpec file_spec;
      if (file_path) {
        file_spec.SetFile(file_path, FileSpec::Style::native);
        FileSystem::Instance().Resolve(file_spec);

        // Try to resolve the exe based on PATH and/or platform-specific
        // suffixes, but only if using the host platform.
        if (platform_sp && platform_sp->IsHost() &&
            !FileSystem::Instance().Exists(file_spec))
          FileSystem::Instance().ResolveExecutableLocation(file_spec);
      }

      if (remote_file) {
        if (platform_sp) {
          // I have a remote file.. two possible cases
          if (file_spec && FileSystem::Instance().Exists(file_spec)) {
            // if the remote file does not exist, push it there
            if (!platform_sp->GetFileExists(remote_file)) {
              Status err = platform_sp->PutFile(file_spec, remote_file);
              if (err.Fail()) {
                result.AppendError(err.AsCString());
                return;
              }
            }
          } else {
            // there is no local file and we need one
            // in order to make the remote ---> local transfer we need a
            // platform
            // TODO: if the user has passed in a --platform argument, use it
            // to fetch the right platform
            if (file_path) {
              // copy the remote file to the local file
              Status err = platform_sp->GetFile(remote_file, file_spec);
              if (err.Fail()) {
                result.AppendError(err.AsCString());
                return;
              }
            } else {
              // If the remote file exists, we can debug reading that out of
              // memory.  If the platform is already connected to an lldb-server
              // then we can at least check the file exists remotely.  Otherwise
              // we'll just have to trust that it will be there when we do
              // process connect.
              // I don't do this for the host platform because it seems odd to
              // support supplying a remote file but no local file for a local
              // debug session.
              if (platform_sp->IsHost()) {
                result.AppendError("Supply a local file, not a remote file, "
                                   "when debugging on the host.");
                return;
              }
              if (platform_sp->IsConnected() && !platform_sp->GetFileExists(remote_file)) {
                result.AppendError("remote --> local transfer without local "
                                 "path is not implemented yet");
                return;
              }
              // Since there's only a remote file, we need to set the executable
              // file spec to the remote one.
              ProcessLaunchInfo launch_info = target_sp->GetProcessLaunchInfo();
              launch_info.SetExecutableFile(FileSpec(remote_file), true);
              target_sp->SetProcessLaunchInfo(launch_info);
            }
          }
        } else {
          result.AppendError("no platform found for target");
          return;
        }
      }

      if (symfile || remote_file) {
        ModuleSP module_sp(target_sp->GetExecutableModule());
        if (module_sp) {
          if (symfile)
            module_sp->SetSymbolFileFileSpec(symfile);
          if (remote_file) {
            std::string remote_path = remote_file.GetPath();
            target_sp->SetArg0(remote_path.c_str());
            module_sp->SetPlatformFileSpec(remote_file);
          }
        }
      }

      if (must_set_platform_path) {
        ModuleSpec main_module_spec(file_spec);
        ModuleSP module_sp =
            target_sp->GetOrCreateModule(main_module_spec, true /* notify */);
        if (module_sp)
          module_sp->SetPlatformFileSpec(remote_file);
      }

      if (core_file) {
        FileSpec core_file_dir;
        core_file_dir.SetDirectory(core_file.GetDirectory());
        target_sp->AppendExecutableSearchPaths(core_file_dir);

        ProcessSP process_sp(target_sp->CreateProcess(
            GetDebugger().GetListener(), llvm::StringRef(), &core_file, false));

        if (process_sp) {
          // Seems weird that we Launch a core file, but that is what we
          // do!
          error = process_sp->LoadCore();

          if (error.Fail()) {
            result.AppendError(error.AsCString("unknown core file format"));
            return;
          } else {
            result.AppendMessageWithFormatv(
                "Core file '{0}' ({1}) was loaded.\n", core_file.GetPath(),
                target_sp->GetArchitecture().GetArchitectureName());
            result.SetStatus(eReturnStatusSuccessFinishNoResult);
            on_error.release();
          }
        } else {
          result.AppendErrorWithFormatv("Unknown core file format '{0}'\n",
                                        core_file.GetPath());
        }
      } else {
        result.AppendMessageWithFormat(
            "Current executable set to '%s' (%s).\n",
            file_spec.GetPath().c_str(),
            target_sp->GetArchitecture().GetArchitectureName());
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
        on_error.release();
      }
    } else {
      result.AppendErrorWithFormat("'%s' takes exactly one executable path "
                                   "argument, or use the --core option.\n",
                                   m_cmd_name.c_str());
    }
  }

private:
  OptionGroupOptions m_option_group;
  OptionGroupArchitecture m_arch_option;
  OptionGroupPlatform m_platform_options;
  OptionGroupFile m_core_file;
  OptionGroupString m_label;
  OptionGroupFile m_symbol_file;
  OptionGroupFile m_remote_file;
  OptionGroupDependents m_add_dependents;
};

#pragma mark CommandObjectTargetList

class CommandObjectTargetList : public CommandObjectParsed {
public:
  CommandObjectTargetList(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target list",
            "List all current targets in the current debug session.", nullptr) {
  }

  ~CommandObjectTargetList() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Stream &strm = result.GetOutputStream();

    bool show_stopped_process_status = false;
    if (DumpTargetList(GetDebugger().GetTargetList(),
                       show_stopped_process_status, strm) == 0) {
      strm.PutCString("No targets.\n");
    }
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectTargetSelect

class CommandObjectTargetSelect : public CommandObjectParsed {
public:
  CommandObjectTargetSelect(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target select",
            "Select a target as the current target by target index.", nullptr) {
    AddSimpleArgumentList(eArgTypeTargetID);
  }

  ~CommandObjectTargetSelect() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    if (args.GetArgumentCount() == 1) {
      const char *target_identifier = args.GetArgumentAtIndex(0);
      uint32_t target_idx = LLDB_INVALID_INDEX32;
      TargetList &target_list = GetDebugger().GetTargetList();
      const uint32_t num_targets = target_list.GetNumTargets();
      if (llvm::to_integer(target_identifier, target_idx)) {
        if (target_idx < num_targets) {
          target_list.SetSelectedTarget(target_idx);
          Stream &strm = result.GetOutputStream();
          bool show_stopped_process_status = false;
          DumpTargetList(target_list, show_stopped_process_status, strm);
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          if (num_targets > 0) {
            result.AppendErrorWithFormat(
                "index %u is out of range, valid target indexes are 0 - %u\n",
                target_idx, num_targets - 1);
          } else {
            result.AppendErrorWithFormat(
                "index %u is out of range since there are no active targets\n",
                target_idx);
          }
        }
      } else {
        for (size_t i = 0; i < num_targets; i++) {
          if (TargetSP target_sp = target_list.GetTargetAtIndex(i)) {
            const std::string &label = target_sp->GetLabel();
            if (!label.empty() && label == target_identifier) {
              target_idx = i;
              break;
            }
          }
        }

        if (target_idx != LLDB_INVALID_INDEX32) {
          target_list.SetSelectedTarget(target_idx);
          Stream &strm = result.GetOutputStream();
          bool show_stopped_process_status = false;
          DumpTargetList(target_list, show_stopped_process_status, strm);
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat("invalid index string value '%s'\n",
                                       target_identifier);
        }
      }
    } else {
      result.AppendError(
          "'target select' takes a single argument: a target index\n");
    }
  }
};

#pragma mark CommandObjectTargetDelete

class CommandObjectTargetDelete : public CommandObjectParsed {
public:
  CommandObjectTargetDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target delete",
                            "Delete one or more targets by target index.",
                            nullptr),
        m_all_option(LLDB_OPT_SET_1, false, "all", 'a', "Delete all targets.",
                     false, true),
        m_cleanup_option(
            LLDB_OPT_SET_1, false, "clean", 'c',
            "Perform extra cleanup to minimize memory consumption after "
            "deleting the target.  "
            "By default, LLDB will keep in memory any modules previously "
            "loaded by the target as well "
            "as all of its debug info.  Specifying --clean will unload all of "
            "these shared modules and "
            "cause them to be reparsed again the next time the target is run",
            false, true) {
    m_option_group.Append(&m_all_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_cleanup_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
    AddSimpleArgumentList(eArgTypeTargetID, eArgRepeatStar);
  }

  ~CommandObjectTargetDelete() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    const size_t argc = args.GetArgumentCount();
    std::vector<TargetSP> delete_target_list;
    TargetList &target_list = GetDebugger().GetTargetList();
    TargetSP target_sp;

    if (m_all_option.GetOptionValue()) {
      for (size_t i = 0; i < target_list.GetNumTargets(); ++i)
        delete_target_list.push_back(target_list.GetTargetAtIndex(i));
    } else if (argc > 0) {
      const uint32_t num_targets = target_list.GetNumTargets();
      // Bail out if don't have any targets.
      if (num_targets == 0) {
        result.AppendError("no targets to delete");
        return;
      }

      for (auto &entry : args.entries()) {
        uint32_t target_idx;
        if (entry.ref().getAsInteger(0, target_idx)) {
          result.AppendErrorWithFormat("invalid target index '%s'\n",
                                       entry.c_str());
          return;
        }
        if (target_idx < num_targets) {
          target_sp = target_list.GetTargetAtIndex(target_idx);
          if (target_sp) {
            delete_target_list.push_back(target_sp);
            continue;
          }
        }
        if (num_targets > 1)
          result.AppendErrorWithFormat("target index %u is out of range, valid "
                                       "target indexes are 0 - %u\n",
                                       target_idx, num_targets - 1);
        else
          result.AppendErrorWithFormat(
              "target index %u is out of range, the only valid index is 0\n",
              target_idx);

        return;
      }
    } else {
      target_sp = target_list.GetSelectedTarget();
      if (!target_sp) {
        result.AppendErrorWithFormat("no target is currently selected\n");
        return;
      }
      delete_target_list.push_back(target_sp);
    }

    const size_t num_targets_to_delete = delete_target_list.size();
    for (size_t idx = 0; idx < num_targets_to_delete; ++idx) {
      target_sp = delete_target_list[idx];
      target_list.DeleteTarget(target_sp);
      target_sp->Destroy();
    }
    // If "--clean" was specified, prune any orphaned shared modules from the
    // global shared module list
    if (m_cleanup_option.GetOptionValue()) {
      const bool mandatory = true;
      ModuleList::RemoveOrphanSharedModules(mandatory);
    }
    result.GetOutputStream().Printf("%u targets deleted.\n",
                                    (uint32_t)num_targets_to_delete);
    result.SetStatus(eReturnStatusSuccessFinishResult);

    return;
  }

  OptionGroupOptions m_option_group;
  OptionGroupBoolean m_all_option;
  OptionGroupBoolean m_cleanup_option;
};

class CommandObjectTargetShowLaunchEnvironment : public CommandObjectParsed {
public:
  CommandObjectTargetShowLaunchEnvironment(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target show-launch-environment",
            "Shows the environment being passed to the process when launched, "
            "taking info account 3 settings: target.env-vars, "
            "target.inherit-env and target.unset-env-vars.",
            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectTargetShowLaunchEnvironment() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    Environment env = target->GetEnvironment();

    std::vector<Environment::value_type *> env_vector;
    env_vector.reserve(env.size());
    for (auto &KV : env)
      env_vector.push_back(&KV);
    std::sort(env_vector.begin(), env_vector.end(),
              [](Environment::value_type *a, Environment::value_type *b) {
                return a->first() < b->first();
              });

    auto &strm = result.GetOutputStream();
    for (auto &KV : env_vector)
      strm.Format("{0}={1}\n", KV->first(), KV->second);

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectTargetVariable

class CommandObjectTargetVariable : public CommandObjectParsed {
  static const uint32_t SHORT_OPTION_FILE = 0x66696c65; // 'file'
  static const uint32_t SHORT_OPTION_SHLB = 0x73686c62; // 'shlb'

public:
  CommandObjectTargetVariable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target variable",
                            "Read global variables for the current target, "
                            "before or while running a process.",
                            nullptr, eCommandRequiresTarget),
        m_option_variable(false), // Don't include frame options
        m_option_format(eFormatDefault),
        m_option_compile_units(LLDB_OPT_SET_1, false, "file", SHORT_OPTION_FILE,
                               0, eArgTypeFilename,
                               "A basename or fullpath to a file that contains "
                               "global variables. This option can be "
                               "specified multiple times."),
        m_option_shared_libraries(
            LLDB_OPT_SET_1, false, "shlib", SHORT_OPTION_SHLB, 0,
            eArgTypeFilename,
            "A basename or fullpath to a shared library to use in the search "
            "for global "
            "variables. This option can be specified multiple times.") {
    AddSimpleArgumentList(eArgTypeVarName, eArgRepeatPlus);

    m_option_group.Append(&m_varobj_options, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_variable, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_format,
                          OptionGroupFormat::OPTION_GROUP_FORMAT |
                              OptionGroupFormat::OPTION_GROUP_GDB_FMT,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_compile_units, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_shared_libraries, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectTargetVariable() override = default;

  void DumpValueObject(Stream &s, VariableSP &var_sp, ValueObjectSP &valobj_sp,
                       const char *root_name) {
    DumpValueObjectOptions options(m_varobj_options.GetAsDumpOptions());

    if (!valobj_sp->GetTargetSP()->GetDisplayRuntimeSupportValues() &&
        valobj_sp->IsRuntimeSupportValue())
      return;

    switch (var_sp->GetScope()) {
    case eValueTypeVariableGlobal:
      if (m_option_variable.show_scope)
        s.PutCString("GLOBAL: ");
      break;

    case eValueTypeVariableStatic:
      if (m_option_variable.show_scope)
        s.PutCString("STATIC: ");
      break;

    case eValueTypeVariableArgument:
      if (m_option_variable.show_scope)
        s.PutCString("   ARG: ");
      break;

    case eValueTypeVariableLocal:
      if (m_option_variable.show_scope)
        s.PutCString(" LOCAL: ");
      break;

    case eValueTypeVariableThreadLocal:
      if (m_option_variable.show_scope)
        s.PutCString("THREAD: ");
      break;

    default:
      break;
    }

    if (m_option_variable.show_decl) {
      bool show_fullpaths = false;
      bool show_module = true;
      if (var_sp->DumpDeclaration(&s, show_fullpaths, show_module))
        s.PutCString(": ");
    }

    const Format format = m_option_format.GetFormat();
    if (format != eFormatDefault)
      options.SetFormat(format);

    options.SetRootValueObjectName(root_name);

    if (llvm::Error error = valobj_sp->Dump(s, options))
      s << "error: " << toString(std::move(error));
  }

  static size_t GetVariableCallback(void *baton, const char *name,
                                    VariableList &variable_list) {
    size_t old_size = variable_list.GetSize();
    Target *target = static_cast<Target *>(baton);
    if (target)
      target->GetImages().FindGlobalVariables(ConstString(name), UINT32_MAX,
                                              variable_list);
    return variable_list.GetSize() - old_size;
  }

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DumpGlobalVariableList(const ExecutionContext &exe_ctx,
                              const SymbolContext &sc,
                              const VariableList &variable_list, Stream &s) {
    if (variable_list.Empty())
      return;
    if (sc.module_sp) {
      if (sc.comp_unit) {
        s.Format("Global variables for {0} in {1}:\n",
                 sc.comp_unit->GetPrimaryFile(), sc.module_sp->GetFileSpec());
      } else {
        s.Printf("Global variables for %s\n",
                 sc.module_sp->GetFileSpec().GetPath().c_str());
      }
    } else if (sc.comp_unit) {
      s.Format("Global variables for {0}\n", sc.comp_unit->GetPrimaryFile());
    }

    for (VariableSP var_sp : variable_list) {
      if (!var_sp)
        continue;
      ValueObjectSP valobj_sp(ValueObjectVariable::Create(
          exe_ctx.GetBestExecutionContextScope(), var_sp));

      if (valobj_sp)
        DumpValueObject(s, var_sp, valobj_sp, var_sp->GetName().GetCString());
    }
  }

  void DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    const size_t argc = args.GetArgumentCount();
    Stream &s = result.GetOutputStream();

    if (argc > 0) {
      for (const Args::ArgEntry &arg : args) {
        VariableList variable_list;
        ValueObjectList valobj_list;

        size_t matches = 0;
        bool use_var_name = false;
        if (m_option_variable.use_regex) {
          RegularExpression regex(arg.ref());
          if (!regex.IsValid()) {
            result.GetErrorStream().Printf(
                "error: invalid regular expression: '%s'\n", arg.c_str());
            return;
          }
          use_var_name = true;
          target->GetImages().FindGlobalVariables(regex, UINT32_MAX,
                                                  variable_list);
          matches = variable_list.GetSize();
        } else {
          Status error(Variable::GetValuesForVariableExpressionPath(
              arg.c_str(), m_exe_ctx.GetBestExecutionContextScope(),
              GetVariableCallback, target, variable_list, valobj_list));
          matches = variable_list.GetSize();
        }

        if (matches == 0) {
          result.AppendErrorWithFormat("can't find global variable '%s'",
                                       arg.c_str());
          return;
        } else {
          for (uint32_t global_idx = 0; global_idx < matches; ++global_idx) {
            VariableSP var_sp(variable_list.GetVariableAtIndex(global_idx));
            if (var_sp) {
              ValueObjectSP valobj_sp(
                  valobj_list.GetValueObjectAtIndex(global_idx));
              if (!valobj_sp)
                valobj_sp = ValueObjectVariable::Create(
                    m_exe_ctx.GetBestExecutionContextScope(), var_sp);

              if (valobj_sp)
                DumpValueObject(s, var_sp, valobj_sp,
                                use_var_name ? var_sp->GetName().GetCString()
                                             : arg.c_str());
            }
          }
        }
      }
    } else {
      const FileSpecList &compile_units =
          m_option_compile_units.GetOptionValue().GetCurrentValue();
      const FileSpecList &shlibs =
          m_option_shared_libraries.GetOptionValue().GetCurrentValue();
      SymbolContextList sc_list;
      const size_t num_compile_units = compile_units.GetSize();
      const size_t num_shlibs = shlibs.GetSize();
      if (num_compile_units == 0 && num_shlibs == 0) {
        bool success = false;
        StackFrame *frame = m_exe_ctx.GetFramePtr();
        CompileUnit *comp_unit = nullptr;
        if (frame) {
          SymbolContext sc = frame->GetSymbolContext(eSymbolContextCompUnit);
          comp_unit = sc.comp_unit;
          if (sc.comp_unit) {
            const bool can_create = true;
            VariableListSP comp_unit_varlist_sp(
                sc.comp_unit->GetVariableList(can_create));
            if (comp_unit_varlist_sp) {
              size_t count = comp_unit_varlist_sp->GetSize();
              if (count > 0) {
                DumpGlobalVariableList(m_exe_ctx, sc, *comp_unit_varlist_sp, s);
                success = true;
              }
            }
          }
        }
        if (!success) {
          if (frame) {
            if (comp_unit)
              result.AppendErrorWithFormatv(
                  "no global variables in current compile unit: {0}\n",
                  comp_unit->GetPrimaryFile());
            else
              result.AppendErrorWithFormat(
                  "no debug information for frame %u\n",
                  frame->GetFrameIndex());
          } else
            result.AppendError("'target variable' takes one or more global "
                               "variable names as arguments\n");
        }
      } else {
        SymbolContextList sc_list;
        // We have one or more compile unit or shlib
        if (num_shlibs > 0) {
          for (size_t shlib_idx = 0; shlib_idx < num_shlibs; ++shlib_idx) {
            const FileSpec module_file(shlibs.GetFileSpecAtIndex(shlib_idx));
            ModuleSpec module_spec(module_file);

            ModuleSP module_sp(
                target->GetImages().FindFirstModule(module_spec));
            if (module_sp) {
              if (num_compile_units > 0) {
                for (size_t cu_idx = 0; cu_idx < num_compile_units; ++cu_idx)
                  module_sp->FindCompileUnits(
                      compile_units.GetFileSpecAtIndex(cu_idx), sc_list);
              } else {
                SymbolContext sc;
                sc.module_sp = module_sp;
                sc_list.Append(sc);
              }
            } else {
              // Didn't find matching shlib/module in target...
              result.AppendErrorWithFormat(
                  "target doesn't contain the specified shared library: %s\n",
                  module_file.GetPath().c_str());
            }
          }
        } else {
          // No shared libraries, we just want to find globals for the compile
          // units files that were specified
          for (size_t cu_idx = 0; cu_idx < num_compile_units; ++cu_idx)
            target->GetImages().FindCompileUnits(
                compile_units.GetFileSpecAtIndex(cu_idx), sc_list);
        }

        for (const SymbolContext &sc : sc_list) {
          if (sc.comp_unit) {
            const bool can_create = true;
            VariableListSP comp_unit_varlist_sp(
                sc.comp_unit->GetVariableList(can_create));
            if (comp_unit_varlist_sp)
              DumpGlobalVariableList(m_exe_ctx, sc, *comp_unit_varlist_sp, s);
          } else if (sc.module_sp) {
            // Get all global variables for this module
            lldb_private::RegularExpression all_globals_regex(
                llvm::StringRef(".")); // Any global with at least one character
            VariableList variable_list;
            sc.module_sp->FindGlobalVariables(all_globals_regex, UINT32_MAX,
                                              variable_list);
            DumpGlobalVariableList(m_exe_ctx, sc, variable_list, s);
          }
        }
      }
    }

    m_interpreter.PrintWarningsIfNecessary(result.GetOutputStream(),
                                           m_cmd_name);
  }

  OptionGroupOptions m_option_group;
  OptionGroupVariable m_option_variable;
  OptionGroupFormat m_option_format;
  OptionGroupFileList m_option_compile_units;
  OptionGroupFileList m_option_shared_libraries;
  OptionGroupValueObjectDisplay m_varobj_options;
};

#pragma mark CommandObjectTargetModulesSearchPathsAdd

class CommandObjectTargetModulesSearchPathsAdd : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules search-paths add",
                            "Add new image search paths substitution pairs to "
                            "the current target.",
                            nullptr, eCommandRequiresTarget) {
    CommandArgumentEntry arg;
    CommandArgumentData old_prefix_arg;
    CommandArgumentData new_prefix_arg;

    // Define the first variant of this arg pair.
    old_prefix_arg.arg_type = eArgTypeOldPathPrefix;
    old_prefix_arg.arg_repetition = eArgRepeatPairPlus;

    // Define the first variant of this arg pair.
    new_prefix_arg.arg_type = eArgTypeNewPathPrefix;
    new_prefix_arg.arg_repetition = eArgRepeatPairPlus;

    // There are two required arguments that must always occur together, i.e.
    // an argument "pair".  Because they must always occur together, they are
    // treated as two variants of one argument rather than two independent
    // arguments.  Push them both into the first argument position for
    // m_arguments...

    arg.push_back(old_prefix_arg);
    arg.push_back(new_prefix_arg);

    m_arguments.push_back(arg);
  }

  ~CommandObjectTargetModulesSearchPathsAdd() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    const size_t argc = command.GetArgumentCount();
    if (argc & 1) {
      result.AppendError("add requires an even number of arguments\n");
    } else {
      for (size_t i = 0; i < argc; i += 2) {
        const char *from = command.GetArgumentAtIndex(i);
        const char *to = command.GetArgumentAtIndex(i + 1);

        if (from[0] && to[0]) {
          Log *log = GetLog(LLDBLog::Host);
          if (log) {
            LLDB_LOGF(log,
                      "target modules search path adding ImageSearchPath "
                      "pair: '%s' -> '%s'",
                      from, to);
          }
          bool last_pair = ((argc - i) == 2);
          target->GetImageSearchPathList().Append(
              from, to, last_pair); // Notify if this is the last pair
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        } else {
          if (from[0])
            result.AppendError("<path-prefix> can't be empty\n");
          else
            result.AppendError("<new-path-prefix> can't be empty\n");
        }
      }
    }
  }
};

#pragma mark CommandObjectTargetModulesSearchPathsClear

class CommandObjectTargetModulesSearchPathsClear : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsClear(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules search-paths clear",
                            "Clear all current image search path substitution "
                            "pairs from the current target.",
                            "target modules search-paths clear",
                            eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesSearchPathsClear() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    bool notify = true;
    target->GetImageSearchPathList().Clear(notify);
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }
};

#pragma mark CommandObjectTargetModulesSearchPathsInsert

class CommandObjectTargetModulesSearchPathsInsert : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsInsert(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules search-paths insert",
                            "Insert a new image search path substitution pair "
                            "into the current target at the specified index.",
                            nullptr, eCommandRequiresTarget) {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData index_arg;
    CommandArgumentData old_prefix_arg;
    CommandArgumentData new_prefix_arg;

    // Define the first and only variant of this arg.
    index_arg.arg_type = eArgTypeIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // Put the one and only variant into the first arg for m_arguments:
    arg1.push_back(index_arg);

    // Define the first variant of this arg pair.
    old_prefix_arg.arg_type = eArgTypeOldPathPrefix;
    old_prefix_arg.arg_repetition = eArgRepeatPairPlus;

    // Define the first variant of this arg pair.
    new_prefix_arg.arg_type = eArgTypeNewPathPrefix;
    new_prefix_arg.arg_repetition = eArgRepeatPairPlus;

    // There are two required arguments that must always occur together, i.e.
    // an argument "pair".  Because they must always occur together, they are
    // treated as two variants of one argument rather than two independent
    // arguments.  Push them both into the same argument position for
    // m_arguments...

    arg2.push_back(old_prefix_arg);
    arg2.push_back(new_prefix_arg);

    // Add arguments to m_arguments.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectTargetModulesSearchPathsInsert() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (!m_exe_ctx.HasTargetScope() || request.GetCursorIndex() != 0)
      return;

    Target *target = m_exe_ctx.GetTargetPtr();
    const PathMappingList &list = target->GetImageSearchPathList();
    const size_t num = list.GetSize();
    ConstString old_path, new_path;
    for (size_t i = 0; i < num; ++i) {
      if (!list.GetPathsAtIndex(i, old_path, new_path))
        break;
      StreamString strm;
      strm << old_path << " -> " << new_path;
      request.TryCompleteCurrentArg(std::to_string(i), strm.GetString());
    }
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    size_t argc = command.GetArgumentCount();
    // check for at least 3 arguments and an odd number of parameters
    if (argc >= 3 && argc & 1) {
      uint32_t insert_idx;

      if (!llvm::to_integer(command.GetArgumentAtIndex(0), insert_idx)) {
        result.AppendErrorWithFormat(
            "<index> parameter is not an integer: '%s'.\n",
            command.GetArgumentAtIndex(0));
        return;
      }

      // shift off the index
      command.Shift();
      argc = command.GetArgumentCount();

      for (uint32_t i = 0; i < argc; i += 2, ++insert_idx) {
        const char *from = command.GetArgumentAtIndex(i);
        const char *to = command.GetArgumentAtIndex(i + 1);

        if (from[0] && to[0]) {
          bool last_pair = ((argc - i) == 2);
          target->GetImageSearchPathList().Insert(from, to, insert_idx,
                                                  last_pair);
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        } else {
          if (from[0])
            result.AppendError("<path-prefix> can't be empty\n");
          else
            result.AppendError("<new-path-prefix> can't be empty\n");
          return;
        }
      }
    } else {
      result.AppendError("insert requires at least three arguments\n");
    }
  }
};

#pragma mark CommandObjectTargetModulesSearchPathsList

class CommandObjectTargetModulesSearchPathsList : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules search-paths list",
                            "List all current image search path substitution "
                            "pairs in the current target.",
                            "target modules search-paths list",
                            eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesSearchPathsList() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();

    target->GetImageSearchPathList().Dump(&result.GetOutputStream());
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectTargetModulesSearchPathsQuery

class CommandObjectTargetModulesSearchPathsQuery : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsQuery(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target modules search-paths query",
            "Transform a path using the first applicable image search path.",
            nullptr, eCommandRequiresTarget) {
    AddSimpleArgumentList(eArgTypeDirectoryName);
  }

  ~CommandObjectTargetModulesSearchPathsQuery() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    if (command.GetArgumentCount() != 1) {
      result.AppendError("query requires one argument\n");
      return;
    }

    ConstString orig(command.GetArgumentAtIndex(0));
    ConstString transformed;
    if (target->GetImageSearchPathList().RemapPath(orig, transformed))
      result.GetOutputStream().Printf("%s\n", transformed.GetCString());
    else
      result.GetOutputStream().Printf("%s\n", orig.GetCString());

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// Static Helper functions
static void DumpModuleArchitecture(Stream &strm, Module *module,
                                   bool full_triple, uint32_t width) {
  if (module) {
    StreamString arch_strm;

    if (full_triple)
      module->GetArchitecture().DumpTriple(arch_strm.AsRawOstream());
    else
      arch_strm.PutCString(module->GetArchitecture().GetArchitectureName());
    std::string arch_str = std::string(arch_strm.GetString());

    if (width)
      strm.Printf("%-*s", width, arch_str.c_str());
    else
      strm.PutCString(arch_str);
  }
}

static void DumpModuleUUID(Stream &strm, Module *module) {
  if (module && module->GetUUID().IsValid())
    module->GetUUID().Dump(strm);
  else
    strm.PutCString("                                    ");
}

static uint32_t DumpCompileUnitLineTable(CommandInterpreter &interpreter,
                                         Stream &strm, Module *module,
                                         const FileSpec &file_spec,
                                         lldb::DescriptionLevel desc_level) {
  uint32_t num_matches = 0;
  if (module) {
    SymbolContextList sc_list;
    num_matches = module->ResolveSymbolContextsForFileSpec(
        file_spec, 0, false, eSymbolContextCompUnit, sc_list);

    bool first_module = true;
    for (const SymbolContext &sc : sc_list) {
      if (!first_module)
        strm << "\n\n";

      strm << "Line table for " << sc.comp_unit->GetPrimaryFile() << " in `"
           << module->GetFileSpec().GetFilename() << "\n";
      LineTable *line_table = sc.comp_unit->GetLineTable();
      if (line_table)
        line_table->GetDescription(
            &strm, interpreter.GetExecutionContext().GetTargetPtr(),
            desc_level);
      else
        strm << "No line table";

      first_module = false;
    }
  }
  return num_matches;
}

static void DumpFullpath(Stream &strm, const FileSpec *file_spec_ptr,
                         uint32_t width) {
  if (file_spec_ptr) {
    if (width > 0) {
      std::string fullpath = file_spec_ptr->GetPath();
      strm.Printf("%-*s", width, fullpath.c_str());
      return;
    } else {
      file_spec_ptr->Dump(strm.AsRawOstream());
      return;
    }
  }
  // Keep the width spacing correct if things go wrong...
  if (width > 0)
    strm.Printf("%-*s", width, "");
}

static void DumpDirectory(Stream &strm, const FileSpec *file_spec_ptr,
                          uint32_t width) {
  if (file_spec_ptr) {
    if (width > 0)
      strm.Printf("%-*s", width, file_spec_ptr->GetDirectory().AsCString(""));
    else
      file_spec_ptr->GetDirectory().Dump(&strm);
    return;
  }
  // Keep the width spacing correct if things go wrong...
  if (width > 0)
    strm.Printf("%-*s", width, "");
}

static void DumpBasename(Stream &strm, const FileSpec *file_spec_ptr,
                         uint32_t width) {
  if (file_spec_ptr) {
    if (width > 0)
      strm.Printf("%-*s", width, file_spec_ptr->GetFilename().AsCString(""));
    else
      file_spec_ptr->GetFilename().Dump(&strm);
    return;
  }
  // Keep the width spacing correct if things go wrong...
  if (width > 0)
    strm.Printf("%-*s", width, "");
}

static size_t DumpModuleObjfileHeaders(Stream &strm, ModuleList &module_list) {
  std::lock_guard<std::recursive_mutex> guard(module_list.GetMutex());
  const size_t num_modules = module_list.GetSize();
  if (num_modules == 0)
    return 0;

  size_t num_dumped = 0;
  strm.Format("Dumping headers for {0} module(s).\n", num_modules);
  strm.IndentMore();
  for (ModuleSP module_sp : module_list.ModulesNoLocking()) {
    if (module_sp) {
      if (num_dumped++ > 0) {
        strm.EOL();
        strm.EOL();
      }
      ObjectFile *objfile = module_sp->GetObjectFile();
      if (objfile)
        objfile->Dump(&strm);
      else {
        strm.Format("No object file for module: {0:F}\n",
                    module_sp->GetFileSpec());
      }
    }
  }
  strm.IndentLess();
  return num_dumped;
}

static void DumpModuleSymtab(CommandInterpreter &interpreter, Stream &strm,
                             Module *module, SortOrder sort_order,
                             Mangled::NamePreference name_preference) {
  if (!module)
    return;
  if (Symtab *symtab = module->GetSymtab())
    symtab->Dump(&strm, interpreter.GetExecutionContext().GetTargetPtr(),
                 sort_order, name_preference);
}

static void DumpModuleSections(CommandInterpreter &interpreter, Stream &strm,
                               Module *module) {
  if (module) {
    SectionList *section_list = module->GetSectionList();
    if (section_list) {
      strm.Printf("Sections for '%s' (%s):\n",
                  module->GetSpecificationDescription().c_str(),
                  module->GetArchitecture().GetArchitectureName());
      section_list->Dump(strm.AsRawOstream(), strm.GetIndentLevel() + 2,
                         interpreter.GetExecutionContext().GetTargetPtr(), true,
                         UINT32_MAX);
    }
  }
}

static bool DumpModuleSymbolFile(Stream &strm, Module *module) {
  if (module) {
    if (SymbolFile *symbol_file = module->GetSymbolFile(true)) {
      symbol_file->Dump(strm);
      return true;
    }
  }
  return false;
}

static bool GetSeparateDebugInfoList(StructuredData::Array &list,
                                     Module *module, bool errors_only) {
  if (module) {
    if (SymbolFile *symbol_file = module->GetSymbolFile(/*can_create=*/true)) {
      StructuredData::Dictionary d;
      if (symbol_file->GetSeparateDebugInfo(d, errors_only)) {
        list.AddItem(
            std::make_shared<StructuredData::Dictionary>(std::move(d)));
        return true;
      }
    }
  }
  return false;
}

static void DumpDwoFilesTable(Stream &strm,
                              StructuredData::Array &dwo_listings) {
  strm.PutCString("Dwo ID             Err Dwo Path");
  strm.EOL();
  strm.PutCString(
      "------------------ --- -----------------------------------------");
  strm.EOL();
  dwo_listings.ForEach([&strm](StructuredData::Object *dwo) {
    StructuredData::Dictionary *dict = dwo->GetAsDictionary();
    if (!dict)
      return false;

    uint64_t dwo_id;
    if (dict->GetValueForKeyAsInteger("dwo_id", dwo_id))
      strm.Printf("0x%16.16" PRIx64 " ", dwo_id);
    else
      strm.Printf("0x???????????????? ");

    llvm::StringRef error;
    if (dict->GetValueForKeyAsString("error", error))
      strm << "E   " << error;
    else {
      llvm::StringRef resolved_dwo_path;
      if (dict->GetValueForKeyAsString("resolved_dwo_path",
                                       resolved_dwo_path)) {
        strm << "    " << resolved_dwo_path;
        if (resolved_dwo_path.ends_with(".dwp")) {
          llvm::StringRef dwo_name;
          if (dict->GetValueForKeyAsString("dwo_name", dwo_name))
            strm << "(" << dwo_name << ")";
        }
      }
    }
    strm.EOL();
    return true;
  });
}

static void DumpOsoFilesTable(Stream &strm,
                              StructuredData::Array &oso_listings) {
  strm.PutCString("Mod Time           Err Oso Path");
  strm.EOL();
  strm.PutCString("------------------ --- ---------------------");
  strm.EOL();
  oso_listings.ForEach([&strm](StructuredData::Object *oso) {
    StructuredData::Dictionary *dict = oso->GetAsDictionary();
    if (!dict)
      return false;

    uint32_t oso_mod_time;
    if (dict->GetValueForKeyAsInteger("oso_mod_time", oso_mod_time))
      strm.Printf("0x%16.16" PRIx32 " ", oso_mod_time);

    llvm::StringRef error;
    if (dict->GetValueForKeyAsString("error", error))
      strm << "E   " << error;
    else {
      llvm::StringRef oso_path;
      if (dict->GetValueForKeyAsString("oso_path", oso_path))
        strm << "    " << oso_path;
    }
    strm.EOL();
    return true;
  });
}

static void
DumpAddress(ExecutionContextScope *exe_scope, const Address &so_addr,
            bool verbose, bool all_ranges, Stream &strm,
            std::optional<Stream::HighlightSettings> settings = std::nullopt) {
  strm.IndentMore();
  strm.Indent("    Address: ");
  so_addr.Dump(&strm, exe_scope, Address::DumpStyleModuleWithFileAddress);
  strm.PutCString(" (");
  so_addr.Dump(&strm, exe_scope, Address::DumpStyleSectionNameOffset);
  strm.PutCString(")\n");
  strm.Indent("    Summary: ");
  const uint32_t save_indent = strm.GetIndentLevel();
  strm.SetIndentLevel(save_indent + 13);
  so_addr.Dump(&strm, exe_scope, Address::DumpStyleResolvedDescription,
               Address::DumpStyleInvalid, UINT32_MAX, false, settings);
  strm.SetIndentLevel(save_indent);
  // Print out detailed address information when verbose is enabled
  if (verbose) {
    strm.EOL();
    so_addr.Dump(&strm, exe_scope, Address::DumpStyleDetailedSymbolContext,
                 Address::DumpStyleInvalid, UINT32_MAX, all_ranges, settings);
  }
  strm.IndentLess();
}

static bool LookupAddressInModule(CommandInterpreter &interpreter, Stream &strm,
                                  Module *module, uint32_t resolve_mask,
                                  lldb::addr_t raw_addr, lldb::addr_t offset,
                                  bool verbose, bool all_ranges) {
  if (module) {
    lldb::addr_t addr = raw_addr - offset;
    Address so_addr;
    SymbolContext sc;
    Target *target = interpreter.GetExecutionContext().GetTargetPtr();
    if (target && !target->GetSectionLoadList().IsEmpty()) {
      if (!target->GetSectionLoadList().ResolveLoadAddress(addr, so_addr))
        return false;
      else if (so_addr.GetModule().get() != module)
        return false;
    } else {
      if (!module->ResolveFileAddress(addr, so_addr))
        return false;
    }

    ExecutionContextScope *exe_scope =
        interpreter.GetExecutionContext().GetBestExecutionContextScope();
    DumpAddress(exe_scope, so_addr, verbose, all_ranges, strm);
    return true;
  }

  return false;
}

static uint32_t LookupSymbolInModule(CommandInterpreter &interpreter,
                                     Stream &strm, Module *module,
                                     const char *name, bool name_is_regex,
                                     bool verbose, bool all_ranges) {
  if (!module)
    return 0;

  Symtab *symtab = module->GetSymtab();
  if (!symtab)
    return 0;

  SymbolContext sc;
  const bool use_color = interpreter.GetDebugger().GetUseColor();
  std::vector<uint32_t> match_indexes;
  ConstString symbol_name(name);
  uint32_t num_matches = 0;
  if (name_is_regex) {
    RegularExpression name_regexp(symbol_name.GetStringRef());
    num_matches = symtab->AppendSymbolIndexesMatchingRegExAndType(
        name_regexp, eSymbolTypeAny, match_indexes);
  } else {
    num_matches =
        symtab->AppendSymbolIndexesWithName(symbol_name, match_indexes);
  }

  if (num_matches > 0) {
    strm.Indent();
    strm.Printf("%u symbols match %s'%s' in ", num_matches,
                name_is_regex ? "the regular expression " : "", name);
    DumpFullpath(strm, &module->GetFileSpec(), 0);
    strm.PutCString(":\n");
    strm.IndentMore();
    Stream::HighlightSettings settings(
        name, interpreter.GetDebugger().GetRegexMatchAnsiPrefix(),
        interpreter.GetDebugger().GetRegexMatchAnsiSuffix());
    for (uint32_t i = 0; i < num_matches; ++i) {
      Symbol *symbol = symtab->SymbolAtIndex(match_indexes[i]);
      if (symbol) {
        if (symbol->ValueIsAddress()) {
          DumpAddress(
              interpreter.GetExecutionContext().GetBestExecutionContextScope(),
              symbol->GetAddressRef(), verbose, all_ranges, strm,
              use_color && name_is_regex
                  ? std::optional<Stream::HighlightSettings>{settings}
                  : std::nullopt);
          strm.EOL();
        } else {
          strm.IndentMore();
          strm.Indent("    Name: ");
          strm.PutCStringColorHighlighted(
              symbol->GetDisplayName().GetStringRef(),
              use_color && name_is_regex
                  ? std::optional<Stream::HighlightSettings>{settings}
                  : std::nullopt);
          strm.EOL();
          strm.Indent("    Value: ");
          strm.Printf("0x%16.16" PRIx64 "\n", symbol->GetRawValue());
          if (symbol->GetByteSizeIsValid()) {
            strm.Indent("    Size: ");
            strm.Printf("0x%16.16" PRIx64 "\n", symbol->GetByteSize());
          }
          strm.IndentLess();
        }
      }
    }
    strm.IndentLess();
  }
  return num_matches;
}

static void DumpSymbolContextList(
    ExecutionContextScope *exe_scope, Stream &strm,
    const SymbolContextList &sc_list, bool verbose, bool all_ranges,
    std::optional<Stream::HighlightSettings> settings = std::nullopt) {
  strm.IndentMore();
  bool first_module = true;
  for (const SymbolContext &sc : sc_list) {
    if (!first_module)
      strm.EOL();

    AddressRange range;

    sc.GetAddressRange(eSymbolContextEverything, 0, true, range);

    DumpAddress(exe_scope, range.GetBaseAddress(), verbose, all_ranges, strm,
                settings);
    first_module = false;
  }
  strm.IndentLess();
}

static size_t LookupFunctionInModule(CommandInterpreter &interpreter,
                                     Stream &strm, Module *module,
                                     const char *name, bool name_is_regex,
                                     const ModuleFunctionSearchOptions &options,
                                     bool verbose, bool all_ranges) {
  if (module && name && name[0]) {
    SymbolContextList sc_list;
    size_t num_matches = 0;
    if (name_is_regex) {
      RegularExpression function_name_regex((llvm::StringRef(name)));
      module->FindFunctions(function_name_regex, options, sc_list);
    } else {
      ConstString function_name(name);
      module->FindFunctions(function_name, CompilerDeclContext(),
                            eFunctionNameTypeAuto, options, sc_list);
    }
    num_matches = sc_list.GetSize();
    if (num_matches) {
      strm.Indent();
      strm.Printf("%" PRIu64 " match%s found in ", (uint64_t)num_matches,
                  num_matches > 1 ? "es" : "");
      DumpFullpath(strm, &module->GetFileSpec(), 0);
      strm.PutCString(":\n");
      DumpSymbolContextList(
          interpreter.GetExecutionContext().GetBestExecutionContextScope(),
          strm, sc_list, verbose, all_ranges);
    }
    return num_matches;
  }
  return 0;
}

static size_t LookupTypeInModule(Target *target,
                                 CommandInterpreter &interpreter, Stream &strm,
                                 Module *module, const char *name_cstr,
                                 bool name_is_regex) {
  if (module && name_cstr && name_cstr[0]) {
    TypeQuery query(name_cstr);
    TypeResults results;
    module->FindTypes(query, results);

    TypeList type_list;
    SymbolContext sc;
    if (module)
      sc.module_sp = module->shared_from_this();
    // Sort the type results and put the results that matched in \a module
    // first if \a module was specified.
    sc.SortTypeList(results.GetTypeMap(), type_list);
    if (type_list.Empty())
      return 0;

    const uint64_t num_matches = type_list.GetSize();

    strm.Indent();
    strm.Printf("%" PRIu64 " match%s found in ", num_matches,
                num_matches > 1 ? "es" : "");
    DumpFullpath(strm, &module->GetFileSpec(), 0);
    strm.PutCString(":\n");
    for (TypeSP type_sp : type_list.Types()) {
      if (!type_sp)
        continue;
      // Resolve the clang type so that any forward references to types
      // that haven't yet been parsed will get parsed.
      type_sp->GetFullCompilerType();
      type_sp->GetDescription(&strm, eDescriptionLevelFull, true, target);
      // Print all typedef chains
      TypeSP typedef_type_sp(type_sp);
      TypeSP typedefed_type_sp(typedef_type_sp->GetTypedefType());
      while (typedefed_type_sp) {
        strm.EOL();
        strm.Printf("     typedef '%s': ",
                    typedef_type_sp->GetName().GetCString());
        typedefed_type_sp->GetFullCompilerType();
        typedefed_type_sp->GetDescription(&strm, eDescriptionLevelFull, true,
                                          target);
        typedef_type_sp = typedefed_type_sp;
        typedefed_type_sp = typedef_type_sp->GetTypedefType();
      }
      strm.EOL();
    }
    return type_list.GetSize();
  }
  return 0;
}

static size_t LookupTypeHere(Target *target, CommandInterpreter &interpreter,
                             Stream &strm, Module &module,
                             const char *name_cstr, bool name_is_regex) {
  TypeQuery query(name_cstr);
  TypeResults results;
  module.FindTypes(query, results);
  TypeList type_list;
  SymbolContext sc;
  sc.module_sp = module.shared_from_this();
  sc.SortTypeList(results.GetTypeMap(), type_list);
  if (type_list.Empty())
    return 0;

  strm.Indent();
  strm.PutCString("Best match found in ");
  DumpFullpath(strm, &module.GetFileSpec(), 0);
  strm.PutCString(":\n");

  TypeSP type_sp(type_list.GetTypeAtIndex(0));
  if (type_sp) {
    // Resolve the clang type so that any forward references to types that
    // haven't yet been parsed will get parsed.
    type_sp->GetFullCompilerType();
    type_sp->GetDescription(&strm, eDescriptionLevelFull, true, target);
    // Print all typedef chains.
    TypeSP typedef_type_sp(type_sp);
    TypeSP typedefed_type_sp(typedef_type_sp->GetTypedefType());
    while (typedefed_type_sp) {
      strm.EOL();
      strm.Printf("     typedef '%s': ",
                  typedef_type_sp->GetName().GetCString());
      typedefed_type_sp->GetFullCompilerType();
      typedefed_type_sp->GetDescription(&strm, eDescriptionLevelFull, true,
                                        target);
      typedef_type_sp = typedefed_type_sp;
      typedefed_type_sp = typedef_type_sp->GetTypedefType();
    }
  }
  strm.EOL();
  return type_list.GetSize();
}

static uint32_t LookupFileAndLineInModule(CommandInterpreter &interpreter,
                                          Stream &strm, Module *module,
                                          const FileSpec &file_spec,
                                          uint32_t line, bool check_inlines,
                                          bool verbose, bool all_ranges) {
  if (module && file_spec) {
    SymbolContextList sc_list;
    const uint32_t num_matches = module->ResolveSymbolContextsForFileSpec(
        file_spec, line, check_inlines, eSymbolContextEverything, sc_list);
    if (num_matches > 0) {
      strm.Indent();
      strm.Printf("%u match%s found in ", num_matches,
                  num_matches > 1 ? "es" : "");
      strm << file_spec;
      if (line > 0)
        strm.Printf(":%u", line);
      strm << " in ";
      DumpFullpath(strm, &module->GetFileSpec(), 0);
      strm.PutCString(":\n");
      DumpSymbolContextList(
          interpreter.GetExecutionContext().GetBestExecutionContextScope(),
          strm, sc_list, verbose, all_ranges);
      return num_matches;
    }
  }
  return 0;
}

static size_t FindModulesByName(Target *target, const char *module_name,
                                ModuleList &module_list,
                                bool check_global_list) {
  FileSpec module_file_spec(module_name);
  ModuleSpec module_spec(module_file_spec);

  const size_t initial_size = module_list.GetSize();

  if (check_global_list) {
    // Check the global list
    std::lock_guard<std::recursive_mutex> guard(
        Module::GetAllocationModuleCollectionMutex());
    const size_t num_modules = Module::GetNumberAllocatedModules();
    ModuleSP module_sp;
    for (size_t image_idx = 0; image_idx < num_modules; ++image_idx) {
      Module *module = Module::GetAllocatedModuleAtIndex(image_idx);

      if (module) {
        if (module->MatchesModuleSpec(module_spec)) {
          module_sp = module->shared_from_this();
          module_list.AppendIfNeeded(module_sp);
        }
      }
    }
  } else {
    if (target) {
      target->GetImages().FindModules(module_spec, module_list);
      const size_t num_matches = module_list.GetSize();

      // Not found in our module list for our target, check the main shared
      // module list in case it is a extra file used somewhere else
      if (num_matches == 0) {
        module_spec.GetArchitecture() = target->GetArchitecture();
        ModuleList::FindSharedModules(module_spec, module_list);
      }
    } else {
      ModuleList::FindSharedModules(module_spec, module_list);
    }
  }

  return module_list.GetSize() - initial_size;
}

#pragma mark CommandObjectTargetModulesModuleAutoComplete

// A base command object class that can auto complete with module file
// paths

class CommandObjectTargetModulesModuleAutoComplete
    : public CommandObjectParsed {
public:
  CommandObjectTargetModulesModuleAutoComplete(CommandInterpreter &interpreter,
                                               const char *name,
                                               const char *help,
                                               const char *syntax,
                                               uint32_t flags = 0)
      : CommandObjectParsed(interpreter, name, help, syntax, flags) {
    AddSimpleArgumentList(eArgTypeFilename, eArgRepeatStar);
  }

  ~CommandObjectTargetModulesModuleAutoComplete() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eModuleCompletion, request, nullptr);
  }
};

#pragma mark CommandObjectTargetModulesSourceFileAutoComplete

// A base command object class that can auto complete with module source
// file paths

class CommandObjectTargetModulesSourceFileAutoComplete
    : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSourceFileAutoComplete(
      CommandInterpreter &interpreter, const char *name, const char *help,
      const char *syntax, uint32_t flags)
      : CommandObjectParsed(interpreter, name, help, syntax, flags) {
    AddSimpleArgumentList(eArgTypeSourceFile, eArgRepeatPlus);
  }

  ~CommandObjectTargetModulesSourceFileAutoComplete() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eSourceFileCompletion, request, nullptr);
  }
};

#pragma mark CommandObjectTargetModulesDumpObjfile

class CommandObjectTargetModulesDumpObjfile
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpObjfile(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump objfile",
            "Dump the object file headers from one or more target modules.",
            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesDumpObjfile() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();

    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);

    size_t num_dumped = 0;
    if (command.GetArgumentCount() == 0) {
      // Dump all headers for all modules images
      num_dumped = DumpModuleObjfileHeaders(result.GetOutputStream(),
                                            target->GetImages());
      if (num_dumped == 0) {
        result.AppendError("the target has no associated executable images");
      }
    } else {
      // Find the modules that match the basename or full path.
      ModuleList module_list;
      const char *arg_cstr;
      for (int arg_idx = 0;
           (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
           ++arg_idx) {
        size_t num_matched =
            FindModulesByName(target, arg_cstr, module_list, true);
        if (num_matched == 0) {
          result.AppendWarningWithFormat(
              "Unable to find an image that matches '%s'.\n", arg_cstr);
        }
      }
      // Dump all the modules we found.
      num_dumped =
          DumpModuleObjfileHeaders(result.GetOutputStream(), module_list);
    }

    if (num_dumped > 0) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("no matching executable images found");
    }
  }
};

#define LLDB_OPTIONS_target_modules_dump_symtab
#include "CommandOptions.inc"

class CommandObjectTargetModulesDumpSymtab
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpSymtab(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump symtab",
            "Dump the symbol table from one or more target modules.", nullptr,
            eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesDumpSymtab() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'm':
        m_prefer_mangled.SetCurrentValue(true);
        m_prefer_mangled.SetOptionWasSet();
        break;

      case 's':
        m_sort_order = (SortOrder)OptionArgParser::ToOptionEnum(
            option_arg, GetDefinitions()[option_idx].enum_values,
            eSortOrderNone, error);
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_sort_order = eSortOrderNone;
      m_prefer_mangled.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_target_modules_dump_symtab_options);
    }

    SortOrder m_sort_order = eSortOrderNone;
    OptionValueBoolean m_prefer_mangled = {false, false};
  };

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    uint32_t num_dumped = 0;
    Mangled::NamePreference name_preference =
        (m_options.m_prefer_mangled ? Mangled::ePreferMangled
                                    : Mangled::ePreferDemangled);

    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);

    if (command.GetArgumentCount() == 0) {
      // Dump all sections for all modules images
      const ModuleList &module_list = target->GetImages();
      std::lock_guard<std::recursive_mutex> guard(module_list.GetMutex());
      const size_t num_modules = module_list.GetSize();
      if (num_modules > 0) {
        result.GetOutputStream().Format(
            "Dumping symbol table for {0} modules.\n", num_modules);
        for (ModuleSP module_sp : module_list.ModulesNoLocking()) {
          if (num_dumped > 0) {
            result.GetOutputStream().EOL();
            result.GetOutputStream().EOL();
          }
          if (INTERRUPT_REQUESTED(GetDebugger(),
                                  "Interrupted in dump all symtabs with {0} "
                                  "of {1} dumped.", num_dumped, num_modules))
            break;

          num_dumped++;
          DumpModuleSymtab(m_interpreter, result.GetOutputStream(),
                           module_sp.get(), m_options.m_sort_order,
                           name_preference);
        }
      } else {
        result.AppendError("the target has no associated executable images");
        return;
      }
    } else {
      // Dump specified images (by basename or fullpath)
      const char *arg_cstr;
      for (int arg_idx = 0;
           (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
           ++arg_idx) {
        ModuleList module_list;
        const size_t num_matches =
            FindModulesByName(target, arg_cstr, module_list, true);
        if (num_matches > 0) {
          for (ModuleSP module_sp : module_list.Modules()) {
            if (module_sp) {
              if (num_dumped > 0) {
                result.GetOutputStream().EOL();
                result.GetOutputStream().EOL();
              }
              if (INTERRUPT_REQUESTED(GetDebugger(),
                    "Interrupted in dump symtab list with {0} of {1} dumped.",
                    num_dumped, num_matches))
                break;

              num_dumped++;
              DumpModuleSymtab(m_interpreter, result.GetOutputStream(),
                               module_sp.get(), m_options.m_sort_order,
                               name_preference);
            }
          }
        } else
          result.AppendWarningWithFormat(
              "Unable to find an image that matches '%s'.\n", arg_cstr);
      }
    }

    if (num_dumped > 0)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else {
      result.AppendError("no matching executable images found");
    }
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectTargetModulesDumpSections

// Image section dumping command

class CommandObjectTargetModulesDumpSections
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpSections(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump sections",
            "Dump the sections from one or more target modules.",
            //"target modules dump sections [<file1> ...]")
            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesDumpSections() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    uint32_t num_dumped = 0;

    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);

    if (command.GetArgumentCount() == 0) {
      // Dump all sections for all modules images
      const size_t num_modules = target->GetImages().GetSize();
      if (num_modules == 0) {
        result.AppendError("the target has no associated executable images");
        return;
      }

      result.GetOutputStream().Format("Dumping sections for {0} modules.\n",
                                      num_modules);
      for (size_t image_idx = 0; image_idx < num_modules; ++image_idx) {
        if (INTERRUPT_REQUESTED(GetDebugger(),
              "Interrupted in dump all sections with {0} of {1} dumped",
              image_idx, num_modules))
          break;

        num_dumped++;
        DumpModuleSections(
            m_interpreter, result.GetOutputStream(),
            target->GetImages().GetModulePointerAtIndex(image_idx));
      }
    } else {
      // Dump specified images (by basename or fullpath)
      const char *arg_cstr;
      for (int arg_idx = 0;
           (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
           ++arg_idx) {
        ModuleList module_list;
        const size_t num_matches =
            FindModulesByName(target, arg_cstr, module_list, true);
        if (num_matches > 0) {
          for (size_t i = 0; i < num_matches; ++i) {
            if (INTERRUPT_REQUESTED(GetDebugger(),
                  "Interrupted in dump section list with {0} of {1} dumped.",
                  i, num_matches))
              break;

            Module *module = module_list.GetModulePointerAtIndex(i);
            if (module) {
              num_dumped++;
              DumpModuleSections(m_interpreter, result.GetOutputStream(),
                                 module);
            }
          }
        } else {
          // Check the global list
          std::lock_guard<std::recursive_mutex> guard(
              Module::GetAllocationModuleCollectionMutex());

          result.AppendWarningWithFormat(
              "Unable to find an image that matches '%s'.\n", arg_cstr);
        }
      }
    }

    if (num_dumped > 0)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else {
      result.AppendError("no matching executable images found");
    }
  }
};

class CommandObjectTargetModulesDumpClangPCMInfo : public CommandObjectParsed {
public:
  CommandObjectTargetModulesDumpClangPCMInfo(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target modules dump pcm-info",
            "Dump information about the given clang module (pcm).") {
    // Take a single file argument.
    AddSimpleArgumentList(eArgTypeFilename);
  }

  ~CommandObjectTargetModulesDumpClangPCMInfo() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat("'%s' takes exactly one pcm path argument.",
                                   m_cmd_name.c_str());
      return;
    }

    const char *pcm_path = command.GetArgumentAtIndex(0);
    const FileSpec pcm_file{pcm_path};

    if (pcm_file.GetFileNameExtension() != ".pcm") {
      result.AppendError("file must have a .pcm extension");
      return;
    }

    if (!FileSystem::Instance().Exists(pcm_file)) {
      result.AppendError("pcm file does not exist");
      return;
    }

    clang::CompilerInstance compiler;
    compiler.createDiagnostics();

    const char *clang_args[] = {"clang", pcm_path};
    compiler.setInvocation(clang::createInvocation(clang_args));

    // Pass empty deleter to not attempt to free memory that was allocated
    // outside of the current scope, possibly statically.
    std::shared_ptr<llvm::raw_ostream> Out(
        &result.GetOutputStream().AsRawOstream(), [](llvm::raw_ostream *) {});
    clang::DumpModuleInfoAction dump_module_info(Out);
    // DumpModuleInfoAction requires ObjectFilePCHContainerReader.
    compiler.getPCHContainerOperations()->registerReader(
        std::make_unique<clang::ObjectFilePCHContainerReader>());

    if (compiler.ExecuteAction(dump_module_info))
      result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectTargetModulesDumpClangAST

// Clang AST dumping command

class CommandObjectTargetModulesDumpClangAST
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpClangAST(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump ast",
            "Dump the clang ast for a given module's symbol file.",
            //"target modules dump ast [<file1> ...]")
            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesDumpClangAST() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();

    const ModuleList &module_list = target->GetImages();
    const size_t num_modules = module_list.GetSize();
    if (num_modules == 0) {
      result.AppendError("the target has no associated executable images");
      return;
    }

    if (command.GetArgumentCount() == 0) {
      // Dump all ASTs for all modules images
      result.GetOutputStream().Format("Dumping clang ast for {0} modules.\n",
                                      num_modules);
      for (ModuleSP module_sp : module_list.ModulesNoLocking()) {
        if (INTERRUPT_REQUESTED(GetDebugger(), "Interrupted dumping clang ast"))
          break;
        if (SymbolFile *sf = module_sp->GetSymbolFile())
          sf->DumpClangAST(result.GetOutputStream());
      }
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return;
    }

    // Dump specified ASTs (by basename or fullpath)
    for (const Args::ArgEntry &arg : command.entries()) {
      ModuleList module_list;
      const size_t num_matches =
          FindModulesByName(target, arg.c_str(), module_list, true);
      if (num_matches == 0) {
        // Check the global list
        std::lock_guard<std::recursive_mutex> guard(
            Module::GetAllocationModuleCollectionMutex());

        result.AppendWarningWithFormat(
            "Unable to find an image that matches '%s'.\n", arg.c_str());
        continue;
      }

      for (size_t i = 0; i < num_matches; ++i) {
        if (INTERRUPT_REQUESTED(GetDebugger(),
              "Interrupted in dump clang ast list with {0} of {1} dumped.",
              i, num_matches))
          break;

        Module *m = module_list.GetModulePointerAtIndex(i);
        if (SymbolFile *sf = m->GetSymbolFile())
          sf->DumpClangAST(result.GetOutputStream());
      }
    }
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectTargetModulesDumpSymfile

// Image debug symbol dumping command

class CommandObjectTargetModulesDumpSymfile
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpSymfile(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump symfile",
            "Dump the debug symbol file for one or more target modules.",
            //"target modules dump symfile [<file1> ...]")
            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesDumpSymfile() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    uint32_t num_dumped = 0;

    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);

    if (command.GetArgumentCount() == 0) {
      // Dump all sections for all modules images
      const ModuleList &target_modules = target->GetImages();
      std::lock_guard<std::recursive_mutex> guard(target_modules.GetMutex());
      const size_t num_modules = target_modules.GetSize();
      if (num_modules == 0) {
        result.AppendError("the target has no associated executable images");
        return;
      }
      result.GetOutputStream().Format(
          "Dumping debug symbols for {0} modules.\n", num_modules);
      for (ModuleSP module_sp : target_modules.ModulesNoLocking()) {
        if (INTERRUPT_REQUESTED(GetDebugger(), "Interrupted in dumping all "
                                "debug symbols with {0} of {1} modules dumped",
                                 num_dumped, num_modules))
          break;

        if (DumpModuleSymbolFile(result.GetOutputStream(), module_sp.get()))
          num_dumped++;
      }
    } else {
      // Dump specified images (by basename or fullpath)
      const char *arg_cstr;
      for (int arg_idx = 0;
           (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
           ++arg_idx) {
        ModuleList module_list;
        const size_t num_matches =
            FindModulesByName(target, arg_cstr, module_list, true);
        if (num_matches > 0) {
          for (size_t i = 0; i < num_matches; ++i) {
            if (INTERRUPT_REQUESTED(GetDebugger(), "Interrupted dumping {0} "
                                                   "of {1} requested modules",
                                                   i, num_matches))
              break;
            Module *module = module_list.GetModulePointerAtIndex(i);
            if (module) {
              if (DumpModuleSymbolFile(result.GetOutputStream(), module))
                num_dumped++;
            }
          }
        } else
          result.AppendWarningWithFormat(
              "Unable to find an image that matches '%s'.\n", arg_cstr);
      }
    }

    if (num_dumped > 0)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else {
      result.AppendError("no matching executable images found");
    }
  }
};

#pragma mark CommandObjectTargetModulesDumpLineTable
#define LLDB_OPTIONS_target_modules_dump
#include "CommandOptions.inc"

// Image debug line table dumping command

class CommandObjectTargetModulesDumpLineTable
    : public CommandObjectTargetModulesSourceFileAutoComplete {
public:
  CommandObjectTargetModulesDumpLineTable(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesSourceFileAutoComplete(
            interpreter, "target modules dump line-table",
            "Dump the line table for one or more compilation units.", nullptr,
            eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesDumpLineTable() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    uint32_t total_num_dumped = 0;

    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);

    if (command.GetArgumentCount() == 0) {
      result.AppendError("file option must be specified.");
      return;
    } else {
      // Dump specified images (by basename or fullpath)
      const char *arg_cstr;
      for (int arg_idx = 0;
           (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
           ++arg_idx) {
        FileSpec file_spec(arg_cstr);

        const ModuleList &target_modules = target->GetImages();
        std::lock_guard<std::recursive_mutex> guard(target_modules.GetMutex());
        size_t num_modules = target_modules.GetSize();
        if (num_modules > 0) {
          uint32_t num_dumped = 0;
          for (ModuleSP module_sp : target_modules.ModulesNoLocking()) {
            if (INTERRUPT_REQUESTED(GetDebugger(),
                                    "Interrupted in dump all line tables with "
                                    "{0} of {1} dumped", num_dumped,
                                    num_modules))
              break;

            if (DumpCompileUnitLineTable(
                    m_interpreter, result.GetOutputStream(), module_sp.get(),
                    file_spec,
                    m_options.m_verbose ? eDescriptionLevelFull
                                        : eDescriptionLevelBrief))
              num_dumped++;
          }
          if (num_dumped == 0)
            result.AppendWarningWithFormat(
                "No source filenames matched '%s'.\n", arg_cstr);
          else
            total_num_dumped += num_dumped;
        }
      }
    }

    if (total_num_dumped > 0)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else {
      result.AppendError("no source filenames matched any command arguments");
    }
  }

  class CommandOptions : public Options {
  public:
    CommandOptions() { OptionParsingStarting(nullptr); }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      assert(option_idx == 0 && "We only have one option.");
      m_verbose = true;

      return Status();
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_verbose = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_target_modules_dump_options);
    }

    bool m_verbose;
  };

  CommandOptions m_options;
};

#pragma mark CommandObjectTargetModulesDumpSeparateDebugInfoFiles
#define LLDB_OPTIONS_target_modules_dump_separate_debug_info
#include "CommandOptions.inc"

// Image debug separate debug info dumping command

class CommandObjectTargetModulesDumpSeparateDebugInfoFiles
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpSeparateDebugInfoFiles(
      CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump separate-debug-info",
            "List the separate debug info symbol files for one or more target "
            "modules.",
            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesDumpSeparateDebugInfoFiles() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'j':
        m_json.SetCurrentValue(true);
        m_json.SetOptionWasSet();
        break;
      case 'e':
        m_errors_only.SetCurrentValue(true);
        m_errors_only.SetOptionWasSet();
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_json.Clear();
      m_errors_only.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_target_modules_dump_separate_debug_info_options);
    }

    OptionValueBoolean m_json = false;
    OptionValueBoolean m_errors_only = false;
  };

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target &target = GetSelectedTarget();
    uint32_t num_dumped = 0;

    uint32_t addr_byte_size = target.GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);

    StructuredData::Array separate_debug_info_lists_by_module;
    if (command.GetArgumentCount() == 0) {
      // Dump all sections for all modules images
      const ModuleList &target_modules = target.GetImages();
      std::lock_guard<std::recursive_mutex> guard(target_modules.GetMutex());
      const size_t num_modules = target_modules.GetSize();
      if (num_modules == 0) {
        result.AppendError("the target has no associated executable images");
        return;
      }
      for (ModuleSP module_sp : target_modules.ModulesNoLocking()) {
        if (INTERRUPT_REQUESTED(
                GetDebugger(),
                "Interrupted in dumping all "
                "separate debug info with {0} of {1} modules dumped",
                num_dumped, num_modules))
          break;

        if (GetSeparateDebugInfoList(separate_debug_info_lists_by_module,
                                     module_sp.get(),
                                     bool(m_options.m_errors_only)))
          num_dumped++;
      }
    } else {
      // Dump specified images (by basename or fullpath)
      const char *arg_cstr;
      for (int arg_idx = 0;
           (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
           ++arg_idx) {
        ModuleList module_list;
        const size_t num_matches =
            FindModulesByName(&target, arg_cstr, module_list, true);
        if (num_matches > 0) {
          for (size_t i = 0; i < num_matches; ++i) {
            if (INTERRUPT_REQUESTED(GetDebugger(),
                                    "Interrupted dumping {0} "
                                    "of {1} requested modules",
                                    i, num_matches))
              break;
            Module *module = module_list.GetModulePointerAtIndex(i);
            if (GetSeparateDebugInfoList(separate_debug_info_lists_by_module,
                                         module, bool(m_options.m_errors_only)))
              num_dumped++;
          }
        } else
          result.AppendWarningWithFormat(
              "Unable to find an image that matches '%s'.\n", arg_cstr);
      }
    }

    if (num_dumped > 0) {
      Stream &strm = result.GetOutputStream();
      // Display the debug info files in some format.
      if (m_options.m_json) {
        // JSON format
        separate_debug_info_lists_by_module.Dump(strm,
                                                 /*pretty_print=*/true);
      } else {
        // Human-readable table format
        separate_debug_info_lists_by_module.ForEach(
            [&result, &strm](StructuredData::Object *obj) {
              if (!obj) {
                return false;
              }

              // Each item in `separate_debug_info_lists_by_module` should be a
              // valid structured data dictionary.
              StructuredData::Dictionary *separate_debug_info_list =
                  obj->GetAsDictionary();
              if (!separate_debug_info_list) {
                return false;
              }

              llvm::StringRef type;
              llvm::StringRef symfile;
              StructuredData::Array *files;
              if (!(separate_debug_info_list->GetValueForKeyAsString("type",
                                                                     type) &&
                    separate_debug_info_list->GetValueForKeyAsString("symfile",
                                                                     symfile) &&
                    separate_debug_info_list->GetValueForKeyAsArray(
                        "separate-debug-info-files", files))) {
                assert(false);
              }

              strm << "Symbol file: " << symfile;
              strm.EOL();
              strm << "Type: \"" << type << "\"";
              strm.EOL();
              if (type == "dwo") {
                DumpDwoFilesTable(strm, *files);
              } else if (type == "oso") {
                DumpOsoFilesTable(strm, *files);
              } else {
                result.AppendWarningWithFormat(
                    "Found unsupported debug info type '%s'.\n",
                    type.str().c_str());
              }
              return true;
            });
      }
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("no matching executable images found");
    }
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectTargetModulesDump

// Dump multi-word command for target modules

class CommandObjectTargetModulesDump : public CommandObjectMultiword {
public:
  // Constructors and Destructors
  CommandObjectTargetModulesDump(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "target modules dump",
            "Commands for dumping information about one or more target "
            "modules.",
            "target modules dump "
            "[objfile|symtab|sections|ast|symfile|line-table|pcm-info|separate-"
            "debug-info] "
            "[<file1> <file2> ...]") {
    LoadSubCommand("objfile",
                   CommandObjectSP(
                       new CommandObjectTargetModulesDumpObjfile(interpreter)));
    LoadSubCommand(
        "symtab",
        CommandObjectSP(new CommandObjectTargetModulesDumpSymtab(interpreter)));
    LoadSubCommand("sections",
                   CommandObjectSP(new CommandObjectTargetModulesDumpSections(
                       interpreter)));
    LoadSubCommand("symfile",
                   CommandObjectSP(
                       new CommandObjectTargetModulesDumpSymfile(interpreter)));
    LoadSubCommand(
        "ast", CommandObjectSP(
                   new CommandObjectTargetModulesDumpClangAST(interpreter)));
    LoadSubCommand("line-table",
                   CommandObjectSP(new CommandObjectTargetModulesDumpLineTable(
                       interpreter)));
    LoadSubCommand(
        "pcm-info",
        CommandObjectSP(
            new CommandObjectTargetModulesDumpClangPCMInfo(interpreter)));
    LoadSubCommand("separate-debug-info",
                   CommandObjectSP(
                       new CommandObjectTargetModulesDumpSeparateDebugInfoFiles(
                           interpreter)));
  }

  ~CommandObjectTargetModulesDump() override = default;
};

class CommandObjectTargetModulesAdd : public CommandObjectParsed {
public:
  CommandObjectTargetModulesAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules add",
                            "Add a new module to the current target's modules.",
                            "target modules add [<module>]",
                            eCommandRequiresTarget),
        m_symbol_file(LLDB_OPT_SET_1, false, "symfile", 's', 0,
                      eArgTypeFilename,
                      "Fullpath to a stand alone debug "
                      "symbols file for when debug symbols "
                      "are not in the executable.") {
    m_option_group.Append(&m_uuid_option_group, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_symbol_file, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
    AddSimpleArgumentList(eArgTypePath, eArgRepeatStar);
  }

  ~CommandObjectTargetModulesAdd() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  OptionGroupOptions m_option_group;
  OptionGroupUUID m_uuid_option_group;
  OptionGroupFile m_symbol_file;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    bool flush = false;

    const size_t argc = args.GetArgumentCount();
    if (argc == 0) {
      if (m_uuid_option_group.GetOptionValue().OptionWasSet()) {
        // We are given a UUID only, go locate the file
        ModuleSpec module_spec;
        module_spec.GetUUID() =
            m_uuid_option_group.GetOptionValue().GetCurrentValue();
        if (m_symbol_file.GetOptionValue().OptionWasSet())
          module_spec.GetSymbolFileSpec() =
              m_symbol_file.GetOptionValue().GetCurrentValue();
        Status error;
        if (PluginManager::DownloadObjectAndSymbolFile(module_spec, error)) {
          ModuleSP module_sp(
              target->GetOrCreateModule(module_spec, true /* notify */));
          if (module_sp) {
            result.SetStatus(eReturnStatusSuccessFinishResult);
            return;
          } else {
            StreamString strm;
            module_spec.GetUUID().Dump(strm);
            if (module_spec.GetFileSpec()) {
              if (module_spec.GetSymbolFileSpec()) {
                result.AppendErrorWithFormat(
                    "Unable to create the executable or symbol file with "
                    "UUID %s with path %s and symbol file %s",
                    strm.GetData(), module_spec.GetFileSpec().GetPath().c_str(),
                    module_spec.GetSymbolFileSpec().GetPath().c_str());
              } else {
                result.AppendErrorWithFormat(
                    "Unable to create the executable or symbol file with "
                    "UUID %s with path %s",
                    strm.GetData(),
                    module_spec.GetFileSpec().GetPath().c_str());
              }
            } else {
              result.AppendErrorWithFormat("Unable to create the executable "
                                           "or symbol file with UUID %s",
                                           strm.GetData());
            }
            return;
          }
        } else {
          StreamString strm;
          module_spec.GetUUID().Dump(strm);
          result.AppendErrorWithFormat(
              "Unable to locate the executable or symbol file with UUID %s",
              strm.GetData());
          result.SetError(error);
          return;
        }
      } else {
        result.AppendError(
            "one or more executable image paths must be specified");
        return;
      }
    } else {
      for (auto &entry : args.entries()) {
        if (entry.ref().empty())
          continue;

        FileSpec file_spec(entry.ref());
        if (FileSystem::Instance().Exists(file_spec)) {
          ModuleSpec module_spec(file_spec);
          if (m_uuid_option_group.GetOptionValue().OptionWasSet())
            module_spec.GetUUID() =
                m_uuid_option_group.GetOptionValue().GetCurrentValue();
          if (m_symbol_file.GetOptionValue().OptionWasSet())
            module_spec.GetSymbolFileSpec() =
                m_symbol_file.GetOptionValue().GetCurrentValue();
          if (!module_spec.GetArchitecture().IsValid())
            module_spec.GetArchitecture() = target->GetArchitecture();
          Status error;
          ModuleSP module_sp(target->GetOrCreateModule(
              module_spec, true /* notify */, &error));
          if (!module_sp) {
            const char *error_cstr = error.AsCString();
            if (error_cstr)
              result.AppendError(error_cstr);
            else
              result.AppendErrorWithFormat("unsupported module: %s",
                                           entry.c_str());
            return;
          } else {
            flush = true;
          }
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          std::string resolved_path = file_spec.GetPath();
          if (resolved_path != entry.ref()) {
            result.AppendErrorWithFormat(
                "invalid module path '%s' with resolved path '%s'\n",
                entry.ref().str().c_str(), resolved_path.c_str());
            break;
          }
          result.AppendErrorWithFormat("invalid module path '%s'\n",
                                       entry.c_str());
          break;
        }
      }
    }

    if (flush) {
      ProcessSP process = target->GetProcessSP();
      if (process)
        process->Flush();
    }
  }
};

class CommandObjectTargetModulesLoad
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesLoad(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules load",
            "Set the load addresses for one or more sections in a target "
            "module.",
            "target modules load [--file <module> --uuid <uuid>] <sect-name> "
            "<address> [<sect-name> <address> ....]",
            eCommandRequiresTarget),
        m_file_option(LLDB_OPT_SET_1, false, "file", 'f', 0, eArgTypeName,
                      "Fullpath or basename for module to load.", ""),
        m_load_option(LLDB_OPT_SET_1, false, "load", 'l',
                      "Write file contents to the memory.", false, true),
        m_pc_option(LLDB_OPT_SET_1, false, "set-pc-to-entry", 'p',
                    "Set PC to the entry point."
                    " Only applicable with '--load' option.",
                    false, true),
        m_slide_option(LLDB_OPT_SET_1, false, "slide", 's', 0, eArgTypeOffset,
                       "Set the load address for all sections to be the "
                       "virtual address in the file plus the offset.",
                       0) {
    m_option_group.Append(&m_uuid_option_group, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_file_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_load_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_pc_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_slide_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectTargetModulesLoad() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    const bool load = m_load_option.GetOptionValue().GetCurrentValue();
    const bool set_pc = m_pc_option.GetOptionValue().GetCurrentValue();

    const size_t argc = args.GetArgumentCount();
    ModuleSpec module_spec;
    bool search_using_module_spec = false;

    // Allow "load" option to work without --file or --uuid option.
    if (load) {
      if (!m_file_option.GetOptionValue().OptionWasSet() &&
          !m_uuid_option_group.GetOptionValue().OptionWasSet()) {
        ModuleList &module_list = target->GetImages();
        if (module_list.GetSize() == 1) {
          search_using_module_spec = true;
          module_spec.GetFileSpec() =
              module_list.GetModuleAtIndex(0)->GetFileSpec();
        }
      }
    }

    if (m_file_option.GetOptionValue().OptionWasSet()) {
      search_using_module_spec = true;
      const char *arg_cstr = m_file_option.GetOptionValue().GetCurrentValue();
      const bool use_global_module_list = true;
      ModuleList module_list;
      const size_t num_matches = FindModulesByName(
          target, arg_cstr, module_list, use_global_module_list);
      if (num_matches == 1) {
        module_spec.GetFileSpec() =
            module_list.GetModuleAtIndex(0)->GetFileSpec();
      } else if (num_matches > 1) {
        search_using_module_spec = false;
        result.AppendErrorWithFormat(
            "more than 1 module matched by name '%s'\n", arg_cstr);
      } else {
        search_using_module_spec = false;
        result.AppendErrorWithFormat("no object file for module '%s'\n",
                                     arg_cstr);
      }
    }

    if (m_uuid_option_group.GetOptionValue().OptionWasSet()) {
      search_using_module_spec = true;
      module_spec.GetUUID() =
          m_uuid_option_group.GetOptionValue().GetCurrentValue();
    }

    if (search_using_module_spec) {
      ModuleList matching_modules;
      target->GetImages().FindModules(module_spec, matching_modules);
      const size_t num_matches = matching_modules.GetSize();

      char path[PATH_MAX];
      if (num_matches == 1) {
        Module *module = matching_modules.GetModulePointerAtIndex(0);
        if (module) {
          ObjectFile *objfile = module->GetObjectFile();
          if (objfile) {
            SectionList *section_list = module->GetSectionList();
            if (section_list) {
              bool changed = false;
              if (argc == 0) {
                if (m_slide_option.GetOptionValue().OptionWasSet()) {
                  const addr_t slide =
                      m_slide_option.GetOptionValue().GetCurrentValue();
                  const bool slide_is_offset = true;
                  module->SetLoadAddress(*target, slide, slide_is_offset,
                                         changed);
                } else {
                  result.AppendError("one or more section name + load "
                                     "address pair must be specified");
                  return;
                }
              } else {
                if (m_slide_option.GetOptionValue().OptionWasSet()) {
                  result.AppendError("The \"--slide <offset>\" option can't "
                                     "be used in conjunction with setting "
                                     "section load addresses.\n");
                  return;
                }

                for (size_t i = 0; i < argc; i += 2) {
                  const char *sect_name = args.GetArgumentAtIndex(i);
                  const char *load_addr_cstr = args.GetArgumentAtIndex(i + 1);
                  if (sect_name && load_addr_cstr) {
                    ConstString const_sect_name(sect_name);
                    addr_t load_addr;
                    if (llvm::to_integer(load_addr_cstr, load_addr)) {
                      SectionSP section_sp(
                          section_list->FindSectionByName(const_sect_name));
                      if (section_sp) {
                        if (section_sp->IsThreadSpecific()) {
                          result.AppendErrorWithFormat(
                              "thread specific sections are not yet "
                              "supported (section '%s')\n",
                              sect_name);
                          break;
                        } else {
                          if (target->GetSectionLoadList()
                                  .SetSectionLoadAddress(section_sp, load_addr))
                            changed = true;
                          result.AppendMessageWithFormat(
                              "section '%s' loaded at 0x%" PRIx64 "\n",
                              sect_name, load_addr);
                        }
                      } else {
                        result.AppendErrorWithFormat("no section found that "
                                                     "matches the section "
                                                     "name '%s'\n",
                                                     sect_name);
                        break;
                      }
                    } else {
                      result.AppendErrorWithFormat(
                          "invalid load address string '%s'\n", load_addr_cstr);
                      break;
                    }
                  } else {
                    if (sect_name)
                      result.AppendError("section names must be followed by "
                                         "a load address.\n");
                    else
                      result.AppendError("one or more section name + load "
                                         "address pair must be specified.\n");
                    break;
                  }
                }
              }

              if (changed) {
                target->ModulesDidLoad(matching_modules);
                Process *process = m_exe_ctx.GetProcessPtr();
                if (process)
                  process->Flush();
              }
              if (load) {
                ProcessSP process = target->CalculateProcess();
                Address file_entry = objfile->GetEntryPointAddress();
                if (!process) {
                  result.AppendError("No process");
                  return;
                }
                if (set_pc && !file_entry.IsValid()) {
                  result.AppendError("No entry address in object file");
                  return;
                }
                std::vector<ObjectFile::LoadableData> loadables(
                    objfile->GetLoadableData(*target));
                if (loadables.size() == 0) {
                  result.AppendError("No loadable sections");
                  return;
                }
                Status error = process->WriteObjectFile(std::move(loadables));
                if (error.Fail()) {
                  result.AppendError(error.AsCString());
                  return;
                }
                if (set_pc) {
                  ThreadList &thread_list = process->GetThreadList();
                  RegisterContextSP reg_context(
                      thread_list.GetSelectedThread()->GetRegisterContext());
                  addr_t file_entry_addr = file_entry.GetLoadAddress(target);
                  if (!reg_context->SetPC(file_entry_addr)) {
                    result.AppendErrorWithFormat("failed to set PC value to "
                                                 "0x%" PRIx64 "\n",
                                                 file_entry_addr);
                  }
                }
              }
            } else {
              module->GetFileSpec().GetPath(path, sizeof(path));
              result.AppendErrorWithFormat("no sections in object file '%s'\n",
                                           path);
            }
          } else {
            module->GetFileSpec().GetPath(path, sizeof(path));
            result.AppendErrorWithFormat("no object file for module '%s'\n",
                                         path);
          }
        } else {
          FileSpec *module_spec_file = module_spec.GetFileSpecPtr();
          if (module_spec_file) {
            module_spec_file->GetPath(path, sizeof(path));
            result.AppendErrorWithFormat("invalid module '%s'.\n", path);
          } else
            result.AppendError("no module spec");
        }
      } else {
        std::string uuid_str;

        if (module_spec.GetFileSpec())
          module_spec.GetFileSpec().GetPath(path, sizeof(path));
        else
          path[0] = '\0';

        if (module_spec.GetUUIDPtr())
          uuid_str = module_spec.GetUUID().GetAsString();
        if (num_matches > 1) {
          result.AppendErrorWithFormat(
              "multiple modules match%s%s%s%s:\n", path[0] ? " file=" : "",
              path, !uuid_str.empty() ? " uuid=" : "", uuid_str.c_str());
          for (size_t i = 0; i < num_matches; ++i) {
            if (matching_modules.GetModulePointerAtIndex(i)
                    ->GetFileSpec()
                    .GetPath(path, sizeof(path)))
              result.AppendMessageWithFormat("%s\n", path);
          }
        } else {
          result.AppendErrorWithFormat(
              "no modules were found  that match%s%s%s%s.\n",
              path[0] ? " file=" : "", path, !uuid_str.empty() ? " uuid=" : "",
              uuid_str.c_str());
        }
      }
    } else {
      result.AppendError("either the \"--file <module>\" or the \"--uuid "
                         "<uuid>\" option must be specified.\n");
    }
  }

  OptionGroupOptions m_option_group;
  OptionGroupUUID m_uuid_option_group;
  OptionGroupString m_file_option;
  OptionGroupBoolean m_load_option;
  OptionGroupBoolean m_pc_option;
  OptionGroupUInt64 m_slide_option;
};

#pragma mark CommandObjectTargetModulesList
// List images with associated information
#define LLDB_OPTIONS_target_modules_list
#include "CommandOptions.inc"

class CommandObjectTargetModulesList : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;

      const int short_option = m_getopt_table[option_idx].val;
      if (short_option == 'g') {
        m_use_global_module_list = true;
      } else if (short_option == 'a') {
        m_module_addr = OptionArgParser::ToAddress(
            execution_context, option_arg, LLDB_INVALID_ADDRESS, &error);
      } else {
        unsigned long width = 0;
        option_arg.getAsInteger(0, width);
        m_format_array.push_back(std::make_pair(short_option, width));
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_format_array.clear();
      m_use_global_module_list = false;
      m_module_addr = LLDB_INVALID_ADDRESS;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_target_modules_list_options);
    }

    // Instance variables to hold the values for command options.
    typedef std::vector<std::pair<char, uint32_t>> FormatWidthCollection;
    FormatWidthCollection m_format_array;
    bool m_use_global_module_list = false;
    lldb::addr_t m_module_addr = LLDB_INVALID_ADDRESS;
  };

  CommandObjectTargetModulesList(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target modules list",
            "List current executable and dependent shared library images.") {
    AddSimpleArgumentList(eArgTypeModule, eArgRepeatStar);
  }

  ~CommandObjectTargetModulesList() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = GetDebugger().GetSelectedTarget().get();
    const bool use_global_module_list = m_options.m_use_global_module_list;
    // Define a local module list here to ensure it lives longer than any
    // "locker" object which might lock its contents below (through the
    // "module_list_ptr" variable).
    ModuleList module_list;
    if (target == nullptr && !use_global_module_list) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      return;
    } else {
      if (target) {
        uint32_t addr_byte_size =
            target->GetArchitecture().GetAddressByteSize();
        result.GetOutputStream().SetAddressByteSize(addr_byte_size);
        result.GetErrorStream().SetAddressByteSize(addr_byte_size);
      }
      // Dump all sections for all modules images
      Stream &strm = result.GetOutputStream();

      if (m_options.m_module_addr != LLDB_INVALID_ADDRESS) {
        if (target) {
          Address module_address;
          if (module_address.SetLoadAddress(m_options.m_module_addr, target)) {
            ModuleSP module_sp(module_address.GetModule());
            if (module_sp) {
              PrintModule(target, module_sp.get(), 0, strm);
              result.SetStatus(eReturnStatusSuccessFinishResult);
            } else {
              result.AppendErrorWithFormat(
                  "Couldn't find module matching address: 0x%" PRIx64 ".",
                  m_options.m_module_addr);
            }
          } else {
            result.AppendErrorWithFormat(
                "Couldn't find module containing address: 0x%" PRIx64 ".",
                m_options.m_module_addr);
          }
        } else {
          result.AppendError(
              "Can only look up modules by address with a valid target.");
        }
        return;
      }

      size_t num_modules = 0;

      // This locker will be locked on the mutex in module_list_ptr if it is
      // non-nullptr. Otherwise it will lock the
      // AllocationModuleCollectionMutex when accessing the global module list
      // directly.
      std::unique_lock<std::recursive_mutex> guard(
          Module::GetAllocationModuleCollectionMutex(), std::defer_lock);

      const ModuleList *module_list_ptr = nullptr;
      const size_t argc = command.GetArgumentCount();
      if (argc == 0) {
        if (use_global_module_list) {
          guard.lock();
          num_modules = Module::GetNumberAllocatedModules();
        } else {
          module_list_ptr = &target->GetImages();
        }
      } else {
        for (const Args::ArgEntry &arg : command) {
          // Dump specified images (by basename or fullpath)
          const size_t num_matches = FindModulesByName(
              target, arg.c_str(), module_list, use_global_module_list);
          if (num_matches == 0) {
            if (argc == 1) {
              result.AppendErrorWithFormat("no modules found that match '%s'",
                                           arg.c_str());
              return;
            }
          }
        }

        module_list_ptr = &module_list;
      }

      std::unique_lock<std::recursive_mutex> lock;
      if (module_list_ptr != nullptr) {
        lock =
            std::unique_lock<std::recursive_mutex>(module_list_ptr->GetMutex());

        num_modules = module_list_ptr->GetSize();
      }

      if (num_modules > 0) {
        for (uint32_t image_idx = 0; image_idx < num_modules; ++image_idx) {
          ModuleSP module_sp;
          Module *module;
          if (module_list_ptr) {
            module_sp = module_list_ptr->GetModuleAtIndexUnlocked(image_idx);
            module = module_sp.get();
          } else {
            module = Module::GetAllocatedModuleAtIndex(image_idx);
            module_sp = module->shared_from_this();
          }

          const size_t indent = strm.Printf("[%3u] ", image_idx);
          PrintModule(target, module, indent, strm);
        }
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        if (argc) {
          if (use_global_module_list)
            result.AppendError(
                "the global module list has no matching modules");
          else
            result.AppendError("the target has no matching modules");
        } else {
          if (use_global_module_list)
            result.AppendError("the global module list is empty");
          else
            result.AppendError(
                "the target has no associated executable images");
        }
        return;
      }
    }
  }

  void PrintModule(Target *target, Module *module, int indent, Stream &strm) {
    if (module == nullptr) {
      strm.PutCString("Null module");
      return;
    }

    bool dump_object_name = false;
    if (m_options.m_format_array.empty()) {
      m_options.m_format_array.push_back(std::make_pair('u', 0));
      m_options.m_format_array.push_back(std::make_pair('h', 0));
      m_options.m_format_array.push_back(std::make_pair('f', 0));
      m_options.m_format_array.push_back(std::make_pair('S', 0));
    }
    const size_t num_entries = m_options.m_format_array.size();
    bool print_space = false;
    for (size_t i = 0; i < num_entries; ++i) {
      if (print_space)
        strm.PutChar(' ');
      print_space = true;
      const char format_char = m_options.m_format_array[i].first;
      uint32_t width = m_options.m_format_array[i].second;
      switch (format_char) {
      case 'A':
        DumpModuleArchitecture(strm, module, false, width);
        break;

      case 't':
        DumpModuleArchitecture(strm, module, true, width);
        break;

      case 'f':
        DumpFullpath(strm, &module->GetFileSpec(), width);
        dump_object_name = true;
        break;

      case 'd':
        DumpDirectory(strm, &module->GetFileSpec(), width);
        break;

      case 'b':
        DumpBasename(strm, &module->GetFileSpec(), width);
        dump_object_name = true;
        break;

      case 'h':
      case 'o':
        // Image header address
        {
          uint32_t addr_nibble_width =
              target ? (target->GetArchitecture().GetAddressByteSize() * 2)
                     : 16;

          ObjectFile *objfile = module->GetObjectFile();
          if (objfile) {
            Address base_addr(objfile->GetBaseAddress());
            if (base_addr.IsValid()) {
              if (target && !target->GetSectionLoadList().IsEmpty()) {
                lldb::addr_t load_addr = base_addr.GetLoadAddress(target);
                if (load_addr == LLDB_INVALID_ADDRESS) {
                  base_addr.Dump(&strm, target,
                                 Address::DumpStyleModuleWithFileAddress,
                                 Address::DumpStyleFileAddress);
                } else {
                  if (format_char == 'o') {
                    // Show the offset of slide for the image
                    strm.Printf("0x%*.*" PRIx64, addr_nibble_width,
                                addr_nibble_width,
                                load_addr - base_addr.GetFileAddress());
                  } else {
                    // Show the load address of the image
                    strm.Printf("0x%*.*" PRIx64, addr_nibble_width,
                                addr_nibble_width, load_addr);
                  }
                }
                break;
              }
              // The address was valid, but the image isn't loaded, output the
              // address in an appropriate format
              base_addr.Dump(&strm, target, Address::DumpStyleFileAddress);
              break;
            }
          }
          strm.Printf("%*s", addr_nibble_width + 2, "");
        }
        break;

      case 'r': {
        size_t ref_count = 0;
        char in_shared_cache = 'Y';

        ModuleSP module_sp(module->shared_from_this());
        if (!ModuleList::ModuleIsInCache(module))
          in_shared_cache = 'N';
        if (module_sp) {
          // Take one away to make sure we don't count our local "module_sp"
          ref_count = module_sp.use_count() - 1;
        }
        if (width)
          strm.Printf("{%c %*" PRIu64 "}", in_shared_cache, width, (uint64_t)ref_count);
        else
          strm.Printf("{%c %" PRIu64 "}", in_shared_cache, (uint64_t)ref_count);
      } break;

      case 's':
      case 'S': {
        if (const SymbolFile *symbol_file = module->GetSymbolFile()) {
          const FileSpec symfile_spec =
              symbol_file->GetObjectFile()->GetFileSpec();
          if (format_char == 'S') {
            // Dump symbol file only if different from module file
            if (!symfile_spec || symfile_spec == module->GetFileSpec()) {
              print_space = false;
              break;
            }
            // Add a newline and indent past the index
            strm.Printf("\n%*s", indent, "");
          }
          DumpFullpath(strm, &symfile_spec, width);
          dump_object_name = true;
          break;
        }
        strm.Printf("%.*s", width, "<NONE>");
      } break;

      case 'm':
        strm.Format("{0:%c}", llvm::fmt_align(module->GetModificationTime(),
                                              llvm::AlignStyle::Left, width));
        break;

      case 'p':
        strm.Printf("%p", static_cast<void *>(module));
        break;

      case 'u':
        DumpModuleUUID(strm, module);
        break;

      default:
        break;
      }
    }
    if (dump_object_name) {
      const char *object_name = module->GetObjectName().GetCString();
      if (object_name)
        strm.Printf("(%s)", object_name);
    }
    strm.EOL();
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectTargetModulesShowUnwind

// Lookup unwind information in images
#define LLDB_OPTIONS_target_modules_show_unwind
#include "CommandOptions.inc"

class CommandObjectTargetModulesShowUnwind : public CommandObjectParsed {
public:
  enum {
    eLookupTypeInvalid = -1,
    eLookupTypeAddress = 0,
    eLookupTypeSymbol,
    eLookupTypeFunction,
    eLookupTypeFunctionOrSymbol,
    kNumLookupTypes
  };

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;

      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a': {
        m_str = std::string(option_arg);
        m_type = eLookupTypeAddress;
        m_addr = OptionArgParser::ToAddress(execution_context, option_arg,
                                            LLDB_INVALID_ADDRESS, &error);
        if (m_addr == LLDB_INVALID_ADDRESS)
          error.SetErrorStringWithFormat("invalid address string '%s'",
                                         option_arg.str().c_str());
        break;
      }

      case 'n':
        m_str = std::string(option_arg);
        m_type = eLookupTypeFunctionOrSymbol;
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_type = eLookupTypeInvalid;
      m_str.clear();
      m_addr = LLDB_INVALID_ADDRESS;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_target_modules_show_unwind_options);
    }

    // Instance variables to hold the values for command options.

    int m_type = eLookupTypeInvalid; // Should be a eLookupTypeXXX enum after
                                     // parsing options
    std::string m_str; // Holds name lookup
    lldb::addr_t m_addr = LLDB_INVALID_ADDRESS; // Holds the address to lookup
  };

  CommandObjectTargetModulesShowUnwind(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target modules show-unwind",
            "Show synthesized unwind instructions for a function.", nullptr,
            eCommandRequiresTarget | eCommandRequiresProcess |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectTargetModulesShowUnwind() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    Process *process = m_exe_ctx.GetProcessPtr();
    ABI *abi = nullptr;
    if (process)
      abi = process->GetABI().get();

    if (process == nullptr) {
      result.AppendError(
          "You must have a process running to use this command.");
      return;
    }

    ThreadList threads(process->GetThreadList());
    if (threads.GetSize() == 0) {
      result.AppendError("The process must be paused to use this command.");
      return;
    }

    ThreadSP thread(threads.GetThreadAtIndex(0));
    if (!thread) {
      result.AppendError("The process must be paused to use this command.");
      return;
    }

    SymbolContextList sc_list;

    if (m_options.m_type == eLookupTypeFunctionOrSymbol) {
      ConstString function_name(m_options.m_str.c_str());
      ModuleFunctionSearchOptions function_options;
      function_options.include_symbols = true;
      function_options.include_inlines = false;
      target->GetImages().FindFunctions(function_name, eFunctionNameTypeAuto,
                                        function_options, sc_list);
    } else if (m_options.m_type == eLookupTypeAddress && target) {
      Address addr;
      if (target->GetSectionLoadList().ResolveLoadAddress(m_options.m_addr,
                                                          addr)) {
        SymbolContext sc;
        ModuleSP module_sp(addr.GetModule());
        module_sp->ResolveSymbolContextForAddress(addr,
                                                  eSymbolContextEverything, sc);
        if (sc.function || sc.symbol) {
          sc_list.Append(sc);
        }
      }
    } else {
      result.AppendError(
          "address-expression or function name option must be specified.");
      return;
    }

    if (sc_list.GetSize() == 0) {
      result.AppendErrorWithFormat("no unwind data found that matches '%s'.",
                                   m_options.m_str.c_str());
      return;
    }

    for (const SymbolContext &sc : sc_list) {
      if (sc.symbol == nullptr && sc.function == nullptr)
        continue;
      if (!sc.module_sp || sc.module_sp->GetObjectFile() == nullptr)
        continue;
      AddressRange range;
      if (!sc.GetAddressRange(eSymbolContextFunction | eSymbolContextSymbol, 0,
                              false, range))
        continue;
      if (!range.GetBaseAddress().IsValid())
        continue;
      ConstString funcname(sc.GetFunctionName());
      if (funcname.IsEmpty())
        continue;
      addr_t start_addr = range.GetBaseAddress().GetLoadAddress(target);
      if (abi)
        start_addr = abi->FixCodeAddress(start_addr);

      FuncUnwindersSP func_unwinders_sp(
          sc.module_sp->GetUnwindTable()
              .GetUncachedFuncUnwindersContainingAddress(start_addr, sc));
      if (!func_unwinders_sp)
        continue;

      result.GetOutputStream().Printf(
          "UNWIND PLANS for %s`%s (start addr 0x%" PRIx64 ")\n",
          sc.module_sp->GetPlatformFileSpec().GetFilename().AsCString(),
          funcname.AsCString(), start_addr);

      Args args;
      target->GetUserSpecifiedTrapHandlerNames(args);
      size_t count = args.GetArgumentCount();
      for (size_t i = 0; i < count; i++) {
        const char *trap_func_name = args.GetArgumentAtIndex(i);
        if (strcmp(funcname.GetCString(), trap_func_name) == 0)
          result.GetOutputStream().Printf(
              "This function is "
              "treated as a trap handler function via user setting.\n");
      }
      PlatformSP platform_sp(target->GetPlatform());
      if (platform_sp) {
        const std::vector<ConstString> trap_handler_names(
            platform_sp->GetTrapHandlerSymbolNames());
        for (ConstString trap_name : trap_handler_names) {
          if (trap_name == funcname) {
            result.GetOutputStream().Printf(
                "This function's "
                "name is listed by the platform as a trap handler.\n");
          }
        }
      }

      result.GetOutputStream().Printf("\n");

      UnwindPlanSP non_callsite_unwind_plan =
          func_unwinders_sp->GetUnwindPlanAtNonCallSite(*target, *thread);
      if (non_callsite_unwind_plan) {
        result.GetOutputStream().Printf(
            "Asynchronous (not restricted to call-sites) UnwindPlan is '%s'\n",
            non_callsite_unwind_plan->GetSourceName().AsCString());
      }
      UnwindPlanSP callsite_unwind_plan =
          func_unwinders_sp->GetUnwindPlanAtCallSite(*target, *thread);
      if (callsite_unwind_plan) {
        result.GetOutputStream().Printf(
            "Synchronous (restricted to call-sites) UnwindPlan is '%s'\n",
            callsite_unwind_plan->GetSourceName().AsCString());
      }
      UnwindPlanSP fast_unwind_plan =
          func_unwinders_sp->GetUnwindPlanFastUnwind(*target, *thread);
      if (fast_unwind_plan) {
        result.GetOutputStream().Printf(
            "Fast UnwindPlan is '%s'\n",
            fast_unwind_plan->GetSourceName().AsCString());
      }

      result.GetOutputStream().Printf("\n");

      UnwindPlanSP assembly_sp =
          func_unwinders_sp->GetAssemblyUnwindPlan(*target, *thread);
      if (assembly_sp) {
        result.GetOutputStream().Printf(
            "Assembly language inspection UnwindPlan:\n");
        assembly_sp->Dump(result.GetOutputStream(), thread.get(),
                          LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP of_unwind_sp =
          func_unwinders_sp->GetObjectFileUnwindPlan(*target);
      if (of_unwind_sp) {
        result.GetOutputStream().Printf("object file UnwindPlan:\n");
        of_unwind_sp->Dump(result.GetOutputStream(), thread.get(),
                           LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP of_unwind_augmented_sp =
          func_unwinders_sp->GetObjectFileAugmentedUnwindPlan(*target, *thread);
      if (of_unwind_augmented_sp) {
        result.GetOutputStream().Printf("object file augmented UnwindPlan:\n");
        of_unwind_augmented_sp->Dump(result.GetOutputStream(), thread.get(),
                                     LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP ehframe_sp =
          func_unwinders_sp->GetEHFrameUnwindPlan(*target);
      if (ehframe_sp) {
        result.GetOutputStream().Printf("eh_frame UnwindPlan:\n");
        ehframe_sp->Dump(result.GetOutputStream(), thread.get(),
                         LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP ehframe_augmented_sp =
          func_unwinders_sp->GetEHFrameAugmentedUnwindPlan(*target, *thread);
      if (ehframe_augmented_sp) {
        result.GetOutputStream().Printf("eh_frame augmented UnwindPlan:\n");
        ehframe_augmented_sp->Dump(result.GetOutputStream(), thread.get(),
                                   LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      if (UnwindPlanSP plan_sp =
              func_unwinders_sp->GetDebugFrameUnwindPlan(*target)) {
        result.GetOutputStream().Printf("debug_frame UnwindPlan:\n");
        plan_sp->Dump(result.GetOutputStream(), thread.get(),
                      LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      if (UnwindPlanSP plan_sp =
              func_unwinders_sp->GetDebugFrameAugmentedUnwindPlan(*target,
                                                                  *thread)) {
        result.GetOutputStream().Printf("debug_frame augmented UnwindPlan:\n");
        plan_sp->Dump(result.GetOutputStream(), thread.get(),
                      LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP arm_unwind_sp =
          func_unwinders_sp->GetArmUnwindUnwindPlan(*target);
      if (arm_unwind_sp) {
        result.GetOutputStream().Printf("ARM.exidx unwind UnwindPlan:\n");
        arm_unwind_sp->Dump(result.GetOutputStream(), thread.get(),
                            LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      if (UnwindPlanSP symfile_plan_sp =
              func_unwinders_sp->GetSymbolFileUnwindPlan(*thread)) {
        result.GetOutputStream().Printf("Symbol file UnwindPlan:\n");
        symfile_plan_sp->Dump(result.GetOutputStream(), thread.get(),
                              LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP compact_unwind_sp =
          func_unwinders_sp->GetCompactUnwindUnwindPlan(*target);
      if (compact_unwind_sp) {
        result.GetOutputStream().Printf("Compact unwind UnwindPlan:\n");
        compact_unwind_sp->Dump(result.GetOutputStream(), thread.get(),
                                LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      if (fast_unwind_plan) {
        result.GetOutputStream().Printf("Fast UnwindPlan:\n");
        fast_unwind_plan->Dump(result.GetOutputStream(), thread.get(),
                               LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      ABISP abi_sp = process->GetABI();
      if (abi_sp) {
        UnwindPlan arch_default(lldb::eRegisterKindGeneric);
        if (abi_sp->CreateDefaultUnwindPlan(arch_default)) {
          result.GetOutputStream().Printf("Arch default UnwindPlan:\n");
          arch_default.Dump(result.GetOutputStream(), thread.get(),
                            LLDB_INVALID_ADDRESS);
          result.GetOutputStream().Printf("\n");
        }

        UnwindPlan arch_entry(lldb::eRegisterKindGeneric);
        if (abi_sp->CreateFunctionEntryUnwindPlan(arch_entry)) {
          result.GetOutputStream().Printf(
              "Arch default at entry point UnwindPlan:\n");
          arch_entry.Dump(result.GetOutputStream(), thread.get(),
                          LLDB_INVALID_ADDRESS);
          result.GetOutputStream().Printf("\n");
        }
      }

      result.GetOutputStream().Printf("\n");
    }
  }

  CommandOptions m_options;
};

// Lookup information in images
#define LLDB_OPTIONS_target_modules_lookup
#include "CommandOptions.inc"

class CommandObjectTargetModulesLookup : public CommandObjectParsed {
public:
  enum {
    eLookupTypeInvalid = -1,
    eLookupTypeAddress = 0,
    eLookupTypeSymbol,
    eLookupTypeFileLine, // Line is optional
    eLookupTypeFunction,
    eLookupTypeFunctionOrSymbol,
    eLookupTypeType,
    kNumLookupTypes
  };

  class CommandOptions : public Options {
  public:
    CommandOptions() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;

      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a': {
        m_type = eLookupTypeAddress;
        m_addr = OptionArgParser::ToAddress(execution_context, option_arg,
                                            LLDB_INVALID_ADDRESS, &error);
      } break;

      case 'o':
        if (option_arg.getAsInteger(0, m_offset))
          error.SetErrorStringWithFormat("invalid offset string '%s'",
                                         option_arg.str().c_str());
        break;

      case 's':
        m_str = std::string(option_arg);
        m_type = eLookupTypeSymbol;
        break;

      case 'f':
        m_file.SetFile(option_arg, FileSpec::Style::native);
        m_type = eLookupTypeFileLine;
        break;

      case 'i':
        m_include_inlines = false;
        break;

      case 'l':
        if (option_arg.getAsInteger(0, m_line_number))
          error.SetErrorStringWithFormat("invalid line number string '%s'",
                                         option_arg.str().c_str());
        else if (m_line_number == 0)
          error.SetErrorString("zero is an invalid line number");
        m_type = eLookupTypeFileLine;
        break;

      case 'F':
        m_str = std::string(option_arg);
        m_type = eLookupTypeFunction;
        break;

      case 'n':
        m_str = std::string(option_arg);
        m_type = eLookupTypeFunctionOrSymbol;
        break;

      case 't':
        m_str = std::string(option_arg);
        m_type = eLookupTypeType;
        break;

      case 'v':
        m_verbose = true;
        break;

      case 'A':
        m_print_all = true;
        break;

      case 'r':
        m_use_regex = true;
        break;

      case '\x01':
        m_all_ranges = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_type = eLookupTypeInvalid;
      m_str.clear();
      m_file.Clear();
      m_addr = LLDB_INVALID_ADDRESS;
      m_offset = 0;
      m_line_number = 0;
      m_use_regex = false;
      m_include_inlines = true;
      m_all_ranges = false;
      m_verbose = false;
      m_print_all = false;
    }

    Status OptionParsingFinished(ExecutionContext *execution_context) override {
      Status status;
      if (m_all_ranges && !m_verbose) {
        status.SetErrorString("--show-variable-ranges must be used in "
                              "conjunction with --verbose.");
      }
      return status;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_target_modules_lookup_options);
    }

    int m_type;        // Should be a eLookupTypeXXX enum after parsing options
    std::string m_str; // Holds name lookup
    FileSpec m_file;   // Files for file lookups
    lldb::addr_t m_addr; // Holds the address to lookup
    lldb::addr_t
        m_offset; // Subtract this offset from m_addr before doing lookups.
    uint32_t m_line_number; // Line number for file+line lookups
    bool m_use_regex;       // Name lookups in m_str are regular expressions.
    bool m_include_inlines; // Check for inline entries when looking up by
                            // file/line.
    bool m_all_ranges;      // Print all ranges or single range.
    bool m_verbose;         // Enable verbose lookup info
    bool m_print_all; // Print all matches, even in cases where there's a best
                      // match.
  };

  CommandObjectTargetModulesLookup(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules lookup",
                            "Look up information within executable and "
                            "dependent shared library images.",
                            nullptr, eCommandRequiresTarget) {
    AddSimpleArgumentList(eArgTypeFilename, eArgRepeatStar);
  }

  ~CommandObjectTargetModulesLookup() override = default;

  Options *GetOptions() override { return &m_options; }

  bool LookupHere(CommandInterpreter &interpreter, CommandReturnObject &result,
                  bool &syntax_error) {
    switch (m_options.m_type) {
    case eLookupTypeAddress:
    case eLookupTypeFileLine:
    case eLookupTypeFunction:
    case eLookupTypeFunctionOrSymbol:
    case eLookupTypeSymbol:
    default:
      return false;
    case eLookupTypeType:
      break;
    }

    StackFrameSP frame = m_exe_ctx.GetFrameSP();

    if (!frame)
      return false;

    const SymbolContext &sym_ctx(frame->GetSymbolContext(eSymbolContextModule));

    if (!sym_ctx.module_sp)
      return false;

    switch (m_options.m_type) {
    default:
      return false;
    case eLookupTypeType:
      if (!m_options.m_str.empty()) {
        if (LookupTypeHere(&GetSelectedTarget(), m_interpreter,
                           result.GetOutputStream(), *sym_ctx.module_sp,
                           m_options.m_str.c_str(), m_options.m_use_regex)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;
    }

    return false;
  }

  bool LookupInModule(CommandInterpreter &interpreter, Module *module,
                      CommandReturnObject &result, bool &syntax_error) {
    switch (m_options.m_type) {
    case eLookupTypeAddress:
      if (m_options.m_addr != LLDB_INVALID_ADDRESS) {
        if (LookupAddressInModule(
                m_interpreter, result.GetOutputStream(), module,
                eSymbolContextEverything |
                    (m_options.m_verbose
                         ? static_cast<int>(eSymbolContextVariable)
                         : 0),
                m_options.m_addr, m_options.m_offset, m_options.m_verbose,
                m_options.m_all_ranges)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    case eLookupTypeSymbol:
      if (!m_options.m_str.empty()) {
        if (LookupSymbolInModule(m_interpreter, result.GetOutputStream(),
                                 module, m_options.m_str.c_str(),
                                 m_options.m_use_regex, m_options.m_verbose,
                                 m_options.m_all_ranges)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    case eLookupTypeFileLine:
      if (m_options.m_file) {
        if (LookupFileAndLineInModule(
                m_interpreter, result.GetOutputStream(), module,
                m_options.m_file, m_options.m_line_number,
                m_options.m_include_inlines, m_options.m_verbose,
                m_options.m_all_ranges)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    case eLookupTypeFunctionOrSymbol:
    case eLookupTypeFunction:
      if (!m_options.m_str.empty()) {
        ModuleFunctionSearchOptions function_options;
        function_options.include_symbols =
            m_options.m_type == eLookupTypeFunctionOrSymbol;
        function_options.include_inlines = m_options.m_include_inlines;

        if (LookupFunctionInModule(m_interpreter, result.GetOutputStream(),
                                   module, m_options.m_str.c_str(),
                                   m_options.m_use_regex, function_options,
                                   m_options.m_verbose,
                                   m_options.m_all_ranges)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    case eLookupTypeType:
      if (!m_options.m_str.empty()) {
        if (LookupTypeInModule(
                &GetSelectedTarget(), m_interpreter, result.GetOutputStream(),
                module, m_options.m_str.c_str(), m_options.m_use_regex)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    default:
      m_options.GenerateOptionUsage(
          result.GetErrorStream(), *this,
          GetCommandInterpreter().GetDebugger().GetTerminalWidth());
      syntax_error = true;
      break;
    }

    result.SetStatus(eReturnStatusFailed);
    return false;
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();
    bool syntax_error = false;
    uint32_t i;
    uint32_t num_successful_lookups = 0;
    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);
    // Dump all sections for all modules images

    if (command.GetArgumentCount() == 0) {
      ModuleSP current_module;

      // Where it is possible to look in the current symbol context first,
      // try that.  If this search was successful and --all was not passed,
      // don't print anything else.
      if (LookupHere(m_interpreter, result, syntax_error)) {
        result.GetOutputStream().EOL();
        num_successful_lookups++;
        if (!m_options.m_print_all) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return;
        }
      }

      // Dump all sections for all other modules

      const ModuleList &target_modules = target->GetImages();
      std::lock_guard<std::recursive_mutex> guard(target_modules.GetMutex());
      if (target_modules.GetSize() == 0) {
        result.AppendError("the target has no associated executable images");
        return;
      }

      for (ModuleSP module_sp : target_modules.ModulesNoLocking()) {
        if (module_sp != current_module &&
            LookupInModule(m_interpreter, module_sp.get(), result,
                           syntax_error)) {
          result.GetOutputStream().EOL();
          num_successful_lookups++;
        }
      }
    } else {
      // Dump specified images (by basename or fullpath)
      const char *arg_cstr;
      for (i = 0; (arg_cstr = command.GetArgumentAtIndex(i)) != nullptr &&
                  !syntax_error;
           ++i) {
        ModuleList module_list;
        const size_t num_matches =
            FindModulesByName(target, arg_cstr, module_list, false);
        if (num_matches > 0) {
          for (size_t j = 0; j < num_matches; ++j) {
            Module *module = module_list.GetModulePointerAtIndex(j);
            if (module) {
              if (LookupInModule(m_interpreter, module, result, syntax_error)) {
                result.GetOutputStream().EOL();
                num_successful_lookups++;
              }
            }
          }
        } else
          result.AppendWarningWithFormat(
              "Unable to find an image that matches '%s'.\n", arg_cstr);
      }
    }

    if (num_successful_lookups > 0)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else
      result.SetStatus(eReturnStatusFailed);
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectMultiwordImageSearchPaths

// CommandObjectMultiwordImageSearchPaths

class CommandObjectTargetModulesImageSearchPaths
    : public CommandObjectMultiword {
public:
  CommandObjectTargetModulesImageSearchPaths(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "target modules search-paths",
            "Commands for managing module search paths for a target.",
            "target modules search-paths <subcommand> [<subcommand-options>]") {
    LoadSubCommand(
        "add", CommandObjectSP(
                   new CommandObjectTargetModulesSearchPathsAdd(interpreter)));
    LoadSubCommand(
        "clear", CommandObjectSP(new CommandObjectTargetModulesSearchPathsClear(
                     interpreter)));
    LoadSubCommand(
        "insert",
        CommandObjectSP(
            new CommandObjectTargetModulesSearchPathsInsert(interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTargetModulesSearchPathsList(
                    interpreter)));
    LoadSubCommand(
        "query", CommandObjectSP(new CommandObjectTargetModulesSearchPathsQuery(
                     interpreter)));
  }

  ~CommandObjectTargetModulesImageSearchPaths() override = default;
};

#pragma mark CommandObjectTargetModules

// CommandObjectTargetModules

class CommandObjectTargetModules : public CommandObjectMultiword {
public:
  // Constructors and Destructors
  CommandObjectTargetModules(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "target modules",
                               "Commands for accessing information for one or "
                               "more target modules.",
                               "target modules <sub-command> ...") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTargetModulesAdd(interpreter)));
    LoadSubCommand("load", CommandObjectSP(new CommandObjectTargetModulesLoad(
                               interpreter)));
    LoadSubCommand("dump", CommandObjectSP(new CommandObjectTargetModulesDump(
                               interpreter)));
    LoadSubCommand("list", CommandObjectSP(new CommandObjectTargetModulesList(
                               interpreter)));
    LoadSubCommand(
        "lookup",
        CommandObjectSP(new CommandObjectTargetModulesLookup(interpreter)));
    LoadSubCommand(
        "search-paths",
        CommandObjectSP(
            new CommandObjectTargetModulesImageSearchPaths(interpreter)));
    LoadSubCommand(
        "show-unwind",
        CommandObjectSP(new CommandObjectTargetModulesShowUnwind(interpreter)));
  }

  ~CommandObjectTargetModules() override = default;

private:
  // For CommandObjectTargetModules only
  CommandObjectTargetModules(const CommandObjectTargetModules &) = delete;
  const CommandObjectTargetModules &
  operator=(const CommandObjectTargetModules &) = delete;
};

class CommandObjectTargetSymbolsAdd : public CommandObjectParsed {
public:
  CommandObjectTargetSymbolsAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target symbols add",
            "Add a debug symbol file to one of the target's current modules by "
            "specifying a path to a debug symbols file or by using the options "
            "to specify a module.",
            "target symbols add <cmd-options> [<symfile>]",
            eCommandRequiresTarget),
        m_file_option(
            LLDB_OPT_SET_1, false, "shlib", 's', lldb::eModuleCompletion,
            eArgTypeShlibName,
            "Locate the debug symbols for the shared library specified by "
            "name."),
        m_current_frame_option(
            LLDB_OPT_SET_2, false, "frame", 'F',
            "Locate the debug symbols for the currently selected frame.", false,
            true),
        m_current_stack_option(LLDB_OPT_SET_2, false, "stack", 'S',
                               "Locate the debug symbols for every frame in "
                               "the current call stack.",
                               false, true)

  {
    m_option_group.Append(&m_uuid_option_group, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_file_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_current_frame_option, LLDB_OPT_SET_2,
                          LLDB_OPT_SET_2);
    m_option_group.Append(&m_current_stack_option, LLDB_OPT_SET_2,
                          LLDB_OPT_SET_2);
    m_option_group.Finalize();
    AddSimpleArgumentList(eArgTypeFilename);
  }

  ~CommandObjectTargetSymbolsAdd() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  bool AddModuleSymbols(Target *target, ModuleSpec &module_spec, bool &flush,
                        CommandReturnObject &result) {
    const FileSpec &symbol_fspec = module_spec.GetSymbolFileSpec();
    if (!symbol_fspec) {
      result.AppendError(
          "one or more executable image paths must be specified");
      return false;
    }

    char symfile_path[PATH_MAX];
    symbol_fspec.GetPath(symfile_path, sizeof(symfile_path));

    if (!module_spec.GetUUID().IsValid()) {
      if (!module_spec.GetFileSpec() && !module_spec.GetPlatformFileSpec())
        module_spec.GetFileSpec().SetFilename(symbol_fspec.GetFilename());
    }

    // Now module_spec represents a symbol file for a module that might exist
    // in the current target.  Let's find possible matches.
    ModuleList matching_modules;

    // First extract all module specs from the symbol file
    lldb_private::ModuleSpecList symfile_module_specs;
    if (ObjectFile::GetModuleSpecifications(module_spec.GetSymbolFileSpec(),
                                            0, 0, symfile_module_specs)) {
      // Now extract the module spec that matches the target architecture
      ModuleSpec target_arch_module_spec;
      ModuleSpec symfile_module_spec;
      target_arch_module_spec.GetArchitecture() = target->GetArchitecture();
      if (symfile_module_specs.FindMatchingModuleSpec(target_arch_module_spec,
                                                      symfile_module_spec)) {
        if (symfile_module_spec.GetUUID().IsValid()) {
          // It has a UUID, look for this UUID in the target modules
          ModuleSpec symfile_uuid_module_spec;
          symfile_uuid_module_spec.GetUUID() = symfile_module_spec.GetUUID();
          target->GetImages().FindModules(symfile_uuid_module_spec,
                                          matching_modules);
        }
      }

      if (matching_modules.IsEmpty()) {
        // No matches yet.  Iterate through the module specs to find a UUID
        // value that we can match up to an image in our target.
        const size_t num_symfile_module_specs = symfile_module_specs.GetSize();
        for (size_t i = 0;
             i < num_symfile_module_specs && matching_modules.IsEmpty(); ++i) {
          if (symfile_module_specs.GetModuleSpecAtIndex(
                  i, symfile_module_spec)) {
            if (symfile_module_spec.GetUUID().IsValid()) {
              // It has a UUID.  Look for this UUID in the target modules.
              ModuleSpec symfile_uuid_module_spec;
              symfile_uuid_module_spec.GetUUID() =
                  symfile_module_spec.GetUUID();
              target->GetImages().FindModules(symfile_uuid_module_spec,
                                              matching_modules);
            }
          }
        }
      }
    }

    // Just try to match up the file by basename if we have no matches at
    // this point.  For example, module foo might have symbols in foo.debug.
    if (matching_modules.IsEmpty())
      target->GetImages().FindModules(module_spec, matching_modules);

    while (matching_modules.IsEmpty()) {
      ConstString filename_no_extension(
          module_spec.GetFileSpec().GetFileNameStrippingExtension());
      // Empty string returned, let's bail
      if (!filename_no_extension)
        break;

      // Check if there was no extension to strip and the basename is the same
      if (filename_no_extension == module_spec.GetFileSpec().GetFilename())
        break;

      // Replace basename with one fewer extension
      module_spec.GetFileSpec().SetFilename(filename_no_extension);
      target->GetImages().FindModules(module_spec, matching_modules);
    }

    if (matching_modules.GetSize() > 1) {
      result.AppendErrorWithFormat("multiple modules match symbol file '%s', "
                                   "use the --uuid option to resolve the "
                                   "ambiguity.\n",
                                   symfile_path);
      return false;
    }

    if (matching_modules.GetSize() == 1) {
      ModuleSP module_sp(matching_modules.GetModuleAtIndex(0));

      // The module has not yet created its symbol vendor, we can just give
      // the existing target module the symfile path to use for when it
      // decides to create it!
      module_sp->SetSymbolFileFileSpec(symbol_fspec);

      SymbolFile *symbol_file =
          module_sp->GetSymbolFile(true, &result.GetErrorStream());
      if (symbol_file) {
        ObjectFile *object_file = symbol_file->GetObjectFile();
        if (object_file && object_file->GetFileSpec() == symbol_fspec) {
          // Provide feedback that the symfile has been successfully added.
          const FileSpec &module_fs = module_sp->GetFileSpec();
          result.AppendMessageWithFormat(
              "symbol file '%s' has been added to '%s'\n", symfile_path,
              module_fs.GetPath().c_str());

          // Let clients know something changed in the module if it is
          // currently loaded
          ModuleList module_list;
          module_list.Append(module_sp);
          target->SymbolsDidLoad(module_list);

          // Make sure we load any scripting resources that may be embedded
          // in the debug info files in case the platform supports that.
          Status error;
          StreamString feedback_stream;
          module_sp->LoadScriptingResourceInTarget(target, error,
                                                   feedback_stream);
          if (error.Fail() && error.AsCString())
            result.AppendWarningWithFormat(
                "unable to load scripting data for module %s - error "
                "reported was %s",
                module_sp->GetFileSpec()
                    .GetFileNameStrippingExtension()
                    .GetCString(),
                error.AsCString());
          else if (feedback_stream.GetSize())
            result.AppendWarning(feedback_stream.GetData());

          flush = true;
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      // Clear the symbol file spec if anything went wrong
      module_sp->SetSymbolFileFileSpec(FileSpec());
    }

    StreamString ss_symfile_uuid;
    if (module_spec.GetUUID().IsValid()) {
      ss_symfile_uuid << " (";
      module_spec.GetUUID().Dump(ss_symfile_uuid);
      ss_symfile_uuid << ')';
    }
    result.AppendErrorWithFormat(
        "symbol file '%s'%s does not match any existing module%s\n",
        symfile_path, ss_symfile_uuid.GetData(),
        !llvm::sys::fs::is_regular_file(symbol_fspec.GetPath())
            ? "\n       please specify the full path to the symbol file"
            : "");
    return false;
  }

  bool DownloadObjectAndSymbolFile(ModuleSpec &module_spec,
                                   CommandReturnObject &result, bool &flush) {
    Status error;
    if (PluginManager::DownloadObjectAndSymbolFile(module_spec, error)) {
      if (module_spec.GetSymbolFileSpec())
        return AddModuleSymbols(m_exe_ctx.GetTargetPtr(), module_spec, flush,
                                result);
    } else {
      result.SetError(error);
    }
    return false;
  }

  bool AddSymbolsForUUID(CommandReturnObject &result, bool &flush) {
    assert(m_uuid_option_group.GetOptionValue().OptionWasSet());

    ModuleSpec module_spec;
    module_spec.GetUUID() =
        m_uuid_option_group.GetOptionValue().GetCurrentValue();

    if (!DownloadObjectAndSymbolFile(module_spec, result, flush)) {
      StreamString error_strm;
      error_strm.PutCString("unable to find debug symbols for UUID ");
      module_spec.GetUUID().Dump(error_strm);
      result.AppendError(error_strm.GetString());
      return false;
    }

    return true;
  }

  bool AddSymbolsForFile(CommandReturnObject &result, bool &flush) {
    assert(m_file_option.GetOptionValue().OptionWasSet());

    ModuleSpec module_spec;
    module_spec.GetFileSpec() =
        m_file_option.GetOptionValue().GetCurrentValue();

    Target *target = m_exe_ctx.GetTargetPtr();
    ModuleSP module_sp(target->GetImages().FindFirstModule(module_spec));
    if (module_sp) {
      module_spec.GetFileSpec() = module_sp->GetFileSpec();
      module_spec.GetPlatformFileSpec() = module_sp->GetPlatformFileSpec();
      module_spec.GetUUID() = module_sp->GetUUID();
      module_spec.GetArchitecture() = module_sp->GetArchitecture();
    } else {
      module_spec.GetArchitecture() = target->GetArchitecture();
    }

    if (!DownloadObjectAndSymbolFile(module_spec, result, flush)) {
      StreamString error_strm;
      error_strm.PutCString(
          "unable to find debug symbols for the executable file ");
      error_strm << module_spec.GetFileSpec();
      result.AppendError(error_strm.GetString());
      return false;
    }

    return true;
  }

  bool AddSymbolsForFrame(CommandReturnObject &result, bool &flush) {
    assert(m_current_frame_option.GetOptionValue().OptionWasSet());

    Process *process = m_exe_ctx.GetProcessPtr();
    if (!process) {
      result.AppendError(
          "a process must exist in order to use the --frame option");
      return false;
    }

    const StateType process_state = process->GetState();
    if (!StateIsStoppedState(process_state, true)) {
      result.AppendErrorWithFormat("process is not stopped: %s",
                                   StateAsCString(process_state));
      return false;
    }

    StackFrame *frame = m_exe_ctx.GetFramePtr();
    if (!frame) {
      result.AppendError("invalid current frame");
      return false;
    }

    ModuleSP frame_module_sp(
        frame->GetSymbolContext(eSymbolContextModule).module_sp);
    if (!frame_module_sp) {
      result.AppendError("frame has no module");
      return false;
    }

    ModuleSpec module_spec;
    module_spec.GetUUID() = frame_module_sp->GetUUID();
    module_spec.GetArchitecture() = frame_module_sp->GetArchitecture();
    module_spec.GetFileSpec() = frame_module_sp->GetPlatformFileSpec();

    if (!DownloadObjectAndSymbolFile(module_spec, result, flush)) {
      result.AppendError("unable to find debug symbols for the current frame");
      return false;
    }

    return true;
  }

  bool AddSymbolsForStack(CommandReturnObject &result, bool &flush) {
    assert(m_current_stack_option.GetOptionValue().OptionWasSet());

    Process *process = m_exe_ctx.GetProcessPtr();
    if (!process) {
      result.AppendError(
          "a process must exist in order to use the --stack option");
      return false;
    }

    const StateType process_state = process->GetState();
    if (!StateIsStoppedState(process_state, true)) {
      result.AppendErrorWithFormat("process is not stopped: %s",
                                   StateAsCString(process_state));
      return false;
    }

    Thread *thread = m_exe_ctx.GetThreadPtr();
    if (!thread) {
      result.AppendError("invalid current thread");
      return false;
    }

    bool symbols_found = false;
    uint32_t frame_count = thread->GetStackFrameCount();
    for (uint32_t i = 0; i < frame_count; ++i) {
      lldb::StackFrameSP frame_sp = thread->GetStackFrameAtIndex(i);

      ModuleSP frame_module_sp(
          frame_sp->GetSymbolContext(eSymbolContextModule).module_sp);
      if (!frame_module_sp)
        continue;

      ModuleSpec module_spec;
      module_spec.GetUUID() = frame_module_sp->GetUUID();
      module_spec.GetFileSpec() = frame_module_sp->GetPlatformFileSpec();
      module_spec.GetArchitecture() = frame_module_sp->GetArchitecture();

      bool current_frame_flush = false;
      if (DownloadObjectAndSymbolFile(module_spec, result, current_frame_flush))
        symbols_found = true;
      flush |= current_frame_flush;
    }

    if (!symbols_found) {
      result.AppendError(
          "unable to find debug symbols in the current call stack");
      return false;
    }

    return true;
  }

  void DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    result.SetStatus(eReturnStatusFailed);
    bool flush = false;
    ModuleSpec module_spec;
    const bool uuid_option_set =
        m_uuid_option_group.GetOptionValue().OptionWasSet();
    const bool file_option_set = m_file_option.GetOptionValue().OptionWasSet();
    const bool frame_option_set =
        m_current_frame_option.GetOptionValue().OptionWasSet();
    const bool stack_option_set =
        m_current_stack_option.GetOptionValue().OptionWasSet();
    const size_t argc = args.GetArgumentCount();

    if (argc == 0) {
      if (uuid_option_set)
        AddSymbolsForUUID(result, flush);
      else if (file_option_set)
        AddSymbolsForFile(result, flush);
      else if (frame_option_set)
        AddSymbolsForFrame(result, flush);
      else if (stack_option_set)
        AddSymbolsForStack(result, flush);
      else
        result.AppendError("one or more symbol file paths must be specified, "
                           "or options must be specified");
    } else {
      if (uuid_option_set) {
        result.AppendError("specify either one or more paths to symbol files "
                           "or use the --uuid option without arguments");
      } else if (frame_option_set) {
        result.AppendError("specify either one or more paths to symbol files "
                           "or use the --frame option without arguments");
      } else if (file_option_set && argc > 1) {
        result.AppendError("specify at most one symbol file path when "
                           "--shlib option is set");
      } else {
        PlatformSP platform_sp(target->GetPlatform());

        for (auto &entry : args.entries()) {
          if (!entry.ref().empty()) {
            auto &symbol_file_spec = module_spec.GetSymbolFileSpec();
            symbol_file_spec.SetFile(entry.ref(), FileSpec::Style::native);
            FileSystem::Instance().Resolve(symbol_file_spec);
            if (file_option_set) {
              module_spec.GetFileSpec() =
                  m_file_option.GetOptionValue().GetCurrentValue();
            }
            if (platform_sp) {
              FileSpec symfile_spec;
              if (platform_sp
                      ->ResolveSymbolFile(*target, module_spec, symfile_spec)
                      .Success())
                module_spec.GetSymbolFileSpec() = symfile_spec;
            }

            bool symfile_exists =
                FileSystem::Instance().Exists(module_spec.GetSymbolFileSpec());

            if (symfile_exists) {
              if (!AddModuleSymbols(target, module_spec, flush, result))
                break;
            } else {
              std::string resolved_symfile_path =
                  module_spec.GetSymbolFileSpec().GetPath();
              if (resolved_symfile_path != entry.ref()) {
                result.AppendErrorWithFormat(
                    "invalid module path '%s' with resolved path '%s'\n",
                    entry.c_str(), resolved_symfile_path.c_str());
                break;
              }
              result.AppendErrorWithFormat("invalid module path '%s'\n",
                                           entry.c_str());
              break;
            }
          }
        }
      }
    }

    if (flush) {
      Process *process = m_exe_ctx.GetProcessPtr();
      if (process)
        process->Flush();
    }
  }

  OptionGroupOptions m_option_group;
  OptionGroupUUID m_uuid_option_group;
  OptionGroupFile m_file_option;
  OptionGroupBoolean m_current_frame_option;
  OptionGroupBoolean m_current_stack_option;
};

#pragma mark CommandObjectTargetSymbols

// CommandObjectTargetSymbols

class CommandObjectTargetSymbols : public CommandObjectMultiword {
public:
  // Constructors and Destructors
  CommandObjectTargetSymbols(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "target symbols",
            "Commands for adding and managing debug symbol files.",
            "target symbols <sub-command> ...") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTargetSymbolsAdd(interpreter)));
  }

  ~CommandObjectTargetSymbols() override = default;

private:
  // For CommandObjectTargetModules only
  CommandObjectTargetSymbols(const CommandObjectTargetSymbols &) = delete;
  const CommandObjectTargetSymbols &
  operator=(const CommandObjectTargetSymbols &) = delete;
};

#pragma mark CommandObjectTargetStopHookAdd

// CommandObjectTargetStopHookAdd
#define LLDB_OPTIONS_target_stop_hook_add
#include "CommandOptions.inc"

class CommandObjectTargetStopHookAdd : public CommandObjectParsed,
                                       public IOHandlerDelegateMultiline {
public:
  class CommandOptions : public OptionGroup {
  public:
    CommandOptions() : m_line_end(UINT_MAX) {}

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_target_stop_hook_add_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option =
          g_target_stop_hook_add_options[option_idx].short_option;

      switch (short_option) {
      case 'c':
        m_class_name = std::string(option_arg);
        m_sym_ctx_specified = true;
        break;

      case 'e':
        if (option_arg.getAsInteger(0, m_line_end)) {
          error.SetErrorStringWithFormat("invalid end line number: \"%s\"",
                                         option_arg.str().c_str());
          break;
        }
        m_sym_ctx_specified = true;
        break;

      case 'G': {
        bool value, success;
        value = OptionArgParser::ToBoolean(option_arg, false, &success);
        if (success) {
          m_auto_continue = value;
        } else
          error.SetErrorStringWithFormat(
              "invalid boolean value '%s' passed for -G option",
              option_arg.str().c_str());
      } break;
      case 'l':
        if (option_arg.getAsInteger(0, m_line_start)) {
          error.SetErrorStringWithFormat("invalid start line number: \"%s\"",
                                         option_arg.str().c_str());
          break;
        }
        m_sym_ctx_specified = true;
        break;

      case 'i':
        m_no_inlines = true;
        break;

      case 'n':
        m_function_name = std::string(option_arg);
        m_func_name_type_mask |= eFunctionNameTypeAuto;
        m_sym_ctx_specified = true;
        break;

      case 'f':
        m_file_name = std::string(option_arg);
        m_sym_ctx_specified = true;
        break;

      case 's':
        m_module_name = std::string(option_arg);
        m_sym_ctx_specified = true;
        break;

      case 't':
        if (option_arg.getAsInteger(0, m_thread_id))
          error.SetErrorStringWithFormat("invalid thread id string '%s'",
                                         option_arg.str().c_str());
        m_thread_specified = true;
        break;

      case 'T':
        m_thread_name = std::string(option_arg);
        m_thread_specified = true;
        break;

      case 'q':
        m_queue_name = std::string(option_arg);
        m_thread_specified = true;
        break;

      case 'x':
        if (option_arg.getAsInteger(0, m_thread_index))
          error.SetErrorStringWithFormat("invalid thread index string '%s'",
                                         option_arg.str().c_str());
        m_thread_specified = true;
        break;

      case 'o':
        m_use_one_liner = true;
        m_one_liner.push_back(std::string(option_arg));
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_class_name.clear();
      m_function_name.clear();
      m_line_start = 0;
      m_line_end = LLDB_INVALID_LINE_NUMBER;
      m_file_name.clear();
      m_module_name.clear();
      m_func_name_type_mask = eFunctionNameTypeAuto;
      m_thread_id = LLDB_INVALID_THREAD_ID;
      m_thread_index = UINT32_MAX;
      m_thread_name.clear();
      m_queue_name.clear();

      m_no_inlines = false;
      m_sym_ctx_specified = false;
      m_thread_specified = false;

      m_use_one_liner = false;
      m_one_liner.clear();
      m_auto_continue = false;
    }

    std::string m_class_name;
    std::string m_function_name;
    uint32_t m_line_start = 0;
    uint32_t m_line_end = LLDB_INVALID_LINE_NUMBER;
    std::string m_file_name;
    std::string m_module_name;
    uint32_t m_func_name_type_mask =
        eFunctionNameTypeAuto; // A pick from lldb::FunctionNameType.
    lldb::tid_t m_thread_id = LLDB_INVALID_THREAD_ID;
    uint32_t m_thread_index = UINT32_MAX;
    std::string m_thread_name;
    std::string m_queue_name;
    bool m_sym_ctx_specified = false;
    bool m_no_inlines = false;
    bool m_thread_specified = false;
    // Instance variables to hold the values for one_liner options.
    bool m_use_one_liner = false;
    std::vector<std::string> m_one_liner;

    bool m_auto_continue = false;
  };

  CommandObjectTargetStopHookAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target stop-hook add",
                            "Add a hook to be executed when the target stops."
                            "The hook can either be a list of commands or an "
                            "appropriately defined Python class.  You can also "
                            "add filters so the hook only runs a certain stop "
                            "points.",
                            "target stop-hook add"),
        IOHandlerDelegateMultiline("DONE",
                                   IOHandlerDelegate::Completion::LLDBCommand),
        m_python_class_options("scripted stop-hook", true, 'P') {
    SetHelpLong(
        R"(
Command Based stop-hooks:
-------------------------
  Stop hooks can run a list of lldb commands by providing one or more
  --one-line-command options.  The commands will get run in the order they are
  added.  Or you can provide no commands, in which case you will enter a
  command editor where you can enter the commands to be run.

Python Based Stop Hooks:
------------------------
  Stop hooks can be implemented with a suitably defined Python class, whose name
  is passed in the --python-class option.

  When the stop hook is added, the class is initialized by calling:

    def __init__(self, target, extra_args, internal_dict):

    target: The target that the stop hook is being added to.
    extra_args: An SBStructuredData Dictionary filled with the -key -value
                option pairs passed to the command.
    dict: An implementation detail provided by lldb.

  Then when the stop-hook triggers, lldb will run the 'handle_stop' method.
  The method has the signature:

    def handle_stop(self, exe_ctx, stream):

    exe_ctx: An SBExecutionContext for the thread that has stopped.
    stream: An SBStream, anything written to this stream will be printed in the
            the stop message when the process stops.

    Return Value: The method returns "should_stop".  If should_stop is false
                  from all the stop hook executions on threads that stopped
                  with a reason, then the process will continue.  Note that this
                  will happen only after all the stop hooks are run.

Filter Options:
---------------
  Stop hooks can be set to always run, or to only run when the stopped thread
  matches the filter options passed on the command line.  The available filter
  options include a shared library or a thread or queue specification,
  a line range in a source file, a function name or a class name.
            )");
    m_all_options.Append(&m_python_class_options,
                         LLDB_OPT_SET_1 | LLDB_OPT_SET_2,
                         LLDB_OPT_SET_FROM_TO(4, 6));
    m_all_options.Append(&m_options);
    m_all_options.Finalize();
  }

  ~CommandObjectTargetStopHookAdd() override = default;

  Options *GetOptions() override { return &m_all_options; }

protected:
  void IOHandlerActivated(IOHandler &io_handler, bool interactive) override {
    StreamFileSP output_sp(io_handler.GetOutputStreamFileSP());
    if (output_sp && interactive) {
      output_sp->PutCString(
          "Enter your stop hook command(s).  Type 'DONE' to end.\n");
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &line) override {
    if (m_stop_hook_sp) {
      if (line.empty()) {
        StreamFileSP error_sp(io_handler.GetErrorStreamFileSP());
        if (error_sp) {
          error_sp->Printf("error: stop hook #%" PRIu64
                           " aborted, no commands.\n",
                           m_stop_hook_sp->GetID());
          error_sp->Flush();
        }
        Target *target = GetDebugger().GetSelectedTarget().get();
        if (target) {
          target->UndoCreateStopHook(m_stop_hook_sp->GetID());
        }
      } else {
        // The IOHandler editor is only for command lines stop hooks:
        Target::StopHookCommandLine *hook_ptr =
            static_cast<Target::StopHookCommandLine *>(m_stop_hook_sp.get());

        hook_ptr->SetActionFromString(line);
        StreamFileSP output_sp(io_handler.GetOutputStreamFileSP());
        if (output_sp) {
          output_sp->Printf("Stop hook #%" PRIu64 " added.\n",
                            m_stop_hook_sp->GetID());
          output_sp->Flush();
        }
      }
      m_stop_hook_sp.reset();
    }
    io_handler.SetIsDone(true);
  }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    m_stop_hook_sp.reset();

    Target &target = GetSelectedOrDummyTarget();
    Target::StopHookSP new_hook_sp =
        target.CreateStopHook(m_python_class_options.GetName().empty() ?
                               Target::StopHook::StopHookKind::CommandBased
                               : Target::StopHook::StopHookKind::ScriptBased);

    //  First step, make the specifier.
    std::unique_ptr<SymbolContextSpecifier> specifier_up;
    if (m_options.m_sym_ctx_specified) {
      specifier_up = std::make_unique<SymbolContextSpecifier>(
          GetDebugger().GetSelectedTarget());

      if (!m_options.m_module_name.empty()) {
        specifier_up->AddSpecification(
            m_options.m_module_name.c_str(),
            SymbolContextSpecifier::eModuleSpecified);
      }

      if (!m_options.m_class_name.empty()) {
        specifier_up->AddSpecification(
            m_options.m_class_name.c_str(),
            SymbolContextSpecifier::eClassOrNamespaceSpecified);
      }

      if (!m_options.m_file_name.empty()) {
        specifier_up->AddSpecification(m_options.m_file_name.c_str(),
                                       SymbolContextSpecifier::eFileSpecified);
      }

      if (m_options.m_line_start != 0) {
        specifier_up->AddLineSpecification(
            m_options.m_line_start,
            SymbolContextSpecifier::eLineStartSpecified);
      }

      if (m_options.m_line_end != UINT_MAX) {
        specifier_up->AddLineSpecification(
            m_options.m_line_end, SymbolContextSpecifier::eLineEndSpecified);
      }

      if (!m_options.m_function_name.empty()) {
        specifier_up->AddSpecification(
            m_options.m_function_name.c_str(),
            SymbolContextSpecifier::eFunctionSpecified);
      }
    }

    if (specifier_up)
      new_hook_sp->SetSpecifier(specifier_up.release());

    // Next see if any of the thread options have been entered:

    if (m_options.m_thread_specified) {
      ThreadSpec *thread_spec = new ThreadSpec();

      if (m_options.m_thread_id != LLDB_INVALID_THREAD_ID) {
        thread_spec->SetTID(m_options.m_thread_id);
      }

      if (m_options.m_thread_index != UINT32_MAX)
        thread_spec->SetIndex(m_options.m_thread_index);

      if (!m_options.m_thread_name.empty())
        thread_spec->SetName(m_options.m_thread_name.c_str());

      if (!m_options.m_queue_name.empty())
        thread_spec->SetQueueName(m_options.m_queue_name.c_str());

      new_hook_sp->SetThreadSpecifier(thread_spec);
    }

    new_hook_sp->SetAutoContinue(m_options.m_auto_continue);
    if (m_options.m_use_one_liner) {
      // This is a command line stop hook:
      Target::StopHookCommandLine *hook_ptr =
          static_cast<Target::StopHookCommandLine *>(new_hook_sp.get());
      hook_ptr->SetActionFromStrings(m_options.m_one_liner);
      result.AppendMessageWithFormat("Stop hook #%" PRIu64 " added.\n",
                                     new_hook_sp->GetID());
    } else if (!m_python_class_options.GetName().empty()) {
      // This is a scripted stop hook:
      Target::StopHookScripted *hook_ptr =
          static_cast<Target::StopHookScripted *>(new_hook_sp.get());
      Status error = hook_ptr->SetScriptCallback(
          m_python_class_options.GetName(),
          m_python_class_options.GetStructuredData());
      if (error.Success())
        result.AppendMessageWithFormat("Stop hook #%" PRIu64 " added.\n",
                                       new_hook_sp->GetID());
      else {
        // FIXME: Set the stop hook ID counter back.
        result.AppendErrorWithFormat("Couldn't add stop hook: %s",
                                     error.AsCString());
        target.UndoCreateStopHook(new_hook_sp->GetID());
        return;
      }
    } else {
      m_stop_hook_sp = new_hook_sp;
      m_interpreter.GetLLDBCommandsFromIOHandler("> ",   // Prompt
                                                 *this); // IOHandlerDelegate
    }
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }

private:
  CommandOptions m_options;
  OptionGroupPythonClassWithDict m_python_class_options;
  OptionGroupOptions m_all_options;

  Target::StopHookSP m_stop_hook_sp;
};

#pragma mark CommandObjectTargetStopHookDelete

// CommandObjectTargetStopHookDelete

class CommandObjectTargetStopHookDelete : public CommandObjectParsed {
public:
  CommandObjectTargetStopHookDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target stop-hook delete",
                            "Delete a stop-hook.",
                            "target stop-hook delete [<idx>]") {
    AddSimpleArgumentList(eArgTypeStopHookID, eArgRepeatStar);
  }

  ~CommandObjectTargetStopHookDelete() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex())
      return;
    CommandObject::HandleArgumentCompletion(request, opt_element_vector);
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target &target = GetSelectedOrDummyTarget();
    // FIXME: see if we can use the breakpoint id style parser?
    size_t num_args = command.GetArgumentCount();
    if (num_args == 0) {
      if (!m_interpreter.Confirm("Delete all stop hooks?", true)) {
        result.SetStatus(eReturnStatusFailed);
        return;
      } else {
        target.RemoveAllStopHooks();
      }
    } else {
      for (size_t i = 0; i < num_args; i++) {
        lldb::user_id_t user_id;
        if (!llvm::to_integer(command.GetArgumentAtIndex(i), user_id)) {
          result.AppendErrorWithFormat("invalid stop hook id: \"%s\".\n",
                                       command.GetArgumentAtIndex(i));
          return;
        }
        if (!target.RemoveStopHookByID(user_id)) {
          result.AppendErrorWithFormat("unknown stop hook id: \"%s\".\n",
                                       command.GetArgumentAtIndex(i));
          return;
        }
      }
    }
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }
};

#pragma mark CommandObjectTargetStopHookEnableDisable

// CommandObjectTargetStopHookEnableDisable

class CommandObjectTargetStopHookEnableDisable : public CommandObjectParsed {
public:
  CommandObjectTargetStopHookEnableDisable(CommandInterpreter &interpreter,
                                           bool enable, const char *name,
                                           const char *help, const char *syntax)
      : CommandObjectParsed(interpreter, name, help, syntax), m_enable(enable) {
    AddSimpleArgumentList(eArgTypeStopHookID, eArgRepeatStar);
  }

  ~CommandObjectTargetStopHookEnableDisable() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex())
      return;
    CommandObject::HandleArgumentCompletion(request, opt_element_vector);
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target &target = GetSelectedOrDummyTarget();
    // FIXME: see if we can use the breakpoint id style parser?
    size_t num_args = command.GetArgumentCount();
    bool success;

    if (num_args == 0) {
      target.SetAllStopHooksActiveState(m_enable);
    } else {
      for (size_t i = 0; i < num_args; i++) {
        lldb::user_id_t user_id;
        if (!llvm::to_integer(command.GetArgumentAtIndex(i), user_id)) {
          result.AppendErrorWithFormat("invalid stop hook id: \"%s\".\n",
                                       command.GetArgumentAtIndex(i));
          return;
        }
        success = target.SetStopHookActiveStateByID(user_id, m_enable);
        if (!success) {
          result.AppendErrorWithFormat("unknown stop hook id: \"%s\".\n",
                                       command.GetArgumentAtIndex(i));
          return;
        }
      }
    }
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }

private:
  bool m_enable;
};

#pragma mark CommandObjectTargetStopHookList

// CommandObjectTargetStopHookList

class CommandObjectTargetStopHookList : public CommandObjectParsed {
public:
  CommandObjectTargetStopHookList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target stop-hook list",
                            "List all stop-hooks.", "target stop-hook list") {}

  ~CommandObjectTargetStopHookList() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target &target = GetSelectedOrDummyTarget();

    size_t num_hooks = target.GetNumStopHooks();
    if (num_hooks == 0) {
      result.GetOutputStream().PutCString("No stop hooks.\n");
    } else {
      for (size_t i = 0; i < num_hooks; i++) {
        Target::StopHookSP this_hook = target.GetStopHookAtIndex(i);
        if (i > 0)
          result.GetOutputStream().PutCString("\n");
        this_hook->GetDescription(result.GetOutputStream(),
                                  eDescriptionLevelFull);
      }
    }
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectMultiwordTargetStopHooks

// CommandObjectMultiwordTargetStopHooks

class CommandObjectMultiwordTargetStopHooks : public CommandObjectMultiword {
public:
  CommandObjectMultiwordTargetStopHooks(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "target stop-hook",
            "Commands for operating on debugger target stop-hooks.",
            "target stop-hook <subcommand> [<subcommand-options>]") {
    LoadSubCommand("add", CommandObjectSP(
                              new CommandObjectTargetStopHookAdd(interpreter)));
    LoadSubCommand(
        "delete",
        CommandObjectSP(new CommandObjectTargetStopHookDelete(interpreter)));
    LoadSubCommand("disable",
                   CommandObjectSP(new CommandObjectTargetStopHookEnableDisable(
                       interpreter, false, "target stop-hook disable [<id>]",
                       "Disable a stop-hook.", "target stop-hook disable")));
    LoadSubCommand("enable",
                   CommandObjectSP(new CommandObjectTargetStopHookEnableDisable(
                       interpreter, true, "target stop-hook enable [<id>]",
                       "Enable a stop-hook.", "target stop-hook enable")));
    LoadSubCommand("list", CommandObjectSP(new CommandObjectTargetStopHookList(
                               interpreter)));
  }

  ~CommandObjectMultiwordTargetStopHooks() override = default;
};

#pragma mark CommandObjectTargetDumpTypesystem

/// Dumps the TypeSystem of the selected Target.
class CommandObjectTargetDumpTypesystem : public CommandObjectParsed {
public:
  CommandObjectTargetDumpTypesystem(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target dump typesystem",
            "Dump the state of the target's internal type system. Intended to "
            "be used for debugging LLDB itself.",
            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectTargetDumpTypesystem() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    // Go over every scratch TypeSystem and dump to the command output.
    for (lldb::TypeSystemSP ts : GetSelectedTarget().GetScratchTypeSystems())
      if (ts)
        ts->Dump(result.GetOutputStream().AsRawOstream());

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectTargetDumpSectionLoadList

/// Dumps the SectionLoadList of the selected Target.
class CommandObjectTargetDumpSectionLoadList : public CommandObjectParsed {
public:
  CommandObjectTargetDumpSectionLoadList(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target dump section-load-list",
            "Dump the state of the target's internal section load list. "
            "Intended to be used for debugging LLDB itself.",
            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectTargetDumpSectionLoadList() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target &target = GetSelectedTarget();
    target.GetSectionLoadList().Dump(result.GetOutputStream(), &target);
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectTargetDump

/// Multi-word command for 'target dump'.
class CommandObjectTargetDump : public CommandObjectMultiword {
public:
  // Constructors and Destructors
  CommandObjectTargetDump(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "target dump",
            "Commands for dumping information about the target.",
            "target dump [typesystem|section-load-list]") {
    LoadSubCommand(
        "typesystem",
        CommandObjectSP(new CommandObjectTargetDumpTypesystem(interpreter)));
    LoadSubCommand("section-load-list",
                   CommandObjectSP(new CommandObjectTargetDumpSectionLoadList(
                       interpreter)));
  }

  ~CommandObjectTargetDump() override = default;
};

#pragma mark CommandObjectMultiwordTarget

// CommandObjectMultiwordTarget

CommandObjectMultiwordTarget::CommandObjectMultiwordTarget(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "target",
                             "Commands for operating on debugger targets.",
                             "target <subcommand> [<subcommand-options>]") {
  LoadSubCommand("create",
                 CommandObjectSP(new CommandObjectTargetCreate(interpreter)));
  LoadSubCommand("delete",
                 CommandObjectSP(new CommandObjectTargetDelete(interpreter)));
  LoadSubCommand("dump",
                 CommandObjectSP(new CommandObjectTargetDump(interpreter)));
  LoadSubCommand("list",
                 CommandObjectSP(new CommandObjectTargetList(interpreter)));
  LoadSubCommand("select",
                 CommandObjectSP(new CommandObjectTargetSelect(interpreter)));
  LoadSubCommand("show-launch-environment",
                 CommandObjectSP(new CommandObjectTargetShowLaunchEnvironment(
                     interpreter)));
  LoadSubCommand(
      "stop-hook",
      CommandObjectSP(new CommandObjectMultiwordTargetStopHooks(interpreter)));
  LoadSubCommand("modules",
                 CommandObjectSP(new CommandObjectTargetModules(interpreter)));
  LoadSubCommand("symbols",
                 CommandObjectSP(new CommandObjectTargetSymbols(interpreter)));
  LoadSubCommand("variable",
                 CommandObjectSP(new CommandObjectTargetVariable(interpreter)));
}

CommandObjectMultiwordTarget::~CommandObjectMultiwordTarget() = default;
