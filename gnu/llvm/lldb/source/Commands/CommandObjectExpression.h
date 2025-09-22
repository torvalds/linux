//===-- CommandObjectExpression.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTEXPRESSION_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTEXPRESSION_H

#include "lldb/Core/IOHandler.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/OptionGroupBoolean.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"
#include "lldb/Target/Target.h"
#include "lldb/lldb-private-enumerations.h"

namespace lldb_private {

class CommandObjectExpression : public CommandObjectRaw,
                                public IOHandlerDelegate {
public:
  class CommandOptions : public OptionGroup {
  public:
    CommandOptions();

    ~CommandOptions() override;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override;

    void OptionParsingStarting(ExecutionContext *execution_context) override;

    /// Return the appropriate expression options used for evaluating the
    /// expression in the given target.
    EvaluateExpressionOptions GetEvaluateExpressionOptions(
        const Target &target,
        const OptionGroupValueObjectDisplay &display_opts);

    bool ShouldSuppressResult(
        const OptionGroupValueObjectDisplay &display_opts) const;

    bool top_level;
    bool unwind_on_error;
    bool ignore_breakpoints;
    bool allow_jit;
    bool show_types;
    bool show_summary;
    bool debug;
    uint32_t timeout;
    bool try_all_threads;
    lldb::LanguageType language;
    LanguageRuntimeDescriptionDisplayVerbosity m_verbosity;
    LazyBool auto_apply_fixits;
    LazyBool suppress_persistent_result;
  };

  CommandObjectExpression(CommandInterpreter &interpreter);

  ~CommandObjectExpression() override;

  Options *GetOptions() override;

  void HandleCompletion(CompletionRequest &request) override;

protected:
  // IOHandler::Delegate functions
  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &line) override;

  bool IOHandlerIsInputComplete(IOHandler &io_handler,
                                StringList &lines) override;

  void DoExecute(llvm::StringRef command, CommandReturnObject &result) override;

  /// Evaluates the given expression.
  /// \param output_stream The stream to which the evaluation result will be
  ///                      printed.
  /// \param error_stream Contains error messages that should be displayed to
  ///                     the user in case the evaluation fails.
  /// \param result A CommandReturnObject which status will be set to the
  ///               appropriate value depending on evaluation success and
  ///               whether the expression produced any result.
  /// \return Returns true iff the expression was successfully evaluated,
  ///         executed and the result could be printed to the output stream.
  bool EvaluateExpression(llvm::StringRef expr, Stream &output_stream,
                          Stream &error_stream, CommandReturnObject &result);

  void GetMultilineExpression();

  OptionGroupOptions m_option_group;
  OptionGroupFormat m_format_options;
  OptionGroupValueObjectDisplay m_varobj_options;
  OptionGroupBoolean m_repl_option;
  CommandOptions m_command_options;
  uint32_t m_expr_line_count;
  std::string m_expr_lines;       // Multi-line expression support
  std::string m_fixed_expression; // Holds the current expression's fixed text.
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTEXPRESSION_H
