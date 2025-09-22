//===-- RISCVBaseInfo.h - Top level definitions for RISC-V MC ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone enum definitions for the RISC-V target
// useful for the compiler back-end and the MC libraries.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVBASEINFO_H
#define LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVBASEINFO_H

#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include "llvm/TargetParser/RISCVTargetParser.h"
#include "llvm/TargetParser/SubtargetFeature.h"

namespace llvm {

// RISCVII - This namespace holds all of the target specific flags that
// instruction info tracks. All definitions must match RISCVInstrFormats.td.
namespace RISCVII {
enum {
  InstFormatPseudo = 0,
  InstFormatR = 1,
  InstFormatR4 = 2,
  InstFormatI = 3,
  InstFormatS = 4,
  InstFormatB = 5,
  InstFormatU = 6,
  InstFormatJ = 7,
  InstFormatCR = 8,
  InstFormatCI = 9,
  InstFormatCSS = 10,
  InstFormatCIW = 11,
  InstFormatCL = 12,
  InstFormatCS = 13,
  InstFormatCA = 14,
  InstFormatCB = 15,
  InstFormatCJ = 16,
  InstFormatCU = 17,
  InstFormatCLB = 18,
  InstFormatCLH = 19,
  InstFormatCSB = 20,
  InstFormatCSH = 21,
  InstFormatOther = 22,

  InstFormatMask = 31,
  InstFormatShift = 0,

  ConstraintShift = InstFormatShift + 5,
  VS2Constraint = 0b001 << ConstraintShift,
  VS1Constraint = 0b010 << ConstraintShift,
  VMConstraint = 0b100 << ConstraintShift,
  ConstraintMask = 0b111 << ConstraintShift,

  VLMulShift = ConstraintShift + 3,
  VLMulMask = 0b111 << VLMulShift,

  // Force a tail agnostic policy even this instruction has a tied destination.
  ForceTailAgnosticShift = VLMulShift + 3,
  ForceTailAgnosticMask = 1 << ForceTailAgnosticShift,

  // Is this a _TIED vector pseudo instruction. For these instructions we
  // shouldn't skip the tied operand when converting to MC instructions.
  IsTiedPseudoShift = ForceTailAgnosticShift + 1,
  IsTiedPseudoMask = 1 << IsTiedPseudoShift,

  // Does this instruction have a SEW operand. It will be the last explicit
  // operand unless there is a vector policy operand. Used by RVV Pseudos.
  HasSEWOpShift = IsTiedPseudoShift + 1,
  HasSEWOpMask = 1 << HasSEWOpShift,

  // Does this instruction have a VL operand. It will be the second to last
  // explicit operand unless there is a vector policy operand. Used by RVV
  // Pseudos.
  HasVLOpShift = HasSEWOpShift + 1,
  HasVLOpMask = 1 << HasVLOpShift,

  // Does this instruction have a vector policy operand. It will be the last
  // explicit operand. Used by RVV Pseudos.
  HasVecPolicyOpShift = HasVLOpShift + 1,
  HasVecPolicyOpMask = 1 << HasVecPolicyOpShift,

  // Is this instruction a vector widening reduction instruction. Used by RVV
  // Pseudos.
  IsRVVWideningReductionShift = HasVecPolicyOpShift + 1,
  IsRVVWideningReductionMask = 1 << IsRVVWideningReductionShift,

  // Does this instruction care about mask policy. If it is not, the mask policy
  // could be either agnostic or undisturbed. For example, unmasked, store, and
  // reduction operations result would not be affected by mask policy, so
  // compiler has free to select either one.
  UsesMaskPolicyShift = IsRVVWideningReductionShift + 1,
  UsesMaskPolicyMask = 1 << UsesMaskPolicyShift,

  // Indicates that the result can be considered sign extended from bit 31. Some
  // instructions with this flag aren't W instructions, but are either sign
  // extended from a smaller size, always outputs a small integer, or put zeros
  // in bits 63:31. Used by the SExtWRemoval pass.
  IsSignExtendingOpWShift = UsesMaskPolicyShift + 1,
  IsSignExtendingOpWMask = 1ULL << IsSignExtendingOpWShift,

  HasRoundModeOpShift = IsSignExtendingOpWShift + 1,
  HasRoundModeOpMask = 1 << HasRoundModeOpShift,

  UsesVXRMShift = HasRoundModeOpShift + 1,
  UsesVXRMMask = 1 << UsesVXRMShift,

  // Indicates whether these instructions can partially overlap between source
  // registers and destination registers according to the vector spec.
  // 0 -> not a vector pseudo
  // 1 -> default value for vector pseudos. not widening or narrowing.
  // 2 -> narrowing case
  // 3 -> widening case
  TargetOverlapConstraintTypeShift = UsesVXRMShift + 1,
  TargetOverlapConstraintTypeMask = 3ULL << TargetOverlapConstraintTypeShift,
};

// Helper functions to read TSFlags.
/// \returns the format of the instruction.
static inline unsigned getFormat(uint64_t TSFlags) {
  return (TSFlags & InstFormatMask) >> InstFormatShift;
}
/// \returns the LMUL for the instruction.
static inline VLMUL getLMul(uint64_t TSFlags) {
  return static_cast<VLMUL>((TSFlags & VLMulMask) >> VLMulShift);
}
/// \returns true if tail agnostic is enforced for the instruction.
static inline bool doesForceTailAgnostic(uint64_t TSFlags) {
  return TSFlags & ForceTailAgnosticMask;
}
/// \returns true if this a _TIED pseudo.
static inline bool isTiedPseudo(uint64_t TSFlags) {
  return TSFlags & IsTiedPseudoMask;
}
/// \returns true if there is a SEW operand for the instruction.
static inline bool hasSEWOp(uint64_t TSFlags) {
  return TSFlags & HasSEWOpMask;
}
/// \returns true if there is a VL operand for the instruction.
static inline bool hasVLOp(uint64_t TSFlags) {
  return TSFlags & HasVLOpMask;
}
/// \returns true if there is a vector policy operand for this instruction.
static inline bool hasVecPolicyOp(uint64_t TSFlags) {
  return TSFlags & HasVecPolicyOpMask;
}
/// \returns true if it is a vector widening reduction instruction.
static inline bool isRVVWideningReduction(uint64_t TSFlags) {
  return TSFlags & IsRVVWideningReductionMask;
}
/// \returns true if mask policy is valid for the instruction.
static inline bool usesMaskPolicy(uint64_t TSFlags) {
  return TSFlags & UsesMaskPolicyMask;
}

/// \returns true if there is a rounding mode operand for this instruction
static inline bool hasRoundModeOp(uint64_t TSFlags) {
  return TSFlags & HasRoundModeOpMask;
}

/// \returns true if this instruction uses vxrm
static inline bool usesVXRM(uint64_t TSFlags) { return TSFlags & UsesVXRMMask; }

static inline unsigned getVLOpNum(const MCInstrDesc &Desc) {
  const uint64_t TSFlags = Desc.TSFlags;
  // This method is only called if we expect to have a VL operand, and all
  // instructions with VL also have SEW.
  assert(hasSEWOp(TSFlags) && hasVLOp(TSFlags));
  unsigned Offset = 2;
  if (hasVecPolicyOp(TSFlags))
    Offset = 3;
  return Desc.getNumOperands() - Offset;
}

static inline unsigned getSEWOpNum(const MCInstrDesc &Desc) {
  const uint64_t TSFlags = Desc.TSFlags;
  assert(hasSEWOp(TSFlags));
  unsigned Offset = 1;
  if (hasVecPolicyOp(TSFlags))
    Offset = 2;
  return Desc.getNumOperands() - Offset;
}

static inline unsigned getVecPolicyOpNum(const MCInstrDesc &Desc) {
  assert(hasVecPolicyOp(Desc.TSFlags));
  return Desc.getNumOperands() - 1;
}

/// \returns  the index to the rounding mode immediate value if any, otherwise
/// returns -1.
static inline int getFRMOpNum(const MCInstrDesc &Desc) {
  const uint64_t TSFlags = Desc.TSFlags;
  if (!hasRoundModeOp(TSFlags) || usesVXRM(TSFlags))
    return -1;

  // The operand order
  // --------------------------------------
  // | n-1 (if any)   | n-2  | n-3 | n-4 |
  // | policy         | sew  | vl  | frm |
  // --------------------------------------
  return getVLOpNum(Desc) - 1;
}

/// \returns  the index to the rounding mode immediate value if any, otherwise
/// returns -1.
static inline int getVXRMOpNum(const MCInstrDesc &Desc) {
  const uint64_t TSFlags = Desc.TSFlags;
  if (!hasRoundModeOp(TSFlags) || !usesVXRM(TSFlags))
    return -1;
  // The operand order
  // --------------------------------------
  // | n-1 (if any)   | n-2  | n-3 | n-4  |
  // | policy         | sew  | vl  | vxrm |
  // --------------------------------------
  return getVLOpNum(Desc) - 1;
}

// Is the first def operand tied to the first use operand. This is true for
// vector pseudo instructions that have a merge operand for tail/mask
// undisturbed. It's also true for vector FMA instructions where one of the
// operands is also the destination register.
static inline bool isFirstDefTiedToFirstUse(const MCInstrDesc &Desc) {
  return Desc.getNumDefs() < Desc.getNumOperands() &&
         Desc.getOperandConstraint(Desc.getNumDefs(), MCOI::TIED_TO) == 0;
}

// RISC-V Specific Machine Operand Flags
enum {
  MO_None = 0,
  MO_CALL = 1,
  MO_LO = 3,
  MO_HI = 4,
  MO_PCREL_LO = 5,
  MO_PCREL_HI = 6,
  MO_GOT_HI = 7,
  MO_TPREL_LO = 8,
  MO_TPREL_HI = 9,
  MO_TPREL_ADD = 10,
  MO_TLS_GOT_HI = 11,
  MO_TLS_GD_HI = 12,
  MO_TLSDESC_HI = 13,
  MO_TLSDESC_LOAD_LO = 14,
  MO_TLSDESC_ADD_LO = 15,
  MO_TLSDESC_CALL = 16,

  // Used to differentiate between target-specific "direct" flags and "bitmask"
  // flags. A machine operand can only have one "direct" flag, but can have
  // multiple "bitmask" flags.
  MO_DIRECT_FLAG_MASK = 31
};
} // namespace RISCVII

namespace RISCVOp {
enum OperandType : unsigned {
  OPERAND_FIRST_RISCV_IMM = MCOI::OPERAND_FIRST_TARGET,
  OPERAND_UIMM1 = OPERAND_FIRST_RISCV_IMM,
  OPERAND_UIMM2,
  OPERAND_UIMM2_LSB0,
  OPERAND_UIMM3,
  OPERAND_UIMM4,
  OPERAND_UIMM5,
  OPERAND_UIMM5_LSB0,
  OPERAND_UIMM6,
  OPERAND_UIMM6_LSB0,
  OPERAND_UIMM7,
  OPERAND_UIMM7_LSB00,
  OPERAND_UIMM8_LSB00,
  OPERAND_UIMM8,
  OPERAND_UIMM8_LSB000,
  OPERAND_UIMM8_GE32,
  OPERAND_UIMM9_LSB000,
  OPERAND_UIMM10_LSB00_NONZERO,
  OPERAND_UIMM12,
  OPERAND_UIMM16,
  OPERAND_UIMM32,
  OPERAND_ZERO,
  OPERAND_SIMM5,
  OPERAND_SIMM5_PLUS1,
  OPERAND_SIMM6,
  OPERAND_SIMM6_NONZERO,
  OPERAND_SIMM10_LSB0000_NONZERO,
  OPERAND_SIMM12,
  OPERAND_SIMM12_LSB00000,
  OPERAND_UIMM20,
  OPERAND_UIMMLOG2XLEN,
  OPERAND_UIMMLOG2XLEN_NONZERO,
  OPERAND_CLUI_IMM,
  OPERAND_VTYPEI10,
  OPERAND_VTYPEI11,
  OPERAND_RVKRNUM,
  OPERAND_RVKRNUM_0_7,
  OPERAND_RVKRNUM_1_10,
  OPERAND_RVKRNUM_2_14,
  OPERAND_SPIMM,
  OPERAND_LAST_RISCV_IMM = OPERAND_SPIMM,
  // Operand is either a register or uimm5, this is used by V extension pseudo
  // instructions to represent a value that be passed as AVL to either vsetvli
  // or vsetivli.
  OPERAND_AVL,
};
} // namespace RISCVOp

// Describes the predecessor/successor bits used in the FENCE instruction.
namespace RISCVFenceField {
enum FenceField {
  I = 8,
  O = 4,
  R = 2,
  W = 1
};
}

// Describes the supported floating point rounding mode encodings.
namespace RISCVFPRndMode {
enum RoundingMode {
  RNE = 0,
  RTZ = 1,
  RDN = 2,
  RUP = 3,
  RMM = 4,
  DYN = 7,
  Invalid
};

inline static StringRef roundingModeToString(RoundingMode RndMode) {
  switch (RndMode) {
  default:
    llvm_unreachable("Unknown floating point rounding mode");
  case RISCVFPRndMode::RNE:
    return "rne";
  case RISCVFPRndMode::RTZ:
    return "rtz";
  case RISCVFPRndMode::RDN:
    return "rdn";
  case RISCVFPRndMode::RUP:
    return "rup";
  case RISCVFPRndMode::RMM:
    return "rmm";
  case RISCVFPRndMode::DYN:
    return "dyn";
  }
}

inline static RoundingMode stringToRoundingMode(StringRef Str) {
  return StringSwitch<RoundingMode>(Str)
      .Case("rne", RISCVFPRndMode::RNE)
      .Case("rtz", RISCVFPRndMode::RTZ)
      .Case("rdn", RISCVFPRndMode::RDN)
      .Case("rup", RISCVFPRndMode::RUP)
      .Case("rmm", RISCVFPRndMode::RMM)
      .Case("dyn", RISCVFPRndMode::DYN)
      .Default(RISCVFPRndMode::Invalid);
}

inline static bool isValidRoundingMode(unsigned Mode) {
  switch (Mode) {
  default:
    return false;
  case RISCVFPRndMode::RNE:
  case RISCVFPRndMode::RTZ:
  case RISCVFPRndMode::RDN:
  case RISCVFPRndMode::RUP:
  case RISCVFPRndMode::RMM:
  case RISCVFPRndMode::DYN:
    return true;
  }
}
} // namespace RISCVFPRndMode

namespace RISCVVXRndMode {
enum RoundingMode {
  RNU = 0,
  RNE = 1,
  RDN = 2,
  ROD = 3,
};
} // namespace RISCVVXRndMode

//===----------------------------------------------------------------------===//
// Floating-point Immediates
//

namespace RISCVLoadFPImm {
float getFPImm(unsigned Imm);

/// getLoadFPImm - Return a 5-bit binary encoding of the floating-point
/// immediate value. If the value cannot be represented as a 5-bit binary
/// encoding, then return -1.
int getLoadFPImm(APFloat FPImm);
} // namespace RISCVLoadFPImm

namespace RISCVSysReg {
struct SysReg {
  const char *Name;
  const char *AltName;
  const char *DeprecatedName;
  unsigned Encoding;
  // FIXME: add these additional fields when needed.
  // Privilege Access: Read, Write, Read-Only.
  // unsigned ReadWrite;
  // Privilege Mode: User, System or Machine.
  // unsigned Mode;
  // Check field name.
  // unsigned Extra;
  // Register number without the privilege bits.
  // unsigned Number;
  FeatureBitset FeaturesRequired;
  bool isRV32Only;

  bool haveRequiredFeatures(const FeatureBitset &ActiveFeatures) const {
    // Not in 32-bit mode.
    if (isRV32Only && ActiveFeatures[RISCV::Feature64Bit])
      return false;
    // No required feature associated with the system register.
    if (FeaturesRequired.none())
      return true;
    return (FeaturesRequired & ActiveFeatures) == FeaturesRequired;
  }
};

#define GET_SysRegsList_DECL
#include "RISCVGenSearchableTables.inc"
} // end namespace RISCVSysReg

namespace RISCVInsnOpcode {
struct RISCVOpcode {
  const char *Name;
  unsigned Value;
};

#define GET_RISCVOpcodesList_DECL
#include "RISCVGenSearchableTables.inc"
} // end namespace RISCVInsnOpcode

namespace RISCVABI {

enum ABI {
  ABI_ILP32,
  ABI_ILP32F,
  ABI_ILP32D,
  ABI_ILP32E,
  ABI_LP64,
  ABI_LP64F,
  ABI_LP64D,
  ABI_LP64E,
  ABI_Unknown
};

// Returns the target ABI, or else a StringError if the requested ABIName is
// not supported for the given TT and FeatureBits combination.
ABI computeTargetABI(const Triple &TT, const FeatureBitset &FeatureBits,
                     StringRef ABIName);

ABI getTargetABI(StringRef ABIName);

// Returns the register used to hold the stack pointer after realignment.
MCRegister getBPReg();

// Returns the register holding shadow call stack pointer.
MCRegister getSCSPReg();

} // namespace RISCVABI

namespace RISCVFeatures {

// Validates if the given combination of features are valid for the target
// triple. Exits with report_fatal_error if not.
void validate(const Triple &TT, const FeatureBitset &FeatureBits);

llvm::Expected<std::unique_ptr<RISCVISAInfo>>
parseFeatureBits(bool IsRV64, const FeatureBitset &FeatureBits);

} // namespace RISCVFeatures

namespace RISCVRVC {
bool compress(MCInst &OutInst, const MCInst &MI, const MCSubtargetInfo &STI);
bool uncompress(MCInst &OutInst, const MCInst &MI, const MCSubtargetInfo &STI);
} // namespace RISCVRVC

namespace RISCVZC {
enum RLISTENCODE {
  RA = 4,
  RA_S0,
  RA_S0_S1,
  RA_S0_S2,
  RA_S0_S3,
  RA_S0_S4,
  RA_S0_S5,
  RA_S0_S6,
  RA_S0_S7,
  RA_S0_S8,
  RA_S0_S9,
  // note - to include s10, s11 must also be included
  RA_S0_S11,
  INVALID_RLIST,
};

inline unsigned encodeRlist(MCRegister EndReg, bool IsRV32E = false) {
  assert((!IsRV32E || EndReg <= RISCV::X9) && "Invalid Rlist for RV32E");
  switch (EndReg) {
  case RISCV::X1:
    return RLISTENCODE::RA;
  case RISCV::X8:
    return RLISTENCODE::RA_S0;
  case RISCV::X9:
    return RLISTENCODE::RA_S0_S1;
  case RISCV::X18:
    return RLISTENCODE::RA_S0_S2;
  case RISCV::X19:
    return RLISTENCODE::RA_S0_S3;
  case RISCV::X20:
    return RLISTENCODE::RA_S0_S4;
  case RISCV::X21:
    return RLISTENCODE::RA_S0_S5;
  case RISCV::X22:
    return RLISTENCODE::RA_S0_S6;
  case RISCV::X23:
    return RLISTENCODE::RA_S0_S7;
  case RISCV::X24:
    return RLISTENCODE::RA_S0_S8;
  case RISCV::X25:
    return RLISTENCODE::RA_S0_S9;
  case RISCV::X26:
    return RLISTENCODE::INVALID_RLIST;
  case RISCV::X27:
    return RLISTENCODE::RA_S0_S11;
  default:
    llvm_unreachable("Undefined input.");
  }
}

inline static unsigned getStackAdjBase(unsigned RlistVal, bool IsRV64) {
  assert(RlistVal != RLISTENCODE::INVALID_RLIST &&
         "{ra, s0-s10} is not supported, s11 must be included.");
  if (!IsRV64) {
    switch (RlistVal) {
    case RLISTENCODE::RA:
    case RLISTENCODE::RA_S0:
    case RLISTENCODE::RA_S0_S1:
    case RLISTENCODE::RA_S0_S2:
      return 16;
    case RLISTENCODE::RA_S0_S3:
    case RLISTENCODE::RA_S0_S4:
    case RLISTENCODE::RA_S0_S5:
    case RLISTENCODE::RA_S0_S6:
      return 32;
    case RLISTENCODE::RA_S0_S7:
    case RLISTENCODE::RA_S0_S8:
    case RLISTENCODE::RA_S0_S9:
      return 48;
    case RLISTENCODE::RA_S0_S11:
      return 64;
    }
  } else {
    switch (RlistVal) {
    case RLISTENCODE::RA:
    case RLISTENCODE::RA_S0:
      return 16;
    case RLISTENCODE::RA_S0_S1:
    case RLISTENCODE::RA_S0_S2:
      return 32;
    case RLISTENCODE::RA_S0_S3:
    case RLISTENCODE::RA_S0_S4:
      return 48;
    case RLISTENCODE::RA_S0_S5:
    case RLISTENCODE::RA_S0_S6:
      return 64;
    case RLISTENCODE::RA_S0_S7:
    case RLISTENCODE::RA_S0_S8:
      return 80;
    case RLISTENCODE::RA_S0_S9:
      return 96;
    case RLISTENCODE::RA_S0_S11:
      return 112;
    }
  }
  llvm_unreachable("Unexpected RlistVal");
}

inline static bool getSpimm(unsigned RlistVal, unsigned &SpimmVal,
                            int64_t StackAdjustment, bool IsRV64) {
  if (RlistVal == RLISTENCODE::INVALID_RLIST)
    return false;
  unsigned StackAdjBase = getStackAdjBase(RlistVal, IsRV64);
  StackAdjustment -= StackAdjBase;
  if (StackAdjustment % 16 != 0)
    return false;
  SpimmVal = StackAdjustment / 16;
  if (SpimmVal > 3)
    return false;
  return true;
}

void printRlist(unsigned SlistEncode, raw_ostream &OS);
} // namespace RISCVZC

} // namespace llvm

#endif
