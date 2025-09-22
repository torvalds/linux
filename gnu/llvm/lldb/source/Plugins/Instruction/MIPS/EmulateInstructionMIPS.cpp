//===-- EmulateInstructionMIPS.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EmulateInstructionMIPS.h"

#include <cstdlib>
#include <optional>

#include "lldb/Core/Address.h"
#include "lldb/Core/Opcode.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Target.h"
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

LLDB_PLUGIN_DEFINE_ADV(EmulateInstructionMIPS, InstructionMIPS)

#define UInt(x) ((uint64_t)x)
#define integer int64_t

//
// EmulateInstructionMIPS implementation
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

EmulateInstructionMIPS::EmulateInstructionMIPS(
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

  /* Create alternate disassembler for microMIPS */
  if (arch_flags & ArchSpec::eMIPSAse_mips16)
    features += "+mips16,";
  else if (arch_flags & ArchSpec::eMIPSAse_micromips)
    features += "+micromips,";

  m_alt_subtype_info.reset(
      target->createMCSubtargetInfo(triple.getTriple(), cpu, features));
  assert(m_alt_subtype_info.get());

  m_alt_disasm.reset(
      target->createMCDisassembler(*m_alt_subtype_info, *m_context));
  assert(m_alt_disasm.get());

  m_next_inst_size = 0;
  m_use_alt_disaasm = false;
}

void EmulateInstructionMIPS::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void EmulateInstructionMIPS::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef EmulateInstructionMIPS::GetPluginDescriptionStatic() {
  return "Emulate instructions for the MIPS32 architecture.";
}

EmulateInstruction *
EmulateInstructionMIPS::CreateInstance(const ArchSpec &arch,
                                       InstructionType inst_type) {
  if (EmulateInstructionMIPS::SupportsEmulatingInstructionsOfTypeStatic(
          inst_type)) {
    if (arch.GetTriple().getArch() == llvm::Triple::mips ||
        arch.GetTriple().getArch() == llvm::Triple::mipsel) {
      return new EmulateInstructionMIPS(arch);
    }
  }

  return nullptr;
}

bool EmulateInstructionMIPS::SetTargetTriple(const ArchSpec &arch) {
  return arch.GetTriple().getArch() == llvm::Triple::mips ||
         arch.GetTriple().getArch() == llvm::Triple::mipsel;
}

const char *EmulateInstructionMIPS::GetRegisterName(unsigned reg_num,
                                                    bool alternate_name) {
  if (alternate_name) {
    switch (reg_num) {
    case dwarf_sp_mips:
      return "r29";
    case dwarf_r30_mips:
      return "r30";
    case dwarf_ra_mips:
      return "r31";
    case dwarf_f0_mips:
      return "f0";
    case dwarf_f1_mips:
      return "f1";
    case dwarf_f2_mips:
      return "f2";
    case dwarf_f3_mips:
      return "f3";
    case dwarf_f4_mips:
      return "f4";
    case dwarf_f5_mips:
      return "f5";
    case dwarf_f6_mips:
      return "f6";
    case dwarf_f7_mips:
      return "f7";
    case dwarf_f8_mips:
      return "f8";
    case dwarf_f9_mips:
      return "f9";
    case dwarf_f10_mips:
      return "f10";
    case dwarf_f11_mips:
      return "f11";
    case dwarf_f12_mips:
      return "f12";
    case dwarf_f13_mips:
      return "f13";
    case dwarf_f14_mips:
      return "f14";
    case dwarf_f15_mips:
      return "f15";
    case dwarf_f16_mips:
      return "f16";
    case dwarf_f17_mips:
      return "f17";
    case dwarf_f18_mips:
      return "f18";
    case dwarf_f19_mips:
      return "f19";
    case dwarf_f20_mips:
      return "f20";
    case dwarf_f21_mips:
      return "f21";
    case dwarf_f22_mips:
      return "f22";
    case dwarf_f23_mips:
      return "f23";
    case dwarf_f24_mips:
      return "f24";
    case dwarf_f25_mips:
      return "f25";
    case dwarf_f26_mips:
      return "f26";
    case dwarf_f27_mips:
      return "f27";
    case dwarf_f28_mips:
      return "f28";
    case dwarf_f29_mips:
      return "f29";
    case dwarf_f30_mips:
      return "f30";
    case dwarf_f31_mips:
      return "f31";
    case dwarf_w0_mips:
      return "w0";
    case dwarf_w1_mips:
      return "w1";
    case dwarf_w2_mips:
      return "w2";
    case dwarf_w3_mips:
      return "w3";
    case dwarf_w4_mips:
      return "w4";
    case dwarf_w5_mips:
      return "w5";
    case dwarf_w6_mips:
      return "w6";
    case dwarf_w7_mips:
      return "w7";
    case dwarf_w8_mips:
      return "w8";
    case dwarf_w9_mips:
      return "w9";
    case dwarf_w10_mips:
      return "w10";
    case dwarf_w11_mips:
      return "w11";
    case dwarf_w12_mips:
      return "w12";
    case dwarf_w13_mips:
      return "w13";
    case dwarf_w14_mips:
      return "w14";
    case dwarf_w15_mips:
      return "w15";
    case dwarf_w16_mips:
      return "w16";
    case dwarf_w17_mips:
      return "w17";
    case dwarf_w18_mips:
      return "w18";
    case dwarf_w19_mips:
      return "w19";
    case dwarf_w20_mips:
      return "w20";
    case dwarf_w21_mips:
      return "w21";
    case dwarf_w22_mips:
      return "w22";
    case dwarf_w23_mips:
      return "w23";
    case dwarf_w24_mips:
      return "w24";
    case dwarf_w25_mips:
      return "w25";
    case dwarf_w26_mips:
      return "w26";
    case dwarf_w27_mips:
      return "w27";
    case dwarf_w28_mips:
      return "w28";
    case dwarf_w29_mips:
      return "w29";
    case dwarf_w30_mips:
      return "w30";
    case dwarf_w31_mips:
      return "w31";
    case dwarf_mir_mips:
      return "mir";
    case dwarf_mcsr_mips:
      return "mcsr";
    case dwarf_config5_mips:
      return "config5";
    default:
      break;
    }
    return nullptr;
  }

  switch (reg_num) {
  case dwarf_zero_mips:
    return "r0";
  case dwarf_r1_mips:
    return "r1";
  case dwarf_r2_mips:
    return "r2";
  case dwarf_r3_mips:
    return "r3";
  case dwarf_r4_mips:
    return "r4";
  case dwarf_r5_mips:
    return "r5";
  case dwarf_r6_mips:
    return "r6";
  case dwarf_r7_mips:
    return "r7";
  case dwarf_r8_mips:
    return "r8";
  case dwarf_r9_mips:
    return "r9";
  case dwarf_r10_mips:
    return "r10";
  case dwarf_r11_mips:
    return "r11";
  case dwarf_r12_mips:
    return "r12";
  case dwarf_r13_mips:
    return "r13";
  case dwarf_r14_mips:
    return "r14";
  case dwarf_r15_mips:
    return "r15";
  case dwarf_r16_mips:
    return "r16";
  case dwarf_r17_mips:
    return "r17";
  case dwarf_r18_mips:
    return "r18";
  case dwarf_r19_mips:
    return "r19";
  case dwarf_r20_mips:
    return "r20";
  case dwarf_r21_mips:
    return "r21";
  case dwarf_r22_mips:
    return "r22";
  case dwarf_r23_mips:
    return "r23";
  case dwarf_r24_mips:
    return "r24";
  case dwarf_r25_mips:
    return "r25";
  case dwarf_r26_mips:
    return "r26";
  case dwarf_r27_mips:
    return "r27";
  case dwarf_gp_mips:
    return "gp";
  case dwarf_sp_mips:
    return "sp";
  case dwarf_r30_mips:
    return "fp";
  case dwarf_ra_mips:
    return "ra";
  case dwarf_sr_mips:
    return "sr";
  case dwarf_lo_mips:
    return "lo";
  case dwarf_hi_mips:
    return "hi";
  case dwarf_bad_mips:
    return "bad";
  case dwarf_cause_mips:
    return "cause";
  case dwarf_pc_mips:
    return "pc";
  case dwarf_f0_mips:
    return "f0";
  case dwarf_f1_mips:
    return "f1";
  case dwarf_f2_mips:
    return "f2";
  case dwarf_f3_mips:
    return "f3";
  case dwarf_f4_mips:
    return "f4";
  case dwarf_f5_mips:
    return "f5";
  case dwarf_f6_mips:
    return "f6";
  case dwarf_f7_mips:
    return "f7";
  case dwarf_f8_mips:
    return "f8";
  case dwarf_f9_mips:
    return "f9";
  case dwarf_f10_mips:
    return "f10";
  case dwarf_f11_mips:
    return "f11";
  case dwarf_f12_mips:
    return "f12";
  case dwarf_f13_mips:
    return "f13";
  case dwarf_f14_mips:
    return "f14";
  case dwarf_f15_mips:
    return "f15";
  case dwarf_f16_mips:
    return "f16";
  case dwarf_f17_mips:
    return "f17";
  case dwarf_f18_mips:
    return "f18";
  case dwarf_f19_mips:
    return "f19";
  case dwarf_f20_mips:
    return "f20";
  case dwarf_f21_mips:
    return "f21";
  case dwarf_f22_mips:
    return "f22";
  case dwarf_f23_mips:
    return "f23";
  case dwarf_f24_mips:
    return "f24";
  case dwarf_f25_mips:
    return "f25";
  case dwarf_f26_mips:
    return "f26";
  case dwarf_f27_mips:
    return "f27";
  case dwarf_f28_mips:
    return "f28";
  case dwarf_f29_mips:
    return "f29";
  case dwarf_f30_mips:
    return "f30";
  case dwarf_f31_mips:
    return "f31";
  case dwarf_fcsr_mips:
    return "fcsr";
  case dwarf_fir_mips:
    return "fir";
  case dwarf_w0_mips:
    return "w0";
  case dwarf_w1_mips:
    return "w1";
  case dwarf_w2_mips:
    return "w2";
  case dwarf_w3_mips:
    return "w3";
  case dwarf_w4_mips:
    return "w4";
  case dwarf_w5_mips:
    return "w5";
  case dwarf_w6_mips:
    return "w6";
  case dwarf_w7_mips:
    return "w7";
  case dwarf_w8_mips:
    return "w8";
  case dwarf_w9_mips:
    return "w9";
  case dwarf_w10_mips:
    return "w10";
  case dwarf_w11_mips:
    return "w11";
  case dwarf_w12_mips:
    return "w12";
  case dwarf_w13_mips:
    return "w13";
  case dwarf_w14_mips:
    return "w14";
  case dwarf_w15_mips:
    return "w15";
  case dwarf_w16_mips:
    return "w16";
  case dwarf_w17_mips:
    return "w17";
  case dwarf_w18_mips:
    return "w18";
  case dwarf_w19_mips:
    return "w19";
  case dwarf_w20_mips:
    return "w20";
  case dwarf_w21_mips:
    return "w21";
  case dwarf_w22_mips:
    return "w22";
  case dwarf_w23_mips:
    return "w23";
  case dwarf_w24_mips:
    return "w24";
  case dwarf_w25_mips:
    return "w25";
  case dwarf_w26_mips:
    return "w26";
  case dwarf_w27_mips:
    return "w27";
  case dwarf_w28_mips:
    return "w28";
  case dwarf_w29_mips:
    return "w29";
  case dwarf_w30_mips:
    return "w30";
  case dwarf_w31_mips:
    return "w31";
  case dwarf_mcsr_mips:
    return "mcsr";
  case dwarf_mir_mips:
    return "mir";
  case dwarf_config5_mips:
    return "config5";
  }
  return nullptr;
}

std::optional<RegisterInfo>
EmulateInstructionMIPS::GetRegisterInfo(RegisterKind reg_kind,
                                        uint32_t reg_num) {
  if (reg_kind == eRegisterKindGeneric) {
    switch (reg_num) {
    case LLDB_REGNUM_GENERIC_PC:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_pc_mips;
      break;
    case LLDB_REGNUM_GENERIC_SP:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_sp_mips;
      break;
    case LLDB_REGNUM_GENERIC_FP:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_r30_mips;
      break;
    case LLDB_REGNUM_GENERIC_RA:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_ra_mips;
      break;
    case LLDB_REGNUM_GENERIC_FLAGS:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_sr_mips;
      break;
    default:
      return {};
    }
  }

  if (reg_kind == eRegisterKindDWARF) {
    RegisterInfo reg_info;
    ::memset(&reg_info, 0, sizeof(RegisterInfo));
    ::memset(reg_info.kinds, LLDB_INVALID_REGNUM, sizeof(reg_info.kinds));

    if (reg_num == dwarf_sr_mips || reg_num == dwarf_fcsr_mips ||
        reg_num == dwarf_fir_mips || reg_num == dwarf_mcsr_mips ||
        reg_num == dwarf_mir_mips || reg_num == dwarf_config5_mips) {
      reg_info.byte_size = 4;
      reg_info.format = eFormatHex;
      reg_info.encoding = eEncodingUint;
    } else if ((int)reg_num >= dwarf_zero_mips &&
               (int)reg_num <= dwarf_f31_mips) {
      reg_info.byte_size = 4;
      reg_info.format = eFormatHex;
      reg_info.encoding = eEncodingUint;
    } else if ((int)reg_num >= dwarf_w0_mips &&
               (int)reg_num <= dwarf_w31_mips) {
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
    case dwarf_r30_mips:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
      break;
    case dwarf_ra_mips:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_RA;
      break;
    case dwarf_sp_mips:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
      break;
    case dwarf_pc_mips:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
      break;
    case dwarf_sr_mips:
      reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      break;
    default:
      break;
    }
    return reg_info;
  }
  return {};
}

EmulateInstructionMIPS::MipsOpcode *
EmulateInstructionMIPS::GetOpcodeForInstruction(llvm::StringRef name) {
  static EmulateInstructionMIPS::MipsOpcode g_opcodes[] = {
      // Prologue/Epilogue instructions
      {"ADDiu", &EmulateInstructionMIPS::Emulate_ADDiu,
       "ADDIU rt, rs, immediate"},
      {"SW", &EmulateInstructionMIPS::Emulate_SW, "SW rt, offset(rs)"},
      {"LW", &EmulateInstructionMIPS::Emulate_LW, "LW rt, offset(base)"},
      {"SUBU", &EmulateInstructionMIPS::Emulate_SUBU_ADDU, "SUBU rd, rs, rt"},
      {"ADDU", &EmulateInstructionMIPS::Emulate_SUBU_ADDU, "ADDU rd, rs, rt"},
      {"LUI", &EmulateInstructionMIPS::Emulate_LUI, "LUI rt, immediate"},

      // MicroMIPS Prologue/Epilogue instructions
      {"ADDIUSP_MM", &EmulateInstructionMIPS::Emulate_ADDIUSP,
       "ADDIU immediate"},
      {"ADDIUS5_MM", &EmulateInstructionMIPS::Emulate_ADDIUS5,
       "ADDIUS5 rd,immediate"},
      {"SWSP_MM", &EmulateInstructionMIPS::Emulate_SWSP, "SWSP rt,offset(sp)"},
      {"SWM16_MM", &EmulateInstructionMIPS::Emulate_SWM16_32,
       "SWM16 reglist,offset(sp)"},
      {"SWM32_MM", &EmulateInstructionMIPS::Emulate_SWM16_32,
       "SWM32 reglist,offset(base)"},
      {"SWP_MM", &EmulateInstructionMIPS::Emulate_SWM16_32,
       "SWP rs1,offset(base)"},
      {"LWSP_MM", &EmulateInstructionMIPS::Emulate_LWSP, "LWSP rt,offset(sp)"},
      {"LWM16_MM", &EmulateInstructionMIPS::Emulate_LWM16_32,
       "LWM16 reglist,offset(sp)"},
      {"LWM32_MM", &EmulateInstructionMIPS::Emulate_LWM16_32,
       "LWM32 reglist,offset(base)"},
      {"LWP_MM", &EmulateInstructionMIPS::Emulate_LWM16_32,
       "LWP rd,offset(base)"},
      {"JRADDIUSP", &EmulateInstructionMIPS::Emulate_JRADDIUSP,
       "JRADDIUSP immediate"},

      // Load/Store  instructions
      /* Following list of emulated instructions are required by implementation
         of hardware watchpoint
         for MIPS in lldb. As we just need the address accessed by instructions,
         we have generalised
         all these instructions in 2 functions depending on their addressing
         modes */

      {"LB", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LB    rt, offset(base)"},
      {"LBE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LBE   rt, offset(base)"},
      {"LBU", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LBU   rt, offset(base)"},
      {"LBUE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LBUE  rt, offset(base)"},
      {"LDC1", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LDC1  ft, offset(base)"},
      {"LD", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LD    rt, offset(base)"},
      {"LDL", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LDL   rt, offset(base)"},
      {"LDR", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LDR   rt, offset(base)"},
      {"LLD", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LLD   rt, offset(base)"},
      {"LDC2", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LDC2  rt, offset(base)"},
      {"LDXC1", &EmulateInstructionMIPS::Emulate_LDST_Reg,
       "LDXC1 fd, index (base)"},
      {"LH", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LH    rt, offset(base)"},
      {"LHE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LHE   rt, offset(base)"},
      {"LHU", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LHU   rt, offset(base)"},
      {"LHUE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LHUE  rt, offset(base)"},
      {"LL", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LL    rt, offset(base)"},
      {"LLE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LLE   rt, offset(base)"},
      {"LUXC1", &EmulateInstructionMIPS::Emulate_LDST_Reg,
       "LUXC1 fd, index (base)"},
      {"LW", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LW    rt, offset(base)"},
      {"LWC1", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LWC1  ft, offset(base)"},
      {"LWC2", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LWC2  rt, offset(base)"},
      {"LWE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LWE   rt, offset(base)"},
      {"LWL", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LWL   rt, offset(base)"},
      {"LWLE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LWLE  rt, offset(base)"},
      {"LWR", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LWR   rt, offset(base)"},
      {"LWRE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LWRE  rt, offset(base)"},
      {"LWXC1", &EmulateInstructionMIPS::Emulate_LDST_Reg,
       "LWXC1 fd, index (base)"},
      {"LLX", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LLX   rt, offset(base)"},
      {"LLXE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LLXE  rt, offset(base)"},
      {"LLDX", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LLDX  rt, offset(base)"},

      {"SB", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SB    rt, offset(base)"},
      {"SBE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SBE   rt, offset(base)"},
      {"SC", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SC    rt, offset(base)"},
      {"SCE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SCE   rt, offset(base)"},
      {"SCD", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SCD   rt, offset(base)"},
      {"SD", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SD    rt, offset(base)"},
      {"SDL", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SDL   rt, offset(base)"},
      {"SDR", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SDR   rt, offset(base)"},
      {"SDC1", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SDC1  ft, offset(base)"},
      {"SDC2", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SDC2  rt, offset(base)"},
      {"SDXC1", &EmulateInstructionMIPS::Emulate_LDST_Reg,
       "SDXC1 fs, index(base)"},
      {"SH", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SH    rt, offset(base)"},
      {"SHE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SHE   rt, offset(base)"},
      {"SUXC1", &EmulateInstructionMIPS::Emulate_LDST_Reg,
       "SUXC1 fs, index (base)"},
      {"SWC1", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SWC1  ft, offset(base)"},
      {"SWC2", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SWC2  rt, offset(base)"},
      {"SWE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SWE   rt, offset(base)"},
      {"SWL", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SWL   rt, offset(base)"},
      {"SWLE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SWLE  rt, offset(base)"},
      {"SWR", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SWR   rt, offset(base)"},
      {"SWRE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SWRE  rt, offset(base)"},
      {"SWXC1", &EmulateInstructionMIPS::Emulate_LDST_Reg,
       "SWXC1 fs, index (base)"},
      {"SCX", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SCX   rt, offset(base)"},
      {"SCXE", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SCXE  rt, offset(base)"},
      {"SCDX", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SCDX  rt, offset(base)"},

      // MicroMIPS Load/Store instructions
      {"LBU16_MM", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LBU16 rt, decoded_offset(base)"},
      {"LHU16_MM", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LHU16 rt, left_shifted_offset(base)"},
      {"LW16_MM", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LW16  rt, left_shifted_offset(base)"},
      {"LWGP_MM", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "LWGP  rt, left_shifted_offset(gp)"},
      {"SH16_MM", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SH16  rt, left_shifted_offset(base)"},
      {"SW16_MM", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SW16  rt, left_shifted_offset(base)"},
      {"SW_MM", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SWSP  rt, left_shifted_offset(base)"},
      {"SB16_MM", &EmulateInstructionMIPS::Emulate_LDST_Imm,
       "SB16  rt, offset(base)"},

      // Branch instructions
      {"BEQ", &EmulateInstructionMIPS::Emulate_BXX_3ops, "BEQ rs,rt,offset"},
      {"BNE", &EmulateInstructionMIPS::Emulate_BXX_3ops, "BNE rs,rt,offset"},
      {"BEQL", &EmulateInstructionMIPS::Emulate_BXX_3ops, "BEQL rs,rt,offset"},
      {"BNEL", &EmulateInstructionMIPS::Emulate_BXX_3ops, "BNEL rs,rt,offset"},
      {"BGEZALL", &EmulateInstructionMIPS::Emulate_Bcond_Link,
       "BGEZALL rt,offset"},
      {"BAL", &EmulateInstructionMIPS::Emulate_BAL, "BAL offset"},
      {"BGEZAL", &EmulateInstructionMIPS::Emulate_Bcond_Link,
       "BGEZAL rs,offset"},
      {"BALC", &EmulateInstructionMIPS::Emulate_BALC, "BALC offset"},
      {"BC", &EmulateInstructionMIPS::Emulate_BC, "BC offset"},
      {"BGEZ", &EmulateInstructionMIPS::Emulate_BXX_2ops, "BGEZ rs,offset"},
      {"BLEZALC", &EmulateInstructionMIPS::Emulate_Bcond_Link_C,
       "BLEZALC rs,offset"},
      {"BGEZALC", &EmulateInstructionMIPS::Emulate_Bcond_Link_C,
       "BGEZALC rs,offset"},
      {"BLTZALC", &EmulateInstructionMIPS::Emulate_Bcond_Link_C,
       "BLTZALC rs,offset"},
      {"BGTZALC", &EmulateInstructionMIPS::Emulate_Bcond_Link_C,
       "BGTZALC rs,offset"},
      {"BEQZALC", &EmulateInstructionMIPS::Emulate_Bcond_Link_C,
       "BEQZALC rs,offset"},
      {"BNEZALC", &EmulateInstructionMIPS::Emulate_Bcond_Link_C,
       "BNEZALC rs,offset"},
      {"BEQC", &EmulateInstructionMIPS::Emulate_BXX_3ops_C,
       "BEQC rs,rt,offset"},
      {"BNEC", &EmulateInstructionMIPS::Emulate_BXX_3ops_C,
       "BNEC rs,rt,offset"},
      {"BLTC", &EmulateInstructionMIPS::Emulate_BXX_3ops_C,
       "BLTC rs,rt,offset"},
      {"BGEC", &EmulateInstructionMIPS::Emulate_BXX_3ops_C,
       "BGEC rs,rt,offset"},
      {"BLTUC", &EmulateInstructionMIPS::Emulate_BXX_3ops_C,
       "BLTUC rs,rt,offset"},
      {"BGEUC", &EmulateInstructionMIPS::Emulate_BXX_3ops_C,
       "BGEUC rs,rt,offset"},
      {"BLTZC", &EmulateInstructionMIPS::Emulate_BXX_2ops_C, "BLTZC rt,offset"},
      {"BLEZC", &EmulateInstructionMIPS::Emulate_BXX_2ops_C, "BLEZC rt,offset"},
      {"BGEZC", &EmulateInstructionMIPS::Emulate_BXX_2ops_C, "BGEZC rt,offset"},
      {"BGTZC", &EmulateInstructionMIPS::Emulate_BXX_2ops_C, "BGTZC rt,offset"},
      {"BEQZC", &EmulateInstructionMIPS::Emulate_BXX_2ops_C, "BEQZC rt,offset"},
      {"BNEZC", &EmulateInstructionMIPS::Emulate_BXX_2ops_C, "BNEZC rt,offset"},
      {"BGEZL", &EmulateInstructionMIPS::Emulate_BXX_2ops, "BGEZL rt,offset"},
      {"BGTZ", &EmulateInstructionMIPS::Emulate_BXX_2ops, "BGTZ rt,offset"},
      {"BGTZL", &EmulateInstructionMIPS::Emulate_BXX_2ops, "BGTZL rt,offset"},
      {"BLEZ", &EmulateInstructionMIPS::Emulate_BXX_2ops, "BLEZ rt,offset"},
      {"BLEZL", &EmulateInstructionMIPS::Emulate_BXX_2ops, "BLEZL rt,offset"},
      {"BLTZ", &EmulateInstructionMIPS::Emulate_BXX_2ops, "BLTZ rt,offset"},
      {"BLTZAL", &EmulateInstructionMIPS::Emulate_Bcond_Link,
       "BLTZAL rt,offset"},
      {"BLTZALL", &EmulateInstructionMIPS::Emulate_Bcond_Link,
       "BLTZALL rt,offset"},
      {"BLTZL", &EmulateInstructionMIPS::Emulate_BXX_2ops, "BLTZL rt,offset"},
      {"BOVC", &EmulateInstructionMIPS::Emulate_BXX_3ops_C,
       "BOVC rs,rt,offset"},
      {"BNVC", &EmulateInstructionMIPS::Emulate_BXX_3ops_C,
       "BNVC rs,rt,offset"},
      {"J", &EmulateInstructionMIPS::Emulate_J, "J target"},
      {"JAL", &EmulateInstructionMIPS::Emulate_JAL, "JAL target"},
      {"JALX", &EmulateInstructionMIPS::Emulate_JAL, "JALX target"},
      {"JALR", &EmulateInstructionMIPS::Emulate_JALR, "JALR target"},
      {"JALR_HB", &EmulateInstructionMIPS::Emulate_JALR, "JALR.HB target"},
      {"JIALC", &EmulateInstructionMIPS::Emulate_JIALC, "JIALC rt,offset"},
      {"JIC", &EmulateInstructionMIPS::Emulate_JIC, "JIC rt,offset"},
      {"JR", &EmulateInstructionMIPS::Emulate_JR, "JR target"},
      {"JR_HB", &EmulateInstructionMIPS::Emulate_JR, "JR.HB target"},
      {"BC1F", &EmulateInstructionMIPS::Emulate_FP_branch, "BC1F cc, offset"},
      {"BC1T", &EmulateInstructionMIPS::Emulate_FP_branch, "BC1T cc, offset"},
      {"BC1FL", &EmulateInstructionMIPS::Emulate_FP_branch, "BC1FL cc, offset"},
      {"BC1TL", &EmulateInstructionMIPS::Emulate_FP_branch, "BC1TL cc, offset"},
      {"BC1EQZ", &EmulateInstructionMIPS::Emulate_BC1EQZ, "BC1EQZ ft, offset"},
      {"BC1NEZ", &EmulateInstructionMIPS::Emulate_BC1NEZ, "BC1NEZ ft, offset"},
      {"BC1ANY2F", &EmulateInstructionMIPS::Emulate_3D_branch,
       "BC1ANY2F cc, offset"},
      {"BC1ANY2T", &EmulateInstructionMIPS::Emulate_3D_branch,
       "BC1ANY2T cc, offset"},
      {"BC1ANY4F", &EmulateInstructionMIPS::Emulate_3D_branch,
       "BC1ANY4F cc, offset"},
      {"BC1ANY4T", &EmulateInstructionMIPS::Emulate_3D_branch,
       "BC1ANY4T cc, offset"},
      {"BNZ_B", &EmulateInstructionMIPS::Emulate_BNZB, "BNZ.b wt,s16"},
      {"BNZ_H", &EmulateInstructionMIPS::Emulate_BNZH, "BNZ.h wt,s16"},
      {"BNZ_W", &EmulateInstructionMIPS::Emulate_BNZW, "BNZ.w wt,s16"},
      {"BNZ_D", &EmulateInstructionMIPS::Emulate_BNZD, "BNZ.d wt,s16"},
      {"BZ_B", &EmulateInstructionMIPS::Emulate_BZB, "BZ.b wt,s16"},
      {"BZ_H", &EmulateInstructionMIPS::Emulate_BZH, "BZ.h wt,s16"},
      {"BZ_W", &EmulateInstructionMIPS::Emulate_BZW, "BZ.w wt,s16"},
      {"BZ_D", &EmulateInstructionMIPS::Emulate_BZD, "BZ.d wt,s16"},
      {"BNZ_V", &EmulateInstructionMIPS::Emulate_BNZV, "BNZ.V wt,s16"},
      {"BZ_V", &EmulateInstructionMIPS::Emulate_BZV, "BZ.V wt,s16"},

      // MicroMIPS Branch instructions
      {"B16_MM", &EmulateInstructionMIPS::Emulate_B16_MM, "B16 offset"},
      {"BEQZ16_MM", &EmulateInstructionMIPS::Emulate_Branch_MM,
       "BEQZ16 rs, offset"},
      {"BNEZ16_MM", &EmulateInstructionMIPS::Emulate_Branch_MM,
       "BNEZ16 rs, offset"},
      {"BEQZC_MM", &EmulateInstructionMIPS::Emulate_Branch_MM,
       "BEQZC rs, offset"},
      {"BNEZC_MM", &EmulateInstructionMIPS::Emulate_Branch_MM,
       "BNEZC rs, offset"},
      {"BGEZALS_MM", &EmulateInstructionMIPS::Emulate_Branch_MM,
       "BGEZALS rs, offset"},
      {"BLTZALS_MM", &EmulateInstructionMIPS::Emulate_Branch_MM,
       "BLTZALS rs, offset"},
      {"JALR16_MM", &EmulateInstructionMIPS::Emulate_JALRx16_MM, "JALR16 rs"},
      {"JALRS16_MM", &EmulateInstructionMIPS::Emulate_JALRx16_MM, "JALRS16 rs"},
      {"JR16_MM", &EmulateInstructionMIPS::Emulate_JR, "JR16 rs rs"},
      {"JRC16_MM", &EmulateInstructionMIPS::Emulate_JR, "JRC16 rs rs"},
      {"JALS_MM", &EmulateInstructionMIPS::Emulate_JALx, "JALS target"},
      {"JALX_MM", &EmulateInstructionMIPS::Emulate_JALx, "JALX target"},
      {"JALRS_MM", &EmulateInstructionMIPS::Emulate_JALRS, "JALRS rt, rs"},
  };

  for (MipsOpcode &opcode : g_opcodes) {
    if (name.equals_insensitive(opcode.op_name))
      return &opcode;
  }
  return nullptr;
}

uint32_t
EmulateInstructionMIPS::GetSizeOfInstruction(lldb_private::DataExtractor &data,
                                             uint64_t inst_addr) {
  uint64_t next_inst_size = 0;
  llvm::MCInst mc_insn;
  llvm::MCDisassembler::DecodeStatus decode_status;
  llvm::ArrayRef<uint8_t> raw_insn(data.GetDataStart(), data.GetByteSize());

  if (m_use_alt_disaasm)
    decode_status = m_alt_disasm->getInstruction(
        mc_insn, next_inst_size, raw_insn, inst_addr, llvm::nulls());
  else
    decode_status = m_disasm->getInstruction(mc_insn, next_inst_size, raw_insn,
                                             inst_addr, llvm::nulls());

  if (decode_status != llvm::MCDisassembler::Success)
    return false;

  return m_insn_info->get(mc_insn.getOpcode()).getSize();
}

bool EmulateInstructionMIPS::SetInstruction(const Opcode &insn_opcode,
                                            const Address &inst_addr,
                                            Target *target) {
  m_use_alt_disaasm = false;

  if (EmulateInstruction::SetInstruction(insn_opcode, inst_addr, target)) {
    if (inst_addr.GetAddressClass() == AddressClass::eCodeAlternateISA) {
      Status error;
      lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;

      /*
       * The address belongs to microMIPS function. To find the size of
       * next instruction use microMIPS disassembler.
      */
      m_use_alt_disaasm = true;

      uint32_t current_inst_size = insn_opcode.GetByteSize();
      uint8_t buf[sizeof(uint32_t)];
      uint64_t next_inst_addr = (m_addr & (~1ull)) + current_inst_size;
      Address next_addr(next_inst_addr);

      const size_t bytes_read =
          target->ReadMemory(next_addr, /* Address of next instruction */
                             buf, sizeof(uint32_t), error, 
                             false,  /* force_live_memory */
                             &load_addr);

      if (bytes_read == 0)
        return true;

      DataExtractor data(buf, sizeof(uint32_t), GetByteOrder(),
                         GetAddressByteSize());
      m_next_inst_size = GetSizeOfInstruction(data, next_inst_addr);
      return true;
    } else {
      /*
       * If the address class is not AddressClass::eCodeAlternateISA then
       * the function is not microMIPS. In this case instruction size is
       * always 4 bytes.
      */
      m_next_inst_size = 4;
      return true;
    }
  }
  return false;
}

bool EmulateInstructionMIPS::ReadInstruction() {
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

bool EmulateInstructionMIPS::EvaluateInstruction(uint32_t evaluate_options) {
  bool success = false;
  llvm::MCInst mc_insn;
  uint64_t insn_size;
  DataExtractor data;

  /* Keep the complexity of the decode logic with the llvm::MCDisassembler
   * class. */
  if (m_opcode.GetData(data)) {
    llvm::MCDisassembler::DecodeStatus decode_status;
    llvm::ArrayRef<uint8_t> raw_insn(data.GetDataStart(), data.GetByteSize());
    if (m_use_alt_disaasm)
      decode_status = m_alt_disasm->getInstruction(mc_insn, insn_size, raw_insn,
                                                   m_addr, llvm::nulls());
    else
      decode_status = m_disasm->getInstruction(mc_insn, insn_size, raw_insn,
                                               m_addr, llvm::nulls());

    if (decode_status != llvm::MCDisassembler::Success)
      return false;
  }

  /*
   * mc_insn.getOpcode() returns decoded opcode. However to make use
   * of llvm::Mips::<insn> we would need "MipsGenInstrInfo.inc".
  */
  const char *op_name = m_insn_info->getName(mc_insn.getOpcode()).data();

  if (op_name == nullptr)
    return false;

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
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
    if (!success)
      return false;
  }

  /* emulate instruction */
  success = (this->*opcode_data->callback)(mc_insn);
  if (!success)
    return false;

  if (auto_advance_pc) {
    new_pc =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
    if (!success)
      return false;

    /* If we haven't changed the PC, change it here */
    if (old_pc == new_pc) {
      new_pc += 4;
      Context context;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                                 new_pc))
        return false;
    }
  }

  return true;
}

bool EmulateInstructionMIPS::CreateFunctionEntryUnwind(
    UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  const bool can_replace = false;

  // Our previous Call Frame Address is the stack pointer
  row->GetCFAValue().SetIsRegisterPlusOffset(dwarf_sp_mips, 0);

  // Our previous PC is in the RA
  row->SetRegisterLocationToRegister(dwarf_pc_mips, dwarf_ra_mips, can_replace);

  unwind_plan.AppendRow(row);

  // All other registers are the same.
  unwind_plan.SetSourceName("EmulateInstructionMIPS");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  unwind_plan.SetUnwindPlanForSignalTrap(eLazyBoolNo);
  unwind_plan.SetReturnAddressRegister(dwarf_ra_mips);

  return true;
}

bool EmulateInstructionMIPS::nonvolatile_reg_p(uint32_t regnum) {
  switch (regnum) {
  case dwarf_r16_mips:
  case dwarf_r17_mips:
  case dwarf_r18_mips:
  case dwarf_r19_mips:
  case dwarf_r20_mips:
  case dwarf_r21_mips:
  case dwarf_r22_mips:
  case dwarf_r23_mips:
  case dwarf_gp_mips:
  case dwarf_sp_mips:
  case dwarf_r30_mips:
  case dwarf_ra_mips:
    return true;
  default:
    return false;
  }
  return false;
}

bool EmulateInstructionMIPS::Emulate_ADDiu(llvm::MCInst &insn) {
  // ADDIU rt, rs, immediate
  // GPR[rt] <- GPR[rs] + sign_extend(immediate)

  uint8_t dst, src;
  bool success = false;
  const uint32_t imm16 = insn.getOperand(2).getImm();
  int64_t imm = SignedBits(imm16, 15, 0);

  dst = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  src = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  // If immediate value is greater then 2^16 - 1 then clang generate LUI,
  // ADDIU, SUBU instructions in prolog. Example lui    $1, 0x2 addiu $1, $1,
  // -0x5920 subu  $sp, $sp, $1 In this case, ADDIU dst and src will be same
  // and not equal to sp
  if (dst == src) {
    Context context;

    /* read <src> register */
    const int64_t src_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips + src, 0, &success);
    if (!success)
      return false;

    /* Check if this is daddiu sp, sp, imm16 */
    if (dst == dwarf_sp_mips) {
      uint64_t result = src_opd_val + imm;
      std::optional<RegisterInfo> reg_info_sp =
          GetRegisterInfo(eRegisterKindDWARF, dwarf_sp_mips);
      if (reg_info_sp)
        context.SetRegisterPlusOffset(*reg_info_sp, imm);

      /* We are allocating bytes on stack */
      context.type = eContextAdjustStackPointer;

      WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_sp_mips, result);
      return true;
    }

    imm += src_opd_val;
    context.SetImmediateSigned(imm);
    context.type = eContextImmediate;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF,
                               dwarf_zero_mips + dst, imm))
      return false;
  }

  return true;
}

bool EmulateInstructionMIPS::Emulate_SW(llvm::MCInst &insn) {
  bool success = false;
  uint32_t imm16 = insn.getOperand(2).getImm();
  uint32_t imm = SignedBits(imm16, 15, 0);
  uint32_t src, base;
  int32_t address;
  Context bad_vaddr_context;

  src = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  base = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  std::optional<RegisterInfo> reg_info_base =
      GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + base);
  if (!reg_info_base)
    return false;

  /* read base register */
  address = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                          dwarf_zero_mips + base, 0, &success);
  if (!success)
    return false;

  /* destination address */
  address = address + imm;

  /* Set the bad_vaddr register with base address used in the instruction */
  bad_vaddr_context.type = eContextInvalid;
  WriteRegisterUnsigned(bad_vaddr_context, eRegisterKindDWARF, dwarf_bad_mips,
                        address);

  /* We look for sp based non-volatile register stores */
  if (nonvolatile_reg_p(src)) {
    std::optional<RegisterInfo> reg_info_src =
        GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + src);
    if (!reg_info_src)
      return false;

    Context context;
    context.type = eContextPushRegisterOnStack;
    context.SetRegisterToRegisterPlusOffset(*reg_info_src, *reg_info_base, 0);

    RegisterValue::BytesContainer buffer(reg_info_src->byte_size);
    Status error;

    std::optional<RegisterValue> data_src = ReadRegister(*reg_info_base);
    if (!data_src)
      return false;

    if (data_src->GetAsMemoryData(*reg_info_src, buffer.data(),
                                  reg_info_src->byte_size, eByteOrderLittle,
                                  error) == 0)
      return false;

    if (!WriteMemory(context, address, buffer.data(), reg_info_src->byte_size))
      return false;

    return true;
  }

  return false;
}

bool EmulateInstructionMIPS::Emulate_LW(llvm::MCInst &insn) {
  bool success = false;
  uint32_t src, base;
  int32_t imm, address;
  Context bad_vaddr_context;

  src = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  base = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());
  imm = insn.getOperand(2).getImm();

  if (GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + base))
    return false;

  /* read base register */
  address = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                          dwarf_zero_mips + base, 0, &success);
  if (!success)
    return false;

  /* destination address */
  address = address + imm;

  /* Set the bad_vaddr register with base address used in the instruction */
  bad_vaddr_context.type = eContextInvalid;
  WriteRegisterUnsigned(bad_vaddr_context, eRegisterKindDWARF, dwarf_bad_mips,
                        address);

  if (nonvolatile_reg_p(src)) {
    RegisterValue data_src;
    std::optional<RegisterInfo> reg_info_src =
        GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + src);
    if (!reg_info_src)
      return false;

    Context context;
    context.type = eContextPopRegisterOffStack;
    context.SetAddress(address);

    return WriteRegister(context, *reg_info_src, data_src);
  }

  return false;
}

bool EmulateInstructionMIPS::Emulate_SUBU_ADDU(llvm::MCInst &insn) {
  // SUBU sp, <src>, <rt>
  // ADDU sp, <src>, <rt>
  // ADDU dst, sp, <rt>

  bool success = false;
  uint64_t result;
  uint8_t src, dst, rt;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  dst = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  src = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  /* Check if sp is destination register */
  if (dst == dwarf_sp_mips) {
    rt = m_reg_info->getEncodingValue(insn.getOperand(2).getReg());

    /* read <src> register */
    uint64_t src_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips + src, 0, &success);
    if (!success)
      return false;

    /* read <rt > register */
    uint64_t rt_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips + rt, 0, &success);
    if (!success)
      return false;

    if (op_name.equals_insensitive("SUBU"))
      result = src_opd_val - rt_opd_val;
    else
      result = src_opd_val + rt_opd_val;

    Context context;
    std::optional<RegisterInfo> reg_info_sp =
        GetRegisterInfo(eRegisterKindDWARF, dwarf_sp_mips);
    if (reg_info_sp)
      context.SetRegisterPlusOffset(*reg_info_sp, rt_opd_val);

    /* We are allocating bytes on stack */
    context.type = eContextAdjustStackPointer;

    WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_sp_mips, result);

    return true;
  } else if (src == dwarf_sp_mips) {
    rt = m_reg_info->getEncodingValue(insn.getOperand(2).getReg());

    /* read <src> register */
    uint64_t src_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips + src, 0, &success);
    if (!success)
      return false;

    /* read <rt> register */
    uint64_t rt_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips + rt, 0, &success);
    if (!success)
      return false;

    Context context;

    if (op_name.equals_insensitive("SUBU"))
      result = src_opd_val - rt_opd_val;
    else
      result = src_opd_val + rt_opd_val;

    context.SetImmediateSigned(result);
    context.type = eContextImmediate;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF,
                               dwarf_zero_mips + dst, result))
      return false;
  }

  return true;
}

bool EmulateInstructionMIPS::Emulate_LUI(llvm::MCInst &insn) {
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
                               dwarf_zero_mips + rt, imm);
}

bool EmulateInstructionMIPS::Emulate_ADDIUSP(llvm::MCInst &insn) {
  bool success = false;
  const uint32_t imm9 = insn.getOperand(0).getImm();
  uint64_t result;

  // This instruction operates implicitly on stack pointer, so read <sp>
  // register.
  uint64_t src_opd_val =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_sp_mips, 0, &success);
  if (!success)
    return false;

  result = src_opd_val + imm9;

  Context context;
  std::optional<RegisterInfo> reg_info_sp =
      GetRegisterInfo(eRegisterKindDWARF, dwarf_sp_mips);
  if (reg_info_sp)
    context.SetRegisterPlusOffset(*reg_info_sp, imm9);

  // We are adjusting the stack.
  context.type = eContextAdjustStackPointer;

  WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_sp_mips, result);
  return true;
}

bool EmulateInstructionMIPS::Emulate_ADDIUS5(llvm::MCInst &insn) {
  bool success = false;
  uint32_t base;
  const uint32_t imm4 = insn.getOperand(2).getImm();
  uint64_t result;

  // The source and destination register is same for this instruction.
  base = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());

  // We are looking for stack adjustment only
  if (base == dwarf_sp_mips) {
    // Read stack pointer register
    uint64_t src_opd_val = ReadRegisterUnsigned(
        eRegisterKindDWARF, dwarf_zero_mips + base, 0, &success);
    if (!success)
      return false;

    result = src_opd_val + imm4;

    Context context;
    std::optional<RegisterInfo> reg_info_sp =
        GetRegisterInfo(eRegisterKindDWARF, dwarf_sp_mips);
    if (reg_info_sp)
      context.SetRegisterPlusOffset(*reg_info_sp, imm4);

    // We are adjusting the stack.
    context.type = eContextAdjustStackPointer;

    WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_sp_mips, result);
  }

  return true;
}

bool EmulateInstructionMIPS::Emulate_SWSP(llvm::MCInst &insn) {
  bool success = false;
  uint32_t imm5 = insn.getOperand(2).getImm();
  uint32_t src, base;
  Context bad_vaddr_context;
  uint32_t address;

  src = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  base = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  std::optional<RegisterInfo> reg_info_base =
      GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + base);
  if (!reg_info_base)
    return false;

  // read base register
  address = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips + base, 0,
                                 &success);
  if (!success)
    return false;

  // destination address
  address = address + imm5;

  // We use bad_vaddr_context to store base address which is used by H/W
  // watchpoint Set the bad_vaddr register with base address used in the
  // instruction
  bad_vaddr_context.type = eContextInvalid;
  WriteRegisterUnsigned(bad_vaddr_context, eRegisterKindDWARF, dwarf_bad_mips,
                        address);

  // We look for sp based non-volatile register stores.
  if (base == dwarf_sp_mips && nonvolatile_reg_p(src)) {
    RegisterInfo reg_info_src = {};
    Context context;
    context.type = eContextPushRegisterOnStack;
    context.SetRegisterToRegisterPlusOffset(reg_info_src, *reg_info_base, 0);

    RegisterValue::BytesContainer buffer(reg_info_src.byte_size);
    Status error;

    std::optional<RegisterValue> data_src = ReadRegister(*reg_info_base);
    if (!data_src)
      return false;

    if (data_src->GetAsMemoryData(reg_info_src, buffer.data(),
                                  reg_info_src.byte_size, eByteOrderLittle,
                                  error) == 0)
      return false;

    if (!WriteMemory(context, address, buffer.data(), reg_info_src.byte_size))
      return false;

    return true;
  }

  return false;
}

/* Emulate SWM16,SWM32 and SWP instruction.

   SWM16 always has stack pointer as a base register (but it is still available
   in MCInst as an operand).
   SWM32 and SWP can have base register other than stack pointer.
*/
bool EmulateInstructionMIPS::Emulate_SWM16_32(llvm::MCInst &insn) {
  bool success = false;
  uint32_t src, base;
  uint32_t num_operands = insn.getNumOperands(); // No of operands vary based on
                                                 // no of regs to store.

  // Base register is second last operand of the instruction.
  base =
      m_reg_info->getEncodingValue(insn.getOperand(num_operands - 2).getReg());

  // We are looking for sp based stores so if base is not a stack pointer then
  // don't proceed.
  if (base != dwarf_sp_mips)
    return false;

  // offset is always the last operand.
  uint32_t offset = insn.getOperand(num_operands - 1).getImm();

  std::optional<RegisterInfo> reg_info_base =
      GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + base);
  if (!reg_info_base)
    return false;

  // read SP
  uint32_t base_address = ReadRegisterUnsigned(
      eRegisterKindDWARF, dwarf_zero_mips + base, 0, &success);
  if (!success)
    return false;

  // Resulting base addrss
  base_address = base_address + offset;

  // Total no of registers to be stored are num_operands-2.
  for (uint32_t i = 0; i < num_operands - 2; i++) {
    // Get the register number to be stored.
    src = m_reg_info->getEncodingValue(insn.getOperand(i).getReg());

    /*
        Record only non-volatile stores.
        This check is required for SWP instruction because source operand could
       be any register.
        SWM16 and SWM32 instruction always has saved registers as source
       operands.
    */
    if (!nonvolatile_reg_p(src))
      return false;

    std::optional<RegisterInfo> reg_info_src =
        GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + src);
    if (!reg_info_src)
      return false;

    Context context;
    context.type = eContextPushRegisterOnStack;
    context.SetRegisterToRegisterPlusOffset(*reg_info_src, *reg_info_base, 0);

    RegisterValue::BytesContainer buffer(reg_info_src->byte_size);
    Status error;

    std::optional<RegisterValue> data_src = ReadRegister(*reg_info_base);
    if (!data_src)
      return false;

    if (data_src->GetAsMemoryData(*reg_info_src, buffer.data(),
                                  reg_info_src->byte_size, eByteOrderLittle,
                                  error) == 0)
      return false;

    if (!WriteMemory(context, base_address, buffer.data(),
                     reg_info_src->byte_size))
      return false;

    // Stack address for next register
    base_address = base_address + reg_info_src->byte_size;
  }
  return true;
}

bool EmulateInstructionMIPS::Emulate_LWSP(llvm::MCInst &insn) {
  bool success = false;
  uint32_t src = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  uint32_t base = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());
  uint32_t imm5 = insn.getOperand(2).getImm();
  Context bad_vaddr_context;

  if (!GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + base))
    return false;

  // read base register
  uint32_t base_address = ReadRegisterUnsigned(
      eRegisterKindDWARF, dwarf_zero_mips + base, 0, &success);
  if (!success)
    return false;

  base_address = base_address + imm5;

  // We use bad_vaddr_context to store base address which is used by H/W
  // watchpoint Set the bad_vaddr register with base address used in the
  // instruction
  bad_vaddr_context.type = eContextInvalid;
  WriteRegisterUnsigned(bad_vaddr_context, eRegisterKindDWARF, dwarf_bad_mips,
                        base_address);

  if (base == dwarf_sp_mips && nonvolatile_reg_p(src)) {
    RegisterValue data_src;
    std::optional<RegisterInfo> reg_info_src =
        GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + src);
    if (!reg_info_src)
      return false;

    Context context;
    context.type = eContextPopRegisterOffStack;
    context.SetAddress(base_address);

    return WriteRegister(context, *reg_info_src, data_src);
  }

  return false;
}

/* Emulate LWM16, LWM32 and LWP instructions.

   LWM16 always has stack pointer as a base register (but it is still available
   in MCInst as an operand).
   LWM32 and LWP can have base register other than stack pointer.
*/
bool EmulateInstructionMIPS::Emulate_LWM16_32(llvm::MCInst &insn) {
  bool success = false;
  uint32_t dst, base;
  uint32_t num_operands = insn.getNumOperands(); // No of operands vary based on
                                                 // no of regs to store.
  uint32_t imm = insn.getOperand(num_operands - 1)
                     .getImm(); // imm is the last operand in the instruction.

  // Base register is second last operand of the instruction.
  base =
      m_reg_info->getEncodingValue(insn.getOperand(num_operands - 2).getReg());

  // We are looking for sp based loads so if base is not a stack pointer then
  // don't proceed.
  if (base != dwarf_sp_mips)
    return false;

  uint32_t base_address = ReadRegisterUnsigned(
      eRegisterKindDWARF, dwarf_zero_mips + base, 0, &success);
  if (!success)
    return false;

  base_address = base_address + imm;

  RegisterValue data_dst;

  // Total no of registers to be re-stored are num_operands-2.
  for (uint32_t i = 0; i < num_operands - 2; i++) {
    // Get the register number to be re-stored.
    dst = m_reg_info->getEncodingValue(insn.getOperand(i).getReg());

    /*
        Record only non-volatile loads.
        This check is required for LWP instruction because destination operand
       could be any register.
        LWM16 and LWM32 instruction always has saved registers as destination
       operands.
    */
    if (!nonvolatile_reg_p(dst))
      return false;

    std::optional<RegisterInfo> reg_info_dst =
        GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + dst);
    if (!reg_info_dst)
      return false;

    Context context;
    context.type = eContextPopRegisterOffStack;
    context.SetAddress(base_address + (i * 4));

    if (!WriteRegister(context, *reg_info_dst, data_dst))
      return false;
  }

  return true;
}

bool EmulateInstructionMIPS::Emulate_JRADDIUSP(llvm::MCInst &insn) {
  bool success = false;
  int32_t imm5 = insn.getOperand(0).getImm();

  /* JRADDIUSP immediate
  *       PC <- RA
  *       SP <- SP + zero_extend(Immediate << 2)
  */

  // This instruction operates implicitly on stack pointer, so read <sp>
  // register.
  int32_t src_opd_val =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_sp_mips, 0, &success);
  if (!success)
    return false;

  int32_t ra_val =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_ra_mips, 0, &success);
  if (!success)
    return false;

  int32_t result = src_opd_val + imm5;

  Context context;

  // Update the PC
  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             ra_val))
    return false;

  std::optional<RegisterInfo> reg_info_sp =
      GetRegisterInfo(eRegisterKindDWARF, dwarf_sp_mips);
  if (reg_info_sp)
    context.SetRegisterPlusOffset(*reg_info_sp, imm5);

  // We are adjusting stack
  context.type = eContextAdjustStackPointer;

  // update SP
  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_sp_mips,
                               result);
}

static int IsAdd64bitOverflow(int32_t a, int32_t b) {
  int32_t r = (uint32_t)a + (uint32_t)b;
  return (a < 0 && b < 0 && r >= 0) || (a >= 0 && b >= 0 && r < 0);
}

/*
    Emulate below MIPS branch instructions.
    BEQ, BNE : Branch on condition
    BEQL, BNEL : Branch likely
*/
bool EmulateInstructionMIPS::Emulate_BXX_3ops(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs, rt;
  int32_t offset, pc, target = 0, rs_val, rt_val;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  rt = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());
  offset = insn.getOperand(2).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  rs_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rs, 0, &success);
  if (!success)
    return false;

  rt_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rt, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BEQ") || op_name.equals_insensitive("BEQL")) {
    if (rs_val == rt_val)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BNE") ||
             op_name.equals_insensitive("BNEL")) {
    if (rs_val != rt_val)
      target = pc + offset;
    else
      target = pc + 8;
  }

  Context context;
  context.type = eContextRelativeBranchImmediate;
  context.SetImmediate(offset);

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

/*
    Emulate below MIPS branch instructions.
    BEQC, BNEC, BLTC, BGEC, BLTUC, BGEUC, BOVC, BNVC: Compact branch
   instructions with no delay slot
*/
bool EmulateInstructionMIPS::Emulate_BXX_3ops_C(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs, rt;
  int32_t offset, pc, target = 0, rs_val, rt_val;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());
  uint32_t current_inst_size = m_insn_info->get(insn.getOpcode()).getSize();

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  rt = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());
  offset = insn.getOperand(2).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  rs_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rs, 0, &success);
  if (!success)
    return false;

  rt_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rt, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BEQC")) {
    if (rs_val == rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BNEC")) {
    if (rs_val != rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BLTC")) {
    if (rs_val < rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGEC")) {
    if (rs_val >= rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BLTUC")) {
    if (rs_val < rt_val)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGEUC")) {
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

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

/*
    Emulate below MIPS conditional branch and link instructions.
    BLEZALC, BGEZALC, BLTZALC, BGTZALC, BEQZALC, BNEZALC : Compact branches
*/
bool EmulateInstructionMIPS::Emulate_Bcond_Link_C(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  int32_t offset, pc, target = 0;
  int32_t rs_val;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  rs_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rs, 0, &success);
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

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips,
                             pc + 4))
    return false;

  return true;
}

/*
    Emulate below MIPS Non-Compact conditional branch and link instructions.
    BLTZAL, BGEZAL      :
    BLTZALL, BGEZALL    : Branch likely
*/
bool EmulateInstructionMIPS::Emulate_Bcond_Link(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  int32_t offset, pc, target = 0;
  int32_t rs_val;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  rs_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rs, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BLTZAL") ||
      op_name.equals_insensitive("BLTZALL")) {
    if ((int32_t)rs_val < 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BGEZAL") ||
             op_name.equals_insensitive("BGEZALL")) {
    if ((int32_t)rs_val >= 0)
      target = pc + offset;
    else
      target = pc + 8;
  }

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips,
                             pc + 8))
    return false;

  return true;
}

/*
    Emulate below MIPS branch instructions.
    BLTZL, BGEZL, BGTZL, BLEZL : Branch likely
    BLTZ, BGEZ, BGTZ, BLEZ     : Non-compact branches
*/
bool EmulateInstructionMIPS::Emulate_BXX_2ops(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  int32_t offset, pc, target = 0;
  int32_t rs_val;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  rs_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rs, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BLTZL") ||
      op_name.equals_insensitive("BLTZ")) {
    if (rs_val < 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BGEZL") ||
             op_name.equals_insensitive("BGEZ")) {
    if (rs_val >= 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BGTZL") ||
             op_name.equals_insensitive("BGTZ")) {
    if (rs_val > 0)
      target = pc + offset;
    else
      target = pc + 8;
  } else if (op_name.equals_insensitive("BLEZL") ||
             op_name.equals_insensitive("BLEZ")) {
    if (rs_val <= 0)
      target = pc + offset;
    else
      target = pc + 8;
  }

  Context context;
  context.type = eContextRelativeBranchImmediate;
  context.SetImmediate(offset);

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

/*
    Emulate below MIPS branch instructions.
    BLTZC, BLEZC, BGEZC, BGTZC, BEQZC, BNEZC : Compact Branches
*/
bool EmulateInstructionMIPS::Emulate_BXX_2ops_C(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  int32_t offset, pc, target = 0;
  int32_t rs_val;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());
  uint32_t current_inst_size = m_insn_info->get(insn.getOpcode()).getSize();

  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  rs_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rs, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BLTZC")) {
    if (rs_val < 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BLEZC")) {
    if (rs_val <= 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGEZC")) {
    if (rs_val >= 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BGTZC")) {
    if (rs_val > 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BEQZC")) {
    if (rs_val == 0)
      target = pc + offset;
    else
      target = pc + 4;
  } else if (op_name.equals_insensitive("BNEZC")) {
    if (rs_val != 0)
      target = pc + offset;
    else
      target = pc + 4;
  }

  Context context;
  context.type = eContextRelativeBranchImmediate;
  context.SetImmediate(current_inst_size + offset);

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

bool EmulateInstructionMIPS::Emulate_B16_MM(llvm::MCInst &insn) {
  bool success = false;
  int32_t offset, pc, target;
  uint32_t current_inst_size = m_insn_info->get(insn.getOpcode()).getSize();

  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  // unconditional branch
  target = pc + offset;

  Context context;
  context.type = eContextRelativeBranchImmediate;
  context.SetImmediate(current_inst_size + offset);

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

/*
   BEQZC, BNEZC are 32 bit compact instructions without a delay slot.
   BEQZ16, BNEZ16 are 16 bit instructions with delay slot.
   BGEZALS, BLTZALS are 16 bit instructions with short (2-byte) delay slot.
*/
bool EmulateInstructionMIPS::Emulate_Branch_MM(llvm::MCInst &insn) {
  bool success = false;
  int32_t target = 0;
  uint32_t current_inst_size = m_insn_info->get(insn.getOpcode()).getSize();
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());
  bool update_ra = false;
  uint32_t ra_offset = 0;

  /*
   * BEQZ16 rs, offset
   *      condition <- (GPR[rs] = 0)
   *      if condition then
   *          PC = PC + sign_ext (offset || 0)
   *
   * BNEZ16 rs, offset
   *      condition <- (GPR[rs] != 0)
   *      if condition then
   *          PC = PC + sign_ext (offset || 0)
   *
   * BEQZC rs, offset     (compact instruction: No delay slot)
   *      condition <- (GPR[rs] == 0)
   *      if condition then
   *         PC = PC + 4 + sign_ext (offset || 0)
  */

  uint32_t rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  int32_t offset = insn.getOperand(1).getImm();

  int32_t pc =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  int32_t rs_val = (int32_t)ReadRegisterUnsigned(
      eRegisterKindDWARF, dwarf_zero_mips + rs, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("BEQZ16_MM")) {
    if (rs_val == 0)
      target = pc + offset;
    else
      target = pc + current_inst_size +
               m_next_inst_size; // Skip delay slot instruction.
  } else if (op_name.equals_insensitive("BNEZ16_MM")) {
    if (rs_val != 0)
      target = pc + offset;
    else
      target = pc + current_inst_size +
               m_next_inst_size; // Skip delay slot instruction.
  } else if (op_name.equals_insensitive("BEQZC_MM")) {
    if (rs_val == 0)
      target = pc + 4 + offset;
    else
      target =
          pc +
          4; // 32 bit instruction and does not have delay slot instruction.
  } else if (op_name.equals_insensitive("BNEZC_MM")) {
    if (rs_val != 0)
      target = pc + 4 + offset;
    else
      target =
          pc +
          4; // 32 bit instruction and does not have delay slot instruction.
  } else if (op_name.equals_insensitive("BGEZALS_MM")) {
    if (rs_val >= 0)
      target = pc + offset;
    else
      target = pc + 6; // 32 bit instruction with short (2-byte) delay slot

    update_ra = true;
    ra_offset = 6;
  } else if (op_name.equals_insensitive("BLTZALS_MM")) {
    if (rs_val >= 0)
      target = pc + offset;
    else
      target = pc + 6; // 32 bit instruction with short (2-byte) delay slot

    update_ra = true;
    ra_offset = 6;
  }

  Context context;
  context.type = eContextRelativeBranchImmediate;
  context.SetImmediate(current_inst_size + offset);

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             target))
    return false;

  if (update_ra) {
    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips,
                               pc + ra_offset))
      return false;
  }
  return true;
}

/* Emulate micromips jump instructions.
   JALR16,JALRS16
*/
bool EmulateInstructionMIPS::Emulate_JALRx16_MM(llvm::MCInst &insn) {
  bool success = false;
  uint32_t ra_offset = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  uint32_t rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());

  uint32_t pc =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  uint32_t rs_val = ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rs, 0, &success);
  if (!success)
    return false;

  if (op_name.equals_insensitive("JALR16_MM"))
    ra_offset = 6; // 2-byte instruction with 4-byte delay slot.
  else if (op_name.equals_insensitive("JALRS16_MM"))
    ra_offset = 4; // 2-byte instruction with 2-byte delay slot.

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             rs_val))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips,
                             pc + ra_offset))
    return false;

  return true;
}

/* Emulate JALS and JALX instructions.
    JALS 32 bit instruction with short (2-byte) delay slot.
    JALX 32 bit instruction with 4-byte delay slot.
*/
bool EmulateInstructionMIPS::Emulate_JALx(llvm::MCInst &insn) {
  bool success = false;
  uint32_t offset = 0, target = 0, pc = 0, ra_offset = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  /*
   * JALS target
   *      RA = PC + 6
   *      offset = sign_ext (offset << 1)
   *      PC = PC[31-27] | offset
   * JALX target
   *      RA = PC + 8
   *      offset = sign_ext (offset << 2)
   *      PC = PC[31-28] | offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  // These are PC-region branches and not PC-relative.
  if (op_name.equals_insensitive("JALS_MM")) {
    // target address is in the “current” 128 MB-aligned region
    target = (pc & 0xF8000000UL) | offset;
    ra_offset = 6;
  } else if (op_name.equals_insensitive("JALX_MM")) {
    // target address is in the “current” 256 MB-aligned region
    target = (pc & 0xF0000000UL) | offset;
    ra_offset = 8;
  }

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips,
                             pc + ra_offset))
    return false;

  return true;
}

bool EmulateInstructionMIPS::Emulate_JALRS(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs = 0, rt = 0;
  int32_t pc = 0, rs_val = 0;

  /*
      JALRS rt, rs
          GPR[rt] <- PC + 6
          PC <- GPR[rs]
  */

  rt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  rs = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  rs_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rs, 0, &success);
  if (!success)
    return false;

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             rs_val))
    return false;

  // This is 4-byte instruction with 2-byte delay slot.
  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_zero_mips + rt,
                             pc + 6))
    return false;

  return true;
}

bool EmulateInstructionMIPS::Emulate_BAL(llvm::MCInst &insn) {
  bool success = false;
  int32_t offset, pc, target;

  /*
   * BAL offset
   *      offset = sign_ext (offset << 2)
   *      RA = PC + 8
   *      PC = PC + offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  target = pc + offset;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips,
                             pc + 8))
    return false;

  return true;
}

bool EmulateInstructionMIPS::Emulate_BALC(llvm::MCInst &insn) {
  bool success = false;
  int32_t offset, pc, target;

  /*
   * BALC offset
   *      offset = sign_ext (offset << 2)
   *      RA = PC + 4
   *      PC = PC + 4 + offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  target = pc + offset;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips,
                             pc + 4))
    return false;

  return true;
}

bool EmulateInstructionMIPS::Emulate_BC(llvm::MCInst &insn) {
  bool success = false;
  int32_t offset, pc, target;

  /*
   * BC offset
   *      offset = sign_ext (offset << 2)
   *      PC = PC + 4 + offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  target = pc + offset;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

bool EmulateInstructionMIPS::Emulate_J(llvm::MCInst &insn) {
  bool success = false;
  uint32_t offset, pc;

  /*
   * J offset
   *      offset = sign_ext (offset << 2)
   *      PC = PC[63-28] | offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  /* This is a PC-region branch and not PC-relative */
  pc = (pc & 0xF0000000UL) | offset;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips, pc);
}

bool EmulateInstructionMIPS::Emulate_JAL(llvm::MCInst &insn) {
  bool success = false;
  uint32_t offset, target, pc;

  /*
   * JAL offset
   *      offset = sign_ext (offset << 2)
   *      PC = PC[63-28] | offset
  */
  offset = insn.getOperand(0).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  /* This is a PC-region branch and not PC-relative */
  target = (pc & 0xF0000000UL) | offset;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips,
                             pc + 8))
    return false;

  return true;
}

bool EmulateInstructionMIPS::Emulate_JALR(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs, rt;
  uint32_t pc, rs_val;

  /*
   * JALR rt, rs
   *      GPR[rt] = PC + 8
   *      PC = GPR[rs]
  */
  rt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  rs = m_reg_info->getEncodingValue(insn.getOperand(1).getReg());

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  rs_val = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips + rs, 0,
                                &success);
  if (!success)
    return false;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             rs_val))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_zero_mips + rt,
                             pc + 8))
    return false;

  return true;
}

bool EmulateInstructionMIPS::Emulate_JIALC(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rt;
  int32_t target, offset, pc, rt_val;

  /*
   * JIALC rt, offset
   *      offset = sign_ext (offset)
   *      PC = GPR[rt] + offset
   *      RA = PC + 4
  */
  rt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  rt_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rt, 0, &success);
  if (!success)
    return false;

  target = rt_val + offset;

  Context context;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                             target))
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_ra_mips,
                             pc + 4))
    return false;

  return true;
}

bool EmulateInstructionMIPS::Emulate_JIC(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rt;
  int32_t target, offset, rt_val;

  /*
   * JIC rt, offset
   *      offset = sign_ext (offset)
   *      PC = GPR[rt] + offset
  */
  rt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  rt_val = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                         dwarf_zero_mips + rt, 0, &success);
  if (!success)
    return false;

  target = rt_val + offset;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

bool EmulateInstructionMIPS::Emulate_JR(llvm::MCInst &insn) {
  bool success = false;
  uint32_t rs;
  uint32_t rs_val;

  /*
   * JR rs
   *      PC = GPR[rs]
  */
  rs = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());

  rs_val = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips + rs, 0,
                                &success);
  if (!success)
    return false;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               rs_val);
}

/*
    Emulate Branch on FP True/False
    BC1F, BC1FL :   Branch on FP False (L stands for branch likely)
    BC1T, BC1TL :   Branch on FP True  (L stands for branch likely)
*/
bool EmulateInstructionMIPS::Emulate_FP_branch(llvm::MCInst &insn) {
  bool success = false;
  uint32_t cc, fcsr;
  int32_t pc, offset, target = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  cc = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  fcsr = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_fcsr_mips, 0, &success);
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

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

bool EmulateInstructionMIPS::Emulate_BC1EQZ(llvm::MCInst &insn) {
  bool success = false;
  uint32_t ft;
  uint32_t ft_val;
  int32_t target, pc, offset;

  /*
   * BC1EQZ ft, offset
   *  condition <- (FPR[ft].bit0 == 0)
   *      if condition then
   *          offset = sign_ext (offset)
   *          PC = PC + 4 + offset
  */
  ft = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  ft_val = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips + ft, 0,
                                &success);
  if (!success)
    return false;

  if ((ft_val & 1) == 0)
    target = pc + 4 + offset;
  else
    target = pc + 8;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

bool EmulateInstructionMIPS::Emulate_BC1NEZ(llvm::MCInst &insn) {
  bool success = false;
  uint32_t ft;
  uint32_t ft_val;
  int32_t target, pc, offset;

  /*
   * BC1NEZ ft, offset
   *  condition <- (FPR[ft].bit0 != 0)
   *      if condition then
   *          offset = sign_ext (offset)
   *          PC = PC + 4 + offset
  */
  ft = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  ft_val = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_zero_mips + ft, 0,
                                &success);
  if (!success)
    return false;

  if ((ft_val & 1) != 0)
    target = pc + 4 + offset;
  else
    target = pc + 8;

  Context context;

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

/*
    Emulate MIPS-3D Branch instructions
    BC1ANY2F, BC1ANY2T  : Branch on Any of Two Floating Point Condition Codes
   False/True
    BC1ANY4F, BC1ANY4T  : Branch on Any of Four Floating Point Condition Codes
   False/True
*/
bool EmulateInstructionMIPS::Emulate_3D_branch(llvm::MCInst &insn) {
  bool success = false;
  uint32_t cc, fcsr;
  int32_t pc, offset, target = 0;
  llvm::StringRef op_name = m_insn_info->getName(insn.getOpcode());

  cc = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  offset = insn.getOperand(1).getImm();

  pc = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  fcsr = (uint32_t)ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_fcsr_mips, 0,
                                        &success);
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

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

bool EmulateInstructionMIPS::Emulate_BNZB(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 1, true);
}

bool EmulateInstructionMIPS::Emulate_BNZH(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 2, true);
}

bool EmulateInstructionMIPS::Emulate_BNZW(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 4, true);
}

bool EmulateInstructionMIPS::Emulate_BNZD(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 8, true);
}

bool EmulateInstructionMIPS::Emulate_BZB(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 1, false);
}

bool EmulateInstructionMIPS::Emulate_BZH(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 2, false);
}

bool EmulateInstructionMIPS::Emulate_BZW(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 4, false);
}

bool EmulateInstructionMIPS::Emulate_BZD(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_DF(insn, 8, false);
}

bool EmulateInstructionMIPS::Emulate_MSA_Branch_DF(llvm::MCInst &insn,
                                                   int element_byte_size,
                                                   bool bnz) {
  bool success = false, branch_hit = true;
  int32_t target = 0;
  RegisterValue reg_value;
  const uint8_t *ptr = nullptr;

  uint32_t wt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  int32_t offset = insn.getOperand(1).getImm();

  int32_t pc =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  if (ReadRegister(eRegisterKindDWARF, dwarf_w0_mips + wt, reg_value))
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

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

bool EmulateInstructionMIPS::Emulate_BNZV(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_V(insn, true);
}

bool EmulateInstructionMIPS::Emulate_BZV(llvm::MCInst &insn) {
  return Emulate_MSA_Branch_V(insn, false);
}

bool EmulateInstructionMIPS::Emulate_MSA_Branch_V(llvm::MCInst &insn,
                                                  bool bnz) {
  bool success = false;
  int32_t target = 0;
  llvm::APInt wr_val = llvm::APInt::getZero(128);
  llvm::APInt fail_value = llvm::APInt::getMaxValue(128);
  llvm::APInt zero_value = llvm::APInt::getZero(128);
  RegisterValue reg_value;

  uint32_t wt = m_reg_info->getEncodingValue(insn.getOperand(0).getReg());
  int32_t offset = insn.getOperand(1).getImm();

  int32_t pc =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc_mips, 0, &success);
  if (!success)
    return false;

  if (ReadRegister(eRegisterKindDWARF, dwarf_w0_mips + wt, reg_value))
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

  return WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc_mips,
                               target);
}

bool EmulateInstructionMIPS::Emulate_LDST_Imm(llvm::MCInst &insn) {
  bool success = false;
  uint32_t base;
  int32_t imm, address;
  Context bad_vaddr_context;

  uint32_t num_operands = insn.getNumOperands();
  base =
      m_reg_info->getEncodingValue(insn.getOperand(num_operands - 2).getReg());
  imm = insn.getOperand(num_operands - 1).getImm();

  if (!GetRegisterInfo(eRegisterKindDWARF, dwarf_zero_mips + base))
    return false;

  /* read base register */
  address = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                          dwarf_zero_mips + base, 0, &success);
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

bool EmulateInstructionMIPS::Emulate_LDST_Reg(llvm::MCInst &insn) {
  bool success = false;
  uint32_t base, index;
  int32_t address, index_address;
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
  address = (int32_t)ReadRegisterUnsigned(eRegisterKindDWARF,
                                          dwarf_zero_mips + base, 0, &success);
  if (!success)
    return false;

  /* read index register */
  index_address = (int32_t)ReadRegisterUnsigned(
      eRegisterKindDWARF, dwarf_zero_mips + index, 0, &success);
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
