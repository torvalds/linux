//===-- llvm/MC/MCInstrInfo.h - Target Instruction Info ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the target machine instruction set.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTRINFO_H
#define LLVM_MC_MCINSTRINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInstrDesc.h"
#include <cassert>

namespace llvm {

class MCSubtargetInfo;

//---------------------------------------------------------------------------
/// Interface to description of machine instruction set.
class MCInstrInfo {
public:
  using ComplexDeprecationPredicate = bool (*)(MCInst &,
                                               const MCSubtargetInfo &,
                                               std::string &);

private:
  const MCInstrDesc *LastDesc;      // Raw array to allow static init'n
  const unsigned *InstrNameIndices; // Array for name indices in InstrNameData
  const char *InstrNameData;        // Instruction name string pool
  // Subtarget feature that an instruction is deprecated on, if any
  // -1 implies this is not deprecated by any single feature. It may still be
  // deprecated due to a "complex" reason, below.
  const uint8_t *DeprecatedFeatures;
  // A complex method to determine if a certain instruction is deprecated or
  // not, and return the reason for deprecation.
  const ComplexDeprecationPredicate *ComplexDeprecationInfos;
  unsigned NumOpcodes;              // Number of entries in the desc array

public:
  /// Initialize MCInstrInfo, called by TableGen auto-generated routines.
  /// *DO NOT USE*.
  void InitMCInstrInfo(const MCInstrDesc *D, const unsigned *NI, const char *ND,
                       const uint8_t *DF,
                       const ComplexDeprecationPredicate *CDI, unsigned NO) {
    LastDesc = D + NO - 1;
    InstrNameIndices = NI;
    InstrNameData = ND;
    DeprecatedFeatures = DF;
    ComplexDeprecationInfos = CDI;
    NumOpcodes = NO;
  }

  unsigned getNumOpcodes() const { return NumOpcodes; }

  /// Return the machine instruction descriptor that corresponds to the
  /// specified instruction opcode.
  const MCInstrDesc &get(unsigned Opcode) const {
    assert(Opcode < NumOpcodes && "Invalid opcode!");
    // The table is indexed backwards from the last entry.
    return *(LastDesc - Opcode);
  }

  /// Returns the name for the instructions with the given opcode.
  StringRef getName(unsigned Opcode) const {
    assert(Opcode < NumOpcodes && "Invalid opcode!");
    return StringRef(&InstrNameData[InstrNameIndices[Opcode]]);
  }

  /// Returns true if a certain instruction is deprecated and if so
  /// returns the reason in \p Info.
  bool getDeprecatedInfo(MCInst &MI, const MCSubtargetInfo &STI,
                         std::string &Info) const;
};

} // End llvm namespace

#endif
