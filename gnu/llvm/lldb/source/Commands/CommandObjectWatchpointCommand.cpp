//===-- CommandObjectWatchpointCommand.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <vector>

#include "CommandObjectWatchpoint.h"
#include "CommandObjectWatchpointCommand.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

#define LLDB_OPTIONS_watchpoint_command_add
#include "CommandOptions.inc"

class CommandObjectWatchpointCommandAdd : public CommandObjectParsed,
                                          public IOHandlerDelegateMultiline {
public:
  CommandObjectWatchpointCommandAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "add",
                            "Add a set of LLDB commands to a watchpoint, to be "
                            "executed whenever the watchpoint is hit.  "
                            "The commands added to the watchpoint replace any "
                            "commands previously added to it.",
                            nullptr, eCommandRequiresTarget),
        IOHandlerDelegateMultiline("DONE",
                                   IOHandlerDelegate::Completion::LLDBCommand) {
    SetHelpLong(
        R"(
General information about entering watchpoint commands
------------------------------------------------------

)"
        "This command will prompt for commands to be executed when the specified \
watchpoint is hit.  Each command is typed on its own line following the '> ' \
prompt until 'DONE' is entered."
        R"(

)"
        "Syntactic errors may not be detected when initially entered, and many \
malformed commands can silently fail when executed.  If your watchpoint commands \
do not appear to be executing, double-check the command syntax."
        R"(

)"
        "Note: You may enter any debugger command exactly as you would at the debugger \
prompt.  There is no limit to the number of commands supplied, but do NOT enter \
more than one command per line."
        R"(

Special information about PYTHON watchpoint commands
----------------------------------------------------

)"
        "You may enter either one or more lines of Python, including function \
definitions or calls to functions that will have been imported by the time \
the code executes.  Single line watchpoint commands will be interpreted 'as is' \
when the watchpoint is hit.  Multiple lines of Python will be wrapped in a \
generated function, and a call to the function will be attached to the watchpoint."
        R"(

This auto-generated function is passed in three arguments:

    frame:  an lldb.SBFrame object for the frame which hit the watchpoint.

    wp:     the watchpoint that was hit.

)"
        "When specifying a python function with the --python-function option, you need \
to supply the function name prepended by the module name:"
        R"(

    --python-function myutils.watchpoint_callback

The function itself must have the following prototype:

def watchpoint_callback(frame, wp):
  # Your code goes here

)"
        "The arguments are the same as the arguments passed to generated functions as \
described above.  Note that the global variable 'lldb.frame' will NOT be updated when \
this function is called, so be sure to use the 'frame' argument. The 'frame' argument \
can get you to the thread via frame.GetThread(), the thread can get you to the \
process via thread.GetProcess(), and the process can get you back to the target \
via process.GetTarget()."
        R"(

)"
        "Important Note: As Python code gets collected into functions, access to global \
variables requires explicit scoping using the 'global' keyword.  Be sure to use correct \
Python syntax, including indentation, when entering Python watchpoint commands."
        R"(

Example Python one-line watchpoint command:

(lldb) watchpoint command add -s python 1
Enter your Python command(s). Type 'DONE' to end.
> print "Hit this watchpoint!"
> DONE

As a convenience, this also works for a short Python one-liner:

(lldb) watchpoint command add -s python 1 -o 'import time; print time.asctime()'
(lldb) run
Launching '.../a.out'  (x86_64)
(lldb) Fri Sep 10 12:17:45 2010
Process 21778 Stopped
* thread #1: tid = 0x2e03, 0x0000000100000de8 a.out`c + 7 at main.c:39, stop reason = watchpoint 1.1, queue = com.apple.main-thread
  36
  37   	int c(int val)
  38   	{
  39 ->	    return val + 3;
  40   	}
  41
  42   	int main (int argc, char const *argv[])

Example multiple line Python watchpoint command, using function definition:

(lldb) watchpoint command add -s python 1
Enter your Python command(s). Type 'DONE' to end.
> def watchpoint_output (wp_no):
>     out_string = "Hit watchpoint number " + repr (wp_no)
>     print out_string
>     return True
> watchpoint_output (1)
> DONE

Example multiple line Python watchpoint command, using 'loose' Python:

(lldb) watchpoint command add -s p 1
Enter your Python command(s). Type 'DONE' to end.
> global wp_count
> wp_count = wp_count + 1
> print "Hit this watchpoint " + repr(wp_count) + " times!"
> DONE

)"
        "In this case, since there is a reference to a global variable, \
'wp_count', you will also need to make sure 'wp_count' exists and is \
initialized:"
        R"(

(lldb) script
>>> wp_count = 0
>>> quit()

)"
        "Final Note: A warning that no watchpoint command was generated when there \
are no syntax errors may indicate that a function was declared but never called.");

    AddSimpleArgumentList(eArgTypeWatchpointID);
  }

  ~CommandObjectWatchpointCommandAdd() override = default;

  Options *GetOptions() override { return &m_options; }

  void IOHandlerActivated(IOHandler &io_handler, bool interactive) override {
    StreamFileSP output_sp(io_handler.GetOutputStreamFileSP());
    if (output_sp && interactive) {
      output_sp->PutCString(
          "Enter your debugger command(s).  Type 'DONE' to end.\n");
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &line) override {
    io_handler.SetIsDone(true);

    // The WatchpointOptions object is owned by the watchpoint or watchpoint
    // location
    WatchpointOptions *wp_options =
        (WatchpointOptions *)io_handler.GetUserData();
    if (wp_options) {
      std::unique_ptr<WatchpointOptions::CommandData> data_up(
          new WatchpointOptions::CommandData());
      if (data_up) {
        data_up->user_source.SplitIntoLines(line);
        auto baton_sp = std::make_shared<WatchpointOptions::CommandBaton>(
            std::move(data_up));
        wp_options->SetCallback(WatchpointOptionsCallbackFunction, baton_sp);
      }
    }
  }

  void CollectDataForWatchpointCommandCallback(WatchpointOptions *wp_options,
                                               CommandReturnObject &result) {
    m_interpreter.GetLLDBCommandsFromIOHandler(
        "> ",        // Prompt
        *this,       // IOHandlerDelegate
        wp_options); // Baton for the "io_handler" that will be passed back into
                     // our IOHandlerDelegate functions
  }

  /// Set a one-liner as the callback for the watchpoint.
  void SetWatchpointCommandCallback(WatchpointOptions *wp_options,
                                    const char *oneliner) {
    std::unique_ptr<WatchpointOptions::CommandData> data_up(
        new WatchpointOptions::CommandData());

    // It's necessary to set both user_source and script_source to the
    // oneliner. The former is used to generate callback description (as in
    // watchpoint command list) while the latter is used for Python to
    // interpret during the actual callback.
    data_up->user_source.AppendString(oneliner);
    data_up->script_source.assign(oneliner);
    data_up->stop_on_error = m_options.m_stop_on_error;

    auto baton_sp =
        std::make_shared<WatchpointOptions::CommandBaton>(std::move(data_up));
    wp_options->SetCallback(WatchpointOptionsCallbackFunction, baton_sp);
  }

  static bool
  WatchpointOptionsCallbackFunction(void *baton,
                                    StoppointCallbackContext *context,
                                    lldb::user_id_t watch_id) {
    bool ret_value = true;
    if (baton == nullptr)
      return true;

    WatchpointOptions::CommandData *data =
        (WatchpointOptions::CommandData *)baton;
    StringList &commands = data->user_source;

    if (commands.GetSize() > 0) {
      ExecutionContext exe_ctx(context->exe_ctx_ref);
      Target *target = exe_ctx.GetTargetPtr();
      if (target) {
        Debugger &debugger = target->GetDebugger();
        CommandReturnObject result(debugger.GetUseColor());

        // Rig up the results secondary output stream to the debugger's, so the
        // output will come out synchronously if the debugger is set up that
        // way.
        StreamSP output_stream(debugger.GetAsyncOutputStream());
        StreamSP error_stream(debugger.GetAsyncErrorStream());
        result.SetImmediateOutputStream(output_stream);
        result.SetImmediateErrorStream(error_stream);

        CommandInterpreterRunOptions options;
        options.SetStopOnContinue(true);
        options.SetStopOnError(data->stop_on_error);
        options.SetEchoCommands(false);
        options.SetPrintResults(true);
        options.SetPrintErrors(true);
        options.SetAddToHistory(false);

        debugger.GetCommandInterpreter().HandleCommands(commands, exe_ctx,
                                                        options, result);
        result.GetImmediateOutputStream()->Flush();
        result.GetImmediateErrorStream()->Flush();
      }
    }
    return ret_value;
  }

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'o':
        m_use_one_liner = true;
        m_one_liner = std::string(option_arg);
        break;

      case 's':
        m_script_language = (lldb::ScriptLanguage)OptionArgParser::ToOptionEnum(
            option_arg, GetDefinitions()[option_idx].enum_values,
            eScriptLanguageNone, error);

        switch (m_script_language) {
        case eScriptLanguagePython:
        case eScriptLanguageLua:
          m_use_script_language = true;
          break;
        case eScriptLanguageNone:
        case eScriptLanguageUnknown:
          m_use_script_language = false;
          break;
        }
        break;

      case 'e': {
        bool success = false;
        m_stop_on_error =
            OptionArgParser::ToBoolean(option_arg, false, &success);
        if (!success)
          error.SetErrorStringWithFormat(
              "invalid value for stop-on-error: \"%s\"",
              option_arg.str().c_str());
      } break;

      case 'F':
        m_use_one_liner = false;
        m_function_name.assign(std::string(option_arg));
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_use_commands = true;
      m_use_script_language = false;
      m_script_language = eScriptLanguageNone;

      m_use_one_liner = false;
      m_stop_on_error = true;
      m_one_liner.clear();
      m_function_name.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_watchpoint_command_add_options);
    }

    // Instance variables to hold the values for command options.

    bool m_use_commands = false;
    bool m_use_script_language = false;
    lldb::ScriptLanguage m_script_language = eScriptLanguageNone;

    // Instance variables to hold the values for one_liner options.
    bool m_use_one_liner = false;
    std::string m_one_liner;
    bool m_stop_on_error;
    std::string m_function_name;
  };

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();

    const WatchpointList &watchpoints = target->GetWatchpointList();
    size_t num_watchpoints = watchpoints.GetSize();

    if (num_watchpoints == 0) {
      result.AppendError("No watchpoints exist to have commands added");
      return;
    }

    if (!m_options.m_function_name.empty()) {
      if (!m_options.m_use_script_language) {
        m_options.m_script_language = GetDebugger().GetScriptLanguage();
        m_options.m_use_script_language = true;
      }
    }

    std::vector<uint32_t> valid_wp_ids;
    if (!CommandObjectMultiwordWatchpoint::VerifyWatchpointIDs(target, command,
                                                               valid_wp_ids)) {
      result.AppendError("Invalid watchpoints specification.");
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    const size_t count = valid_wp_ids.size();
    for (size_t i = 0; i < count; ++i) {
      uint32_t cur_wp_id = valid_wp_ids.at(i);
      if (cur_wp_id != LLDB_INVALID_WATCH_ID) {
        Watchpoint *wp = target->GetWatchpointList().FindByID(cur_wp_id).get();
        // Sanity check wp first.
        if (wp == nullptr)
          continue;

        WatchpointOptions *wp_options = wp->GetOptions();
        // Skip this watchpoint if wp_options is not good.
        if (wp_options == nullptr)
          continue;

        // If we are using script language, get the script interpreter in order
        // to set or collect command callback.  Otherwise, call the methods
        // associated with this object.
        if (m_options.m_use_script_language) {
          ScriptInterpreter *script_interp = GetDebugger().GetScriptInterpreter(
              /*can_create=*/true, m_options.m_script_language);
          // Special handling for one-liner specified inline.
          if (m_options.m_use_one_liner) {
            script_interp->SetWatchpointCommandCallback(
                wp_options, m_options.m_one_liner.c_str(),
                /*is_callback=*/false);
          }
          // Special handling for using a Python function by name instead of
          // extending the watchpoint callback data structures, we just
          // automatize what the user would do manually: make their watchpoint
          // command be a function call
          else if (!m_options.m_function_name.empty()) {
            std::string function_signature = m_options.m_function_name;
            function_signature += "(frame, wp, internal_dict)";
            script_interp->SetWatchpointCommandCallback(
                wp_options, function_signature.c_str(), /*is_callback=*/true);
          } else {
            script_interp->CollectDataForWatchpointCommandCallback(wp_options,
                                                                   result);
          }
        } else {
          // Special handling for one-liner specified inline.
          if (m_options.m_use_one_liner)
            SetWatchpointCommandCallback(wp_options,
                                         m_options.m_one_liner.c_str());
          else
            CollectDataForWatchpointCommandCallback(wp_options, result);
        }
      }
    }
  }

private:
  CommandOptions m_options;
};

// CommandObjectWatchpointCommandDelete

class CommandObjectWatchpointCommandDelete : public CommandObjectParsed {
public:
  CommandObjectWatchpointCommandDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "delete",
                            "Delete the set of commands from a watchpoint.",
                            nullptr, eCommandRequiresTarget) {
    AddSimpleArgumentList(eArgTypeWatchpointID);
  }

  ~CommandObjectWatchpointCommandDelete() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();

    const WatchpointList &watchpoints = target->GetWatchpointList();
    size_t num_watchpoints = watchpoints.GetSize();

    if (num_watchpoints == 0) {
      result.AppendError("No watchpoints exist to have commands deleted");
      return;
    }

    if (command.GetArgumentCount() == 0) {
      result.AppendError(
          "No watchpoint specified from which to delete the commands");
      return;
    }

    std::vector<uint32_t> valid_wp_ids;
    if (!CommandObjectMultiwordWatchpoint::VerifyWatchpointIDs(target, command,
                                                               valid_wp_ids)) {
      result.AppendError("Invalid watchpoints specification.");
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    const size_t count = valid_wp_ids.size();
    for (size_t i = 0; i < count; ++i) {
      uint32_t cur_wp_id = valid_wp_ids.at(i);
      if (cur_wp_id != LLDB_INVALID_WATCH_ID) {
        Watchpoint *wp = target->GetWatchpointList().FindByID(cur_wp_id).get();
        if (wp)
          wp->ClearCallback();
      } else {
        result.AppendErrorWithFormat("Invalid watchpoint ID: %u.\n", cur_wp_id);
        return;
      }
    }
  }
};

// CommandObjectWatchpointCommandList

class CommandObjectWatchpointCommandList : public CommandObjectParsed {
public:
  CommandObjectWatchpointCommandList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "list",
                            "List the script or set of commands to be executed "
                            "when the watchpoint is hit.",
                            nullptr, eCommandRequiresTarget) {
    AddSimpleArgumentList(eArgTypeWatchpointID);
  }

  ~CommandObjectWatchpointCommandList() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();

    const WatchpointList &watchpoints = target->GetWatchpointList();
    size_t num_watchpoints = watchpoints.GetSize();

    if (num_watchpoints == 0) {
      result.AppendError("No watchpoints exist for which to list commands");
      return;
    }

    if (command.GetArgumentCount() == 0) {
      result.AppendError(
          "No watchpoint specified for which to list the commands");
      return;
    }

    std::vector<uint32_t> valid_wp_ids;
    if (!CommandObjectMultiwordWatchpoint::VerifyWatchpointIDs(target, command,
                                                               valid_wp_ids)) {
      result.AppendError("Invalid watchpoints specification.");
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    const size_t count = valid_wp_ids.size();
    for (size_t i = 0; i < count; ++i) {
      uint32_t cur_wp_id = valid_wp_ids.at(i);
      if (cur_wp_id != LLDB_INVALID_WATCH_ID) {
        Watchpoint *wp = target->GetWatchpointList().FindByID(cur_wp_id).get();

        if (wp) {
          const WatchpointOptions *wp_options = wp->GetOptions();
          if (wp_options) {
            // Get the callback baton associated with the current watchpoint.
            const Baton *baton = wp_options->GetBaton();
            if (baton) {
              result.GetOutputStream().Printf("Watchpoint %u:\n", cur_wp_id);
              baton->GetDescription(result.GetOutputStream().AsRawOstream(),
                                    eDescriptionLevelFull,
                                    result.GetOutputStream().GetIndentLevel() +
                                        2);
            } else {
              result.AppendMessageWithFormat(
                  "Watchpoint %u does not have an associated command.\n",
                  cur_wp_id);
            }
          }
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat("Invalid watchpoint ID: %u.\n",
                                       cur_wp_id);
        }
      }
    }
  }
};

// CommandObjectWatchpointCommand

CommandObjectWatchpointCommand::CommandObjectWatchpointCommand(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(
          interpreter, "command",
          "Commands for adding, removing and examining LLDB commands "
          "executed when the watchpoint is hit (watchpoint 'commands').",
          "command <sub-command> [<sub-command-options>] <watchpoint-id>") {
  CommandObjectSP add_command_object(
      new CommandObjectWatchpointCommandAdd(interpreter));
  CommandObjectSP delete_command_object(
      new CommandObjectWatchpointCommandDelete(interpreter));
  CommandObjectSP list_command_object(
      new CommandObjectWatchpointCommandList(interpreter));

  add_command_object->SetCommandName("watchpoint command add");
  delete_command_object->SetCommandName("watchpoint command delete");
  list_command_object->SetCommandName("watchpoint command list");

  LoadSubCommand("add", add_command_object);
  LoadSubCommand("delete", delete_command_object);
  LoadSubCommand("list", list_command_object);
}

CommandObjectWatchpointCommand::~CommandObjectWatchpointCommand() = default;
