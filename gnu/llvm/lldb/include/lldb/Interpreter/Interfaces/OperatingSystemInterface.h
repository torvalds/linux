//===-- OperatingSystemInterface.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_INTERFACES_OPERATINGSYSTEMINTERFACE_H
#define LLDB_INTERPRETER_INTERFACES_OPERATINGSYSTEMINTERFACE_H

#include "ScriptedThreadInterface.h"
#include "lldb/Core/StructuredDataImpl.h"

#include "lldb/lldb-private.h"

namespace lldb_private {
class OperatingSystemInterface : virtual public ScriptedThreadInterface {
public:
  virtual StructuredData::DictionarySP CreateThread(lldb::tid_t tid,
                                                    lldb::addr_t context) {
    return {};
  }

  virtual StructuredData::ArraySP GetThreadInfo() { return {}; }

  virtual std::optional<std::string> GetRegisterContextForTID(lldb::tid_t tid) {
    return std::nullopt;
  }
};
} // namespace lldb_private

#endif // LLDB_INTERPRETER_INTERFACES_OPERATINGSYSTEMINTERFACE_H
