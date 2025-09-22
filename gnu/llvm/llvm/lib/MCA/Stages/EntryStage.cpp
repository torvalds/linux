//===---------------------- EntryStage.cpp ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the Fetch stage of an instruction pipeline.  Its sole
/// purpose in life is to produce instructions for the rest of the pipeline.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Stages/EntryStage.h"
#include "llvm/MCA/Instruction.h"

namespace llvm {
namespace mca {

bool EntryStage::hasWorkToComplete() const {
  return static_cast<bool>(CurrentInstruction) || !SM.isEnd();
}

bool EntryStage::isAvailable(const InstRef & /* unused */) const {
  if (CurrentInstruction)
    return checkNextStage(CurrentInstruction);
  return false;
}

Error EntryStage::getNextInstruction() {
  assert(!CurrentInstruction && "There is already an instruction to process!");
  if (!SM.hasNext()) {
    if (!SM.isEnd())
      return llvm::make_error<InstStreamPause>();
    else
      return llvm::ErrorSuccess();
  }
  SourceRef SR = SM.peekNext();
  std::unique_ptr<Instruction> Inst = std::make_unique<Instruction>(SR.second);
  CurrentInstruction = InstRef(SR.first, Inst.get());
  Instructions.emplace_back(std::move(Inst));
  SM.updateNext();
  return llvm::ErrorSuccess();
}

llvm::Error EntryStage::execute(InstRef & /*unused */) {
  assert(CurrentInstruction && "There is no instruction to process!");
  if (llvm::Error Val = moveToTheNextStage(CurrentInstruction))
    return Val;

  // Move the program counter.
  CurrentInstruction.invalidate();
  return getNextInstruction();
}

llvm::Error EntryStage::cycleStart() {
  if (!CurrentInstruction)
    return getNextInstruction();
  return llvm::ErrorSuccess();
}

llvm::Error EntryStage::cycleResume() {
  assert(!CurrentInstruction);
  return getNextInstruction();
}

llvm::Error EntryStage::cycleEnd() {
  // Find the first instruction which hasn't been retired.
  auto Range = drop_begin(Instructions, NumRetired);
  auto It = find_if(Range, [](const std::unique_ptr<Instruction> &I) {
    return !I->isRetired();
  });

  NumRetired = std::distance(Instructions.begin(), It);
  // Erase instructions up to the first that hasn't been retired.
  if ((NumRetired * 2) >= Instructions.size()) {
    Instructions.erase(Instructions.begin(), It);
    NumRetired = 0;
  }

  return llvm::ErrorSuccess();
}

} // namespace mca
} // namespace llvm
