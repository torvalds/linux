//===-- AMDGPUAnnotateUniformValues.cpp - ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass adds amdgpu.uniform metadata to IR values so this information
/// can be used during instruction selection.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "Utils/AMDGPUMemoryUtils.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/InitializePasses.h"

#define DEBUG_TYPE "amdgpu-annotate-uniform"

using namespace llvm;

namespace {

class AMDGPUAnnotateUniformValues : public FunctionPass,
                       public InstVisitor<AMDGPUAnnotateUniformValues> {
  UniformityInfo *UA;
  MemorySSA *MSSA;
  AliasAnalysis *AA;
  bool isEntryFunc;
  bool Changed;

  void setUniformMetadata(Instruction *I) {
    I->setMetadata("amdgpu.uniform", MDNode::get(I->getContext(), {}));
    Changed = true;
  }

  void setNoClobberMetadata(Instruction *I) {
    I->setMetadata("amdgpu.noclobber", MDNode::get(I->getContext(), {}));
    Changed = true;
  }

public:
  static char ID;
  AMDGPUAnnotateUniformValues() :
    FunctionPass(ID) { }
  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;
  StringRef getPassName() const override {
    return "AMDGPU Annotate Uniform Values";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<UniformityInfoWrapperPass>();
    AU.addRequired<MemorySSAWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.setPreservesAll();
 }

  void visitBranchInst(BranchInst &I);
  void visitLoadInst(LoadInst &I);
};

} // End anonymous namespace

INITIALIZE_PASS_BEGIN(AMDGPUAnnotateUniformValues, DEBUG_TYPE,
                      "Add AMDGPU uniform metadata", false, false)
INITIALIZE_PASS_DEPENDENCY(UniformityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(AMDGPUAnnotateUniformValues, DEBUG_TYPE,
                    "Add AMDGPU uniform metadata", false, false)

char AMDGPUAnnotateUniformValues::ID = 0;

void AMDGPUAnnotateUniformValues::visitBranchInst(BranchInst &I) {
  if (UA->isUniform(&I))
    setUniformMetadata(&I);
}

void AMDGPUAnnotateUniformValues::visitLoadInst(LoadInst &I) {
  Value *Ptr = I.getPointerOperand();
  if (!UA->isUniform(Ptr))
    return;
  Instruction *PtrI = dyn_cast<Instruction>(Ptr);
  if (PtrI)
    setUniformMetadata(PtrI);

  // We're tracking up to the Function boundaries, and cannot go beyond because
  // of FunctionPass restrictions. We can ensure that is memory not clobbered
  // for memory operations that are live in to entry points only.
  if (!isEntryFunc)
    return;
  bool GlobalLoad = I.getPointerAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS;
  if (GlobalLoad && !AMDGPU::isClobberedInFunction(&I, MSSA, AA))
    setNoClobberMetadata(&I);
}

bool AMDGPUAnnotateUniformValues::doInitialization(Module &M) {
  return false;
}

bool AMDGPUAnnotateUniformValues::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  UA = &getAnalysis<UniformityInfoWrapperPass>().getUniformityInfo();
  MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  isEntryFunc = AMDGPU::isEntryFunctionCC(F.getCallingConv());

  Changed = false;
  visit(F);
  return Changed;
}

FunctionPass *
llvm::createAMDGPUAnnotateUniformValues() {
  return new AMDGPUAnnotateUniformValues();
}
