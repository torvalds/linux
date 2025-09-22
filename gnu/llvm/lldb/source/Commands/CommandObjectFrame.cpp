//===-- CommandObjectFrame.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "CommandObjectFrame.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/ValueObjectPrinter.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"
#include "lldb/Interpreter/OptionGroupVariable.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Args.h"

#include <memory>
#include <optional>
#include <string>

using namespace lldb;
using namespace lldb_private;

#pragma mark CommandObjectFrameDiagnose

// CommandObjectFrameInfo

// CommandObjectFrameDiagnose

#define LLDB_OPTIONS_frame_diag
#include "CommandOptions.inc"

class CommandObjectFrameDiagnose : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 'r':
        reg = ConstString(option_arg);
        break;

      case 'a': {
        address.emplace();
        if (option_arg.getAsInteger(0, *address)) {
          address.reset();
          error.SetErrorStringWithFormat("invalid address argument '%s'",
                                         option_arg.str().c_str());
        }
      } break;

      case 'o': {
        offset.emplace();
        if (option_arg.getAsInteger(0, *offset)) {
          offset.reset();
          error.SetErrorStringWithFormat("invalid offset argument '%s'",
                                         option_arg.str().c_str());
        }
      } break;

      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      address.reset();
      reg.reset();
      offset.reset();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_frame_diag_options);
    }

    // Options.
    std::optional<lldb::addr_t> address;
    std::optional<ConstString> reg;
    std::optional<int64_t> offset;
  };

  CommandObjectFrameDiagnose(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame diagnose",
                            "Try to determine what path the current stop "
                            "location used to get to a register or address",
                            nullptr,
                            eCommandRequiresThread | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypeFrameIndex, eArgRepeatOptional);
  }

  ~CommandObjectFrameDiagnose() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Thread *thread = m_exe_ctx.GetThreadPtr();
    StackFrameSP frame_sp = thread->GetSelectedFrame(SelectMostRelevantFrame);

    ValueObjectSP valobj_sp;

    if (m_options.address) {
      if (m_options.reg || m_options.offset) {
        result.AppendError(
            "`frame diagnose --address` is incompatible with other arguments.");
        return;
      }
      valobj_sp = frame_sp->GuessValueForAddress(*m_options.address);
    } else if (m_options.reg) {
      valobj_sp = frame_sp->GuessValueForRegisterAndOffset(
          *m_options.reg, m_options.offset.value_or(0));
    } else {
      StopInfoSP stop_info_sp = thread->GetStopInfo();
      if (!stop_info_sp) {
        result.AppendError("No arguments provided, and no stop info.");
        return;
      }

      valobj_sp = StopInfo::GetCrashingDereference(stop_info_sp);
    }

    if (!valobj_sp) {
      result.AppendError("No diagnosis available.");
      return;
    }

    DumpValueObjectOptions::DeclPrintingHelper helper =
        [&valobj_sp](ConstString type, ConstString var,
                     const DumpValueObjectOptions &opts,
                     Stream &stream) -> bool {
      const ValueObject::GetExpressionPathFormat format = ValueObject::
          GetExpressionPathFormat::eGetExpressionPathFormatHonorPointers;
      valobj_sp->GetExpressionPath(stream, format);
      stream.PutCString(" =");
      return true;
    };

    DumpValueObjectOptions options;
    options.SetDeclPrintingHelper(helper);
    // We've already handled the case where the value object sp is null, so
    // this is just to make sure future changes don't skip that:
    assert(valobj_sp.get() && "Must have a valid ValueObject to print");
    ValueObjectPrinter printer(*valobj_sp, &result.GetOutputStream(),
                               options);
    if (llvm::Error error = printer.PrintValueObject())
      result.AppendError(toString(std::move(error)));
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectFrameInfo

// CommandObjectFrameInfo

class CommandObjectFrameInfo : public CommandObjectParsed {
public:
  CommandObjectFrameInfo(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame info",
                            "List information about the current "
                            "stack frame in the current thread.",
                            "frame info",
                            eCommandRequiresFrame | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {}

  ~CommandObjectFrameInfo() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    m_exe_ctx.GetFrameRef().DumpUsingSettingsFormat(&result.GetOutputStream());
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectFrameSelect

// CommandObjectFrameSelect

#define LLDB_OPTIONS_frame_select
#include "CommandOptions.inc"

class CommandObjectFrameSelect : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 'r': {
        int32_t offset = 0;
        if (option_arg.getAsInteger(0, offset) || offset == INT32_MIN) {
          error.SetErrorStringWithFormat("invalid frame offset argument '%s'",
                                         option_arg.str().c_str());
        } else
          relative_frame_offset = offset;
        break;
      }

      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      relative_frame_offset.reset();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_frame_select_options);
    }

    std::optional<int32_t> relative_frame_offset;
  };

  CommandObjectFrameSelect(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame select",
                            "Select the current stack frame by "
                            "index from within the current thread "
                            "(see 'thread backtrace'.)",
                            nullptr,
                            eCommandRequiresThread | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    AddSimpleArgumentList(eArgTypeFrameIndex, eArgRepeatOptional);
  }

  ~CommandObjectFrameSelect() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    // No need to check "thread" for validity as eCommandRequiresThread ensures
    // it is valid
    Thread *thread = m_exe_ctx.GetThreadPtr();

    uint32_t frame_idx = UINT32_MAX;
    if (m_options.relative_frame_offset) {
      // The one and only argument is a signed relative frame index
      frame_idx = thread->GetSelectedFrameIndex(SelectMostRelevantFrame);
      if (frame_idx == UINT32_MAX)
        frame_idx = 0;

      if (*m_options.relative_frame_offset < 0) {
        if (static_cast<int32_t>(frame_idx) >=
            -*m_options.relative_frame_offset)
          frame_idx += *m_options.relative_frame_offset;
        else {
          if (frame_idx == 0) {
            // If you are already at the bottom of the stack, then just warn
            // and don't reset the frame.
            result.AppendError("Already at the bottom of the stack.");
            return;
          } else
            frame_idx = 0;
        }
      } else if (*m_options.relative_frame_offset > 0) {
        // I don't want "up 20" where "20" takes you past the top of the stack
        // to produce an error, but rather to just go to the top.  OTOH, start
        // by seeing if the requested frame exists, in which case we can avoid 
        // counting the stack here...
        const uint32_t frame_requested = frame_idx 
            + *m_options.relative_frame_offset;
        StackFrameSP frame_sp = thread->GetStackFrameAtIndex(frame_requested);
        if (frame_sp)
          frame_idx = frame_requested;
        else {
          // The request went past the stack, so handle that case:
          const uint32_t num_frames = thread->GetStackFrameCount();
          if (static_cast<int32_t>(num_frames - frame_idx) >
              *m_options.relative_frame_offset)
          frame_idx += *m_options.relative_frame_offset;
          else {
            if (frame_idx == num_frames - 1) {
              // If we are already at the top of the stack, just warn and don't
              // reset the frame.
              result.AppendError("Already at the top of the stack.");
              return;
            } else
              frame_idx = num_frames - 1;
          }
        }
      }
    } else {
      if (command.GetArgumentCount() > 1) {
        result.AppendErrorWithFormat(
            "too many arguments; expected frame-index, saw '%s'.\n",
            command[0].c_str());
        m_options.GenerateOptionUsage(
            result.GetErrorStream(), *this,
            GetCommandInterpreter().GetDebugger().GetTerminalWidth());
        return;
      }

      if (command.GetArgumentCount() == 1) {
        if (command[0].ref().getAsInteger(0, frame_idx)) {
          result.AppendErrorWithFormat("invalid frame index argument '%s'.",
                                       command[0].c_str());
          return;
        }
      } else if (command.GetArgumentCount() == 0) {
        frame_idx = thread->GetSelectedFrameIndex(SelectMostRelevantFrame);
        if (frame_idx == UINT32_MAX) {
          frame_idx = 0;
        }
      }
    }

    bool success = thread->SetSelectedFrameByIndexNoisily(
        frame_idx, result.GetOutputStream());
    if (success) {
      m_exe_ctx.SetFrameSP(thread->GetSelectedFrame(SelectMostRelevantFrame));
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendErrorWithFormat("Frame index (%u) out of range.\n",
                                   frame_idx);
    }
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectFrameVariable
// List images with associated information
class CommandObjectFrameVariable : public CommandObjectParsed {
public:
  CommandObjectFrameVariable(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "frame variable",
            "Show variables for the current stack frame. Defaults to all "
            "arguments and local variables in scope. Names of argument, "
            "local, file static and file global variables can be specified.",
            nullptr,
            eCommandRequiresFrame | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused |
                eCommandRequiresProcess),
        m_option_variable(
            true), // Include the frame specific options by passing "true"
        m_option_format(eFormatDefault) {
    SetHelpLong(R"(
Children of aggregate variables can be specified such as 'var->child.x'.  In
'frame variable', the operators -> and [] do not invoke operator overloads if
they exist, but directly access the specified element.  If you want to trigger
operator overloads use the expression command to print the variable instead.

It is worth noting that except for overloaded operators, when printing local
variables 'expr local_var' and 'frame var local_var' produce the same results.
However, 'frame variable' is more efficient, since it uses debug information and
memory reads directly, rather than parsing and evaluating an expression, which
may even involve JITing and running code in the target program.)");

    AddSimpleArgumentList(eArgTypeVarName, eArgRepeatStar);

    m_option_group.Append(&m_option_variable, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_format,
                          OptionGroupFormat::OPTION_GROUP_FORMAT |
                              OptionGroupFormat::OPTION_GROUP_GDB_FMT,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_varobj_options, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectFrameVariable() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  llvm::StringRef GetScopeString(VariableSP var_sp) {
    if (!var_sp)
      return llvm::StringRef();

    switch (var_sp->GetScope()) {
    case eValueTypeVariableGlobal:
      return "GLOBAL: ";
    case eValueTypeVariableStatic:
      return "STATIC: ";
    case eValueTypeVariableArgument:
      return "ARG: ";
    case eValueTypeVariableLocal:
      return "LOCAL: ";
    case eValueTypeVariableThreadLocal:
      return "THREAD: ";
    default:
      break;
    }

    return llvm::StringRef();
  }

  /// Returns true if `scope` matches any of the options in `m_option_variable`.
  bool ScopeRequested(lldb::ValueType scope) {
    switch (scope) {
    case eValueTypeVariableGlobal:
    case eValueTypeVariableStatic:
      return m_option_variable.show_globals;
    case eValueTypeVariableArgument:
      return m_option_variable.show_args;
    case eValueTypeVariableLocal:
      return m_option_variable.show_locals;
    case eValueTypeInvalid:
    case eValueTypeRegister:
    case eValueTypeRegisterSet:
    case eValueTypeConstResult:
    case eValueTypeVariableThreadLocal:
    case eValueTypeVTable:
    case eValueTypeVTableEntry:
      return false;
    }
    llvm_unreachable("Unexpected scope value");
  }

  /// Finds all the variables in `all_variables` whose name matches `regex`,
  /// inserting them into `matches`. Variables already contained in `matches`
  /// are not inserted again.
  /// Nullopt is returned in case of no matches.
  /// A sub-range of `matches` with all newly inserted variables is returned.
  /// This may be empty if all matches were already contained in `matches`.
  std::optional<llvm::ArrayRef<VariableSP>>
  findUniqueRegexMatches(RegularExpression &regex,
                         VariableList &matches,
                         const VariableList &all_variables) {
    bool any_matches = false;
    const size_t previous_num_vars = matches.GetSize();

    for (const VariableSP &var : all_variables) {
      if (!var->NameMatches(regex) || !ScopeRequested(var->GetScope()))
        continue;
      any_matches = true;
      matches.AddVariableIfUnique(var);
    }

    if (any_matches)
      return matches.toArrayRef().drop_front(previous_num_vars);
    return std::nullopt;
  }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    // No need to check "frame" for validity as eCommandRequiresFrame ensures
    // it is valid
    StackFrame *frame = m_exe_ctx.GetFramePtr();

    Stream &s = result.GetOutputStream();

    // Using a regex should behave like looking for an exact name match: it
    // also finds globals.
    m_option_variable.show_globals |= m_option_variable.use_regex;

    // Be careful about the stack frame, if any summary formatter runs code, it
    // might clear the StackFrameList for the thread.  So hold onto a shared
    // pointer to the frame so it stays alive.

    Status error;
    VariableList *variable_list =
        frame->GetVariableList(m_option_variable.show_globals, &error);

    if (error.Fail() && (!variable_list || variable_list->GetSize() == 0)) {
      result.AppendError(error.AsCString());

    }
    ValueObjectSP valobj_sp;

    TypeSummaryImplSP summary_format_sp;
    if (!m_option_variable.summary.IsCurrentValueEmpty())
      DataVisualization::NamedSummaryFormats::GetSummaryFormat(
          ConstString(m_option_variable.summary.GetCurrentValue()),
          summary_format_sp);
    else if (!m_option_variable.summary_string.IsCurrentValueEmpty())
      summary_format_sp = std::make_shared<StringSummaryFormat>(
          TypeSummaryImpl::Flags(),
          m_option_variable.summary_string.GetCurrentValue());

    DumpValueObjectOptions options(m_varobj_options.GetAsDumpOptions(
        eLanguageRuntimeDescriptionDisplayVerbosityFull, eFormatDefault,
        summary_format_sp));

    const SymbolContext &sym_ctx =
        frame->GetSymbolContext(eSymbolContextFunction);
    if (sym_ctx.function && sym_ctx.function->IsTopLevelFunction())
      m_option_variable.show_globals = true;

    if (variable_list) {
      const Format format = m_option_format.GetFormat();
      options.SetFormat(format);

      if (!command.empty()) {
        VariableList regex_var_list;

        // If we have any args to the variable command, we will make variable
        // objects from them...
        for (auto &entry : command) {
          if (m_option_variable.use_regex) {
            llvm::StringRef name_str = entry.ref();
            RegularExpression regex(name_str);
            if (regex.IsValid()) {
              std::optional<llvm::ArrayRef<VariableSP>> results =
                  findUniqueRegexMatches(regex, regex_var_list, *variable_list);
              if (!results) {
                result.AppendErrorWithFormat(
                    "no variables matched the regular expression '%s'.",
                    entry.c_str());
                continue;
              }
              for (const VariableSP &var_sp : *results) {
                valobj_sp = frame->GetValueObjectForFrameVariable(
                    var_sp, m_varobj_options.use_dynamic);
                if (valobj_sp) {
                  std::string scope_string;
                  if (m_option_variable.show_scope)
                    scope_string = GetScopeString(var_sp).str();

                  if (!scope_string.empty())
                    s.PutCString(scope_string);

                  if (m_option_variable.show_decl &&
                      var_sp->GetDeclaration().GetFile()) {
                    bool show_fullpaths = false;
                    bool show_module = true;
                    if (var_sp->DumpDeclaration(&s, show_fullpaths,
                                                show_module))
                      s.PutCString(": ");
                  }
                  auto &strm = result.GetOutputStream();
                  if (llvm::Error error = valobj_sp->Dump(strm, options))
                    result.AppendError(toString(std::move(error)));
                }
              }
            } else {
              if (llvm::Error err = regex.GetError())
                result.AppendError(llvm::toString(std::move(err)));
              else
                result.AppendErrorWithFormat(
                    "unknown regex error when compiling '%s'", entry.c_str());
            }
          } else // No regex, either exact variable names or variable
                 // expressions.
          {
            Status error;
            uint32_t expr_path_options =
                StackFrame::eExpressionPathOptionCheckPtrVsMember |
                StackFrame::eExpressionPathOptionsAllowDirectIVarAccess |
                StackFrame::eExpressionPathOptionsInspectAnonymousUnions;
            lldb::VariableSP var_sp;
            valobj_sp = frame->GetValueForVariableExpressionPath(
                entry.ref(), m_varobj_options.use_dynamic, expr_path_options,
                var_sp, error);
            if (valobj_sp) {
              std::string scope_string;
              if (m_option_variable.show_scope)
                scope_string = GetScopeString(var_sp).str();

              if (!scope_string.empty())
                s.PutCString(scope_string);
              if (m_option_variable.show_decl && var_sp &&
                  var_sp->GetDeclaration().GetFile()) {
                var_sp->GetDeclaration().DumpStopContext(&s, false);
                s.PutCString(": ");
              }

              options.SetFormat(format);
              options.SetVariableFormatDisplayLanguage(
                  valobj_sp->GetPreferredDisplayLanguage());

              Stream &output_stream = result.GetOutputStream();
              options.SetRootValueObjectName(
                  valobj_sp->GetParent() ? entry.c_str() : nullptr);
              if (llvm::Error error = valobj_sp->Dump(output_stream, options))
                result.AppendError(toString(std::move(error)));
            } else {
              if (auto error_cstr = error.AsCString(nullptr))
                result.AppendError(error_cstr);
              else
                result.AppendErrorWithFormat(
                    "unable to find any variable expression path that matches "
                    "'%s'.",
                    entry.c_str());
            }
          }
        }
      } else // No command arg specified.  Use variable_list, instead.
      {
        const size_t num_variables = variable_list->GetSize();
        if (num_variables > 0) {
          for (size_t i = 0; i < num_variables; i++) {
            VariableSP var_sp = variable_list->GetVariableAtIndex(i);
            if (!ScopeRequested(var_sp->GetScope()))
                continue;
            std::string scope_string;
            if (m_option_variable.show_scope)
              scope_string = GetScopeString(var_sp).str();

            // Use the variable object code to make sure we are using the same
            // APIs as the public API will be using...
            valobj_sp = frame->GetValueObjectForFrameVariable(
                var_sp, m_varobj_options.use_dynamic);
            if (valobj_sp) {
              // When dumping all variables, don't print any variables that are
              // not in scope to avoid extra unneeded output
              if (valobj_sp->IsInScope()) {
                if (!valobj_sp->GetTargetSP()
                         ->GetDisplayRuntimeSupportValues() &&
                    valobj_sp->IsRuntimeSupportValue())
                  continue;

                if (!scope_string.empty())
                  s.PutCString(scope_string);

                if (m_option_variable.show_decl &&
                    var_sp->GetDeclaration().GetFile()) {
                  var_sp->GetDeclaration().DumpStopContext(&s, false);
                  s.PutCString(": ");
                }

                options.SetFormat(format);
                options.SetVariableFormatDisplayLanguage(
                    valobj_sp->GetPreferredDisplayLanguage());
                options.SetRootValueObjectName(
                    var_sp ? var_sp->GetName().AsCString() : nullptr);
                if (llvm::Error error =
                        valobj_sp->Dump(result.GetOutputStream(), options))
                  result.AppendError(toString(std::move(error)));
              }
            }
          }
        }
      }
      if (result.GetStatus() != eReturnStatusFailed)
        result.SetStatus(eReturnStatusSuccessFinishResult);
    }

    if (m_option_variable.show_recognized_args) {
      auto recognized_frame = frame->GetRecognizedFrame();
      if (recognized_frame) {
        ValueObjectListSP recognized_arg_list =
            recognized_frame->GetRecognizedArguments();
        if (recognized_arg_list) {
          for (auto &rec_value_sp : recognized_arg_list->GetObjects()) {
            options.SetFormat(m_option_format.GetFormat());
            options.SetVariableFormatDisplayLanguage(
                rec_value_sp->GetPreferredDisplayLanguage());
            options.SetRootValueObjectName(rec_value_sp->GetName().AsCString());
            if (llvm::Error error =
                    rec_value_sp->Dump(result.GetOutputStream(), options))
              result.AppendError(toString(std::move(error)));
          }
        }
      }
    }

    m_interpreter.PrintWarningsIfNecessary(result.GetOutputStream(),
                                           m_cmd_name);

    // Increment statistics.
    TargetStats &target_stats = GetSelectedOrDummyTarget().GetStatistics();
    if (result.Succeeded())
      target_stats.GetFrameVariableStats().NotifySuccess();
    else
      target_stats.GetFrameVariableStats().NotifyFailure();
  }

  OptionGroupOptions m_option_group;
  OptionGroupVariable m_option_variable;
  OptionGroupFormat m_option_format;
  OptionGroupValueObjectDisplay m_varobj_options;
};

#pragma mark CommandObjectFrameRecognizer

#define LLDB_OPTIONS_frame_recognizer_add
#include "CommandOptions.inc"

class CommandObjectFrameRecognizerAdd : public CommandObjectParsed {
private:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;
    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'f': {
        bool value, success;
        value = OptionArgParser::ToBoolean(option_arg, true, &success);
        if (success) {
          m_first_instruction_only = value;
        } else {
          error.SetErrorStringWithFormat(
              "invalid boolean value '%s' passed for -f option",
              option_arg.str().c_str());
        }
      } break;
      case 'l':
        m_class_name = std::string(option_arg);
        break;
      case 's':
        m_module = std::string(option_arg);
        break;
      case 'n':
        m_symbols.push_back(std::string(option_arg));
        break;
      case 'x':
        m_regex = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_module = "";
      m_symbols.clear();
      m_class_name = "";
      m_regex = false;
      m_first_instruction_only = true;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_frame_recognizer_add_options);
    }

    // Instance variables to hold the values for command options.
    std::string m_class_name;
    std::string m_module;
    std::vector<std::string> m_symbols;
    bool m_regex;
    bool m_first_instruction_only;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override;

public:
  CommandObjectFrameRecognizerAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame recognizer add",
                            "Add a new frame recognizer.", nullptr) {
    SetHelpLong(R"(
Frame recognizers allow for retrieving information about special frames based on
ABI, arguments or other special properties of that frame, even without source
code or debug info. Currently, one use case is to extract function arguments
that would otherwise be unaccesible, or augment existing arguments.

Adding a custom frame recognizer is possible by implementing a Python class
and using the 'frame recognizer add' command. The Python class should have a
'get_recognized_arguments' method and it will receive an argument of type
lldb.SBFrame representing the current frame that we are trying to recognize.
The method should return a (possibly empty) list of lldb.SBValue objects that
represent the recognized arguments.

An example of a recognizer that retrieves the file descriptor values from libc
functions 'read', 'write' and 'close' follows:

  class LibcFdRecognizer(object):
    def get_recognized_arguments(self, frame):
      if frame.name in ["read", "write", "close"]:
        fd = frame.EvaluateExpression("$arg1").unsigned
        target = frame.thread.process.target
        value = target.CreateValueFromExpression("fd", "(int)%d" % fd)
        return [value]
      return []

The file containing this implementation can be imported via 'command script
import' and then we can register this recognizer with 'frame recognizer add'.
It's important to restrict the recognizer to the libc library (which is
libsystem_kernel.dylib on macOS) to avoid matching functions with the same name
in other modules:

(lldb) command script import .../fd_recognizer.py
(lldb) frame recognizer add -l fd_recognizer.LibcFdRecognizer -n read -s libsystem_kernel.dylib

When the program is stopped at the beginning of the 'read' function in libc, we
can view the recognizer arguments in 'frame variable':

(lldb) b read
(lldb) r
Process 1234 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.3
    frame #0: 0x00007fff06013ca0 libsystem_kernel.dylib`read
(lldb) frame variable
(int) fd = 3

    )");
  }
  ~CommandObjectFrameRecognizerAdd() override = default;
};

void CommandObjectFrameRecognizerAdd::DoExecute(Args &command,
                                                CommandReturnObject &result) {
#if LLDB_ENABLE_PYTHON
  if (m_options.m_class_name.empty()) {
    result.AppendErrorWithFormat(
        "%s needs a Python class name (-l argument).\n", m_cmd_name.c_str());
    return;
  }

  if (m_options.m_module.empty()) {
    result.AppendErrorWithFormat("%s needs a module name (-s argument).\n",
                                 m_cmd_name.c_str());
    return;
  }

  if (m_options.m_symbols.empty()) {
    result.AppendErrorWithFormat(
        "%s needs at least one symbol name (-n argument).\n",
        m_cmd_name.c_str());
    return;
  }

  if (m_options.m_regex && m_options.m_symbols.size() > 1) {
    result.AppendErrorWithFormat(
        "%s needs only one symbol regular expression (-n argument).\n",
        m_cmd_name.c_str());
    return;
  }

  ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();

  if (interpreter &&
      !interpreter->CheckObjectExists(m_options.m_class_name.c_str())) {
    result.AppendWarning("The provided class does not exist - please define it "
                         "before attempting to use this frame recognizer");
  }

  StackFrameRecognizerSP recognizer_sp =
      StackFrameRecognizerSP(new ScriptedStackFrameRecognizer(
          interpreter, m_options.m_class_name.c_str()));
  if (m_options.m_regex) {
    auto module =
        RegularExpressionSP(new RegularExpression(m_options.m_module));
    auto func =
        RegularExpressionSP(new RegularExpression(m_options.m_symbols.front()));
    GetSelectedOrDummyTarget().GetFrameRecognizerManager().AddRecognizer(
        recognizer_sp, module, func, m_options.m_first_instruction_only);
  } else {
    auto module = ConstString(m_options.m_module);
    std::vector<ConstString> symbols(m_options.m_symbols.begin(),
                                     m_options.m_symbols.end());
    GetSelectedOrDummyTarget().GetFrameRecognizerManager().AddRecognizer(
        recognizer_sp, module, symbols, m_options.m_first_instruction_only);
  }
#endif

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
}

class CommandObjectFrameRecognizerClear : public CommandObjectParsed {
public:
  CommandObjectFrameRecognizerClear(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame recognizer clear",
                            "Delete all frame recognizers.", nullptr) {}

  ~CommandObjectFrameRecognizerClear() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    GetSelectedOrDummyTarget()
        .GetFrameRecognizerManager()
        .RemoveAllRecognizers();
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

class CommandObjectFrameRecognizerDelete : public CommandObjectParsed {
public:
  CommandObjectFrameRecognizerDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame recognizer delete",
                            "Delete an existing frame recognizer by id.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeRecognizerID);
  }

  ~CommandObjectFrameRecognizerDelete() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex() != 0)
      return;

    GetSelectedOrDummyTarget().GetFrameRecognizerManager().ForEach(
        [&request](uint32_t rid, std::string rname, std::string module,
                   llvm::ArrayRef<lldb_private::ConstString> symbols,
                   bool regexp) {
          StreamString strm;
          if (rname.empty())
            rname = "(internal)";

          strm << rname;
          if (!module.empty())
            strm << ", module " << module;
          if (!symbols.empty())
            for (auto &symbol : symbols)
              strm << ", symbol " << symbol;
          if (regexp)
            strm << " (regexp)";

          request.TryCompleteCurrentArg(std::to_string(rid), strm.GetString());
        });
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.GetArgumentCount() == 0) {
      if (!m_interpreter.Confirm(
              "About to delete all frame recognizers, do you want to do that?",
              true)) {
        result.AppendMessage("Operation cancelled...");
        return;
      }

      GetSelectedOrDummyTarget()
          .GetFrameRecognizerManager()
          .RemoveAllRecognizers();
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return;
    }

    if (command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat("'%s' takes zero or one arguments.\n",
                                   m_cmd_name.c_str());
      return;
    }

    uint32_t recognizer_id;
    if (!llvm::to_integer(command.GetArgumentAtIndex(0), recognizer_id)) {
      result.AppendErrorWithFormat("'%s' is not a valid recognizer id.\n",
                                   command.GetArgumentAtIndex(0));
      return;
    }

    if (!GetSelectedOrDummyTarget()
             .GetFrameRecognizerManager()
             .RemoveRecognizerWithID(recognizer_id)) {
      result.AppendErrorWithFormat("'%s' is not a valid recognizer id.\n",
                                   command.GetArgumentAtIndex(0));
      return;
    }
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

class CommandObjectFrameRecognizerList : public CommandObjectParsed {
public:
  CommandObjectFrameRecognizerList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "frame recognizer list",
                            "Show a list of active frame recognizers.",
                            nullptr) {}

  ~CommandObjectFrameRecognizerList() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    bool any_printed = false;
    GetSelectedOrDummyTarget().GetFrameRecognizerManager().ForEach(
        [&result, &any_printed](
            uint32_t recognizer_id, std::string name, std::string module,
            llvm::ArrayRef<ConstString> symbols, bool regexp) {
          Stream &stream = result.GetOutputStream();

          if (name.empty())
            name = "(internal)";

          stream << std::to_string(recognizer_id) << ": " << name;
          if (!module.empty())
            stream << ", module " << module;
          if (!symbols.empty())
            for (auto &symbol : symbols)
              stream << ", symbol " << symbol;
          if (regexp)
            stream << " (regexp)";

          stream.EOL();
          stream.Flush();

          any_printed = true;
        });

    if (any_printed)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else {
      result.GetOutputStream().PutCString("no matching results found.\n");
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    }
  }
};

class CommandObjectFrameRecognizerInfo : public CommandObjectParsed {
public:
  CommandObjectFrameRecognizerInfo(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "frame recognizer info",
            "Show which frame recognizer is applied a stack frame (if any).",
            nullptr) {
    AddSimpleArgumentList(eArgTypeFrameIndex);
  }

  ~CommandObjectFrameRecognizerInfo() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const char *frame_index_str = command.GetArgumentAtIndex(0);
    uint32_t frame_index;
    if (!llvm::to_integer(frame_index_str, frame_index)) {
      result.AppendErrorWithFormat("'%s' is not a valid frame index.",
                                   frame_index_str);
      return;
    }

    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process");
      return;
    }
    Thread *thread = m_exe_ctx.GetThreadPtr();
    if (thread == nullptr) {
      result.AppendError("no thread");
      return;
    }
    if (command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat(
          "'%s' takes exactly one frame index argument.\n", m_cmd_name.c_str());
      return;
    }

    StackFrameSP frame_sp = thread->GetStackFrameAtIndex(frame_index);
    if (!frame_sp) {
      result.AppendErrorWithFormat("no frame with index %u", frame_index);
      return;
    }

    auto recognizer = GetSelectedOrDummyTarget()
                          .GetFrameRecognizerManager()
                          .GetRecognizerForFrame(frame_sp);

    Stream &output_stream = result.GetOutputStream();
    output_stream.Printf("frame %d ", frame_index);
    if (recognizer) {
      output_stream << "is recognized by ";
      output_stream << recognizer->GetName();
    } else {
      output_stream << "not recognized by any recognizer";
    }
    output_stream.EOL();
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

class CommandObjectFrameRecognizer : public CommandObjectMultiword {
public:
  CommandObjectFrameRecognizer(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "frame recognizer",
            "Commands for editing and viewing frame recognizers.",
            "frame recognizer [<sub-command-options>] ") {
    LoadSubCommand("add", CommandObjectSP(new CommandObjectFrameRecognizerAdd(
                              interpreter)));
    LoadSubCommand(
        "clear",
        CommandObjectSP(new CommandObjectFrameRecognizerClear(interpreter)));
    LoadSubCommand(
        "delete",
        CommandObjectSP(new CommandObjectFrameRecognizerDelete(interpreter)));
    LoadSubCommand("list", CommandObjectSP(new CommandObjectFrameRecognizerList(
                               interpreter)));
    LoadSubCommand("info", CommandObjectSP(new CommandObjectFrameRecognizerInfo(
                               interpreter)));
  }

  ~CommandObjectFrameRecognizer() override = default;
};

#pragma mark CommandObjectMultiwordFrame

// CommandObjectMultiwordFrame

CommandObjectMultiwordFrame::CommandObjectMultiwordFrame(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "frame",
                             "Commands for selecting and "
                             "examing the current "
                             "thread's stack frames.",
                             "frame <subcommand> [<subcommand-options>]") {
  LoadSubCommand("diagnose",
                 CommandObjectSP(new CommandObjectFrameDiagnose(interpreter)));
  LoadSubCommand("info",
                 CommandObjectSP(new CommandObjectFrameInfo(interpreter)));
  LoadSubCommand("select",
                 CommandObjectSP(new CommandObjectFrameSelect(interpreter)));
  LoadSubCommand("variable",
                 CommandObjectSP(new CommandObjectFrameVariable(interpreter)));
#if LLDB_ENABLE_PYTHON
  LoadSubCommand("recognizer", CommandObjectSP(new CommandObjectFrameRecognizer(
                                   interpreter)));
#endif
}

CommandObjectMultiwordFrame::~CommandObjectMultiwordFrame() = default;
