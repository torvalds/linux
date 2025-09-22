//===-- ObjCARC.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements common infrastructure for libLLVMObjCARCOpts.a, which
// implements several scalar transformations over the LLVM intermediate
// representation, including the C bindings for that library.
//
//===----------------------------------------------------------------------===//

#include "ObjCARC.h"
#include "llvm/Analysis/ObjCARCUtil.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;
using namespace llvm::objcarc;

CallInst *objcarc::createCallInstWithColors(
    FunctionCallee Func, ArrayRef<Value *> Args, const Twine &NameStr,
    BasicBlock::iterator InsertBefore,
    const DenseMap<BasicBlock *, ColorVector> &BlockColors) {
  FunctionType *FTy = Func.getFunctionType();
  Value *Callee = Func.getCallee();
  SmallVector<OperandBundleDef, 1> OpBundles;

  if (!BlockColors.empty()) {
    const ColorVector &CV = BlockColors.find(InsertBefore->getParent())->second;
    assert(CV.size() == 1 && "non-unique color for block!");
    Instruction *EHPad = CV.front()->getFirstNonPHI();
    if (EHPad->isEHPad())
      OpBundles.emplace_back("funclet", EHPad);
  }

  return CallInst::Create(FTy, Callee, Args, OpBundles, NameStr, InsertBefore);
}

std::pair<bool, bool>
BundledRetainClaimRVs::insertAfterInvokes(Function &F, DominatorTree *DT) {
  bool Changed = false, CFGChanged = false;

  for (BasicBlock &BB : F) {
    auto *I = dyn_cast<InvokeInst>(BB.getTerminator());

    if (!I)
      continue;

    if (!objcarc::hasAttachedCallOpBundle(I))
      continue;

    BasicBlock *DestBB = I->getNormalDest();

    if (!DestBB->getSinglePredecessor()) {
      assert(I->getSuccessor(0) == DestBB &&
             "the normal dest is expected to be the first successor");
      DestBB = SplitCriticalEdge(I, 0, CriticalEdgeSplittingOptions(DT));
      CFGChanged = true;
    }

    // We don't have to call insertRVCallWithColors since DestBB is the normal
    // destination of the invoke.
    insertRVCall(DestBB->getFirstInsertionPt(), I);
    Changed = true;
  }

  return std::make_pair(Changed, CFGChanged);
}

CallInst *BundledRetainClaimRVs::insertRVCall(BasicBlock::iterator InsertPt,
                                              CallBase *AnnotatedCall) {
  DenseMap<BasicBlock *, ColorVector> BlockColors;
  return insertRVCallWithColors(InsertPt, AnnotatedCall, BlockColors);
}

CallInst *BundledRetainClaimRVs::insertRVCallWithColors(
    BasicBlock::iterator InsertPt, CallBase *AnnotatedCall,
    const DenseMap<BasicBlock *, ColorVector> &BlockColors) {
  IRBuilder<> Builder(InsertPt->getParent(), InsertPt);
  Function *Func = *objcarc::getAttachedARCFunction(AnnotatedCall);
  assert(Func && "operand isn't a Function");
  Type *ParamTy = Func->getArg(0)->getType();
  Value *CallArg = Builder.CreateBitCast(AnnotatedCall, ParamTy);
  auto *Call =
      createCallInstWithColors(Func, CallArg, "", InsertPt, BlockColors);
  RVCalls[Call] = AnnotatedCall;
  return Call;
}

BundledRetainClaimRVs::~BundledRetainClaimRVs() {
  for (auto P : RVCalls) {
    if (ContractPass) {
      CallBase *CB = P.second;
      // At this point, we know that the annotated calls can't be tail calls
      // as they are followed by marker instructions and retainRV/claimRV
      // calls. Mark them as notail so that the backend knows these calls
      // can't be tail calls.
      if (auto *CI = dyn_cast<CallInst>(CB))
        CI->setTailCallKind(CallInst::TCK_NoTail);
    }

    EraseInstruction(P.first);
  }

  RVCalls.clear();
}
