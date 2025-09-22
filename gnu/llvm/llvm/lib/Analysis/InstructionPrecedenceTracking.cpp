//===-- InstructionPrecedenceTracking.cpp -----------------------*- C++ -*-===//
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

#include "llvm/Analysis/InstructionPrecedenceTracking.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ipt"
STATISTIC(NumInstScanned, "Number of insts scanned while updating ibt");

#ifndef NDEBUG
static cl::opt<bool> ExpensiveAsserts(
    "ipt-expensive-asserts",
    cl::desc("Perform expensive assert validation on every query to Instruction"
             " Precedence Tracking"),
    cl::init(false), cl::Hidden);
#endif

const Instruction *InstructionPrecedenceTracking::getFirstSpecialInstruction(
    const BasicBlock *BB) {
#ifndef NDEBUG
  // If there is a bug connected to invalid cache, turn on ExpensiveAsserts to
  // catch this situation as early as possible.
  if (ExpensiveAsserts)
    validateAll();
  else
    validate(BB);
#endif

  if (!FirstSpecialInsts.contains(BB)) {
    fill(BB);
    assert(FirstSpecialInsts.contains(BB) && "Must be!");
  }
  return FirstSpecialInsts[BB];
}

bool InstructionPrecedenceTracking::hasSpecialInstructions(
    const BasicBlock *BB) {
  return getFirstSpecialInstruction(BB) != nullptr;
}

bool InstructionPrecedenceTracking::isPreceededBySpecialInstruction(
    const Instruction *Insn) {
  const Instruction *MaybeFirstSpecial =
      getFirstSpecialInstruction(Insn->getParent());
  return MaybeFirstSpecial && MaybeFirstSpecial->comesBefore(Insn);
}

void InstructionPrecedenceTracking::fill(const BasicBlock *BB) {
  FirstSpecialInsts.erase(BB);
  for (const auto &I : *BB) {
    NumInstScanned++;
    if (isSpecialInstruction(&I)) {
      FirstSpecialInsts[BB] = &I;
      return;
    }
  }

  // Mark this block as having no special instructions.
  FirstSpecialInsts[BB] = nullptr;
}

#ifndef NDEBUG
void InstructionPrecedenceTracking::validate(const BasicBlock *BB) const {
  auto It = FirstSpecialInsts.find(BB);
  // Bail if we don't have anything cached for this block.
  if (It == FirstSpecialInsts.end())
    return;

  for (const Instruction &Insn : *BB)
    if (isSpecialInstruction(&Insn)) {
      assert(It->second == &Insn &&
             "Cached first special instruction is wrong!");
      return;
    }

  assert(It->second == nullptr &&
         "Block is marked as having special instructions but in fact it  has "
         "none!");
}

void InstructionPrecedenceTracking::validateAll() const {
  // Check that for every known block the cached value is correct.
  for (const auto &It : FirstSpecialInsts)
    validate(It.first);
}
#endif

void InstructionPrecedenceTracking::insertInstructionTo(const Instruction *Inst,
                                                        const BasicBlock *BB) {
  if (isSpecialInstruction(Inst))
    FirstSpecialInsts.erase(BB);
}

void InstructionPrecedenceTracking::removeInstruction(const Instruction *Inst) {
  auto *BB = Inst->getParent();
  assert(BB && "must be called before instruction is actually removed");
  if (FirstSpecialInsts.count(BB) && FirstSpecialInsts[BB] == Inst)
    FirstSpecialInsts.erase(BB);
}

void InstructionPrecedenceTracking::removeUsersOf(const Instruction *Inst) {
  for (const auto *U : Inst->users()) {
    if (const auto *UI = dyn_cast<Instruction>(U))
      removeInstruction(UI);
  }
}

void InstructionPrecedenceTracking::clear() {
  FirstSpecialInsts.clear();
#ifndef NDEBUG
  // The map should be valid after clearing (at least empty).
  validateAll();
#endif
}

bool ImplicitControlFlowTracking::isSpecialInstruction(
    const Instruction *Insn) const {
  // If a block's instruction doesn't always pass the control to its successor
  // instruction, mark the block as having implicit control flow. We use them
  // to avoid wrong assumptions of sort "if A is executed and B post-dominates
  // A, then B is also executed". This is not true is there is an implicit
  // control flow instruction (e.g. a guard) between them.
  return !isGuaranteedToTransferExecutionToSuccessor(Insn);
}

bool MemoryWriteTracking::isSpecialInstruction(
    const Instruction *Insn) const {
  using namespace PatternMatch;
  if (match(Insn, m_Intrinsic<Intrinsic::experimental_widenable_condition>()))
    return false;
  return Insn->mayWriteToMemory();
}
