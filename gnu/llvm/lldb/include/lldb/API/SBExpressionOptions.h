//===-- SBExpressionOptions.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBEXPRESSIONOPTIONS_H
#define LLDB_API_SBEXPRESSIONOPTIONS_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBLanguages.h"

#include <vector>

namespace lldb {

class LLDB_API SBExpressionOptions {
public:
  SBExpressionOptions();

  SBExpressionOptions(const lldb::SBExpressionOptions &rhs);

  ~SBExpressionOptions();

  const SBExpressionOptions &operator=(const lldb::SBExpressionOptions &rhs);

  bool GetCoerceResultToId() const;

  void SetCoerceResultToId(bool coerce = true);

  bool GetUnwindOnError() const;

  void SetUnwindOnError(bool unwind = true);

  bool GetIgnoreBreakpoints() const;

  void SetIgnoreBreakpoints(bool ignore = true);

  lldb::DynamicValueType GetFetchDynamicValue() const;

  void SetFetchDynamicValue(
      lldb::DynamicValueType dynamic = lldb::eDynamicCanRunTarget);

  uint32_t GetTimeoutInMicroSeconds() const;

  // Set the timeout for the expression, 0 means wait forever.
  void SetTimeoutInMicroSeconds(uint32_t timeout = 0);

  uint32_t GetOneThreadTimeoutInMicroSeconds() const;

  // Set the timeout for running on one thread, 0 means use the default
  // behavior. If you set this higher than the overall timeout, you'll get an
  // error when you try to run the expression.
  void SetOneThreadTimeoutInMicroSeconds(uint32_t timeout = 0);

  bool GetTryAllThreads() const;

  void SetTryAllThreads(bool run_others = true);

  bool GetStopOthers() const;

  void SetStopOthers(bool stop_others = true);

  bool GetTrapExceptions() const;

  void SetTrapExceptions(bool trap_exceptions = true);

  void SetLanguage(lldb::LanguageType language);
  /// Set the language using a pair of language code and version as
  /// defined by the DWARF 6 specification.
  /// WARNING: These codes may change until DWARF 6 is finalized.
  void SetLanguage(lldb::SBSourceLanguageName name, uint32_t version);

#ifndef SWIG
  void SetCancelCallback(lldb::ExpressionCancelCallback callback, void *baton);
#endif

  bool GetGenerateDebugInfo();

  void SetGenerateDebugInfo(bool b = true);

  bool GetSuppressPersistentResult();

  void SetSuppressPersistentResult(bool b = false);

  const char *GetPrefix() const;

  void SetPrefix(const char *prefix);

  void SetAutoApplyFixIts(bool b = true);

  bool GetAutoApplyFixIts();

  void SetRetriesWithFixIts(uint64_t retries);

  uint64_t GetRetriesWithFixIts();

  bool GetTopLevel();

  void SetTopLevel(bool b = true);

  // Gets whether we will JIT an expression if it cannot be interpreted
  bool GetAllowJIT();

  // Sets whether we will JIT an expression if it cannot be interpreted
  void SetAllowJIT(bool allow);

protected:
  lldb_private::EvaluateExpressionOptions *get() const;

  lldb_private::EvaluateExpressionOptions &ref() const;

  friend class SBFrame;
  friend class SBValue;
  friend class SBTarget;

private:
  // This auto_pointer is made in the constructor and is always valid.
  mutable std::unique_ptr<lldb_private::EvaluateExpressionOptions> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBEXPRESSIONOPTIONS_H
