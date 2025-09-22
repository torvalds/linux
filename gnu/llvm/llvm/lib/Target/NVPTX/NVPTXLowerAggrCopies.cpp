//===- NVPTXLowerAggrCopies.cpp - ------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// Lower aggregate copies, memset, memcpy, memmov intrinsics into loops when
// the size is large or is not a compile-time constant.
//
//===----------------------------------------------------------------------===//

#include "NVPTXLowerAggrCopies.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"

#define DEBUG_TYPE "nvptx"

using namespace llvm;

namespace {

// actual analysis class, which is a functionpass
struct NVPTXLowerAggrCopies : public FunctionPass {
  static char ID;

  NVPTXLowerAggrCopies() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addPreserved<StackProtector>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
  }

  bool runOnFunction(Function &F) override;

  static const unsigned MaxAggrCopySize = 128;

  StringRef getPassName() const override {
    return "Lower aggregate copies/intrinsics into loops";
  }
};

char NVPTXLowerAggrCopies::ID = 0;

bool NVPTXLowerAggrCopies::runOnFunction(Function &F) {
  SmallVector<LoadInst *, 4> AggrLoads;
  SmallVector<MemIntrinsic *, 4> MemCalls;

  const DataLayout &DL = F.getDataLayout();
  LLVMContext &Context = F.getParent()->getContext();
  const TargetTransformInfo &TTI =
      getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);

  // Collect all aggregate loads and mem* calls.
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
        if (!LI->hasOneUse())
          continue;

        if (DL.getTypeStoreSize(LI->getType()) < MaxAggrCopySize)
          continue;

        if (StoreInst *SI = dyn_cast<StoreInst>(LI->user_back())) {
          if (SI->getOperand(0) != LI)
            continue;
          AggrLoads.push_back(LI);
        }
      } else if (MemIntrinsic *IntrCall = dyn_cast<MemIntrinsic>(&I)) {
        // Convert intrinsic calls with variable size or with constant size
        // larger than the MaxAggrCopySize threshold.
        if (ConstantInt *LenCI = dyn_cast<ConstantInt>(IntrCall->getLength())) {
          if (LenCI->getZExtValue() >= MaxAggrCopySize) {
            MemCalls.push_back(IntrCall);
          }
        } else {
          MemCalls.push_back(IntrCall);
        }
      }
    }
  }

  if (AggrLoads.size() == 0 && MemCalls.size() == 0) {
    return false;
  }

  //
  // Do the transformation of an aggr load/copy/set to a loop
  //
  for (LoadInst *LI : AggrLoads) {
    auto *SI = cast<StoreInst>(*LI->user_begin());
    Value *SrcAddr = LI->getOperand(0);
    Value *DstAddr = SI->getOperand(1);
    unsigned NumLoads = DL.getTypeStoreSize(LI->getType());
    ConstantInt *CopyLen =
        ConstantInt::get(Type::getInt32Ty(Context), NumLoads);

    createMemCpyLoopKnownSize(/* ConvertedInst */ SI,
                              /* SrcAddr */ SrcAddr, /* DstAddr */ DstAddr,
                              /* CopyLen */ CopyLen,
                              /* SrcAlign */ LI->getAlign(),
                              /* DestAlign */ SI->getAlign(),
                              /* SrcIsVolatile */ LI->isVolatile(),
                              /* DstIsVolatile */ SI->isVolatile(),
                              /* CanOverlap */ true, TTI);

    SI->eraseFromParent();
    LI->eraseFromParent();
  }

  // Transform mem* intrinsic calls.
  for (MemIntrinsic *MemCall : MemCalls) {
    if (MemCpyInst *Memcpy = dyn_cast<MemCpyInst>(MemCall)) {
      expandMemCpyAsLoop(Memcpy, TTI);
    } else if (MemMoveInst *Memmove = dyn_cast<MemMoveInst>(MemCall)) {
      expandMemMoveAsLoop(Memmove, TTI);
    } else if (MemSetInst *Memset = dyn_cast<MemSetInst>(MemCall)) {
      expandMemSetAsLoop(Memset);
    }
    MemCall->eraseFromParent();
  }

  return true;
}

} // namespace

namespace llvm {
void initializeNVPTXLowerAggrCopiesPass(PassRegistry &);
}

INITIALIZE_PASS(NVPTXLowerAggrCopies, "nvptx-lower-aggr-copies",
                "Lower aggregate copies, and llvm.mem* intrinsics into loops",
                false, false)

FunctionPass *llvm::createLowerAggrCopies() {
  return new NVPTXLowerAggrCopies();
}
