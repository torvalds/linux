//===-- DisassemblerLLVMC.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_DISASSEMBLER_LLVMC_DISASSEMBLERLLVMC_H
#define LLDB_SOURCE_PLUGINS_DISASSEMBLER_LLVMC_DISASSEMBLERLLVMC_H

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "lldb/Core/Address.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/PluginManager.h"

class InstructionLLVMC;

class DisassemblerLLVMC : public lldb_private::Disassembler {
public:
  DisassemblerLLVMC(const lldb_private::ArchSpec &arch,
                    const char *flavor /* = NULL */);

  ~DisassemblerLLVMC() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "llvm-mc"; }

  static lldb::DisassemblerSP CreateInstance(const lldb_private::ArchSpec &arch,
                                             const char *flavor);

  size_t DecodeInstructions(const lldb_private::Address &base_addr,
                            const lldb_private::DataExtractor &data,
                            lldb::offset_t data_offset, size_t num_instructions,
                            bool append, bool data_from_file) override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  friend class InstructionLLVMC;

  bool FlavorValidForArchSpec(const lldb_private::ArchSpec &arch,
                              const char *flavor) override;

  bool IsValid() const;

  int OpInfo(uint64_t PC, uint64_t Offset, uint64_t Size, int TagType,
             void *TagBug);

  const char *SymbolLookup(uint64_t ReferenceValue, uint64_t *ReferenceType,
                           uint64_t ReferencePC, const char **ReferenceName);

  static int OpInfoCallback(void *DisInfo, uint64_t PC, uint64_t Offset,
                            uint64_t Size, int TagType, void *TagBug);

  static const char *SymbolLookupCallback(void *DisInfo,
                                          uint64_t ReferenceValue,
                                          uint64_t *ReferenceType,
                                          uint64_t ReferencePC,
                                          const char **ReferenceName);

  const lldb_private::ExecutionContext *m_exe_ctx;
  InstructionLLVMC *m_inst;
  std::mutex m_mutex;
  bool m_data_from_file;
  // Save the AArch64 ADRP instruction word and address it was at,
  // in case the next instruction is an ADD to the same register;
  // this is a pc-relative address calculation and we need both
  // parts to calculate the symbolication.
  lldb::addr_t m_adrp_address;
  std::optional<uint32_t> m_adrp_insn;

  // Since we need to make two actual MC Disassemblers for ARM (ARM & THUMB),
  // and there's a bit of goo to set up and own in the MC disassembler world,
  // this class was added to manage the actual disassemblers.
  class MCDisasmInstance;
  std::unique_ptr<MCDisasmInstance> m_disasm_up;
  std::unique_ptr<MCDisasmInstance> m_alternate_disasm_up;
};

#endif // LLDB_SOURCE_PLUGINS_DISASSEMBLER_LLVMC_DISASSEMBLERLLVMC_H
