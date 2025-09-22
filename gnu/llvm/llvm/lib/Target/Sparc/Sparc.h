//===-- Sparc.h - Top-level interface for Sparc representation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// Sparc back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPARC_SPARC_H
#define LLVM_LIB_TARGET_SPARC_SPARC_H

#include "MCTargetDesc/SparcMCTargetDesc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class AsmPrinter;
class FunctionPass;
class MCInst;
class MachineInstr;
class PassRegistry;
class SparcTargetMachine;

FunctionPass *createSparcISelDag(SparcTargetMachine &TM);
FunctionPass *createSparcDelaySlotFillerPass();

void LowerSparcMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                    AsmPrinter &AP);
void initializeSparcDAGToDAGISelLegacyPass(PassRegistry &);
} // namespace llvm

namespace llvm {
  // Enums corresponding to Sparc condition codes, both icc's and fcc's.  These
  // values must be kept in sync with the ones in the .td file.
  namespace SPCC {
  enum CondCodes {
    ICC_A = 8,    // Always
    ICC_N = 0,    // Never
    ICC_NE = 9,   // Not Equal
    ICC_E = 1,    // Equal
    ICC_G = 10,   // Greater
    ICC_LE = 2,   // Less or Equal
    ICC_GE = 11,  // Greater or Equal
    ICC_L = 3,    // Less
    ICC_GU = 12,  // Greater Unsigned
    ICC_LEU = 4,  // Less or Equal Unsigned
    ICC_CC = 13,  // Carry Clear/Great or Equal Unsigned
    ICC_CS = 5,   // Carry Set/Less Unsigned
    ICC_POS = 14, // Positive
    ICC_NEG = 6,  // Negative
    ICC_VC = 15,  // Overflow Clear
    ICC_VS = 7,   // Overflow Set

    FCC_BEGIN = 16,
    FCC_A = 8 + FCC_BEGIN,    // Always
    FCC_N = 0 + FCC_BEGIN,    // Never
    FCC_U = 7 + FCC_BEGIN,    // Unordered
    FCC_G = 6 + FCC_BEGIN,    // Greater
    FCC_UG = 5 + FCC_BEGIN,   // Unordered or Greater
    FCC_L = 4 + FCC_BEGIN,    // Less
    FCC_UL = 3 + FCC_BEGIN,   // Unordered or Less
    FCC_LG = 2 + FCC_BEGIN,   // Less or Greater
    FCC_NE = 1 + FCC_BEGIN,   // Not Equal
    FCC_E = 9 + FCC_BEGIN,    // Equal
    FCC_UE = 10 + FCC_BEGIN,  // Unordered or Equal
    FCC_GE = 11 + FCC_BEGIN,  // Greater or Equal
    FCC_UGE = 12 + FCC_BEGIN, // Unordered or Greater or Equal
    FCC_LE = 13 + FCC_BEGIN,  // Less or Equal
    FCC_ULE = 14 + FCC_BEGIN, // Unordered or Less or Equal
    FCC_O = 15 + FCC_BEGIN,   // Ordered

    CPCC_BEGIN = 32,
    CPCC_A = 8 + CPCC_BEGIN, // Always
    CPCC_N = 0 + CPCC_BEGIN, // Never
    CPCC_3 = 7 + CPCC_BEGIN,
    CPCC_2 = 6 + CPCC_BEGIN,
    CPCC_23 = 5 + CPCC_BEGIN,
    CPCC_1 = 4 + CPCC_BEGIN,
    CPCC_13 = 3 + CPCC_BEGIN,
    CPCC_12 = 2 + CPCC_BEGIN,
    CPCC_123 = 1 + CPCC_BEGIN,
    CPCC_0 = 9 + CPCC_BEGIN,
    CPCC_03 = 10 + CPCC_BEGIN,
    CPCC_02 = 11 + CPCC_BEGIN,
    CPCC_023 = 12 + CPCC_BEGIN,
    CPCC_01 = 13 + CPCC_BEGIN,
    CPCC_013 = 14 + CPCC_BEGIN,
    CPCC_012 = 15 + CPCC_BEGIN,

    REG_BEGIN = 48,
    REG_Z = 1 + REG_BEGIN,   // Is zero
    REG_LEZ = 2 + REG_BEGIN, // Less or equal to zero
    REG_LZ = 3 + REG_BEGIN,  // Less than zero
    REG_NZ = 5 + REG_BEGIN,  // Is not zero
    REG_GZ = 6 + REG_BEGIN,  // Greater than zero
    REG_GEZ = 7 + REG_BEGIN  // Greater than or equal to zero
  };
  }

  inline static const char *SPARCCondCodeToString(SPCC::CondCodes CC) {
    switch (CC) {
    case SPCC::ICC_A:   return "a";
    case SPCC::ICC_N:   return "n";
    case SPCC::ICC_NE:  return "ne";
    case SPCC::ICC_E:   return "e";
    case SPCC::ICC_G:   return "g";
    case SPCC::ICC_LE:  return "le";
    case SPCC::ICC_GE:  return "ge";
    case SPCC::ICC_L:   return "l";
    case SPCC::ICC_GU:  return "gu";
    case SPCC::ICC_LEU: return "leu";
    case SPCC::ICC_CC:  return "cc";
    case SPCC::ICC_CS:  return "cs";
    case SPCC::ICC_POS: return "pos";
    case SPCC::ICC_NEG: return "neg";
    case SPCC::ICC_VC:  return "vc";
    case SPCC::ICC_VS:  return "vs";
    case SPCC::FCC_A:   return "a";
    case SPCC::FCC_N:   return "n";
    case SPCC::FCC_U:   return "u";
    case SPCC::FCC_G:   return "g";
    case SPCC::FCC_UG:  return "ug";
    case SPCC::FCC_L:   return "l";
    case SPCC::FCC_UL:  return "ul";
    case SPCC::FCC_LG:  return "lg";
    case SPCC::FCC_NE:  return "ne";
    case SPCC::FCC_E:   return "e";
    case SPCC::FCC_UE:  return "ue";
    case SPCC::FCC_GE:  return "ge";
    case SPCC::FCC_UGE: return "uge";
    case SPCC::FCC_LE:  return "le";
    case SPCC::FCC_ULE: return "ule";
    case SPCC::FCC_O:   return "o";
    case SPCC::CPCC_A:   return "a";
    case SPCC::CPCC_N:   return "n";
    case SPCC::CPCC_3:   return "3";
    case SPCC::CPCC_2:   return "2";
    case SPCC::CPCC_23:  return "23";
    case SPCC::CPCC_1:   return "1";
    case SPCC::CPCC_13:  return "13";
    case SPCC::CPCC_12:  return "12";
    case SPCC::CPCC_123: return "123";
    case SPCC::CPCC_0:   return "0";
    case SPCC::CPCC_03:  return "03";
    case SPCC::CPCC_02:  return "02";
    case SPCC::CPCC_023: return "023";
    case SPCC::CPCC_01:  return "01";
    case SPCC::CPCC_013: return "013";
    case SPCC::CPCC_012: return "012";
    case SPCC::REG_BEGIN:
      llvm_unreachable("Use of reserved cond code");
    case SPCC::REG_Z:
      return "z";
    case SPCC::REG_LEZ:
      return "lez";
    case SPCC::REG_LZ:
      return "lz";
    case SPCC::REG_NZ:
      return "nz";
    case SPCC::REG_GZ:
      return "gz";
    case SPCC::REG_GEZ:
      return "gez";
    }
    llvm_unreachable("Invalid cond code");
  }

  inline static unsigned HI22(int64_t imm) {
    return (unsigned)((imm >> 10) & ((1 << 22)-1));
  }

  inline static unsigned LO10(int64_t imm) {
    return (unsigned)(imm & 0x3FF);
  }

  inline static unsigned HIX22(int64_t imm) {
    return HI22(~imm);
  }

  inline static unsigned LOX10(int64_t imm) {
    return ~LO10(~imm);
  }

}  // end namespace llvm
#endif
