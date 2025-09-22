//===-- UserExpression.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_USEREXPRESSION_H
#define LLDB_EXPRESSION_USEREXPRESSION_H

#include <memory>
#include <string>
#include <vector>

#include "lldb/Core/Address.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Expression/Materializer.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// \class UserExpression UserExpression.h "lldb/Expression/UserExpression.h"
/// Encapsulates a one-time expression for use in lldb.
///
/// LLDB uses expressions for various purposes, notably to call functions
/// and as a backend for the expr command.  UserExpression is a virtual base
/// class that encapsulates the objects needed to parse and interpret or
/// JIT an expression.  The actual parsing part will be provided by the specific
/// implementations of UserExpression - which will be vended through the
/// appropriate TypeSystem.
class UserExpression : public Expression {
  /// LLVM RTTI support.
  static char ID;

public:
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const Expression *obj) { return obj->isA(&ID); }

  enum { kDefaultTimeout = 500000u };

  /// Constructor
  ///
  /// \param[in] expr
  ///     The expression to parse.
  ///
  /// \param[in] language
  ///     If not eLanguageTypeUnknown, a language to use when parsing
  ///     the expression.  Currently restricted to those languages
  ///     supported by Clang.
  ///
  /// \param[in] desired_type
  ///     If not eResultTypeAny, the type to use for the expression
  ///     result.
  UserExpression(ExecutionContextScope &exe_scope, llvm::StringRef expr,
                 llvm::StringRef prefix, SourceLanguage language,
                 ResultType desired_type,
                 const EvaluateExpressionOptions &options);

  /// Destructor
  ~UserExpression() override;

  /// Parse the expression
  ///
  /// \param[in] diagnostic_manager
  ///     A diagnostic manager to report parse errors and warnings to.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to use when looking up entities that
  ///     are needed for parsing (locations of functions, types of
  ///     variables, persistent variables, etc.)
  ///
  /// \param[in] execution_policy
  ///     Determines whether interpretation is possible or mandatory.
  ///
  /// \param[in] keep_result_in_memory
  ///     True if the resulting persistent variable should reside in
  ///     target memory, if applicable.
  ///
  /// \return
  ///     True on success (no errors); false otherwise.
  virtual bool Parse(DiagnosticManager &diagnostic_manager,
                     ExecutionContext &exe_ctx,
                     lldb_private::ExecutionPolicy execution_policy,
                     bool keep_result_in_memory, bool generate_debug_info) = 0;

  /// Attempts to find possible command line completions for the given
  /// (possible incomplete) user expression.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to use when looking up entities that
  ///     are needed for parsing and completing (locations of functions, types
  ///     of variables, persistent variables, etc.)
  ///
  /// \param[out] request
  ///     The completion request to fill out. The completion should be a string
  ///     that would complete the current token at the cursor position.
  ///     Note that the string in the list replaces the current token
  ///     in the command line.
  ///
  /// \param[in] complete_pos
  ///     The position of the cursor inside the user expression string.
  ///     The completion process starts on the token that the cursor is in.
  ///
  /// \return
  ///     True if we added any completion results to the output;
  ///     false otherwise.
  virtual bool Complete(ExecutionContext &exe_ctx, CompletionRequest &request,
                        unsigned complete_pos) {
    return false;
  }

  virtual bool CanInterpret() = 0;

  bool MatchesContext(ExecutionContext &exe_ctx);

  /// Execute the parsed expression by callinng the derived class's DoExecute
  /// method.
  ///
  /// \param[in] diagnostic_manager
  ///     A diagnostic manager to report errors to.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to use when looking up entities that
  ///     are needed for parsing (locations of variables, etc.)
  ///
  /// \param[in] options
  ///     Expression evaluation options.
  ///
  /// \param[in] shared_ptr_to_me
  ///     This is a shared pointer to this UserExpression.  This is
  ///     needed because Execute can push a thread plan that will hold onto
  ///     the UserExpression for an unbounded period of time.  So you
  ///     need to give the thread plan a reference to this object that can
  ///     keep it alive.
  ///
  /// \param[in] result
  ///     A pointer to direct at the persistent variable in which the
  ///     expression's result is stored.
  ///
  /// \return
  ///     A Process::Execution results value.
  lldb::ExpressionResults Execute(DiagnosticManager &diagnostic_manager,
                                  ExecutionContext &exe_ctx,
                                  const EvaluateExpressionOptions &options,
                                  lldb::UserExpressionSP &shared_ptr_to_me,
                                  lldb::ExpressionVariableSP &result);

  /// Apply the side effects of the function to program state.
  ///
  /// \param[in] diagnostic_manager
  ///     A diagnostic manager to report errors to.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to use when looking up entities that
  ///     are needed for parsing (locations of variables, etc.)
  ///
  /// \param[in] result
  ///     A pointer to direct at the persistent variable in which the
  ///     expression's result is stored.
  ///
  /// \param[in] function_stack_bottom
  ///     A pointer to the bottom of the function's stack frame.  This
  ///     is used to determine whether the expression result resides in
  ///     memory that will still be valid, or whether it needs to be
  ///     treated as homeless for the purpose of future expressions.
  ///
  /// \param[in] function_stack_top
  ///     A pointer to the top of the function's stack frame.  This
  ///     is used to determine whether the expression result resides in
  ///     memory that will still be valid, or whether it needs to be
  ///     treated as homeless for the purpose of future expressions.
  ///
  /// \return
  ///     A Process::Execution results value.
  virtual bool FinalizeJITExecution(
      DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
      lldb::ExpressionVariableSP &result,
      lldb::addr_t function_stack_bottom = LLDB_INVALID_ADDRESS,
      lldb::addr_t function_stack_top = LLDB_INVALID_ADDRESS) = 0;

  /// Return the string that the parser should parse.
  const char *Text() override { return m_expr_text.c_str(); }

  /// Return the string that the user typed.
  const char *GetUserText() { return m_expr_text.c_str(); }

  /// Return the function name that should be used for executing the
  /// expression.  Text() should contain the definition of this function.
  const char *FunctionName() override { return "$__lldb_expr"; }

  /// Returns whether the call to Parse on this user expression is cacheable.
  /// This function exists to provide an escape hatch for supporting languages
  /// where parsing an expression in the exact same context is unsafe. For
  /// example, languages where generic functions aren't monomorphized, but
  /// implement some other mechanism to represent generic values, may be unsafe
  /// to cache, as the concrete type substitution may be different in every
  /// expression evaluation.
  virtual bool IsParseCacheable() { return true; }
  /// Return the language that should be used when parsing.  To use the
  /// default, return eLanguageTypeUnknown.
  SourceLanguage Language() const override { return m_language; }

  /// Return the desired result type of the function, or eResultTypeAny if
  /// indifferent.
  ResultType DesiredResultType() override { return m_desired_type; }

  /// Return true if validation code should be inserted into the expression.
  bool NeedsValidation() override { return true; }

  /// Return true if external variables in the expression should be resolved.
  bool NeedsVariableResolution() override { return true; }

  EvaluateExpressionOptions *GetOptions() override { return &m_options; }

  virtual lldb::ExpressionVariableSP
  GetResultAfterDematerialization(ExecutionContextScope *exe_scope) {
    return lldb::ExpressionVariableSP();
  }

  /// Evaluate one expression in the scratch context of the target passed in
  /// the exe_ctx and return its result.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to use when evaluating the expression.
  ///
  /// \param[in] options
  ///     Expression evaluation options.  N.B. The language in the
  ///     evaluation options will be used to determine the language used for
  ///     expression evaluation.
  ///
  /// \param[in] expr_cstr
  ///     A C string containing the expression to be evaluated.
  ///
  /// \param[in] expr_prefix
  ///     If non-nullptr, a C string containing translation-unit level
  ///     definitions to be included when the expression is parsed.
  ///
  /// \param[in,out] result_valobj_sp
  ///      If execution is successful, the result valobj is placed here.
  ///
  /// \param[out] error
  ///     Filled in with an error in case the expression evaluation
  ///     fails to parse, run, or evaluated.
  ///
  /// \param[out] fixed_expression
  ///     If non-nullptr, the fixed expression is copied into the provided
  ///     string.
  ///
  /// \param[in] ctx_obj
  ///     If specified, then the expression will be evaluated in the context of
  ///     this object. It means that the context object's address will be
  ///     treated as `this` for the expression (the expression will be
  ///     evaluated as if it was inside of a method of the context object's
  ///     class, and its `this` parameter were pointing to the context object).
  ///     The parameter makes sense for class and union types only.
  ///     Currently there is a limitation: the context object must be located
  ///     in the debuggee process' memory (and have the load address).
  ///
  /// \result
  ///      A Process::ExpressionResults value.  eExpressionCompleted for
  ///      success.
  static lldb::ExpressionResults
  Evaluate(ExecutionContext &exe_ctx, const EvaluateExpressionOptions &options,
           llvm::StringRef expr_cstr, llvm::StringRef expr_prefix,
           lldb::ValueObjectSP &result_valobj_sp, Status &error,
           std::string *fixed_expression = nullptr,
           ValueObject *ctx_obj = nullptr);

  static const Status::ValueType kNoResult =
      0x1001; ///< ValueObject::GetError() returns this if there is no result
              /// from the expression.

  llvm::StringRef GetFixedText() {
    return m_fixed_text;
  }

protected:
  virtual lldb::ExpressionResults
  DoExecute(DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
            const EvaluateExpressionOptions &options,
            lldb::UserExpressionSP &shared_ptr_to_me,
            lldb::ExpressionVariableSP &result) = 0;

  static lldb::addr_t GetObjectPointer(lldb::StackFrameSP frame_sp,
                                       llvm::StringRef object_name,
                                       Status &err);

  /// Return ValueObject for a given variable name in the current stack frame
  ///
  /// \param[in] frame Current stack frame. When passed a 'nullptr', this
  ///                  function returns an empty ValueObjectSP.
  ///
  /// \param[in] object_name Name of the variable in the current stack frame
  ///                        for which we want the ValueObjectSP.
  ///
  /// \param[out] err Status object which will get set on error.
  ///
  /// \returns On success returns a ValueObjectSP corresponding to the variable
  ///          with 'object_name' in the current 'frame'. Otherwise, returns
  ///          'nullptr' (and sets the error status parameter 'err').
  static lldb::ValueObjectSP
  GetObjectPointerValueObject(lldb::StackFrameSP frame,
                              llvm::StringRef object_name, Status &err);

  /// Populate m_in_cplusplus_method and m_in_objectivec_method based on the
  /// environment.

  void InstallContext(ExecutionContext &exe_ctx);

  bool LockAndCheckContext(ExecutionContext &exe_ctx, lldb::TargetSP &target_sp,
                           lldb::ProcessSP &process_sp,
                           lldb::StackFrameSP &frame_sp);

  /// The address the process is stopped in.
  Address m_address;
  /// The text of the expression, as typed by the user.
  std::string m_expr_text;
  /// The text of the translation-level definitions, as provided by the user.
  std::string m_expr_prefix;
  /// The text of the expression with fix-its applied this won't be set if the
  /// fixed text doesn't parse.
  std::string m_fixed_text;
  /// The language to use when parsing (unknown means use defaults).
  SourceLanguage m_language;
  /// The type to coerce the expression's result to. If eResultTypeAny, inferred
  /// from the expression.
  ResultType m_desired_type;
  /// Additional options provided by the user.
  EvaluateExpressionOptions m_options;
};

} // namespace lldb_private

#endif // LLDB_EXPRESSION_USEREXPRESSION_H
