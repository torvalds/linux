//===-- CommandObjectSettings.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectSettings.h"

#include "llvm/ADT/StringRef.h"

#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionValueProperties.h"

using namespace lldb;
using namespace lldb_private;

// CommandObjectSettingsSet
#define LLDB_OPTIONS_settings_set
#include "CommandOptions.inc"

class CommandObjectSettingsSet : public CommandObjectRaw {
public:
  CommandObjectSettingsSet(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings set",
                         "Set the value of the specified debugger setting.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData var_name_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);

    SetHelpLong(
        "\nWhen setting a dictionary or array variable, you can set multiple entries \
at once by giving the values to the set command.  For example:"
        R"(

(lldb) settings set target.run-args value1 value2 value3
(lldb) settings set target.env-vars MYPATH=~/.:/usr/bin  SOME_ENV_VAR=12345

(lldb) settings show target.run-args
  [0]: 'value1'
  [1]: 'value2'
  [3]: 'value3'
(lldb) settings show target.env-vars
  'MYPATH=~/.:/usr/bin'
  'SOME_ENV_VAR=12345'

)"
        "Warning:  The 'set' command re-sets the entire array or dictionary.  If you \
just want to add, remove or update individual values (or add something to \
the end), use one of the other settings sub-commands: append, replace, \
insert-before or insert-after.");
  }

  ~CommandObjectSettingsSet() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

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
      case 'f':
        m_force = true;
        break;
      case 'g':
        m_global = true;
        break;
      case 'e':
        m_exists = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_global = false;
      m_force = false;
      m_exists = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_settings_set_options);
    }

    // Instance variables to hold the values for command options.
    bool m_global = false;
    bool m_force = false;
    bool m_exists = false;
  };

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {

    const size_t argc = request.GetParsedLine().GetArgumentCount();
    const char *arg = nullptr;
    size_t setting_var_idx;
    for (setting_var_idx = 0; setting_var_idx < argc; ++setting_var_idx) {
      arg = request.GetParsedLine().GetArgumentAtIndex(setting_var_idx);
      if (arg && arg[0] != '-')
        break; // We found our setting variable name index
    }
    if (request.GetCursorIndex() == setting_var_idx) {
      // Attempting to complete setting variable name
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eSettingsNameCompletion, request,
          nullptr);
      return;
    }
    arg = request.GetParsedLine().GetArgumentAtIndex(request.GetCursorIndex());

    if (!arg)
      return;

    // Complete option name
    if (arg[0] == '-')
      return;

    // Complete setting value
    const char *setting_var_name =
        request.GetParsedLine().GetArgumentAtIndex(setting_var_idx);
    Status error;
    lldb::OptionValueSP value_sp(
        GetDebugger().GetPropertyValue(&m_exe_ctx, setting_var_name, error));
    if (!value_sp)
      return;
    value_sp->AutoComplete(m_interpreter, request);
  }

protected:
  void DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    Args cmd_args(command);

    // Process possible options.
    if (!ParseOptions(cmd_args, result))
      return;

    const size_t min_argc = m_options.m_force ? 1 : 2;
    const size_t argc = cmd_args.GetArgumentCount();

    if ((argc < min_argc) && (!m_options.m_global)) {
      result.AppendError("'settings set' takes more arguments");
      return;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError(
          "'settings set' command requires a valid variable name");
      return;
    }

    // A missing value corresponds to clearing the setting when "force" is
    // specified.
    if (argc == 1 && m_options.m_force) {
      Status error(GetDebugger().SetPropertyValue(
          &m_exe_ctx, eVarSetOperationClear, var_name, llvm::StringRef()));
      if (error.Fail()) {
        result.AppendError(error.AsCString());
      }
      return;
    }

    // Split the raw command into var_name and value pair.
    llvm::StringRef var_value(command);
    var_value = var_value.split(var_name).second.ltrim();

    Status error;
    if (m_options.m_global)
      error = GetDebugger().SetPropertyValue(nullptr, eVarSetOperationAssign,
                                             var_name, var_value);

    if (error.Success()) {
      // FIXME this is the same issue as the one in commands script import
      // we could be setting target.load-script-from-symbol-file which would
      // cause Python scripts to be loaded, which could run LLDB commands (e.g.
      // settings set target.process.python-os-plugin-path) and cause a crash
      // if we did not clear the command's exe_ctx first
      ExecutionContext exe_ctx(m_exe_ctx);
      m_exe_ctx.Clear();
      error = GetDebugger().SetPropertyValue(&exe_ctx, eVarSetOperationAssign,
                                             var_name, var_value);
    }

    if (error.Fail() && !m_options.m_exists) {
      result.AppendError(error.AsCString());
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }

private:
  CommandOptions m_options;
};

// CommandObjectSettingsShow -- Show current values

class CommandObjectSettingsShow : public CommandObjectParsed {
public:
  CommandObjectSettingsShow(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "settings show",
                            "Show matching debugger settings and their current "
                            "values.  Defaults to showing all settings.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeSettingVariableName, eArgRepeatOptional);
  }

  ~CommandObjectSettingsShow() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishResult);

    if (!args.empty()) {
      for (const auto &arg : args) {
        Status error(GetDebugger().DumpPropertyValue(
            &m_exe_ctx, result.GetOutputStream(), arg.ref(),
            OptionValue::eDumpGroupValue));
        if (error.Success()) {
          result.GetOutputStream().EOL();
        } else {
          result.AppendError(error.AsCString());
        }
      }
    } else {
      GetDebugger().DumpAllPropertyValues(&m_exe_ctx, result.GetOutputStream(),
                                          OptionValue::eDumpGroupValue);
    }
  }
};

// CommandObjectSettingsWrite -- Write settings to file
#define LLDB_OPTIONS_settings_write
#include "CommandOptions.inc"

class CommandObjectSettingsWrite : public CommandObjectParsed {
public:
  CommandObjectSettingsWrite(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "settings export",
            "Write matching debugger settings and their "
            "current values to a file that can be read in with "
            "\"settings read\". Defaults to writing all settings.",
            nullptr) {
    AddSimpleArgumentList(eArgTypeSettingVariableName, eArgRepeatOptional);
  }

  ~CommandObjectSettingsWrite() override = default;

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
      case 'f':
        m_filename.assign(std::string(option_arg));
        break;
      case 'a':
        m_append = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_filename.clear();
      m_append = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_settings_write_options);
    }

    // Instance variables to hold the values for command options.
    std::string m_filename;
    bool m_append = false;
  };

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    FileSpec file_spec(m_options.m_filename);
    FileSystem::Instance().Resolve(file_spec);
    std::string path(file_spec.GetPath());
    auto options = File::eOpenOptionWriteOnly | File::eOpenOptionCanCreate;
    if (m_options.m_append)
      options |= File::eOpenOptionAppend;
    else
      options |= File::eOpenOptionTruncate;

    StreamFile out_file(path.c_str(), options,
                        lldb::eFilePermissionsFileDefault);

    if (!out_file.GetFile().IsValid()) {
      result.AppendErrorWithFormat("%s: unable to write to file", path.c_str());
      return;
    }

    // Exporting should not be context sensitive.
    ExecutionContext clean_ctx;

    if (args.empty()) {
      GetDebugger().DumpAllPropertyValues(&clean_ctx, out_file,
                                          OptionValue::eDumpGroupExport);
      return;
    }

    for (const auto &arg : args) {
      Status error(GetDebugger().DumpPropertyValue(
          &clean_ctx, out_file, arg.ref(), OptionValue::eDumpGroupExport));
      if (!error.Success()) {
        result.AppendError(error.AsCString());
      }
    }
  }

private:
  CommandOptions m_options;
};

// CommandObjectSettingsRead -- Read settings from file
#define LLDB_OPTIONS_settings_read
#include "CommandOptions.inc"

class CommandObjectSettingsRead : public CommandObjectParsed {
public:
  CommandObjectSettingsRead(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "settings read",
            "Read settings previously saved to a file with \"settings write\".",
            nullptr) {}

  ~CommandObjectSettingsRead() override = default;

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
      case 'f':
        m_filename.assign(std::string(option_arg));
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_filename.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_settings_read_options);
    }

    // Instance variables to hold the values for command options.
    std::string m_filename;
  };

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    FileSpec file(m_options.m_filename);
    FileSystem::Instance().Resolve(file);
    CommandInterpreterRunOptions options;
    options.SetAddToHistory(false);
    options.SetEchoCommands(false);
    options.SetPrintResults(true);
    options.SetPrintErrors(true);
    options.SetStopOnError(false);
    m_interpreter.HandleCommandsFromFile(file, options, result);
  }

private:
  CommandOptions m_options;
};

// CommandObjectSettingsList -- List settable variables

class CommandObjectSettingsList : public CommandObjectParsed {
public:
  CommandObjectSettingsList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "settings list",
                            "List and describe matching debugger settings.  "
                            "Defaults to all listing all settings.",
                            nullptr) {
    CommandArgumentEntry arg;
    CommandArgumentData var_name_arg;
    CommandArgumentData prefix_name_arg;

    // Define the first variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatOptional;

    // Define the second variant of this arg.
    prefix_name_arg.arg_type = eArgTypeSettingPrefix;
    prefix_name_arg.arg_repetition = eArgRepeatOptional;

    arg.push_back(var_name_arg);
    arg.push_back(prefix_name_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectSettingsList() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eSettingsNameCompletion, request,
        nullptr);
  }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishResult);

    const size_t argc = args.GetArgumentCount();
    if (argc > 0) {
      const bool dump_qualified_name = true;

      for (const Args::ArgEntry &arg : args) {
        const char *property_path = arg.c_str();

        const Property *property =
            GetDebugger().GetValueProperties()->GetPropertyAtPath(
                &m_exe_ctx, property_path);

        if (property) {
          property->DumpDescription(m_interpreter, result.GetOutputStream(), 0,
                                    dump_qualified_name);
        } else {
          result.AppendErrorWithFormat("invalid property path '%s'",
                                       property_path);
        }
      }
    } else {
      GetDebugger().DumpAllDescriptions(m_interpreter,
                                        result.GetOutputStream());
    }
  }
};

// CommandObjectSettingsRemove

class CommandObjectSettingsRemove : public CommandObjectRaw {
public:
  CommandObjectSettingsRemove(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings remove",
                         "Remove a value from a setting, specified by array "
                         "index or dictionary key.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData var_name_arg;
    CommandArgumentData index_arg;
    CommandArgumentData key_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first variant of this arg.
    index_arg.arg_type = eArgTypeSettingIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // Define the second variant of this arg.
    key_arg.arg_type = eArgTypeSettingKey;
    key_arg.arg_repetition = eArgRepeatPlain;

    // Push both variants into this arg
    arg2.push_back(index_arg);
    arg2.push_back(key_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectSettingsRemove() override = default;

  bool WantsCompletion() override { return true; }

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex() < 2)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eSettingsNameCompletion, request,
          nullptr);
  }

protected:
  void DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    Args cmd_args(command);

    // Process possible options.
    if (!ParseOptions(cmd_args, result))
      return;

    const size_t argc = cmd_args.GetArgumentCount();
    if (argc == 0) {
      result.AppendError("'settings remove' takes an array or dictionary item, "
                         "or an array followed by one or more indexes, or a "
                         "dictionary followed by one or more key names to "
                         "remove");
      return;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError(
          "'settings remove' command requires a valid variable name");
      return;
    }

    // Split the raw command into var_name and value pair.
    llvm::StringRef var_value(command);
    var_value = var_value.split(var_name).second.trim();

    Status error(GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationRemove, var_name, var_value));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
    }
  }
};

// CommandObjectSettingsReplace

class CommandObjectSettingsReplace : public CommandObjectRaw {
public:
  CommandObjectSettingsReplace(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings replace",
                         "Replace the debugger setting value specified by "
                         "array index or dictionary key.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentEntry arg3;
    CommandArgumentData var_name_arg;
    CommandArgumentData index_arg;
    CommandArgumentData key_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (variant of this arg.
    index_arg.arg_type = eArgTypeSettingIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // Define the second (variant of this arg.
    key_arg.arg_type = eArgTypeSettingKey;
    key_arg.arg_repetition = eArgRepeatPlain;

    // Put both variants into this arg
    arg2.push_back(index_arg);
    arg2.push_back(key_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg3.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
    m_arguments.push_back(arg3);
  }

  ~CommandObjectSettingsReplace() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eSettingsNameCompletion, request,
          nullptr);
  }

protected:
  void DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    Args cmd_args(command);
    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings replace' command requires a valid variable "
                         "name; No value supplied");
      return;
    }

    // Split the raw command into var_name, index_value, and value triple.
    llvm::StringRef var_value(command);
    var_value = var_value.split(var_name).second.trim();

    Status error(GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationReplace, var_name, var_value));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
    } else {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    }
  }
};

// CommandObjectSettingsInsertBefore

class CommandObjectSettingsInsertBefore : public CommandObjectRaw {
public:
  CommandObjectSettingsInsertBefore(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings insert-before",
                         "Insert one or more values into an debugger array "
                         "setting immediately before the specified element "
                         "index.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentEntry arg3;
    CommandArgumentData var_name_arg;
    CommandArgumentData index_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (variant of this arg.
    index_arg.arg_type = eArgTypeSettingIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(index_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg3.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
    m_arguments.push_back(arg3);
  }

  ~CommandObjectSettingsInsertBefore() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eSettingsNameCompletion, request,
          nullptr);
  }

protected:
  void DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    Args cmd_args(command);
    const size_t argc = cmd_args.GetArgumentCount();

    if (argc < 3) {
      result.AppendError("'settings insert-before' takes more arguments");
      return;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings insert-before' command requires a valid "
                         "variable name; No value supplied");
      return;
    }

    // Split the raw command into var_name, index_value, and value triple.
    llvm::StringRef var_value(command);
    var_value = var_value.split(var_name).second.trim();

    Status error(GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationInsertBefore, var_name, var_value));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
    }
  }
};

// CommandObjectSettingInsertAfter

class CommandObjectSettingsInsertAfter : public CommandObjectRaw {
public:
  CommandObjectSettingsInsertAfter(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings insert-after",
                         "Insert one or more values into a debugger array "
                         "settings after the specified element index.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentEntry arg3;
    CommandArgumentData var_name_arg;
    CommandArgumentData index_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (variant of this arg.
    index_arg.arg_type = eArgTypeSettingIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(index_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg3.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
    m_arguments.push_back(arg3);
  }

  ~CommandObjectSettingsInsertAfter() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eSettingsNameCompletion, request,
          nullptr);
  }

protected:
  void DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    Args cmd_args(command);
    const size_t argc = cmd_args.GetArgumentCount();

    if (argc < 3) {
      result.AppendError("'settings insert-after' takes more arguments");
      return;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings insert-after' command requires a valid "
                         "variable name; No value supplied");
      return;
    }

    // Split the raw command into var_name, index_value, and value triple.
    llvm::StringRef var_value(command);
    var_value = var_value.split(var_name).second.trim();

    Status error(GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationInsertAfter, var_name, var_value));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
    }
  }
};

// CommandObjectSettingsAppend

class CommandObjectSettingsAppend : public CommandObjectRaw {
public:
  CommandObjectSettingsAppend(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "settings append",
                         "Append one or more values to a debugger array, "
                         "dictionary, or string setting.") {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData var_name_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeSettingVariableName;
    var_name_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(var_name_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectSettingsAppend() override = default;

  // Overrides base class's behavior where WantsCompletion =
  // !WantsRawCommandString.
  bool WantsCompletion() override { return true; }

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eSettingsNameCompletion, request,
          nullptr);
  }

protected:
  void DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    Args cmd_args(command);
    const size_t argc = cmd_args.GetArgumentCount();

    if (argc < 2) {
      result.AppendError("'settings append' takes more arguments");
      return;
    }

    const char *var_name = cmd_args.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings append' command requires a valid variable "
                         "name; No value supplied");
      return;
    }

    // Do not perform cmd_args.Shift() since StringRef is manipulating the raw
    // character string later on.

    // Split the raw command into var_name and value pair.
    llvm::StringRef var_value(command);
    var_value = var_value.split(var_name).second.trim();

    Status error(GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationAppend, var_name, var_value));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
    }
  }
};

// CommandObjectSettingsClear
#define LLDB_OPTIONS_settings_clear
#include "CommandOptions.inc"

class CommandObjectSettingsClear : public CommandObjectParsed {
public:
  CommandObjectSettingsClear(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "settings clear",
            "Clear a debugger setting array, dictionary, or string. "
            "If '-a' option is specified, it clears all settings.", nullptr) {
    AddSimpleArgumentList(eArgTypeSettingVariableName);
  }

  ~CommandObjectSettingsClear() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    // Attempting to complete variable name
    if (request.GetCursorIndex() < 2)
      lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
          GetCommandInterpreter(), lldb::eSettingsNameCompletion, request,
          nullptr);
  }

   Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 'a':
        m_clear_all = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return Status();
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_clear_all = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_settings_clear_options);
    }

    bool m_clear_all = false;
  };

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    const size_t argc = command.GetArgumentCount();

    if (m_options.m_clear_all) {
      if (argc != 0) {
        result.AppendError("'settings clear --all' doesn't take any arguments");
        return;
      }
      GetDebugger().GetValueProperties()->Clear();
      return;
    }

    if (argc != 1) {
      result.AppendError("'settings clear' takes exactly one argument");
      return;
    }

    const char *var_name = command.GetArgumentAtIndex(0);
    if ((var_name == nullptr) || (var_name[0] == '\0')) {
      result.AppendError("'settings clear' command requires a valid variable "
                         "name; No value supplied");
      return;
    }

    Status error(GetDebugger().SetPropertyValue(
        &m_exe_ctx, eVarSetOperationClear, var_name, llvm::StringRef()));
    if (error.Fail()) {
      result.AppendError(error.AsCString());
    }
  }

  private:
    CommandOptions m_options;
};

// CommandObjectMultiwordSettings

CommandObjectMultiwordSettings::CommandObjectMultiwordSettings(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "settings",
                             "Commands for managing LLDB settings.",
                             "settings <subcommand> [<command-options>]") {
  LoadSubCommand("set",
                 CommandObjectSP(new CommandObjectSettingsSet(interpreter)));
  LoadSubCommand("show",
                 CommandObjectSP(new CommandObjectSettingsShow(interpreter)));
  LoadSubCommand("list",
                 CommandObjectSP(new CommandObjectSettingsList(interpreter)));
  LoadSubCommand("remove",
                 CommandObjectSP(new CommandObjectSettingsRemove(interpreter)));
  LoadSubCommand("replace", CommandObjectSP(
                                new CommandObjectSettingsReplace(interpreter)));
  LoadSubCommand(
      "insert-before",
      CommandObjectSP(new CommandObjectSettingsInsertBefore(interpreter)));
  LoadSubCommand(
      "insert-after",
      CommandObjectSP(new CommandObjectSettingsInsertAfter(interpreter)));
  LoadSubCommand("append",
                 CommandObjectSP(new CommandObjectSettingsAppend(interpreter)));
  LoadSubCommand("clear",
                 CommandObjectSP(new CommandObjectSettingsClear(interpreter)));
  LoadSubCommand("write",
                 CommandObjectSP(new CommandObjectSettingsWrite(interpreter)));
  LoadSubCommand("read",
                 CommandObjectSP(new CommandObjectSettingsRead(interpreter)));
}

CommandObjectMultiwordSettings::~CommandObjectMultiwordSettings() = default;
