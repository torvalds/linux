//===-- CommandObjectExpression.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"

#include "CommandObjectExpression.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Expression/REPL.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-enumerations.h"

using namespace lldb;
using namespace lldb_private;

CommandObjectExpression::CommandOptions::CommandOptions() = default;

CommandObjectExpression::CommandOptions::~CommandOptions() = default;

#define LLDB_OPTIONS_expression
#include "CommandOptions.inc"

Status CommandObjectExpression::CommandOptions::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;

  const int short_option = GetDefinitions()[option_idx].short_option;

  switch (short_option) {
  case 'l':
    language = Language::GetLanguageTypeFromString(option_arg);
    if (language == eLanguageTypeUnknown) {
      StreamString sstr;
      sstr.Printf("unknown language type: '%s' for expression. "
                  "List of supported languages:\n",
                  option_arg.str().c_str());

      Language::PrintSupportedLanguagesForExpressions(sstr, "  ", "\n");
      error.SetErrorString(sstr.GetString());
    }
    break;

  case 'a': {
    bool success;
    bool result;
    result = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (!success)
      error.SetErrorStringWithFormat(
          "invalid all-threads value setting: \"%s\"",
          option_arg.str().c_str());
    else
      try_all_threads = result;
  } break;

  case 'i': {
    bool success;
    bool tmp_value = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      ignore_breakpoints = tmp_value;
    else
      error.SetErrorStringWithFormat(
          "could not convert \"%s\" to a boolean value.",
          option_arg.str().c_str());
    break;
  }

  case 'j': {
    bool success;
    bool tmp_value = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      allow_jit = tmp_value;
    else
      error.SetErrorStringWithFormat(
          "could not convert \"%s\" to a boolean value.",
          option_arg.str().c_str());
    break;
  }

  case 't':
    if (option_arg.getAsInteger(0, timeout)) {
      timeout = 0;
      error.SetErrorStringWithFormat("invalid timeout setting \"%s\"",
                                     option_arg.str().c_str());
    }
    break;

  case 'u': {
    bool success;
    bool tmp_value = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      unwind_on_error = tmp_value;
    else
      error.SetErrorStringWithFormat(
          "could not convert \"%s\" to a boolean value.",
          option_arg.str().c_str());
    break;
  }

  case 'v':
    if (option_arg.empty()) {
      m_verbosity = eLanguageRuntimeDescriptionDisplayVerbosityFull;
      break;
    }
    m_verbosity = (LanguageRuntimeDescriptionDisplayVerbosity)
        OptionArgParser::ToOptionEnum(
            option_arg, GetDefinitions()[option_idx].enum_values, 0, error);
    if (!error.Success())
      error.SetErrorStringWithFormat(
          "unrecognized value for description-verbosity '%s'",
          option_arg.str().c_str());
    break;

  case 'g':
    debug = true;
    unwind_on_error = false;
    ignore_breakpoints = false;
    break;

  case 'p':
    top_level = true;
    break;

  case 'X': {
    bool success;
    bool tmp_value = OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      auto_apply_fixits = tmp_value ? eLazyBoolYes : eLazyBoolNo;
    else
      error.SetErrorStringWithFormat(
          "could not convert \"%s\" to a boolean value.",
          option_arg.str().c_str());
    break;
  }

  case '\x01': {
    bool success;
    bool persist_result =
        OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      suppress_persistent_result = !persist_result ? eLazyBoolYes : eLazyBoolNo;
    else
      error.SetErrorStringWithFormat(
          "could not convert \"%s\" to a boolean value.",
          option_arg.str().c_str());
    break;
  }

  default:
    llvm_unreachable("Unimplemented option");
  }

  return error;
}

void CommandObjectExpression::CommandOptions::OptionParsingStarting(
    ExecutionContext *execution_context) {
  auto process_sp =
      execution_context ? execution_context->GetProcessSP() : ProcessSP();
  if (process_sp) {
    ignore_breakpoints = process_sp->GetIgnoreBreakpointsInExpressions();
    unwind_on_error = process_sp->GetUnwindOnErrorInExpressions();
  } else {
    ignore_breakpoints = true;
    unwind_on_error = true;
  }

  show_summary = true;
  try_all_threads = true;
  timeout = 0;
  debug = false;
  language = eLanguageTypeUnknown;
  m_verbosity = eLanguageRuntimeDescriptionDisplayVerbosityCompact;
  auto_apply_fixits = eLazyBoolCalculate;
  top_level = false;
  allow_jit = true;
  suppress_persistent_result = eLazyBoolCalculate;
}

llvm::ArrayRef<OptionDefinition>
CommandObjectExpression::CommandOptions::GetDefinitions() {
  return llvm::ArrayRef(g_expression_options);
}

EvaluateExpressionOptions
CommandObjectExpression::CommandOptions::GetEvaluateExpressionOptions(
    const Target &target, const OptionGroupValueObjectDisplay &display_opts) {
  EvaluateExpressionOptions options;
  options.SetCoerceToId(display_opts.use_objc);
  options.SetUnwindOnError(unwind_on_error);
  options.SetIgnoreBreakpoints(ignore_breakpoints);
  options.SetKeepInMemory(true);
  options.SetUseDynamic(display_opts.use_dynamic);
  options.SetTryAllThreads(try_all_threads);
  options.SetDebug(debug);
  options.SetLanguage(language);
  options.SetExecutionPolicy(
      allow_jit ? EvaluateExpressionOptions::default_execution_policy
                : lldb_private::eExecutionPolicyNever);

  bool auto_apply_fixits;
  if (this->auto_apply_fixits == eLazyBoolCalculate)
    auto_apply_fixits = target.GetEnableAutoApplyFixIts();
  else
    auto_apply_fixits = this->auto_apply_fixits == eLazyBoolYes;

  options.SetAutoApplyFixIts(auto_apply_fixits);
  options.SetRetriesWithFixIts(target.GetNumberOfRetriesWithFixits());

  if (top_level)
    options.SetExecutionPolicy(eExecutionPolicyTopLevel);

  // If there is any chance we are going to stop and want to see what went
  // wrong with our expression, we should generate debug info
  if (!ignore_breakpoints || !unwind_on_error)
    options.SetGenerateDebugInfo(true);

  if (timeout > 0)
    options.SetTimeout(std::chrono::microseconds(timeout));
  else
    options.SetTimeout(std::nullopt);
  return options;
}

bool CommandObjectExpression::CommandOptions::ShouldSuppressResult(
    const OptionGroupValueObjectDisplay &display_opts) const {
  // Explicitly disabling persistent results takes precedence over the
  // m_verbosity/use_objc logic.
  if (suppress_persistent_result != eLazyBoolCalculate)
    return suppress_persistent_result == eLazyBoolYes;

  return display_opts.use_objc &&
         m_verbosity == eLanguageRuntimeDescriptionDisplayVerbosityCompact;
}

CommandObjectExpression::CommandObjectExpression(
    CommandInterpreter &interpreter)
    : CommandObjectRaw(interpreter, "expression",
                       "Evaluate an expression on the current "
                       "thread.  Displays any returned value "
                       "with LLDB's default formatting.",
                       "",
                       eCommandProcessMustBePaused | eCommandTryTargetAPILock),
      IOHandlerDelegate(IOHandlerDelegate::Completion::Expression),
      m_format_options(eFormatDefault),
      m_repl_option(LLDB_OPT_SET_1, false, "repl", 'r', "Drop into REPL", false,
                    true),
      m_expr_line_count(0) {
  SetHelpLong(
      R"(
Single and multi-line expressions:

)"
      "    The expression provided on the command line must be a complete expression \
with no newlines.  To evaluate a multi-line expression, \
hit a return after an empty expression, and lldb will enter the multi-line expression editor. \
Hit return on an empty line to end the multi-line expression."

      R"(

Timeouts:

)"
      "    If the expression can be evaluated statically (without running code) then it will be.  \
Otherwise, by default the expression will run on the current thread with a short timeout: \
currently .25 seconds.  If it doesn't return in that time, the evaluation will be interrupted \
and resumed with all threads running.  You can use the -a option to disable retrying on all \
threads.  You can use the -t option to set a shorter timeout."
      R"(

User defined variables:

)"
      "    You can define your own variables for convenience or to be used in subsequent expressions.  \
You define them the same way you would define variables in C.  If the first character of \
your user defined variable is a $, then the variable's value will be available in future \
expressions, otherwise it will just be available in the current expression."
      R"(

Continuing evaluation after a breakpoint:

)"
      "    If the \"-i false\" option is used, and execution is interrupted by a breakpoint hit, once \
you are done with your investigation, you can either remove the expression execution frames \
from the stack with \"thread return -x\" or if you are still interested in the expression result \
you can issue the \"continue\" command and the expression evaluation will complete and the \
expression result will be available using the \"thread.completed-expression\" key in the thread \
format."

      R"(

Examples:

    expr my_struct->a = my_array[3]
    expr -f bin -- (index * 8) + 5
    expr unsigned int $foo = 5
    expr char c[] = \"foo\"; c[0])");

  AddSimpleArgumentList(eArgTypeExpression);

  // Add the "--format" and "--gdb-format"
  m_option_group.Append(&m_format_options,
                        OptionGroupFormat::OPTION_GROUP_FORMAT |
                            OptionGroupFormat::OPTION_GROUP_GDB_FMT,
                        LLDB_OPT_SET_1);
  m_option_group.Append(&m_command_options);
  m_option_group.Append(&m_varobj_options, LLDB_OPT_SET_ALL,
                        LLDB_OPT_SET_1 | LLDB_OPT_SET_2);
  m_option_group.Append(&m_repl_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_3);
  m_option_group.Finalize();
}

CommandObjectExpression::~CommandObjectExpression() = default;

Options *CommandObjectExpression::GetOptions() { return &m_option_group; }

void CommandObjectExpression::HandleCompletion(CompletionRequest &request) {
  EvaluateExpressionOptions options;
  options.SetCoerceToId(m_varobj_options.use_objc);
  options.SetLanguage(m_command_options.language);
  options.SetExecutionPolicy(lldb_private::eExecutionPolicyNever);
  options.SetAutoApplyFixIts(false);
  options.SetGenerateDebugInfo(false);

  ExecutionContext exe_ctx(m_interpreter.GetExecutionContext());

  // Get out before we start doing things that expect a valid frame pointer.
  if (exe_ctx.GetFramePtr() == nullptr)
    return;

  Target *exe_target = exe_ctx.GetTargetPtr();
  Target &target = exe_target ? *exe_target : GetDummyTarget();

  unsigned cursor_pos = request.GetRawCursorPos();
  // Get the full user input including the suffix. The suffix is necessary
  // as OptionsWithRaw will use it to detect if the cursor is cursor is in the
  // argument part of in the raw input part of the arguments. If we cut of
  // of the suffix then "expr -arg[cursor] --" would interpret the "-arg" as
  // the raw input (as the "--" is hidden in the suffix).
  llvm::StringRef code = request.GetRawLineWithUnusedSuffix();

  const std::size_t original_code_size = code.size();

  // Remove the first token which is 'expr' or some alias/abbreviation of that.
  code = llvm::getToken(code).second.ltrim();
  OptionsWithRaw args(code);
  code = args.GetRawPart();

  // The position where the expression starts in the command line.
  assert(original_code_size >= code.size());
  std::size_t raw_start = original_code_size - code.size();

  // Check if the cursor is actually in the expression string, and if not, we
  // exit.
  // FIXME: We should complete the options here.
  if (cursor_pos < raw_start)
    return;

  // Make the cursor_pos again relative to the start of the code string.
  assert(cursor_pos >= raw_start);
  cursor_pos -= raw_start;

  auto language = exe_ctx.GetFrameRef().GetLanguage();

  Status error;
  lldb::UserExpressionSP expr(target.GetUserExpressionForLanguage(
      code, llvm::StringRef(), language, UserExpression::eResultTypeAny,
      options, nullptr, error));
  if (error.Fail())
    return;

  expr->Complete(exe_ctx, request, cursor_pos);
}

static lldb_private::Status
CanBeUsedForElementCountPrinting(ValueObject &valobj) {
  CompilerType type(valobj.GetCompilerType());
  CompilerType pointee;
  if (!type.IsPointerType(&pointee))
    return Status("as it does not refer to a pointer");
  if (pointee.IsVoidType())
    return Status("as it refers to a pointer to void");
  return Status();
}

bool CommandObjectExpression::EvaluateExpression(llvm::StringRef expr,
                                                 Stream &output_stream,
                                                 Stream &error_stream,
                                                 CommandReturnObject &result) {
  // Don't use m_exe_ctx as this might be called asynchronously after the
  // command object DoExecute has finished when doing multi-line expression
  // that use an input reader...
  ExecutionContext exe_ctx(m_interpreter.GetExecutionContext());
  Target *exe_target = exe_ctx.GetTargetPtr();
  Target &target = exe_target ? *exe_target : GetDummyTarget();

  lldb::ValueObjectSP result_valobj_sp;
  StackFrame *frame = exe_ctx.GetFramePtr();

  if (m_command_options.top_level && !m_command_options.allow_jit) {
    result.AppendErrorWithFormat(
        "Can't disable JIT compilation for top-level expressions.\n");
    return false;
  }

  EvaluateExpressionOptions eval_options =
      m_command_options.GetEvaluateExpressionOptions(target, m_varobj_options);
  // This command manually removes the result variable, make sure expression
  // evaluation doesn't do it first.
  eval_options.SetSuppressPersistentResult(false);

  ExpressionResults success = target.EvaluateExpression(
      expr, frame, result_valobj_sp, eval_options, &m_fixed_expression);

  // Only mention Fix-Its if the expression evaluator applied them.
  // Compiler errors refer to the final expression after applying Fix-It(s).
  if (!m_fixed_expression.empty() && target.GetEnableNotifyAboutFixIts()) {
    error_stream << "  Evaluated this expression after applying Fix-It(s):\n";
    error_stream << "    " << m_fixed_expression << "\n";
  }

  if (result_valobj_sp) {
    Format format = m_format_options.GetFormat();

    if (result_valobj_sp->GetError().Success()) {
      if (format != eFormatVoid) {
        if (format != eFormatDefault)
          result_valobj_sp->SetFormat(format);

        if (m_varobj_options.elem_count > 0) {
          Status error(CanBeUsedForElementCountPrinting(*result_valobj_sp));
          if (error.Fail()) {
            result.AppendErrorWithFormat(
                "expression cannot be used with --element-count %s\n",
                error.AsCString(""));
            return false;
          }
        }

        bool suppress_result =
            m_command_options.ShouldSuppressResult(m_varobj_options);

        DumpValueObjectOptions options(m_varobj_options.GetAsDumpOptions(
            m_command_options.m_verbosity, format));
        options.SetHideRootName(suppress_result);
        options.SetVariableFormatDisplayLanguage(
            result_valobj_sp->GetPreferredDisplayLanguage());

        if (llvm::Error error =
                result_valobj_sp->Dump(output_stream, options)) {
          result.AppendError(toString(std::move(error)));
          return false;
        }

        if (suppress_result)
          if (auto result_var_sp =
                  target.GetPersistentVariable(result_valobj_sp->GetName())) {
            auto language = result_valobj_sp->GetPreferredDisplayLanguage();
            if (auto *persistent_state =
                    target.GetPersistentExpressionStateForLanguage(language))
              persistent_state->RemovePersistentVariable(result_var_sp);
          }
        result.SetStatus(eReturnStatusSuccessFinishResult);
      }
    } else {
      if (result_valobj_sp->GetError().GetError() ==
          UserExpression::kNoResult) {
        if (format != eFormatVoid && GetDebugger().GetNotifyVoid()) {
          error_stream.PutCString("(void)\n");
        }

        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        const char *error_cstr = result_valobj_sp->GetError().AsCString();
        if (error_cstr && error_cstr[0]) {
          const size_t error_cstr_len = strlen(error_cstr);
          const bool ends_with_newline = error_cstr[error_cstr_len - 1] == '\n';
          if (strstr(error_cstr, "error:") != error_cstr)
            error_stream.PutCString("error: ");
          error_stream.Write(error_cstr, error_cstr_len);
          if (!ends_with_newline)
            error_stream.EOL();
        } else {
          error_stream.PutCString("error: unknown error\n");
        }

        result.SetStatus(eReturnStatusFailed);
      }
    }
  } else {
    error_stream.Printf("error: unknown error\n");
  }

  return (success != eExpressionSetupError &&
          success != eExpressionParseError);
}

void CommandObjectExpression::IOHandlerInputComplete(IOHandler &io_handler,
                                                     std::string &line) {
  io_handler.SetIsDone(true);
  StreamFileSP output_sp = io_handler.GetOutputStreamFileSP();
  StreamFileSP error_sp = io_handler.GetErrorStreamFileSP();

  CommandReturnObject return_obj(
      GetCommandInterpreter().GetDebugger().GetUseColor());
  EvaluateExpression(line.c_str(), *output_sp, *error_sp, return_obj);
  if (output_sp)
    output_sp->Flush();
  if (error_sp)
    error_sp->Flush();
}

bool CommandObjectExpression::IOHandlerIsInputComplete(IOHandler &io_handler,
                                                       StringList &lines) {
  // An empty lines is used to indicate the end of input
  const size_t num_lines = lines.GetSize();
  if (num_lines > 0 && lines[num_lines - 1].empty()) {
    // Remove the last empty line from "lines" so it doesn't appear in our
    // resulting input and return true to indicate we are done getting lines
    lines.PopBack();
    return true;
  }
  return false;
}

void CommandObjectExpression::GetMultilineExpression() {
  m_expr_lines.clear();
  m_expr_line_count = 0;

  Debugger &debugger = GetCommandInterpreter().GetDebugger();
  bool color_prompt = debugger.GetUseColor();
  const bool multiple_lines = true; // Get multiple lines
  IOHandlerSP io_handler_sp(
      new IOHandlerEditline(debugger, IOHandler::Type::Expression,
                            "lldb-expr", // Name of input reader for history
                            llvm::StringRef(), // No prompt
                            llvm::StringRef(), // Continuation prompt
                            multiple_lines, color_prompt,
                            1, // Show line numbers starting at 1
                            *this));

  StreamFileSP output_sp = io_handler_sp->GetOutputStreamFileSP();
  if (output_sp) {
    output_sp->PutCString(
        "Enter expressions, then terminate with an empty line to evaluate:\n");
    output_sp->Flush();
  }
  debugger.RunIOHandlerAsync(io_handler_sp);
}

static EvaluateExpressionOptions
GetExprOptions(ExecutionContext &ctx,
               CommandObjectExpression::CommandOptions command_options) {
  command_options.OptionParsingStarting(&ctx);

  // Default certain settings for REPL regardless of the global settings.
  command_options.unwind_on_error = false;
  command_options.ignore_breakpoints = false;
  command_options.debug = false;

  EvaluateExpressionOptions expr_options;
  expr_options.SetUnwindOnError(command_options.unwind_on_error);
  expr_options.SetIgnoreBreakpoints(command_options.ignore_breakpoints);
  expr_options.SetTryAllThreads(command_options.try_all_threads);

  if (command_options.timeout > 0)
    expr_options.SetTimeout(std::chrono::microseconds(command_options.timeout));
  else
    expr_options.SetTimeout(std::nullopt);

  return expr_options;
}

void CommandObjectExpression::DoExecute(llvm::StringRef command,
                                        CommandReturnObject &result) {
  m_fixed_expression.clear();
  auto exe_ctx = GetCommandInterpreter().GetExecutionContext();
  m_option_group.NotifyOptionParsingStarting(&exe_ctx);

  if (command.empty()) {
    GetMultilineExpression();
    return;
  }

  OptionsWithRaw args(command);
  llvm::StringRef expr = args.GetRawPart();

  if (args.HasArgs()) {
    if (!ParseOptionsAndNotify(args.GetArgs(), result, m_option_group, exe_ctx))
      return;

    if (m_repl_option.GetOptionValue().GetCurrentValue()) {
      Target &target = GetSelectedOrDummyTarget();
      // Drop into REPL
      m_expr_lines.clear();
      m_expr_line_count = 0;

      Debugger &debugger = target.GetDebugger();

      // Check if the LLDB command interpreter is sitting on top of a REPL
      // that launched it...
      if (debugger.CheckTopIOHandlerTypes(IOHandler::Type::CommandInterpreter,
                                          IOHandler::Type::REPL)) {
        // the LLDB command interpreter is sitting on top of a REPL that
        // launched it, so just say the command interpreter is done and
        // fall back to the existing REPL
        m_interpreter.GetIOHandler(false)->SetIsDone(true);
      } else {
        // We are launching the REPL on top of the current LLDB command
        // interpreter, so just push one
        bool initialize = false;
        Status repl_error;
        REPLSP repl_sp(target.GetREPL(repl_error, m_command_options.language,
                                       nullptr, false));

        if (!repl_sp) {
          initialize = true;
          repl_sp = target.GetREPL(repl_error, m_command_options.language,
                                    nullptr, true);
          if (!repl_error.Success()) {
            result.SetError(repl_error);
            return;
          }
        }

        if (repl_sp) {
          if (initialize) {
            repl_sp->SetEvaluateOptions(
                GetExprOptions(exe_ctx, m_command_options));
            repl_sp->SetFormatOptions(m_format_options);
            repl_sp->SetValueObjectDisplayOptions(m_varobj_options);
          }

          IOHandlerSP io_handler_sp(repl_sp->GetIOHandler());
          io_handler_sp->SetIsDone(false);
          debugger.RunIOHandlerAsync(io_handler_sp);
        } else {
          repl_error.SetErrorStringWithFormat(
              "Couldn't create a REPL for %s",
              Language::GetNameForLanguageType(m_command_options.language));
          result.SetError(repl_error);
          return;
        }
      }
    }
    // No expression following options
    else if (expr.empty()) {
      GetMultilineExpression();
      return;
    }
  }

  Target &target = GetSelectedOrDummyTarget();
  if (EvaluateExpression(expr, result.GetOutputStream(),
                         result.GetErrorStream(), result)) {

    if (!m_fixed_expression.empty() && target.GetEnableNotifyAboutFixIts()) {
      CommandHistory &history = m_interpreter.GetCommandHistory();
      // FIXME: Can we figure out what the user actually typed (e.g. some alias
      // for expr???)
      // If we can it would be nice to show that.
      std::string fixed_command("expression ");
      if (args.HasArgs()) {
        // Add in any options that might have been in the original command:
        fixed_command.append(std::string(args.GetArgStringWithDelimiter()));
        fixed_command.append(m_fixed_expression);
      } else
        fixed_command.append(m_fixed_expression);
      history.AppendString(fixed_command);
    }
    return;
  }
  result.SetStatus(eReturnStatusFailed);
}
