//===-- ScriptedProcessInterface.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_INTERFACES_SCRIPTEDPROCESSINTERFACE_H
#define LLDB_INTERPRETER_INTERFACES_SCRIPTEDPROCESSINTERFACE_H

#include "ScriptedInterface.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Target/MemoryRegionInfo.h"

#include "lldb/lldb-private.h"

#include <optional>
#include <string>

namespace lldb_private {
class ScriptedProcessInterface : virtual public ScriptedInterface {
public:
  virtual llvm::Expected<StructuredData::GenericSP>
  CreatePluginObject(llvm::StringRef class_name, ExecutionContext &exe_ctx,
                     StructuredData::DictionarySP args_sp,
                     StructuredData::Generic *script_obj = nullptr) = 0;

  virtual StructuredData::DictionarySP GetCapabilities() { return {}; }

  virtual Status Attach(const ProcessAttachInfo &attach_info) {
    return Status("ScriptedProcess did not attach");
  }

  virtual Status Launch() { return Status("ScriptedProcess did not launch"); }

  virtual Status Resume() { return Status("ScriptedProcess did not resume"); }

  virtual std::optional<MemoryRegionInfo>
  GetMemoryRegionContainingAddress(lldb::addr_t address, Status &error) {
    error.SetErrorString("ScriptedProcess have no memory region.");
    return {};
  }

  virtual StructuredData::DictionarySP GetThreadsInfo() { return {}; }

  virtual bool CreateBreakpoint(lldb::addr_t addr, Status &error) {
    error.SetErrorString("ScriptedProcess don't support creating breakpoints.");
    return {};
  }

  virtual lldb::DataExtractorSP
  ReadMemoryAtAddress(lldb::addr_t address, size_t size, Status &error) {
    return {};
  }

  virtual lldb::offset_t WriteMemoryAtAddress(lldb::addr_t addr,
                                              lldb::DataExtractorSP data_sp,
                                              Status &error) {
    return LLDB_INVALID_OFFSET;
  };

  virtual StructuredData::ArraySP GetLoadedImages() { return {}; }

  virtual lldb::pid_t GetProcessID() { return LLDB_INVALID_PROCESS_ID; }

  virtual bool IsAlive() { return true; }

  virtual std::optional<std::string> GetScriptedThreadPluginName() {
    return std::nullopt;
  }

  virtual StructuredData::DictionarySP GetMetadata() { return {}; }

protected:
  friend class ScriptedThread;
  virtual lldb::ScriptedThreadInterfaceSP CreateScriptedThreadInterface() {
    return {};
  }
};
} // namespace lldb_private

#endif // LLDB_INTERPRETER_INTERFACES_SCRIPTEDPROCESSINTERFACE_H
