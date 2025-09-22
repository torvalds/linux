//===-- RunInTerminal.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_RUNINTERMINAL_H
#define LLDB_TOOLS_LLDB_DAP_RUNINTERMINAL_H

#include "FifoFiles.h"

#include <future>
#include <thread>

namespace lldb_dap {

enum RunInTerminalMessageKind {
  eRunInTerminalMessageKindPID = 0,
  eRunInTerminalMessageKindError,
  eRunInTerminalMessageKindDidAttach,
};

struct RunInTerminalMessage;
struct RunInTerminalMessagePid;
struct RunInTerminalMessageError;
struct RunInTerminalMessageDidAttach;

struct RunInTerminalMessage {
  RunInTerminalMessage(RunInTerminalMessageKind kind);

  virtual ~RunInTerminalMessage() = default;

  /// Serialize this object to JSON
  virtual llvm::json::Value ToJSON() const = 0;

  const RunInTerminalMessagePid *GetAsPidMessage() const;

  const RunInTerminalMessageError *GetAsErrorMessage() const;

  RunInTerminalMessageKind kind;
};

using RunInTerminalMessageUP = std::unique_ptr<RunInTerminalMessage>;

struct RunInTerminalMessagePid : RunInTerminalMessage {
  RunInTerminalMessagePid(lldb::pid_t pid);

  llvm::json::Value ToJSON() const override;

  lldb::pid_t pid;
};

struct RunInTerminalMessageError : RunInTerminalMessage {
  RunInTerminalMessageError(llvm::StringRef error);

  llvm::json::Value ToJSON() const override;

  std::string error;
};

struct RunInTerminalMessageDidAttach : RunInTerminalMessage {
  RunInTerminalMessageDidAttach();

  llvm::json::Value ToJSON() const override;
};

class RunInTerminalLauncherCommChannel {
public:
  RunInTerminalLauncherCommChannel(llvm::StringRef comm_file);

  /// Wait until the debug adaptor attaches.
  ///
  /// \param[in] timeout
  ///     How long to wait to be attached.
  //
  /// \return
  ///     An \a llvm::Error object in case of errors or if this operation times
  ///     out.
  llvm::Error WaitUntilDebugAdaptorAttaches(std::chrono::milliseconds timeout);

  /// Notify the debug adaptor this process' pid.
  ///
  /// \return
  ///     An \a llvm::Error object in case of errors or if this operation times
  ///     out.
  llvm::Error NotifyPid();

  /// Notify the debug adaptor that there's been an error.
  void NotifyError(llvm::StringRef error);

private:
  FifoFileIO m_io;
};

class RunInTerminalDebugAdapterCommChannel {
public:
  RunInTerminalDebugAdapterCommChannel(llvm::StringRef comm_file);

  /// Notify the runInTerminal launcher that it was attached.
  ///
  /// \return
  ///     A future indicated whether the runInTerminal launcher received the
  ///     message correctly or not.
  std::future<lldb::SBError> NotifyDidAttach();

  /// Fetch the pid of the runInTerminal launcher.
  ///
  /// \return
  ///     An \a llvm::Error object in case of errors or if this operation times
  ///     out.
  llvm::Expected<lldb::pid_t> GetLauncherPid();

  /// Fetch any errors emitted by the runInTerminal launcher or return a
  /// default error message if a certain timeout if reached.
  std::string GetLauncherError();

private:
  FifoFileIO m_io;
};

/// Create a fifo file used to communicate the debug adaptor with
/// the runInTerminal launcher.
llvm::Expected<std::shared_ptr<FifoFile>> CreateRunInTerminalCommFile();

} // namespace lldb_dap

#endif // LLDB_TOOLS_LLDB_DAP_RUNINTERMINAL_H
