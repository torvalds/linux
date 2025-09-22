//===-- LLVMUserExpression.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_LLVMUSEREXPRESSION_H
#define LLDB_EXPRESSION_LLVMUSEREXPRESSION_H

#include <map>
#include <string>
#include <vector>

#include "llvm/IR/LegacyPassManager.h"

#include "lldb/Expression/UserExpression.h"

namespace lldb_private {

/// \class LLVMUserExpression LLVMUserExpression.h
/// "lldb/Expression/LLVMUserExpression.h" Encapsulates a one-time expression
/// for use in lldb.
///
/// LLDB uses expressions for various purposes, notably to call functions
/// and as a backend for the expr command.  LLVMUserExpression is a virtual
/// base class that encapsulates the objects needed to parse and JIT an
/// expression. The actual parsing part will be provided by the specific
/// implementations of LLVMUserExpression - which will be vended through the
/// appropriate TypeSystem.
class LLVMUserExpression : public UserExpression {
  // LLVM RTTI support
  static char ID;

public:
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || UserExpression::isA(ClassID);
  }
  static bool classof(const Expression *obj) { return obj->isA(&ID); }

  // The IRPasses struct is filled in by a runtime after an expression is
  // compiled and can be used to run fixups/analysis passes as required.
  // EarlyPasses are run on the generated module before lldb runs its own IR
  // fixups and inserts instrumentation code/pointer checks. LatePasses are run
  // after the module has been processed by llvm, before the module is
  // assembled and run in the ThreadPlan.
  struct IRPasses {
    IRPasses() : EarlyPasses(nullptr), LatePasses(nullptr){};
    std::shared_ptr<llvm::legacy::PassManager> EarlyPasses;
    std::shared_ptr<llvm::legacy::PassManager> LatePasses;
  };

  LLVMUserExpression(ExecutionContextScope &exe_scope, llvm::StringRef expr,
                     llvm::StringRef prefix, SourceLanguage language,
                     ResultType desired_type,
                     const EvaluateExpressionOptions &options);
  ~LLVMUserExpression() override;

  bool FinalizeJITExecution(
      DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
      lldb::ExpressionVariableSP &result,
      lldb::addr_t function_stack_bottom = LLDB_INVALID_ADDRESS,
      lldb::addr_t function_stack_top = LLDB_INVALID_ADDRESS) override;

  bool CanInterpret() override { return m_can_interpret; }

  Materializer *GetMaterializer() override { return m_materializer_up.get(); }

  /// Return the string that the parser should parse.  Must be a full
  /// translation unit.
  const char *Text() override { return m_transformed_text.c_str(); }

protected:
  lldb::ExpressionResults
  DoExecute(DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
            const EvaluateExpressionOptions &options,
            lldb::UserExpressionSP &shared_ptr_to_me,
            lldb::ExpressionVariableSP &result) override;

  virtual void ScanContext(ExecutionContext &exe_ctx,
                           lldb_private::Status &err) = 0;

  bool PrepareToExecuteJITExpression(DiagnosticManager &diagnostic_manager,
                                     ExecutionContext &exe_ctx,
                                     lldb::addr_t &struct_address);

  virtual bool AddArguments(ExecutionContext &exe_ctx,
                            std::vector<lldb::addr_t> &args,
                            lldb::addr_t struct_address,
                            DiagnosticManager &diagnostic_manager) = 0;

  lldb::addr_t
      m_stack_frame_bottom;       ///< The bottom of the allocated stack frame.
  lldb::addr_t m_stack_frame_top; ///< The top of the allocated stack frame.

  bool m_allow_cxx;  ///< True if the language allows C++.
  bool m_allow_objc; ///< True if the language allows Objective-C.
  std::string
      m_transformed_text; ///< The text of the expression, as send to the parser

  std::shared_ptr<IRExecutionUnit>
      m_execution_unit_sp; ///< The execution unit the expression is stored in.
  std::unique_ptr<Materializer> m_materializer_up; ///< The materializer to use
                                                   /// when running the
                                                   /// expression.
  lldb::ModuleWP m_jit_module_wp;
  Target *m_target; ///< The target for storing persistent data like types and
                    ///variables.

  bool m_can_interpret; ///< True if the expression could be evaluated
                        ///statically; false otherwise.
  lldb::addr_t m_materialized_address; ///< The address at which the arguments
                                       ///to the expression have been
                                       ///materialized.
  Materializer::DematerializerSP m_dematerializer_sp; ///< The dematerializer.
};

} // namespace lldb_private
#endif
