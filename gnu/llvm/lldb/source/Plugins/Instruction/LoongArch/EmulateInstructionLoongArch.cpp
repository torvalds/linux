//===---EmulateInstructionLoongArch.cpp------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <optional>

#include "EmulateInstructionLoongArch.h"
#include "Plugins/Process/Utility/InstructionUtils.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_loongarch64.h"
#include "Plugins/Process/Utility/lldb-loongarch-register-enums.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Interpreter/OptionValueArray.h"
#include "lldb/Interpreter/OptionValueDictionary.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Stream.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/MathExtras.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(EmulateInstructionLoongArch, InstructionLoongArch)

namespace lldb_private {

EmulateInstructionLoongArch::Opcode *
EmulateInstructionLoongArch::GetOpcodeForInstruction(uint32_t inst) {
  // TODO: Add the mask for other instruction.
  static EmulateInstructionLoongArch::Opcode g_opcodes[] = {
      {0xfc000000, 0x40000000, &EmulateInstructionLoongArch::EmulateBEQZ,
       "beqz rj, offs21"},
      {0xfc000000, 0x44000000, &EmulateInstructionLoongArch::EmulateBNEZ,
       "bnez rj, offs21"},
      {0xfc000300, 0x48000000, &EmulateInstructionLoongArch::EmulateBCEQZ,
       "bceqz cj, offs21"},
      {0xfc000300, 0x48000100, &EmulateInstructionLoongArch::EmulateBCNEZ,
       "bcnez cj, offs21"},
      {0xfc000000, 0x4c000000, &EmulateInstructionLoongArch::EmulateJIRL,
       "jirl rd, rj, offs16"},
      {0xfc000000, 0x50000000, &EmulateInstructionLoongArch::EmulateB,
       " b  offs26"},
      {0xfc000000, 0x54000000, &EmulateInstructionLoongArch::EmulateBL,
       "bl  offs26"},
      {0xfc000000, 0x58000000, &EmulateInstructionLoongArch::EmulateBEQ,
       "beq  rj, rd, offs16"},
      {0xfc000000, 0x5c000000, &EmulateInstructionLoongArch::EmulateBNE,
       "bne  rj, rd, offs16"},
      {0xfc000000, 0x60000000, &EmulateInstructionLoongArch::EmulateBLT,
       "blt  rj, rd, offs16"},
      {0xfc000000, 0x64000000, &EmulateInstructionLoongArch::EmulateBGE,
       "bge  rj, rd, offs16"},
      {0xfc000000, 0x68000000, &EmulateInstructionLoongArch::EmulateBLTU,
       "bltu rj, rd, offs16"},
      {0xfc000000, 0x6c000000, &EmulateInstructionLoongArch::EmulateBGEU,
       "bgeu rj, rd, offs16"},
      {0x00000000, 0x00000000, &EmulateInstructionLoongArch::EmulateNonJMP,
       "NonJMP"}};
  static const size_t num_loongarch_opcodes = std::size(g_opcodes);

  for (size_t i = 0; i < num_loongarch_opcodes; ++i)
    if ((g_opcodes[i].mask & inst) == g_opcodes[i].value)
      return &g_opcodes[i];
  return nullptr;
}

bool EmulateInstructionLoongArch::TestExecute(uint32_t inst) {
  Opcode *opcode_data = GetOpcodeForInstruction(inst);
  if (!opcode_data)
    return false;
  // Call the Emulate... function.
  if (!(this->*opcode_data->callback)(inst))
    return false;
  return true;
}

bool EmulateInstructionLoongArch::EvaluateInstruction(uint32_t options) {
  uint32_t inst_size = m_opcode.GetByteSize();
  uint32_t inst = m_opcode.GetOpcode32();
  bool increase_pc = options & eEmulateInstructionOptionAutoAdvancePC;
  bool success = false;

  Opcode *opcode_data = GetOpcodeForInstruction(inst);
  if (!opcode_data)
    return false;

  lldb::addr_t old_pc = 0;
  if (increase_pc) {
    old_pc = ReadPC(&success);
    if (!success)
      return false;
  }

  // Call the Emulate... function.
  if (!(this->*opcode_data->callback)(inst))
    return false;

  if (increase_pc) {
    lldb::addr_t new_pc = ReadPC(&success);
    if (!success)
      return false;

    if (new_pc == old_pc && !WritePC(old_pc + inst_size))
      return false;
  }
  return true;
}

bool EmulateInstructionLoongArch::ReadInstruction() {
  bool success = false;
  m_addr = ReadPC(&success);
  if (!success) {
    m_addr = LLDB_INVALID_ADDRESS;
    return false;
  }

  Context ctx;
  ctx.type = eContextReadOpcode;
  ctx.SetNoArgs();
  uint32_t inst = (uint32_t)ReadMemoryUnsigned(ctx, m_addr, 4, 0, &success);
  m_opcode.SetOpcode32(inst, GetByteOrder());

  return true;
}

lldb::addr_t EmulateInstructionLoongArch::ReadPC(bool *success) {
  return ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC,
                              LLDB_INVALID_ADDRESS, success);
}

bool EmulateInstructionLoongArch::WritePC(lldb::addr_t pc) {
  EmulateInstruction::Context ctx;
  ctx.type = eContextAdvancePC;
  ctx.SetNoArgs();
  return WriteRegisterUnsigned(ctx, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_PC, pc);
}

std::optional<RegisterInfo>
EmulateInstructionLoongArch::GetRegisterInfo(lldb::RegisterKind reg_kind,
                                             uint32_t reg_index) {
  if (reg_kind == eRegisterKindGeneric) {
    switch (reg_index) {
    case LLDB_REGNUM_GENERIC_PC:
      reg_kind = eRegisterKindLLDB;
      reg_index = gpr_pc_loongarch;
      break;
    case LLDB_REGNUM_GENERIC_SP:
      reg_kind = eRegisterKindLLDB;
      reg_index = gpr_sp_loongarch;
      break;
    case LLDB_REGNUM_GENERIC_FP:
      reg_kind = eRegisterKindLLDB;
      reg_index = gpr_fp_loongarch;
      break;
    case LLDB_REGNUM_GENERIC_RA:
      reg_kind = eRegisterKindLLDB;
      reg_index = gpr_ra_loongarch;
      break;
    // We may handle LLDB_REGNUM_GENERIC_ARGx when more instructions are
    // supported.
    default:
      llvm_unreachable("unsupported register");
    }
  }

  const RegisterInfo *array =
      RegisterInfoPOSIX_loongarch64::GetRegisterInfoPtr(m_arch);
  const uint32_t length =
      RegisterInfoPOSIX_loongarch64::GetRegisterInfoCount(m_arch);

  if (reg_index >= length || reg_kind != eRegisterKindLLDB)
    return {};
  return array[reg_index];
}

bool EmulateInstructionLoongArch::SetTargetTriple(const ArchSpec &arch) {
  return SupportsThisArch(arch);
}

bool EmulateInstructionLoongArch::TestEmulation(
    Stream &out_stream, ArchSpec &arch, OptionValueDictionary *test_data) {
  return false;
}

void EmulateInstructionLoongArch::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void EmulateInstructionLoongArch::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::EmulateInstruction *
EmulateInstructionLoongArch::CreateInstance(const ArchSpec &arch,
                                            InstructionType inst_type) {
  if (EmulateInstructionLoongArch::SupportsThisInstructionType(inst_type) &&
      SupportsThisArch(arch))
    return new EmulateInstructionLoongArch(arch);
  return nullptr;
}

bool EmulateInstructionLoongArch::SupportsThisArch(const ArchSpec &arch) {
  return arch.GetTriple().isLoongArch();
}

bool EmulateInstructionLoongArch::EmulateBEQZ(uint32_t inst) {
  return IsLoongArch64() ? EmulateBEQZ64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBNEZ(uint32_t inst) {
  return IsLoongArch64() ? EmulateBNEZ64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBCEQZ(uint32_t inst) {
  return IsLoongArch64() ? EmulateBCEQZ64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBCNEZ(uint32_t inst) {
  return IsLoongArch64() ? EmulateBCNEZ64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateJIRL(uint32_t inst) {
  return IsLoongArch64() ? EmulateJIRL64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateB(uint32_t inst) {
  return IsLoongArch64() ? EmulateB64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBL(uint32_t inst) {
  return IsLoongArch64() ? EmulateBL64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBEQ(uint32_t inst) {
  return IsLoongArch64() ? EmulateBEQ64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBNE(uint32_t inst) {
  return IsLoongArch64() ? EmulateBNE64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBLT(uint32_t inst) {
  return IsLoongArch64() ? EmulateBLT64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBGE(uint32_t inst) {
  return IsLoongArch64() ? EmulateBGE64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBLTU(uint32_t inst) {
  return IsLoongArch64() ? EmulateBLTU64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateBGEU(uint32_t inst) {
  return IsLoongArch64() ? EmulateBGEU64(inst) : false;
}

bool EmulateInstructionLoongArch::EmulateNonJMP(uint32_t inst) { return false; }

// beqz rj, offs21
// if GR[rj] == 0:
//   PC = PC + SignExtend({offs21, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBEQZ64(uint32_t inst) {
  bool success = false;
  uint32_t rj = Bits32(inst, 9, 5);
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  uint32_t offs21 = Bits32(inst, 25, 10) + (Bits32(inst, 4, 0) << 16);
  uint64_t rj_val = ReadRegisterUnsigned(eRegisterKindLLDB, rj, 0, &success);
  if (!success)
    return false;
  if (rj_val == 0) {
    uint64_t next_pc = pc + llvm::SignExtend64<23>(offs21 << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
}

// bnez rj, offs21
// if GR[rj] != 0:
//   PC = PC + SignExtend({offs21, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBNEZ64(uint32_t inst) {
  bool success = false;
  uint32_t rj = Bits32(inst, 9, 5);
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  uint32_t offs21 = Bits32(inst, 25, 10) + (Bits32(inst, 4, 0) << 16);
  uint64_t rj_val = ReadRegisterUnsigned(eRegisterKindLLDB, rj, 0, &success);
  if (!success)
    return false;
  if (rj_val != 0) {
    uint64_t next_pc = pc + llvm::SignExtend64<23>(offs21 << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
}

// bceqz cj, offs21
// if CFR[cj] == 0:
//	PC = PC + SignExtend({offs21, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBCEQZ64(uint32_t inst) {
  bool success = false;
  uint32_t cj = Bits32(inst, 7, 5) + fpr_fcc0_loongarch;
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  uint32_t offs21 = Bits32(inst, 25, 10) + (Bits32(inst, 4, 0) << 16);
  uint8_t cj_val =
      (uint8_t)ReadRegisterUnsigned(eRegisterKindLLDB, cj, 0, &success);
  if (!success)
    return false;
  if (cj_val == 0) {
    uint64_t next_pc = pc + llvm::SignExtend64<23>(offs21 << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
  return false;
}

// bcnez cj, offs21
// if CFR[cj] != 0:
//	PC = PC + SignExtend({offs21, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBCNEZ64(uint32_t inst) {
  bool success = false;
  uint32_t cj = Bits32(inst, 7, 5) + fpr_fcc0_loongarch;
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  uint32_t offs21 = Bits32(inst, 25, 10) + (Bits32(inst, 4, 0) << 16);
  uint8_t cj_val =
      (uint8_t)ReadRegisterUnsigned(eRegisterKindLLDB, cj, 0, &success);
  if (!success)
    return false;
  if (cj_val != 0) {
    uint64_t next_pc = pc + llvm::SignExtend64<23>(offs21 << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
  return false;
}

// jirl rd, rj, offs16
// GR[rd] = PC + 4
// PC = GR[rj] + SignExtend({offs16, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateJIRL64(uint32_t inst) {
  uint32_t rj = Bits32(inst, 9, 5);
  uint32_t rd = Bits32(inst, 4, 0);
  bool success = false;
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  EmulateInstruction::Context ctx;
  if (!WriteRegisterUnsigned(ctx, eRegisterKindLLDB, rd, pc + 4))
    return false;
  uint64_t rj_val = ReadRegisterUnsigned(eRegisterKindLLDB, rj, 0, &success);
  if (!success)
    return false;
  uint64_t next_pc = rj_val + llvm::SignExtend64<18>(Bits32(inst, 25, 10) << 2);
  return WritePC(next_pc);
}

// b offs26
// PC = PC + SignExtend({offs26, 2' b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateB64(uint32_t inst) {
  bool success = false;
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  uint32_t offs26 = Bits32(inst, 25, 10) + (Bits32(inst, 9, 0) << 16);
  uint64_t next_pc = pc + llvm::SignExtend64<28>(offs26 << 2);
  return WritePC(next_pc);
}

// bl offs26
// GR[1] = PC + 4
// PC = PC + SignExtend({offs26, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBL64(uint32_t inst) {
  bool success = false;
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  EmulateInstruction::Context ctx;
  if (!WriteRegisterUnsigned(ctx, eRegisterKindLLDB, gpr_r1_loongarch, pc + 4))
    return false;
  uint32_t offs26 = Bits32(inst, 25, 10) + (Bits32(inst, 9, 0) << 16);
  uint64_t next_pc = pc + llvm::SignExtend64<28>(offs26 << 2);
  return WritePC(next_pc);
}

// beq rj, rd, offs16
// if GR[rj] == GR[rd]:
//   PC = PC + SignExtend({offs16, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBEQ64(uint32_t inst) {
  bool success = false;
  uint32_t rj = Bits32(inst, 9, 5);
  uint32_t rd = Bits32(inst, 4, 0);
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  uint64_t rj_val = ReadRegisterUnsigned(eRegisterKindLLDB, rj, 0, &success);
  if (!success)
    return false;
  uint64_t rd_val = ReadRegisterUnsigned(eRegisterKindLLDB, rd, 0, &success);
  if (!success)
    return false;
  if (rj_val == rd_val) {
    uint64_t next_pc = pc + llvm::SignExtend64<18>(Bits32(inst, 25, 10) << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
}

// bne rj, rd, offs16
// if GR[rj] != GR[rd]:
//   PC = PC + SignExtend({offs16, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBNE64(uint32_t inst) {
  bool success = false;
  uint32_t rj = Bits32(inst, 9, 5);
  uint32_t rd = Bits32(inst, 4, 0);
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  uint64_t rj_val = ReadRegisterUnsigned(eRegisterKindLLDB, rj, 0, &success);
  if (!success)
    return false;
  uint64_t rd_val = ReadRegisterUnsigned(eRegisterKindLLDB, rd, 0, &success);
  if (!success)
    return false;
  if (rj_val != rd_val) {
    uint64_t next_pc = pc + llvm::SignExtend64<18>(Bits32(inst, 25, 10) << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
}

// blt rj, rd, offs16
// if signed(GR[rj]) < signed(GR[rd]):
//   PC = PC + SignExtend({offs16, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBLT64(uint32_t inst) {
  bool success = false;
  uint32_t rj = Bits32(inst, 9, 5);
  uint32_t rd = Bits32(inst, 4, 0);
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  int64_t rj_val =
      (int64_t)ReadRegisterUnsigned(eRegisterKindLLDB, rj, 0, &success);
  if (!success)
    return false;
  int64_t rd_val =
      (int64_t)ReadRegisterUnsigned(eRegisterKindLLDB, rd, 0, &success);
  if (!success)
    return false;
  if (rj_val < rd_val) {
    uint64_t next_pc = pc + llvm::SignExtend64<18>(Bits32(inst, 25, 10) << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
}

// bge rj, rd, offs16
// if signed(GR[rj]) >= signed(GR[rd]):
//   PC = PC + SignExtend({offs16, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBGE64(uint32_t inst) {
  bool success = false;
  uint32_t rj = Bits32(inst, 9, 5);
  uint32_t rd = Bits32(inst, 4, 0);
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  int64_t rj_val =
      (int64_t)ReadRegisterUnsigned(eRegisterKindLLDB, rj, 0, &success);
  if (!success)
    return false;
  int64_t rd_val =
      (int64_t)ReadRegisterUnsigned(eRegisterKindLLDB, rd, 0, &success);
  if (!success)
    return false;
  if (rj_val >= rd_val) {
    uint64_t next_pc = pc + llvm::SignExtend64<18>(Bits32(inst, 25, 10) << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
}

// bltu rj, rd, offs16
// if unsigned(GR[rj]) < unsigned(GR[rd]):
//   PC = PC + SignExtend({offs16, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBLTU64(uint32_t inst) {
  bool success = false;
  uint32_t rj = Bits32(inst, 9, 5);
  uint32_t rd = Bits32(inst, 4, 0);
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  uint64_t rj_val = ReadRegisterUnsigned(eRegisterKindLLDB, rj, 0, &success);
  if (!success)
    return false;
  uint64_t rd_val = ReadRegisterUnsigned(eRegisterKindLLDB, rd, 0, &success);
  if (!success)
    return false;
  if (rj_val < rd_val) {
    uint64_t next_pc = pc + llvm::SignExtend64<18>(Bits32(inst, 25, 10) << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
}

// bgeu rj, rd, offs16
// if unsigned(GR[rj]) >= unsigned(GR[rd]):
//   PC = PC + SignExtend({offs16, 2'b0}, GRLEN)
bool EmulateInstructionLoongArch::EmulateBGEU64(uint32_t inst) {
  bool success = false;
  uint32_t rj = Bits32(inst, 9, 5);
  uint32_t rd = Bits32(inst, 4, 0);
  uint64_t pc = ReadPC(&success);
  if (!success)
    return false;
  uint64_t rj_val = ReadRegisterUnsigned(eRegisterKindLLDB, rj, 0, &success);
  if (!success)
    return false;
  uint64_t rd_val = ReadRegisterUnsigned(eRegisterKindLLDB, rd, 0, &success);
  if (!success)
    return false;
  if (rj_val >= rd_val) {
    uint64_t next_pc = pc + llvm::SignExtend64<18>(Bits32(inst, 25, 10) << 2);
    return WritePC(next_pc);
  } else
    return WritePC(pc + 4);
}

} // namespace lldb_private
