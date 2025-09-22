//===-- ClangExpressionParser.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONPARSER_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONPARSER_H

#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/ExpressionParser.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-public.h"

#include <string>
#include <vector>

namespace llvm {
class LLVMContext;
}

namespace clang {
class CodeGenerator;
class CodeCompleteConsumer;
class CompilerInstance;
} // namespace clang

namespace lldb_private {

class IRExecutionUnit;
class TypeSystemClang;

/// \class ClangExpressionParser ClangExpressionParser.h
/// "lldb/Expression/ClangExpressionParser.h" Encapsulates an instance of
/// Clang that can parse expressions.
///
/// ClangExpressionParser is responsible for preparing an instance of
/// ClangExpression for execution.  ClangExpressionParser uses ClangExpression
/// as a glorified parameter list, performing the required parsing and
/// conversion to formats (DWARF bytecode, or JIT compiled machine code) that
/// can be executed.
class ClangExpressionParser : public ExpressionParser {
public:
  /// Constructor
  ///
  /// Initializes class variables.
  ///
  /// \param[in] exe_scope
  ///     If non-NULL, an execution context scope that can help to
  ///     correctly create an expression with a valid process for
  ///     optional tuning Objective-C runtime support. Can be NULL.
  ///
  /// \param[in] expr
  ///     The expression to be parsed.
  ///
  /// @param[in] include_directories
  ///     List of include directories that should be used when parsing the
  ///     expression.
  ///
  /// @param[in] filename
  ///     Name of the source file that should be used when rendering
  ///     diagnostics (i.e. errors, warnings or notes from Clang).
  ClangExpressionParser(ExecutionContextScope *exe_scope, Expression &expr,
                        bool generate_debug_info,
                        std::vector<std::string> include_directories = {},
                        std::string filename = "<clang expression>");

  /// Destructor
  ~ClangExpressionParser() override;

  bool Complete(CompletionRequest &request, unsigned line, unsigned pos,
                unsigned typed_pos) override;

  /// Parse a single expression and convert it to IR using Clang.  Don't wrap
  /// the expression in anything at all.
  ///
  /// \param[in] diagnostic_manager
  ///     The diagnostic manager to report errors to.
  ///
  /// \return
  ///     The number of errors encountered during parsing.  0 means
  ///     success.
  unsigned Parse(DiagnosticManager &diagnostic_manager);

  bool RewriteExpression(DiagnosticManager &diagnostic_manager) override;

  /// Ready an already-parsed expression for execution, possibly evaluating it
  /// statically.
  ///
  /// \param[out] func_addr
  ///     The address to which the function has been written.
  ///
  /// \param[out] func_end
  ///     The end of the function's allocated memory region.  (func_addr
  ///     and func_end do not delimit an allocated region; the allocated
  ///     region may begin before func_addr.)
  ///
  /// \param[in] execution_unit_sp
  ///     After parsing, ownership of the execution unit for
  ///     for the expression is handed to this shared pointer.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to write the function into.
  ///
  /// \param[in] execution_policy
  ///     Determines whether the expression must be JIT-compiled, must be
  ///     evaluated statically, or whether this decision may be made
  ///     opportunistically.
  ///
  /// \return
  ///     An error code indicating the success or failure of the operation.
  ///     Test with Success().
  Status DoPrepareForExecution(
      lldb::addr_t &func_addr, lldb::addr_t &func_end,
      lldb::IRExecutionUnitSP &execution_unit_sp, ExecutionContext &exe_ctx,
      bool &can_interpret,
      lldb_private::ExecutionPolicy execution_policy) override;

  /// Returns a string representing current ABI.
  ///
  /// \param[in] target_arch
  ///     The target architecture.
  ///
  /// \return
  ///     A string representing target ABI for the current architecture.
  std::string GetClangTargetABI(const ArchSpec &target_arch);

private:
  /// Parses the expression.
  ///
  /// \param[in] diagnostic_manager
  ///     The diagnostic manager that should receive the diagnostics
  ///     from the parsing process.
  ///
  /// \param[in] completion
  ///     The completion consumer that should be used during parsing
  ///     (or a nullptr if no consumer should be attached).
  ///
  /// \param[in] completion_line
  ///     The line in which the completion marker should be placed.
  ///     The first line is represented by the value 0.
  ///
  /// \param[in] completion_column
  ///     The column in which the completion marker should be placed.
  ///     The first column is represented by the value 0.
  ///
  /// \return
  ///    The number of parsing errors.
  unsigned ParseInternal(DiagnosticManager &diagnostic_manager,
                         clang::CodeCompleteConsumer *completion = nullptr,
                         unsigned completion_line = 0,
                         unsigned completion_column = 0);

  std::unique_ptr<llvm::LLVMContext>
      m_llvm_context; ///< The LLVM context to generate IR into
  std::unique_ptr<clang::CompilerInstance>
      m_compiler; ///< The Clang compiler used to parse expressions into IR
  std::unique_ptr<clang::CodeGenerator>
      m_code_generator; ///< The Clang object that generates IR

  class LLDBPreprocessorCallbacks;
  LLDBPreprocessorCallbacks *m_pp_callbacks; ///< Called when the preprocessor
                                             ///encounters module imports
  std::shared_ptr<TypeSystemClang> m_ast_context;

  std::vector<std::string> m_include_directories;
  /// File name used for the user expression.
  std::string m_filename;
};
}

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONPARSER_H
