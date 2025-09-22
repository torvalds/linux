//===-- Runtime.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_RUNTIME_H
#define LLDB_TARGET_RUNTIME_H

#include "lldb/Target/Process.h"

namespace lldb_private {
class Runtime {
public:
  Runtime(Process *process) : m_process(process) {}
  virtual ~Runtime() = default;
  Runtime(const Runtime &) = delete;
  const Runtime &operator=(const Runtime &) = delete;

  Process *GetProcess() { return m_process; }
  Target &GetTargetRef() { return m_process->GetTarget(); }

  /// Called when modules have been loaded in the process.
  virtual void ModulesDidLoad(const ModuleList &module_list) = 0;

protected:
  Process *m_process;
};
} // namespace lldb_private

#endif // LLDB_TARGET_RUNTIME_H
