//===- LowerExpectIntrinsic.cpp - Lower expect intrinsic ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers the 'expect' intrinsic to LLVM metadata.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LowerExpectIntrinsic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/MisExpect.h"

#include <cmath>

using namespace llvm;

#define DEBUG_TYPE "lower-expect-intrinsic"

STATISTIC(ExpectIntrinsicsHandled,
          "Number of 'expect' intrinsic instructions handled");

// These default values are chosen to represent an extremely skewed outcome for
// a condition, but they leave some room for interpretation by later passes.
//
// If the documentation for __builtin_expect() was made explicit that it should
// only be used in extreme cases, we could make this ratio higher. As it stands,
// programmers may be using __builtin_expect() / llvm.expect to annotate that a
// branch is likely or unlikely to be taken.

// WARNING: these values are internal implementation detail of the pass.
// They should not be exposed to the outside of the pass, front-end codegen
// should emit @llvm.expect intrinsics instead of using these weights directly.
// Transforms should use TargetTransformInfo's getPredictableBranchThreshold().
static cl::opt<uint32_t> LikelyBranchWeight(
    "likely-branch-weight", cl::Hidden, cl::init(2000),
    cl::desc("Weight of the branch likely to be taken (default = 2000)"));
static cl::opt<uint32_t> UnlikelyBranchWeight(
    "unlikely-branch-weight", cl::Hidden, cl::init(1),
    cl::desc("Weight of the branch unlikely to be taken (default = 1)"));

static std::tuple<uint32_t, uint32_t>
getBranchWeight(Intrinsic::ID IntrinsicID, CallInst *CI, int BranchCount) {
  if (IntrinsicID == Intrinsic::expect) {
    // __builtin_expect
    return std::make_tuple(LikelyBranchWeight.getValue(),
                           UnlikelyBranchWeight.getValue());
  } else {
    // __builtin_expect_with_probability
    assert(CI->getNumOperands() >= 3 &&
           "expect with probability must have 3 arguments");
    auto *Confidence = cast<ConstantFP>(CI->getArgOperand(2));
    double TrueProb = Confidence->getValueAPF().convertToDouble();
    assert((TrueProb >= 0.0 && TrueProb <= 1.0) &&
           "probability value must be in the range [0.0, 1.0]");
    double FalseProb = (1.0 - TrueProb) / (BranchCount - 1);
    uint32_t LikelyBW = ceil((TrueProb * (double)(INT32_MAX - 1)) + 1.0);
    uint32_t UnlikelyBW = ceil((FalseProb * (double)(INT32_MAX - 1)) + 1.0);
    return std::make_tuple(LikelyBW, UnlikelyBW);
  }
}

static bool handleSwitchExpect(SwitchInst &SI) {
  CallInst *CI = dyn_cast<CallInst>(SI.getCondition());
  if (!CI)
    return false;

  Function *Fn = CI->getCalledFunction();
  if (!Fn || (Fn->getIntrinsicID() != Intrinsic::expect &&
              Fn->getIntrinsicID() != Intrinsic::expect_with_probability))
    return false;

  Value *ArgValue = CI->getArgOperand(0);
  ConstantInt *ExpectedValue = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  if (!ExpectedValue)
    return false;

  SwitchInst::CaseHandle Case = *SI.findCaseValue(ExpectedValue);
  unsigned n = SI.getNumCases(); // +1 for default case.
  uint32_t LikelyBranchWeightVal, UnlikelyBranchWeightVal;
  std::tie(LikelyBranchWeightVal, UnlikelyBranchWeightVal) =
      getBranchWeight(Fn->getIntrinsicID(), CI, n + 1);

  SmallVector<uint32_t, 16> Weights(n + 1, UnlikelyBranchWeightVal);

  uint64_t Index = (Case == *SI.case_default()) ? 0 : Case.getCaseIndex() + 1;
  Weights[Index] = LikelyBranchWeightVal;

  misexpect::checkExpectAnnotations(SI, Weights, /*IsFrontend=*/true);

  SI.setCondition(ArgValue);
  setBranchWeights(SI, Weights, /*IsExpected=*/true);
  return true;
}

/// Handler for PHINodes that define the value argument to an
/// @llvm.expect call.
///
/// If the operand of the phi has a constant value and it 'contradicts'
/// with the expected value of phi def, then the corresponding incoming
/// edge of the phi is unlikely to be taken. Using that information,
/// the branch probability info for the originating branch can be inferred.
static void handlePhiDef(CallInst *Expect) {
  Value &Arg = *Expect->getArgOperand(0);
  ConstantInt *ExpectedValue = dyn_cast<ConstantInt>(Expect->getArgOperand(1));
  if (!ExpectedValue)
    return;
  const APInt &ExpectedPhiValue = ExpectedValue->getValue();
  bool ExpectedValueIsLikely = true;
  Function *Fn = Expect->getCalledFunction();
  // If the function is expect_with_probability, then we need to take the
  // probability into consideration. For example, in
  // expect.with.probability.i64(i64 %a, i64 1, double 0.0), the
  // "ExpectedValue" 1 is unlikely. This affects probability propagation later.
  if (Fn->getIntrinsicID() == Intrinsic::expect_with_probability) {
    auto *Confidence = cast<ConstantFP>(Expect->getArgOperand(2));
    double TrueProb = Confidence->getValueAPF().convertToDouble();
    ExpectedValueIsLikely = (TrueProb > 0.5);
  }

  // Walk up in backward a list of instructions that
  // have 'copy' semantics by 'stripping' the copies
  // until a PHI node or an instruction of unknown kind
  // is reached. Negation via xor is also handled.
  //
  //       C = PHI(...);
  //       B = C;
  //       A = B;
  //       D = __builtin_expect(A, 0);
  //
  Value *V = &Arg;
  SmallVector<Instruction *, 4> Operations;
  while (!isa<PHINode>(V)) {
    if (ZExtInst *ZExt = dyn_cast<ZExtInst>(V)) {
      V = ZExt->getOperand(0);
      Operations.push_back(ZExt);
      continue;
    }

    if (SExtInst *SExt = dyn_cast<SExtInst>(V)) {
      V = SExt->getOperand(0);
      Operations.push_back(SExt);
      continue;
    }

    BinaryOperator *BinOp = dyn_cast<BinaryOperator>(V);
    if (!BinOp || BinOp->getOpcode() != Instruction::Xor)
      return;

    ConstantInt *CInt = dyn_cast<ConstantInt>(BinOp->getOperand(1));
    if (!CInt)
      return;

    V = BinOp->getOperand(0);
    Operations.push_back(BinOp);
  }

  // Executes the recorded operations on input 'Value'.
  auto ApplyOperations = [&](const APInt &Value) {
    APInt Result = Value;
    for (auto *Op : llvm::reverse(Operations)) {
      switch (Op->getOpcode()) {
      case Instruction::Xor:
        Result ^= cast<ConstantInt>(Op->getOperand(1))->getValue();
        break;
      case Instruction::ZExt:
        Result = Result.zext(Op->getType()->getIntegerBitWidth());
        break;
      case Instruction::SExt:
        Result = Result.sext(Op->getType()->getIntegerBitWidth());
        break;
      default:
        llvm_unreachable("Unexpected operation");
      }
    }
    return Result;
  };

  auto *PhiDef = cast<PHINode>(V);

  // Get the first dominating conditional branch of the operand
  // i's incoming block.
  auto GetDomConditional = [&](unsigned i) -> BranchInst * {
    BasicBlock *BB = PhiDef->getIncomingBlock(i);
    BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
    if (BI && BI->isConditional())
      return BI;
    BB = BB->getSinglePredecessor();
    if (!BB)
      return nullptr;
    BI = dyn_cast<BranchInst>(BB->getTerminator());
    if (!BI || BI->isUnconditional())
      return nullptr;
    return BI;
  };

  // Now walk through all Phi operands to find phi oprerands with values
  // conflicting with the expected phi output value. Any such operand
  // indicates the incoming edge to that operand is unlikely.
  for (unsigned i = 0, e = PhiDef->getNumIncomingValues(); i != e; ++i) {

    Value *PhiOpnd = PhiDef->getIncomingValue(i);
    ConstantInt *CI = dyn_cast<ConstantInt>(PhiOpnd);
    if (!CI)
      continue;

    // Not an interesting case when IsUnlikely is false -- we can not infer
    // anything useful when:
    // (1) We expect some phi output and the operand value matches it, or
    // (2) We don't expect some phi output (i.e. the "ExpectedValue" has low
    //     probability) and the operand value doesn't match that.
    const APInt &CurrentPhiValue = ApplyOperations(CI->getValue());
    if (ExpectedValueIsLikely == (ExpectedPhiValue == CurrentPhiValue))
      continue;

    BranchInst *BI = GetDomConditional(i);
    if (!BI)
      continue;

    MDBuilder MDB(PhiDef->getContext());

    // There are two situations in which an operand of the PhiDef comes
    // from a given successor of a branch instruction BI.
    // 1) When the incoming block of the operand is the successor block;
    // 2) When the incoming block is BI's enclosing block and the
    // successor is the PhiDef's enclosing block.
    //
    // Returns true if the operand which comes from OpndIncomingBB
    // comes from outgoing edge of BI that leads to Succ block.
    auto *OpndIncomingBB = PhiDef->getIncomingBlock(i);
    auto IsOpndComingFromSuccessor = [&](BasicBlock *Succ) {
      if (OpndIncomingBB == Succ)
        // If this successor is the incoming block for this
        // Phi operand, then this successor does lead to the Phi.
        return true;
      if (OpndIncomingBB == BI->getParent() && Succ == PhiDef->getParent())
        // Otherwise, if the edge is directly from the branch
        // to the Phi, this successor is the one feeding this
        // Phi operand.
        return true;
      return false;
    };
    uint32_t LikelyBranchWeightVal, UnlikelyBranchWeightVal;
    std::tie(LikelyBranchWeightVal, UnlikelyBranchWeightVal) = getBranchWeight(
        Expect->getCalledFunction()->getIntrinsicID(), Expect, 2);
    if (!ExpectedValueIsLikely)
      std::swap(LikelyBranchWeightVal, UnlikelyBranchWeightVal);

    if (IsOpndComingFromSuccessor(BI->getSuccessor(1)))
      BI->setMetadata(LLVMContext::MD_prof,
                      MDB.createBranchWeights(LikelyBranchWeightVal,
                                              UnlikelyBranchWeightVal,
                                              /*IsExpected=*/true));
    else if (IsOpndComingFromSuccessor(BI->getSuccessor(0)))
      BI->setMetadata(LLVMContext::MD_prof,
                      MDB.createBranchWeights(UnlikelyBranchWeightVal,
                                              LikelyBranchWeightVal,
                                              /*IsExpected=*/true));
  }
}

// Handle both BranchInst and SelectInst.
template <class BrSelInst> static bool handleBrSelExpect(BrSelInst &BSI) {

  // Handle non-optimized IR code like:
  //   %expval = call i64 @llvm.expect.i64(i64 %conv1, i64 1)
  //   %tobool = icmp ne i64 %expval, 0
  //   br i1 %tobool, label %if.then, label %if.end
  //
  // Or the following simpler case:
  //   %expval = call i1 @llvm.expect.i1(i1 %cmp, i1 1)
  //   br i1 %expval, label %if.then, label %if.end

  CallInst *CI;

  ICmpInst *CmpI = dyn_cast<ICmpInst>(BSI.getCondition());
  CmpInst::Predicate Predicate;
  ConstantInt *CmpConstOperand = nullptr;
  if (!CmpI) {
    CI = dyn_cast<CallInst>(BSI.getCondition());
    Predicate = CmpInst::ICMP_NE;
  } else {
    Predicate = CmpI->getPredicate();
    if (Predicate != CmpInst::ICMP_NE && Predicate != CmpInst::ICMP_EQ)
      return false;

    CmpConstOperand = dyn_cast<ConstantInt>(CmpI->getOperand(1));
    if (!CmpConstOperand)
      return false;
    CI = dyn_cast<CallInst>(CmpI->getOperand(0));
  }

  if (!CI)
    return false;

  uint64_t ValueComparedTo = 0;
  if (CmpConstOperand) {
    if (CmpConstOperand->getBitWidth() > 64)
      return false;
    ValueComparedTo = CmpConstOperand->getZExtValue();
  }

  Function *Fn = CI->getCalledFunction();
  if (!Fn || (Fn->getIntrinsicID() != Intrinsic::expect &&
              Fn->getIntrinsicID() != Intrinsic::expect_with_probability))
    return false;

  Value *ArgValue = CI->getArgOperand(0);
  ConstantInt *ExpectedValue = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  if (!ExpectedValue)
    return false;

  MDBuilder MDB(CI->getContext());
  MDNode *Node;

  uint32_t LikelyBranchWeightVal, UnlikelyBranchWeightVal;
  std::tie(LikelyBranchWeightVal, UnlikelyBranchWeightVal) =
      getBranchWeight(Fn->getIntrinsicID(), CI, 2);

  SmallVector<uint32_t, 4> ExpectedWeights;
  if ((ExpectedValue->getZExtValue() == ValueComparedTo) ==
      (Predicate == CmpInst::ICMP_EQ)) {
    Node = MDB.createBranchWeights(
        LikelyBranchWeightVal, UnlikelyBranchWeightVal, /*IsExpected=*/true);
    ExpectedWeights = {LikelyBranchWeightVal, UnlikelyBranchWeightVal};
  } else {
    Node = MDB.createBranchWeights(UnlikelyBranchWeightVal,
                                   LikelyBranchWeightVal, /*IsExpected=*/true);
    ExpectedWeights = {UnlikelyBranchWeightVal, LikelyBranchWeightVal};
  }

  if (CmpI)
    CmpI->setOperand(0, ArgValue);
  else
    BSI.setCondition(ArgValue);

  misexpect::checkFrontendInstrumentation(BSI, ExpectedWeights);

  BSI.setMetadata(LLVMContext::MD_prof, Node);

  return true;
}

static bool handleBranchExpect(BranchInst &BI) {
  if (BI.isUnconditional())
    return false;

  return handleBrSelExpect<BranchInst>(BI);
}

static bool lowerExpectIntrinsic(Function &F) {
  bool Changed = false;

  for (BasicBlock &BB : F) {
    // Create "block_weights" metadata.
    if (BranchInst *BI = dyn_cast<BranchInst>(BB.getTerminator())) {
      if (handleBranchExpect(*BI))
        ExpectIntrinsicsHandled++;
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(BB.getTerminator())) {
      if (handleSwitchExpect(*SI))
        ExpectIntrinsicsHandled++;
    }

    // Remove llvm.expect intrinsics. Iterate backwards in order
    // to process select instructions before the intrinsic gets
    // removed.
    for (Instruction &Inst : llvm::make_early_inc_range(llvm::reverse(BB))) {
      CallInst *CI = dyn_cast<CallInst>(&Inst);
      if (!CI) {
        if (SelectInst *SI = dyn_cast<SelectInst>(&Inst)) {
          if (handleBrSelExpect(*SI))
            ExpectIntrinsicsHandled++;
        }
        continue;
      }

      Function *Fn = CI->getCalledFunction();
      if (Fn && (Fn->getIntrinsicID() == Intrinsic::expect ||
                 Fn->getIntrinsicID() == Intrinsic::expect_with_probability)) {
        // Before erasing the llvm.expect, walk backward to find
        // phi that define llvm.expect's first arg, and
        // infer branch probability:
        handlePhiDef(CI);
        Value *Exp = CI->getArgOperand(0);
        CI->replaceAllUsesWith(Exp);
        CI->eraseFromParent();
        Changed = true;
      }
    }
  }

  return Changed;
}

PreservedAnalyses LowerExpectIntrinsicPass::run(Function &F,
                                                FunctionAnalysisManager &) {
  if (lowerExpectIntrinsic(F))
    return PreservedAnalyses::none();

  return PreservedAnalyses::all();
}
