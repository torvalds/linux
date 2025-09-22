//===- ExpandReductions.cpp - Expand reduction intrinsics -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements IR expansion for reduction intrinsics, allowing targets
// to enable the intrinsics until just before codegen.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ExpandReductions.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;

namespace {

bool expandReductions(Function &F, const TargetTransformInfo *TTI) {
  bool Changed = false;
  SmallVector<IntrinsicInst *, 4> Worklist;
  for (auto &I : instructions(F)) {
    if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
      switch (II->getIntrinsicID()) {
      default: break;
      case Intrinsic::vector_reduce_fadd:
      case Intrinsic::vector_reduce_fmul:
      case Intrinsic::vector_reduce_add:
      case Intrinsic::vector_reduce_mul:
      case Intrinsic::vector_reduce_and:
      case Intrinsic::vector_reduce_or:
      case Intrinsic::vector_reduce_xor:
      case Intrinsic::vector_reduce_smax:
      case Intrinsic::vector_reduce_smin:
      case Intrinsic::vector_reduce_umax:
      case Intrinsic::vector_reduce_umin:
      case Intrinsic::vector_reduce_fmax:
      case Intrinsic::vector_reduce_fmin:
        if (TTI->shouldExpandReduction(II))
          Worklist.push_back(II);

        break;
      }
    }
  }

  for (auto *II : Worklist) {
    FastMathFlags FMF =
        isa<FPMathOperator>(II) ? II->getFastMathFlags() : FastMathFlags{};
    Intrinsic::ID ID = II->getIntrinsicID();
    RecurKind RK = getMinMaxReductionRecurKind(ID);
    TargetTransformInfo::ReductionShuffle RS =
        TTI->getPreferredExpandedReductionShuffle(II);

    Value *Rdx = nullptr;
    IRBuilder<> Builder(II);
    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.setFastMathFlags(FMF);
    switch (ID) {
    default: llvm_unreachable("Unexpected intrinsic!");
    case Intrinsic::vector_reduce_fadd:
    case Intrinsic::vector_reduce_fmul: {
      // FMFs must be attached to the call, otherwise it's an ordered reduction
      // and it can't be handled by generating a shuffle sequence.
      Value *Acc = II->getArgOperand(0);
      Value *Vec = II->getArgOperand(1);
      unsigned RdxOpcode = getArithmeticReductionInstruction(ID);
      if (!FMF.allowReassoc())
        Rdx = getOrderedReduction(Builder, Acc, Vec, RdxOpcode, RK);
      else {
        if (!isPowerOf2_32(
                cast<FixedVectorType>(Vec->getType())->getNumElements()))
          continue;
        Rdx = getShuffleReduction(Builder, Vec, RdxOpcode, RS, RK);
        Rdx = Builder.CreateBinOp((Instruction::BinaryOps)RdxOpcode, Acc, Rdx,
                                  "bin.rdx");
      }
      break;
    }
    case Intrinsic::vector_reduce_and:
    case Intrinsic::vector_reduce_or: {
      // Canonicalize logical or/and reductions:
      // Or reduction for i1 is represented as:
      // %val = bitcast <ReduxWidth x i1> to iReduxWidth
      // %res = cmp ne iReduxWidth %val, 0
      // And reduction for i1 is represented as:
      // %val = bitcast <ReduxWidth x i1> to iReduxWidth
      // %res = cmp eq iReduxWidth %val, 11111
      Value *Vec = II->getArgOperand(0);
      auto *FTy = cast<FixedVectorType>(Vec->getType());
      unsigned NumElts = FTy->getNumElements();
      if (!isPowerOf2_32(NumElts))
        continue;

      if (FTy->getElementType() == Builder.getInt1Ty()) {
        Rdx = Builder.CreateBitCast(Vec, Builder.getIntNTy(NumElts));
        if (ID == Intrinsic::vector_reduce_and) {
          Rdx = Builder.CreateICmpEQ(
              Rdx, ConstantInt::getAllOnesValue(Rdx->getType()));
        } else {
          assert(ID == Intrinsic::vector_reduce_or && "Expected or reduction.");
          Rdx = Builder.CreateIsNotNull(Rdx);
        }
        break;
      }
      unsigned RdxOpcode = getArithmeticReductionInstruction(ID);
      Rdx = getShuffleReduction(Builder, Vec, RdxOpcode, RS, RK);
      break;
    }
    case Intrinsic::vector_reduce_add:
    case Intrinsic::vector_reduce_mul:
    case Intrinsic::vector_reduce_xor:
    case Intrinsic::vector_reduce_smax:
    case Intrinsic::vector_reduce_smin:
    case Intrinsic::vector_reduce_umax:
    case Intrinsic::vector_reduce_umin: {
      Value *Vec = II->getArgOperand(0);
      if (!isPowerOf2_32(
              cast<FixedVectorType>(Vec->getType())->getNumElements()))
        continue;
      unsigned RdxOpcode = getArithmeticReductionInstruction(ID);
      Rdx = getShuffleReduction(Builder, Vec, RdxOpcode, RS, RK);
      break;
    }
    case Intrinsic::vector_reduce_fmax:
    case Intrinsic::vector_reduce_fmin: {
      // We require "nnan" to use a shuffle reduction; "nsz" is implied by the
      // semantics of the reduction.
      Value *Vec = II->getArgOperand(0);
      if (!isPowerOf2_32(
              cast<FixedVectorType>(Vec->getType())->getNumElements()) ||
          !FMF.noNaNs())
        continue;
      unsigned RdxOpcode = getArithmeticReductionInstruction(ID);
      Rdx = getShuffleReduction(Builder, Vec, RdxOpcode, RS, RK);
      break;
    }
    }
    II->replaceAllUsesWith(Rdx);
    II->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

class ExpandReductions : public FunctionPass {
public:
  static char ID;
  ExpandReductions() : FunctionPass(ID) {
    initializeExpandReductionsPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    const auto *TTI =&getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    return expandReductions(F, TTI);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.setPreservesCFG();
  }
};
}

char ExpandReductions::ID;
INITIALIZE_PASS_BEGIN(ExpandReductions, "expand-reductions",
                      "Expand reduction intrinsics", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(ExpandReductions, "expand-reductions",
                    "Expand reduction intrinsics", false, false)

FunctionPass *llvm::createExpandReductionsPass() {
  return new ExpandReductions();
}

PreservedAnalyses ExpandReductionsPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  const auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  if (!expandReductions(F, &TTI))
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
