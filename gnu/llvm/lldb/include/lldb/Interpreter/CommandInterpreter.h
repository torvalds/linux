//===-- CommandInterpreter.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_COMMANDINTERPRETER_H
#define LLDB_INTERPRETER_COMMANDINTERPRETER_H

#include "lldb/Core/Debugger.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/Interpreter/CommandAlias.h"
#include "lldb/Interpreter/CommandHistory.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

#include <mutex>
#include <optional>
#include <stack>
#include <unordered_map>

namespace lldb_private {
class CommandInterpreter;

class CommandInterpreterRunResult {
public:
  CommandInterpreterRunResult() = default;

  uint32_t GetNumErrors() const { return m_num_errors; }

  lldb::CommandInterpreterResult GetResult() const { return m_result; }

  bool IsResult(lldb::CommandInterpreterResult result) {
    return m_result == result;
  }

protected:
  friend CommandInterpreter;

  void IncrementNumberOfErrors() { m_num_errors++; }

  void SetResult(lldb::CommandInterpreterResult result) { m_result = result; }

private:
  int m_num_errors = 0;
  lldb::CommandInterpreterResult m_result =
      lldb::eCommandInterpreterResultSuccess;
};

class CommandInterpreterRunOptions {
public:
  /// Construct a CommandInterpreterRunOptions object. This class is used to
  /// control all the instances where we run multiple commands, e.g.
  /// HandleCommands, HandleCommandsFromFile, RunCommandInterpreter.
  ///
  /// The meanings of the options in this object are:
  ///
  /// \param[in] stop_on_continue
  ///    If \b true, execution will end on the first command that causes the
  ///    process in the execution context to continue. If \b false, we won't
  ///    check the execution status.
  /// \param[in] stop_on_error
  ///    If \b true, execution will end on the first command that causes an
  ///    error.
  /// \param[in] stop_on_crash
  ///    If \b true, when a command causes the target to run, and the end of the
  ///    run is a signal or exception, stop executing the commands.
  /// \param[in] echo_commands
  ///    If \b true, echo the command before executing it. If \b false, execute
  ///    silently.
  /// \param[in] echo_comments
  ///    If \b true, echo command even if it is a pure comment line. If
  ///    \b false, print no ouput in this case. This setting has an effect only
  ///    if echo_commands is \b true.
  /// \param[in] print_results
  ///    If \b true and the command succeeds, print the results of the command
  ///    after executing it. If \b false, execute silently.
  /// \param[in] print_errors
  ///    If \b true and the command fails, print the results of the command
  ///    after executing it. If \b false, execute silently.
  /// \param[in] add_to_history
  ///    If \b true add the commands to the command history. If \b false, don't
  ///    add them.
  /// \param[in] handle_repeats
  ///    If \b true then treat empty lines as repeat commands even if the
  ///    interpreter is non-interactive.
  CommandInterpreterRunOptions(LazyBool stop_on_continue,
                               LazyBool stop_on_error, LazyBool stop_on_crash,
                               LazyBool echo_commands, LazyBool echo_comments,
                               LazyBool print_results, LazyBool print_errors,
                               LazyBool add_to_history,
                               LazyBool handle_repeats)
      : m_stop_on_continue(stop_on_continue), m_stop_on_error(stop_on_error),
        m_stop_on_crash(stop_on_crash), m_echo_commands(echo_commands),
        m_echo_comment_commands(echo_comments), m_print_results(print_results),
        m_print_errors(print_errors), m_add_to_history(add_to_history),
        m_allow_repeats(handle_repeats) {}

  CommandInterpreterRunOptions() = default;

  void SetSilent(bool silent) {
    LazyBool value = silent ? eLazyBoolNo : eLazyBoolYes;

    m_print_results = value;
    m_print_errors = value;
    m_echo_commands = value;
    m_echo_comment_commands = value;
    m_add_to_history = value;
  }
  // These return the default behaviors if the behavior is not
  // eLazyBoolCalculate. But I've also left the ivars public since for
  // different ways of running the interpreter you might want to force
  // different defaults...  In that case, just grab the LazyBool ivars directly
  // and do what you want with eLazyBoolCalculate.
  bool GetStopOnContinue() const { return DefaultToNo(m_stop_on_continue); }

  void SetStopOnContinue(bool stop_on_continue) {
    m_stop_on_continue = stop_on_continue ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetStopOnError() const { return DefaultToNo(m_stop_on_error); }

  void SetStopOnError(bool stop_on_error) {
    m_stop_on_error = stop_on_error ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetStopOnCrash() const { return DefaultToNo(m_stop_on_crash); }

  void SetStopOnCrash(bool stop_on_crash) {
    m_stop_on_crash = stop_on_crash ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetEchoCommands() const { return DefaultToYes(m_echo_commands); }

  void SetEchoCommands(bool echo_commands) {
    m_echo_commands = echo_commands ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetEchoCommentCommands() const {
    return DefaultToYes(m_echo_comment_commands);
  }

  void SetEchoCommentCommands(bool echo_comments) {
    m_echo_comment_commands = echo_comments ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetPrintResults() const { return DefaultToYes(m_print_results); }

  void SetPrintResults(bool print_results) {
    m_print_results = print_results ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetPrintErrors() const { return DefaultToYes(m_print_errors); }

  void SetPrintErrors(bool print_errors) {
    m_print_errors = print_errors ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetAddToHistory() const { return DefaultToYes(m_add_to_history); }

  void SetAddToHistory(bool add_to_history) {
    m_add_to_history = add_to_history ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetAutoHandleEvents() const {
    return DefaultToYes(m_auto_handle_events);
  }

  void SetAutoHandleEvents(bool auto_handle_events) {
    m_auto_handle_events = auto_handle_events ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetSpawnThread() const { return DefaultToNo(m_spawn_thread); }

  void SetSpawnThread(bool spawn_thread) {
    m_spawn_thread = spawn_thread ? eLazyBoolYes : eLazyBoolNo;
  }

  bool GetAllowRepeats() const { return DefaultToNo(m_allow_repeats); }

  void SetAllowRepeats(bool allow_repeats) {
    m_allow_repeats = allow_repeats ? eLazyBoolYes : eLazyBoolNo;
  }

  LazyBool m_stop_on_continue = eLazyBoolCalculate;
  LazyBool m_stop_on_error = eLazyBoolCalculate;
  LazyBool m_stop_on_crash = eLazyBoolCalculate;
  LazyBool m_echo_commands = eLazyBoolCalculate;
  LazyBool m_echo_comment_commands = eLazyBoolCalculate;
  LazyBool m_print_results = eLazyBoolCalculate;
  LazyBool m_print_errors = eLazyBoolCalculate;
  LazyBool m_add_to_history = eLazyBoolCalculate;
  LazyBool m_auto_handle_events;
  LazyBool m_spawn_thread;
  LazyBool m_allow_repeats = eLazyBoolCalculate;

private:
  static bool DefaultToYes(LazyBool flag) {
    switch (flag) {
    case eLazyBoolNo:
      return false;
    default:
      return true;
    }
  }

  static bool DefaultToNo(LazyBool flag) {
    switch (flag) {
    case eLazyBoolYes:
      return true;
    default:
      return false;
    }
  }
};

class CommandInterpreter : public Broadcaster,
                           public Properties,
                           public IOHandlerDelegate {
public:
  enum {
    eBroadcastBitThreadShouldExit = (1 << 0),
    eBroadcastBitResetPrompt = (1 << 1),
    eBroadcastBitQuitCommandReceived = (1 << 2), // User entered quit
    eBroadcastBitAsynchronousOutputData = (1 << 3),
    eBroadcastBitAsynchronousErrorData = (1 << 4)
  };

  /// Tristate boolean to manage children omission warnings.
  enum ChildrenOmissionWarningStatus {
    eNoOmission = 0,       ///< No children were omitted.
    eUnwarnedOmission = 1, ///< Children omitted, and not yet notified.
    eWarnedOmission = 2    ///< Children omitted and notified.
  };

  enum CommandTypes {
    eCommandTypesBuiltin = 0x0001, //< native commands such as "frame"
    eCommandTypesUserDef = 0x0002, //< scripted commands
    eCommandTypesUserMW  = 0x0004, //< multiword commands (command containers)
    eCommandTypesAliases = 0x0008, //< aliases such as "po"
    eCommandTypesHidden  = 0x0010, //< commands prefixed with an underscore
    eCommandTypesAllThem = 0xFFFF  //< all commands
  };

  // The CommandAlias and CommandInterpreter both have a hand in 
  // substituting for alias commands.  They work by writing special tokens
  // in the template form of the Alias command, and then detecting them when the
  // command is executed.  These are the special tokens:
  static const char *g_no_argument;
  static const char *g_need_argument;
  static const char *g_argument;

  CommandInterpreter(Debugger &debugger, bool synchronous_execution);

  ~CommandInterpreter() override = default;

  // These two functions fill out the Broadcaster interface:

  static llvm::StringRef GetStaticBroadcasterClass();

  llvm::StringRef GetBroadcasterClass() const override {
    return GetStaticBroadcasterClass();
  }

  void SourceInitFileCwd(CommandReturnObject &result);
  void SourceInitFileHome(CommandReturnObject &result, bool is_repl);
  void SourceInitFileGlobal(CommandReturnObject &result);

  bool AddCommand(llvm::StringRef name, const lldb::CommandObjectSP &cmd_sp,
                  bool can_replace);

  Status AddUserCommand(llvm::StringRef name,
                        const lldb::CommandObjectSP &cmd_sp, bool can_replace);

  lldb::CommandObjectSP GetCommandSPExact(llvm::StringRef cmd,
                                          bool include_aliases = false) const;

  CommandObject *GetCommandObject(llvm::StringRef cmd,
                                  StringList *matches = nullptr,
                                  StringList *descriptions = nullptr) const;

  CommandObject *GetUserCommandObject(llvm::StringRef cmd,
                                      StringList *matches = nullptr,
                                      StringList *descriptions = nullptr) const;

  /// Determine whether a root level, built-in command with this name exists.
  bool CommandExists(llvm::StringRef cmd) const;

  /// Determine whether an alias command with this name exists
  bool AliasExists(llvm::StringRef cmd) const;

  /// Determine whether a root-level user command with this name exists.
  bool UserCommandExists(llvm::StringRef cmd) const;

  /// Determine whether a root-level user multiword command with this name
  /// exists.
  bool UserMultiwordCommandExists(llvm::StringRef cmd) const;

  /// Look up the command pointed to by path encoded in the arguments of
  /// the incoming command object.  If all the path components exist
  /// and are all actual commands - not aliases, and the leaf command is a
  /// multiword command, return the command.  Otherwise return nullptr, and put
  /// a useful diagnostic in the Status object.
  ///
  /// \param[in] path
  ///    An Args object holding the path in its arguments
  /// \param[in] leaf_is_command
  ///    If true, return the container of the leaf name rather than looking up
  ///    the whole path as a leaf command.  The leaf needn't exist in this case.
  /// \param[in,out] result
  ///    If the path is not found, this error shows where we got off track.
  /// \return
  ///    If found, a pointer to the CommandObjectMultiword pointed to by path,
  ///    or to the container of the leaf element is is_leaf_command.
  ///    Returns nullptr under two circumstances:
  ///      1) The command in not found (check error.Fail)
  ///      2) is_leaf is true and the path has only a leaf.  We don't have a
  ///         dummy "contains everything MWC, so we return null here, but
  ///         in this case error.Success is true.

  CommandObjectMultiword *VerifyUserMultiwordCmdPath(Args &path,
                                                     bool leaf_is_command,
                                                     Status &result);

  CommandAlias *AddAlias(llvm::StringRef alias_name,
                         lldb::CommandObjectSP &command_obj_sp,
                         llvm::StringRef args_string = llvm::StringRef());

  /// Remove a command if it is removable (python or regex command). If \b force
  /// is provided, the command is removed regardless of its removable status.
  bool RemoveCommand(llvm::StringRef cmd, bool force = false);

  bool RemoveAlias(llvm::StringRef alias_name);

  bool GetAliasFullName(llvm::StringRef cmd, std::string &full_name) const;

  bool RemoveUserMultiword(llvm::StringRef multiword_name);

  // Do we want to allow top-level user multiword commands to be deleted?
  void RemoveAllUserMultiword() { m_user_mw_dict.clear(); }

  bool RemoveUser(llvm::StringRef alias_name);

  void RemoveAllUser() { m_user_dict.clear(); }

  const CommandAlias *GetAlias(llvm::StringRef alias_name) const;

  CommandObject *BuildAliasResult(llvm::StringRef alias_name,
                                  std::string &raw_input_string,
                                  std::string &alias_result,
                                  CommandReturnObject &result);

  bool HandleCommand(const char *command_line, LazyBool add_to_history,
                     const ExecutionContext &override_context,
                     CommandReturnObject &result);

  bool HandleCommand(const char *command_line, LazyBool add_to_history,
                     CommandReturnObject &result,
                     bool force_repeat_command = false);

  bool InterruptCommand();

  /// Execute a list of commands in sequence.
  ///
  /// \param[in] commands
  ///    The list of commands to execute.
  /// \param[in,out] context
  ///    The execution context in which to run the commands.
  /// \param[in] options
  ///    This object holds the options used to control when to stop, whether to
  ///    execute commands,
  ///    etc.
  /// \param[out] result
  ///    This is marked as succeeding with no output if all commands execute
  ///    safely,
  ///    and failed with some explanation if we aborted executing the commands
  ///    at some point.
  void HandleCommands(const StringList &commands,
                      const ExecutionContext &context,
                      const CommandInterpreterRunOptions &options,
                      CommandReturnObject &result);

  void HandleCommands(const StringList &commands,
                      const CommandInterpreterRunOptions &options,
                      CommandReturnObject &result);

  /// Execute a list of commands from a file.
  ///
  /// \param[in] file
  ///    The file from which to read in commands.
  /// \param[in,out] context
  ///    The execution context in which to run the commands.
  /// \param[in] options
  ///    This object holds the options used to control when to stop, whether to
  ///    execute commands,
  ///    etc.
  /// \param[out] result
  ///    This is marked as succeeding with no output if all commands execute
  ///    safely,
  ///    and failed with some explanation if we aborted executing the commands
  ///    at some point.
  void HandleCommandsFromFile(FileSpec &file, const ExecutionContext &context,
                              const CommandInterpreterRunOptions &options,
                              CommandReturnObject &result);

  void HandleCommandsFromFile(FileSpec &file,
                              const CommandInterpreterRunOptions &options,
                              CommandReturnObject &result);

  CommandObject *GetCommandObjectForCommand(llvm::StringRef &command_line);

  /// Returns the auto-suggestion string that should be added to the given
  /// command line.
  std::optional<std::string> GetAutoSuggestionForCommand(llvm::StringRef line);

  // This handles command line completion.
  void HandleCompletion(CompletionRequest &request);

  // This version just returns matches, and doesn't compute the substring. It
  // is here so the Help command can call it for the first argument.
  void HandleCompletionMatches(CompletionRequest &request);

  int GetCommandNamesMatchingPartialString(const char *cmd_cstr,
                                           bool include_aliases,
                                           StringList &matches,
                                           StringList &descriptions);

  void GetHelp(CommandReturnObject &result,
               uint32_t types = eCommandTypesAllThem);

  void GetAliasHelp(const char *alias_name, StreamString &help_string);

  void OutputFormattedHelpText(Stream &strm, llvm::StringRef prefix,
                               llvm::StringRef help_text);

  void OutputFormattedHelpText(Stream &stream, llvm::StringRef command_word,
                               llvm::StringRef separator,
                               llvm::StringRef help_text, size_t max_word_len);

  // this mimics OutputFormattedHelpText but it does perform a much simpler
  // formatting, basically ensuring line alignment. This is only good if you
  // have some complicated layout for your help text and want as little help as
  // reasonable in properly displaying it. Most of the times, you simply want
  // to type some text and have it printed in a reasonable way on screen. If
  // so, use OutputFormattedHelpText
  void OutputHelpText(Stream &stream, llvm::StringRef command_word,
                      llvm::StringRef separator, llvm::StringRef help_text,
                      uint32_t max_word_len);

  Debugger &GetDebugger() { return m_debugger; }

  ExecutionContext GetExecutionContext() const;

  lldb::PlatformSP GetPlatform(bool prefer_target_platform);

  const char *ProcessEmbeddedScriptCommands(const char *arg);

  void UpdatePrompt(llvm::StringRef prompt);

  bool Confirm(llvm::StringRef message, bool default_answer);

  void LoadCommandDictionary();

  void Initialize();

  void Clear();

  bool HasCommands() const;

  bool HasAliases() const;

  bool HasUserCommands() const;

  bool HasUserMultiwordCommands() const;

  bool HasAliasOptions() const;

  void BuildAliasCommandArgs(CommandObject *alias_cmd_obj,
                             const char *alias_name, Args &cmd_args,
                             std::string &raw_input_string,
                             CommandReturnObject &result);

  /// Picks the number out of a string of the form "%NNN", otherwise return 0.
  int GetOptionArgumentPosition(const char *in_string);

  void SkipLLDBInitFiles(bool skip_lldbinit_files) {
    m_skip_lldbinit_files = skip_lldbinit_files;
  }

  void SkipAppInitFiles(bool skip_app_init_files) {
    m_skip_app_init_files = skip_app_init_files;
  }

  bool GetSynchronous();

  void FindCommandsForApropos(llvm::StringRef word, StringList &commands_found,
                              StringList &commands_help,
                              bool search_builtin_commands,
                              bool search_user_commands,
                              bool search_alias_commands,
                              bool search_user_mw_commands);

  bool GetBatchCommandMode() { return m_batch_command_mode; }

  bool SetBatchCommandMode(bool value) {
    const bool old_value = m_batch_command_mode;
    m_batch_command_mode = value;
    return old_value;
  }

  void ChildrenTruncated() {
    if (m_truncation_warning == eNoOmission)
      m_truncation_warning = eUnwarnedOmission;
  }

  void SetReachedMaximumDepth() {
    if (m_max_depth_warning == eNoOmission)
      m_max_depth_warning = eUnwarnedOmission;
  }

  void PrintWarningsIfNecessary(Stream &s, const std::string &cmd_name) {
    if (m_truncation_warning == eUnwarnedOmission) {
      s.Printf("*** Some of the displayed variables have more members than the "
               "debugger will show by default. To show all of them, you can "
               "either use the --show-all-children option to %s or raise the "
               "limit by changing the target.max-children-count setting.\n",
               cmd_name.c_str());
      m_truncation_warning = eWarnedOmission;
    }

    if (m_max_depth_warning == eUnwarnedOmission) {
      s.Printf("*** Some of the displayed variables have a greater depth of "
               "members than the debugger will show by default. To increase "
               "the limit, use the --depth option to %s, or raise the limit by "
               "changing the target.max-children-depth setting.\n",
               cmd_name.c_str());
      m_max_depth_warning = eWarnedOmission;
    }
  }

  CommandHistory &GetCommandHistory() { return m_command_history; }

  bool IsActive();

  CommandInterpreterRunResult
  RunCommandInterpreter(CommandInterpreterRunOptions &options);

  void GetLLDBCommandsFromIOHandler(const char *prompt,
                                    IOHandlerDelegate &delegate,
                                    void *baton = nullptr);

  void GetPythonCommandsFromIOHandler(const char *prompt,
                                      IOHandlerDelegate &delegate,
                                      void *baton = nullptr);

  const char *GetCommandPrefix();

  // Properties
  bool GetExpandRegexAliases() const;

  bool GetPromptOnQuit() const;
  void SetPromptOnQuit(bool enable);

  bool GetSaveTranscript() const;
  void SetSaveTranscript(bool enable);

  bool GetSaveSessionOnQuit() const;
  void SetSaveSessionOnQuit(bool enable);

  bool GetOpenTranscriptInEditor() const;
  void SetOpenTranscriptInEditor(bool enable);

  FileSpec GetSaveSessionDirectory() const;
  void SetSaveSessionDirectory(llvm::StringRef path);

  bool GetEchoCommands() const;
  void SetEchoCommands(bool enable);

  bool GetEchoCommentCommands() const;
  void SetEchoCommentCommands(bool enable);

  bool GetRepeatPreviousCommand() const;
  
  bool GetRequireCommandOverwrite() const;

  const CommandObject::CommandMap &GetUserCommands() const {
    return m_user_dict;
  }

  const CommandObject::CommandMap &GetUserMultiwordCommands() const {
    return m_user_mw_dict;
  }

  const CommandObject::CommandMap &GetCommands() const {
    return m_command_dict;
  }

  const CommandObject::CommandMap &GetAliases() const { return m_alias_dict; }

  /// Specify if the command interpreter should allow that the user can
  /// specify a custom exit code when calling 'quit'.
  void AllowExitCodeOnQuit(bool allow);

  /// Sets the exit code for the quit command.
  /// \param[in] exit_code
  ///     The exit code that the driver should return on exit.
  /// \return True if the exit code was successfully set; false if the
  ///         interpreter doesn't allow custom exit codes.
  /// \see AllowExitCodeOnQuit
  [[nodiscard]] bool SetQuitExitCode(int exit_code);

  /// Returns the exit code that the user has specified when running the
  /// 'quit' command.
  /// \param[out] exited
  ///     Set to true if the user has called quit with a custom exit code.
  int GetQuitExitCode(bool &exited) const;

  void ResolveCommand(const char *command_line, CommandReturnObject &result);

  bool GetStopCmdSourceOnError() const;

  lldb::IOHandlerSP
  GetIOHandler(bool force_create = false,
               CommandInterpreterRunOptions *options = nullptr);

  bool GetSpaceReplPrompts() const;

  /// Save the current debugger session transcript to a file on disk.
  /// \param output_file
  ///     The file path to which the session transcript will be written. Since
  ///     the argument is optional, an arbitrary temporary file will be create
  ///     when no argument is passed.
  /// \param result
  ///     This is used to pass function output and error messages.
  /// \return \b true if the session transcript was successfully written to
  /// disk, \b false otherwise.
  bool SaveTranscript(CommandReturnObject &result,
                      std::optional<std::string> output_file = std::nullopt);

  FileSpec GetCurrentSourceDir();

  bool IsInteractive();

  bool IOHandlerInterrupt(IOHandler &io_handler) override;

  Status PreprocessCommand(std::string &command);
  Status PreprocessToken(std::string &token);

  void IncreaseCommandUsage(const CommandObject &cmd_obj) {
    ++m_command_usages[cmd_obj.GetCommandName()];
  }

  llvm::json::Value GetStatistics();
  const StructuredData::Array &GetTranscript() const;

protected:
  friend class Debugger;

  // This checks just the RunCommandInterpreter interruption state.  It is only
  // meant to be used in Debugger::InterruptRequested
  bool WasInterrupted() const;

  // IOHandlerDelegate functions
  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &line) override;

  llvm::StringRef IOHandlerGetControlSequence(char ch) override {
    static constexpr llvm::StringLiteral control_sequence("quit\n");
    if (ch == 'd')
      return control_sequence;
    return {};
  }

  void GetProcessOutput();

  bool DidProcessStopAbnormally() const;

  void SetSynchronous(bool value);

  lldb::CommandObjectSP GetCommandSP(llvm::StringRef cmd,
                                     bool include_aliases = true,
                                     bool exact = true,
                                     StringList *matches = nullptr,
                                     StringList *descriptions = nullptr) const;

private:
  void OverrideExecutionContext(const ExecutionContext &override_context);

  void RestoreExecutionContext();

  void SourceInitFile(FileSpec file, CommandReturnObject &result);

  // Completely resolves aliases and abbreviations, returning a pointer to the
  // final command object and updating command_line to the fully substituted
  // and translated command.
  CommandObject *ResolveCommandImpl(std::string &command_line,
                                    CommandReturnObject &result);

  void FindCommandsForApropos(llvm::StringRef word, StringList &commands_found,
                              StringList &commands_help,
                              const CommandObject::CommandMap &command_map);

  // An interruptible wrapper around the stream output
  void PrintCommandOutput(IOHandler &io_handler, llvm::StringRef str,
                          bool is_stdout);

  bool EchoCommandNonInteractive(llvm::StringRef line,
                                 const Flags &io_handler_flags) const;

  // A very simple state machine which models the command handling transitions
  enum class CommandHandlingState {
    eIdle,
    eInProgress,
    eInterrupted,
  };

  std::atomic<CommandHandlingState> m_command_state{
      CommandHandlingState::eIdle};

  int m_iohandler_nesting_level = 0;

  void StartHandlingCommand();
  void FinishHandlingCommand();

  Debugger &m_debugger; // The debugger session that this interpreter is
                        // associated with
  // Execution contexts that were temporarily set by some of HandleCommand*
  // overloads.
  std::stack<ExecutionContext> m_overriden_exe_contexts;
  bool m_synchronous_execution;
  bool m_skip_lldbinit_files;
  bool m_skip_app_init_files;
  CommandObject::CommandMap m_command_dict; // Stores basic built-in commands
                                            // (they cannot be deleted, removed
                                            // or overwritten).
  CommandObject::CommandMap
      m_alias_dict; // Stores user aliases/abbreviations for commands
  CommandObject::CommandMap m_user_dict; // Stores user-defined commands
  CommandObject::CommandMap
      m_user_mw_dict; // Stores user-defined multiword commands
  CommandHistory m_command_history;
  std::string m_repeat_command; // Stores the command that will be executed for
                                // an empty command string.
  lldb::IOHandlerSP m_command_io_handler_sp;
  char m_comment_char;
  bool m_batch_command_mode;
  /// Whether we truncated a value's list of children and whether the user has
  /// been told.
  ChildrenOmissionWarningStatus m_truncation_warning;
  /// Whether we reached the maximum child nesting depth and whether the user
  /// has been told.
  ChildrenOmissionWarningStatus m_max_depth_warning;

  // FIXME: Stop using this to control adding to the history and then replace
  // this with m_command_source_dirs.size().
  uint32_t m_command_source_depth;
  /// A stack of directory paths. When not empty, the last one is the directory
  /// of the file that's currently sourced.
  std::vector<FileSpec> m_command_source_dirs;
  std::vector<uint32_t> m_command_source_flags;
  CommandInterpreterRunResult m_result;

  // The exit code the user has requested when calling the 'quit' command.
  // No value means the user hasn't set a custom exit code so far.
  std::optional<int> m_quit_exit_code;
  // If the driver is accepts custom exit codes for the 'quit' command.
  bool m_allow_exit_code = false;

  /// Command usage statistics.
  typedef llvm::StringMap<uint64_t> CommandUsageMap;
  CommandUsageMap m_command_usages;

  /// Turn on settings `interpreter.save-transcript` for LLDB to populate
  /// this stream. Otherwise this stream is empty.
  StreamString m_transcript_stream;

  /// Contains a list of handled commands and their details. Each element in
  /// the list is a dictionary with the following keys/values:
  /// - "command" (string): The command that was given by the user.
  /// - "commandName" (string): The name of the executed command.
  /// - "commandArguments" (string): The arguments of the executed command.
  /// - "output" (string): The output of the command. Empty ("") if no output.
  /// - "error" (string): The error of the command. Empty ("") if no error.
  /// - "durationInSeconds" (float): The time it took to execute the command.
  /// - "timestampInEpochSeconds" (int): The timestamp when the command is
  ///   executed.
  ///
  /// Turn on settings `interpreter.save-transcript` for LLDB to populate
  /// this list. Otherwise this list is empty.
  StructuredData::Array m_transcript;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_COMMANDINTERPRETER_H
