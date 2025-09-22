//===-- CommandObjectBreakpointCommand.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectBreakpointCommand.h"
#include "CommandObjectBreakpoint.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointIDList.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupPythonClassWithDict.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

#define LLDB_OPTIONS_breakpoint_command_add
#include "CommandOptions.inc"

class CommandObjectBreakpointCommandAdd : public CommandObjectParsed,
                                          public IOHandlerDelegateMultiline {
public:
  CommandObjectBreakpointCommandAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "add",
                            "Add LLDB commands to a breakpoint, to be executed "
                            "whenever the breakpoint is hit.  "
                            "The commands added to the breakpoint replace any "
                            "commands previously added to it."
                            "  If no breakpoint is specified, adds the "
                            "commands to the last created breakpoint.",
                            nullptr),
        IOHandlerDelegateMultiline("DONE",
                                   IOHandlerDelegate::Completion::LLDBCommand),
        m_func_options("breakpoint command", false, 'F') {
    SetHelpLong(
        R"(
General information about entering breakpoint commands
------------------------------------------------------

)"
        "This command will prompt for commands to be executed when the specified \
breakpoint is hit.  Each command is typed on its own line following the '> ' \
prompt until 'DONE' is entered."
        R"(

)"
        "Syntactic errors may not be detected when initially entered, and many \
malformed commands can silently fail when executed.  If your breakpoint commands \
do not appear to be executing, double-check the command syntax."
        R"(

)"
        "Note: You may enter any debugger command exactly as you would at the debugger \
prompt.  There is no limit to the number of commands supplied, but do NOT enter \
more than one command per line."
        R"(

Special information about PYTHON breakpoint commands
----------------------------------------------------

)"
        "You may enter either one or more lines of Python, including function \
definitions or calls to functions that will have been imported by the time \
the code executes.  Single line breakpoint commands will be interpreted 'as is' \
when the breakpoint is hit.  Multiple lines of Python will be wrapped in a \
generated function, and a call to the function will be attached to the breakpoint."
        R"(

This auto-generated function is passed in three arguments:

    frame:  an lldb.SBFrame object for the frame which hit breakpoint.

    bp_loc: an lldb.SBBreakpointLocation object that represents the breakpoint location that was hit.

    dict:   the python session dictionary hit.

)"
        "When specifying a python function with the --python-function option, you need \
to supply the function name prepended by the module name:"
        R"(

    --python-function myutils.breakpoint_callback

The function itself must have either of the following prototypes:

def breakpoint_callback(frame, bp_loc, internal_dict):
  # Your code goes here

or:

def breakpoint_callback(frame, bp_loc, extra_args, internal_dict):
  # Your code goes here

)"
        "The arguments are the same as the arguments passed to generated functions as \
described above.  In the second form, any -k and -v pairs provided to the command will \
be packaged into a SBDictionary in an SBStructuredData and passed as the extra_args parameter. \
\n\n\
Note that the global variable 'lldb.frame' will NOT be updated when \
this function is called, so be sure to use the 'frame' argument. The 'frame' argument \
can get you to the thread via frame.GetThread(), the thread can get you to the \
process via thread.GetProcess(), and the process can get you back to the target \
via process.GetTarget()."
        R"(

)"
        "Important Note: As Python code gets collected into functions, access to global \
variables requires explicit scoping using the 'global' keyword.  Be sure to use correct \
Python syntax, including indentation, when entering Python breakpoint commands."
        R"(

Example Python one-line breakpoint command:

(lldb) breakpoint command add -s python 1
Enter your Python command(s). Type 'DONE' to end.
def function (frame, bp_loc, internal_dict):
    """frame: the lldb.SBFrame for the location at which you stopped
       bp_loc: an lldb.SBBreakpointLocation for the breakpoint location information
       internal_dict: an LLDB support object not to be used"""
    print("Hit this breakpoint!")
    DONE

As a convenience, this also works for a short Python one-liner:

(lldb) breakpoint command add -s python 1 -o 'import time; print(time.asctime())'
(lldb) run
Launching '.../a.out'  (x86_64)
(lldb) Fri Sep 10 12:17:45 2010
Process 21778 Stopped
* thread #1: tid = 0x2e03, 0x0000000100000de8 a.out`c + 7 at main.c:39, stop reason = breakpoint 1.1, queue = com.apple.main-thread
  36
  37   	int c(int val)
  38   	{
  39 ->	    return val + 3;
  40   	}
  41
  42   	int main (int argc, char const *argv[])

Example multiple line Python breakpoint command:

(lldb) breakpoint command add -s p 1
Enter your Python command(s). Type 'DONE' to end.
def function (frame, bp_loc, internal_dict):
    """frame: the lldb.SBFrame for the location at which you stopped
       bp_loc: an lldb.SBBreakpointLocation for the breakpoint location information
       internal_dict: an LLDB support object not to be used"""
    global bp_count
    bp_count = bp_count + 1
    print("Hit this breakpoint " + repr(bp_count) + " times!")
    DONE

)"
        "In this case, since there is a reference to a global variable, \
'bp_count', you will also need to make sure 'bp_count' exists and is \
initialized:"
        R"(

(lldb) script
>>> bp_count = 0
>>> quit()

)"
        "Your Python code, however organized, can optionally return a value.  \
If the returned value is False, that tells LLDB not to stop at the breakpoint \
to which the code is associated. Returning anything other than False, or even \
returning None, or even omitting a return statement entirely, will cause \
LLDB to stop."
        R"(

)"
        "Final Note: A warning that no breakpoint command was generated when there \
are no syntax errors may indicate that a function was declared but never called.");

    m_all_options.Append(&m_options);
    m_all_options.Append(&m_func_options, LLDB_OPT_SET_2 | LLDB_OPT_SET_3,
                         LLDB_OPT_SET_2);
    m_all_options.Finalize();

    AddSimpleArgumentList(eArgTypeBreakpointID, eArgRepeatOptional);
  }

  ~CommandObjectBreakpointCommandAdd() override = default;

  Options *GetOptions() override { return &m_all_options; }

  void IOHandlerActivated(IOHandler &io_handler, bool interactive) override {
    StreamFileSP output_sp(io_handler.GetOutputStreamFileSP());
    if (output_sp && interactive) {
      output_sp->PutCString(g_reader_instructions);
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &line) override {
    io_handler.SetIsDone(true);

    std::vector<std::reference_wrapper<BreakpointOptions>> *bp_options_vec =
        (std::vector<std::reference_wrapper<BreakpointOptions>> *)
            io_handler.GetUserData();
    for (BreakpointOptions &bp_options : *bp_options_vec) {
      auto cmd_data = std::make_unique<BreakpointOptions::CommandData>();
      cmd_data->user_source.SplitIntoLines(line.c_str(), line.size());
      bp_options.SetCommandDataCallback(cmd_data);
    }
  }

  void CollectDataForBreakpointCommandCallback(
      std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
      CommandReturnObject &result) {
    m_interpreter.GetLLDBCommandsFromIOHandler(
        "> ",             // Prompt
        *this,            // IOHandlerDelegate
        &bp_options_vec); // Baton for the "io_handler" that will be passed back
                          // into our IOHandlerDelegate functions
  }

  /// Set a one-liner as the callback for the breakpoint.
  void SetBreakpointCommandCallback(
      std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
      const char *oneliner) {
    for (BreakpointOptions &bp_options : bp_options_vec) {
      auto cmd_data = std::make_unique<BreakpointOptions::CommandData>();

      cmd_data->user_source.AppendString(oneliner);
      cmd_data->stop_on_error = m_options.m_stop_on_error;

      bp_options.SetCommandDataCallback(cmd_data);
    }
  }

  class CommandOptions : public OptionGroup {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option =
          g_breakpoint_command_add_options[option_idx].short_option;

      switch (short_option) {
      case 'o':
        m_use_one_liner = true;
        m_one_liner = std::string(option_arg);
        break;

      case 's':
        m_script_language = (lldb::ScriptLanguage)OptionArgParser::ToOptionEnum(
            option_arg,
            g_breakpoint_command_add_options[option_idx].enum_values,
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

      case 'D':
        m_use_dummy = true;
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
      m_use_dummy = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_breakpoint_command_add_options);
    }

    // Instance variables to hold the values for command options.

    bool m_use_commands = false;
    bool m_use_script_language = false;
    lldb::ScriptLanguage m_script_language = eScriptLanguageNone;

    // Instance variables to hold the values for one_liner options.
    bool m_use_one_liner = false;
    std::string m_one_liner;
    bool m_stop_on_error;
    bool m_use_dummy;
  };

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target &target = GetSelectedOrDummyTarget(m_options.m_use_dummy);

    const BreakpointList &breakpoints = target.GetBreakpointList();
    size_t num_breakpoints = breakpoints.GetSize();

    if (num_breakpoints == 0) {
      result.AppendError("No breakpoints exist to have commands added");
      return;
    }

    if (!m_func_options.GetName().empty()) {
      m_options.m_use_one_liner = false;
      if (!m_options.m_use_script_language) {
        m_options.m_script_language = GetDebugger().GetScriptLanguage();
        m_options.m_use_script_language = true;
      }
    }

    BreakpointIDList valid_bp_ids;
    CommandObjectMultiwordBreakpoint::VerifyBreakpointOrLocationIDs(
        command, &target, result, &valid_bp_ids,
        BreakpointName::Permissions::PermissionKinds::listPerm);

    m_bp_options_vec.clear();

    if (result.Succeeded()) {
      const size_t count = valid_bp_ids.GetSize();

      for (size_t i = 0; i < count; ++i) {
        BreakpointID cur_bp_id = valid_bp_ids.GetBreakpointIDAtIndex(i);
        if (cur_bp_id.GetBreakpointID() != LLDB_INVALID_BREAK_ID) {
          Breakpoint *bp =
              target.GetBreakpointByID(cur_bp_id.GetBreakpointID()).get();
          if (cur_bp_id.GetLocationID() == LLDB_INVALID_BREAK_ID) {
            // This breakpoint does not have an associated location.
            m_bp_options_vec.push_back(bp->GetOptions());
          } else {
            BreakpointLocationSP bp_loc_sp(
                bp->FindLocationByID(cur_bp_id.GetLocationID()));
            // This breakpoint does have an associated location. Get its
            // breakpoint options.
            if (bp_loc_sp)
              m_bp_options_vec.push_back(bp_loc_sp->GetLocationOptions());
          }
        }
      }

      // If we are using script language, get the script interpreter in order
      // to set or collect command callback.  Otherwise, call the methods
      // associated with this object.
      if (m_options.m_use_script_language) {
        Status error;
        ScriptInterpreter *script_interp = GetDebugger().GetScriptInterpreter(
            /*can_create=*/true, m_options.m_script_language);
        // Special handling for one-liner specified inline.
        if (m_options.m_use_one_liner) {
          error = script_interp->SetBreakpointCommandCallback(
              m_bp_options_vec, m_options.m_one_liner.c_str());
        } else if (!m_func_options.GetName().empty()) {
          error = script_interp->SetBreakpointCommandCallbackFunction(
              m_bp_options_vec, m_func_options.GetName().c_str(),
              m_func_options.GetStructuredData());
        } else {
          script_interp->CollectDataForBreakpointCommandCallback(
              m_bp_options_vec, result);
        }
        if (!error.Success())
          result.SetError(error);
      } else {
        // Special handling for one-liner specified inline.
        if (m_options.m_use_one_liner)
          SetBreakpointCommandCallback(m_bp_options_vec,
                                       m_options.m_one_liner.c_str());
        else
          CollectDataForBreakpointCommandCallback(m_bp_options_vec, result);
      }
    }
  }

private:
  CommandOptions m_options;
  OptionGroupPythonClassWithDict m_func_options;
  OptionGroupOptions m_all_options;

  std::vector<std::reference_wrapper<BreakpointOptions>>
      m_bp_options_vec; // This stores the
                        // breakpoint options that
                        // we are currently
  // collecting commands for.  In the CollectData... calls we need to hand this
  // off to the IOHandler, which may run asynchronously. So we have to have
  // some way to keep it alive, and not leak it. Making it an ivar of the
  // command object, which never goes away achieves this.  Note that if we were
  // able to run the same command concurrently in one interpreter we'd have to
  // make this "per invocation".  But there are many more reasons why it is not
  // in general safe to do that in lldb at present, so it isn't worthwhile to
  // come up with a more complex mechanism to address this particular weakness
  // right now.
  static const char *g_reader_instructions;
};

const char *CommandObjectBreakpointCommandAdd::g_reader_instructions =
    "Enter your debugger command(s).  Type 'DONE' to end.\n";

// CommandObjectBreakpointCommandDelete

#define LLDB_OPTIONS_breakpoint_command_delete
#include "CommandOptions.inc"

class CommandObjectBreakpointCommandDelete : public CommandObjectParsed {
public:
  CommandObjectBreakpointCommandDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "delete",
                            "Delete the set of commands from a breakpoint.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeBreakpointID);
  }

  ~CommandObjectBreakpointCommandDelete() override = default;

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
      case 'D':
        m_use_dummy = true;
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_use_dummy = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_breakpoint_command_delete_options);
    }

    // Instance variables to hold the values for command options.
    bool m_use_dummy = false;
  };

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target &target = GetSelectedOrDummyTarget(m_options.m_use_dummy);

    const BreakpointList &breakpoints = target.GetBreakpointList();
    size_t num_breakpoints = breakpoints.GetSize();

    if (num_breakpoints == 0) {
      result.AppendError("No breakpoints exist to have commands deleted");
      return;
    }

    if (command.empty()) {
      result.AppendError(
          "No breakpoint specified from which to delete the commands");
      return;
    }

    BreakpointIDList valid_bp_ids;
    CommandObjectMultiwordBreakpoint::VerifyBreakpointOrLocationIDs(
        command, &target, result, &valid_bp_ids,
        BreakpointName::Permissions::PermissionKinds::listPerm);

    if (result.Succeeded()) {
      const size_t count = valid_bp_ids.GetSize();
      for (size_t i = 0; i < count; ++i) {
        BreakpointID cur_bp_id = valid_bp_ids.GetBreakpointIDAtIndex(i);
        if (cur_bp_id.GetBreakpointID() != LLDB_INVALID_BREAK_ID) {
          Breakpoint *bp =
              target.GetBreakpointByID(cur_bp_id.GetBreakpointID()).get();
          if (cur_bp_id.GetLocationID() != LLDB_INVALID_BREAK_ID) {
            BreakpointLocationSP bp_loc_sp(
                bp->FindLocationByID(cur_bp_id.GetLocationID()));
            if (bp_loc_sp)
              bp_loc_sp->ClearCallback();
            else {
              result.AppendErrorWithFormat("Invalid breakpoint ID: %u.%u.\n",
                                           cur_bp_id.GetBreakpointID(),
                                           cur_bp_id.GetLocationID());
              return;
            }
          } else {
            bp->ClearCallback();
          }
        }
      }
    }
  }

private:
  CommandOptions m_options;
};

// CommandObjectBreakpointCommandList

class CommandObjectBreakpointCommandList : public CommandObjectParsed {
public:
  CommandObjectBreakpointCommandList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "list",
                            "List the script or set of commands to be "
                            "executed when the breakpoint is hit.",
                            nullptr, eCommandRequiresTarget) {
    AddSimpleArgumentList(eArgTypeBreakpointID);
  }

  ~CommandObjectBreakpointCommandList() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = &GetSelectedTarget();

    const BreakpointList &breakpoints = target->GetBreakpointList();
    size_t num_breakpoints = breakpoints.GetSize();

    if (num_breakpoints == 0) {
      result.AppendError("No breakpoints exist for which to list commands");
      return;
    }

    if (command.empty()) {
      result.AppendError(
          "No breakpoint specified for which to list the commands");
      return;
    }

    BreakpointIDList valid_bp_ids;
    CommandObjectMultiwordBreakpoint::VerifyBreakpointOrLocationIDs(
        command, target, result, &valid_bp_ids,
        BreakpointName::Permissions::PermissionKinds::listPerm);

    if (result.Succeeded()) {
      const size_t count = valid_bp_ids.GetSize();
      for (size_t i = 0; i < count; ++i) {
        BreakpointID cur_bp_id = valid_bp_ids.GetBreakpointIDAtIndex(i);
        if (cur_bp_id.GetBreakpointID() != LLDB_INVALID_BREAK_ID) {
          Breakpoint *bp =
              target->GetBreakpointByID(cur_bp_id.GetBreakpointID()).get();

          if (bp) {
            BreakpointLocationSP bp_loc_sp;
            if (cur_bp_id.GetLocationID() != LLDB_INVALID_BREAK_ID) {
              bp_loc_sp = bp->FindLocationByID(cur_bp_id.GetLocationID());
              if (!bp_loc_sp) {
                result.AppendErrorWithFormat("Invalid breakpoint ID: %u.%u.\n",
                                             cur_bp_id.GetBreakpointID(),
                                             cur_bp_id.GetLocationID());
                return;
              }
            }

            StreamString id_str;
            BreakpointID::GetCanonicalReference(&id_str,
                                                cur_bp_id.GetBreakpointID(),
                                                cur_bp_id.GetLocationID());
            const Baton *baton = nullptr;
            if (bp_loc_sp)
              baton =
                  bp_loc_sp
                      ->GetOptionsSpecifyingKind(BreakpointOptions::eCallback)
                      .GetBaton();
            else
              baton = bp->GetOptions().GetBaton();

            if (baton) {
              result.GetOutputStream().Printf("Breakpoint %s:\n",
                                              id_str.GetData());
              baton->GetDescription(result.GetOutputStream().AsRawOstream(),
                                    eDescriptionLevelFull,
                                    result.GetOutputStream().GetIndentLevel() +
                                        2);
            } else {
              result.AppendMessageWithFormat(
                  "Breakpoint %s does not have an associated command.\n",
                  id_str.GetData());
            }
          }
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat("Invalid breakpoint ID: %u.\n",
                                       cur_bp_id.GetBreakpointID());
        }
      }
    }
  }
};

// CommandObjectBreakpointCommand

CommandObjectBreakpointCommand::CommandObjectBreakpointCommand(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(
          interpreter, "command",
          "Commands for adding, removing and listing "
          "LLDB commands executed when a breakpoint is "
          "hit.",
          "command <sub-command> [<sub-command-options>] <breakpoint-id>") {
  CommandObjectSP add_command_object(
      new CommandObjectBreakpointCommandAdd(interpreter));
  CommandObjectSP delete_command_object(
      new CommandObjectBreakpointCommandDelete(interpreter));
  CommandObjectSP list_command_object(
      new CommandObjectBreakpointCommandList(interpreter));

  add_command_object->SetCommandName("breakpoint command add");
  delete_command_object->SetCommandName("breakpoint command delete");
  list_command_object->SetCommandName("breakpoint command list");

  LoadSubCommand("add", add_command_object);
  LoadSubCommand("delete", delete_command_object);
  LoadSubCommand("list", list_command_object);
}

CommandObjectBreakpointCommand::~CommandObjectBreakpointCommand() = default;
