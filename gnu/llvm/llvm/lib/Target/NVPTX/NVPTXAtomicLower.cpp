//===-- NVPTXAtomicLower.cpp - Lower atomics of local memory ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Lower atomics of local memory to simple load/stores
//
//===----------------------------------------------------------------------===//

#include "NVPTXAtomicLower.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/LowerAtomic.h"

#include "MCTargetDesc/NVPTXBaseInfo.h"
using namespace llvm;

namespace {
// Hoisting the alloca instructions in the non-entry blocks to the entry
// block.
class NVPTXAtomicLower : public FunctionPass {
public:
  static char ID; // Pass ID
  NVPTXAtomicLower() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }

  StringRef getPassName() const override {
    return "NVPTX lower atomics of local memory";
  }

  bool runOnFunction(Function &F) override;
};
} // namespace

bool NVPTXAtomicLower::runOnFunction(Function &F) {
  SmallVector<AtomicRMWInst *> LocalMemoryAtomics;
  for (Instruction &I : instructions(F))
    if (AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(&I))
      if (RMWI->getPointerAddressSpace() == ADDRESS_SPACE_LOCAL)
        LocalMemoryAtomics.push_back(RMWI);

  bool Changed = false;
  for (AtomicRMWInst *RMWI : LocalMemoryAtomics)
    Changed |= lowerAtomicRMWInst(RMWI);
  return Changed;
}

char NVPTXAtomicLower::ID = 0;

namespace llvm {
void initializeNVPTXAtomicLowerPass(PassRegistry &);
}

INITIALIZE_PASS(NVPTXAtomicLower, "nvptx-atomic-lower",
                "Lower atomics of local memory to simple load/stores", false,
                false)

FunctionPass *llvm::createNVPTXAtomicLowerPass() {
  return new NVPTXAtomicLower();
}
