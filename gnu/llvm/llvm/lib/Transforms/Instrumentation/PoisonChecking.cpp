//===- PoisonChecking.cpp - -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements a transform pass which instruments IR such that poison semantics
// are made explicit.  That is, it provides a (possibly partial) executable
// semantics for every instruction w.r.t. poison as specified in the LLVM
// LangRef.  There are obvious parallels to the sanitizer tools, but this pass
// is focused purely on the semantics of LLVM IR, not any particular source
// language.   If you're looking for something to see if your C/C++ contains
// UB, this is not it.
//
// The rewritten semantics of each instruction will include the following
// components:
//
// 1) The original instruction, unmodified.
// 2) A propagation rule which translates dynamic information about the poison
//    state of each input to whether the dynamic output of the instruction
//    produces poison.
// 3) A creation rule which validates any poison producing flags on the
//    instruction itself (e.g. checks for overflow on nsw).
// 4) A check rule which traps (to a handler function) if this instruction must
//    execute undefined behavior given the poison state of it's inputs.
//
// This is a must analysis based transform; that is, the resulting code may
// produce a false negative result (not report UB when actually exists
// according to the LangRef spec), but should never produce a false positive
// (report UB where it doesn't exist).
//
// Use cases for this pass include:
// - Understanding (and testing!) the implications of the definition of poison
//   from the LangRef.
// - Validating the output of a IR fuzzer to ensure that all programs produced
//   are well defined on the specific input used.
// - Finding/confirming poison specific miscompiles by checking the poison
//   status of an input/IR pair is the same before and after an optimization
//   transform.
// - Checking that a bugpoint reduction does not introduce UB which didn't
//   exist in the original program being reduced.
//
// The major sources of inaccuracy are currently:
// - Most validation rules not yet implemented for instructions with poison
//   relavant flags.  At the moment, only nsw/nuw on add/sub are supported.
// - UB which is control dependent on a branch on poison is not yet
//   reported. Currently, only data flow dependence is modeled.
// - Poison which is propagated through memory is not modeled.  As such,
//   storing poison to memory and then reloading it will cause a false negative
//   as we consider the reloaded value to not be poisoned.
// - Poison propagation across function boundaries is not modeled.  At the
//   moment, all arguments and return values are assumed not to be poison.
// - Undef is not modeled.  In particular, the optimizer's freedom to pick
//   concrete values for undef bits so as to maximize potential for producing
//   poison is not modeled.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/PoisonChecking.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "poison-checking"

static cl::opt<bool>
LocalCheck("poison-checking-function-local",
           cl::init(false),
           cl::desc("Check that returns are non-poison (for testing)"));


static bool isConstantFalse(Value* V) {
  assert(V->getType()->isIntegerTy(1));
  if (auto *CI = dyn_cast<ConstantInt>(V))
    return CI->isZero();
  return false;
}

static Value *buildOrChain(IRBuilder<> &B, ArrayRef<Value*> Ops) {
  if (Ops.size() == 0)
    return B.getFalse();
  unsigned i = 0;
  for (; i < Ops.size() && isConstantFalse(Ops[i]); i++) {}
  if (i == Ops.size())
    return B.getFalse();
  Value *Accum = Ops[i++];
  for (Value *Op : llvm::drop_begin(Ops, i))
    if (!isConstantFalse(Op))
      Accum = B.CreateOr(Accum, Op);
  return Accum;
}

static void generateCreationChecksForBinOp(Instruction &I,
                                           SmallVectorImpl<Value*> &Checks) {
  assert(isa<BinaryOperator>(I));

  IRBuilder<> B(&I);
  Value *LHS = I.getOperand(0);
  Value *RHS = I.getOperand(1);
  switch (I.getOpcode()) {
  default:
    return;
  case Instruction::Add: {
    if (I.hasNoSignedWrap()) {
      auto *OverflowOp =
        B.CreateBinaryIntrinsic(Intrinsic::sadd_with_overflow, LHS, RHS);
      Checks.push_back(B.CreateExtractValue(OverflowOp, 1));
    }
    if (I.hasNoUnsignedWrap()) {
      auto *OverflowOp =
        B.CreateBinaryIntrinsic(Intrinsic::uadd_with_overflow, LHS, RHS);
      Checks.push_back(B.CreateExtractValue(OverflowOp, 1));
    }
    break;
  }
  case Instruction::Sub: {
    if (I.hasNoSignedWrap()) {
      auto *OverflowOp =
        B.CreateBinaryIntrinsic(Intrinsic::ssub_with_overflow, LHS, RHS);
      Checks.push_back(B.CreateExtractValue(OverflowOp, 1));
    }
    if (I.hasNoUnsignedWrap()) {
      auto *OverflowOp =
        B.CreateBinaryIntrinsic(Intrinsic::usub_with_overflow, LHS, RHS);
      Checks.push_back(B.CreateExtractValue(OverflowOp, 1));
    }
    break;
  }
  case Instruction::Mul: {
    if (I.hasNoSignedWrap()) {
      auto *OverflowOp =
        B.CreateBinaryIntrinsic(Intrinsic::smul_with_overflow, LHS, RHS);
      Checks.push_back(B.CreateExtractValue(OverflowOp, 1));
    }
    if (I.hasNoUnsignedWrap()) {
      auto *OverflowOp =
        B.CreateBinaryIntrinsic(Intrinsic::umul_with_overflow, LHS, RHS);
      Checks.push_back(B.CreateExtractValue(OverflowOp, 1));
    }
    break;
  }
  case Instruction::UDiv: {
    if (I.isExact()) {
      auto *Check =
        B.CreateICmp(ICmpInst::ICMP_NE, B.CreateURem(LHS, RHS),
                     ConstantInt::get(LHS->getType(), 0));
      Checks.push_back(Check);
    }
    break;
  }
  case Instruction::SDiv: {
    if (I.isExact()) {
      auto *Check =
        B.CreateICmp(ICmpInst::ICMP_NE, B.CreateSRem(LHS, RHS),
                     ConstantInt::get(LHS->getType(), 0));
      Checks.push_back(Check);
    }
    break;
  }
  case Instruction::AShr:
  case Instruction::LShr:
  case Instruction::Shl: {
    Value *ShiftCheck =
      B.CreateICmp(ICmpInst::ICMP_UGE, RHS,
                   ConstantInt::get(RHS->getType(),
                                    LHS->getType()->getScalarSizeInBits()));
    Checks.push_back(ShiftCheck);
    break;
  }
  };
}

/// Given an instruction which can produce poison on non-poison inputs
/// (i.e. canCreatePoison returns true), generate runtime checks to produce
/// boolean indicators of when poison would result.
static void generateCreationChecks(Instruction &I,
                                   SmallVectorImpl<Value*> &Checks) {
  IRBuilder<> B(&I);
  if (isa<BinaryOperator>(I) && !I.getType()->isVectorTy())
    generateCreationChecksForBinOp(I, Checks);

  // Handle non-binops separately
  switch (I.getOpcode()) {
  default:
    // Note there are a couple of missing cases here, once implemented, this
    // should become an llvm_unreachable.
    break;
  case Instruction::ExtractElement: {
    Value *Vec = I.getOperand(0);
    auto *VecVTy = dyn_cast<FixedVectorType>(Vec->getType());
    if (!VecVTy)
      break;
    Value *Idx = I.getOperand(1);
    unsigned NumElts = VecVTy->getNumElements();
    Value *Check =
      B.CreateICmp(ICmpInst::ICMP_UGE, Idx,
                   ConstantInt::get(Idx->getType(), NumElts));
    Checks.push_back(Check);
    break;
  }
  case Instruction::InsertElement: {
    Value *Vec = I.getOperand(0);
    auto *VecVTy = dyn_cast<FixedVectorType>(Vec->getType());
    if (!VecVTy)
      break;
    Value *Idx = I.getOperand(2);
    unsigned NumElts = VecVTy->getNumElements();
    Value *Check =
      B.CreateICmp(ICmpInst::ICMP_UGE, Idx,
                   ConstantInt::get(Idx->getType(), NumElts));
    Checks.push_back(Check);
    break;
  }
  };
}

static Value *getPoisonFor(DenseMap<Value *, Value *> &ValToPoison, Value *V) {
  auto Itr = ValToPoison.find(V);
  if (Itr != ValToPoison.end())
    return Itr->second;
  if (isa<Constant>(V)) {
    return ConstantInt::getFalse(V->getContext());
  }
  // Return false for unknwon values - this implements a non-strict mode where
  // unhandled IR constructs are simply considered to never produce poison.  At
  // some point in the future, we probably want a "strict mode" for testing if
  // nothing else.
  return ConstantInt::getFalse(V->getContext());
}

static void CreateAssert(IRBuilder<> &B, Value *Cond) {
  assert(Cond->getType()->isIntegerTy(1));
  if (auto *CI = dyn_cast<ConstantInt>(Cond))
    if (CI->isAllOnesValue())
      return;

  Module *M = B.GetInsertBlock()->getModule();
  M->getOrInsertFunction("__poison_checker_assert",
                         Type::getVoidTy(M->getContext()),
                         Type::getInt1Ty(M->getContext()));
  Function *TrapFunc = M->getFunction("__poison_checker_assert");
  B.CreateCall(TrapFunc, Cond);
}

static void CreateAssertNot(IRBuilder<> &B, Value *Cond) {
  assert(Cond->getType()->isIntegerTy(1));
  CreateAssert(B, B.CreateNot(Cond));
}

static bool rewrite(Function &F) {
  auto * const Int1Ty = Type::getInt1Ty(F.getContext());

  DenseMap<Value *, Value *> ValToPoison;

  for (BasicBlock &BB : F)
    for (auto I = BB.begin(); isa<PHINode>(&*I); I++) {
      auto *OldPHI = cast<PHINode>(&*I);
      auto *NewPHI = PHINode::Create(Int1Ty, OldPHI->getNumIncomingValues());
      for (unsigned i = 0; i < OldPHI->getNumIncomingValues(); i++)
        NewPHI->addIncoming(UndefValue::get(Int1Ty),
                            OldPHI->getIncomingBlock(i));
      NewPHI->insertBefore(OldPHI);
      ValToPoison[OldPHI] = NewPHI;
    }

  for (BasicBlock &BB : F)
    for (Instruction &I : BB) {
      if (isa<PHINode>(I)) continue;

      IRBuilder<> B(cast<Instruction>(&I));

      // Note: There are many more sources of documented UB, but this pass only
      // attempts to find UB triggered by propagation of poison.
      SmallVector<const Value *, 4> NonPoisonOps;
      SmallPtrSet<const Value *, 4> SeenNonPoisonOps;
      getGuaranteedNonPoisonOps(&I, NonPoisonOps);
      for (const Value *Op : NonPoisonOps)
        if (SeenNonPoisonOps.insert(Op).second)
          CreateAssertNot(B,
                          getPoisonFor(ValToPoison, const_cast<Value *>(Op)));

      if (LocalCheck)
        if (auto *RI = dyn_cast<ReturnInst>(&I))
          if (RI->getNumOperands() != 0) {
            Value *Op = RI->getOperand(0);
            CreateAssertNot(B, getPoisonFor(ValToPoison, Op));
          }

      SmallVector<Value*, 4> Checks;
      for (const Use &U : I.operands()) {
        if (ValToPoison.count(U) && propagatesPoison(U))
          Checks.push_back(getPoisonFor(ValToPoison, U));
      }

      if (canCreatePoison(cast<Operator>(&I)))
        generateCreationChecks(I, Checks);
      ValToPoison[&I] = buildOrChain(B, Checks);
    }

  for (BasicBlock &BB : F)
    for (auto I = BB.begin(); isa<PHINode>(&*I); I++) {
      auto *OldPHI = cast<PHINode>(&*I);
      if (!ValToPoison.count(OldPHI))
        continue; // skip the newly inserted phis
      auto *NewPHI = cast<PHINode>(ValToPoison[OldPHI]);
      for (unsigned i = 0; i < OldPHI->getNumIncomingValues(); i++) {
        auto *OldVal = OldPHI->getIncomingValue(i);
        NewPHI->setIncomingValue(i, getPoisonFor(ValToPoison, OldVal));
      }
    }
  return true;
}


PreservedAnalyses PoisonCheckingPass::run(Module &M,
                                          ModuleAnalysisManager &AM) {
  bool Changed = false;
  for (auto &F : M)
    Changed |= rewrite(F);

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

PreservedAnalyses PoisonCheckingPass::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  return rewrite(F) ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

/* Major TODO Items:
   - Control dependent poison UB
   - Strict mode - (i.e. must analyze every operand)
     - Poison through memory
     - Function ABIs
     - Full coverage of intrinsics, etc.. (ouch)

   Instructions w/Unclear Semantics:
   - shufflevector - It would seem reasonable for an out of bounds mask element
     to produce poison, but the LangRef does not state.
   - all binary ops w/vector operands - The likely interpretation would be that
     any element overflowing should produce poison for the entire result, but
     the LangRef does not state.
   - Floating point binary ops w/fmf flags other than (nnan, noinfs).  It seems
     strange that only certian flags should be documented as producing poison.

   Cases of clear poison semantics not yet implemented:
   - Exact flags on ashr/lshr produce poison
   - NSW/NUW flags on shl produce poison
   - Inbounds flag on getelementptr produce poison
   - fptosi/fptoui (out of bounds input) produce poison
   - Scalable vector types for insertelement/extractelement
   - Floating point binary ops w/fmf nnan/noinfs flags produce poison
 */
