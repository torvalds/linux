//===-- EmulateInstructionMIPS64.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EmulateInstructionMIPS64.h"

#include <cstdlib>
#include <optional>

#include "lldb/Core/Address.h"
#include "lldb/Core/Opcode.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Stream.h"
#include "llvm-c/Disassembler.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"

#include "llvm/ADT/STLExtras.h"

#include "Plugins/Process/Utility/InstructionUtils.h"
#include "Plugins/Process/Utility/RegisterContext_mips.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(EmulateInstructionMIPS64, InstructionMIPS64)

#define UInt(x) ((uint64_t)x)
#define integer int64_t

//
// EmulateInstructionMIPS64 implementation
//

#ifdef __mips__
extern "C" {
void LLVMInitializeMipsTargetInfo();
void LLVMInitializeMipsTarget();
void LLVMInitializeMipsAsmPrinter();
void LLVMInitializeMipsTargetMC();
void LLVMInitializeMipsDisassembler();
}
#endif

EmulateInstructionMIPS64::EmulateInstructionMIPS64(
    const lldb_private::ArchSpec &arch)
    : EmulateInstruction(arch) {
  /* Create instance of llvm::MCDisassembler */
  std::string Status;
  llvm::Triple triple = arch.GetTriple();
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(triple.getTriple(), Status);

/*
 * If we fail to get the target then we haven't registered it. The
 * SystemInitializerCommon
 * does not initialize targets, MCs and disassemblers. However we need the
 * MCDisassembler
 * to decode the instructions so that the decoding complexity stays with LLVM.
 * Initialize the MIPS targets and disassemblers.
*/
#ifdef __mips__
  if (!target) {
    LLVMInitializeMipsTargetInfo();
    LLVMInitializeMipsTarget();
    LLVMInitializeMipsAsmPrinter();
    LLVMInitializeMipsTargetMC();
    LLVMInitializeMipsDisassembler();
    target = llvm::TargetRegistry::lookupTarget(triple.getTriple(), Status);
  }
#endif

  assert(target);

  llvm::StringRef cpu;

  switch (arch.GetCore()) {
  case ArchSpec::eCore_mips32:
  case ArchSpec::eCore_mips32el:
    cpu = "mips32";
    break;
  case ArchSpec::eCore_mips32r2:
  case ArchSpec::eCore_mips32r2el:
    cpu = "mips32r2";
    break;
  case ArchSpec::eCore_mips32r3:
  case ArchSpec::eCore_mips32r3el:
    cpu = "mips32r3";
    break;
  case ArchSpec::eCore_mips32r5:
  case ArchSpec::eCore_mips32r5el:
    cpu = "mips32r5";
    break;
  case ArchSpec::eCore_mips32r6:
  case ArchSpec::eCore_mips32r6el:
    cpu = "mips32r6";
    break;
  case ArchSpec::eCore_mips64:
  case ArchSpec::eCore_mips64el:
    cpu = "mips64";
    break;
  case ArchSpec::eCore_mips64r2:
  case ArchSpec::eCore_mips64r2el:
    cpu = "mips64r2";
    break;
  case ArchSpec::eCore_mips64r3:
  case ArchSpec::eCore_mips64r3el:
    cpu = "mips64r3";
    break;
  case ArchSpec::eCore_mips64r5:
  case ArchSpec::eCore_mips64r5el:
    cpu = "mips64r5";
    break;
  case ArchSpec::eCore_mips64r6:
  case ArchSpec::eCore_mips64r6el:
    cpu = "mips64r6";
    break;
  default:
    cpu = "generic";
    break;
  }

  std::string features;
  uint32_t arch_flags = arch.GetFlags();
  if (arch_flags & ArchSpec::eMIPSAse_msa)
    features += "+msa,";
  if (arch_flags & ArchSpec::eMIPSAse_dsp)
    features += "+dsp,";
  if (arch_flags & ArchSpec::eMIPSAse_dspr2)
    features += "+dspr2,";
  if (arch_flags & ArchSpec::eMIPSAse_mips16)
    features += "+mips16,";
  if (arch_flags & ArchSpec::eMIPSAse_micromips)
    features += "+micromips,";

  m_reg_info.reset(target->createMCRegInfo(triple.getTriple()));
  assert(m_reg_info.get());

  m_insn_info.reset(target->createMCInstrInfo());
  assert(m_insn_info.get());

  llvm::MCTargetOptions MCOptions;
  m_asm_info.reset(
      target->createMCAsmInfo(*m_reg_info, triple.getTriple(), MCOptions));
  m_subtype_info.reset(
      target->createMCSubtargetInfo(triple.getTriple(), cpu, features));
  assert(m_asm_info.get() && m_subtype_info.get());

  m_context = std::make_unique<llvm::MCContext>(
      triple, m_asm_info.get(), m_reg_info.get(), m_subtype_info.get());
  assert(m_context.get());

  m_disasm.reset(target->createMCDisassembler(*m_subtype_info, *m_context));
  assert(m_disasm.get());
}

void EmulateInstructionMIPS64::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void EmulateInstructionMIPS64::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef EmulateInstructionMIPS64::GetPluginDescriptionStatic() {
  return "Emulate instructions for the MIPS64 architecture.";
}

EmulateInstruction *
EmulateInstructionMIPS64::CreateInstance(const ArchSpec &arch,
                                         InstructionType inst_type) {
  if (EmulateInstructionMIPS64::SupportsEmulatingInstructionsOfTypeStatic(
          inst_type)) {
    if (arch.GetTriple().getArch() == llvm::Triple::mips64 ||
        arch.GetTriple().getArch() == llvm::Triple::mips64el) {
      return new EmulateInstructionMIPS64(arch);
    }
  }

  return nullptr;
}

bool EmulateInstructionMIPS64::SetTargetTriple(const ArchSpec &arch) {
  return arch.GetTriple().getArch() == llvm::Triple::mips64 ||
         arch.GetTriple().getArch() == llvm::Triple::mips64el;
}

const char *EmulateInstructionMIPS64::GetRegisterName(unsigned reg_num,
                                                      bool alternate_name) {
  if (alternate_name) {
    switch (reg_num) {
    case dwarf_sp_mips64:
      return "r29";
    case dwarf_r30_mips64:
      return "r30";
    case dwarf_ra_mips64:
      return "r31";
    case dwarf_f0_mips64:
      return "f0";
    case dwarf_f1_mips64:
      return "f1";
    case dwarf_f2_mips64:
      return "f2";
    case dwarf_f3_mips64:
      return "f3";
    case dwarf_f4_mips64:
      return "f4";
    case dwarf_f5_mips64:
      return "f5";
    case dwarf_f6_mips64:
      return "f6";
    case dwarf_f7_mips64:
      return "f7";
    case dwarf_f8_mips64:
      return "f8";
    case dwarf_f9_mips64:
      return "f9";
    case dwarf_f10_mips64:
      return "f10";
    case dwarf_f11_mips64:
      return "f11";
    case dwarf_f12_mips64:
      return "f12";
    case dwarf_f13_mips64:
      return "f13";
    case dwarf_f14_mips64:
      return "f14";
    case dwarf_f15_mips64:
      return "f15";
    case dwarf_f16_mips64:
      return "f16";
    case dwarf_f17_mips64:
      return "f17";
    case dwarf_f18_mips64:
      return "f18";
    case dwarf_f19_mips64:
      return "f19";
    case dwarf_f20_mips64:
      return "f20";
    case dwarf_f21_mips64:
      return "f21";
    case dwarf_f22_mips64:
      return "f22";
    case dwarf_f23_mips64:
      return "f23";
    case dwarf_f24_mips64:
      return "f24";
    case dwarf_f25_mips64:
      return "f25";
    case dwarf_f26_mips64:
      return "f26";
    case dwarf_f27_mips64:
      return "f27";
    case dwarf_f28_mips64:
      return "f28";
    case dwarf_f29_mips64:
      return "f29";
    case dwarf_f30_mips64:
      return "f30";
    case dwarf_f31_mips64:
      return "f31";
    case dwarf_w0_mips64:
      return "w0";
    case dwarf_w1_mips64:
      return "w1";
    case dwarf_w2_mips64:
      return "w2";
    case dwarf_w3_mips64:
      return "w3";
    case dwarf_w4_mips64:
      return "w4";
    case dwarf_w5_mips64:
      return "w5";
    case dwarf_w6_mips64:
      return "w6";
    case dwarf_w7_mips64:
      return "w7";
    case dwarf_w8_mips64:
      return "w8";
    case dwarf_w9_mips64:
      return "w9";
    case dwarf_w10_mips64:
      return "w10";
    case dwarf_w11_mips64:
      return "w11";
    case dwarf_w12_mips64:
      return "w12";
    case dwarf_w13_mips64:
      return "w13";
    case dwarf_w14_mips64:
      return "w14";
    case dwarf_w15_mips64:
      return "w15";
    case dwarf_w16_mips64:
      return "w16";
    case dwarf_w17_mips64:
      return "w17";
    case dwarf_w18_mips64:
      return "w18";
    case dwarf_w19_mips64:
      return "w19";
    case dwarf_w20_mips64:
      return "w20";
    case dwarf_w21_mips64:
      return "w21";
    case dwarf_w22_mips64:
      return "w22";
    case dwarf_w23_mips64:
      return "w23";
    case dwarf_w24_mips64:
      return "w24";
    case dwarf_w25_mips64:
      return "w25";
    case dwarf_w26_mips64:
      return "w26";
    case dwarf_w27_mips64:
      return "w27";
    case dwarf_w28_mips64:
      return "w28";
    case dwarf_w29_mips64:
      return "w29";
    case dwarf_w30_mips64:
      return "w30";
    case dwarf_w31_mips64:
      return "w31";
    case dwarf_mir_mips64:
      return "mir";
    case dwarf_mcsr_mips64:
      return "mcsr";
    case dwarf_config5_mips64:
      return "config5";
    default:
      break;
    }
    return nullptr;
  }

  switch (reg_num) {
  case dwarf_zero_mips64:
    return "r0";
  case dwarf_r1_mips64:
    return "r1";
  case dwarf_r2_mips64:
    return "r2";
  case dwarf_r3_mips64:
    return "r3";
  case dwarf_r4_mips64:
    return "r4";
  case dwarf_r5_mips64:
    return "r5";
  case dwarf_r6_mips64:
    return "r6";
  case dwarf_r7_mips64:
    return "r7";
  case dwarf_r8_mips64:
    return "r8";
  case dwarf_r9_mips64:
    return "r9";
  case dwarf_r10_mips64:
    return "r10";
  case dwarf_r11_mips64:
    return "r11";
  case dwarf_r12_mips64:
    return "r12";
  case dwarf_r13_mips64:
    return "r13";
  case dwarf_r14_mips64:
    return "r14";
  case dwarf_r15_mips64:
    return "r15";
  case dwarf_r16_mips64:
    return "r16";
  case dwarf_r17_mips64:
    return "r17";
  case dwarf_r18_mips64:
    return "r18";
  case dwarf_r19_mips64:
    return "r19";
  case dwarf_r20_mips64:
    return "r20";
  case dwarf_r21_mips64:
    return "r21";
  case dwarf_r22_mips64:
    return "r22";
  case dwarf_r23_mips64:
    return "r23";
  case dwarf_r24_mips64:
    return "r24";
  case dwarf_r25_mips64:
    return "r25";
  case dwarf_r26_mips64:
    return "r26";
  case dwarf_r27_mips64:
    return "r27";
  case dwarf_gp_mips64:
    return "gp";
  case dwarf_sp_mips64:
    return "sp";
  case dwarf_r30_mips64:
    return "fp";
  case dwarf_ra_mips64:
    return "ra";
  case dwarf_sr_mips64:
    return "sr";
  case dwarf_lo_mips64:
    return "lo";
  case dwarf_hi_mips64:
    return "hi";
  case dwarf_bad_mips64:
    return "bad";
  case dwarf_cause_mips64:
    return "cause";
  case dwarf_pc_mips64:
    return "pc";
  case dwarf_f0_mips64:
    return "f0";
  case dwarf_f1_mips64:
    return "f1";
  case dwarf_f2_mips64:
    return "f2";
  case dwarf_f3_mips64:
    return "f3";
  case dwarf_f4_mips64:
    return "f4";
  case dwarf_f5_mips64:
    return "f5";
  case dwarf_f6_mips64:
    return "f6";
  case dwarf_f7_mips64:
    return "f7";
  case dwarf_f8_mips64:
    return "f8";
  case dwarf_f9_mips64:
    return "f9";
  case dwarf_f10_mips64:
    return "f10";
  case dwarf_f11_mips64:
    return "f11";
  case dwarf_f12_mips64:
    return "f12";
  case dwarf_f13_mips64:
    return "f13";
  case dwarf_f14_mips64:
    return "f14";
  case dwarf_f15_mips64:
    return "f15";
  case dwarf_f16_mips64:
    return "f16";
  case dwarf_f17_mips64:
    return "f17";
  case dwarf_f18_mips64:
    return "f18";
  case dwarf_f19_mips64:
    return "f19";
  case dwarf_f20_mips64:
    return "f20";
  case dwarf_f21_mips64:
    return "f21";
  case dwarf_f22_mips64:
    return "f22";
  case dwarf_f23_mips64:
    return "f23";
  case dwarf_f24_mips64:
    return "f24";
  case dwarf_f25_mips64:
    return "f25";
  case dwarf_f26_mips64:
    return "f26";
  case dwarf_f27_mips64:
    return "f27";
  case dwarf_f28_mips64:
    return "f28";
  case dwarf_f29_mips64:
    return "f29";
  case dwarf_f30_mips64:
    return "f30";
  case dwarf_f31_mips64:
    return "f31";
  case dwarf_fcsr_mips64:
    return "fcsr";
  case dwarf_fir_mips64:
    return "fir";
  case dwarf_w0_mips64:
    return "w0";
  case dwarf_w1_mips64:
    return "w1";
  case dwarf_w2_mips64:
    return "w2";
  case dwarf_w3_mips64:
    return "w3";
  case dwarf_w4_mips64:
    return "w4";
  case dwarf_w5_mips64:
    return "w5";
  case dwarf_w6_mips64:
    return "w6";
  case dwarf_w7_mips64:
    return "w7";
  case dwarf_w8_mips64:
    return "w8";
  case dwarf_w9_mips64:
    return "w9";
  case dwarf_w10_mips64:
    return "w10";
  case dwarf_w11_mips64:
    return "w11";
  case dwarf_w12_mips64:
    return "w12";
  case dwarf_w13_mips64:
    return "w13";
  case dwarf_w14_mips64:
    return "w14";
  case dwarf_w15_mips64:
    return "w15";
  case dwarf_w16_mips64:
    return "w16";
  case dwarf_w17_mips64:
    return "w17";
  case dwarf_w18_mips64:
    return "w18";
  case dwarf_w19_mips64:
    return "w19";
  case dwarf_w20_mips64:
    return "w20";
  case dwarf_w21_mips64:
    return "w21";
  case dwarf_w22_mips64:
    return "w22";
  case dwarf_w23_mips64:
    return "w23";
  case dwarf_w24_mips64:
    return "w24";
  case dwarf_w25_mips64:
    return "w25";
  case dwarf_w26_mips64:
    return "w26";
  case dwarf_w27_mips64:
    return "w27";
  case dwarf_w28_mips64:
    return "w28";
  case dwarf_w29_mips64:
    return "w29";
  case dwarf_w30_mips64:
    return "w30";
  case dwarf_w31_mips64:
    return "w31";
  case dwarf_mcsr_mips64:
    return "mcsr";
  case dwarf_mir_mips64:
    return "mir";
  case dwarf_config5_mips64:
    return "config5";
  }
  return nullptr;
}

std::optional<RegisterInfo>
EmulateInstructionMIPS64::GetRegisterInfo(RegisterKind reg_kind,
                                          uint32_t reg_num) {
  if (reg_kind == eRegisterKindGeneric) {
    switch (reg_num) {
    case LLDB_REGNUM_GENERIC_PC:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_pc_mips64;
      break;
    case LLDB_REGNUM_GENERIC_SP:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_sp_mips64;
      break;
    case LLDB_REGNUM_GENERIC_FP:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_r30_mips64;
      break;
    case LLDB_REGNUM_GENERIC_RA:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_ra_mips64;
      break;
    case LLDB_REGNUM_GENERIC_FLAGS:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_sr_mips64;
      break;
    default:
      return {};
    }
  }

  if (reg_kind == eRegisterKindDWARF) {
    RegisterInfo reg_info;
    ::memset(&reg_info, 0, sizeof(RegisterInfo));
    ::memset(reg_info.kinds, LLDB_INVALID_REGNUM, sizeof(reg_info.kinds));

    if (reg_num == dwarf_sr_mips64 || reg_num == dwarf_fcsr_mips64 ||
        reg_num == dwarf_fir_mips64 || reg_num == dwarf_mcsr_mips64 ||
        reg_num == dwarf_mir_mips64 || reg_num == dwarf_config5_mips64) {
      reg_info.byte_size = 4;
      reg_info.format = eFormatHex;
      reg_info.encoding = eEncodingUint;
    } else if ((int)reg_num >= dwarf_zero_mips64 &&
               (int)reg_num <= dwarf_f31_mips64) {
      reg_info.byte_size = 8;
      reg_info.format = eFormatHex;
      reg_info.encoding = eEncodingUint;
    } else if ((int)reg_num >= dwarf_w0_mips64 &&
               (int)reg_num <= dwarf_w31_mips64) {
      reg_info.byte_size = 16;
      reg_info.format = eFormatVectorOfUInt8;
      reg_info.encoding = eEncodingVector;
    } else {
      return {};
    }

    reg_info.name = GetRegisterName(reg_num, false);
    reg_info.alt_name = GetRegisterName(reg_num, true);
    reg_info.kinds[eRegisterKindDWARF] = reg_num;

    switch (reg_num) {
    case dwarf_r30_mips64:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
      break;
    case dwarf_ra_mips64:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_RA;
      break;
    case dwarf_sp_mips64:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
      break;
    case dwarf_pc_mips64:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
      break;
    case dwarf_sr_mips64:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      break;
    default:
      break;
    }
    return reg_info;
  }
  return {};
}

EmulateInstructionMIPS64::MipsOpcode *
EmulateInstructionMIPS64::GetOpcodeForInstruction(llvm::StringRef op_name) {
  static EmulateInstructionMIPS64::MipsOpcode g_opcodes[] = {
      // Prologue/Epilogue instructions
      {"DADDiu", &EmulateInstructionMIPS64::Emulate_DADDiu,
       "DADDIU rt, rs, immediate"},
      {"ADDiu", &EmulateInstructionMIPS64::Emulate_DADDiu,
       "ADDIU  rt, rs, immediate"},
      {"SD", &EmulateInstructionMIPS64::Emulate_SD, "SD     rt, offset(rs)"},
      {"LD", &EmulateInstructionMIPS64::Emulate_LD, "LD     rt, offset(base)"},
      {"DSUBU", &EmulateInstructionMIPS64::Emulate_DSUBU_DADDU,
       "DSUBU  rd, rs, rt"},
      {"SUBU", &EmulateInstructionMIPS64::Emulate_DSUBU_DADDU,
       "SUBU   rd, rs, rt"},
      {"DADDU", &EmulateInstructionMIPS64::Emulate_DSUBU_DADDU,
       "DADDU  rd, rs, rt"},
      {"ADDU", &EmulateInstructionMIPS64::Emulate_DSUBU_DADDU,
       "ADDU   rd, rs, rt"},
      {"LUI", &EmulateInstructionMIPS64::Emulate_LUI, "LUI    rt, immediate"},

      // Load/Store  instructions
      /* Following list of emulated instructions are required by implementation
         of hardware watchpoint
         for MIPS in lldb. As we just need the address accessed by instructions,
         we have generalised
         all these instructions in 2 functions depending on their addressing
         modes */

      {"LB", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LB    rt, offset(base)"},
      {"LBE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LBE   rt, offset(base)"},
      {"LBU", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LBU   rt, offset(base)"},
      {"LBUE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LBUE  rt, offset(base)"},
      {"LDC1", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LDC1  ft, offset(base)"},
      {"LDL", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LDL   rt, offset(base)"},
      {"LDR", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LDR   rt, offset(base)"},
      {"LLD", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LLD   rt, offset(base)"},
      {"LDC2", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LDC2  rt, offset(base)"},
      {"LDXC1", &EmulateInstructionMIPS64::Emulate_LDST_Reg,
       "LDXC1 fd, index (base)"},
      {"LH", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LH    rt, offset(base)"},
      {"LHE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LHE   rt, offset(base)"},
      {"LHU", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LHU   rt, offset(base)"},
      {"LHUE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LHUE  rt, offset(base)"},
      {"LL", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LL    rt, offset(base)"},
      {"LLE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LLE   rt, offset(base)"},
      {"LUXC1", &EmulateInstructionMIPS64::Emulate_LDST_Reg,
       "LUXC1 fd, index (base)"},
      {"LW", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LW    rt, offset(rs)"},
      {"LWC1", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LWC1  ft, offset(base)"},
      {"LWC2", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LWC2  rt, offset(base)"},
      {"LWE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LWE   rt, offset(base)"},
      {"LWL", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LWL   rt, offset(base)"},
      {"LWLE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LWLE  rt, offset(base)"},
      {"LWR", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LWR   rt, offset(base)"},
      {"LWRE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "LWRE  rt, offset(base)"},
      {"LWXC1", &EmulateInstructionMIPS64::Emulate_LDST_Reg,
       "LWXC1 fd, index (base)"},

      {"SB", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SB    rt, offset(base)"},
      {"SBE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SBE   rt, offset(base)"},
      {"SC", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SC    rt, offset(base)"},
      {"SCE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SCE   rt, offset(base)"},
      {"SCD", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SCD   rt, offset(base)"},
      {"SDL", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SDL   rt, offset(base)"},
      {"SDR", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SDR   rt, offset(base)"},
      {"SDC1", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SDC1  ft, offset(base)"},
      {"SDC2", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SDC2  rt, offset(base)"},
      {"SDXC1", &EmulateInstructionMIPS64::Emulate_LDST_Reg,
       "SDXC1 fs, index (base)"},
      {"SH", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SH    rt, offset(base)"},
      {"SHE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SHE   rt, offset(base)"},
      {"SUXC1", &EmulateInstructionMIPS64::Emulate_LDST_Reg,
       "SUXC1 fs, index (base)"},
      {"SW", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SW    rt, offset(rs)"},
      {"SWC1", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SWC1  ft, offset(base)"},
      {"SWC2", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SWC2  rt, offset(base)"},
      {"SWE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SWE   rt, offset(base)"},
      {"SWL", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SWL   rt, offset(base)"},
      {"SWLE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SWLE  rt, offset(base)"},
      {"SWR", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SWR   rt, offset(base)"},
      {"SWRE", &EmulateInstructionMIPS64::Emulate_LDST_Imm,
       "SWRE  rt, offset(base)"},
      {"SWXC1", &EmulateInstructionMIPS64::Emulate_LDST_Reg,
       "SWXC1 fs, index (base)"},

      // Branch instructions
      {"BEQ", &EmulateInstructionMIPS64::Emulate_BXX_3ops, "BEQ rs,rt,offset"},
      {"BEQ64", &EmulateInstructionMIPS64::Emulate_BXX_3ops, "BEQ rs,rt,offset"},
      {"BNE", &EmulateInstructionMIPS64::Emulate_BXX_3ops, "BNE rs,rt,offset"},
      {"BNE64", &EmulateInstructionMIPS64::Emulate_BXX_3ops, "BNE rs,rt,offset"},
      {"BEQL", &EmulateInstructionMIPS64::Emulate_BXX_3ops,
       "BEQL rs,rt,offset"},
      {"BNEL", &EmulateInstructionMIPS64::Emulate_BXX_3ops,
       "BNEL rs,rt,offset"},
      {"BGEZALL", &EmulateInstructionMIPS64::Emulate_Bcond_Link,
       "BGEZALL rt,offset"},
      {"BAL", &EmulateInstructionMIPS64::Emulate_BAL, "BAL offset"},
      {"BGEZAL", &EmulateInstructionMIPS64::Emulate_Bcond_Link,
       "BGEZAL rs,offset"},
      {"BALC", &EmulateInstructionMIPS64::Emulate_BALC, "BALC offset"},
      {"BC", &EmulateInstructionMIPS64::Emulate_BC, "BC offset"},
      {"BGEZ", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BGEZ rs,offset"},
      {"BGEZ64", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BGEZ rs,offset"},
      {"BLEZALC", &EmulateInstructionMIPS64::Emulate_Bcond_Link_C,
       "BLEZALC rs,offset"},
      {"BGEZALC", &EmulateInstructionMIPS64::Emulate_Bcond_Link_C,
       "BGEZALC rs,offset"},
      {"BLTZALC", &EmulateInstructionMIPS64::Emulate_Bcond_Link_C,
       "BLTZALC rs,offset"},
      {"BGTZALC", &EmulateInstructionMIPS64::Emulate_Bcond_Link_C,
       "BGTZALC rs,offset"},
      {"BEQZALC", &EmulateInstructionMIPS64::Emulate_Bcond_Link_C,
       "BEQZALC rs,offset"},
      {"BNEZALC", &EmulateInstructionMIPS64::Emulate_Bcond_Link_C,
       "BNEZALC rs,offset"},
      {"BEQC", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BEQC rs,rt,offset"},
      {"BEQC64", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BEQC rs,rt,offset"},
      {"BNEC", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BNEC rs,rt,offset"},
      {"BNEC64", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BNEC rs,rt,offset"},
      {"BLTC", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BLTC rs,rt,offset"},
      {"BLTC64", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BLTC rs,rt,offset"},
      {"BGEC", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BGEC rs,rt,offset"},
      {"BGEC64", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BGEC rs,rt,offset"},
      {"BLTUC", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BLTUC rs,rt,offset"},
      {"BLTUC64", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BLTUC rs,rt,offset"},
      {"BGEUC", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BGEUC rs,rt,offset"},
      {"BGEUC64", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BGEUC rs,rt,offset"},
      {"BLTZC", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BLTZC rt,offset"},
      {"BLTZC64", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BLTZC rt,offset"},
      {"BLEZC", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BLEZC rt,offset"},
      {"BLEZC64", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BLEZC rt,offset"},
      {"BGEZC", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BGEZC rt,offset"},
      {"BGEZC64", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BGEZC rt,offset"},
      {"BGTZC", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BGTZC rt,offset"},
      {"BGTZC64", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BGTZC rt,offset"},
      {"BEQZC", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BEQZC rt,offset"},
      {"BEQZC64", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BEQZC rt,offset"},
      {"BNEZC", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BNEZC rt,offset"},
      {"BNEZC64", &EmulateInstructionMIPS64::Emulate_BXX_2ops_C,
       "BNEZC rt,offset"},
      {"BGEZL", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BGEZL rt,offset"},
      {"BGTZ", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BGTZ rt,offset"},
      {"BGTZ64", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BGTZ rt,offset"},
      {"BGTZL", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BGTZL rt,offset"},
      {"BLEZ", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BLEZ rt,offset"},
      {"BLEZ64", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BLEZ rt,offset"},
      {"BLEZL", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BLEZL rt,offset"},
      {"BLTZ", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BLTZ rt,offset"},
      {"BLTZ64", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BLTZ rt,offset"},
      {"BLTZAL", &EmulateInstructionMIPS64::Emulate_Bcond_Link,
       "BLTZAL rt,offset"},
      {"BLTZALL", &EmulateInstructionMIPS64::Emulate_Bcond_Link,
       "BLTZALL rt,offset"},
      {"BLTZL", &EmulateInstructionMIPS64::Emulate_BXX_2ops, "BLTZL rt,offset"},
      {"BOVC", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BOVC rs,rt,offset"},
      {"BNVC", &EmulateInstructionMIPS64::Emulate_BXX_3ops_C,
       "BNVC rs,rt,offset"},
      {"J", &EmulateInstructionMIPS64::Emulate_J, "J target"},
      {"JAL", &EmulateInstructionMIPS64::Emulate_JAL, "JAL target"},
      {"JALX", &EmulateInstructionMIPS64::Emulate_JAL, "JALX target"},
      {"JALR", &EmulateInstructionMIPS64::Emulate_JALR, "JALR target"},
      {"JALR64", &EmulateInstructionMIPS64::Emulate_JALR, "JALR target"},
      {"JALR_HB", &EmulateInstructionMIPS64::Emulate_JALR, "JALR.HB target"},
      {"JIALC", &EmulateInstructionMIPS64::Emulate_JIALC, "JIALC rt,offset"},
      {"JIALC64", &EmulateInstructionMIPS64::Emulate_JIALC, "JIALC rt,offset"},
      {"JIC", &EmulateInstructionMIPS64::Emulate_JIC, "JIC rt,offset"},
      {"JIC64", &EmulateInstructionMIPS64::Emulate_JIC, "JIC rt,offset"},
      {"JR", &EmulateInstructionMIPS64::Emulate_JR, "JR target"},
      {"JR64", &EmulateInstructionMIPS64::Emulate_JR, "JR target"},
      {"JR_HB", &EmulateInstructionMIPS64::Emulate_JR, "JR.HB target"},
      {"BC1F", &EmulateInstructionMIPS64::Emulate_FP_branch, "BC1F cc, offset"},
      {"BC1T", &EmulateInstructionMIPS64::Emulate_FP_branch, "BC1T cc, offset"},
      {"BC1FL", &EmulateInstructionMIPS64::Emulate_FP_branch,
       "BC1FL cc, offset"},
      {"BC1TL", &EmulateInstructionMIPS64::Emulate_FP_branch,
       "BC1TL cc, offset"},
      {"BC1EQZ", &EmulateInstructionMIPS64::Emulate_BC1EQZ,
       "BC1EQZ ft, offset"},
      {"BC1NEZ", &EmulateInstructionMIPS64::Emulate_BC1NEZ,
       "BC1NEZ ft, offset"},
      {"BC1ANY2F", &EmulateInstructionMIPS64::Emulate_3D_branch,
       "BC1ANY2F cc, offset"},
      {"BC1ANY2T", &EmulateInstructionMIPS64::Emulate_3D_branch,
       "BC1ANY2T cc, offset"},
      {"BC1ANY4F", &EmulateInstructionMIPS64::Emulate_3D_branch,
       "BC1ANY4F cc, offset"},
      {"BC1ANY4T", &EmulateInstructionMIPS64::Emulate_3D_branch,
       "BC1ANY4T cc, offset"},
      {"BNZ_B", &EmulateInstructionMIPS64::Emulate_BNZB, "BNZ.b wt,s16"},
      {"BNZ_H", &EmulateInstructionMIPS64::Emulate_BNZH, "BNZ.h wt,s16"},
      {"BNZ_W", &EmulateInstructionMIPS64::Emulate_BNZW, "BNZ.w wt,s16"},
      {"BNZ_D", &EmulateInstructionMIPS64::Emulate_BNZD, "BNZ.d wt,s16"},
      {"BZ_B", &EmulateInstructionMIPS64::Emulate_BZB, "BZ.b wt,s16"},
      {"BZ_H", &EmulateInstructionMIPS64::Emulate_BZH, "BZ.h wt,s16"},
      {"BZ_W", &EmulateInstructionMIPS64::Emulate_BZW, "BZ.w wt,s16"},
      {"BZ_D", &EmulateInstructionMIPS64::Emulate_BZD, "BZ.d wt,s16"},
      {"BNZ_V", &EmulateInstructionMIPS64::Emulate_BNZV, "BNZ.V wt,s16"},
      {"BZ_V", &EmulateInstructionMIPS64::Emulate_BZV, "BZ.V wt,s16"},
  };

  for (MipsOpcode &opcode : g_opcodes) {
    if (op_name.equals_insensitive(opcode.op_name))
      return &opcode;
  }
  return nullptr;
}

bool EmulateInstructionMIPS64::ReadInstruction() {
  bool success = false;
  m_addr = ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC,
                                LLDB_INVALID_ADDRESS, &success);
  if (success) {
    Context read_inst_context;
    read_inst_context.type = eContextReadOpcode;
    read_inst_context.SetNoArgs();
    m_opcode.SetOpcode32(
        ReadMemoryUnsigned(read_inst_context, m_addr, 4, 0, &success),
        GetByteOrder());
  }
  if (!success)
    m_addr = LLDB_INVALID_ADDRESS;
  return success;
}

bool EmulateInstructionMIPS64::EvaluateInstruction(uint32_t evaluate_options) {
  bool success = false;
  llvm::MCInst mc_insn;
  uint64_t insn_size;
  DataExtractor data;

  /* Keep the complexity of the decode logic with the llvm::MCDisassembler
   * class. */
  if (m_opcode.GetData(data)) {
    llvm::MCDisassembler::DecodeStatus decode_status;
    llvm::ArrayRef<uint8_t> raw_insn(data.GetDataStart(), data.GetByteSize());
    decode_status = m_disasm->getInstruction(mc_insn, insn_size, raw_insn,
                                             m_addr, llvm::nulls());
    if (decode_status != llvm::MCDisassembler::Success)
      return false;
  }

  /*
   * mc_insn.getOpcode() returns decoded opcode. However to make use
   * of llvm::Mips::<insn> we would need "MipsGenInstrInfo.inc".
  */
  llvm::StringRef op_name = m_insn_info->getName(mc_insn.getOpcode());

  /*
   * Decoding has been done already. Just get the call-back function
   * and emulate the instruction.
  */
  MipsOpcode *opcode_data = GetOpcodeForInstruction(op_name);

  if (opcode_data == nullptr)
    return false;

  uint64_t old_pc = 0, new_pc = 0;
  const bool auto_advance_pc =
      evaluate_options & eEmulateInstructionOptionAutoAdvancePC;

  if (auto_advance_pc) {
    old_pc =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
    if (!success)
      return false;
  }

  /* emulate instruction */
  success = (this->*opcode_data->callback)(mc_insn);
  if (!success)
    return false;

  if (auto_advance_pc) {
    new_pc =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
    if (!success)
      return false;

    /* If we haven't changed the PC, change it here */
    if (old_pc == new_pc) {
      new_pc += 4;
      Context context;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                                 new_pc))
        return false;
    }
  }

  return true;
}

bool EmulateInstructionMIPS64::CreateFunctionEntryUnwind(
    UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  const bool can_replace = false;

  // Our previous Call Frame Address is the stack pointer
  row->GetCFAValue().SetIsRegisterPlusOffset(dwarf_sp_mips64, 0);

  // Our previous PC is in the RA
  row->SetRegisterLocationToRegister(dwarf_pc_mips64, dwarf_ra_mips64,
                                     can_replace);

  unwind_plan.AppendRow(row);

  // All other registers are the same.
  unwind_plan.SetSourceName("EmulateInstructionMIPS64");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  unwind_plan.SetUnwindPlanForSignalTrap(eLazyBoolNo);
  unwind_plan.SetReturnAddressRegister(dwarf_ra_mips64);

  return true;
}

bool EmulateInstructionMIPS64::nonvolatile_reg_p(uint64_t regnum) {
  switch (regnum) {
  case dwarf_r16_mips64:
  case dwarf_r17_mips64:
  case dwarf_r18_mips64:
  case dwarf_r19_mips64:
  case dwarf_r20_mips64:
  case dwarf_r21_mips64:
  case dwarf_r22_mips64:
  case dwarf_r23_mips64:
  case dwarf_gp_mips64:
  case dwarf_sp_mips64:
  case dwarf_r30_mips64:
  case dwarf_ra_mips64:
    return true;
  default:
    return false;
  }
  return false;
}

bool EmulateInstructionMIPS64::Emulate_DADDiu(llvm::MCInst &insn) {
  // DADDIU rt, rs, immediate
  // GPR[rt] <- GPR[rs] + sign_extend(immediate)

  uint8_t dst, src;
  bool success = false;
  const uint32_t imm16 = insn.getOperand(2).getImm();
  int64_t imm = SignedBits(imm16, 15, 0);

  dst = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  src = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  // If immediate is greater than 2^16 - 1 then clang generate LUI,
  // (D)ADDIU,(D)SUBU instructions in prolog. Example lui    $1, 0x2 daddiu $1,
  // $1, -0x5920 dsubu  $sp, $sp, $1 In this case, (D)ADDIU dst and src will be
  // same and not equal to sp
  if (dst == src) {
    Context context;

    /* read <src> register */
    const uint64_t src_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips64 + src, 0, &success);
    if (!success)
      return false;

    /* Check if this is daddiu sp, sp, imm16 */
    if (dst == dwarf_sp_mips64) {
      /*
       * From the MIPS IV spec:
       *
       * The term “unsigned” in the instruction name is a misnomer; this
       * operation is 64-bit modulo arithmetic that does not trap on overflow.
       * It is appropriate for arithmetic which is not signed, such as address
       * arithmetic, or integer arithmetic environments that ignore overflow,
       * such as “C” language arithmetic.
       *
       * Assume 2's complement and rely on unsigned overflow here.
       */
      uint64_t result = src_opd_val + imm;
      std::optional<RegisterInfo> reg_info_sp =
          GetRegisterInfo(eRegisterKindDWARF, dwarf_sp_mips64);
      if (reg_info_sp)
        context.SetRegisterPlusOffset(*reg_info_sp, imm);

      /* We are allocating bytes on stack */
      context.type = eContextAdjustStackPointer;

      WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_sp_mips64,
                            result);
      return true;
    }

    imm += src_opd_val;
    context.SetImmediateSigned(imm);
    context.type = eContextImmediate;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF,
                               dwarf_zero_mips64 + dst, imm))
      return false;
  }

  return true;
}

bool EmulateInstructionMIPS64::Emulate_SD(llvm::MCInst &insn) {
  uint64_t address;
  bool success = false;
  uint32_t imm16 = insn.getOperand(2).getImm();
  uint64_t imm = SignedBits(imm16, 15, 0);
  uint32_t src, base;
  Context bad_vaddr_context;

  src = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  base = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  std::optional<RegisterInfo> reg_info_base =
      GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips64 + base);
  std::optional<RegisterInfo> reg_info_src =
      GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips64 + src);
  if (!reg_info_base || !reg_info_src)
    return false;

  /* read SP */
  address = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips64 + base,
                                 0, &success);
  if (!success)
    return false;

  /* destination address */
  address = address + imm;

  /* We look for sp based non-volatile register stores */
  if (nonvolatile_reg_p(src)) {
    Context context;
    context.type = eContextPushRegisterOnStack;
    context.SetRegisterToRegisterPlusOffset(*reg_info_src, *reg_info_base, 0);

    std::optional<RegisterValue> data_src = ReadRegister(*reg_info_base);
    if (!data_src)
      return false;

    Status error;
    RegisterValue::BytesContainer buffer(reg_info_src->byte_size);
    if (data_src->GetAsMemoryData(*reg_info_src, buffer.data(),
                                  reg_info_src->byte_size, eByteOrderLittle,
                                  error) == 0)
      return false;

    if (!WriteMemory(context, address, buffer.data(), reg_info_src->byte_size))
      return false;
  }

  /* Set the bad_vaddr register with base address used in the instruction */
  bad_vaddr_context.type = eContextInvalid;
  WriteRegisterUnsigned(bad_vaddr_context, eRegisterKindDWARF, dwarf_bad_mips64,
                        address);

  return true;
}

bool EmulateInstructionMIPS64::Emulate_LD(llvm::MCInst &insn) {
  bool success = false;
  uint32_t src, base;
  int64_t imm, address;
  Context bad_vaddr_context;

  src = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  base = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());
  imm = insn.getOperand(2).getImm();

  if (!GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips64 + base))
    return false;

  /* read base register */
  address = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips64 + base,
                                 0, &success);
  if (!success)
    return false;

  /* destination address */
  address = address + imm;

  /* Set the bad_vaddr register with base address used in the instruction */
  bad_vaddr_context.type = eContextInvalid;
  WriteRegisterUnsigned(bad_vaddr_context, eRegisterKindDWARF, dwarf_bad_mips64,
                        address);

  if (nonvolatile_reg_p(src)) {
    RegisterValue data_src;
    std::optional<RegisterInfo> reg_info_src =
        GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips64 + src);
    if (!reg_info_src)
      return false;

    Context context;
    context.type = eContextRegisterLoad;

    return WriteRegister(context, *reg_info_src, data_src);
  }

  return false;
}

bool EmulateInstructionMIPS64::Emulate_LUI(llvm::MCInst &insn) {
  // LUI rt, immediate
  // GPR[rt] <- sign_extend(immediate << 16)

  const uint32_t imm32 = insn.getOperand(1).getImm() << 16;
  int64_t imm = SignedBits(imm32, 31, 0);
  uint8_t rt;
  Context context;

  rt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  context.SetImmediateSigned(imm);
  context.type = eContextImmediate;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF,
                               dwarf_zero_mips64 + rt, imm);
}

bool EmulateInstructionMIPS64::Emulate_DSUBU_DADDU(llvm::MCInst &insn) {
  // DSUBU sp, <src>, <rt>
  // DADDU sp, <src>, <rt>
  // DADDU dst, sp, <rt>

  bool success = false;
  uint64_t result;
  uint8_t src, dst, rt;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  dst = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  src = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  /* Check if sp is destination register */
  if (dst == dwarf_sp_mips64) {
    rt = m_reg_info->getEncodingValue(insn.getOperand(2).getReg());

    /* read <src> register */
    uint64_t src_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips64 + src, 0, &success);
    if (!success)
      return false;

    /* read <rt > register */
    uint64_t rt_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips64 + rt, 0, &success);
    if (!success)
      return false;

    if (op_name.equals_insensitive("DSUBU") ||
        op_name.equals_insensitive("SUBU"))
      result = src_opd_val - rt_opd_val;
    else
      result = src_opd_val + rt_opd_val;

    Context context;
    std::optional<RegisterInfo> reg_info_sp =
        GetRegisterInfo(eRegisterKindDWARF, dwarf_sp_mips64);
    if (reg_info_sp)
      context.SetRegisterPlusOffset(*reg_info_sp, rt_opd_val);

    /* We are allocating bytes on stack */
    context.type = eContextAdjustStackPointer;

    WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_sp_mips64, result);

    return true;
  } else if (src == dwarf_sp_mips64) {
    rt = m_reg_info->getEncodingValue(insn.getOperand(2).getReg());

    /* read <src> register */
    uint64_t src_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips64 + src, 0, &success);
    if (!success)
      return false;

    /* read <rt> register */
    uint64_t rt_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips64 + rt, 0, &success);
    if (!success)
      return false;

    Context context;

    if (op_name.equals_insensitive("DSUBU") ||
        op_name.equals_insensitive("SUBU"))
      result = src_opd_val - rt_opd_val;
    else
      result = src_opd_val + rt_opd_val;

    context.SetImmediateSigned(result);
    context.type = eContextImmediate;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF,
                               dwarf_zero_mips64 + dst, result))
      return false;
  }

  return true;
}

/*
    Emulate below MIPS branch instructions.
    BEQ, BNE : Branch on condition
    BEQL, BNEL : Branch likely
*/
bool EmulateInstructionMIPS64::Emulate_BXX_3ops(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs, rt;
  int64_t offset, pc, rs_val, rt_val, target = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  rt = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());
  offset = insn.getOperand(2).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  rs_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rs, 0, &success);
  if (!success)
    return false;

  rt_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rt, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BEQ") || op_name.equals_insensitive("BEQL") ||
      op_name.equals_insensitive("BEQ64")) {
    if (rs_val == rt_val)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BNE") ||
             op_name.equals_insensitive("BNEL") ||
             op_name.equals_insensitive("BNE64")) {
    if (rs_val != rt_val)
      target = pc + offset;
    else
      target = pc + 8;
  }

  Context context;
  context.type = eContextRelativeBranchImmediate;
  context.SetImmediate(offset);

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

/*
    Emulate below MIPS Non-Compact conditional branch and link instructions.
    BLTZAL, BGEZAL      :
    BLTZALL, BGEZALL    : Branch likely
*/
bool EmulateInstructionMIPS64::Emulate_Bcond_Link(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  int64_t offset, pc, target = 0;
  int64_t rs_val;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  rs_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rs, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BLTZAL") ||
      op_name.equals_insensitive("BLTZALL")) {
    if (rs_val < 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BGEZAL") ||
             op_name.equals_insensitive("BGEZALL")) {
    if (rs_val >= 0)
      target = pc + offset;
    else
      target = pc + 8;
  }

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips64,
                             pc + 8))
    return false;

  return true;
}

bool EmulateInstructionMIPS64::Emulate_BAL(llvm::MCInst &insn) {
  bool success = false;
  int64_t offset, pc, target;

  /*
   * BAL offset
   *      offset = sign_ext (offset << 2)
   *      RA = PC + 8
   *      PC = PC + offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  target = pc + offset;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips64,
                             pc + 8))
    return false;

  return true;
}

bool EmulateInstructionMIPS64::Emulate_BALC(llvm::MCInst &insn) {
  bool success = false;
  int64_t offset, pc, target;

  /*
   * BALC offset
   *      offset = sign_ext (offset << 2)
   *      RA = PC + 4
   *      PC = PC + 4 + offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  target = pc + offset;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips64,
                             pc + 4))
    return false;

  return true;
}

/*
    Emulate below MIPS conditional branch and link instructions.
    BLEZALC, BGEZALC, BLTZALC, BGTZALC, BEQZALC, BNEZALC : Compact branches
*/
bool EmulateInstructionMIPS64::Emulate_Bcond_Link_C(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  int64_t offset, pc, rs_val, target = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  rs_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rs, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BLEZALC")) {
    if (rs_val <= 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGEZALC")) {
    if (rs_val >= 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BLTZALC")) {
    if (rs_val < 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGTZALC")) {
    if (rs_val > 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BEQZALC")) {
    if (rs_val == 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BNEZALC")) {
    if (rs_val != 0)
      target = pc + offset;
    else
      target = pc + 4;
  }

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips64,
                             pc + 4))
    return false;

  return true;
}

/*
    Emulate below MIPS branch instructions.
    BLTZL, BGEZL, BGTZL, BLEZL : Branch likely
    BLTZ, BGEZ, BGTZ, BLEZ     : Non-compact branches
*/
bool EmulateInstructionMIPS64::Emulate_BXX_2ops(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  int64_t offset, pc, rs_val, target = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  rs_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rs, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BLTZL") ||
      op_name.equals_insensitive("BLTZ") ||
      op_name.equals_insensitive("BLTZ64")) {
    if (rs_val < 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BGEZL") ||
             op_name.equals_insensitive("BGEZ") ||
             op_name.equals_insensitive("BGEZ64")) {
    if (rs_val >= 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BGTZL") ||
             op_name.equals_insensitive("BGTZ") ||
             op_name.equals_insensitive("BGTZ64")) {
    if (rs_val > 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BLEZL") ||
             op_name.equals_insensitive("BLEZ") ||
             op_name.equals_insensitive("BLEZ64")) {
    if (rs_val <= 0)
      target = pc + offset;
    else
      target = pc + 8;
  }

  Context context;
  context.type = eContextRelativeBranchImmediate;
  context.SetImmediate(offset);

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

bool EmulateInstructionMIPS64::Emulate_BC(llvm::MCInst &insn) {
  bool success = false;
  int64_t offset, pc, target;

  /*
   * BC offset
   *      offset = sign_ext (offset << 2)
   *      PC = PC + 4 + offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  target = pc + offset;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

static int IsAdd64bitOverflow(int64_t a, int64_t b) {
  int64_t r = (uint64_t)a + (uint64_t)b;
  return (a < 0 && b < 0 && r >= 0) || (a >= 0 && b >= 0 && r < 0);
}

/*
    Emulate below MIPS branch instructions.
    BEQC, BNEC, BLTC, BGEC, BLTUC, BGEUC, BOVC, BNVC: Compact branch
   instructions with no delay slot
*/
bool EmulateInstructionMIPS64::Emulate_BXX_3ops_C(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs, rt;
  int64_t offset, pc, rs_val, rt_val, target = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());
  uint32_t current_inst_size = m_insn_info->get(insn.getOpcode()).getSize();

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  rt = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());
  offset = insn.getOperand(2).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  rs_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rs, 0, &success);
  if (!success)
    return false;

  rt_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rt, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BEQC") ||
      op_name.equals_insensitive("BEQC64")) {
    if (rs_val == rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BNEC") ||
             op_name.equals_insensitive("BNEC64")) {
    if (rs_val != rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BLTC") ||
             op_name.equals_insensitive("BLTC64")) {
    if (rs_val < rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGEC64") ||
             op_name.equals_insensitive("BGEC")) {
    if (rs_val >= rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BLTUC") ||
             op_name.equals_insensitive("BLTUC64")) {
    if (rs_val < rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGEUC") ||
             op_name.equals_insensitive("BGEUC64")) {
    if ((uint32_t)rs_val >= (uint32_t)rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BOVC")) {
    if (IsAdd64bitOverflow(rs_val, rt_val))
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BNVC")) {
    if (!IsAdd64bitOverflow(rs_val, rt_val))
      target = pc + offset;
    else
      target = pc + 4;
  }

  Context context;
  context.type = eContextRelativeBranchImmediate;
  context.SetImmediate(current_inst_size + offset);

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

/*
    Emulate below MIPS branch instructions.
    BLTZC, BLEZC, BGEZC, BGTZC, BEQZC, BNEZC : Compact Branches
*/
bool EmulateInstructionMIPS64::Emulate_BXX_2ops_C(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  int64_t offset, pc, target = 0;
  int64_t rs_val;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());
  uint32_t current_inst_size = m_insn_info->get(insn.getOpcode()).getSize();

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  rs_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rs, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BLTZC") ||
      op_name.equals_insensitive("BLTZC64")) {
    if (rs_val < 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BLEZC") ||
             op_name.equals_insensitive("BLEZC64")) {
    if (rs_val <= 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGEZC") ||
             op_name.equals_insensitive("BGEZC64")) {
    if (rs_val >= 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGTZC") ||
             op_name.equals_insensitive("BGTZC64")) {
    if (rs_val > 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BEQZC") ||
             op_name.equals_insensitive("BEQZC64")) {
    if (rs_val == 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BNEZC") ||
             op_name.equals_insensitive("BNEZC64")) {
    if (rs_val != 0)
      target = pc + offset;
    else
      target = pc + 4;
  }

  Context context;
  context.type = eContextRelativeBranchImmediate;
  context.SetImmediate(current_inst_size + offset);

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

bool EmulateInstructionMIPS64::Emulate_J(llvm::MCInst &insn) {
  bool success = false;
  uint64_t offset, pc;

  /*
   * J offset
   *      offset = sign_ext (offset << 2)
   *      PC = PC[63-28] | offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  /* This is a PC-region branch and not PC-relative */
  pc = (pc & 0xFFFFFFFFF0000000ULL) | offset;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               pc);
}

bool EmulateInstructionMIPS64::Emulate_JAL(llvm::MCInst &insn) {
  bool success = false;
  uint64_t offset, target, pc;

  /*
   * JAL offset
   *      offset = sign_ext (offset << 2)
   *      PC = PC[63-28] | offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  /* This is a PC-region branch and not PC-relative */
  target = (pc & 0xFFFFFFFFF0000000ULL) | offset;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips64,
                             pc + 8))
    return false;

  return true;
}

bool EmulateInstructionMIPS64::Emulate_JALR(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs, rt;
  uint64_t pc, rs_val;

  /*
   * JALR rt, rs
   *      GPR[rt] = PC + 8
   *      PC = GPR[rs]
  */
  rt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  rs = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  rs_val = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips64 + rs, 0,
                                &success);
  if (!success)
    return false;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                             rs_val))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF,
                             dwarf_zero_mips64 + rt, pc + 8))
    return false;

  return true;
}

bool EmulateInstructionMIPS64::Emulate_JIALC(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rt;
  int64_t target, offset, pc, rt_val;

  /*
   * JIALC rt, offset
   *      offset = sign_ext (offset)
   *      PC = GPR[rt] + offset
   *      RA = PC + 4
  */
  rt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  rt_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rt, 0, &success);
  if (!success)
    return false;

  target = rt_val + offset;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips64,
                             pc + 4))
    return false;

  return true;
}

bool EmulateInstructionMIPS64::Emulate_JIC(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rt;
  int64_t target, offset, rt_val;

  /*
   * JIC rt, offset
   *      offset = sign_ext (offset)
   *      PC = GPR[rt] + offset
  */
  rt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  rt_val = (int64_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips64 + rt, 0, &success);
  if (!success)
    return false;

  target = rt_val + offset;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

bool EmulateInstructionMIPS64::Emulate_JR(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  uint64_t rs_val;

  /*
   * JR rs
   *      PC = GPR[rs]
  */
  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());

  rs_val = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips64 + rs, 0,
                                &success);
  if (!success)
    return false;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               rs_val);
}

/*
    Emulate Branch on FP True/False
    BC1F, BC1FL :   Branch on FP False (L stands for branch likely)
    BC1T, BC1TL :   Branch on FP True  (L stands for branch likely)
*/
bool EmulateInstructionMIPS64::Emulate_FP_branch(llvm::MCInst &insn) {
  bool success = false;
  uint32_t cc, fcsr;
  int64_t pc, offset, target = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  /*
   * BC1F cc, offset
   *  condition <- (FPConditionCode(cc) == 0)
   *      if condition then
   *          offset = sign_ext (offset)
   *          PC = PC + offset
  */
  cc = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  fcsr =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_fcsr_mips64, 0, &success);
  if (!success)
    return false;

  /* fcsr[23], fcsr[25-31] are vaild condition bits */
  fcsr = ((fcsr >> 24) & 0xfe) | ((fcsr >> 23) & 0x01);

  if (op_name.equals_insensitive("BC1F") ||
      op_name.equals_insensitive("BC1FL")) {
    if ((fcsr & (1 << cc)) == 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BC1T") ||
             op_name.equals_insensitive("BC1TL")) {
    if ((fcsr & (1 << cc)) != 0)
      target = pc + offset;
    else
      target = pc + 8;
  }

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

bool EmulateInstructionMIPS64::Emulate_BC1EQZ(llvm::MCInst &insn) {
  bool success = false;
  uint32_t ft;
  uint64_t ft_val;
  int64_t target, pc, offset;

  /*
   * BC1EQZ ft, offset
   *  condition <- (FPR[ft].bit0 == 0)
   *      if condition then
   *          offset = sign_ext (offset)
   *          PC = PC + 4 + offset
  */
  ft = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  ft_val = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips64 + ft, 0,
                                &success);
  if (!success)
    return false;

  if ((ft_val & 1) == 0)
    target = pc + 4 + offset;
  else
    target = pc + 8;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

bool EmulateInstructionMIPS64::Emulate_BC1NEZ(llvm::MCInst &insn) {
  bool success = false;
  uint32_t ft;
  uint64_t ft_val;
  int64_t target, pc, offset;

  /*
   * BC1NEZ ft, offset
   *  condition <- (FPR[ft].bit0 != 0)
   *      if condition then
   *          offset = sign_ext (offset)
   *          PC = PC + 4 + offset
  */
  ft = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  ft_val = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips64 + ft, 0,
                                &success);
  if (!success)
    return false;

  if ((ft_val & 1) != 0)
    target = pc + 4 + offset;
  else
    target = pc + 8;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

/*
    Emulate MIPS-3D Branch instructions
    BC1ANY2F, BC1ANY2T  : Branch on Any of Two Floating Point Condition Codes
   False/True
    BC1ANY4F, BC1ANY4T  : Branch on Any of Four Floating Point Condition Codes
   False/True
*/
bool EmulateInstructionMIPS64::Emulate_3D_branch(llvm::MCInst &insn) {
  bool success = false;
  uint32_t cc, fcsr;
  int64_t pc, offset, target = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  cc = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  fcsr = (uint32_t)ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_fcsr_mips64,
                                        0, &success);
  if (!success)
    return false;

  /* fcsr[23], fcsr[25-31] are vaild condition bits */
  fcsr = ((fcsr >> 24) & 0xfe) | ((fcsr >> 23) & 0x01);

  if (op_name.equals_insensitive("BC1ANY2F")) {
    /* if any one bit is 0 */
    if (((fcsr >> cc) & 3) != 3)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BC1ANY2T")) {
    /* if any one bit is 1 */
    if (((fcsr >> cc) & 3) != 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BC1ANY4F")) {
    /* if any one bit is 0 */
    if (((fcsr >> cc) & 0xf) != 0xf)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BC1ANY4T")) {
    /* if any one bit is 1 */
    if (((fcsr >> cc) & 0xf) != 0)
      target = pc + offset;
    else
      target = pc + 8;
  }

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

bool EmulateInstructionMIPS64::Emulate_BNZB(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 1, true);
}

bool EmulateInstructionMIPS64::Emulate_BNZH(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 2, true);
}

bool EmulateInstructionMIPS64::Emulate_BNZW(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 4, true);
}

bool EmulateInstructionMIPS64::Emulate_BNZD(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 8, true);
}

bool EmulateInstructionMIPS64::Emulate_BZB(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 1, false);
}

bool EmulateInstructionMIPS64::Emulate_BZH(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 2, false);
}

bool EmulateInstructionMIPS64::Emulate_BZW(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 4, false);
}

bool EmulateInstructionMIPS64::Emulate_BZD(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 8, false);
}

bool EmulateInstructionMIPS64::Emulate_MSA_Branch_DF(llvm::MCInst &insn,
                                                     int element_byte_size,
                                                     bool bnz) {
  bool success = false, branch_hit = true;
  int64_t target = 0;
  RegisterValue reg_value;
  const uint8_t *ptr = nullptr;

  uint32_t wt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  int64_t offset = insn.getOperand(1).getImm();

  int64_t pc =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  if (ReadRegister(eRegisterKindDWARF, dwarf_w0_mips64 + wt, reg_value))
    ptr = (const uint8_t *)reg_value.GetBytes();
  else
    return false;

  for (int i = 0; i < 16 / element_byte_size; i++) {
    switch (element_byte_size) {
    case 1:
      if ((*ptr == 0 && bnz) || (*ptr != 0 && !bnz))
        branch_hit = false;
      break;
    case 2:
      if ((*(const uint16_t *)ptr == 0 && bnz) ||
          (*(const uint16_t *)ptr != 0 && !bnz))
        branch_hit = false;
      break;
    case 4:
      if ((*(const uint32_t *)ptr == 0 && bnz) ||
          (*(const uint32_t *)ptr != 0 && !bnz))
        branch_hit = false;
      break;
    case 8:
      if ((*(const uint64_t *)ptr == 0 && bnz) ||
          (*(const uint64_t *)ptr != 0 && !bnz))
        branch_hit = false;
      break;
    }
    if (!branch_hit)
      break;
    ptr = ptr + element_byte_size;
  }

  if (branch_hit)
    target = pc + offset;
  else
    target = pc + 8;

  Context context;
  context.type = eContextRelativeBranchImmediate;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

bool EmulateInstructionMIPS64::Emulate_BNZV(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_V(insn, true);
}

bool EmulateInstructionMIPS64::Emulate_BZV(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_V(insn, false);
}

bool EmulateInstructionMIPS64::Emulate_MSA_Branch_V(llvm::MCInst &insn,
                                                    bool bnz) {
  bool success = false;
  int64_t target = 0;
  llvm::APInt wr_val = llvm::APInt::getZero(128);
  llvm::APInt fail_value = llvm::APInt::getMaxValue(128);
  llvm::APInt zero_value = llvm::APInt::getZero(128);
  RegisterValue reg_value;

  uint32_t wt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  int64_t offset = insn.getOperand(1).getImm();

  int64_t pc =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips64, 0, &success);
  if (!success)
    return false;

  if (ReadRegister(eRegisterKindDWARF, dwarf_w0_mips64 + wt, reg_value))
    wr_val = reg_value.GetAsUInt128(fail_value);
  else
    return false;

  if ((llvm::APInt::isSameValue(zero_value, wr_val) && !bnz) ||
      (!llvm::APInt::isSameValue(zero_value, wr_val) && bnz))
    target = pc + offset;
  else
    target = pc + 8;

  Context context;
  context.type = eContextRelativeBranchImmediate;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips64,
                               target);
}

bool EmulateInstructionMIPS64::Emulate_LDST_Imm(llvm::MCInst &insn) {
  bool success = false;
  uint32_t base;
  int64_t imm, address;
  Context bad_vaddr_context;

  uint32_t num_operands = insn.getNumOperands();
  base =
      m_reg_info->getEncodingValue(insn.getOperand(num_operands - 2).getReg());
  imm = insn.getOperand(num_operands - 1).getImm();

  if (!GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + base))
    return false;

  /* read base register */
  address = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips + base, 0,
                                 &success);
  if (!success)
    return false;

  /* destination address */
  address = address + imm;

  /* Set the bad_vaddr register with base address used in the instruction */
  bad_vaddr_context.type = eContextInvalid;
  WriteRegisterUnsigned(bad_vaddr_context, eRegisterKindDWARF, dwarf_bad_mips,
                        address);

  return true;
}

bool EmulateInstructionMIPS64::Emulate_LDST_Reg(llvm::MCInst &insn) {
  bool success = false;
  uint32_t base, index;
  int64_t address, index_address;
  Context bad_vaddr_context;

  uint32_t num_operands = insn.getNumOperands();
  base =
      m_reg_info->getEncodingValue(insn.getOperand(num_operands - 2).getReg());
  index =
      m_reg_info->getEncodingValue(insn.getOperand(num_operands - 1).getReg());

  if (!GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + base))
    return false;

  if (!GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + index))
    return false;

  /* read base register */
  address = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips + base, 0,
                                 &success);
  if (!success)
    return false;

  /* read index register */
  index_address = ReadRegisterUnsigned(eRegisterKindDWARF,
                                       dwarf_zero_mips + index, 0, &success);
  if (!success)
    return false;

  /* destination address */
  address = address + index_address;

  /* Set the bad_vaddr register with base address used in the instruction */
  bad_vaddr_context.type = eContextInvalid;
  WriteRegisterUnsigned(bad_vaddr_context, eRegisterKindDWARF, dwarf_bad_mips,
                        address);

  return true;
}
