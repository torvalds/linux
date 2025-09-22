//===-- MCInstrDescView.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Provide views around LLVM structures to represents an instruction instance,
/// as well as its implicit and explicit arguments in a uniform way.
/// Arguments that are explicit and independant (non tied) also have a Variable
/// associated to them so the instruction can be fully defined by reading its
/// Variables.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_MCINSTRDESCVIEW_H
#define LLVM_TOOLS_LLVM_EXEGESIS_MCINSTRDESCVIEW_H

#include <memory>
#include <random>
#include <unordered_map>

#include "RegisterAliasing.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"

namespace llvm {
namespace exegesis {

// A variable represents the value associated to an Operand or a set of Operands
// if they are tied together.
struct Variable {
  // Returns the index of this Variable inside Instruction's Variable.
  unsigned getIndex() const;

  // Returns the index of the Operand linked to this Variable.
  unsigned getPrimaryOperandIndex() const;

  // Returns whether this Variable has more than one Operand linked to it.
  bool hasTiedOperands() const;

  // The indices of the operands tied to this Variable.
  SmallVector<unsigned, 2> TiedOperands;

  // The index of this Variable in Instruction.Variables and its associated
  // Value in InstructionBuilder.VariableValues.
  std::optional<uint8_t> Index;
};

// MCOperandInfo can only represents Explicit operands. This object gives a
// uniform view of Implicit and Explicit Operands.
// - Index: can be used to refer to MCInstrDesc::operands for Explicit operands.
// - Tracker: is set for Register Operands and is used to keep track of possible
// registers and the registers reachable from them (aliasing registers).
// - Info: a shortcut for MCInstrDesc::operands()[Index].
// - TiedToIndex: the index of the Operand holding the value or -1.
// - ImplicitReg: the register value when Operand is Implicit, 0 otherwise.
// - VariableIndex: the index of the Variable holding the value for this Operand
// or -1 if this operand is implicit.
struct Operand {
  bool isExplicit() const;
  bool isImplicit() const;
  bool isImplicitReg() const;
  bool isDef() const;
  bool isUse() const;
  bool isReg() const;
  bool isTied() const;
  bool isVariable() const;
  bool isMemory() const;
  bool isImmediate() const;
  unsigned getIndex() const;
  unsigned getTiedToIndex() const;
  unsigned getVariableIndex() const;
  unsigned getImplicitReg() const;
  const RegisterAliasingTracker &getRegisterAliasing() const;
  const MCOperandInfo &getExplicitOperandInfo() const;

  // Please use the accessors above and not the following fields.
  std::optional<uint8_t> Index;
  bool IsDef = false;
  const RegisterAliasingTracker *Tracker = nullptr; // Set for Register Op.
  const MCOperandInfo *Info = nullptr;              // Set for Explicit Op.
  std::optional<uint8_t> TiedToIndex;               // Set for Reg&Explicit Op.
  MCPhysReg ImplicitReg = 0;                        // Non-0 for Implicit Op.
  std::optional<uint8_t> VariableIndex;             // Set for Explicit Op.
};

/// A cache of BitVector to reuse between Instructions.
/// The cache will only be exercised during Instruction initialization.
/// For X86, this is ~160 unique vectors for all of the ~15K Instructions.
struct BitVectorCache {
  // Finds or allocates the provided BitVector in the cache and retrieves it's
  // unique instance.
  const BitVector *getUnique(BitVector &&BV) const;

private:
  mutable std::vector<std::unique_ptr<BitVector>> Cache;
};

// A view over an MCInstrDesc offering a convenient interface to compute
// Register aliasing.
struct Instruction {
  // Create an instruction for a particular Opcode.
  static std::unique_ptr<Instruction>
  create(const MCInstrInfo &InstrInfo, const RegisterAliasingTrackerCache &RATC,
         const BitVectorCache &BVC, unsigned Opcode);

  // Prevent copy or move, instructions are allocated once and cached.
  Instruction(const Instruction &) = delete;
  Instruction(Instruction &&) = delete;
  Instruction &operator=(const Instruction &) = delete;
  Instruction &operator=(Instruction &&) = delete;

  // Returns the Operand linked to this Variable.
  // In case the Variable is tied, the primary (i.e. Def) Operand is returned.
  const Operand &getPrimaryOperand(const Variable &Var) const;

  // Whether this instruction is self aliasing through its tied registers.
  // Repeating this instruction is guaranteed to executes sequentially.
  bool hasTiedRegisters() const;

  // Whether this instruction is self aliasing through its implicit registers.
  // Repeating this instruction is guaranteed to executes sequentially.
  bool hasAliasingImplicitRegisters() const;

  // Whether this instruction is self aliasing through some registers.
  // Repeating this instruction may execute sequentially by picking aliasing
  // Use and Def registers. It may also execute in parallel by picking non
  // aliasing Use and Def registers.
  bool hasAliasingRegisters(const BitVector &ForbiddenRegisters) const;

  // Whether this instruction's registers alias with OtherInstr's registers.
  bool hasAliasingRegistersThrough(const Instruction &OtherInstr,
                                   const BitVector &ForbiddenRegisters) const;

  // Returns whether this instruction has Memory Operands.
  // Repeating this instruction executes sequentially with an instruction that
  // reads or write the same memory region.
  bool hasMemoryOperands() const;

  // Returns whether this instruction as at least one use or one def.
  // Repeating this instruction may execute sequentially by adding an
  // instruction that aliases one of these.
  bool hasOneUseOrOneDef() const;

  // Convenient function to help with debugging.
  void dump(const MCRegisterInfo &RegInfo,
            const RegisterAliasingTrackerCache &RATC,
            raw_ostream &Stream) const;

  const MCInstrDesc &Description;
  const StringRef Name; // The name of this instruction.
  const SmallVector<Operand, 8> Operands;
  const SmallVector<Variable, 4> Variables;
  const BitVector &ImplDefRegs; // The set of aliased implicit def registers.
  const BitVector &ImplUseRegs; // The set of aliased implicit use registers.
  const BitVector &AllDefRegs;  // The set of all aliased def registers.
  const BitVector &AllUseRegs;  // The set of all aliased use registers.
private:
  Instruction(const MCInstrDesc *Description, StringRef Name,
              SmallVector<Operand, 8> Operands,
              SmallVector<Variable, 4> Variables, const BitVector *ImplDefRegs,
              const BitVector *ImplUseRegs, const BitVector *AllDefRegs,
              const BitVector *AllUseRegs);
};

// Instructions are expensive to instantiate. This class provides a cache of
// Instructions with lazy construction.
struct InstructionsCache {
  InstructionsCache(const MCInstrInfo &InstrInfo,
                    const RegisterAliasingTrackerCache &RATC);

  // Returns the Instruction object corresponding to this Opcode.
  const Instruction &getInstr(unsigned Opcode) const;

private:
  const MCInstrInfo &InstrInfo;
  const RegisterAliasingTrackerCache &RATC;
  mutable std::unordered_map<unsigned, std::unique_ptr<Instruction>>
      Instructions;
  const BitVectorCache BVC;
};

// Represents the assignment of a Register to an Operand.
struct RegisterOperandAssignment {
  RegisterOperandAssignment(const Operand *Operand, MCPhysReg Reg)
      : Op(Operand), Reg(Reg) {}

  const Operand *Op; // Pointer to an Explicit Register Operand.
  MCPhysReg Reg;

  bool operator==(const RegisterOperandAssignment &other) const;
};

// Represents a set of Operands that would alias through the use of some
// Registers.
// There are two reasons why operands would alias:
// - The registers assigned to each of the operands are the same or alias each
//   other (e.g. AX/AL)
// - The operands are tied.
struct AliasingRegisterOperands {
  SmallVector<RegisterOperandAssignment, 1> Defs; // Unlikely size() > 1.
  SmallVector<RegisterOperandAssignment, 2> Uses;

  // True is Defs and Use contain an Implicit Operand.
  bool hasImplicitAliasing() const;

  bool operator==(const AliasingRegisterOperands &other) const;
};

// Returns all possible configurations leading Def registers of DefInstruction
// to alias with Use registers of UseInstruction.
struct AliasingConfigurations {
  AliasingConfigurations(const Instruction &DefInstruction,
                         const Instruction &UseInstruction,
                         const BitVector &ForbiddenRegisters);

  bool empty() const; // True if no aliasing configuration is found.
  bool hasImplicitAliasing() const;

  SmallVector<AliasingRegisterOperands, 32> Configurations;
};

// Writes MCInst to OS.
// This is not assembly but the internal LLVM's name for instructions and
// registers.
void DumpMCInst(const MCRegisterInfo &MCRegisterInfo,
                const MCInstrInfo &MCInstrInfo, const MCInst &MCInst,
                raw_ostream &OS);

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_MCINSTRDESCVIEW_H
