//===-- ArchitectureAArch64.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ARCHITECTURE_AARCH64_ARCHITECTUREAARCH64_H
#define LLDB_SOURCE_PLUGINS_ARCHITECTURE_AARCH64_ARCHITECTUREAARCH64_H

#include "Plugins/Process/Utility/MemoryTagManagerAArch64MTE.h"
#include "lldb/Core/Architecture.h"

namespace lldb_private {

class ArchitectureAArch64 : public Architecture {
public:
  static llvm::StringRef GetPluginNameStatic() { return "aarch64"; }
  static void Initialize();
  static void Terminate();

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  void OverrideStopInfo(Thread &thread) const override {}

  const MemoryTagManager *GetMemoryTagManager() const override {
    return &m_memory_tag_manager;
  }

  bool
  RegisterWriteCausesReconfigure(const llvm::StringRef name) const override {
    // lldb treats svg as read only, so only vg can be written. This results in
    // the SVE registers changing size.
    return name == "vg";
  }

  bool ReconfigureRegisterInfo(DynamicRegisterInfo &reg_info,
                               DataExtractor &reg_data,
                               RegisterContext &reg_context) const override;

private:
  static std::unique_ptr<Architecture> Create(const ArchSpec &arch);
  ArchitectureAArch64() = default;
  MemoryTagManagerAArch64MTE m_memory_tag_manager;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_ARCHITECTURE_AARCH64_ARCHITECTUREAARCH64_H
