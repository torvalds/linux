//===----------------------- AlignmentFromAssumptions.cpp -----------------===//
//                  Set Load/Store Alignments From Assumptions
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a ScalarEvolution-based transformation to set
// the alignments of load, stores and memory intrinsics based on the truth
// expressions of assume intrinsics. The primary motivation is to handle
// complex alignment assumptions that apply to vector loads and stores that
// appear after vectorization and unrolling.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/AlignmentFromAssumptions.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "alignment-from-assumptions"
using namespace llvm;

STATISTIC(NumLoadAlignChanged,
  "Number of loads changed by alignment assumptions");
STATISTIC(NumStoreAlignChanged,
  "Number of stores changed by alignment assumptions");
STATISTIC(NumMemIntAlignChanged,
  "Number of memory intrinsics changed by alignment assumptions");

// Given an expression for the (constant) alignment, AlignSCEV, and an
// expression for the displacement between a pointer and the aligned address,
// DiffSCEV, compute the alignment of the displaced pointer if it can be reduced
// to a constant. Using SCEV to compute alignment handles the case where
// DiffSCEV is a recurrence with constant start such that the aligned offset
// is constant. e.g. {16,+,32} % 32 -> 16.
static MaybeAlign getNewAlignmentDiff(const SCEV *DiffSCEV,
                                      const SCEV *AlignSCEV,
                                      ScalarEvolution *SE) {
  // DiffUnits = Diff % int64_t(Alignment)
  const SCEV *DiffUnitsSCEV = SE->getURemExpr(DiffSCEV, AlignSCEV);

  LLVM_DEBUG(dbgs() << "\talignment relative to " << *AlignSCEV << " is "
                    << *DiffUnitsSCEV << " (diff: " << *DiffSCEV << ")\n");

  if (const SCEVConstant *ConstDUSCEV =
      dyn_cast<SCEVConstant>(DiffUnitsSCEV)) {
    int64_t DiffUnits = ConstDUSCEV->getValue()->getSExtValue();

    // If the displacement is an exact multiple of the alignment, then the
    // displaced pointer has the same alignment as the aligned pointer, so
    // return the alignment value.
    if (!DiffUnits)
      return cast<SCEVConstant>(AlignSCEV)->getValue()->getAlignValue();

    // If the displacement is not an exact multiple, but the remainder is a
    // constant, then return this remainder (but only if it is a power of 2).
    uint64_t DiffUnitsAbs = std::abs(DiffUnits);
    if (isPowerOf2_64(DiffUnitsAbs))
      return Align(DiffUnitsAbs);
  }

  return std::nullopt;
}

// There is an address given by an offset OffSCEV from AASCEV which has an
// alignment AlignSCEV. Use that information, if possible, to compute a new
// alignment for Ptr.
static Align getNewAlignment(const SCEV *AASCEV, const SCEV *AlignSCEV,
                             const SCEV *OffSCEV, Value *Ptr,
                             ScalarEvolution *SE) {
  const SCEV *PtrSCEV = SE->getSCEV(Ptr);

  const SCEV *DiffSCEV = SE->getMinusSCEV(PtrSCEV, AASCEV);
  if (isa<SCEVCouldNotCompute>(DiffSCEV))
    return Align(1);

  // On 32-bit platforms, DiffSCEV might now have type i32 -- we've always
  // sign-extended OffSCEV to i64, so make sure they agree again.
  DiffSCEV = SE->getNoopOrSignExtend(DiffSCEV, OffSCEV->getType());

  // What we really want to know is the overall offset to the aligned
  // address. This address is displaced by the provided offset.
  DiffSCEV = SE->getAddExpr(DiffSCEV, OffSCEV);

  LLVM_DEBUG(dbgs() << "AFI: alignment of " << *Ptr << " relative to "
                    << *AlignSCEV << " and offset " << *OffSCEV
                    << " using diff " << *DiffSCEV << "\n");

  if (MaybeAlign NewAlignment = getNewAlignmentDiff(DiffSCEV, AlignSCEV, SE)) {
    LLVM_DEBUG(dbgs() << "\tnew alignment: " << DebugStr(NewAlignment) << "\n");
    return *NewAlignment;
  }

  if (const SCEVAddRecExpr *DiffARSCEV = dyn_cast<SCEVAddRecExpr>(DiffSCEV)) {
    // The relative offset to the alignment assumption did not yield a constant,
    // but we should try harder: if we assume that a is 32-byte aligned, then in
    // for (i = 0; i < 1024; i += 4) r += a[i]; not all of the loads from a are
    // 32-byte aligned, but instead alternate between 32 and 16-byte alignment.
    // As a result, the new alignment will not be a constant, but can still
    // be improved over the default (of 4) to 16.

    const SCEV *DiffStartSCEV = DiffARSCEV->getStart();
    const SCEV *DiffIncSCEV = DiffARSCEV->getStepRecurrence(*SE);

    LLVM_DEBUG(dbgs() << "\ttrying start/inc alignment using start "
                      << *DiffStartSCEV << " and inc " << *DiffIncSCEV << "\n");

    // Now compute the new alignment using the displacement to the value in the
    // first iteration, and also the alignment using the per-iteration delta.
    // If these are the same, then use that answer. Otherwise, use the smaller
    // one, but only if it divides the larger one.
    MaybeAlign NewAlignment = getNewAlignmentDiff(DiffStartSCEV, AlignSCEV, SE);
    MaybeAlign NewIncAlignment =
        getNewAlignmentDiff(DiffIncSCEV, AlignSCEV, SE);

    LLVM_DEBUG(dbgs() << "\tnew start alignment: " << DebugStr(NewAlignment)
                      << "\n");
    LLVM_DEBUG(dbgs() << "\tnew inc alignment: " << DebugStr(NewIncAlignment)
                      << "\n");

    if (!NewAlignment || !NewIncAlignment)
      return Align(1);

    const Align NewAlign = *NewAlignment;
    const Align NewIncAlign = *NewIncAlignment;
    if (NewAlign > NewIncAlign) {
      LLVM_DEBUG(dbgs() << "\tnew start/inc alignment: "
                        << DebugStr(NewIncAlign) << "\n");
      return NewIncAlign;
    }
    if (NewIncAlign > NewAlign) {
      LLVM_DEBUG(dbgs() << "\tnew start/inc alignment: " << DebugStr(NewAlign)
                        << "\n");
      return NewAlign;
    }
    assert(NewIncAlign == NewAlign);
    LLVM_DEBUG(dbgs() << "\tnew start/inc alignment: " << DebugStr(NewAlign)
                      << "\n");
    return NewAlign;
  }

  return Align(1);
}

bool AlignmentFromAssumptionsPass::extractAlignmentInfo(CallInst *I,
                                                        unsigned Idx,
                                                        Value *&AAPtr,
                                                        const SCEV *&AlignSCEV,
                                                        const SCEV *&OffSCEV) {
  Type *Int64Ty = Type::getInt64Ty(I->getContext());
  OperandBundleUse AlignOB = I->getOperandBundleAt(Idx);
  if (AlignOB.getTagName() != "align")
    return false;
  assert(AlignOB.Inputs.size() >= 2);
  AAPtr = AlignOB.Inputs[0].get();
  // TODO: Consider accumulating the offset to the base.
  AAPtr = AAPtr->stripPointerCastsSameRepresentation();
  AlignSCEV = SE->getSCEV(AlignOB.Inputs[1].get());
  AlignSCEV = SE->getTruncateOrZeroExtend(AlignSCEV, Int64Ty);
  if (!isa<SCEVConstant>(AlignSCEV))
    // Added to suppress a crash because consumer doesn't expect non-constant
    // alignments in the assume bundle.  TODO: Consider generalizing caller.
    return false;
  if (!cast<SCEVConstant>(AlignSCEV)->getAPInt().isPowerOf2())
    // Only power of two alignments are supported.
    return false;
  if (AlignOB.Inputs.size() == 3)
    OffSCEV = SE->getSCEV(AlignOB.Inputs[2].get());
  else
    OffSCEV = SE->getZero(Int64Ty);
  OffSCEV = SE->getTruncateOrZeroExtend(OffSCEV, Int64Ty);
  return true;
}

bool AlignmentFromAssumptionsPass::processAssumption(CallInst *ACall,
                                                     unsigned Idx) {
  Value *AAPtr;
  const SCEV *AlignSCEV, *OffSCEV;
  if (!extractAlignmentInfo(ACall, Idx, AAPtr, AlignSCEV, OffSCEV))
    return false;

  // Skip ConstantPointerNull and UndefValue.  Assumptions on these shouldn't
  // affect other users.
  if (isa<ConstantData>(AAPtr))
    return false;

  const SCEV *AASCEV = SE->getSCEV(AAPtr);

  // Apply the assumption to all other users of the specified pointer.
  SmallPtrSet<Instruction *, 32> Visited;
  SmallVector<Instruction*, 16> WorkList;
  for (User *J : AAPtr->users()) {
    if (J == ACall)
      continue;

    if (Instruction *K = dyn_cast<Instruction>(J))
        WorkList.push_back(K);
  }

  while (!WorkList.empty()) {
    Instruction *J = WorkList.pop_back_val();
    if (LoadInst *LI = dyn_cast<LoadInst>(J)) {
      if (!isValidAssumeForContext(ACall, J, DT))
        continue;
      Align NewAlignment = getNewAlignment(AASCEV, AlignSCEV, OffSCEV,
                                           LI->getPointerOperand(), SE);
      if (NewAlignment > LI->getAlign()) {
        LI->setAlignment(NewAlignment);
        ++NumLoadAlignChanged;
      }
    } else if (StoreInst *SI = dyn_cast<StoreInst>(J)) {
      if (!isValidAssumeForContext(ACall, J, DT))
        continue;
      Align NewAlignment = getNewAlignment(AASCEV, AlignSCEV, OffSCEV,
                                           SI->getPointerOperand(), SE);
      if (NewAlignment > SI->getAlign()) {
        SI->setAlignment(NewAlignment);
        ++NumStoreAlignChanged;
      }
    } else if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(J)) {
      if (!isValidAssumeForContext(ACall, J, DT))
        continue;
      Align NewDestAlignment =
          getNewAlignment(AASCEV, AlignSCEV, OffSCEV, MI->getDest(), SE);

      LLVM_DEBUG(dbgs() << "\tmem inst: " << DebugStr(NewDestAlignment)
                        << "\n";);
      if (NewDestAlignment > *MI->getDestAlign()) {
        MI->setDestAlignment(NewDestAlignment);
        ++NumMemIntAlignChanged;
      }

      // For memory transfers, there is also a source alignment that
      // can be set.
      if (MemTransferInst *MTI = dyn_cast<MemTransferInst>(MI)) {
        Align NewSrcAlignment =
            getNewAlignment(AASCEV, AlignSCEV, OffSCEV, MTI->getSource(), SE);

        LLVM_DEBUG(dbgs() << "\tmem trans: " << DebugStr(NewSrcAlignment)
                          << "\n";);

        if (NewSrcAlignment > *MTI->getSourceAlign()) {
          MTI->setSourceAlignment(NewSrcAlignment);
          ++NumMemIntAlignChanged;
        }
      }
    }

    // Now that we've updated that use of the pointer, look for other uses of
    // the pointer to update.
    Visited.insert(J);
    if (isa<GetElementPtrInst>(J) || isa<PHINode>(J))
      for (auto &U : J->uses()) {
        if (U->getType()->isPointerTy()) {
          Instruction *K = cast<Instruction>(U.getUser());
          StoreInst *SI = dyn_cast<StoreInst>(K);
          if (SI && SI->getPointerOperandIndex() != U.getOperandNo())
            continue;
          if (!Visited.count(K))
            WorkList.push_back(K);
        }
      }
  }

  return true;
}

bool AlignmentFromAssumptionsPass::runImpl(Function &F, AssumptionCache &AC,
                                           ScalarEvolution *SE_,
                                           DominatorTree *DT_) {
  SE = SE_;
  DT = DT_;

  bool Changed = false;
  for (auto &AssumeVH : AC.assumptions())
    if (AssumeVH) {
      CallInst *Call = cast<CallInst>(AssumeVH);
      for (unsigned Idx = 0; Idx < Call->getNumOperandBundles(); Idx++)
        Changed |= processAssumption(Call, Idx);
    }

  return Changed;
}

PreservedAnalyses
AlignmentFromAssumptionsPass::run(Function &F, FunctionAnalysisManager &AM) {

  AssumptionCache &AC = AM.getResult<AssumptionAnalysis>(F);
  ScalarEvolution &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
  if (!runImpl(F, AC, &SE, &DT))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<ScalarEvolutionAnalysis>();
  return PA;
}
