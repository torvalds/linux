//===-- ArchitectureMips.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ARCHITECTURE_MIPS_ARCHITECTUREMIPS_H
#define LLDB_SOURCE_PLUGINS_ARCHITECTURE_MIPS_ARCHITECTUREMIPS_H

#include "lldb/Core/Architecture.h"
#include "lldb/Utility/ArchSpec.h"

namespace lldb_private {

class ArchitectureMips : public Architecture {
public:
  static llvm::StringRef GetPluginNameStatic() { return "mips"; }
  static void Initialize();
  static void Terminate();

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  void OverrideStopInfo(Thread &thread) const override {}

  lldb::addr_t GetBreakableLoadAddress(lldb::addr_t addr,
                                       Target &) const override;

  lldb::addr_t GetCallableLoadAddress(lldb::addr_t load_addr,
                                      AddressClass addr_class) const override;

  lldb::addr_t GetOpcodeLoadAddress(lldb::addr_t load_addr,
                                    AddressClass addr_class) const override;

private:
  Instruction *GetInstructionAtAddress(Target &target,
                                       const Address &resolved_addr,
                                       lldb::addr_t symbol_offset) const;

  static std::unique_ptr<Architecture> Create(const ArchSpec &arch);
  ArchitectureMips(const ArchSpec &arch) : m_arch(arch) {}

  ArchSpec m_arch;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_ARCHITECTURE_MIPS_ARCHITECTUREMIPS_H
