//===- NaryReassociate.cpp - Reassociate n-ary expressions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass reassociates n-ary add expressions and eliminates the redundancy
// exposed by the reassociation.
//
// A motivating example:
//
//   void foo(int a, int b) {
//     bar(a + b);
//     bar((a + 2) + b);
//   }
//
// An ideal compiler should reassociate (a + 2) + b to (a + b) + 2 and simplify
// the above code to
//
//   int t = a + b;
//   bar(t);
//   bar(t + 2);
//
// However, the Reassociate pass is unable to do that because it processes each
// instruction individually and believes (a + 2) + b is the best form according
// to its rank system.
//
// To address this limitation, NaryReassociate reassociates an expression in a
// form that reuses existing instructions. As a result, NaryReassociate can
// reassociate (a + 2) + b in the example to (a + b) + 2 because it detects that
// (a + b) is computed before.
//
// NaryReassociate works as follows. For every instruction in the form of (a +
// b) + c, it checks whether a + c or b + c is already computed by a dominating
// instruction. If so, it then reassociates (a + b) + c into (a + c) + b or (b +
// c) + a and removes the redundancy accordingly. To efficiently look up whether
// an expression is computed before, we store each instruction seen and its SCEV
// into an SCEV-to-instruction map.
//
// Although the algorithm pattern-matches only ternary additions, it
// automatically handles many >3-ary expressions by walking through the function
// in the depth-first order. For example, given
//
//   (a + c) + d
//   ((a + b) + c) + d
//
// NaryReassociate first rewrites (a + b) + c to (a + c) + b, and then rewrites
// ((a + c) + b) + d into ((a + c) + d) + b.
//
// Finally, the above dominator-based algorithm may need to be run multiple
// iterations before emitting optimal code. One source of this need is that we
// only split an operand when it is used only once. The above algorithm can
// eliminate an instruction and decrease the usage count of its operands. As a
// result, an instruction that previously had multiple uses may become a
// single-use instruction and thus eligible for split consideration. For
// example,
//
//   ac = a + c
//   ab = a + b
//   abc = ab + c
//   ab2 = ab + b
//   ab2c = ab2 + c
//
// In the first iteration, we cannot reassociate abc to ac+b because ab is used
// twice. However, we can reassociate ab2c to abc+b in the first iteration. As a
// result, ab2 becomes dead and ab will be used only once in the second
// iteration.
//
// Limitations and TODO items:
//
// 1) We only considers n-ary adds and muls for now. This should be extended
// and generalized.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/NaryReassociate.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <cassert>
#include <cstdint>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "nary-reassociate"

namespace {

class NaryReassociateLegacyPass : public FunctionPass {
public:
  static char ID;

  NaryReassociateLegacyPass() : FunctionPass(ID) {
    initializeNaryReassociateLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool doInitialization(Module &M) override {
    return false;
  }

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<ScalarEvolutionWrapperPass>();
    AU.addPreserved<TargetLibraryInfoWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.setPreservesCFG();
  }

private:
  NaryReassociatePass Impl;
};

} // end anonymous namespace

char NaryReassociateLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(NaryReassociateLegacyPass, "nary-reassociate",
                      "Nary reassociation", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(NaryReassociateLegacyPass, "nary-reassociate",
                    "Nary reassociation", false, false)

FunctionPass *llvm::createNaryReassociatePass() {
  return new NaryReassociateLegacyPass();
}

bool NaryReassociateLegacyPass::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto *AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  auto *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  auto *TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
  auto *TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);

  return Impl.runImpl(F, AC, DT, SE, TLI, TTI);
}

PreservedAnalyses NaryReassociatePass::run(Function &F,
                                           FunctionAnalysisManager &AM) {
  auto *AC = &AM.getResult<AssumptionAnalysis>(F);
  auto *DT = &AM.getResult<DominatorTreeAnalysis>(F);
  auto *SE = &AM.getResult<ScalarEvolutionAnalysis>(F);
  auto *TLI = &AM.getResult<TargetLibraryAnalysis>(F);
  auto *TTI = &AM.getResult<TargetIRAnalysis>(F);

  if (!runImpl(F, AC, DT, SE, TLI, TTI))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<ScalarEvolutionAnalysis>();
  return PA;
}

bool NaryReassociatePass::runImpl(Function &F, AssumptionCache *AC_,
                                  DominatorTree *DT_, ScalarEvolution *SE_,
                                  TargetLibraryInfo *TLI_,
                                  TargetTransformInfo *TTI_) {
  AC = AC_;
  DT = DT_;
  SE = SE_;
  TLI = TLI_;
  TTI = TTI_;
  DL = &F.getDataLayout();

  bool Changed = false, ChangedInThisIteration;
  do {
    ChangedInThisIteration = doOneIteration(F);
    Changed |= ChangedInThisIteration;
  } while (ChangedInThisIteration);
  return Changed;
}

bool NaryReassociatePass::doOneIteration(Function &F) {
  bool Changed = false;
  SeenExprs.clear();
  // Process the basic blocks in a depth first traversal of the dominator
  // tree. This order ensures that all bases of a candidate are in Candidates
  // when we process it.
  SmallVector<WeakTrackingVH, 16> DeadInsts;
  for (const auto Node : depth_first(DT)) {
    BasicBlock *BB = Node->getBlock();
    for (Instruction &OrigI : *BB) {
      const SCEV *OrigSCEV = nullptr;
      if (Instruction *NewI = tryReassociate(&OrigI, OrigSCEV)) {
        Changed = true;
        OrigI.replaceAllUsesWith(NewI);

        // Add 'OrigI' to the list of dead instructions.
        DeadInsts.push_back(WeakTrackingVH(&OrigI));
        // Add the rewritten instruction to SeenExprs; the original
        // instruction is deleted.
        const SCEV *NewSCEV = SE->getSCEV(NewI);
        SeenExprs[NewSCEV].push_back(WeakTrackingVH(NewI));

        // Ideally, NewSCEV should equal OldSCEV because tryReassociate(I)
        // is equivalent to I. However, ScalarEvolution::getSCEV may
        // weaken nsw causing NewSCEV not to equal OldSCEV. For example,
        // suppose we reassociate
        //   I = &a[sext(i +nsw j)] // assuming sizeof(a[0]) = 4
        // to
        //   NewI = &a[sext(i)] + sext(j).
        //
        // ScalarEvolution computes
        //   getSCEV(I)    = a + 4 * sext(i + j)
        //   getSCEV(newI) = a + 4 * sext(i) + 4 * sext(j)
        // which are different SCEVs.
        //
        // To alleviate this issue of ScalarEvolution not always capturing
        // equivalence, we add I to SeenExprs[OldSCEV] as well so that we can
        // map both SCEV before and after tryReassociate(I) to I.
        //
        // This improvement is exercised in @reassociate_gep_nsw in
        // nary-gep.ll.
        if (NewSCEV != OrigSCEV)
          SeenExprs[OrigSCEV].push_back(WeakTrackingVH(NewI));
      } else if (OrigSCEV)
        SeenExprs[OrigSCEV].push_back(WeakTrackingVH(&OrigI));
    }
  }
  // Delete all dead instructions from 'DeadInsts'.
  // Please note ScalarEvolution is updated along the way.
  RecursivelyDeleteTriviallyDeadInstructionsPermissive(
      DeadInsts, TLI, nullptr, [this](Value *V) { SE->forgetValue(V); });

  return Changed;
}

template <typename PredT>
Instruction *
NaryReassociatePass::matchAndReassociateMinOrMax(Instruction *I,
                                                 const SCEV *&OrigSCEV) {
  Value *LHS = nullptr;
  Value *RHS = nullptr;

  auto MinMaxMatcher =
      MaxMin_match<ICmpInst, bind_ty<Value>, bind_ty<Value>, PredT>(
          m_Value(LHS), m_Value(RHS));
  if (match(I, MinMaxMatcher)) {
    OrigSCEV = SE->getSCEV(I);
    if (auto *NewMinMax = dyn_cast_or_null<Instruction>(
            tryReassociateMinOrMax(I, MinMaxMatcher, LHS, RHS)))
      return NewMinMax;
    if (auto *NewMinMax = dyn_cast_or_null<Instruction>(
            tryReassociateMinOrMax(I, MinMaxMatcher, RHS, LHS)))
      return NewMinMax;
  }
  return nullptr;
}

Instruction *NaryReassociatePass::tryReassociate(Instruction * I,
                                                 const SCEV *&OrigSCEV) {

  if (!SE->isSCEVable(I->getType()))
    return nullptr;

  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Mul:
    OrigSCEV = SE->getSCEV(I);
    return tryReassociateBinaryOp(cast<BinaryOperator>(I));
  case Instruction::GetElementPtr:
    OrigSCEV = SE->getSCEV(I);
    return tryReassociateGEP(cast<GetElementPtrInst>(I));
  default:
    break;
  }

  // Try to match signed/unsigned Min/Max.
  Instruction *ResI = nullptr;
  // TODO: Currently min/max reassociation is restricted to integer types only
  // due to use of SCEVExpander which my introduce incompatible forms of min/max
  // for pointer types.
  if (I->getType()->isIntegerTy())
    if ((ResI = matchAndReassociateMinOrMax<umin_pred_ty>(I, OrigSCEV)) ||
        (ResI = matchAndReassociateMinOrMax<smin_pred_ty>(I, OrigSCEV)) ||
        (ResI = matchAndReassociateMinOrMax<umax_pred_ty>(I, OrigSCEV)) ||
        (ResI = matchAndReassociateMinOrMax<smax_pred_ty>(I, OrigSCEV)))
      return ResI;

  return nullptr;
}

static bool isGEPFoldable(GetElementPtrInst *GEP,
                          const TargetTransformInfo *TTI) {
  SmallVector<const Value *, 4> Indices(GEP->indices());
  return TTI->getGEPCost(GEP->getSourceElementType(), GEP->getPointerOperand(),
                         Indices) == TargetTransformInfo::TCC_Free;
}

Instruction *NaryReassociatePass::tryReassociateGEP(GetElementPtrInst *GEP) {
  // Not worth reassociating GEP if it is foldable.
  if (isGEPFoldable(GEP, TTI))
    return nullptr;

  gep_type_iterator GTI = gep_type_begin(*GEP);
  for (unsigned I = 1, E = GEP->getNumOperands(); I != E; ++I, ++GTI) {
    if (GTI.isSequential()) {
      if (auto *NewGEP = tryReassociateGEPAtIndex(GEP, I - 1,
                                                  GTI.getIndexedType())) {
        return NewGEP;
      }
    }
  }
  return nullptr;
}

bool NaryReassociatePass::requiresSignExtension(Value *Index,
                                                GetElementPtrInst *GEP) {
  unsigned IndexSizeInBits =
      DL->getIndexSizeInBits(GEP->getType()->getPointerAddressSpace());
  return cast<IntegerType>(Index->getType())->getBitWidth() < IndexSizeInBits;
}

GetElementPtrInst *
NaryReassociatePass::tryReassociateGEPAtIndex(GetElementPtrInst *GEP,
                                              unsigned I, Type *IndexedType) {
  SimplifyQuery SQ(*DL, DT, AC, GEP);
  Value *IndexToSplit = GEP->getOperand(I + 1);
  if (SExtInst *SExt = dyn_cast<SExtInst>(IndexToSplit)) {
    IndexToSplit = SExt->getOperand(0);
  } else if (ZExtInst *ZExt = dyn_cast<ZExtInst>(IndexToSplit)) {
    // zext can be treated as sext if the source is non-negative.
    if (isKnownNonNegative(ZExt->getOperand(0), SQ))
      IndexToSplit = ZExt->getOperand(0);
  }

  if (AddOperator *AO = dyn_cast<AddOperator>(IndexToSplit)) {
    // If the I-th index needs sext and the underlying add is not equipped with
    // nsw, we cannot split the add because
    //   sext(LHS + RHS) != sext(LHS) + sext(RHS).
    if (requiresSignExtension(IndexToSplit, GEP) &&
        computeOverflowForSignedAdd(AO, SQ) != OverflowResult::NeverOverflows)
      return nullptr;

    Value *LHS = AO->getOperand(0), *RHS = AO->getOperand(1);
    // IndexToSplit = LHS + RHS.
    if (auto *NewGEP = tryReassociateGEPAtIndex(GEP, I, LHS, RHS, IndexedType))
      return NewGEP;
    // Symmetrically, try IndexToSplit = RHS + LHS.
    if (LHS != RHS) {
      if (auto *NewGEP =
              tryReassociateGEPAtIndex(GEP, I, RHS, LHS, IndexedType))
        return NewGEP;
    }
  }
  return nullptr;
}

GetElementPtrInst *
NaryReassociatePass::tryReassociateGEPAtIndex(GetElementPtrInst *GEP,
                                              unsigned I, Value *LHS,
                                              Value *RHS, Type *IndexedType) {
  // Look for GEP's closest dominator that has the same SCEV as GEP except that
  // the I-th index is replaced with LHS.
  SmallVector<const SCEV *, 4> IndexExprs;
  for (Use &Index : GEP->indices())
    IndexExprs.push_back(SE->getSCEV(Index));
  // Replace the I-th index with LHS.
  IndexExprs[I] = SE->getSCEV(LHS);
  if (isKnownNonNegative(LHS, SimplifyQuery(*DL, DT, AC, GEP)) &&
      DL->getTypeSizeInBits(LHS->getType()).getFixedValue() <
          DL->getTypeSizeInBits(GEP->getOperand(I)->getType())
              .getFixedValue()) {
    // Zero-extend LHS if it is non-negative. InstCombine canonicalizes sext to
    // zext if the source operand is proved non-negative. We should do that
    // consistently so that CandidateExpr more likely appears before. See
    // @reassociate_gep_assume for an example of this canonicalization.
    IndexExprs[I] =
        SE->getZeroExtendExpr(IndexExprs[I], GEP->getOperand(I)->getType());
  }
  const SCEV *CandidateExpr = SE->getGEPExpr(cast<GEPOperator>(GEP),
                                             IndexExprs);

  Value *Candidate = findClosestMatchingDominator(CandidateExpr, GEP);
  if (Candidate == nullptr)
    return nullptr;

  IRBuilder<> Builder(GEP);
  // Candidate does not necessarily have the same pointer type as GEP. Use
  // bitcast or pointer cast to make sure they have the same type, so that the
  // later RAUW doesn't complain.
  Candidate = Builder.CreateBitOrPointerCast(Candidate, GEP->getType());
  assert(Candidate->getType() == GEP->getType());

  // NewGEP = (char *)Candidate + RHS * sizeof(IndexedType)
  uint64_t IndexedSize = DL->getTypeAllocSize(IndexedType);
  Type *ElementType = GEP->getResultElementType();
  uint64_t ElementSize = DL->getTypeAllocSize(ElementType);
  // Another less rare case: because I is not necessarily the last index of the
  // GEP, the size of the type at the I-th index (IndexedSize) is not
  // necessarily divisible by ElementSize. For example,
  //
  // #pragma pack(1)
  // struct S {
  //   int a[3];
  //   int64 b[8];
  // };
  // #pragma pack()
  //
  // sizeof(S) = 100 is indivisible by sizeof(int64) = 8.
  //
  // TODO: bail out on this case for now. We could emit uglygep.
  if (IndexedSize % ElementSize != 0)
    return nullptr;

  // NewGEP = &Candidate[RHS * (sizeof(IndexedType) / sizeof(Candidate[0])));
  Type *PtrIdxTy = DL->getIndexType(GEP->getType());
  if (RHS->getType() != PtrIdxTy)
    RHS = Builder.CreateSExtOrTrunc(RHS, PtrIdxTy);
  if (IndexedSize != ElementSize) {
    RHS = Builder.CreateMul(
        RHS, ConstantInt::get(PtrIdxTy, IndexedSize / ElementSize));
  }
  GetElementPtrInst *NewGEP = cast<GetElementPtrInst>(
      Builder.CreateGEP(GEP->getResultElementType(), Candidate, RHS));
  NewGEP->setIsInBounds(GEP->isInBounds());
  NewGEP->takeName(GEP);
  return NewGEP;
}

Instruction *NaryReassociatePass::tryReassociateBinaryOp(BinaryOperator *I) {
  Value *LHS = I->getOperand(0), *RHS = I->getOperand(1);
  // There is no need to reassociate 0.
  if (SE->getSCEV(I)->isZero())
    return nullptr;
  if (auto *NewI = tryReassociateBinaryOp(LHS, RHS, I))
    return NewI;
  if (auto *NewI = tryReassociateBinaryOp(RHS, LHS, I))
    return NewI;
  return nullptr;
}

Instruction *NaryReassociatePass::tryReassociateBinaryOp(Value *LHS, Value *RHS,
                                                         BinaryOperator *I) {
  Value *A = nullptr, *B = nullptr;
  // To be conservative, we reassociate I only when it is the only user of (A op
  // B).
  if (LHS->hasOneUse() && matchTernaryOp(I, LHS, A, B)) {
    // I = (A op B) op RHS
    //   = (A op RHS) op B or (B op RHS) op A
    const SCEV *AExpr = SE->getSCEV(A), *BExpr = SE->getSCEV(B);
    const SCEV *RHSExpr = SE->getSCEV(RHS);
    if (BExpr != RHSExpr) {
      if (auto *NewI =
              tryReassociatedBinaryOp(getBinarySCEV(I, AExpr, RHSExpr), B, I))
        return NewI;
    }
    if (AExpr != RHSExpr) {
      if (auto *NewI =
              tryReassociatedBinaryOp(getBinarySCEV(I, BExpr, RHSExpr), A, I))
        return NewI;
    }
  }
  return nullptr;
}

Instruction *NaryReassociatePass::tryReassociatedBinaryOp(const SCEV *LHSExpr,
                                                          Value *RHS,
                                                          BinaryOperator *I) {
  // Look for the closest dominator LHS of I that computes LHSExpr, and replace
  // I with LHS op RHS.
  auto *LHS = findClosestMatchingDominator(LHSExpr, I);
  if (LHS == nullptr)
    return nullptr;

  Instruction *NewI = nullptr;
  switch (I->getOpcode()) {
  case Instruction::Add:
    NewI = BinaryOperator::CreateAdd(LHS, RHS, "", I->getIterator());
    break;
  case Instruction::Mul:
    NewI = BinaryOperator::CreateMul(LHS, RHS, "", I->getIterator());
    break;
  default:
    llvm_unreachable("Unexpected instruction.");
  }
  NewI->setDebugLoc(I->getDebugLoc());
  NewI->takeName(I);
  return NewI;
}

bool NaryReassociatePass::matchTernaryOp(BinaryOperator *I, Value *V,
                                         Value *&Op1, Value *&Op2) {
  switch (I->getOpcode()) {
  case Instruction::Add:
    return match(V, m_Add(m_Value(Op1), m_Value(Op2)));
  case Instruction::Mul:
    return match(V, m_Mul(m_Value(Op1), m_Value(Op2)));
  default:
    llvm_unreachable("Unexpected instruction.");
  }
  return false;
}

const SCEV *NaryReassociatePass::getBinarySCEV(BinaryOperator *I,
                                               const SCEV *LHS,
                                               const SCEV *RHS) {
  switch (I->getOpcode()) {
  case Instruction::Add:
    return SE->getAddExpr(LHS, RHS);
  case Instruction::Mul:
    return SE->getMulExpr(LHS, RHS);
  default:
    llvm_unreachable("Unexpected instruction.");
  }
  return nullptr;
}

Instruction *
NaryReassociatePass::findClosestMatchingDominator(const SCEV *CandidateExpr,
                                                  Instruction *Dominatee) {
  auto Pos = SeenExprs.find(CandidateExpr);
  if (Pos == SeenExprs.end())
    return nullptr;

  auto &Candidates = Pos->second;
  // Because we process the basic blocks in pre-order of the dominator tree, a
  // candidate that doesn't dominate the current instruction won't dominate any
  // future instruction either. Therefore, we pop it out of the stack. This
  // optimization makes the algorithm O(n).
  while (!Candidates.empty()) {
    // Candidates stores WeakTrackingVHs, so a candidate can be nullptr if it's
    // removed during rewriting.
    if (Value *Candidate = Candidates.pop_back_val()) {
      Instruction *CandidateInstruction = cast<Instruction>(Candidate);
      if (!DT->dominates(CandidateInstruction, Dominatee))
        continue;

      // Make sure that the instruction is safe to reuse without introducing
      // poison.
      SmallVector<Instruction *> DropPoisonGeneratingInsts;
      if (!SE->canReuseInstruction(CandidateExpr, CandidateInstruction,
                                   DropPoisonGeneratingInsts))
        continue;

      for (Instruction *I : DropPoisonGeneratingInsts)
        I->dropPoisonGeneratingAnnotations();

      return CandidateInstruction;
    }
  }
  return nullptr;
}

template <typename MaxMinT> static SCEVTypes convertToSCEVype(MaxMinT &MM) {
  if (std::is_same_v<smax_pred_ty, typename MaxMinT::PredType>)
    return scSMaxExpr;
  else if (std::is_same_v<umax_pred_ty, typename MaxMinT::PredType>)
    return scUMaxExpr;
  else if (std::is_same_v<smin_pred_ty, typename MaxMinT::PredType>)
    return scSMinExpr;
  else if (std::is_same_v<umin_pred_ty, typename MaxMinT::PredType>)
    return scUMinExpr;

  llvm_unreachable("Can't convert MinMax pattern to SCEV type");
  return scUnknown;
}

// Parameters:
//  I - instruction matched by MaxMinMatch matcher
//  MaxMinMatch - min/max idiom matcher
//  LHS - first operand of I
//  RHS - second operand of I
template <typename MaxMinT>
Value *NaryReassociatePass::tryReassociateMinOrMax(Instruction *I,
                                                   MaxMinT MaxMinMatch,
                                                   Value *LHS, Value *RHS) {
  Value *A = nullptr, *B = nullptr;
  MaxMinT m_MaxMin(m_Value(A), m_Value(B));

  if (LHS->hasNUsesOrMore(3) ||
      // The optimization is profitable only if LHS can be removed in the end.
      // In other words LHS should be used (directly or indirectly) by I only.
      llvm::any_of(LHS->users(),
                    [&](auto *U) {
                      return U != I &&
                             !(U->hasOneUser() && *U->users().begin() == I);
                    }) ||
      !match(LHS, m_MaxMin))
    return nullptr;

  auto tryCombination = [&](Value *A, const SCEV *AExpr, Value *B,
                            const SCEV *BExpr, Value *C,
                            const SCEV *CExpr) -> Value * {
    SmallVector<const SCEV *, 2> Ops1{BExpr, AExpr};
    const SCEVTypes SCEVType = convertToSCEVype(m_MaxMin);
    const SCEV *R1Expr = SE->getMinMaxExpr(SCEVType, Ops1);

    Instruction *R1MinMax = findClosestMatchingDominator(R1Expr, I);

    if (!R1MinMax)
      return nullptr;

    LLVM_DEBUG(dbgs() << "NARY: Found common sub-expr: " << *R1MinMax << "\n");

    SmallVector<const SCEV *, 2> Ops2{SE->getUnknown(C),
                                      SE->getUnknown(R1MinMax)};
    const SCEV *R2Expr = SE->getMinMaxExpr(SCEVType, Ops2);

    SCEVExpander Expander(*SE, *DL, "nary-reassociate");
    Value *NewMinMax = Expander.expandCodeFor(R2Expr, I->getType(), I);
    NewMinMax->setName(Twine(I->getName()).concat(".nary"));

    LLVM_DEBUG(dbgs() << "NARY: Deleting:  " << *I << "\n"
                      << "NARY: Inserting: " << *NewMinMax << "\n");
    return NewMinMax;
  };

  const SCEV *AExpr = SE->getSCEV(A);
  const SCEV *BExpr = SE->getSCEV(B);
  const SCEV *RHSExpr = SE->getSCEV(RHS);

  if (BExpr != RHSExpr) {
    // Try (A op RHS) op B
    if (auto *NewMinMax = tryCombination(A, AExpr, RHS, RHSExpr, B, BExpr))
      return NewMinMax;
  }

  if (AExpr != RHSExpr) {
    // Try (RHS op B) op A
    if (auto *NewMinMax = tryCombination(RHS, RHSExpr, B, BExpr, A, AExpr))
      return NewMinMax;
  }

  return nullptr;
}
