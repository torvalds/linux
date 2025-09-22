#include "CommandObjectSession.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/OptionValueUInt64.h"
#include "lldb/Interpreter/Options.h"

using namespace lldb;
using namespace lldb_private;

class CommandObjectSessionSave : public CommandObjectParsed {
public:
  CommandObjectSessionSave(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "session save",
                            "Save the current session transcripts to a file.\n"
                            "If no file if specified, transcripts will be "
                            "saved to a temporary file.",
                            "session save [file]") {
    AddSimpleArgumentList(eArgTypePath, eArgRepeatOptional);
  }

  ~CommandObjectSessionSave() override = default;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    llvm::StringRef file_path;

    if (!args.empty())
      file_path = args[0].ref();

    if (m_interpreter.SaveTranscript(result, file_path.str()))
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    else
      result.SetStatus(eReturnStatusFailed);
  }
};

#define LLDB_OPTIONS_history
#include "CommandOptions.inc"

class CommandObjectSessionHistory : public CommandObjectParsed {
public:
  CommandObjectSessionHistory(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "session history",
                            "Dump the history of commands in this session.\n"
                            "Commands in the history list can be run again "
                            "using \"!<INDEX>\".   \"!-<OFFSET>\" will re-run "
                            "the command that is <OFFSET> commands from the end"
                            " of the list (counting the current command).",
                            nullptr) {}

  ~CommandObjectSessionHistory() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  class CommandOptions : public Options {
  public:
    CommandOptions()
        : m_start_idx(0), m_stop_idx(0), m_count(0), m_clear(false) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'c':
        error = m_count.SetValueFromString(option_arg, eVarSetOperationAssign);
        break;
      case 's':
        if (option_arg == "end") {
          m_start_idx.SetCurrentValue(UINT64_MAX);
          m_start_idx.SetOptionWasSet();
        } else
          error = m_start_idx.SetValueFromString(option_arg,
                                                 eVarSetOperationAssign);
        break;
      case 'e':
        error =
            m_stop_idx.SetValueFromString(option_arg, eVarSetOperationAssign);
        break;
      case 'C':
        m_clear.SetCurrentValue(true);
        m_clear.SetOptionWasSet();
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_start_idx.Clear();
      m_stop_idx.Clear();
      m_count.Clear();
      m_clear.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_history_options);
    }

    // Instance variables to hold the values for command options.

    OptionValueUInt64 m_start_idx;
    OptionValueUInt64 m_stop_idx;
    OptionValueUInt64 m_count;
    OptionValueBoolean m_clear;
  };

  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (m_options.m_clear.GetCurrentValue() &&
        m_options.m_clear.OptionWasSet()) {
      m_interpreter.GetCommandHistory().Clear();
      result.SetStatus(lldb::eReturnStatusSuccessFinishNoResult);
    } else {
      if (m_options.m_start_idx.OptionWasSet() &&
          m_options.m_stop_idx.OptionWasSet() &&
          m_options.m_count.OptionWasSet()) {
        result.AppendError("--count, --start-index and --end-index cannot be "
                           "all specified in the same invocation");
        result.SetStatus(lldb::eReturnStatusFailed);
      } else {
        std::pair<bool, uint64_t> start_idx(
            m_options.m_start_idx.OptionWasSet(),
            m_options.m_start_idx.GetCurrentValue());
        std::pair<bool, uint64_t> stop_idx(
            m_options.m_stop_idx.OptionWasSet(),
            m_options.m_stop_idx.GetCurrentValue());
        std::pair<bool, uint64_t> count(m_options.m_count.OptionWasSet(),
                                        m_options.m_count.GetCurrentValue());

        const CommandHistory &history(m_interpreter.GetCommandHistory());

        if (start_idx.first && start_idx.second == UINT64_MAX) {
          if (count.first) {
            start_idx.second = history.GetSize() - count.second;
            stop_idx.second = history.GetSize() - 1;
          } else if (stop_idx.first) {
            start_idx.second = stop_idx.second;
            stop_idx.second = history.GetSize() - 1;
          } else {
            start_idx.second = 0;
            stop_idx.second = history.GetSize() - 1;
          }
        } else {
          if (!start_idx.first && !stop_idx.first && !count.first) {
            start_idx.second = 0;
            stop_idx.second = history.GetSize() - 1;
          } else if (start_idx.first) {
            if (count.first) {
              stop_idx.second = start_idx.second + count.second - 1;
            } else if (!stop_idx.first) {
              stop_idx.second = history.GetSize() - 1;
            }
          } else if (stop_idx.first) {
            if (count.first) {
              if (stop_idx.second >= count.second)
                start_idx.second = stop_idx.second - count.second + 1;
              else
                start_idx.second = 0;
            }
          } else /* if (count.first) */
          {
            start_idx.second = 0;
            stop_idx.second = count.second - 1;
          }
        }
        history.Dump(result.GetOutputStream(), start_idx.second,
                     stop_idx.second);
      }
    }
  }

  CommandOptions m_options;
};

CommandObjectSession::CommandObjectSession(CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "session",
                             "Commands controlling LLDB session.",
                             "session <subcommand> [<command-options>]") {
  LoadSubCommand("save",
                 CommandObjectSP(new CommandObjectSessionSave(interpreter)));
  LoadSubCommand("history",
                 CommandObjectSP(new CommandObjectSessionHistory(interpreter)));
}
