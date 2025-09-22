//===-- FunctionCaller.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_FUNCTIONCALLER_H
#define LLDB_EXPRESSION_FUNCTIONCALLER_H

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "lldb/Core/Address.h"
#include "lldb/Core/Value.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Expression/ExpressionParser.h"
#include "lldb/Symbol/CompilerType.h"

namespace lldb_private {

/// \class FunctionCaller FunctionCaller.h "lldb/Expression/FunctionCaller.h"
/// Encapsulates a function that can be called.
///
/// A given FunctionCaller object can handle a single function signature.
/// Once constructed, it can set up any number of concurrent calls to
/// functions with that signature.
///
/// It performs the call by synthesizing a structure that contains the pointer
/// to the function and the arguments that should be passed to that function,
/// and producing a special-purpose JIT-compiled function that accepts a void*
/// pointing to this struct as its only argument and calls the function in the
/// struct with the written arguments.  This method lets Clang handle the
/// vagaries of function calling conventions.
///
/// The simplest use of the FunctionCaller is to construct it with a function
/// representative of the signature you want to use, then call
/// ExecuteFunction(ExecutionContext &, Stream &, Value &).
///
/// If you need to reuse the arguments for several calls, you can call
/// InsertFunction() followed by WriteFunctionArguments(), which will return
/// the location of the args struct for the wrapper function in args_addr_ref.
///
/// If you need to call the function on the thread plan stack, you can also
/// call InsertFunction() followed by GetThreadPlanToCallFunction().
///
/// Any of the methods that take arg_addr_ptr or arg_addr_ref can be passed a
/// pointer set to LLDB_INVALID_ADDRESS and new structure will be allocated
/// and its address returned in that variable.
///
/// Any of the methods that take arg_addr_ptr can be passed nullptr, and the
/// argument space will be managed for you.
class FunctionCaller : public Expression {
  // LLVM RTTI support
  static char ID;

public:
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const Expression *obj) { return obj->isA(&ID); }

  /// Constructor
  ///
  /// \param[in] exe_scope
  ///     An execution context scope that gets us at least a target and
  ///     process.
  ///
  /// \param[in] return_type
  ///     An opaque Clang QualType for the function result.  Should be
  ///     defined in ast_context.
  ///
  /// \param[in] function_address
  ///     The address of the function to call.
  ///
  /// \param[in] arg_value_list
  ///     The default values to use when calling this function.  Can
  ///     be overridden using WriteFunctionArguments().
  FunctionCaller(ExecutionContextScope &exe_scope,
                 const CompilerType &return_type,
                 const Address &function_address,
                 const ValueList &arg_value_list, const char *name);

  /// Destructor
  ~FunctionCaller() override;

  /// Compile the wrapper function
  ///
  /// \param[in] thread_to_use_sp
  ///     Compilation might end up calling functions.  Pass in the thread you
  ///     want the compilation to use.  If you pass in an empty ThreadSP it will
  ///     use the currently selected thread.
  ///
  /// \param[in] diagnostic_manager
  ///     The diagnostic manager to report parser errors to.
  ///
  /// \return
  ///     The number of errors.
  virtual unsigned CompileFunction(lldb::ThreadSP thread_to_use_sp,
                                   DiagnosticManager &diagnostic_manager) = 0;

  /// Insert the default function wrapper and its default argument struct
  ///
  /// \param[in] exe_ctx
  ///     The execution context to insert the function and its arguments
  ///     into.
  ///
  /// \param[in,out] args_addr_ref
  ///     The address of the structure to write the arguments into.  May
  ///     be LLDB_INVALID_ADDRESS; if it is, a new structure is allocated
  ///     and args_addr_ref is pointed to it.
  ///
  /// \param[in] diagnostic_manager
  ///     The diagnostic manager to report errors to.
  ///
  /// \return
  ///     True on success; false otherwise.
  bool InsertFunction(ExecutionContext &exe_ctx, lldb::addr_t &args_addr_ref,
                      DiagnosticManager &diagnostic_manager);

  /// Insert the default function wrapper (using the JIT)
  ///
  /// \param[in] exe_ctx
  ///     The execution context to insert the function and its arguments
  ///     into.
  ///
  /// \param[in] diagnostic_manager
  ///     The diagnostic manager to report errors to.
  ///
  /// \return
  ///     True on success; false otherwise.
  bool WriteFunctionWrapper(ExecutionContext &exe_ctx,
                            DiagnosticManager &diagnostic_manager);

  /// Insert the default function argument struct
  ///
  /// \param[in] exe_ctx
  ///     The execution context to insert the function and its arguments
  ///     into.
  ///
  /// \param[in,out] args_addr_ref
  ///     The address of the structure to write the arguments into.  May
  ///     be LLDB_INVALID_ADDRESS; if it is, a new structure is allocated
  ///     and args_addr_ref is pointed to it.
  ///
  /// \param[in] diagnostic_manager
  ///     The diagnostic manager to report errors to.
  ///
  /// \return
  ///     True on success; false otherwise.
  bool WriteFunctionArguments(ExecutionContext &exe_ctx,
                              lldb::addr_t &args_addr_ref,
                              DiagnosticManager &diagnostic_manager);

  /// Insert an argument struct with a non-default function address and non-
  /// default argument values
  ///
  /// \param[in] exe_ctx
  ///     The execution context to insert the function and its arguments
  ///     into.
  ///
  /// \param[in,out] args_addr_ref
  ///     The address of the structure to write the arguments into.  May
  ///     be LLDB_INVALID_ADDRESS; if it is, a new structure is allocated
  ///     and args_addr_ref is pointed at it.
  ///
  /// \param[in] arg_values
  ///     The values of the function's arguments.
  ///
  /// \param[in] diagnostic_manager
  ///     The diagnostic manager to report errors to.
  ///
  /// \return
  ///     True on success; false otherwise.
  bool WriteFunctionArguments(ExecutionContext &exe_ctx,
                              lldb::addr_t &args_addr_ref,
                              ValueList &arg_values,
                              DiagnosticManager &diagnostic_manager);

  /// Run the function this FunctionCaller was created with.
  ///
  /// This is the full version.
  ///
  /// \param[in] exe_ctx
  ///     The thread & process in which this function will run.
  ///
  /// \param[in] args_addr_ptr
  ///     If nullptr, the function will take care of allocating & deallocating
  ///     the wrapper
  ///     args structure.  Otherwise, if set to LLDB_INVALID_ADDRESS, a new
  ///     structure
  ///     will be allocated, filled and the address returned to you.  You are
  ///     responsible
  ///     for deallocating it.  And if passed in with a value other than
  ///     LLDB_INVALID_ADDRESS,
  ///     this should point to an already allocated structure with the values
  ///     already written.
  ///
  /// \param[in] diagnostic_manager
  ///     The diagnostic manager to report errors to.
  ///
  /// \param[in] options
  ///     The options for this expression execution.
  ///
  /// \param[out] results
  ///     The result value will be put here after running the function.
  ///
  /// \return
  ///     Returns one of the ExpressionResults enum indicating function call
  ///     status.
  lldb::ExpressionResults
  ExecuteFunction(ExecutionContext &exe_ctx, lldb::addr_t *args_addr_ptr,
                  const EvaluateExpressionOptions &options,
                  DiagnosticManager &diagnostic_manager, Value &results);

  /// Get a thread plan to run the function this FunctionCaller was created
  /// with.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to insert the function and its arguments
  ///     into.
  ///
  /// \param[in] args_addr
  ///     The address of the argument struct.
  ///
  /// \param[in] diagnostic_manager
  ///     The diagnostic manager to report errors to.
  ///
  /// \return
  ///     A ThreadPlan shared pointer for executing the function.
  lldb::ThreadPlanSP
  GetThreadPlanToCallFunction(ExecutionContext &exe_ctx, lldb::addr_t args_addr,
                              const EvaluateExpressionOptions &options,
                              DiagnosticManager &diagnostic_manager);

  /// Get the result of the function from its struct
  ///
  /// \param[in] exe_ctx
  ///     The execution context to retrieve the result from.
  ///
  /// \param[in] args_addr
  ///     The address of the argument struct.
  ///
  /// \param[out] ret_value
  ///     The value returned by the function.
  ///
  /// \return
  ///     True on success; false otherwise.
  bool FetchFunctionResults(ExecutionContext &exe_ctx, lldb::addr_t args_addr,
                            Value &ret_value);

  /// Deallocate the arguments structure
  ///
  /// \param[in] exe_ctx
  ///     The execution context to insert the function and its arguments
  ///     into.
  ///
  /// \param[in] args_addr
  ///     The address of the argument struct.
  void DeallocateFunctionResults(ExecutionContext &exe_ctx,
                                 lldb::addr_t args_addr);

  /// Interface for ClangExpression

  /// Return the string that the parser should parse.  Must be a full
  /// translation unit.
  const char *Text() override { return m_wrapper_function_text.c_str(); }

  /// Return the function name that should be used for executing the
  /// expression.  Text() should contain the definition of this function.
  const char *FunctionName() override {
    return m_wrapper_function_name.c_str();
  }

  /// Return the object that the parser should use when registering local
  /// variables. May be nullptr if the Expression doesn't care.
  ExpressionVariableList *LocalVariables() { return nullptr; }

  /// Return true if validation code should be inserted into the expression.
  bool NeedsValidation() override { return false; }

  /// Return true if external variables in the expression should be resolved.
  bool NeedsVariableResolution() override { return false; }

  ValueList GetArgumentValues() const { return m_arg_values; }

protected:
  // Note: the parser needs to be destructed before the execution unit, so
  // declare the execution unit first.
  std::shared_ptr<IRExecutionUnit> m_execution_unit_sp;
  std::unique_ptr<ExpressionParser>
      m_parser; ///< The parser responsible for compiling the function.
                ///< This will get made in CompileFunction, so it is
                ///< safe to access it after that.

  lldb::ModuleWP m_jit_module_wp;
  std::string
      m_name; ///< The name of this clang function - for debugging purposes.

  Function *m_function_ptr; ///< The function we're going to call. May be
                            ///nullptr if we don't have debug info for the
                            ///function.
  Address m_function_addr;  ///< If we don't have the FunctionSP, we at least
                            ///need the address & return type.
  CompilerType m_function_return_type; ///< The opaque clang qual type for the
                                       ///function return type.

  std::string m_wrapper_function_name; ///< The name of the wrapper function.
  std::string
      m_wrapper_function_text;       ///< The contents of the wrapper function.
  std::string m_wrapper_struct_name; ///< The name of the struct that contains
                                     ///the target function address, arguments,
                                     ///and result.
  std::list<lldb::addr_t> m_wrapper_args_addrs; ///< The addresses of the
                                                ///arguments to the wrapper
                                                ///function.

  bool m_struct_valid; ///< True if the ASTStructExtractor has populated the
                       ///variables below.

  /// These values are populated by the ASTStructExtractor
  size_t m_struct_size; ///< The size of the argument struct, in bytes.
  std::vector<uint64_t>
      m_member_offsets; ///< The offset of each member in the struct, in bytes.
  uint64_t m_return_size;   ///< The size of the result variable, in bytes.
  uint64_t m_return_offset; ///< The offset of the result variable in the
                            ///struct, in bytes.

  ValueList m_arg_values; ///< The default values of the arguments.

  bool m_compiled; ///< True if the wrapper function has already been parsed.
  bool
      m_JITted; ///< True if the wrapper function has already been JIT-compiled.
};

} // namespace lldb_private

#endif // LLDB_EXPRESSION_FUNCTIONCALLER_H
