//===-- VE.h - Top-level interface for VE representation --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// VE back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VE_VE_H
#define LLVM_LIB_TARGET_VE_VE_H

#include "MCTargetDesc/VEMCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class AsmPrinter;
class FunctionPass;
class MCInst;
class MachineInstr;
class PassRegistry;
class VETargetMachine;

FunctionPass *createVEISelDag(VETargetMachine &TM);
FunctionPass *createLVLGenPass();
void initializeVEDAGToDAGISelLegacyPass(PassRegistry &);

void LowerVEMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                 AsmPrinter &AP);
} // namespace llvm

namespace llvm {
// Enums corresponding to VE condition codes, both icc's and fcc's.  These
// values must be kept in sync with the ones in the .td file.
namespace VECC {
enum CondCode {
  // Integer comparison
  CC_IG = 0,  // Greater
  CC_IL = 1,  // Less
  CC_INE = 2, // Not Equal
  CC_IEQ = 3, // Equal
  CC_IGE = 4, // Greater or Equal
  CC_ILE = 5, // Less or Equal

  // Floating point comparison
  CC_AF = 0 + 6,     // Never
  CC_G = 1 + 6,      // Greater
  CC_L = 2 + 6,      // Less
  CC_NE = 3 + 6,     // Not Equal
  CC_EQ = 4 + 6,     // Equal
  CC_GE = 5 + 6,     // Greater or Equal
  CC_LE = 6 + 6,     // Less or Equal
  CC_NUM = 7 + 6,    // Number
  CC_NAN = 8 + 6,    // NaN
  CC_GNAN = 9 + 6,   // Greater or NaN
  CC_LNAN = 10 + 6,  // Less or NaN
  CC_NENAN = 11 + 6, // Not Equal or NaN
  CC_EQNAN = 12 + 6, // Equal or NaN
  CC_GENAN = 13 + 6, // Greater or Equal or NaN
  CC_LENAN = 14 + 6, // Less or Equal or NaN
  CC_AT = 15 + 6,    // Always
  UNKNOWN
};
}
// Enums corresponding to VE Rounding Mode.  These values must be kept in
// sync with the ones in the .td file.
namespace VERD {
enum RoundingMode {
  RD_NONE = 0, // According to PSW
  RD_RZ = 8,   // Round toward Zero
  RD_RP = 9,   // Round toward Plus infinity
  RD_RM = 10,  // Round toward Minus infinity
  RD_RN = 11,  // Round to Nearest (ties to Even)
  RD_RA = 12,  // Round to Nearest (ties to Away)
  UNKNOWN
};
}

inline static const char *VECondCodeToString(VECC::CondCode CC) {
  switch (CC) {
  case VECC::CC_IG:    return "gt";
  case VECC::CC_IL:    return "lt";
  case VECC::CC_INE:   return "ne";
  case VECC::CC_IEQ:   return "eq";
  case VECC::CC_IGE:   return "ge";
  case VECC::CC_ILE:   return "le";
  case VECC::CC_AF:    return "af";
  case VECC::CC_G:     return "gt";
  case VECC::CC_L:     return "lt";
  case VECC::CC_NE:    return "ne";
  case VECC::CC_EQ:    return "eq";
  case VECC::CC_GE:    return "ge";
  case VECC::CC_LE:    return "le";
  case VECC::CC_NUM:   return "num";
  case VECC::CC_NAN:   return "nan";
  case VECC::CC_GNAN:  return "gtnan";
  case VECC::CC_LNAN:  return "ltnan";
  case VECC::CC_NENAN: return "nenan";
  case VECC::CC_EQNAN: return "eqnan";
  case VECC::CC_GENAN: return "genan";
  case VECC::CC_LENAN: return "lenan";
  case VECC::CC_AT:    return "at";
  default:
    llvm_unreachable("Invalid cond code");
  }
}

inline static VECC::CondCode stringToVEICondCode(StringRef S) {
  return StringSwitch<VECC::CondCode>(S)
      .Case("gt", VECC::CC_IG)
      .Case("lt", VECC::CC_IL)
      .Case("ne", VECC::CC_INE)
      .Case("eq", VECC::CC_IEQ)
      .Case("ge", VECC::CC_IGE)
      .Case("le", VECC::CC_ILE)
      .Case("af", VECC::CC_AF)
      .Case("at", VECC::CC_AT)
      .Case("", VECC::CC_AT)
      .Default(VECC::UNKNOWN);
}

inline static VECC::CondCode stringToVEFCondCode(StringRef S) {
  return StringSwitch<VECC::CondCode>(S)
      .Case("gt", VECC::CC_G)
      .Case("lt", VECC::CC_L)
      .Case("ne", VECC::CC_NE)
      .Case("eq", VECC::CC_EQ)
      .Case("ge", VECC::CC_GE)
      .Case("le", VECC::CC_LE)
      .Case("num", VECC::CC_NUM)
      .Case("nan", VECC::CC_NAN)
      .Case("gtnan", VECC::CC_GNAN)
      .Case("ltnan", VECC::CC_LNAN)
      .Case("nenan", VECC::CC_NENAN)
      .Case("eqnan", VECC::CC_EQNAN)
      .Case("genan", VECC::CC_GENAN)
      .Case("lenan", VECC::CC_LENAN)
      .Case("af", VECC::CC_AF)
      .Case("at", VECC::CC_AT)
      .Case("", VECC::CC_AT)
      .Default(VECC::UNKNOWN);
}

inline static bool isIntVECondCode(VECC::CondCode CC) {
  return CC < VECC::CC_AF;
}

inline static unsigned VECondCodeToVal(VECC::CondCode CC) {
  switch (CC) {
  case VECC::CC_IG:
    return 1;
  case VECC::CC_IL:
    return 2;
  case VECC::CC_INE:
    return 3;
  case VECC::CC_IEQ:
    return 4;
  case VECC::CC_IGE:
    return 5;
  case VECC::CC_ILE:
    return 6;
  case VECC::CC_AF:
    return 0;
  case VECC::CC_G:
    return 1;
  case VECC::CC_L:
    return 2;
  case VECC::CC_NE:
    return 3;
  case VECC::CC_EQ:
    return 4;
  case VECC::CC_GE:
    return 5;
  case VECC::CC_LE:
    return 6;
  case VECC::CC_NUM:
    return 7;
  case VECC::CC_NAN:
    return 8;
  case VECC::CC_GNAN:
    return 9;
  case VECC::CC_LNAN:
    return 10;
  case VECC::CC_NENAN:
    return 11;
  case VECC::CC_EQNAN:
    return 12;
  case VECC::CC_GENAN:
    return 13;
  case VECC::CC_LENAN:
    return 14;
  case VECC::CC_AT:
    return 15;
  default:
    llvm_unreachable("Invalid cond code");
  }
}

inline static VECC::CondCode VEValToCondCode(unsigned Val, bool IsInteger) {
  if (IsInteger) {
    switch (Val) {
    case 0:
      return VECC::CC_AF;
    case 1:
      return VECC::CC_IG;
    case 2:
      return VECC::CC_IL;
    case 3:
      return VECC::CC_INE;
    case 4:
      return VECC::CC_IEQ;
    case 5:
      return VECC::CC_IGE;
    case 6:
      return VECC::CC_ILE;
    case 15:
      return VECC::CC_AT;
    }
  } else {
    switch (Val) {
    case 0:
      return VECC::CC_AF;
    case 1:
      return VECC::CC_G;
    case 2:
      return VECC::CC_L;
    case 3:
      return VECC::CC_NE;
    case 4:
      return VECC::CC_EQ;
    case 5:
      return VECC::CC_GE;
    case 6:
      return VECC::CC_LE;
    case 7:
      return VECC::CC_NUM;
    case 8:
      return VECC::CC_NAN;
    case 9:
      return VECC::CC_GNAN;
    case 10:
      return VECC::CC_LNAN;
    case 11:
      return VECC::CC_NENAN;
    case 12:
      return VECC::CC_EQNAN;
    case 13:
      return VECC::CC_GENAN;
    case 14:
      return VECC::CC_LENAN;
    case 15:
      return VECC::CC_AT;
    }
  }
  llvm_unreachable("Invalid cond code");
}

inline static const char *VERDToString(VERD::RoundingMode R) {
  switch (R) {
  case VERD::RD_NONE:
    return "";
  case VERD::RD_RZ:
    return ".rz";
  case VERD::RD_RP:
    return ".rp";
  case VERD::RD_RM:
    return ".rm";
  case VERD::RD_RN:
    return ".rn";
  case VERD::RD_RA:
    return ".ra";
  default:
    llvm_unreachable("Invalid branch predicate");
  }
}

inline static VERD::RoundingMode stringToVERD(StringRef S) {
  return StringSwitch<VERD::RoundingMode>(S)
      .Case("", VERD::RD_NONE)
      .Case(".rz", VERD::RD_RZ)
      .Case(".rp", VERD::RD_RP)
      .Case(".rm", VERD::RD_RM)
      .Case(".rn", VERD::RD_RN)
      .Case(".ra", VERD::RD_RA)
      .Default(VERD::UNKNOWN);
}

inline static unsigned VERDToVal(VERD::RoundingMode R) {
  switch (R) {
  case VERD::RD_NONE:
  case VERD::RD_RZ:
  case VERD::RD_RP:
  case VERD::RD_RM:
  case VERD::RD_RN:
  case VERD::RD_RA:
    return static_cast<unsigned>(R);
  default:
    break;
  }
  llvm_unreachable("Invalid branch predicates");
}

inline static VERD::RoundingMode VEValToRD(unsigned Val) {
  switch (Val) {
  case static_cast<unsigned>(VERD::RD_NONE):
    return VERD::RD_NONE;
  case static_cast<unsigned>(VERD::RD_RZ):
    return VERD::RD_RZ;
  case static_cast<unsigned>(VERD::RD_RP):
    return VERD::RD_RP;
  case static_cast<unsigned>(VERD::RD_RM):
    return VERD::RD_RM;
  case static_cast<unsigned>(VERD::RD_RN):
    return VERD::RD_RN;
  case static_cast<unsigned>(VERD::RD_RA):
    return VERD::RD_RA;
  default:
    break;
  }
  llvm_unreachable("Invalid branch predicates");
}

// MImm - Special immediate value of sequential bit stream of 0 or 1.
//   See VEInstrInfo.td for details.
inline static bool isMImmVal(uint64_t Val) {
  if (Val == 0) {
    // (0)1 is 0
    return true;
  }
  if (isMask_64(Val)) {
    // (m)0 patterns
    return true;
  }
  // (m)1 patterns
  return (Val & (UINT64_C(1) << 63)) && isShiftedMask_64(Val);
}

inline static bool isMImm32Val(uint32_t Val) {
  if (Val == 0) {
    // (0)1 is 0
    return true;
  }
  if (isMask_32(Val)) {
    // (m)0 patterns
    return true;
  }
  // (m)1 patterns
  return (Val & (UINT32_C(1) << 31)) && isShiftedMask_32(Val);
}

/// val2MImm - Convert an integer immediate value to target MImm immediate.
inline static uint64_t val2MImm(uint64_t Val) {
  if (Val == 0)
    return 0; // (0)1
  if (Val & (UINT64_C(1) << 63))
    return llvm::countl_one(Val);       // (m)1
  return llvm::countl_zero(Val) | 0x40; // (m)0
}

/// mimm2Val - Convert a target MImm immediate to an integer immediate value.
inline static uint64_t mimm2Val(uint64_t Val) {
  if (Val == 0)
    return 0; // (0)1
  if ((Val & 0x40) == 0)
    return (uint64_t)((INT64_C(1) << 63) >> (Val & 0x3f)); // (m)1
  return ((uint64_t)INT64_C(-1) >> (Val & 0x3f));          // (m)0
}

inline unsigned M0(unsigned Val) { return Val + 64; }
inline unsigned M1(unsigned Val) { return Val; }

static const unsigned StandardVectorWidth = 256;
static const unsigned PackedVectorWidth = 512;

} // namespace llvm
#endif
