//===-- IndirectCallVisitor.h - indirect call visitor ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements defines a visitor class and a helper function that find
// all indirect call-sites in a function.

#ifndef LLVM_ANALYSIS_INDIRECTCALLVISITOR_H
#define LLVM_ANALYSIS_INDIRECTCALLVISITOR_H

#include "llvm/IR/InstVisitor.h"
#include <vector>

namespace llvm {
// Visitor class that finds indirect calls or instructions that gives vtable
// value, depending on Type.
struct PGOIndirectCallVisitor : public InstVisitor<PGOIndirectCallVisitor> {
  enum class InstructionType {
    kIndirectCall = 0,
    kVTableVal = 1,
  };
  std::vector<CallBase *> IndirectCalls;
  std::vector<Instruction *> ProfiledAddresses;
  PGOIndirectCallVisitor(InstructionType Type) : Type(Type) {}

  // Given an indirect call instruction, try to find the the following pattern
  //
  // %vtable = load ptr, ptr %obj
  // %vfn = getelementptr inbounds ptr, ptr %vtable, i64 1
  // %2 = load ptr, ptr %vfn
  // $call = tail call i32 %2
  //
  // A heuristic is used to find the address feeding instructions.
  static Instruction *tryGetVTableInstruction(CallBase *CB) {
    assert(CB != nullptr && "Caller guaranteed");
    if (!CB->isIndirectCall())
      return nullptr;

    LoadInst *LI = dyn_cast<LoadInst>(CB->getCalledOperand());
    if (LI != nullptr) {
      Value *FuncPtr = LI->getPointerOperand(); // GEP (or bitcast)
      Value *VTablePtr = FuncPtr->stripInBoundsConstantOffsets();
      // FIXME: Add support in the frontend so LLVM type intrinsics are
      // emitted without LTO. This way, added intrinsics could filter
      // non-vtable instructions and reduce instrumentation overhead.
      // Since a non-vtable profiled address is not within the address
      // range of vtable objects, it's stored as zero in indexed profiles.
      // A pass that looks up symbol with an zero hash will (almost) always
      // find nullptr and skip the actual transformation (e.g., comparison
      // of symbols). So the performance overhead from non-vtable profiled
      // address is negligible if exists at all. Comparing loaded address
      // with symbol address guarantees correctness.
      if (VTablePtr != nullptr && isa<Instruction>(VTablePtr))
        return cast<Instruction>(VTablePtr);
    }
    return nullptr;
  }

  void visitCallBase(CallBase &Call) {
    if (Call.isIndirectCall()) {
      IndirectCalls.push_back(&Call);

      if (Type != InstructionType::kVTableVal)
        return;

      Instruction *VPtr =
          PGOIndirectCallVisitor::tryGetVTableInstruction(&Call);
      if (VPtr)
        ProfiledAddresses.push_back(VPtr);
    }
  }

private:
  InstructionType Type;
};

inline std::vector<CallBase *> findIndirectCalls(Function &F) {
  PGOIndirectCallVisitor ICV(
      PGOIndirectCallVisitor::InstructionType::kIndirectCall);
  ICV.visit(F);
  return ICV.IndirectCalls;
}

inline std::vector<Instruction *> findVTableAddrs(Function &F) {
  PGOIndirectCallVisitor ICV(
      PGOIndirectCallVisitor::InstructionType::kVTableVal);
  ICV.visit(F);
  return ICV.ProfiledAddresses;
}

} // namespace llvm

#endif
