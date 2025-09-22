//===-- ClangUserExpression.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGUSEREXPRESSION_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGUSEREXPRESSION_H

#include <optional>
#include <vector>

#include "ASTResultSynthesizer.h"
#include "ASTStructExtractor.h"
#include "ClangExpressionDeclMap.h"
#include "ClangExpressionHelper.h"
#include "ClangExpressionSourceCode.h"
#include "ClangExpressionVariable.h"
#include "IRForTarget.h"

#include "lldb/Core/Address.h"
#include "lldb/Expression/LLVMUserExpression.h"
#include "lldb/Expression/Materializer.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class ClangExpressionParser;

/// \class ClangUserExpression ClangUserExpression.h
/// "lldb/Expression/ClangUserExpression.h" Encapsulates a single expression
/// for use with Clang
///
/// LLDB uses expressions for various purposes, notably to call functions
/// and as a backend for the expr command.  ClangUserExpression encapsulates
/// the objects needed to parse and interpret or JIT an expression.  It uses
/// the Clang parser to produce LLVM IR from the expression.
class ClangUserExpression : public LLVMUserExpression {
  // LLVM RTTI support
  static char ID;

public:
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || LLVMUserExpression::isA(ClassID);
  }
  static bool classof(const Expression *obj) { return obj->isA(&ID); }

  enum { kDefaultTimeout = 500000u };

  class ClangUserExpressionHelper
      : public llvm::RTTIExtends<ClangUserExpressionHelper,
                                 ClangExpressionHelper> {
  public:
    // LLVM RTTI support
    static char ID;

    ClangUserExpressionHelper(Target &target, bool top_level)
        : m_target(target), m_top_level(top_level) {}

    /// Return the object that the parser should use when resolving external
    /// values.  May be NULL if everything should be self-contained.
    ClangExpressionDeclMap *DeclMap() override {
      return m_expr_decl_map_up.get();
    }

    void ResetDeclMap() { m_expr_decl_map_up.reset(); }

    void ResetDeclMap(ExecutionContext &exe_ctx,
                      Materializer::PersistentVariableDelegate &result_delegate,
                      bool keep_result_in_memory,
                      ValueObject *ctx_obj);

    /// Return the object that the parser should allow to access ASTs. May be
    /// NULL if the ASTs do not need to be transformed.
    ///
    /// \param[in] passthrough
    ///     The ASTConsumer that the returned transformer should send
    ///     the ASTs to after transformation.
    clang::ASTConsumer *
    ASTTransformer(clang::ASTConsumer *passthrough) override;

    void CommitPersistentDecls() override;

  private:
    Target &m_target;
    std::unique_ptr<ClangExpressionDeclMap> m_expr_decl_map_up;
    std::unique_ptr<ASTStructExtractor> m_struct_extractor_up; ///< The class
                                                               ///that generates
                                                               ///the argument
                                                               ///struct layout.
    std::unique_ptr<ASTResultSynthesizer> m_result_synthesizer_up;
    bool m_top_level;
  };

  /// Constructor
  ///
  /// \param[in] expr
  ///     The expression to parse.
  ///
  /// \param[in] prefix
  ///     If non-NULL, a C string containing translation-unit level
  ///     definitions to be included when the expression is parsed.
  ///
  /// \param[in] language
  ///     If not unknown, a language to use when parsing the
  ///     expression.  Currently restricted to those languages
  ///     supported by Clang.
  ///
  /// \param[in] desired_type
  ///     If not eResultTypeAny, the type to use for the expression
  ///     result.
  ///
  /// \param[in] options
  ///     Additional options for the expression.
  ///
  /// \param[in] ctx_obj
  ///     The object (if any) in which context the expression
  ///     must be evaluated. For details see the comment to
  ///     `UserExpression::Evaluate`.
  ClangUserExpression(ExecutionContextScope &exe_scope, llvm::StringRef expr,
                      llvm::StringRef prefix, SourceLanguage language,
                      ResultType desired_type,
                      const EvaluateExpressionOptions &options,
                      ValueObject *ctx_obj);

  ~ClangUserExpression() override;

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
  bool Parse(DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
             lldb_private::ExecutionPolicy execution_policy,
             bool keep_result_in_memory, bool generate_debug_info) override;

  bool Complete(ExecutionContext &exe_ctx, CompletionRequest &request,
                unsigned complete_pos) override;

  ExpressionTypeSystemHelper *GetTypeSystemHelper() override {
    return &m_type_system_helper;
  }

  ClangExpressionDeclMap *DeclMap() { return m_type_system_helper.DeclMap(); }

  void ResetDeclMap() { m_type_system_helper.ResetDeclMap(); }

  void ResetDeclMap(ExecutionContext &exe_ctx,
                    Materializer::PersistentVariableDelegate &result_delegate,
                    bool keep_result_in_memory) {
    m_type_system_helper.ResetDeclMap(exe_ctx, result_delegate,
                                      keep_result_in_memory,
                                      m_ctx_obj);
  }

  lldb::ExpressionVariableSP
  GetResultAfterDematerialization(ExecutionContextScope *exe_scope) override;

  /// Returns true iff this expression is using any imported C++ modules.
  bool DidImportCxxModules() const { return !m_imported_cpp_modules.empty(); }

private:
  /// Populate m_in_cplusplus_method and m_in_objectivec_method based on the
  /// environment.

  /// Contains the actual parsing implementation.
  /// The parameter have the same meaning as in ClangUserExpression::Parse.
  /// \see ClangUserExpression::Parse
  bool TryParse(DiagnosticManager &diagnostic_manager,
                ExecutionContext &exe_ctx,
                lldb_private::ExecutionPolicy execution_policy,
                bool keep_result_in_memory, bool generate_debug_info);

  void SetupCppModuleImports(ExecutionContext &exe_ctx);

  void ScanContext(ExecutionContext &exe_ctx,
                   lldb_private::Status &err) override;

  bool AddArguments(ExecutionContext &exe_ctx, std::vector<lldb::addr_t> &args,
                    lldb::addr_t struct_address,
                    DiagnosticManager &diagnostic_manager) override;

  void CreateSourceCode(DiagnosticManager &diagnostic_manager,
                        ExecutionContext &exe_ctx,
                        std::vector<std::string> modules_to_import,
                        bool for_completion);

  lldb::addr_t GetCppObjectPointer(lldb::StackFrameSP frame,
                                   llvm::StringRef object_name, Status &err);

  /// Defines how the current expression should be wrapped.
  ClangExpressionSourceCode::WrapKind GetWrapKind() const;
  bool SetupPersistentState(DiagnosticManager &diagnostic_manager,
                                   ExecutionContext &exe_ctx);
  bool PrepareForParsing(DiagnosticManager &diagnostic_manager,
                         ExecutionContext &exe_ctx, bool for_completion);

  ClangUserExpressionHelper m_type_system_helper;

  class ResultDelegate : public Materializer::PersistentVariableDelegate {
  public:
    ResultDelegate(lldb::TargetSP target) : m_target_sp(target) {}
    ConstString GetName() override;
    void DidDematerialize(lldb::ExpressionVariableSP &variable) override;

    void RegisterPersistentState(PersistentExpressionState *persistent_state);
    lldb::ExpressionVariableSP &GetVariable();

  private:
    PersistentExpressionState *m_persistent_state;
    lldb::ExpressionVariableSP m_variable;
    lldb::TargetSP m_target_sp;
  };

  /// The include directories that should be used when parsing the expression.
  std::vector<std::string> m_include_directories;

  /// The absolute character position in the transformed source code where the
  /// user code (as typed by the user) starts. If the variable is empty, then we
  /// were not able to calculate this position.
  std::optional<size_t> m_user_expression_start_pos;
  ResultDelegate m_result_delegate;
  ClangPersistentVariables *m_clang_state;
  std::unique_ptr<ClangExpressionSourceCode> m_source_code;
  /// The parser instance we used to parse the expression.
  std::unique_ptr<ClangExpressionParser> m_parser;
  /// File name used for the expression.
  std::string m_filename;

  /// The object (if any) in which context the expression is evaluated.
  /// See the comment to `UserExpression::Evaluate` for details.
  ValueObject *m_ctx_obj;

  /// A list of module names that should be imported when parsing.
  /// \see CppModuleConfiguration::GetImportedModules
  std::vector<std::string> m_imported_cpp_modules;

  /// True if the expression parser should enforce the presence of a valid class
  /// pointer in order to generate the expression as a method.
  bool m_enforce_valid_object = true;
  /// True if the expression is compiled as a C++ member function (true if it
  /// was parsed when exe_ctx was in a C++ method).
  bool m_in_cplusplus_method = false;
  /// True if the expression is compiled as an Objective-C method (true if it
  /// was parsed when exe_ctx was in an Objective-C method).
  bool m_in_objectivec_method = false;
  /// True if the expression is compiled as a static (or class) method
  /// (currently true if it was parsed when exe_ctx was in an Objective-C class
  /// method).
  bool m_in_static_method = false;
  /// True if "this" or "self" must be looked up and passed in.  False if the
  /// expression doesn't really use them and they can be NULL.
  bool m_needs_object_ptr = false;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGUSEREXPRESSION_H
