//===-- AllocaHoisting.cpp - Hoist allocas to the entry block --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Hoist the alloca instructions in the non-entry blocks to the entry blocks.
//
//===----------------------------------------------------------------------===//

#include "NVPTXAllocaHoisting.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
using namespace llvm;

namespace {
// Hoisting the alloca instructions in the non-entry blocks to the entry
// block.
class NVPTXAllocaHoisting : public FunctionPass {
public:
  static char ID; // Pass ID
  NVPTXAllocaHoisting() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addPreserved<StackProtector>();
  }

  StringRef getPassName() const override {
    return "NVPTX specific alloca hoisting";
  }

  bool runOnFunction(Function &function) override;
};
} // namespace

bool NVPTXAllocaHoisting::runOnFunction(Function &function) {
  bool functionModified = false;
  Function::iterator I = function.begin();
  Instruction *firstTerminatorInst = (I++)->getTerminator();

  for (Function::iterator E = function.end(); I != E; ++I) {
    for (BasicBlock::iterator BI = I->begin(), BE = I->end(); BI != BE;) {
      AllocaInst *allocaInst = dyn_cast<AllocaInst>(BI++);
      if (allocaInst && isa<ConstantInt>(allocaInst->getArraySize())) {
        allocaInst->moveBefore(firstTerminatorInst);
        functionModified = true;
      }
    }
  }

  return functionModified;
}

char NVPTXAllocaHoisting::ID = 0;

namespace llvm {
void initializeNVPTXAllocaHoistingPass(PassRegistry &);
}

INITIALIZE_PASS(
    NVPTXAllocaHoisting, "alloca-hoisting",
    "Hoisting alloca instructions in non-entry blocks to the entry block",
    false, false)

FunctionPass *llvm::createAllocaHoisting() { return new NVPTXAllocaHoisting; }
