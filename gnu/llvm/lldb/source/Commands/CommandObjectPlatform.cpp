//===-- CommandObjectPlatform.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectPlatform.h"
#include "CommandOptionsProcessAttach.h"
#include "CommandOptionsProcessLaunch.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandOptionValidators.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionGroupFile.h"
#include "lldb/Interpreter/OptionGroupPlatform.h"
#include "lldb/Interpreter/OptionGroupPythonClassWithDict.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/ScriptedMetadata.h"
#include "lldb/Utility/State.h"

#include "llvm/ADT/SmallString.h"

using namespace lldb;
using namespace lldb_private;

static mode_t ParsePermissionString(const char *) = delete;

static mode_t ParsePermissionString(llvm::StringRef permissions) {
  if (permissions.size() != 9)
    return (mode_t)(-1);
  bool user_r, user_w, user_x, group_r, group_w, group_x, world_r, world_w,
      world_x;

  user_r = (permissions[0] == 'r');
  user_w = (permissions[1] == 'w');
  user_x = (permissions[2] == 'x');

  group_r = (permissions[3] == 'r');
  group_w = (permissions[4] == 'w');
  group_x = (permissions[5] == 'x');

  world_r = (permissions[6] == 'r');
  world_w = (permissions[7] == 'w');
  world_x = (permissions[8] == 'x');

  mode_t user, group, world;
  user = (user_r ? 4 : 0) | (user_w ? 2 : 0) | (user_x ? 1 : 0);
  group = (group_r ? 4 : 0) | (group_w ? 2 : 0) | (group_x ? 1 : 0);
  world = (world_r ? 4 : 0) | (world_w ? 2 : 0) | (world_x ? 1 : 0);

  return user | group | world;
}

#define LLDB_OPTIONS_permissions
#include "CommandOptions.inc"

class OptionPermissions : public OptionGroup {
public:
  OptionPermissions() = default;

  ~OptionPermissions() override = default;

  lldb_private::Status
  SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                 ExecutionContext *execution_context) override {
    Status error;
    char short_option = (char)GetDefinitions()[option_idx].short_option;
    switch (short_option) {
    case 'v': {
      if (option_arg.getAsInteger(8, m_permissions)) {
        m_permissions = 0777;
        error.SetErrorStringWithFormat("invalid value for permissions: %s",
                                       option_arg.str().c_str());
      }

    } break;
    case 's': {
      mode_t perms = ParsePermissionString(option_arg);
      if (perms == (mode_t)-1)
        error.SetErrorStringWithFormat("invalid value for permissions: %s",
                                       option_arg.str().c_str());
      else
        m_permissions = perms;
    } break;
    case 'r':
      m_permissions |= lldb::eFilePermissionsUserRead;
      break;
    case 'w':
      m_permissions |= lldb::eFilePermissionsUserWrite;
      break;
    case 'x':
      m_permissions |= lldb::eFilePermissionsUserExecute;
      break;
    case 'R':
      m_permissions |= lldb::eFilePermissionsGroupRead;
      break;
    case 'W':
      m_permissions |= lldb::eFilePermissionsGroupWrite;
      break;
    case 'X':
      m_permissions |= lldb::eFilePermissionsGroupExecute;
      break;
    case 'd':
      m_permissions |= lldb::eFilePermissionsWorldRead;
      break;
    case 't':
      m_permissions |= lldb::eFilePermissionsWorldWrite;
      break;
    case 'e':
      m_permissions |= lldb::eFilePermissionsWorldExecute;
      break;
    default:
      llvm_unreachable("Unimplemented option");
    }

    return error;
  }

  void OptionParsingStarting(ExecutionContext *execution_context) override {
    m_permissions = 0;
  }

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    return llvm::ArrayRef(g_permissions_options);
  }

  // Instance variables to hold the values for command options.

  uint32_t m_permissions;

private:
  OptionPermissions(const OptionPermissions &) = delete;
  const OptionPermissions &operator=(const OptionPermissions &) = delete;
};

// "platform select <platform-name>"
class CommandObjectPlatformSelect : public CommandObjectParsed {
public:
  CommandObjectPlatformSelect(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform select",
                            "Create a platform if needed and select it as the "
                            "current platform.",
                            "platform select <platform-name>", 0),
        m_platform_options(
            false) // Don't include the "--platform" option by passing false
  {
    m_option_group.Append(&m_platform_options, LLDB_OPT_SET_ALL, 1);
    m_option_group.Finalize();
    AddSimpleArgumentList(eArgTypePlatform);
  }

  ~CommandObjectPlatformSelect() override = default;

  void HandleCompletion(CompletionRequest &request) override {
    lldb_private::CommandCompletions::PlatformPluginNames(
        GetCommandInterpreter(), request, nullptr);
  }

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    if (args.GetArgumentCount() == 1) {
      const char *platform_name = args.GetArgumentAtIndex(0);
      if (platform_name && platform_name[0]) {
        const bool select = true;
        m_platform_options.SetPlatformName(platform_name);
        Status error;
        ArchSpec platform_arch;
        PlatformSP platform_sp(m_platform_options.CreatePlatformWithOptions(
            m_interpreter, ArchSpec(), select, error, platform_arch));
        if (platform_sp) {
          GetDebugger().GetPlatformList().SetSelectedPlatform(platform_sp);

          platform_sp->GetStatus(result.GetOutputStream());
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendError(error.AsCString());
        }
      } else {
        result.AppendError("invalid platform name");
      }
    } else {
      result.AppendError(
          "platform create takes a platform name as an argument\n");
    }
  }

  OptionGroupOptions m_option_group;
  OptionGroupPlatform m_platform_options;
};

// "platform list"
class CommandObjectPlatformList : public CommandObjectParsed {
public:
  CommandObjectPlatformList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform list",
                            "List all platforms that are available.", nullptr,
                            0) {}

  ~CommandObjectPlatformList() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Stream &ostrm = result.GetOutputStream();
    ostrm.Printf("Available platforms:\n");

    PlatformSP host_platform_sp(Platform::GetHostPlatform());
    ostrm.Format("{0}: {1}\n", host_platform_sp->GetPluginName(),
                 host_platform_sp->GetDescription());

    uint32_t idx;
    for (idx = 0; true; ++idx) {
      llvm::StringRef plugin_name =
          PluginManager::GetPlatformPluginNameAtIndex(idx);
      if (plugin_name.empty())
        break;
      llvm::StringRef plugin_desc =
          PluginManager::GetPlatformPluginDescriptionAtIndex(idx);
      ostrm.Format("{0}: {1}\n", plugin_name, plugin_desc);
    }

    if (idx == 0) {
      result.AppendError("no platforms are available\n");
    } else
      result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// "platform status"
class CommandObjectPlatformStatus : public CommandObjectParsed {
public:
  CommandObjectPlatformStatus(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform status",
                            "Display status for the current platform.", nullptr,
                            0) {}

  ~CommandObjectPlatformStatus() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Stream &ostrm = result.GetOutputStream();

    Target *target = GetDebugger().GetSelectedTarget().get();
    PlatformSP platform_sp;
    if (target) {
      platform_sp = target->GetPlatform();
    }
    if (!platform_sp) {
      platform_sp = GetDebugger().GetPlatformList().GetSelectedPlatform();
    }
    if (platform_sp) {
      platform_sp->GetStatus(ostrm);
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("no platform is currently selected\n");
    }
  }
};

// "platform connect <connect-url>"
class CommandObjectPlatformConnect : public CommandObjectParsed {
public:
  CommandObjectPlatformConnect(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "platform connect",
            "Select the current platform by providing a connection URL.",
            "platform connect <connect-url>", 0) {
    AddSimpleArgumentList(eArgTypeConnectURL);
  }

  ~CommandObjectPlatformConnect() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Stream &ostrm = result.GetOutputStream();

    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      Status error(platform_sp->ConnectRemote(args));
      if (error.Success()) {
        platform_sp->GetStatus(ostrm);
        result.SetStatus(eReturnStatusSuccessFinishResult);

        platform_sp->ConnectToWaitingProcesses(GetDebugger(), error);
        if (error.Fail()) {
          result.AppendError(error.AsCString());
        }
      } else {
        result.AppendErrorWithFormat("%s\n", error.AsCString());
      }
    } else {
      result.AppendError("no platform is currently selected\n");
    }
  }

  Options *GetOptions() override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    OptionGroupOptions *m_platform_options = nullptr;
    if (platform_sp) {
      m_platform_options = platform_sp->GetConnectionOptions(m_interpreter);
      if (m_platform_options != nullptr && !m_platform_options->m_did_finalize)
        m_platform_options->Finalize();
    }
    return m_platform_options;
  }
};

// "platform disconnect"
class CommandObjectPlatformDisconnect : public CommandObjectParsed {
public:
  CommandObjectPlatformDisconnect(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform disconnect",
                            "Disconnect from the current platform.",
                            "platform disconnect", 0) {}

  ~CommandObjectPlatformDisconnect() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      if (args.GetArgumentCount() == 0) {
        Status error;

        if (platform_sp->IsConnected()) {
          // Cache the instance name if there is one since we are about to
          // disconnect and the name might go with it.
          const char *hostname_cstr = platform_sp->GetHostname();
          std::string hostname;
          if (hostname_cstr)
            hostname.assign(hostname_cstr);

          error = platform_sp->DisconnectRemote();
          if (error.Success()) {
            Stream &ostrm = result.GetOutputStream();
            if (hostname.empty())
              ostrm.Format("Disconnected from \"{0}\"\n",
                           platform_sp->GetPluginName());
            else
              ostrm.Printf("Disconnected from \"%s\"\n", hostname.c_str());
            result.SetStatus(eReturnStatusSuccessFinishResult);
          } else {
            result.AppendErrorWithFormat("%s", error.AsCString());
          }
        } else {
          // Not connected...
          result.AppendErrorWithFormatv("not connected to '{0}'",
                                        platform_sp->GetPluginName());
        }
      } else {
        // Bad args
        result.AppendError(
            "\"platform disconnect\" doesn't take any arguments");
      }
    } else {
      result.AppendError("no platform is currently selected");
    }
  }
};

// "platform settings"
class CommandObjectPlatformSettings : public CommandObjectParsed {
public:
  CommandObjectPlatformSettings(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform settings",
                            "Set settings for the current target's platform.",
                            "platform settings", 0),
        m_option_working_dir(LLDB_OPT_SET_1, false, "working-dir", 'w',
                             lldb::eRemoteDiskDirectoryCompletion, eArgTypePath,
                             "The working directory for the platform.") {
    m_options.Append(&m_option_working_dir, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
  }

  ~CommandObjectPlatformSettings() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      if (m_option_working_dir.GetOptionValue().OptionWasSet())
        platform_sp->SetWorkingDirectory(
            m_option_working_dir.GetOptionValue().GetCurrentValue());
    } else {
      result.AppendError("no platform is currently selected");
    }
  }

  Options *GetOptions() override {
    if (!m_options.DidFinalize())
      m_options.Finalize();
    return &m_options;
  }

  OptionGroupOptions m_options;
  OptionGroupFile m_option_working_dir;
};

// "platform mkdir"
class CommandObjectPlatformMkDir : public CommandObjectParsed {
public:
  CommandObjectPlatformMkDir(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform mkdir",
                            "Make a new directory on the remote end.", nullptr,
                            0) {
    AddSimpleArgumentList(eArgTypeRemotePath);
  }

  ~CommandObjectPlatformMkDir() override = default;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      std::string cmd_line;
      args.GetCommandString(cmd_line);
      uint32_t mode;
      const OptionPermissions *options_permissions =
          (const OptionPermissions *)m_options.GetGroupWithOption('r');
      if (options_permissions)
        mode = options_permissions->m_permissions;
      else
        mode = lldb::eFilePermissionsUserRWX | lldb::eFilePermissionsGroupRWX |
               lldb::eFilePermissionsWorldRX;
      Status error = platform_sp->MakeDirectory(FileSpec(cmd_line), mode);
      if (error.Success()) {
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendError(error.AsCString());
      }
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }

  Options *GetOptions() override {
    if (!m_options.DidFinalize()) {
      m_options.Append(&m_option_permissions);
      m_options.Finalize();
    }
    return &m_options;
  }

  OptionPermissions m_option_permissions;
  OptionGroupOptions m_options;
};

// "platform fopen"
class CommandObjectPlatformFOpen : public CommandObjectParsed {
public:
  CommandObjectPlatformFOpen(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform file open",
                            "Open a file on the remote end.", nullptr, 0) {
    AddSimpleArgumentList(eArgTypeRemotePath);
  }

  ~CommandObjectPlatformFOpen() override = default;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      Status error;
      std::string cmd_line;
      args.GetCommandString(cmd_line);
      mode_t perms;
      const OptionPermissions *options_permissions =
          (const OptionPermissions *)m_options.GetGroupWithOption('r');
      if (options_permissions)
        perms = options_permissions->m_permissions;
      else
        perms = lldb::eFilePermissionsUserRW | lldb::eFilePermissionsGroupRW |
                lldb::eFilePermissionsWorldRead;
      lldb::user_id_t fd = platform_sp->OpenFile(
          FileSpec(cmd_line),
          File::eOpenOptionReadWrite | File::eOpenOptionCanCreate,
          perms, error);
      if (error.Success()) {
        result.AppendMessageWithFormat("File Descriptor = %" PRIu64 "\n", fd);
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendError(error.AsCString());
      }
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }

  Options *GetOptions() override {
    if (!m_options.DidFinalize()) {
      m_options.Append(&m_option_permissions);
      m_options.Finalize();
    }
    return &m_options;
  }

  OptionPermissions m_option_permissions;
  OptionGroupOptions m_options;
};

// "platform fclose"
class CommandObjectPlatformFClose : public CommandObjectParsed {
public:
  CommandObjectPlatformFClose(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform file close",
                            "Close a file on the remote end.", nullptr, 0) {
    AddSimpleArgumentList(eArgTypeUnsignedInteger);
  }

  ~CommandObjectPlatformFClose() override = default;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      std::string cmd_line;
      args.GetCommandString(cmd_line);
      lldb::user_id_t fd;
      if (!llvm::to_integer(cmd_line, fd)) {
        result.AppendErrorWithFormatv("'{0}' is not a valid file descriptor.\n",
                                      cmd_line);
        return;
      }
      Status error;
      bool success = platform_sp->CloseFile(fd, error);
      if (success) {
        result.AppendMessageWithFormat("file %" PRIu64 " closed.\n", fd);
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendError(error.AsCString());
      }
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }
};

// "platform fread"

#define LLDB_OPTIONS_platform_fread
#include "CommandOptions.inc"

class CommandObjectPlatformFRead : public CommandObjectParsed {
public:
  CommandObjectPlatformFRead(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform file read",
                            "Read data from a file on the remote end.", nullptr,
                            0) {
    AddSimpleArgumentList(eArgTypeUnsignedInteger);
  }

  ~CommandObjectPlatformFRead() override = default;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      std::string cmd_line;
      args.GetCommandString(cmd_line);
      lldb::user_id_t fd;
      if (!llvm::to_integer(cmd_line, fd)) {
        result.AppendErrorWithFormatv("'{0}' is not a valid file descriptor.\n",
                                      cmd_line);
        return;
      }
      std::string buffer(m_options.m_count, 0);
      Status error;
      uint64_t retcode = platform_sp->ReadFile(
          fd, m_options.m_offset, &buffer[0], m_options.m_count, error);
      if (retcode != UINT64_MAX) {
        result.AppendMessageWithFormat("Return = %" PRIu64 "\n", retcode);
        result.AppendMessageWithFormat("Data = \"%s\"\n", buffer.c_str());
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendError(error.AsCString());
      }
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }

  Options *GetOptions() override { return &m_options; }

protected:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      char short_option = (char)m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'o':
        if (option_arg.getAsInteger(0, m_offset))
          error.SetErrorStringWithFormat("invalid offset: '%s'",
                                         option_arg.str().c_str());
        break;
      case 'c':
        if (option_arg.getAsInteger(0, m_count))
          error.SetErrorStringWithFormat("invalid offset: '%s'",
                                         option_arg.str().c_str());
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_offset = 0;
      m_count = 1;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_platform_fread_options);
    }

    // Instance variables to hold the values for command options.

    uint32_t m_offset;
    uint32_t m_count;
  };

  CommandOptions m_options;
};

// "platform fwrite"

#define LLDB_OPTIONS_platform_fwrite
#include "CommandOptions.inc"

class CommandObjectPlatformFWrite : public CommandObjectParsed {
public:
  CommandObjectPlatformFWrite(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform file write",
                            "Write data to a file on the remote end.", nullptr,
                            0) {
    AddSimpleArgumentList(eArgTypeUnsignedInteger);
  }

  ~CommandObjectPlatformFWrite() override = default;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      std::string cmd_line;
      args.GetCommandString(cmd_line);
      Status error;
      lldb::user_id_t fd;
      if (!llvm::to_integer(cmd_line, fd)) {
        result.AppendErrorWithFormatv("'{0}' is not a valid file descriptor.",
                                      cmd_line);
        return;
      }
      uint64_t retcode =
          platform_sp->WriteFile(fd, m_options.m_offset, &m_options.m_data[0],
                                 m_options.m_data.size(), error);
      if (retcode != UINT64_MAX) {
        result.AppendMessageWithFormat("Return = %" PRIu64 "\n", retcode);
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendError(error.AsCString());
      }
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }

  Options *GetOptions() override { return &m_options; }

protected:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      char short_option = (char)m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'o':
        if (option_arg.getAsInteger(0, m_offset))
          error.SetErrorStringWithFormat("invalid offset: '%s'",
                                         option_arg.str().c_str());
        break;
      case 'd':
        m_data.assign(std::string(option_arg));
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_offset = 0;
      m_data.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_platform_fwrite_options);
    }

    // Instance variables to hold the values for command options.

    uint32_t m_offset;
    std::string m_data;
  };

  CommandOptions m_options;
};

class CommandObjectPlatformFile : public CommandObjectMultiword {
public:
  // Constructors and Destructors
  CommandObjectPlatformFile(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "platform file",
            "Commands to access files on the current platform.",
            "platform file [open|close|read|write] ...") {
    LoadSubCommand(
        "open", CommandObjectSP(new CommandObjectPlatformFOpen(interpreter)));
    LoadSubCommand(
        "close", CommandObjectSP(new CommandObjectPlatformFClose(interpreter)));
    LoadSubCommand(
        "read", CommandObjectSP(new CommandObjectPlatformFRead(interpreter)));
    LoadSubCommand(
        "write", CommandObjectSP(new CommandObjectPlatformFWrite(interpreter)));
  }

  ~CommandObjectPlatformFile() override = default;

private:
  // For CommandObjectPlatform only
  CommandObjectPlatformFile(const CommandObjectPlatformFile &) = delete;
  const CommandObjectPlatformFile &
  operator=(const CommandObjectPlatformFile &) = delete;
};

// "platform get-file remote-file-path host-file-path"
class CommandObjectPlatformGetFile : public CommandObjectParsed {
public:
  CommandObjectPlatformGetFile(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "platform get-file",
            "Transfer a file from the remote end to the local host.",
            "platform get-file <remote-file-spec> <local-file-spec>", 0) {
    SetHelpLong(
        R"(Examples:

(lldb) platform get-file /the/remote/file/path /the/local/file/path

    Transfer a file from the remote end with file path /the/remote/file/path to the local host.)");

    CommandArgumentEntry arg1, arg2;
    CommandArgumentData file_arg_remote, file_arg_host;

    // Define the first (and only) variant of this arg.
    file_arg_remote.arg_type = eArgTypeRemoteFilename;
    file_arg_remote.arg_repetition = eArgRepeatPlain;
    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(file_arg_remote);

    // Define the second (and only) variant of this arg.
    file_arg_host.arg_type = eArgTypeFilename;
    file_arg_host.arg_repetition = eArgRepeatPlain;
    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(file_arg_host);

    // Push the data for the first and the second arguments into the
    // m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectPlatformGetFile() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex() == 0)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eRemoteDiskFileCompletion, request,
          nullptr);
    else if (request.GetCursorIndex() == 1)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eDiskFileCompletion, request, nullptr);
  }

  void DoExecute(Args &args, CommandReturnObject &result) override {
    // If the number of arguments is incorrect, issue an error message.
    if (args.GetArgumentCount() != 2) {
      result.AppendError("required arguments missing; specify both the "
                         "source and destination file paths");
      return;
    }

    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      const char *remote_file_path = args.GetArgumentAtIndex(0);
      const char *local_file_path = args.GetArgumentAtIndex(1);
      Status error = platform_sp->GetFile(FileSpec(remote_file_path),
                                          FileSpec(local_file_path));
      if (error.Success()) {
        result.AppendMessageWithFormat(
            "successfully get-file from %s (remote) to %s (host)\n",
            remote_file_path, local_file_path);
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendMessageWithFormat("get-file failed: %s\n",
                                       error.AsCString());
      }
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }
};

// "platform get-size remote-file-path"
class CommandObjectPlatformGetSize : public CommandObjectParsed {
public:
  CommandObjectPlatformGetSize(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform get-size",
                            "Get the file size from the remote end.",
                            "platform get-size <remote-file-spec>", 0) {
    SetHelpLong(
        R"(Examples:

(lldb) platform get-size /the/remote/file/path

    Get the file size from the remote end with path /the/remote/file/path.)");

    AddSimpleArgumentList(eArgTypeRemoteFilename);
  }

  ~CommandObjectPlatformGetSize() override = default;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    // If the number of arguments is incorrect, issue an error message.
    if (args.GetArgumentCount() != 1) {
      result.AppendError("required argument missing; specify the source file "
                         "path as the only argument");
      return;
    }

    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      std::string remote_file_path(args.GetArgumentAtIndex(0));
      user_id_t size = platform_sp->GetFileSize(FileSpec(remote_file_path));
      if (size != UINT64_MAX) {
        result.AppendMessageWithFormat("File size of %s (remote): %" PRIu64
                                       "\n",
                                       remote_file_path.c_str(), size);
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendMessageWithFormat(
            "Error getting file size of %s (remote)\n",
            remote_file_path.c_str());
      }
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }
};

// "platform get-permissions remote-file-path"
class CommandObjectPlatformGetPermissions : public CommandObjectParsed {
public:
  CommandObjectPlatformGetPermissions(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform get-permissions",
                            "Get the file permission bits from the remote end.",
                            "platform get-permissions <remote-file-spec>", 0) {
    SetHelpLong(
        R"(Examples:

(lldb) platform get-permissions /the/remote/file/path

    Get the file permissions from the remote end with path /the/remote/file/path.)");

    AddSimpleArgumentList(eArgTypeRemoteFilename);
  }

  ~CommandObjectPlatformGetPermissions() override = default;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    // If the number of arguments is incorrect, issue an error message.
    if (args.GetArgumentCount() != 1) {
      result.AppendError("required argument missing; specify the source file "
                         "path as the only argument");
      return;
    }

    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      std::string remote_file_path(args.GetArgumentAtIndex(0));
      uint32_t permissions;
      Status error = platform_sp->GetFilePermissions(FileSpec(remote_file_path),
                                                     permissions);
      if (error.Success()) {
        result.AppendMessageWithFormat(
            "File permissions of %s (remote): 0o%04" PRIo32 "\n",
            remote_file_path.c_str(), permissions);
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else
        result.AppendError(error.AsCString());
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }
};

// "platform file-exists remote-file-path"
class CommandObjectPlatformFileExists : public CommandObjectParsed {
public:
  CommandObjectPlatformFileExists(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform file-exists",
                            "Check if the file exists on the remote end.",
                            "platform file-exists <remote-file-spec>", 0) {
    SetHelpLong(
        R"(Examples:

(lldb) platform file-exists /the/remote/file/path

    Check if /the/remote/file/path exists on the remote end.)");

    AddSimpleArgumentList(eArgTypeRemoteFilename);
  }

  ~CommandObjectPlatformFileExists() override = default;

  void DoExecute(Args &args, CommandReturnObject &result) override {
    // If the number of arguments is incorrect, issue an error message.
    if (args.GetArgumentCount() != 1) {
      result.AppendError("required argument missing; specify the source file "
                         "path as the only argument");
      return;
    }

    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      std::string remote_file_path(args.GetArgumentAtIndex(0));
      bool exists = platform_sp->GetFileExists(FileSpec(remote_file_path));
      result.AppendMessageWithFormat(
          "File %s (remote) %s\n",
          remote_file_path.c_str(), exists ? "exists" : "does not exist");
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }
};

// "platform put-file"
class CommandObjectPlatformPutFile : public CommandObjectParsed {
public:
  CommandObjectPlatformPutFile(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "platform put-file",
            "Transfer a file from this system to the remote end.",
            "platform put-file <source> [<destination>]", 0) {
    SetHelpLong(
        R"(Examples:

(lldb) platform put-file /source/foo.txt /destination/bar.txt

(lldb) platform put-file /source/foo.txt

    Relative source file paths are resolved against lldb's local working directory.

    Omitting the destination places the file in the platform working directory.)");
    CommandArgumentData source_arg{eArgTypePath, eArgRepeatPlain};
    CommandArgumentData path_arg{eArgTypeRemotePath, eArgRepeatOptional};
    m_arguments.push_back({source_arg});
    m_arguments.push_back({path_arg});
  }

  ~CommandObjectPlatformPutFile() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex() == 0)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eDiskFileCompletion, request, nullptr);
    else if (request.GetCursorIndex() == 1)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eRemoteDiskFileCompletion, request,
          nullptr);
  }

  void DoExecute(Args &args, CommandReturnObject &result) override {
    const char *src = args.GetArgumentAtIndex(0);
    const char *dst = args.GetArgumentAtIndex(1);

    FileSpec src_fs(src);
    FileSystem::Instance().Resolve(src_fs);
    FileSpec dst_fs(dst ? dst : src_fs.GetFilename().GetCString());

    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {
      Status error(platform_sp->PutFile(src_fs, dst_fs));
      if (error.Success()) {
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
      } else {
        result.AppendError(error.AsCString());
      }
    } else {
      result.AppendError("no platform currently selected\n");
    }
  }
};

// "platform process launch"
class CommandObjectPlatformProcessLaunch : public CommandObjectParsed {
public:
  CommandObjectPlatformProcessLaunch(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform process launch",
                            "Launch a new process on a remote platform.",
                            "platform process launch program",
                            eCommandRequiresTarget | eCommandTryTargetAPILock),
        m_class_options("scripted process", true, 'C', 'k', 'v', 0) {
    m_all_options.Append(&m_options);
    m_all_options.Append(&m_class_options, LLDB_OPT_SET_1 | LLDB_OPT_SET_2,
                         LLDB_OPT_SET_ALL);
    m_all_options.Finalize();
    AddSimpleArgumentList(eArgTypeRunArgs, eArgRepeatStar);
  }

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    // I didn't make a type for RemoteRunArgs, but since we're going to run
    // this on the remote system we should use the remote completer.
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eRemoteDiskFileCompletion, request,
        nullptr);
  }

  ~CommandObjectPlatformProcessLaunch() override = default;

  Options *GetOptions() override { return &m_all_options; }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = GetDebugger().GetSelectedTarget().get();
    PlatformSP platform_sp;
    if (target) {
      platform_sp = target->GetPlatform();
    }
    if (!platform_sp) {
      platform_sp = GetDebugger().GetPlatformList().GetSelectedPlatform();
    }

    if (platform_sp) {
      Status error;
      const size_t argc = args.GetArgumentCount();
      Target *target = m_exe_ctx.GetTargetPtr();
      Module *exe_module = target->GetExecutableModulePointer();
      if (exe_module) {
        m_options.launch_info.GetExecutableFile() = exe_module->GetFileSpec();
        llvm::SmallString<128> exe_path;
        m_options.launch_info.GetExecutableFile().GetPath(exe_path);
        if (!exe_path.empty())
          m_options.launch_info.GetArguments().AppendArgument(exe_path);
        m_options.launch_info.GetArchitecture() = exe_module->GetArchitecture();
      }

      if (!m_class_options.GetName().empty()) {
        m_options.launch_info.SetProcessPluginName("ScriptedProcess");
        ScriptedMetadataSP metadata_sp = std::make_shared<ScriptedMetadata>(
            m_class_options.GetName(), m_class_options.GetStructuredData());
        m_options.launch_info.SetScriptedMetadata(metadata_sp);
        target->SetProcessLaunchInfo(m_options.launch_info);
      }

      if (argc > 0) {
        if (m_options.launch_info.GetExecutableFile()) {
          // We already have an executable file, so we will use this and all
          // arguments to this function are extra arguments
          m_options.launch_info.GetArguments().AppendArguments(args);
        } else {
          // We don't have any file yet, so the first argument is our
          // executable, and the rest are program arguments
          const bool first_arg_is_executable = true;
          m_options.launch_info.SetArguments(args, first_arg_is_executable);
        }
      }

      if (m_options.launch_info.GetExecutableFile()) {
        Debugger &debugger = GetDebugger();

        if (argc == 0) {
          // If no arguments were given to the command, use target.run-args.
          Args target_run_args;
          target->GetRunArguments(target_run_args);
          m_options.launch_info.GetArguments().AppendArguments(target_run_args);
        }

        ProcessSP process_sp(platform_sp->DebugProcess(
            m_options.launch_info, debugger, *target, error));

        if (!process_sp && error.Success()) {
          result.AppendError("failed to launch or debug process");
          return;
        } else if (!error.Success()) {
          result.AppendError(error.AsCString());
          return;
        }

        const bool synchronous_execution =
            debugger.GetCommandInterpreter().GetSynchronous();
        auto launch_info = m_options.launch_info;
        bool rebroadcast_first_stop =
            !synchronous_execution &&
            launch_info.GetFlags().Test(eLaunchFlagStopAtEntry);

        EventSP first_stop_event_sp;
        StateType state = process_sp->WaitForProcessToStop(
            std::nullopt, &first_stop_event_sp, rebroadcast_first_stop,
            launch_info.GetHijackListener());
        process_sp->RestoreProcessEvents();

        if (rebroadcast_first_stop) {
          assert(first_stop_event_sp);
          process_sp->BroadcastEvent(first_stop_event_sp);
          return;
        }

        switch (state) {
        case eStateStopped: {
          if (launch_info.GetFlags().Test(eLaunchFlagStopAtEntry))
            break;
          if (synchronous_execution) {
            // Now we have handled the stop-from-attach, and we are just
            // switching to a synchronous resume.  So we should switch to the
            // SyncResume hijacker.
            process_sp->ResumeSynchronous(&result.GetOutputStream());
          } else {
            error = process_sp->Resume();
            if (!error.Success()) {
              result.AppendErrorWithFormat(
                  "process resume at entry point failed: %s",
                  error.AsCString());
            }
          }
        } break;
        default:
          result.AppendErrorWithFormat(
              "initial process state wasn't stopped: %s",
              StateAsCString(state));
          break;
        }

        if (process_sp && process_sp->IsAlive()) {
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
          return;
        }
      } else {
        result.AppendError("'platform process launch' uses the current target "
                           "file and arguments, or the executable and its "
                           "arguments can be specified in this command");
        return;
      }
    } else {
      result.AppendError("no platform is selected\n");
    }
  }

  CommandOptionsProcessLaunch m_options;
  OptionGroupPythonClassWithDict m_class_options;
  OptionGroupOptions m_all_options;
};

// "platform process list"

static PosixPlatformCommandOptionValidator posix_validator;
#define LLDB_OPTIONS_platform_process_list
#include "CommandOptions.inc"

class CommandObjectPlatformProcessList : public CommandObjectParsed {
public:
  CommandObjectPlatformProcessList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform process list",
                            "List processes on a remote platform by name, pid, "
                            "or many other matching attributes.",
                            "platform process list", 0) {}

  ~CommandObjectPlatformProcessList() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = GetDebugger().GetSelectedTarget().get();
    PlatformSP platform_sp;
    if (target) {
      platform_sp = target->GetPlatform();
    }
    if (!platform_sp) {
      platform_sp = GetDebugger().GetPlatformList().GetSelectedPlatform();
    }

    if (platform_sp) {
      Status error;
      if (platform_sp) {
        Stream &ostrm = result.GetOutputStream();

        lldb::pid_t pid = m_options.match_info.GetProcessInfo().GetProcessID();
        if (pid != LLDB_INVALID_PROCESS_ID) {
          ProcessInstanceInfo proc_info;
          if (platform_sp->GetProcessInfo(pid, proc_info)) {
            ProcessInstanceInfo::DumpTableHeader(ostrm, m_options.show_args,
                                                 m_options.verbose);
            proc_info.DumpAsTableRow(ostrm, platform_sp->GetUserIDResolver(),
                                     m_options.show_args, m_options.verbose);
            result.SetStatus(eReturnStatusSuccessFinishResult);
          } else {
            result.AppendErrorWithFormat(
                "no process found with pid = %" PRIu64 "\n", pid);
          }
        } else {
          ProcessInstanceInfoList proc_infos;
          const uint32_t matches =
              platform_sp->FindProcesses(m_options.match_info, proc_infos);
          const char *match_desc = nullptr;
          const char *match_name =
              m_options.match_info.GetProcessInfo().GetName();
          if (match_name && match_name[0]) {
            switch (m_options.match_info.GetNameMatchType()) {
            case NameMatch::Ignore:
              break;
            case NameMatch::Equals:
              match_desc = "matched";
              break;
            case NameMatch::Contains:
              match_desc = "contained";
              break;
            case NameMatch::StartsWith:
              match_desc = "started with";
              break;
            case NameMatch::EndsWith:
              match_desc = "ended with";
              break;
            case NameMatch::RegularExpression:
              match_desc = "matched the regular expression";
              break;
            }
          }

          if (matches == 0) {
            if (match_desc)
              result.AppendErrorWithFormatv(
                  "no processes were found that {0} \"{1}\" on the \"{2}\" "
                  "platform\n",
                  match_desc, match_name, platform_sp->GetName());
            else
              result.AppendErrorWithFormatv(
                  "no processes were found on the \"{0}\" platform\n",
                  platform_sp->GetName());
          } else {
            result.AppendMessageWithFormatv(
                "{0} matching process{1} found on \"{2}\"", matches,
                matches > 1 ? "es were" : " was", platform_sp->GetName());
            if (match_desc)
              result.AppendMessageWithFormat(" whose name %s \"%s\"",
                                             match_desc, match_name);
            result.AppendMessageWithFormat("\n");
            ProcessInstanceInfo::DumpTableHeader(ostrm, m_options.show_args,
                                                 m_options.verbose);
            for (uint32_t i = 0; i < matches; ++i) {
              proc_infos[i].DumpAsTableRow(
                  ostrm, platform_sp->GetUserIDResolver(), m_options.show_args,
                  m_options.verbose);
            }
          }
        }
      }
    } else {
      result.AppendError("no platform is selected\n");
    }
  }

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      bool success = false;

      uint32_t id = LLDB_INVALID_PROCESS_ID;
      success = !option_arg.getAsInteger(0, id);
      switch (short_option) {
      case 'p': {
        match_info.GetProcessInfo().SetProcessID(id);
        if (!success)
          error.SetErrorStringWithFormat("invalid process ID string: '%s'",
                                         option_arg.str().c_str());
        break;
      }
      case 'P':
        match_info.GetProcessInfo().SetParentProcessID(id);
        if (!success)
          error.SetErrorStringWithFormat(
              "invalid parent process ID string: '%s'",
              option_arg.str().c_str());
        break;

      case 'u':
        match_info.GetProcessInfo().SetUserID(success ? id : UINT32_MAX);
        if (!success)
          error.SetErrorStringWithFormat("invalid user ID string: '%s'",
                                         option_arg.str().c_str());
        break;

      case 'U':
        match_info.GetProcessInfo().SetEffectiveUserID(success ? id
                                                               : UINT32_MAX);
        if (!success)
          error.SetErrorStringWithFormat(
              "invalid effective user ID string: '%s'",
              option_arg.str().c_str());
        break;

      case 'g':
        match_info.GetProcessInfo().SetGroupID(success ? id : UINT32_MAX);
        if (!success)
          error.SetErrorStringWithFormat("invalid group ID string: '%s'",
                                         option_arg.str().c_str());
        break;

      case 'G':
        match_info.GetProcessInfo().SetEffectiveGroupID(success ? id
                                                                : UINT32_MAX);
        if (!success)
          error.SetErrorStringWithFormat(
              "invalid effective group ID string: '%s'",
              option_arg.str().c_str());
        break;

      case 'a': {
        TargetSP target_sp =
            execution_context ? execution_context->GetTargetSP() : TargetSP();
        DebuggerSP debugger_sp =
            target_sp ? target_sp->GetDebugger().shared_from_this()
                      : DebuggerSP();
        PlatformSP platform_sp =
            debugger_sp ? debugger_sp->GetPlatformList().GetSelectedPlatform()
                        : PlatformSP();
        match_info.GetProcessInfo().GetArchitecture() =
            Platform::GetAugmentedArchSpec(platform_sp.get(), option_arg);
      } break;

      case 'n':
        match_info.GetProcessInfo().GetExecutableFile().SetFile(
            option_arg, FileSpec::Style::native);
        match_info.SetNameMatchType(NameMatch::Equals);
        break;

      case 'e':
        match_info.GetProcessInfo().GetExecutableFile().SetFile(
            option_arg, FileSpec::Style::native);
        match_info.SetNameMatchType(NameMatch::EndsWith);
        break;

      case 's':
        match_info.GetProcessInfo().GetExecutableFile().SetFile(
            option_arg, FileSpec::Style::native);
        match_info.SetNameMatchType(NameMatch::StartsWith);
        break;

      case 'c':
        match_info.GetProcessInfo().GetExecutableFile().SetFile(
            option_arg, FileSpec::Style::native);
        match_info.SetNameMatchType(NameMatch::Contains);
        break;

      case 'r':
        match_info.GetProcessInfo().GetExecutableFile().SetFile(
            option_arg, FileSpec::Style::native);
        match_info.SetNameMatchType(NameMatch::RegularExpression);
        break;

      case 'A':
        show_args = true;
        break;

      case 'v':
        verbose = true;
        break;

      case 'x':
        match_info.SetMatchAllUsers(true);
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      match_info.Clear();
      show_args = false;
      verbose = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_platform_process_list_options);
    }

    // Instance variables to hold the values for command options.

    ProcessInstanceInfoMatch match_info;
    bool show_args = false;
    bool verbose = false;
  };

  CommandOptions m_options;
};

// "platform process info"
class CommandObjectPlatformProcessInfo : public CommandObjectParsed {
public:
  CommandObjectPlatformProcessInfo(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "platform process info",
            "Get detailed information for one or more process by process ID.",
            "platform process info <pid> [<pid> <pid> ...]", 0) {
    AddSimpleArgumentList(eArgTypePid, eArgRepeatStar);
  }

  ~CommandObjectPlatformProcessInfo() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = GetDebugger().GetSelectedTarget().get();
    PlatformSP platform_sp;
    if (target) {
      platform_sp = target->GetPlatform();
    }
    if (!platform_sp) {
      platform_sp = GetDebugger().GetPlatformList().GetSelectedPlatform();
    }

    if (platform_sp) {
      const size_t argc = args.GetArgumentCount();
      if (argc > 0) {
        Status error;

        if (platform_sp->IsConnected()) {
          Stream &ostrm = result.GetOutputStream();
          for (auto &entry : args.entries()) {
            lldb::pid_t pid;
            if (entry.ref().getAsInteger(0, pid)) {
              result.AppendErrorWithFormat("invalid process ID argument '%s'",
                                           entry.ref().str().c_str());
              break;
            } else {
              ProcessInstanceInfo proc_info;
              if (platform_sp->GetProcessInfo(pid, proc_info)) {
                ostrm.Printf("Process information for process %" PRIu64 ":\n",
                             pid);
                proc_info.Dump(ostrm, platform_sp->GetUserIDResolver());
              } else {
                ostrm.Printf("error: no process information is available for "
                             "process %" PRIu64 "\n",
                             pid);
              }
              ostrm.EOL();
            }
          }
        } else {
          // Not connected...
          result.AppendErrorWithFormatv("not connected to '{0}'",
                                        platform_sp->GetPluginName());
        }
      } else {
        // No args
        result.AppendError("one or more process id(s) must be specified");
      }
    } else {
      result.AppendError("no platform is currently selected");
    }
  }
};

#define LLDB_OPTIONS_platform_process_attach
#include "CommandOptions.inc"

class CommandObjectPlatformProcessAttach : public CommandObjectParsed {
public:
  CommandObjectPlatformProcessAttach(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "platform process attach",
                            "Attach to a process.",
                            "platform process attach <cmd-options>"),
        m_class_options("scripted process", true, 'C', 'k', 'v', 0) {
    m_all_options.Append(&m_options);
    m_all_options.Append(&m_class_options, LLDB_OPT_SET_1 | LLDB_OPT_SET_2,
                         LLDB_OPT_SET_ALL);
    m_all_options.Finalize();
  }

  ~CommandObjectPlatformProcessAttach() override = default;

  void DoExecute(Args &command, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (platform_sp) {

      if (!m_class_options.GetName().empty()) {
        m_options.attach_info.SetProcessPluginName("ScriptedProcess");
        ScriptedMetadataSP metadata_sp = std::make_shared<ScriptedMetadata>(
            m_class_options.GetName(), m_class_options.GetStructuredData());
        m_options.attach_info.SetScriptedMetadata(metadata_sp);
      }

      Status err;
      ProcessSP remote_process_sp = platform_sp->Attach(
          m_options.attach_info, GetDebugger(), nullptr, err);
      if (err.Fail()) {
        result.AppendError(err.AsCString());
      } else if (!remote_process_sp) {
        result.AppendError("could not attach: unknown reason");
      } else
        result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("no platform is currently selected");
    }
  }

  Options *GetOptions() override { return &m_all_options; }

protected:
  CommandOptionsProcessAttach m_options;
  OptionGroupPythonClassWithDict m_class_options;
  OptionGroupOptions m_all_options;
};

class CommandObjectPlatformProcess : public CommandObjectMultiword {
public:
  // Constructors and Destructors
  CommandObjectPlatformProcess(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "platform process",
                               "Commands to query, launch and attach to "
                               "processes on the current platform.",
                               "platform process [attach|launch|list] ...") {
    LoadSubCommand(
        "attach",
        CommandObjectSP(new CommandObjectPlatformProcessAttach(interpreter)));
    LoadSubCommand(
        "launch",
        CommandObjectSP(new CommandObjectPlatformProcessLaunch(interpreter)));
    LoadSubCommand("info", CommandObjectSP(new CommandObjectPlatformProcessInfo(
                               interpreter)));
    LoadSubCommand("list", CommandObjectSP(new CommandObjectPlatformProcessList(
                               interpreter)));
  }

  ~CommandObjectPlatformProcess() override = default;

private:
  // For CommandObjectPlatform only
  CommandObjectPlatformProcess(const CommandObjectPlatformProcess &) = delete;
  const CommandObjectPlatformProcess &
  operator=(const CommandObjectPlatformProcess &) = delete;
};

// "platform shell"
#define LLDB_OPTIONS_platform_shell
#include "CommandOptions.inc"

class CommandObjectPlatformShell : public CommandObjectRaw {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_platform_shell_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;

      const char short_option = (char)GetDefinitions()[option_idx].short_option;

      switch (short_option) {
      case 'h':
        m_use_host_platform = true;
        break;
      case 't':
        uint32_t timeout_sec;
        if (option_arg.getAsInteger(10, timeout_sec))
          error.SetErrorStringWithFormat(
              "could not convert \"%s\" to a numeric value.",
              option_arg.str().c_str());
        else
          m_timeout = std::chrono::seconds(timeout_sec);
        break;
      case 's': {
        if (option_arg.empty()) {
          error.SetErrorStringWithFormat(
              "missing shell interpreter path for option -i|--interpreter.");
          return error;
        }

        m_shell_interpreter = option_arg.str();
        break;
      }
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_timeout.reset();
      m_use_host_platform = false;
      m_shell_interpreter.clear();
    }

    Timeout<std::micro> m_timeout = std::chrono::seconds(10);
    bool m_use_host_platform;
    std::string m_shell_interpreter;
  };

  CommandObjectPlatformShell(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "platform shell",
                         "Run a shell command on the current platform.",
                         "platform shell <shell-command>", 0) {
    AddSimpleArgumentList(eArgTypeNone, eArgRepeatStar);
  }

  ~CommandObjectPlatformShell() override = default;

  Options *GetOptions() override { return &m_options; }

  void DoExecute(llvm::StringRef raw_command_line,
                 CommandReturnObject &result) override {
    ExecutionContext exe_ctx = GetCommandInterpreter().GetExecutionContext();
    m_options.NotifyOptionParsingStarting(&exe_ctx);

    // Print out an usage syntax on an empty command line.
    if (raw_command_line.empty()) {
      result.GetOutputStream().Printf("%s\n", this->GetSyntax().str().c_str());
      return;
    }

    const bool is_alias = !raw_command_line.contains("platform");
    OptionsWithRaw args(raw_command_line);

    if (args.HasArgs())
      if (!ParseOptions(args.GetArgs(), result))
        return;

    if (args.GetRawPart().empty()) {
      result.GetOutputStream().Printf("%s <shell-command>\n",
                                      is_alias ? "shell" : "platform shell");
      return;
    }

    llvm::StringRef cmd = args.GetRawPart();

    PlatformSP platform_sp(
        m_options.m_use_host_platform
            ? Platform::GetHostPlatform()
            : GetDebugger().GetPlatformList().GetSelectedPlatform());
    Status error;
    if (platform_sp) {
      FileSpec working_dir{};
      std::string output;
      int status = -1;
      int signo = -1;
      error = (platform_sp->RunShellCommand(m_options.m_shell_interpreter, cmd,
                                            working_dir, &status, &signo,
                                            &output, m_options.m_timeout));
      if (!output.empty())
        result.GetOutputStream().PutCString(output);
      if (status > 0) {
        if (signo > 0) {
          const char *signo_cstr = Host::GetSignalAsCString(signo);
          if (signo_cstr)
            result.GetOutputStream().Printf(
                "error: command returned with status %i and signal %s\n",
                status, signo_cstr);
          else
            result.GetOutputStream().Printf(
                "error: command returned with status %i and signal %i\n",
                status, signo);
        } else
          result.GetOutputStream().Printf(
              "error: command returned with status %i\n", status);
      }
    } else {
      result.GetOutputStream().Printf(
          "error: cannot run remote shell commands without a platform\n");
      error.SetErrorString(
          "error: cannot run remote shell commands without a platform");
    }

    if (error.Fail()) {
      result.AppendError(error.AsCString());
    } else {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    }
  }

  CommandOptions m_options;
};

// "platform install" - install a target to a remote end
class CommandObjectPlatformInstall : public CommandObjectParsed {
public:
  CommandObjectPlatformInstall(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "platform target-install",
            "Install a target (bundle or executable file) to the remote end.",
            "platform target-install <local-thing> <remote-sandbox>", 0) {
    CommandArgumentData local_arg{eArgTypePath, eArgRepeatPlain};
    CommandArgumentData remote_arg{eArgTypeRemotePath, eArgRepeatPlain};
    m_arguments.push_back({local_arg});
    m_arguments.push_back({remote_arg});
  }

  ~CommandObjectPlatformInstall() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex())
      return;
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eDiskFileCompletion, request, nullptr);
  }

  void DoExecute(Args &args, CommandReturnObject &result) override {
    if (args.GetArgumentCount() != 2) {
      result.AppendError("platform target-install takes two arguments");
      return;
    }
    // TODO: move the bulk of this code over to the platform itself
    FileSpec src(args.GetArgumentAtIndex(0));
    FileSystem::Instance().Resolve(src);
    FileSpec dst(args.GetArgumentAtIndex(1));
    if (!FileSystem::Instance().Exists(src)) {
      result.AppendError("source location does not exist or is not accessible");
      return;
    }
    PlatformSP platform_sp(
        GetDebugger().GetPlatformList().GetSelectedPlatform());
    if (!platform_sp) {
      result.AppendError("no platform currently selected");
      return;
    }

    Status error = platform_sp->Install(src, dst);
    if (error.Success()) {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendErrorWithFormat("install failed: %s", error.AsCString());
    }
  }
};

CommandObjectPlatform::CommandObjectPlatform(CommandInterpreter &interpreter)
    : CommandObjectMultiword(
          interpreter, "platform", "Commands to manage and create platforms.",
          "platform [connect|disconnect|info|list|status|select] ...") {
  LoadSubCommand("select",
                 CommandObjectSP(new CommandObjectPlatformSelect(interpreter)));
  LoadSubCommand("list",
                 CommandObjectSP(new CommandObjectPlatformList(interpreter)));
  LoadSubCommand("status",
                 CommandObjectSP(new CommandObjectPlatformStatus(interpreter)));
  LoadSubCommand("connect", CommandObjectSP(
                                new CommandObjectPlatformConnect(interpreter)));
  LoadSubCommand(
      "disconnect",
      CommandObjectSP(new CommandObjectPlatformDisconnect(interpreter)));
  LoadSubCommand("settings", CommandObjectSP(new CommandObjectPlatformSettings(
                                 interpreter)));
  LoadSubCommand("mkdir",
                 CommandObjectSP(new CommandObjectPlatformMkDir(interpreter)));
  LoadSubCommand("file",
                 CommandObjectSP(new CommandObjectPlatformFile(interpreter)));
  LoadSubCommand("file-exists",
      CommandObjectSP(new CommandObjectPlatformFileExists(interpreter)));
  LoadSubCommand("get-file", CommandObjectSP(new CommandObjectPlatformGetFile(
                                 interpreter)));
  LoadSubCommand("get-permissions",
      CommandObjectSP(new CommandObjectPlatformGetPermissions(interpreter)));
  LoadSubCommand("get-size", CommandObjectSP(new CommandObjectPlatformGetSize(
                                 interpreter)));
  LoadSubCommand("put-file", CommandObjectSP(new CommandObjectPlatformPutFile(
                                 interpreter)));
  LoadSubCommand("process", CommandObjectSP(
                                new CommandObjectPlatformProcess(interpreter)));
  LoadSubCommand("shell",
                 CommandObjectSP(new CommandObjectPlatformShell(interpreter)));
  LoadSubCommand(
      "target-install",
      CommandObjectSP(new CommandObjectPlatformInstall(interpreter)));
}

CommandObjectPlatform::~CommandObjectPlatform() = default;
