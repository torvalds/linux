//===-- UtilityFunction.h ----------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_UTILITYFUNCTION_H
#define LLDB_EXPRESSION_UTILITYFUNCTION_H

#include <memory>
#include <string>

#include "lldb/Expression/Expression.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// \class UtilityFunction UtilityFunction.h
/// "lldb/Expression/UtilityFunction.h" Encapsulates a bit of source code that
/// provides a function that is callable
///
/// LLDB uses expressions for various purposes, notably to call functions
/// and as a backend for the expr command.  UtilityFunction encapsulates a
/// self-contained function meant to be used from other code.  Utility
/// functions can perform error-checking for ClangUserExpressions,
class UtilityFunction : public Expression {
  // LLVM RTTI support
  static char ID;

public:
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const Expression *obj) { return obj->isA(&ID); }

  /// Constructor
  ///
  /// \param[in] text
  ///     The text of the function.  Must be a full translation unit.
  ///
  /// \param[in] name
  ///     The name of the function, as used in the text.
  ///
  /// \param[in] enable_debugging
  ///     Enable debugging of this function.
  UtilityFunction(ExecutionContextScope &exe_scope, std::string text,
                  std::string name, bool enable_debugging);

  ~UtilityFunction() override;

  /// Install the utility function into a process
  ///
  /// \param[in] diagnostic_manager
  ///     A diagnostic manager to print parse errors and warnings to.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to install the utility function to.
  ///
  /// \return
  ///     True on success (no errors); false otherwise.
  virtual bool Install(DiagnosticManager &diagnostic_manager,
                       ExecutionContext &exe_ctx) = 0;

  /// Check whether the given address is inside the function
  ///
  /// Especially useful if the function dereferences nullptr to indicate a
  /// failed assert.
  ///
  /// \param[in] address
  ///     The address to check.
  ///
  /// \return
  ///     True if the address falls within the function's bounds;
  ///     false if not (or the function is not JIT compiled)
  bool ContainsAddress(lldb::addr_t address) {
    // nothing is both >= LLDB_INVALID_ADDRESS and < LLDB_INVALID_ADDRESS, so
    // this always returns false if the function is not JIT compiled yet
    return (address >= m_jit_start_addr && address < m_jit_end_addr);
  }

  /// Return the string that the parser should parse.  Must be a full
  /// translation unit.
  const char *Text() override { return m_function_text.c_str(); }

  /// Return the function name that should be used for executing the
  /// expression.  Text() should contain the definition of this function.
  const char *FunctionName() override { return m_function_name.c_str(); }

  /// Return the object that the parser should use when registering local
  /// variables. May be nullptr if the Expression doesn't care.
  ExpressionVariableList *LocalVariables() { return nullptr; }

  /// Return true if validation code should be inserted into the expression.
  bool NeedsValidation() override { return false; }

  /// Return true if external variables in the expression should be resolved.
  bool NeedsVariableResolution() override { return false; }

  // This makes the function caller function. Pass in the ThreadSP if you have
  // one available, compilation can end up calling code (e.g. to look up
  // indirect functions) and we don't want this to wander onto another thread.
  FunctionCaller *MakeFunctionCaller(const CompilerType &return_type,
                                     const ValueList &arg_value_list,
                                     lldb::ThreadSP compilation_thread,
                                     Status &error);

  // This one retrieves the function caller that is already made.  If you
  // haven't made it yet, this returns nullptr
  FunctionCaller *GetFunctionCaller() { return m_caller_up.get(); }

protected:
  std::shared_ptr<IRExecutionUnit> m_execution_unit_sp;
  lldb::ModuleWP m_jit_module_wp;
  /// The text of the function.  Must be a well-formed translation unit.
  std::string m_function_text;
  /// The name of the function.
  std::string m_function_name;
  std::unique_ptr<FunctionCaller> m_caller_up;
};

} // namespace lldb_private

#endif // LLDB_EXPRESSION_UTILITYFUNCTION_H
