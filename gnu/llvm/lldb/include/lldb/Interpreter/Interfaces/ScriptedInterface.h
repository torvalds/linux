//===-- ScriptedInterface.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_INTERFACES_SCRIPTEDINTERFACE_H
#define LLDB_INTERPRETER_INTERFACES_SCRIPTEDINTERFACE_H

#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/UnimplementedError.h"
#include "lldb/lldb-private.h"

#include "llvm/Support/Compiler.h"

#include <string>

namespace lldb_private {
class ScriptedInterface {
public:
  ScriptedInterface() = default;
  virtual ~ScriptedInterface() = default;

  StructuredData::GenericSP GetScriptObjectInstance() {
    return m_object_instance_sp;
  }

  virtual llvm::SmallVector<llvm::StringLiteral> GetAbstractMethods() const = 0;

  template <typename Ret>
  static Ret ErrorWithMessage(llvm::StringRef caller_name,
                              llvm::StringRef error_msg, Status &error,
                              LLDBLog log_caterogy = LLDBLog::Process) {
    LLDB_LOGF(GetLog(log_caterogy), "%s ERROR = %s", caller_name.data(),
              error_msg.data());
    std::string full_error_message =
        llvm::Twine(caller_name + llvm::Twine(" ERROR = ") +
                    llvm::Twine(error_msg))
            .str();
    if (const char *detailed_error = error.AsCString())
      full_error_message +=
          llvm::Twine(llvm::Twine(" (") + llvm::Twine(detailed_error) +
                      llvm::Twine(")"))
              .str();
    error.SetErrorString(full_error_message);
    return {};
  }

  template <typename T = StructuredData::ObjectSP>
  static bool CheckStructuredDataObject(llvm::StringRef caller, T obj,
                                        Status &error) {
    if (!obj)
      return ErrorWithMessage<bool>(caller, "Null Structured Data object",
                                    error);

    if (!obj->IsValid()) {
      return ErrorWithMessage<bool>(caller, "Invalid StructuredData object",
                                    error);
    }

    if (error.Fail())
      return ErrorWithMessage<bool>(caller, error.AsCString(), error);

    return true;
  }

protected:
  StructuredData::GenericSP m_object_instance_sp;
};
} // namespace lldb_private
#endif // LLDB_INTERPRETER_INTERFACES_SCRIPTEDINTERFACE_H
