//===-- EmulateInstructionRISCV.h -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUCTION_RISCV_EMULATEINSTRUCTIONRISCV_H
#define LLDB_SOURCE_PLUGINS_INSTRUCTION_RISCV_EMULATEINSTRUCTIONRISCV_H

#include "RISCVInstructions.h"

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include <optional>

namespace lldb_private {

class EmulateInstructionRISCV : public EmulateInstruction {
public:
  static llvm::StringRef GetPluginNameStatic() { return "riscv"; }

  static llvm::StringRef GetPluginDescriptionStatic() {
    return "Emulate instructions for the RISC-V architecture.";
  }

  static bool SupportsThisInstructionType(InstructionType inst_type) {
    switch (inst_type) {
    case eInstructionTypeAny:
    case eInstructionTypePCModifying:
      return true;
    case eInstructionTypePrologueEpilogue:
    case eInstructionTypeAll:
      return false;
    }
    llvm_unreachable("Fully covered switch above!");
  }

  static bool SupportsThisArch(const ArchSpec &arch);

  static lldb_private::EmulateInstruction *
  CreateInstance(const lldb_private::ArchSpec &arch, InstructionType inst_type);

  static void Initialize();

  static void Terminate();

public:
  EmulateInstructionRISCV(const ArchSpec &arch) : EmulateInstruction(arch) {}

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  bool SupportsEmulatingInstructionsOfType(InstructionType inst_type) override {
    return SupportsThisInstructionType(inst_type);
  }

  bool SetTargetTriple(const ArchSpec &arch) override;
  bool ReadInstruction() override;
  std::optional<uint32_t> GetLastInstrSize() override { return m_last_size; }
  bool EvaluateInstruction(uint32_t options) override;
  bool TestEmulation(Stream &out_stream, ArchSpec &arch,
                     OptionValueDictionary *test_data) override;
  std::optional<RegisterInfo> GetRegisterInfo(lldb::RegisterKind reg_kind,
                                              uint32_t reg_num) override;

  std::optional<lldb::addr_t> ReadPC();
  bool WritePC(lldb::addr_t pc);

  std::optional<DecodeResult> ReadInstructionAt(lldb::addr_t addr);
  std::optional<DecodeResult> Decode(uint32_t inst);
  bool Execute(DecodeResult inst, bool ignore_cond);

  template <typename T>
  std::enable_if_t<std::is_integral_v<T>, std::optional<T>>
  ReadMem(uint64_t addr) {
    EmulateInstructionRISCV::Context ctx;
    ctx.type = EmulateInstruction::eContextRegisterLoad;
    ctx.SetNoArgs();
    bool success = false;
    T result = ReadMemoryUnsigned(ctx, addr, sizeof(T), T(), &success);
    if (!success)
      return {}; // aka return false
    return result;
  }

  template <typename T> bool WriteMem(uint64_t addr, uint64_t value) {
    EmulateInstructionRISCV::Context ctx;
    ctx.type = EmulateInstruction::eContextRegisterStore;
    ctx.SetNoArgs();
    return WriteMemoryUnsigned(ctx, addr, value, sizeof(T));
  }

  llvm::RoundingMode GetRoundingMode();
  bool SetAccruedExceptions(llvm::APFloatBase::opStatus);

private:
  /// Last decoded instruction from m_opcode
  DecodeResult m_decoded;
  /// Last decoded instruction size estimate.
  std::optional<uint32_t> m_last_size;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_INSTRUCTION_RISCV_EMULATEINSTRUCTIONRISCV_H
