//===-- ScriptedThreadInterface.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_INTERFACES_SCRIPTEDTHREADINTERFACE_H
#define LLDB_INTERPRETER_INTERFACES_SCRIPTEDTHREADINTERFACE_H

#include "ScriptedInterface.h"
#include "lldb/Core/StructuredDataImpl.h"

#include "lldb/lldb-private.h"

#include <optional>
#include <string>

namespace lldb_private {
class ScriptedThreadInterface : virtual public ScriptedInterface {
public:
  virtual llvm::Expected<StructuredData::GenericSP>
  CreatePluginObject(llvm::StringRef class_name, ExecutionContext &exe_ctx,
                     StructuredData::DictionarySP args_sp,
                     StructuredData::Generic *script_obj = nullptr) = 0;

  virtual lldb::tid_t GetThreadID() { return LLDB_INVALID_THREAD_ID; }

  virtual std::optional<std::string> GetName() { return std::nullopt; }

  virtual lldb::StateType GetState() { return lldb::eStateInvalid; }

  virtual std::optional<std::string> GetQueue() { return std::nullopt; }

  virtual StructuredData::DictionarySP GetStopReason() { return {}; }

  virtual StructuredData::ArraySP GetStackFrames() { return {}; }

  virtual StructuredData::DictionarySP GetRegisterInfo() { return {}; }

  virtual std::optional<std::string> GetRegisterContext() {
    return std::nullopt;
  }

  virtual StructuredData::ArraySP GetExtendedInfo() { return {}; }
};
} // namespace lldb_private

#endif // LLDB_INTERPRETER_INTERFACES_SCRIPTEDTHREADINTERFACE_H
