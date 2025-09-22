//===-- SBCommandInterpreterRunOptions.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBCOMMANDINTERPRETERRUNOPTIONS_H
#define LLDB_API_SBCOMMANDINTERPRETERRUNOPTIONS_H

#include <memory>

#include "lldb/API/SBDefines.h"

namespace lldb_private {
class CommandInterpreterRunOptions;
class CommandInterpreterRunResult;
} // namespace lldb_private

namespace lldb {

class LLDB_API SBCommandInterpreterRunOptions {
  friend class SBDebugger;
  friend class SBCommandInterpreter;

public:
  SBCommandInterpreterRunOptions();
  SBCommandInterpreterRunOptions(const SBCommandInterpreterRunOptions &rhs);
  ~SBCommandInterpreterRunOptions();

  SBCommandInterpreterRunOptions &
  operator=(const SBCommandInterpreterRunOptions &rhs);

  bool GetStopOnContinue() const;

  void SetStopOnContinue(bool);

  bool GetStopOnError() const;

  void SetStopOnError(bool);

  bool GetStopOnCrash() const;

  void SetStopOnCrash(bool);

  bool GetEchoCommands() const;

  void SetEchoCommands(bool);

  bool GetEchoCommentCommands() const;

  void SetEchoCommentCommands(bool echo);

  bool GetPrintResults() const;

  void SetPrintResults(bool);

  bool GetPrintErrors() const;

  void SetPrintErrors(bool);

  bool GetAddToHistory() const;

  void SetAddToHistory(bool);

  bool GetAutoHandleEvents() const;

  void SetAutoHandleEvents(bool);

  bool GetSpawnThread() const;

  void SetSpawnThread(bool);

  bool GetAllowRepeats() const;

  /// By default, RunCommandInterpreter will discard repeats if the
  /// IOHandler being used is not interactive.  Setting AllowRepeats to true
  /// will override this behavior and always process empty lines in the input
  /// as a repeat command.
  void SetAllowRepeats(bool);

private:
  lldb_private::CommandInterpreterRunOptions *get() const;

  lldb_private::CommandInterpreterRunOptions &ref() const;

  // This is set in the constructor and will always be valid.
  mutable std::unique_ptr<lldb_private::CommandInterpreterRunOptions>
      m_opaque_up;
};

#ifndef SWIG
class LLDB_API SBCommandInterpreterRunResult {
  friend class SBDebugger;
  friend class SBCommandInterpreter;

public:
  SBCommandInterpreterRunResult();
  SBCommandInterpreterRunResult(const SBCommandInterpreterRunResult &rhs);
  ~SBCommandInterpreterRunResult();

  SBCommandInterpreterRunResult &
  operator=(const SBCommandInterpreterRunResult &rhs);

  int GetNumberOfErrors() const;
  lldb::CommandInterpreterResult GetResult() const;

private:
  SBCommandInterpreterRunResult(
      const lldb_private::CommandInterpreterRunResult &rhs);

  // This is set in the constructor and will always be valid.
  std::unique_ptr<lldb_private::CommandInterpreterRunResult> m_opaque_up;
};
#endif

} // namespace lldb

#endif // LLDB_API_SBCOMMANDINTERPRETERRUNOPTIONS_H
