//===-- CommandObjectTrace.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTTRACE_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTTRACE_H

#include "CommandObjectThreadUtil.h"

namespace lldb_private {

class CommandObjectTrace : public CommandObjectMultiword {
public:
  CommandObjectTrace(CommandInterpreter &interpreter);

  ~CommandObjectTrace() override;
};

/// This class works by delegating the logic to the actual trace plug-in that
/// can support the current process.
class CommandObjectTraceProxy : public CommandObjectProxy {
public:
  CommandObjectTraceProxy(bool live_debug_session_only,
                          CommandInterpreter &interpreter, const char *name,
                          const char *help = nullptr,
                          const char *syntax = nullptr, uint32_t flags = 0)
      : CommandObjectProxy(interpreter, name, help, syntax, flags),
        m_live_debug_session_only(live_debug_session_only) {}

protected:
  virtual lldb::CommandObjectSP GetDelegateCommand(Trace &trace) = 0;

  llvm::Expected<lldb::CommandObjectSP> DoGetProxyCommandObject();

  CommandObject *GetProxyCommandObject() override;

private:
  llvm::StringRef GetUnsupportedError() override { return m_delegate_error; }

  bool m_live_debug_session_only;
  lldb::CommandObjectSP m_delegate_sp;
  std::string m_delegate_error;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTTRACE_H
