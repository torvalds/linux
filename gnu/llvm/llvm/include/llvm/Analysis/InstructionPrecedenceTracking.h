//===-- InstructionPrecedenceTracking.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Implements a class that is able to define some instructions as "special"
// (e.g. as having implicit control flow, or writing memory, or having another
// interesting property) and then efficiently answers queries of the types:
// 1. Are there any special instructions in the block of interest?
// 2. Return first of the special instructions in the given block;
// 3. Check if the given instruction is preceeded by the first special
//    instruction in the same block.
// The class provides caching that allows to answer these queries quickly. The
// user must make sure that the cached data is invalidated properly whenever
// a content of some tracked block is changed.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INSTRUCTIONPRECEDENCETRACKING_H
#define LLVM_ANALYSIS_INSTRUCTIONPRECEDENCETRACKING_H

#include "llvm/ADT/DenseMap.h"

namespace llvm {

class BasicBlock;
class Instruction;

class InstructionPrecedenceTracking {
  // Maps a block to the topmost special instruction in it. If the value is
  // nullptr, it means that it is known that this block does not contain any
  // special instructions.
  DenseMap<const BasicBlock *, const Instruction *> FirstSpecialInsts;

  // Fills information about the given block's special instructions.
  void fill(const BasicBlock *BB);

#ifndef NDEBUG
  /// Asserts that the cached info for \p BB is up-to-date. This helps to catch
  /// the usage error of accessing a block without properly invalidating after a
  /// previous transform.
  void validate(const BasicBlock *BB) const;

  /// Asserts whether or not the contents of this tracking is up-to-date. This
  /// helps to catch the usage error of accessing a block without properly
  /// invalidating after a previous transform.
  void validateAll() const;
#endif

protected:
  /// Returns the topmost special instruction from the block \p BB. Returns
  /// nullptr if there is no special instructions in the block.
  const Instruction *getFirstSpecialInstruction(const BasicBlock *BB);

  /// Returns true iff at least one instruction from the basic block \p BB is
  /// special.
  bool hasSpecialInstructions(const BasicBlock *BB);

  /// Returns true iff the first special instruction of \p Insn's block exists
  /// and dominates \p Insn.
  bool isPreceededBySpecialInstruction(const Instruction *Insn);

  /// A predicate that defines whether or not the instruction \p Insn is
  /// considered special and needs to be tracked. Implementing this method in
  /// children classes allows to implement tracking of implicit control flow,
  /// memory writing instructions or any other kinds of instructions we might
  /// be interested in.
  virtual bool isSpecialInstruction(const Instruction *Insn) const = 0;

  virtual ~InstructionPrecedenceTracking() = default;

public:
  /// Notifies this tracking that we are going to insert a new instruction \p
  /// Inst to the basic block \p BB. It makes all necessary updates to internal
  /// caches to keep them consistent.
  void insertInstructionTo(const Instruction *Inst, const BasicBlock *BB);

  /// Notifies this tracking that we are going to remove the instruction \p Inst
  /// It makes all necessary updates to internal caches to keep them consistent.
  void removeInstruction(const Instruction *Inst);

  /// Notifies this tracking that we are going to replace all uses of \p Inst.
  /// It makes all necessary updates to internal caches to keep them consistent.
  /// Should typically be called before a RAUW.
  void removeUsersOf(const Instruction *Inst);

  /// Invalidates all information from this tracking.
  void clear();
};

/// This class allows to keep track on instructions with implicit control flow.
/// These are instructions that may not pass execution to their successors. For
/// example, throwing calls and guards do not always do this. If we need to know
/// for sure that some instruction is guaranteed to execute if the given block
/// is reached, then we need to make sure that there is no implicit control flow
/// instruction (ICFI) preceding it. For example, this check is required if we
/// perform PRE moving non-speculable instruction to other place.
class ImplicitControlFlowTracking : public InstructionPrecedenceTracking {
public:
  /// Returns the topmost instruction with implicit control flow from the given
  /// basic block. Returns nullptr if there is no such instructions in the block.
  const Instruction *getFirstICFI(const BasicBlock *BB) {
    return getFirstSpecialInstruction(BB);
  }

  /// Returns true if at least one instruction from the given basic block has
  /// implicit control flow.
  bool hasICF(const BasicBlock *BB) {
    return hasSpecialInstructions(BB);
  }

  /// Returns true if the first ICFI of Insn's block exists and dominates Insn.
  bool isDominatedByICFIFromSameBlock(const Instruction *Insn) {
    return isPreceededBySpecialInstruction(Insn);
  }

  bool isSpecialInstruction(const Instruction *Insn) const override;
};

class MemoryWriteTracking : public InstructionPrecedenceTracking {
public:
  /// Returns the topmost instruction that may write memory from the given
  /// basic block. Returns nullptr if there is no such instructions in the block.
  const Instruction *getFirstMemoryWrite(const BasicBlock *BB) {
    return getFirstSpecialInstruction(BB);
  }

  /// Returns true if at least one instruction from the given basic block may
  /// write memory.
  bool mayWriteToMemory(const BasicBlock *BB) {
    return hasSpecialInstructions(BB);
  }

  /// Returns true if the first memory writing instruction of Insn's block
  /// exists and dominates Insn.
  bool isDominatedByMemoryWriteFromSameBlock(const Instruction *Insn) {
    return isPreceededBySpecialInstruction(Insn);
  }

  bool isSpecialInstruction(const Instruction *Insn) const override;
};

} // llvm

#endif // LLVM_ANALYSIS_INSTRUCTIONPRECEDENCETRACKING_H
