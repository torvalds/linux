//===-- DynamicCheckerFunctions.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_DYNAMICCHECKERFUNCTIONS_H
#define LLDB_EXPRESSION_DYNAMICCHECKERFUNCTIONS_H

#include "lldb/lldb-types.h"

#include "llvm/Support/Error.h"

namespace lldb_private {

class DiagnosticManager;
class ExecutionContext;

/// Encapsulates dynamic check functions used by expressions.
///
/// Each of the utility functions encapsulated in this class is responsible
/// for validating some data that an expression is about to use.  Examples
/// are:
///
/// a = *b;     // check that b is a valid pointer
/// [b init];   // check that b is a valid object to send "init" to
///
/// The class installs each checker function into the target process and makes
/// it available to IRDynamicChecks to use.
class DynamicCheckerFunctions {
public:
  enum DynamicCheckerFunctionsKind {
    DCF_Clang,
  };

  DynamicCheckerFunctions(DynamicCheckerFunctionsKind kind) : m_kind(kind) {}
  virtual ~DynamicCheckerFunctions() = default;

  /// Install the utility functions into a process.  This binds the instance
  /// of DynamicCheckerFunctions to that process.
  ///
  /// \param[in] diagnostic_manager
  ///     A diagnostic manager to report errors to.
  ///
  /// \param[in] exe_ctx
  ///     The execution context to install the functions into.
  ///
  /// \return
  ///     Either llvm::ErrorSuccess or Error with llvm::ErrorInfo
  ///
  virtual llvm::Error Install(DiagnosticManager &diagnostic_manager,
                              ExecutionContext &exe_ctx) = 0;
  virtual bool DoCheckersExplainStop(lldb::addr_t addr, Stream &message) = 0;

  DynamicCheckerFunctionsKind GetKind() const { return m_kind; }

private:
  const DynamicCheckerFunctionsKind m_kind;
};
} // namespace lldb_private

#endif // LLDB_EXPRESSION_DYNAMICCHECKERFUNCTIONS_H
