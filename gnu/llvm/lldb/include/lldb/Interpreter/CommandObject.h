//===-- CommandObject.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_COMMANDOBJECT_H
#define LLDB_INTERPRETER_COMMANDOBJECT_H

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lldb/Utility/Flags.h"

#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/StringList.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

// This function really deals with CommandObjectLists, but we didn't make a
// CommandObjectList class, so I'm sticking it here.  But we really should have
// such a class.  Anyway, it looks up the commands in the map that match the
// partial string cmd_str, inserts the matches into matches, and returns the
// number added.

template <typename ValueType>
int AddNamesMatchingPartialString(
    const std::map<std::string, ValueType> &in_map, llvm::StringRef cmd_str,
    StringList &matches, StringList *descriptions = nullptr) {
  int number_added = 0;

  const bool add_all = cmd_str.empty();

  for (auto iter = in_map.begin(), end = in_map.end(); iter != end; iter++) {
    if (add_all || (iter->first.find(std::string(cmd_str), 0) == 0)) {
      ++number_added;
      matches.AppendString(iter->first.c_str());
      if (descriptions)
        descriptions->AppendString(iter->second->GetHelp());
    }
  }

  return number_added;
}

template <typename ValueType>
size_t FindLongestCommandWord(std::map<std::string, ValueType> &dict) {
  auto end = dict.end();
  size_t max_len = 0;

  for (auto pos = dict.begin(); pos != end; ++pos) {
    size_t len = pos->first.size();
    if (max_len < len)
      max_len = len;
  }
  return max_len;
}

class CommandObject : public std::enable_shared_from_this<CommandObject> {
public:
  typedef llvm::StringRef(ArgumentHelpCallbackFunction)();

  struct ArgumentHelpCallback {
    ArgumentHelpCallbackFunction *help_callback;
    bool self_formatting;

    llvm::StringRef operator()() const { return (*help_callback)(); }

    explicit operator bool() const { return (help_callback != nullptr); }
  };

  /// Entries in the main argument information table.
  struct ArgumentTableEntry {
    lldb::CommandArgumentType arg_type;
    const char *arg_name;
    lldb::CompletionType completion_type;
    OptionEnumValues enum_values;
    ArgumentHelpCallback help_function;
    const char *help_text;
  };

  /// Used to build individual command argument lists.
  struct CommandArgumentData {
    lldb::CommandArgumentType arg_type;
    ArgumentRepetitionType arg_repetition;
    /// This arg might be associated only with some particular option set(s). By
    /// default the arg associates to all option sets.
    uint32_t arg_opt_set_association;

    CommandArgumentData(lldb::CommandArgumentType type = lldb::eArgTypeNone,
                        ArgumentRepetitionType repetition = eArgRepeatPlain,
                        uint32_t opt_set = LLDB_OPT_SET_ALL)
        : arg_type(type), arg_repetition(repetition),
          arg_opt_set_association(opt_set) {}
  };

  typedef std::vector<CommandArgumentData>
      CommandArgumentEntry; // Used to build individual command argument lists

  typedef std::map<std::string, lldb::CommandObjectSP> CommandMap;

  CommandObject(CommandInterpreter &interpreter, llvm::StringRef name,
    llvm::StringRef help = "", llvm::StringRef syntax = "",
                uint32_t flags = 0);

  virtual ~CommandObject() = default;

  static const char *
  GetArgumentTypeAsCString(const lldb::CommandArgumentType arg_type);

  static const char *
  GetArgumentDescriptionAsCString(const lldb::CommandArgumentType arg_type);

  CommandInterpreter &GetCommandInterpreter() { return m_interpreter; }
  Debugger &GetDebugger();

  virtual llvm::StringRef GetHelp();

  virtual llvm::StringRef GetHelpLong();

  virtual llvm::StringRef GetSyntax();

  llvm::StringRef GetCommandName() const;

  virtual void SetHelp(llvm::StringRef str);

  virtual void SetHelpLong(llvm::StringRef str);

  void SetSyntax(llvm::StringRef str);

  // override this to return true if you want to enable the user to delete the
  // Command object from the Command dictionary (aliases have their own
  // deletion scheme, so they do not need to care about this)
  virtual bool IsRemovable() const { return false; }

  virtual bool IsMultiwordObject() { return false; }

  bool IsUserCommand() { return m_is_user_command; }

  void SetIsUserCommand(bool is_user) { m_is_user_command = is_user; }

  virtual CommandObjectMultiword *GetAsMultiwordCommand() { return nullptr; }

  virtual bool IsAlias() { return false; }

  // override this to return true if your command is somehow a "dash-dash" form
  // of some other command (e.g. po is expr -O --); this is a powerful hint to
  // the help system that one cannot pass options to this command
  virtual bool IsDashDashCommand() { return false; }

  virtual lldb::CommandObjectSP GetSubcommandSP(llvm::StringRef sub_cmd,
                                                StringList *matches = nullptr) {
    return lldb::CommandObjectSP();
  }

  virtual lldb::CommandObjectSP GetSubcommandSPExact(llvm::StringRef sub_cmd) {
    return lldb::CommandObjectSP();
  }

  virtual CommandObject *GetSubcommandObject(llvm::StringRef sub_cmd,
                                             StringList *matches = nullptr) {
    return nullptr;
  }

  void FormatLongHelpText(Stream &output_strm, llvm::StringRef long_help);

  void GenerateHelpText(CommandReturnObject &result);

  virtual void GenerateHelpText(Stream &result);

  // this is needed in order to allow the SBCommand class to transparently try
  // and load subcommands - it will fail on anything but a multiword command,
  // but it avoids us doing type checkings and casts
  virtual bool LoadSubCommand(llvm::StringRef cmd_name,
                              const lldb::CommandObjectSP &command_obj) {
    return false;
  }

  virtual llvm::Error LoadUserSubcommand(llvm::StringRef cmd_name,
                                         const lldb::CommandObjectSP &command_obj,
                                         bool can_replace) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                              "can only add commands to container commands");
  }

  virtual bool WantsRawCommandString() = 0;

  // By default, WantsCompletion = !WantsRawCommandString. Subclasses who want
  // raw command string but desire, for example, argument completion should
  // override this method to return true.
  virtual bool WantsCompletion() { return !WantsRawCommandString(); }

  virtual Options *GetOptions();

  static lldb::CommandArgumentType LookupArgumentName(llvm::StringRef arg_name);

  static const ArgumentTableEntry *
  FindArgumentDataByType(lldb::CommandArgumentType arg_type);

  // Sets the argument list for this command to one homogenous argument type,
  // with the repeat specified.
  void AddSimpleArgumentList(
      lldb::CommandArgumentType arg_type,
      ArgumentRepetitionType repetition_type = eArgRepeatPlain);

  // Helper function to set BP IDs or ID ranges as the command argument data
  // for this command.
  // This used to just populate an entry you could add to, but that was never
  // used.  If we ever need that we can take optional extra args here.
  // Use this to define a simple argument list:
  enum IDType { eBreakpointArgs = 0, eWatchpointArgs = 1 };
  void AddIDsArgumentData(IDType type);

  int GetNumArgumentEntries();

  CommandArgumentEntry *GetArgumentEntryAtIndex(int idx);

  static void GetArgumentHelp(Stream &str, lldb::CommandArgumentType arg_type,
                              CommandInterpreter &interpreter);

  static const char *GetArgumentName(lldb::CommandArgumentType arg_type);

  // Generates a nicely formatted command args string for help command output.
  // By default, all possible args are taken into account, for example, '<expr
  // | variable-name>'.  This can be refined by passing a second arg specifying
  // which option set(s) we are interested, which could then, for example,
  // produce either '<expr>' or '<variable-name>'.
  void GetFormattedCommandArguments(Stream &str,
                                    uint32_t opt_set_mask = LLDB_OPT_SET_ALL);

  static bool IsPairType(ArgumentRepetitionType arg_repeat_type);

  static std::optional<ArgumentRepetitionType> 
    ArgRepetitionFromString(llvm::StringRef string);

  bool ParseOptions(Args &args, CommandReturnObject &result);

  void SetCommandName(llvm::StringRef name);

  /// This default version handles calling option argument completions and then
  /// calls HandleArgumentCompletion if the cursor is on an argument, not an
  /// option. Don't override this method, override HandleArgumentCompletion
  /// instead unless you have special reasons.
  ///
  /// \param[in,out] request
  ///    The completion request that needs to be answered.
  virtual void HandleCompletion(CompletionRequest &request);

  /// The default version handles argument definitions that have only one
  /// argument type, and use one of the argument types that have an entry in
  /// the CommonCompletions.  Override this if you have a more complex
  /// argument setup.
  /// FIXME: we should be able to extend this to more complex argument
  /// definitions provided we have completers for all the argument types.
  ///
  /// The input array contains a parsed version of the line.
  ///
  /// We've constructed the map of options and their arguments as well if that
  /// is helpful for the completion.
  ///
  /// \param[in,out] request
  ///    The completion request that needs to be answered.
  virtual void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector);

  bool HelpTextContainsWord(llvm::StringRef search_word,
                            bool search_short_help = true,
                            bool search_long_help = true,
                            bool search_syntax = true,
                            bool search_options = true);

  /// The flags accessor.
  ///
  /// \return
  ///     A reference to the Flags member variable.
  Flags &GetFlags() { return m_flags; }

  /// The flags const accessor.
  ///
  /// \return
  ///     A const reference to the Flags member variable.
  const Flags &GetFlags() const { return m_flags; }

  /// Get the command that appropriate for a "repeat" of the current command.
  ///
  /// \param[in] current_command_args
  ///    The command arguments.
  ///
  /// \param[in] index
  ///    This is for internal use - it is how the completion request is tracked
  ///    in CommandObjectMultiword, and should otherwise be ignored.
  ///
  /// \return
  ///     std::nullopt if there is no special repeat command - it will use the
  ///     current command line.
  ///     Otherwise a std::string containing the command to be repeated.
  ///     If the string is empty, the command won't be allow repeating.
  virtual std::optional<std::string>
  GetRepeatCommand(Args &current_command_args, uint32_t index) {
    return std::nullopt;
  }

  bool HasOverrideCallback() const {
    return m_command_override_callback ||
           m_deprecated_command_override_callback;
  }

  void SetOverrideCallback(lldb::CommandOverrideCallback callback,
                           void *baton) {
    m_deprecated_command_override_callback = callback;
    m_command_override_baton = baton;
  }

  void
  SetOverrideCallback(lldb_private::CommandOverrideCallbackWithResult callback,
                      void *baton) {
    m_command_override_callback = callback;
    m_command_override_baton = baton;
  }

  bool InvokeOverrideCallback(const char **argv, CommandReturnObject &result) {
    if (m_command_override_callback)
      return m_command_override_callback(m_command_override_baton, argv,
                                         result);
    else if (m_deprecated_command_override_callback)
      return m_deprecated_command_override_callback(m_command_override_baton,
                                                    argv);
    else
      return false;
  }

  virtual void Execute(const char *args_string,
                       CommandReturnObject &result) = 0;

protected:
  bool ParseOptionsAndNotify(Args &args, CommandReturnObject &result,
                             OptionGroupOptions &group_options,
                             ExecutionContext &exe_ctx);

  virtual const char *GetInvalidTargetDescription() {
    return "invalid target, create a target using the 'target create' command";
  }

  virtual const char *GetInvalidProcessDescription() {
    return "Command requires a current process.";
  }

  virtual const char *GetInvalidThreadDescription() {
    return "Command requires a process which is currently stopped.";
  }

  virtual const char *GetInvalidFrameDescription() {
    return "Command requires a process, which is currently stopped.";
  }

  virtual const char *GetInvalidRegContextDescription() {
    return "invalid frame, no registers, command requires a process which is "
           "currently stopped.";
  }

  // This is for use in the command interpreter, when you either want the
  // selected target, or if no target is present you want to prime the dummy
  // target with entities that will be copied over to new targets.
  Target &GetSelectedOrDummyTarget(bool prefer_dummy = false);
  Target &GetSelectedTarget();
  Target &GetDummyTarget();

  // If a command needs to use the "current" thread, use this call. Command
  // objects will have an ExecutionContext to use, and that may or may not have
  // a thread in it.  If it does, you should use that by default, if not, then
  // use the ExecutionContext's target's selected thread, etc... This call
  // insulates you from the details of this calculation.
  Thread *GetDefaultThread();

  /// Check the command to make sure anything required by this
  /// command is available.
  ///
  /// \param[out] result
  ///     A command result object, if it is not okay to run the command
  ///     this will be filled in with a suitable error.
  ///
  /// \return
  ///     \b true if it is okay to run this command, \b false otherwise.
  bool CheckRequirements(CommandReturnObject &result);

  void Cleanup();

  CommandInterpreter &m_interpreter;
  ExecutionContext m_exe_ctx;
  std::unique_lock<std::recursive_mutex> m_api_locker;
  std::string m_cmd_name;
  std::string m_cmd_help_short;
  std::string m_cmd_help_long;
  std::string m_cmd_syntax;
  Flags m_flags;
  std::vector<CommandArgumentEntry> m_arguments;
  lldb::CommandOverrideCallback m_deprecated_command_override_callback;
  lldb_private::CommandOverrideCallbackWithResult m_command_override_callback;
  void *m_command_override_baton;
  bool m_is_user_command = false;
};

class CommandObjectParsed : public CommandObject {
public:
  CommandObjectParsed(CommandInterpreter &interpreter, const char *name,
                      const char *help = nullptr, const char *syntax = nullptr,
                      uint32_t flags = 0)
      : CommandObject(interpreter, name, help, syntax, flags) {}

  ~CommandObjectParsed() override = default;

  void Execute(const char *args_string, CommandReturnObject &result) override;

protected:
  virtual void DoExecute(Args &command, CommandReturnObject &result) = 0;

  bool WantsRawCommandString() override { return false; }
};

class CommandObjectRaw : public CommandObject {
public:
  CommandObjectRaw(CommandInterpreter &interpreter, llvm::StringRef name,
    llvm::StringRef help = "", llvm::StringRef syntax = "",
                   uint32_t flags = 0)
      : CommandObject(interpreter, name, help, syntax, flags) {}

  ~CommandObjectRaw() override = default;

  void Execute(const char *args_string, CommandReturnObject &result) override;

protected:
  virtual void DoExecute(llvm::StringRef command,
                         CommandReturnObject &result) = 0;

  bool WantsRawCommandString() override { return true; }
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_COMMANDOBJECT_H
