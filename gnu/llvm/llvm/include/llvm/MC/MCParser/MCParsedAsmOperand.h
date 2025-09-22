//===- llvm/MC/MCParsedAsmOperand.h - Asm Parser Operand --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_MCPARSEDASMOPERAND_H
#define LLVM_MC_MCPARSER_MCPARSEDASMOPERAND_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SMLoc.h"
#include <string>

namespace llvm {

class MCRegister;
class raw_ostream;

/// MCParsedAsmOperand - This abstract class represents a source-level assembly
/// instruction operand.  It should be subclassed by target-specific code.  This
/// base class is used by target-independent clients and is the interface
/// between parsing an asm instruction and recognizing it.
class MCParsedAsmOperand {
  /// MCOperandNum - The corresponding MCInst operand number.  Only valid when
  /// parsing MS-style inline assembly.
  unsigned MCOperandNum = ~0u;

  /// Constraint - The constraint on this operand.  Only valid when parsing
  /// MS-style inline assembly.
  std::string Constraint;

protected:
  // This only seems to need to be movable (by ARMOperand) but ARMOperand has
  // lots of members and MSVC doesn't support defaulted move ops, so to avoid
  // that verbosity, just rely on defaulted copy ops. It's only the Constraint
  // string member that would benefit from movement anyway.
  MCParsedAsmOperand() = default;
  MCParsedAsmOperand(const MCParsedAsmOperand &RHS) = default;
  MCParsedAsmOperand &operator=(const MCParsedAsmOperand &) = default;

public:
  virtual ~MCParsedAsmOperand() = default;

  void setConstraint(StringRef C) { Constraint = C.str(); }
  StringRef getConstraint() { return Constraint; }

  void setMCOperandNum (unsigned OpNum) { MCOperandNum = OpNum; }
  unsigned getMCOperandNum() { return MCOperandNum; }

  virtual StringRef getSymName() { return StringRef(); }
  virtual void *getOpDecl() { return nullptr; }

  /// isToken - Is this a token operand?
  virtual bool isToken() const = 0;
  /// isImm - Is this an immediate operand?
  virtual bool isImm() const = 0;
  /// isReg - Is this a register operand?
  virtual bool isReg() const = 0;
  virtual MCRegister getReg() const = 0;

  /// isMem - Is this a memory operand?
  virtual bool isMem() const = 0;

  /// isMemUseUpRegs - Is memory operand use up regs, for example, intel MS
  /// inline asm may use ARR[baseReg + IndexReg + ...] which may use up regs
  /// in [...] expr, so ARR[baseReg + IndexReg + ...] can not use extra reg
  /// for ARR. For example, calculating ARR address to a reg or use another
  /// base reg in PIC model.
  virtual bool isMemUseUpRegs() const { return false; }

  /// getStartLoc - Get the location of the first token of this operand.
  virtual SMLoc getStartLoc() const = 0;
  /// getEndLoc - Get the location of the last token of this operand.
  virtual SMLoc getEndLoc() const = 0;

  /// needAddressOf - Do we need to emit code to get the address of the
  /// variable/label?   Only valid when parsing MS-style inline assembly.
  virtual bool needAddressOf() const { return false; }

  /// isOffsetOfLocal - Do we need to emit code to get the offset of the local
  /// variable, rather than its value?   Only valid when parsing MS-style inline
  /// assembly.
  virtual bool isOffsetOfLocal() const { return false; }

  /// getOffsetOfLoc - Get the location of the offset operator.
  virtual SMLoc getOffsetOfLoc() const { return SMLoc(); }

  /// print - Print a debug representation of the operand to the given stream.
  virtual void print(raw_ostream &OS) const = 0;

  /// dump - Print to the debug stream.
  virtual void dump() const;
};

//===----------------------------------------------------------------------===//
// Debugging Support

inline raw_ostream& operator<<(raw_ostream &OS, const MCParsedAsmOperand &MO) {
  MO.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_MCPARSEDASMOPERAND_H
