//===-- PPCMCTargetDesc.h - PowerPC Target Descriptions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides PowerPC specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCTARGETDESC_H
#define LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCTARGETDESC_H

// GCC #defines PPC on Linux but we use it as our namespace name
#undef PPC

#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrDesc;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;

namespace PPC {
/// stripRegisterPrefix - This method strips the character prefix from a
/// register name so that only the number is left.  Used by for linux asm.
const char *stripRegisterPrefix(const char *RegName);

/// getRegNumForOperand - some operands use different numbering schemes
/// for the same registers. For example, a VSX instruction may have any of
/// vs0-vs63 allocated whereas an Altivec instruction could only have
/// vs32-vs63 allocated (numbered as v0-v31). This function returns the actual
/// register number needed for the opcode/operand number combination.
/// The operand number argument will be useful when we need to extend this
/// to instructions that use both Altivec and VSX numbering (for different
/// operands).
unsigned getRegNumForOperand(const MCInstrDesc &Desc, unsigned Reg,
                             unsigned OpNo);

} // namespace PPC

MCCodeEmitter *createPPCMCCodeEmitter(const MCInstrInfo &MCII,
                                      MCContext &Ctx);

MCAsmBackend *createPPCAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                  const MCRegisterInfo &MRI,
                                  const MCTargetOptions &Options);

/// Construct an PPC ELF object writer.
std::unique_ptr<MCObjectTargetWriter> createPPCELFObjectWriter(bool Is64Bit,
                                                               uint8_t OSABI);
/// Construct a PPC Mach-O object writer.
std::unique_ptr<MCObjectTargetWriter>
createPPCMachObjectWriter(bool Is64Bit, uint32_t CPUType, uint32_t CPUSubtype);

/// Construct a PPC XCOFF object writer.
std::unique_ptr<MCObjectTargetWriter> createPPCXCOFFObjectWriter(bool Is64Bit);

/// Returns true iff Val consists of one contiguous run of 1s with any number of
/// 0s on either side.  The 1s are allowed to wrap from LSB to MSB, so
/// 0x000FFF0, 0x0000FFFF, and 0xFF0000FF are all runs.  0x0F0F0000 is not,
/// since all 1s are not contiguous.
static inline bool isRunOfOnes(unsigned Val, unsigned &MB, unsigned &ME) {
  if (!Val)
    return false;

  if (isShiftedMask_32(Val)) {
    // look for the first non-zero bit
    MB = llvm::countl_zero(Val);
    // look for the first zero bit after the run of ones
    ME = llvm::countl_zero((Val - 1) ^ Val);
    return true;
  } else {
    Val = ~Val; // invert mask
    if (isShiftedMask_32(Val)) {
      // effectively look for the first zero bit
      ME = llvm::countl_zero(Val) - 1;
      // effectively look for the first one bit after the run of zeros
      MB = llvm::countl_zero((Val - 1) ^ Val) + 1;
      return true;
    }
  }
  // no run present
  return false;
}

static inline bool isRunOfOnes64(uint64_t Val, unsigned &MB, unsigned &ME) {
  if (!Val)
    return false;

  if (isShiftedMask_64(Val)) {
    // look for the first non-zero bit
    MB = llvm::countl_zero(Val);
    // look for the first zero bit after the run of ones
    ME = llvm::countl_zero((Val - 1) ^ Val);
    return true;
  } else {
    Val = ~Val; // invert mask
    if (isShiftedMask_64(Val)) {
      // effectively look for the first zero bit
      ME = llvm::countl_zero(Val) - 1;
      // effectively look for the first one bit after the run of zeros
      MB = llvm::countl_zero((Val - 1) ^ Val) + 1;
      return true;
    }
  }
  // no run present
  return false;
}

/// PPCII - This namespace holds all of the PowerPC target-specific
/// per-instruction flags.  These must match the corresponding definitions in
/// PPC.td and PPCInstrFormats.td.
namespace PPCII {
enum {
  // PPC970 Instruction Flags.  These flags describe the characteristics of the
  // PowerPC 970 (aka G5) dispatch groups and how they are formed out of
  // raw machine instructions.

  /// PPC970_First - This instruction starts a new dispatch group, so it will
  /// always be the first one in the group.
  PPC970_First = 0x1,

  /// PPC970_Single - This instruction starts a new dispatch group and
  /// terminates it, so it will be the sole instruction in the group.
  PPC970_Single = 0x2,

  /// PPC970_Cracked - This instruction is cracked into two pieces, requiring
  /// two dispatch pipes to be available to issue.
  PPC970_Cracked = 0x4,

  /// PPC970_Mask/Shift - This is a bitmask that selects the pipeline type that
  /// an instruction is issued to.
  PPC970_Shift = 3,
  PPC970_Mask = 0x07 << PPC970_Shift
};
enum PPC970_Unit {
  /// These are the various PPC970 execution unit pipelines.  Each instruction
  /// is one of these.
  PPC970_Pseudo = 0 << PPC970_Shift,   // Pseudo instruction
  PPC970_FXU    = 1 << PPC970_Shift,   // Fixed Point (aka Integer/ALU) Unit
  PPC970_LSU    = 2 << PPC970_Shift,   // Load Store Unit
  PPC970_FPU    = 3 << PPC970_Shift,   // Floating Point Unit
  PPC970_CRU    = 4 << PPC970_Shift,   // Control Register Unit
  PPC970_VALU   = 5 << PPC970_Shift,   // Vector ALU
  PPC970_VPERM  = 6 << PPC970_Shift,   // Vector Permute Unit
  PPC970_BRU    = 7 << PPC970_Shift    // Branch Unit
};

enum {
  /// Shift count to bypass PPC970 flags
  NewDef_Shift = 6,

  /// This instruction is an X-Form memory operation.
  XFormMemOp = 0x1 << NewDef_Shift,
  /// This instruction is prefixed.
  Prefixed = 0x1 << (NewDef_Shift + 1),
  /// This instruction produced a sign extended result.
  SExt32To64 = 0x1 << (NewDef_Shift + 2),
  /// This instruction produced a zero extended result.
  ZExt32To64 = 0x1 << (NewDef_Shift + 3)
};
} // end namespace PPCII

} // end namespace llvm

// Defines symbolic names for PowerPC registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "PPCGenRegisterInfo.inc"

// Defines symbolic names for the PowerPC instructions.
//
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_SCHED_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "PPCGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "PPCGenSubtargetInfo.inc"

#define PPC_REGS0_7(X)                                                         \
  {                                                                            \
    X##0, X##1, X##2, X##3, X##4, X##5, X##6, X##7                             \
  }

#define PPC_REGS0_31(X)                                                        \
  {                                                                            \
    X##0, X##1, X##2, X##3, X##4, X##5, X##6, X##7, X##8, X##9, X##10, X##11,  \
        X##12, X##13, X##14, X##15, X##16, X##17, X##18, X##19, X##20, X##21,  \
        X##22, X##23, X##24, X##25, X##26, X##27, X##28, X##29, X##30, X##31   \
  }

#define PPC_REGS_EVEN0_30(X)                                                   \
  {                                                                            \
    X##0, X##2, X##4, X##6, X##8, X##10, X##12, X##14, X##16, X##18, X##20,    \
        X##22, X##24, X##26, X##28, X##30                                      \
  }

#define PPC_REGS0_63(X)                                                        \
  {                                                                            \
    X##0, X##1, X##2, X##3, X##4, X##5, X##6, X##7, X##8, X##9, X##10, X##11,  \
        X##12, X##13, X##14, X##15, X##16, X##17, X##18, X##19, X##20, X##21,  \
        X##22, X##23, X##24, X##25, X##26, X##27, X##28, X##29, X##30, X##31,  \
        X##32, X##33, X##34, X##35, X##36, X##37, X##38, X##39, X##40, X##41,  \
        X##42, X##43, X##44, X##45, X##46, X##47, X##48, X##49, X##50, X##51,  \
        X##52, X##53, X##54, X##55, X##56, X##57, X##58, X##59, X##60, X##61,  \
        X##62, X##63                                                           \
  }

#define PPC_REGS_NO0_31(Z, X)                                                  \
  {                                                                            \
    Z, X##1, X##2, X##3, X##4, X##5, X##6, X##7, X##8, X##9, X##10, X##11,     \
        X##12, X##13, X##14, X##15, X##16, X##17, X##18, X##19, X##20, X##21,  \
        X##22, X##23, X##24, X##25, X##26, X##27, X##28, X##29, X##30, X##31   \
  }

#define PPC_REGS_LO_HI(LO, HI)                                                 \
  {                                                                            \
    LO##0, LO##1, LO##2, LO##3, LO##4, LO##5, LO##6, LO##7, LO##8, LO##9,      \
        LO##10, LO##11, LO##12, LO##13, LO##14, LO##15, LO##16, LO##17,        \
        LO##18, LO##19, LO##20, LO##21, LO##22, LO##23, LO##24, LO##25,        \
        LO##26, LO##27, LO##28, LO##29, LO##30, LO##31, HI##0, HI##1, HI##2,   \
        HI##3, HI##4, HI##5, HI##6, HI##7, HI##8, HI##9, HI##10, HI##11,       \
        HI##12, HI##13, HI##14, HI##15, HI##16, HI##17, HI##18, HI##19,        \
        HI##20, HI##21, HI##22, HI##23, HI##24, HI##25, HI##26, HI##27,        \
        HI##28, HI##29, HI##30, HI##31                                         \
  }

#define PPC_REGS0_7(X)                                                         \
  {                                                                            \
    X##0, X##1, X##2, X##3, X##4, X##5, X##6, X##7                             \
  }

#define PPC_REGS0_3(X)                                                         \
  {                                                                            \
    X##0, X##1, X##2, X##3                                                     \
  }

using llvm::MCPhysReg;

#define DEFINE_PPC_REGCLASSES                                                  \
  static const MCPhysReg RRegs[32] = PPC_REGS0_31(PPC::R);                     \
  static const MCPhysReg XRegs[32] = PPC_REGS0_31(PPC::X);                     \
  static const MCPhysReg FRegs[32] = PPC_REGS0_31(PPC::F);                     \
  static const MCPhysReg FpRegs[16] = PPC_REGS_EVEN0_30(PPC::Fpair);           \
  static const MCPhysReg VSRpRegs[32] = PPC_REGS0_31(PPC::VSRp);               \
  static const MCPhysReg SPERegs[32] = PPC_REGS0_31(PPC::S);                   \
  static const MCPhysReg VFRegs[32] = PPC_REGS0_31(PPC::VF);                   \
  static const MCPhysReg VRegs[32] = PPC_REGS0_31(PPC::V);                     \
  static const MCPhysReg RRegsNoR0[32] = PPC_REGS_NO0_31(PPC::ZERO, PPC::R);   \
  static const MCPhysReg XRegsNoX0[32] = PPC_REGS_NO0_31(PPC::ZERO8, PPC::X);  \
  static const MCPhysReg VSRegs[64] = PPC_REGS_LO_HI(PPC::VSL, PPC::V);        \
  static const MCPhysReg VSFRegs[64] = PPC_REGS_LO_HI(PPC::F, PPC::VF);        \
  static const MCPhysReg VSSRegs[64] = PPC_REGS_LO_HI(PPC::F, PPC::VF);        \
  static const MCPhysReg CRBITRegs[32] = {                                     \
      PPC::CR0LT, PPC::CR0GT, PPC::CR0EQ, PPC::CR0UN, PPC::CR1LT, PPC::CR1GT,  \
      PPC::CR1EQ, PPC::CR1UN, PPC::CR2LT, PPC::CR2GT, PPC::CR2EQ, PPC::CR2UN,  \
      PPC::CR3LT, PPC::CR3GT, PPC::CR3EQ, PPC::CR3UN, PPC::CR4LT, PPC::CR4GT,  \
      PPC::CR4EQ, PPC::CR4UN, PPC::CR5LT, PPC::CR5GT, PPC::CR5EQ, PPC::CR5UN,  \
      PPC::CR6LT, PPC::CR6GT, PPC::CR6EQ, PPC::CR6UN, PPC::CR7LT, PPC::CR7GT,  \
      PPC::CR7EQ, PPC::CR7UN};                                                 \
  static const MCPhysReg CRRegs[8] = PPC_REGS0_7(PPC::CR);                     \
  static const MCPhysReg ACCRegs[8] = PPC_REGS0_7(PPC::ACC);                   \
  static const MCPhysReg WACCRegs[8] = PPC_REGS0_7(PPC::WACC);                 \
  static const MCPhysReg WACC_HIRegs[8] = PPC_REGS0_7(PPC::WACC_HI);           \
  static const MCPhysReg DMRROWpRegs[32] = PPC_REGS0_31(PPC::DMRROWp);         \
  static const MCPhysReg DMRROWRegs[64] = PPC_REGS0_63(PPC::DMRROW);           \
  static const MCPhysReg DMRRegs[8] = PPC_REGS0_7(PPC::DMR);                   \
  static const MCPhysReg DMRpRegs[4] = PPC_REGS0_3(PPC::DMRp);

namespace llvm {
namespace PPC {
static inline bool isVFRegister(unsigned Reg) {
  return Reg >= PPC::VF0 && Reg <= PPC::VF31;
}

static inline bool isVRRegister(unsigned Reg) {
  return Reg >= PPC::V0 && Reg <= PPC::V31;
}
} // namespace PPC
} // namespace llvm

#endif // LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCTARGETDESC_H
