//===-- CommandObjectLog.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectLog.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionValueEnumeration.h"
#include "lldb/Interpreter/OptionValueUInt64.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;

#define LLDB_OPTIONS_log_enable
#include "CommandOptions.inc"

#define LLDB_OPTIONS_log_dump
#include "CommandOptions.inc"

/// Common completion logic for log enable/disable.
static void CompleteEnableDisable(CompletionRequest &request) {
  size_t arg_index = request.GetCursorIndex();
  if (arg_index == 0) { // We got: log enable/disable x[tab]
    for (llvm::StringRef channel : Log::ListChannels())
      request.TryCompleteCurrentArg(channel);
  } else if (arg_index >= 1) { // We got: log enable/disable channel x[tab]
    llvm::StringRef channel = request.GetParsedLine().GetArgumentAtIndex(0);
    Log::ForEachChannelCategory(
        channel, [&request](llvm::StringRef name, llvm::StringRef desc) {
          request.TryCompleteCurrentArg(name, desc);
        });
  }
}

class CommandObjectLogEnable : public CommandObjectParsed {
public:
  // Constructors and Destructors
  CommandObjectLogEnable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "log enable",
                            "Enable logging for a single log channel.",
                            nullptr) {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData channel_arg;
    CommandArgumentData category_arg;

    // Define the first (and only) variant of this arg.
    channel_arg.arg_type = eArgTypeLogChannel;
    channel_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(channel_arg);

    category_arg.arg_type = eArgTypeLogCategory;
    category_arg.arg_repetition = eArgRepeatPlus;

    arg2.push_back(category_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectLogEnable() override = default;

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
        log_file.SetFile(option_arg, FileSpec::Style::native);
        FileSystem::Instance().Resolve(log_file);
        break;
      case 'h':
        handler = (LogHandlerKind)OptionArgParser::ToOptionEnum(
            option_arg, GetDefinitions()[option_idx].enum_values, 0, error);
        if (!error.Success())
          error.SetErrorStringWithFormat(
              "unrecognized value for log handler '%s'",
              option_arg.str().c_str());
        break;
      case 'b':
        error =
            buffer_size.SetValueFromString(option_arg, eVarSetOperationAssign);
        break;
      case 'v':
        log_options |= LLDB_LOG_OPTION_VERBOSE;
        break;
      case 's':
        log_options |= LLDB_LOG_OPTION_PREPEND_SEQUENCE;
        break;
      case 'T':
        log_options |= LLDB_LOG_OPTION_PREPEND_TIMESTAMP;
        break;
      case 'p':
        log_options |= LLDB_LOG_OPTION_PREPEND_PROC_AND_THREAD;
        break;
      case 'n':
        log_options |= LLDB_LOG_OPTION_PREPEND_THREAD_NAME;
        break;
      case 'S':
        log_options |= LLDB_LOG_OPTION_BACKTRACE;
        break;
      case 'a':
        log_options |= LLDB_LOG_OPTION_APPEND;
        break;
      case 'F':
        log_options |= LLDB_LOG_OPTION_PREPEND_FILE_FUNCTION;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      log_file.Clear();
      buffer_size.Clear();
      handler = eLogHandlerStream;
      log_options = 0;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_log_enable_options);
    }

    FileSpec log_file;
    OptionValueUInt64 buffer_size;
    LogHandlerKind handler = eLogHandlerStream;
    uint32_t log_options = 0;
  };

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    CompleteEnableDisable(request);
  }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    if (args.GetArgumentCount() < 2) {
      result.AppendErrorWithFormat(
          "%s takes a log channel and one or more log types.\n",
          m_cmd_name.c_str());
      return;
    }

    if (m_options.handler == eLogHandlerCircular &&
        m_options.buffer_size.GetCurrentValue() == 0) {
      result.AppendError(
          "the circular buffer handler requires a non-zero buffer size.\n");
      return;
    }

    if ((m_options.handler != eLogHandlerCircular &&
         m_options.handler != eLogHandlerStream) &&
        m_options.buffer_size.GetCurrentValue() != 0) {
      result.AppendError("a buffer size can only be specified for the circular "
                         "and stream buffer handler.\n");
      return;
    }

    if (m_options.handler != eLogHandlerStream && m_options.log_file) {
      result.AppendError(
          "a file name can only be specified for the stream handler.\n");
      return;
    }

    // Store into a std::string since we're about to shift the channel off.
    const std::string channel = std::string(args[0].ref());
    args.Shift(); // Shift off the channel
    char log_file[PATH_MAX];
    if (m_options.log_file)
      m_options.log_file.GetPath(log_file, sizeof(log_file));
    else
      log_file[0] = '\0';

    std::string error;
    llvm::raw_string_ostream error_stream(error);
    bool success = GetDebugger().EnableLog(
        channel, args.GetArgumentArrayRef(), log_file, m_options.log_options,
        m_options.buffer_size.GetCurrentValue(), m_options.handler,
        error_stream);
    result.GetErrorStream() << error_stream.str();

    if (success)
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    else
      result.SetStatus(eReturnStatusFailed);
  }

  CommandOptions m_options;
};

class CommandObjectLogDisable : public CommandObjectParsed {
public:
  // Constructors and Destructors
  CommandObjectLogDisable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "log disable",
                            "Disable one or more log channel categories.",
                            nullptr) {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData channel_arg;
    CommandArgumentData category_arg;

    // Define the first (and only) variant of this arg.
    channel_arg.arg_type = eArgTypeLogChannel;
    channel_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(channel_arg);

    category_arg.arg_type = eArgTypeLogCategory;
    category_arg.arg_repetition = eArgRepeatPlus;

    arg2.push_back(category_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectLogDisable() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    CompleteEnableDisable(request);
  }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    if (args.empty()) {
      result.AppendErrorWithFormat(
          "%s takes a log channel and one or more log types.\n",
          m_cmd_name.c_str());
      return;
    }

    const std::string channel = std::string(args[0].ref());
    args.Shift(); // Shift off the channel
    if (channel == "all") {
      Log::DisableAllLogChannels();
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      std::string error;
      llvm::raw_string_ostream error_stream(error);
      if (Log::DisableLogChannel(channel, args.GetArgumentArrayRef(),
                                 error_stream))
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
      result.GetErrorStream() << error_stream.str();
    }
  }
};

class CommandObjectLogList : public CommandObjectParsed {
public:
  // Constructors and Destructors
  CommandObjectLogList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "log list",
                            "List the log categories for one or more log "
                            "channels.  If none specified, lists them all.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeLogChannel, eArgRepeatStar);
  }

  ~CommandObjectLogList() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    for (llvm::StringRef channel : Log::ListChannels())
      request.TryCompleteCurrentArg(channel);
  }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    std::string output;
    llvm::raw_string_ostream output_stream(output);
    if (args.empty()) {
      Log::ListAllLogChannels(output_stream);
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      bool success = true;
      for (const auto &entry : args.entries())
        success =
            success && Log::ListChannelCategories(entry.ref(), output_stream);
      if (success)
        result.SetStatus(eReturnStatusSuccessFinishResult);
    }
    result.GetOutputStream() << output_stream.str();
  }
};
class CommandObjectLogDump : public CommandObjectParsed {
public:
  CommandObjectLogDump(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "log dump",
                            "dump circular buffer logs", nullptr) {
    AddSimpleArgumentList(eArgTypeLogChannel);
  }

  ~CommandObjectLogDump() override = default;

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
        log_file.SetFile(option_arg, FileSpec::Style::native);
        FileSystem::Instance().Resolve(log_file);
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      log_file.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_log_dump_options);
    }

    FileSpec log_file;
  };

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    CompleteEnableDisable(request);
  }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    if (args.empty()) {
      result.AppendErrorWithFormat(
          "%s takes a log channel and one or more log types.\n",
          m_cmd_name.c_str());
      return;
    }

    std::unique_ptr<llvm::raw_ostream> stream_up;
    if (m_options.log_file) {
      const File::OpenOptions flags = File::eOpenOptionWriteOnly |
                                      File::eOpenOptionCanCreate |
                                      File::eOpenOptionTruncate;
      llvm::Expected<FileUP> file = FileSystem::Instance().Open(
          m_options.log_file, flags, lldb::eFilePermissionsFileDefault, false);
      if (!file) {
        result.AppendErrorWithFormat("Unable to open log file '%s': %s",
                                     m_options.log_file.GetPath().c_str(),
                                     llvm::toString(file.takeError()).c_str());
        return;
      }
      stream_up = std::make_unique<llvm::raw_fd_ostream>(
          (*file)->GetDescriptor(), /*shouldClose=*/true);
    } else {
      stream_up = std::make_unique<llvm::raw_fd_ostream>(
          GetDebugger().GetOutputFile().GetDescriptor(), /*shouldClose=*/false);
    }

    const std::string channel = std::string(args[0].ref());
    std::string error;
    llvm::raw_string_ostream error_stream(error);
    if (Log::DumpLogChannel(channel, *stream_up, error_stream)) {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.SetStatus(eReturnStatusFailed);
      result.GetErrorStream() << error_stream.str();
    }
  }

  CommandOptions m_options;
};

class CommandObjectLogTimerEnable : public CommandObjectParsed {
public:
  // Constructors and Destructors
  CommandObjectLogTimerEnable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "log timers enable",
                            "enable LLDB internal performance timers",
                            "log timers enable <depth>") {
    AddSimpleArgumentList(eArgTypeCount, eArgRepeatOptional);
  }

  ~CommandObjectLogTimerEnable() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusFailed);

    if (args.GetArgumentCount() == 0) {
      Timer::SetDisplayDepth(UINT32_MAX);
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else if (args.GetArgumentCount() == 1) {
      uint32_t depth;
      if (args[0].ref().consumeInteger(0, depth)) {
        result.AppendError(
            "Could not convert enable depth to an unsigned integer.");
      } else {
        Timer::SetDisplayDepth(depth);
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
      }
    }

    if (!result.Succeeded()) {
      result.AppendError("Missing subcommand");
      result.AppendErrorWithFormat("Usage: %s\n", m_cmd_syntax.c_str());
    }
  }
};

class CommandObjectLogTimerDisable : public CommandObjectParsed {
public:
  // Constructors and Destructors
  CommandObjectLogTimerDisable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "log timers disable",
                            "disable LLDB internal performance timers",
                            nullptr) {}

  ~CommandObjectLogTimerDisable() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Timer::DumpCategoryTimes(result.GetOutputStream());
    Timer::SetDisplayDepth(0);
    result.SetStatus(eReturnStatusSuccessFinishResult);

    if (!result.Succeeded()) {
      result.AppendError("Missing subcommand");
      result.AppendErrorWithFormat("Usage: %s\n", m_cmd_syntax.c_str());
    }
  }
};

class CommandObjectLogTimerDump : public CommandObjectParsed {
public:
  // Constructors and Destructors
  CommandObjectLogTimerDump(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "log timers dump",
                            "dump LLDB internal performance timers", nullptr) {}

  ~CommandObjectLogTimerDump() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Timer::DumpCategoryTimes(result.GetOutputStream());
    result.SetStatus(eReturnStatusSuccessFinishResult);

    if (!result.Succeeded()) {
      result.AppendError("Missing subcommand");
      result.AppendErrorWithFormat("Usage: %s\n", m_cmd_syntax.c_str());
    }
  }
};

class CommandObjectLogTimerReset : public CommandObjectParsed {
public:
  // Constructors and Destructors
  CommandObjectLogTimerReset(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "log timers reset",
                            "reset LLDB internal performance timers", nullptr) {
  }

  ~CommandObjectLogTimerReset() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    Timer::ResetCategoryTimes();
    result.SetStatus(eReturnStatusSuccessFinishResult);

    if (!result.Succeeded()) {
      result.AppendError("Missing subcommand");
      result.AppendErrorWithFormat("Usage: %s\n", m_cmd_syntax.c_str());
    }
  }
};

class CommandObjectLogTimerIncrement : public CommandObjectParsed {
public:
  // Constructors and Destructors
  CommandObjectLogTimerIncrement(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "log timers increment",
                            "increment LLDB internal performance timers",
                            "log timers increment <bool>") {
    AddSimpleArgumentList(eArgTypeBoolean);
  }

  ~CommandObjectLogTimerIncrement() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    request.TryCompleteCurrentArg("true");
    request.TryCompleteCurrentArg("false");
  }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    result.SetStatus(eReturnStatusFailed);

    if (args.GetArgumentCount() == 1) {
      bool success;
      bool increment =
          OptionArgParser::ToBoolean(args[0].ref(), false, &success);

      if (success) {
        Timer::SetQuiet(!increment);
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
      } else
        result.AppendError("Could not convert increment value to boolean.");
    }

    if (!result.Succeeded()) {
      result.AppendError("Missing subcommand");
      result.AppendErrorWithFormat("Usage: %s\n", m_cmd_syntax.c_str());
    }
  }
};

class CommandObjectLogTimer : public CommandObjectMultiword {
public:
  CommandObjectLogTimer(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "log timers",
                               "Enable, disable, dump, and reset LLDB internal "
                               "performance timers.",
                               "log timers < enable <depth> | disable | dump | "
                               "increment <bool> | reset >") {
    LoadSubCommand("enable", CommandObjectSP(
                                 new CommandObjectLogTimerEnable(interpreter)));
    LoadSubCommand("disable", CommandObjectSP(new CommandObjectLogTimerDisable(
                                  interpreter)));
    LoadSubCommand("dump",
                   CommandObjectSP(new CommandObjectLogTimerDump(interpreter)));
    LoadSubCommand(
        "reset", CommandObjectSP(new CommandObjectLogTimerReset(interpreter)));
    LoadSubCommand(
        "increment",
        CommandObjectSP(new CommandObjectLogTimerIncrement(interpreter)));
  }

  ~CommandObjectLogTimer() override = default;
};

CommandObjectLog::CommandObjectLog(CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "log",
                             "Commands controlling LLDB internal logging.",
                             "log <subcommand> [<command-options>]") {
  LoadSubCommand("enable",
                 CommandObjectSP(new CommandObjectLogEnable(interpreter)));
  LoadSubCommand("disable",
                 CommandObjectSP(new CommandObjectLogDisable(interpreter)));
  LoadSubCommand("list",
                 CommandObjectSP(new CommandObjectLogList(interpreter)));
  LoadSubCommand("dump",
                 CommandObjectSP(new CommandObjectLogDump(interpreter)));
  LoadSubCommand("timers",
                 CommandObjectSP(new CommandObjectLogTimer(interpreter)));
}

CommandObjectLog::~CommandObjectLog() = default;
