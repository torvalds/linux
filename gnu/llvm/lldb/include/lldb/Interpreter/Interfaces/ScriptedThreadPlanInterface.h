//===-- ScriptedThreadPlanInterface.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_INTERFACES_SCRIPTEDTHREADPLANINTERFACE_H
#define LLDB_INTERPRETER_INTERFACES_SCRIPTEDTHREADPLANINTERFACE_H

#include "lldb/lldb-private.h"

#include "ScriptedInterface.h"

namespace lldb_private {
class ScriptedThreadPlanInterface : public ScriptedInterface {
public:
  virtual llvm::Expected<StructuredData::GenericSP>
  CreatePluginObject(llvm::StringRef class_name,
                     lldb::ThreadPlanSP thread_plan_sp,
                     const StructuredDataImpl &args_sp) = 0;

  virtual llvm::Expected<bool> ExplainsStop(Event *event) { return true; }

  virtual llvm::Expected<bool> ShouldStop(Event *event) { return true; }

  virtual llvm::Expected<bool> IsStale() { return true; };

  virtual lldb::StateType GetRunState() { return lldb::eStateStepping; }

  virtual llvm::Error GetStopDescription(lldb::StreamSP &stream) {
    return llvm::Error::success();
  }
};
} // namespace lldb_private

#endif // LLDB_INTERPRETER_INTERFACES_SCRIPTEDTHREADPLANINTERFACE_H
