//===-- SnippetRepetitor.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines helpers to fill functions with repetitions of a snippet.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_FUNCTIONFILLER_H
#define LLVM_TOOLS_LLVM_EXEGESIS_FUNCTIONFILLER_H

#include "Assembler.h"
#include "BenchmarkResult.h"
#include "LlvmState.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Object/Binary.h"

namespace llvm {
namespace exegesis {

class SnippetRepetitor {
public:
  static std::unique_ptr<const SnippetRepetitor>
  Create(Benchmark::RepetitionModeE Mode, const LLVMState &State,
         unsigned LoopRegister);

  virtual ~SnippetRepetitor();

  // Returns the set of registers that are reserved by the repetitor.
  virtual BitVector getReservedRegs() const = 0;

  // Returns a functor that repeats `Instructions` so that the function executes
  // at least `MinInstructions` instructions.
  virtual FillFunction Repeat(ArrayRef<MCInst> Instructions,
                              unsigned MinInstructions, unsigned LoopBodySize,
                              bool CleanupMemory) const = 0;

  explicit SnippetRepetitor(const LLVMState &State) : State(State) {}

protected:
  const LLVMState &State;
};

} // namespace exegesis
} // namespace llvm

#endif
