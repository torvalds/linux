//===-- llvm/MC/MCInstBuilder.h - Simplify creation of MCInsts --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MCInstBuilder class for convenient creation of
// MCInsts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTBUILDER_H
#define LLVM_MC_MCINSTBUILDER_H

#include "llvm/MC/MCInst.h"

namespace llvm {

class MCInstBuilder {
  MCInst Inst;

public:
  /// Create a new MCInstBuilder for an MCInst with a specific opcode.
  MCInstBuilder(unsigned Opcode) {
    Inst.setOpcode(Opcode);
  }

  /// Set the location.
  MCInstBuilder &setLoc(SMLoc SM) {
    Inst.setLoc(SM);
    return *this;
  }

  /// Add a new register operand.
  MCInstBuilder &addReg(unsigned Reg) {
    Inst.addOperand(MCOperand::createReg(Reg));
    return *this;
  }

  /// Add a new integer immediate operand.
  MCInstBuilder &addImm(int64_t Val) {
    Inst.addOperand(MCOperand::createImm(Val));
    return *this;
  }

  /// Add a new single floating point immediate operand.
  MCInstBuilder &addSFPImm(uint32_t Val) {
    Inst.addOperand(MCOperand::createSFPImm(Val));
    return *this;
  }

  /// Add a new floating point immediate operand.
  MCInstBuilder &addDFPImm(uint64_t Val) {
    Inst.addOperand(MCOperand::createDFPImm(Val));
    return *this;
  }

  /// Add a new MCExpr operand.
  MCInstBuilder &addExpr(const MCExpr *Val) {
    Inst.addOperand(MCOperand::createExpr(Val));
    return *this;
  }

  /// Add a new MCInst operand.
  MCInstBuilder &addInst(const MCInst *Val) {
    Inst.addOperand(MCOperand::createInst(Val));
    return *this;
  }

  /// Add an operand.
  MCInstBuilder &addOperand(const MCOperand &Op) {
    Inst.addOperand(Op);
    return *this;
  }

  operator MCInst&() {
    return Inst;
  }
};

} // end namespace llvm

#endif
