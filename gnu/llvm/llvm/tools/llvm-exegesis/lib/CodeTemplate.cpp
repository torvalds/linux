//===-- CodeTemplate.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CodeTemplate.h"

namespace llvm {
namespace exegesis {

CodeTemplate::CodeTemplate(const CodeTemplate &) = default;

CodeTemplate::CodeTemplate(CodeTemplate &&) = default;

CodeTemplate &CodeTemplate::operator=(CodeTemplate &&) = default;

CodeTemplate &CodeTemplate::operator=(const CodeTemplate &) = default;

CodeTemplate CodeTemplate::clone() const {
  CodeTemplate CT = *this;
  return CT;
}

InstructionTemplate::InstructionTemplate(const Instruction *Instr)
    : Instr(Instr), VariableValues(Instr->Variables.size()) {}

InstructionTemplate::InstructionTemplate(InstructionTemplate &&) = default;

InstructionTemplate &InstructionTemplate::
operator=(InstructionTemplate &&) = default;

InstructionTemplate::InstructionTemplate(const InstructionTemplate &) = default;

InstructionTemplate &InstructionTemplate::
operator=(const InstructionTemplate &) = default;

unsigned InstructionTemplate::getOpcode() const {
  return Instr->Description.getOpcode();
}

MCOperand &InstructionTemplate::getValueFor(const Variable &Var) {
  return VariableValues[Var.getIndex()];
}

const MCOperand &InstructionTemplate::getValueFor(const Variable &Var) const {
  return VariableValues[Var.getIndex()];
}

MCOperand &InstructionTemplate::getValueFor(const Operand &Op) {
  return getValueFor(Instr->Variables[Op.getVariableIndex()]);
}

const MCOperand &InstructionTemplate::getValueFor(const Operand &Op) const {
  return getValueFor(Instr->Variables[Op.getVariableIndex()]);
}

bool InstructionTemplate::hasImmediateVariables() const {
  return any_of(Instr->Variables, [this](const Variable &Var) {
    return Instr->getPrimaryOperand(Var).isImmediate();
  });
}

MCInst InstructionTemplate::build() const {
  MCInst Result;
  Result.setOpcode(Instr->Description.Opcode);
  for (const auto &Op : Instr->Operands)
    if (Op.isExplicit())
      Result.addOperand(getValueFor(Op));
  return Result;
}

bool isEnumValue(ExecutionMode Execution) {
  return isPowerOf2_32(static_cast<uint32_t>(Execution));
}

StringRef getName(ExecutionMode Bit) {
  assert(isEnumValue(Bit) && "Bit must be a power of two");
  switch (Bit) {
  case ExecutionMode::UNKNOWN:
    return "UNKNOWN";
  case ExecutionMode::ALWAYS_SERIAL_IMPLICIT_REGS_ALIAS:
    return "ALWAYS_SERIAL_IMPLICIT_REGS_ALIAS";
  case ExecutionMode::ALWAYS_SERIAL_TIED_REGS_ALIAS:
    return "ALWAYS_SERIAL_TIED_REGS_ALIAS";
  case ExecutionMode::SERIAL_VIA_MEMORY_INSTR:
    return "SERIAL_VIA_MEMORY_INSTR";
  case ExecutionMode::SERIAL_VIA_EXPLICIT_REGS:
    return "SERIAL_VIA_EXPLICIT_REGS";
  case ExecutionMode::SERIAL_VIA_NON_MEMORY_INSTR:
    return "SERIAL_VIA_NON_MEMORY_INSTR";
  case ExecutionMode::ALWAYS_PARALLEL_MISSING_USE_OR_DEF:
    return "ALWAYS_PARALLEL_MISSING_USE_OR_DEF";
  case ExecutionMode::PARALLEL_VIA_EXPLICIT_REGS:
    return "PARALLEL_VIA_EXPLICIT_REGS";
  }
  llvm_unreachable("Missing enum case");
}

ArrayRef<ExecutionMode> getAllExecutionBits() {
  static const ExecutionMode kAllExecutionModeBits[] = {
      ExecutionMode::ALWAYS_SERIAL_IMPLICIT_REGS_ALIAS,
      ExecutionMode::ALWAYS_SERIAL_TIED_REGS_ALIAS,
      ExecutionMode::SERIAL_VIA_MEMORY_INSTR,
      ExecutionMode::SERIAL_VIA_EXPLICIT_REGS,
      ExecutionMode::SERIAL_VIA_NON_MEMORY_INSTR,
      ExecutionMode::ALWAYS_PARALLEL_MISSING_USE_OR_DEF,
      ExecutionMode::PARALLEL_VIA_EXPLICIT_REGS,
  };
  return ArrayRef(kAllExecutionModeBits);
}

SmallVector<ExecutionMode, 4> getExecutionModeBits(ExecutionMode Execution) {
  SmallVector<ExecutionMode, 4> Result;
  for (const auto Bit : getAllExecutionBits())
    if ((Execution & Bit) == Bit)
      Result.push_back(Bit);
  return Result;
}

} // namespace exegesis
} // namespace llvm
