//===- InlineFunction.cpp - Code to perform function inlining -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements inlining of a function into a call site, resolving
// parameters and the return value as appropriate.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/IndirectCallVisitor.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/MemoryProfileInfo.h"
#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/Analysis/ObjCARCUtil.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define DEBUG_TYPE "inline-function"

using namespace llvm;
using namespace llvm::memprof;
using ProfileCount = Function::ProfileCount;

static cl::opt<bool>
EnableNoAliasConversion("enable-noalias-to-md-conversion", cl::init(true),
  cl::Hidden,
  cl::desc("Convert noalias attributes to metadata during inlining."));

static cl::opt<bool>
    UseNoAliasIntrinsic("use-noalias-intrinsic-during-inlining", cl::Hidden,
                        cl::init(true),
                        cl::desc("Use the llvm.experimental.noalias.scope.decl "
                                 "intrinsic during inlining."));

// Disabled by default, because the added alignment assumptions may increase
// compile-time and block optimizations. This option is not suitable for use
// with frontends that emit comprehensive parameter alignment annotations.
static cl::opt<bool>
PreserveAlignmentAssumptions("preserve-alignment-assumptions-during-inlining",
  cl::init(false), cl::Hidden,
  cl::desc("Convert align attributes to assumptions during inlining."));

static cl::opt<unsigned> InlinerAttributeWindow(
    "max-inst-checked-for-throw-during-inlining", cl::Hidden,
    cl::desc("the maximum number of instructions analyzed for may throw during "
             "attribute inference in inlined body"),
    cl::init(4));

namespace {

  /// A class for recording information about inlining a landing pad.
  class LandingPadInliningInfo {
    /// Destination of the invoke's unwind.
    BasicBlock *OuterResumeDest;

    /// Destination for the callee's resume.
    BasicBlock *InnerResumeDest = nullptr;

    /// LandingPadInst associated with the invoke.
    LandingPadInst *CallerLPad = nullptr;

    /// PHI for EH values from landingpad insts.
    PHINode *InnerEHValuesPHI = nullptr;

    SmallVector<Value*, 8> UnwindDestPHIValues;

  public:
    LandingPadInliningInfo(InvokeInst *II)
        : OuterResumeDest(II->getUnwindDest()) {
      // If there are PHI nodes in the unwind destination block, we need to keep
      // track of which values came into them from the invoke before removing
      // the edge from this block.
      BasicBlock *InvokeBB = II->getParent();
      BasicBlock::iterator I = OuterResumeDest->begin();
      for (; isa<PHINode>(I); ++I) {
        // Save the value to use for this edge.
        PHINode *PHI = cast<PHINode>(I);
        UnwindDestPHIValues.push_back(PHI->getIncomingValueForBlock(InvokeBB));
      }

      CallerLPad = cast<LandingPadInst>(I);
    }

    /// The outer unwind destination is the target of
    /// unwind edges introduced for calls within the inlined function.
    BasicBlock *getOuterResumeDest() const {
      return OuterResumeDest;
    }

    BasicBlock *getInnerResumeDest();

    LandingPadInst *getLandingPadInst() const { return CallerLPad; }

    /// Forward the 'resume' instruction to the caller's landing pad block.
    /// When the landing pad block has only one predecessor, this is
    /// a simple branch. When there is more than one predecessor, we need to
    /// split the landing pad block after the landingpad instruction and jump
    /// to there.
    void forwardResume(ResumeInst *RI,
                       SmallPtrSetImpl<LandingPadInst*> &InlinedLPads);

    /// Add incoming-PHI values to the unwind destination block for the given
    /// basic block, using the values for the original invoke's source block.
    void addIncomingPHIValuesFor(BasicBlock *BB) const {
      addIncomingPHIValuesForInto(BB, OuterResumeDest);
    }

    void addIncomingPHIValuesForInto(BasicBlock *src, BasicBlock *dest) const {
      BasicBlock::iterator I = dest->begin();
      for (unsigned i = 0, e = UnwindDestPHIValues.size(); i != e; ++i, ++I) {
        PHINode *phi = cast<PHINode>(I);
        phi->addIncoming(UnwindDestPHIValues[i], src);
      }
    }
  };

} // end anonymous namespace

/// Get or create a target for the branch from ResumeInsts.
BasicBlock *LandingPadInliningInfo::getInnerResumeDest() {
  if (InnerResumeDest) return InnerResumeDest;

  // Split the landing pad.
  BasicBlock::iterator SplitPoint = ++CallerLPad->getIterator();
  InnerResumeDest =
    OuterResumeDest->splitBasicBlock(SplitPoint,
                                     OuterResumeDest->getName() + ".body");

  // The number of incoming edges we expect to the inner landing pad.
  const unsigned PHICapacity = 2;

  // Create corresponding new PHIs for all the PHIs in the outer landing pad.
  BasicBlock::iterator InsertPoint = InnerResumeDest->begin();
  BasicBlock::iterator I = OuterResumeDest->begin();
  for (unsigned i = 0, e = UnwindDestPHIValues.size(); i != e; ++i, ++I) {
    PHINode *OuterPHI = cast<PHINode>(I);
    PHINode *InnerPHI = PHINode::Create(OuterPHI->getType(), PHICapacity,
                                        OuterPHI->getName() + ".lpad-body");
    InnerPHI->insertBefore(InsertPoint);
    OuterPHI->replaceAllUsesWith(InnerPHI);
    InnerPHI->addIncoming(OuterPHI, OuterResumeDest);
  }

  // Create a PHI for the exception values.
  InnerEHValuesPHI =
      PHINode::Create(CallerLPad->getType(), PHICapacity, "eh.lpad-body");
  InnerEHValuesPHI->insertBefore(InsertPoint);
  CallerLPad->replaceAllUsesWith(InnerEHValuesPHI);
  InnerEHValuesPHI->addIncoming(CallerLPad, OuterResumeDest);

  // All done.
  return InnerResumeDest;
}

/// Forward the 'resume' instruction to the caller's landing pad block.
/// When the landing pad block has only one predecessor, this is a simple
/// branch. When there is more than one predecessor, we need to split the
/// landing pad block after the landingpad instruction and jump to there.
void LandingPadInliningInfo::forwardResume(
    ResumeInst *RI, SmallPtrSetImpl<LandingPadInst *> &InlinedLPads) {
  BasicBlock *Dest = getInnerResumeDest();
  BasicBlock *Src = RI->getParent();

  BranchInst::Create(Dest, Src);

  // Update the PHIs in the destination. They were inserted in an order which
  // makes this work.
  addIncomingPHIValuesForInto(Src, Dest);

  InnerEHValuesPHI->addIncoming(RI->getOperand(0), Src);
  RI->eraseFromParent();
}

/// Helper for getUnwindDestToken/getUnwindDestTokenHelper.
static Value *getParentPad(Value *EHPad) {
  if (auto *FPI = dyn_cast<FuncletPadInst>(EHPad))
    return FPI->getParentPad();
  return cast<CatchSwitchInst>(EHPad)->getParentPad();
}

using UnwindDestMemoTy = DenseMap<Instruction *, Value *>;

/// Helper for getUnwindDestToken that does the descendant-ward part of
/// the search.
static Value *getUnwindDestTokenHelper(Instruction *EHPad,
                                       UnwindDestMemoTy &MemoMap) {
  SmallVector<Instruction *, 8> Worklist(1, EHPad);

  while (!Worklist.empty()) {
    Instruction *CurrentPad = Worklist.pop_back_val();
    // We only put pads on the worklist that aren't in the MemoMap.  When
    // we find an unwind dest for a pad we may update its ancestors, but
    // the queue only ever contains uncles/great-uncles/etc. of CurrentPad,
    // so they should never get updated while queued on the worklist.
    assert(!MemoMap.count(CurrentPad));
    Value *UnwindDestToken = nullptr;
    if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(CurrentPad)) {
      if (CatchSwitch->hasUnwindDest()) {
        UnwindDestToken = CatchSwitch->getUnwindDest()->getFirstNonPHI();
      } else {
        // Catchswitch doesn't have a 'nounwind' variant, and one might be
        // annotated as "unwinds to caller" when really it's nounwind (see
        // e.g. SimplifyCFGOpt::SimplifyUnreachable), so we can't infer the
        // parent's unwind dest from this.  We can check its catchpads'
        // descendants, since they might include a cleanuppad with an
        // "unwinds to caller" cleanupret, which can be trusted.
        for (auto HI = CatchSwitch->handler_begin(),
                  HE = CatchSwitch->handler_end();
             HI != HE && !UnwindDestToken; ++HI) {
          BasicBlock *HandlerBlock = *HI;
          auto *CatchPad = cast<CatchPadInst>(HandlerBlock->getFirstNonPHI());
          for (User *Child : CatchPad->users()) {
            // Intentionally ignore invokes here -- since the catchswitch is
            // marked "unwind to caller", it would be a verifier error if it
            // contained an invoke which unwinds out of it, so any invoke we'd
            // encounter must unwind to some child of the catch.
            if (!isa<CleanupPadInst>(Child) && !isa<CatchSwitchInst>(Child))
              continue;

            Instruction *ChildPad = cast<Instruction>(Child);
            auto Memo = MemoMap.find(ChildPad);
            if (Memo == MemoMap.end()) {
              // Haven't figured out this child pad yet; queue it.
              Worklist.push_back(ChildPad);
              continue;
            }
            // We've already checked this child, but might have found that
            // it offers no proof either way.
            Value *ChildUnwindDestToken = Memo->second;
            if (!ChildUnwindDestToken)
              continue;
            // We already know the child's unwind dest, which can either
            // be ConstantTokenNone to indicate unwind to caller, or can
            // be another child of the catchpad.  Only the former indicates
            // the unwind dest of the catchswitch.
            if (isa<ConstantTokenNone>(ChildUnwindDestToken)) {
              UnwindDestToken = ChildUnwindDestToken;
              break;
            }
            assert(getParentPad(ChildUnwindDestToken) == CatchPad);
          }
        }
      }
    } else {
      auto *CleanupPad = cast<CleanupPadInst>(CurrentPad);
      for (User *U : CleanupPad->users()) {
        if (auto *CleanupRet = dyn_cast<CleanupReturnInst>(U)) {
          if (BasicBlock *RetUnwindDest = CleanupRet->getUnwindDest())
            UnwindDestToken = RetUnwindDest->getFirstNonPHI();
          else
            UnwindDestToken = ConstantTokenNone::get(CleanupPad->getContext());
          break;
        }
        Value *ChildUnwindDestToken;
        if (auto *Invoke = dyn_cast<InvokeInst>(U)) {
          ChildUnwindDestToken = Invoke->getUnwindDest()->getFirstNonPHI();
        } else if (isa<CleanupPadInst>(U) || isa<CatchSwitchInst>(U)) {
          Instruction *ChildPad = cast<Instruction>(U);
          auto Memo = MemoMap.find(ChildPad);
          if (Memo == MemoMap.end()) {
            // Haven't resolved this child yet; queue it and keep searching.
            Worklist.push_back(ChildPad);
            continue;
          }
          // We've checked this child, but still need to ignore it if it
          // had no proof either way.
          ChildUnwindDestToken = Memo->second;
          if (!ChildUnwindDestToken)
            continue;
        } else {
          // Not a relevant user of the cleanuppad
          continue;
        }
        // In a well-formed program, the child/invoke must either unwind to
        // an(other) child of the cleanup, or exit the cleanup.  In the
        // first case, continue searching.
        if (isa<Instruction>(ChildUnwindDestToken) &&
            getParentPad(ChildUnwindDestToken) == CleanupPad)
          continue;
        UnwindDestToken = ChildUnwindDestToken;
        break;
      }
    }
    // If we haven't found an unwind dest for CurrentPad, we may have queued its
    // children, so move on to the next in the worklist.
    if (!UnwindDestToken)
      continue;

    // Now we know that CurrentPad unwinds to UnwindDestToken.  It also exits
    // any ancestors of CurrentPad up to but not including UnwindDestToken's
    // parent pad.  Record this in the memo map, and check to see if the
    // original EHPad being queried is one of the ones exited.
    Value *UnwindParent;
    if (auto *UnwindPad = dyn_cast<Instruction>(UnwindDestToken))
      UnwindParent = getParentPad(UnwindPad);
    else
      UnwindParent = nullptr;
    bool ExitedOriginalPad = false;
    for (Instruction *ExitedPad = CurrentPad;
         ExitedPad && ExitedPad != UnwindParent;
         ExitedPad = dyn_cast<Instruction>(getParentPad(ExitedPad))) {
      // Skip over catchpads since they just follow their catchswitches.
      if (isa<CatchPadInst>(ExitedPad))
        continue;
      MemoMap[ExitedPad] = UnwindDestToken;
      ExitedOriginalPad |= (ExitedPad == EHPad);
    }

    if (ExitedOriginalPad)
      return UnwindDestToken;

    // Continue the search.
  }

  // No definitive information is contained within this funclet.
  return nullptr;
}

/// Given an EH pad, find where it unwinds.  If it unwinds to an EH pad,
/// return that pad instruction.  If it unwinds to caller, return
/// ConstantTokenNone.  If it does not have a definitive unwind destination,
/// return nullptr.
///
/// This routine gets invoked for calls in funclets in inlinees when inlining
/// an invoke.  Since many funclets don't have calls inside them, it's queried
/// on-demand rather than building a map of pads to unwind dests up front.
/// Determining a funclet's unwind dest may require recursively searching its
/// descendants, and also ancestors and cousins if the descendants don't provide
/// an answer.  Since most funclets will have their unwind dest immediately
/// available as the unwind dest of a catchswitch or cleanupret, this routine
/// searches top-down from the given pad and then up. To avoid worst-case
/// quadratic run-time given that approach, it uses a memo map to avoid
/// re-processing funclet trees.  The callers that rewrite the IR as they go
/// take advantage of this, for correctness, by checking/forcing rewritten
/// pads' entries to match the original callee view.
static Value *getUnwindDestToken(Instruction *EHPad,
                                 UnwindDestMemoTy &MemoMap) {
  // Catchpads unwind to the same place as their catchswitch;
  // redirct any queries on catchpads so the code below can
  // deal with just catchswitches and cleanuppads.
  if (auto *CPI = dyn_cast<CatchPadInst>(EHPad))
    EHPad = CPI->getCatchSwitch();

  // Check if we've already determined the unwind dest for this pad.
  auto Memo = MemoMap.find(EHPad);
  if (Memo != MemoMap.end())
    return Memo->second;

  // Search EHPad and, if necessary, its descendants.
  Value *UnwindDestToken = getUnwindDestTokenHelper(EHPad, MemoMap);
  assert((UnwindDestToken == nullptr) != (MemoMap.count(EHPad) != 0));
  if (UnwindDestToken)
    return UnwindDestToken;

  // No information is available for this EHPad from itself or any of its
  // descendants.  An unwind all the way out to a pad in the caller would
  // need also to agree with the unwind dest of the parent funclet, so
  // search up the chain to try to find a funclet with information.  Put
  // null entries in the memo map to avoid re-processing as we go up.
  MemoMap[EHPad] = nullptr;
#ifndef NDEBUG
  SmallPtrSet<Instruction *, 4> TempMemos;
  TempMemos.insert(EHPad);
#endif
  Instruction *LastUselessPad = EHPad;
  Value *AncestorToken;
  for (AncestorToken = getParentPad(EHPad);
       auto *AncestorPad = dyn_cast<Instruction>(AncestorToken);
       AncestorToken = getParentPad(AncestorToken)) {
    // Skip over catchpads since they just follow their catchswitches.
    if (isa<CatchPadInst>(AncestorPad))
      continue;
    // If the MemoMap had an entry mapping AncestorPad to nullptr, since we
    // haven't yet called getUnwindDestTokenHelper for AncestorPad in this
    // call to getUnwindDestToken, that would mean that AncestorPad had no
    // information in itself, its descendants, or its ancestors.  If that
    // were the case, then we should also have recorded the lack of information
    // for the descendant that we're coming from.  So assert that we don't
    // find a null entry in the MemoMap for AncestorPad.
    assert(!MemoMap.count(AncestorPad) || MemoMap[AncestorPad]);
    auto AncestorMemo = MemoMap.find(AncestorPad);
    if (AncestorMemo == MemoMap.end()) {
      UnwindDestToken = getUnwindDestTokenHelper(AncestorPad, MemoMap);
    } else {
      UnwindDestToken = AncestorMemo->second;
    }
    if (UnwindDestToken)
      break;
    LastUselessPad = AncestorPad;
    MemoMap[LastUselessPad] = nullptr;
#ifndef NDEBUG
    TempMemos.insert(LastUselessPad);
#endif
  }

  // We know that getUnwindDestTokenHelper was called on LastUselessPad and
  // returned nullptr (and likewise for EHPad and any of its ancestors up to
  // LastUselessPad), so LastUselessPad has no information from below.  Since
  // getUnwindDestTokenHelper must investigate all downward paths through
  // no-information nodes to prove that a node has no information like this,
  // and since any time it finds information it records it in the MemoMap for
  // not just the immediately-containing funclet but also any ancestors also
  // exited, it must be the case that, walking downward from LastUselessPad,
  // visiting just those nodes which have not been mapped to an unwind dest
  // by getUnwindDestTokenHelper (the nullptr TempMemos notwithstanding, since
  // they are just used to keep getUnwindDestTokenHelper from repeating work),
  // any node visited must have been exhaustively searched with no information
  // for it found.
  SmallVector<Instruction *, 8> Worklist(1, LastUselessPad);
  while (!Worklist.empty()) {
    Instruction *UselessPad = Worklist.pop_back_val();
    auto Memo = MemoMap.find(UselessPad);
    if (Memo != MemoMap.end() && Memo->second) {
      // Here the name 'UselessPad' is a bit of a misnomer, because we've found
      // that it is a funclet that does have information about unwinding to
      // a particular destination; its parent was a useless pad.
      // Since its parent has no information, the unwind edge must not escape
      // the parent, and must target a sibling of this pad.  This local unwind
      // gives us no information about EHPad.  Leave it and the subtree rooted
      // at it alone.
      assert(getParentPad(Memo->second) == getParentPad(UselessPad));
      continue;
    }
    // We know we don't have information for UselesPad.  If it has an entry in
    // the MemoMap (mapping it to nullptr), it must be one of the TempMemos
    // added on this invocation of getUnwindDestToken; if a previous invocation
    // recorded nullptr, it would have had to prove that the ancestors of
    // UselessPad, which include LastUselessPad, had no information, and that
    // in turn would have required proving that the descendants of
    // LastUselesPad, which include EHPad, have no information about
    // LastUselessPad, which would imply that EHPad was mapped to nullptr in
    // the MemoMap on that invocation, which isn't the case if we got here.
    assert(!MemoMap.count(UselessPad) || TempMemos.count(UselessPad));
    // Assert as we enumerate users that 'UselessPad' doesn't have any unwind
    // information that we'd be contradicting by making a map entry for it
    // (which is something that getUnwindDestTokenHelper must have proved for
    // us to get here).  Just assert on is direct users here; the checks in
    // this downward walk at its descendants will verify that they don't have
    // any unwind edges that exit 'UselessPad' either (i.e. they either have no
    // unwind edges or unwind to a sibling).
    MemoMap[UselessPad] = UnwindDestToken;
    if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(UselessPad)) {
      assert(CatchSwitch->getUnwindDest() == nullptr && "Expected useless pad");
      for (BasicBlock *HandlerBlock : CatchSwitch->handlers()) {
        auto *CatchPad = HandlerBlock->getFirstNonPHI();
        for (User *U : CatchPad->users()) {
          assert(
              (!isa<InvokeInst>(U) ||
               (getParentPad(
                    cast<InvokeInst>(U)->getUnwindDest()->getFirstNonPHI()) ==
                CatchPad)) &&
              "Expected useless pad");
          if (isa<CatchSwitchInst>(U) || isa<CleanupPadInst>(U))
            Worklist.push_back(cast<Instruction>(U));
        }
      }
    } else {
      assert(isa<CleanupPadInst>(UselessPad));
      for (User *U : UselessPad->users()) {
        assert(!isa<CleanupReturnInst>(U) && "Expected useless pad");
        assert((!isa<InvokeInst>(U) ||
                (getParentPad(
                     cast<InvokeInst>(U)->getUnwindDest()->getFirstNonPHI()) ==
                 UselessPad)) &&
               "Expected useless pad");
        if (isa<CatchSwitchInst>(U) || isa<CleanupPadInst>(U))
          Worklist.push_back(cast<Instruction>(U));
      }
    }
  }

  return UnwindDestToken;
}

/// When we inline a basic block into an invoke,
/// we have to turn all of the calls that can throw into invokes.
/// This function analyze BB to see if there are any calls, and if so,
/// it rewrites them to be invokes that jump to InvokeDest and fills in the PHI
/// nodes in that block with the values specified in InvokeDestPHIValues.
static BasicBlock *HandleCallsInBlockInlinedThroughInvoke(
    BasicBlock *BB, BasicBlock *UnwindEdge,
    UnwindDestMemoTy *FuncletUnwindMap = nullptr) {
  for (Instruction &I : llvm::make_early_inc_range(*BB)) {
    // We only need to check for function calls: inlined invoke
    // instructions require no special handling.
    CallInst *CI = dyn_cast<CallInst>(&I);

    if (!CI || CI->doesNotThrow())
      continue;

    // We do not need to (and in fact, cannot) convert possibly throwing calls
    // to @llvm.experimental_deoptimize (resp. @llvm.experimental.guard) into
    // invokes.  The caller's "segment" of the deoptimization continuation
    // attached to the newly inlined @llvm.experimental_deoptimize
    // (resp. @llvm.experimental.guard) call should contain the exception
    // handling logic, if any.
    if (auto *F = CI->getCalledFunction())
      if (F->getIntrinsicID() == Intrinsic::experimental_deoptimize ||
          F->getIntrinsicID() == Intrinsic::experimental_guard)
        continue;

    if (auto FuncletBundle = CI->getOperandBundle(LLVMContext::OB_funclet)) {
      // This call is nested inside a funclet.  If that funclet has an unwind
      // destination within the inlinee, then unwinding out of this call would
      // be UB.  Rewriting this call to an invoke which targets the inlined
      // invoke's unwind dest would give the call's parent funclet multiple
      // unwind destinations, which is something that subsequent EH table
      // generation can't handle and that the veirifer rejects.  So when we
      // see such a call, leave it as a call.
      auto *FuncletPad = cast<Instruction>(FuncletBundle->Inputs[0]);
      Value *UnwindDestToken =
          getUnwindDestToken(FuncletPad, *FuncletUnwindMap);
      if (UnwindDestToken && !isa<ConstantTokenNone>(UnwindDestToken))
        continue;
#ifndef NDEBUG
      Instruction *MemoKey;
      if (auto *CatchPad = dyn_cast<CatchPadInst>(FuncletPad))
        MemoKey = CatchPad->getCatchSwitch();
      else
        MemoKey = FuncletPad;
      assert(FuncletUnwindMap->count(MemoKey) &&
             (*FuncletUnwindMap)[MemoKey] == UnwindDestToken &&
             "must get memoized to avoid confusing later searches");
#endif // NDEBUG
    }

    changeToInvokeAndSplitBasicBlock(CI, UnwindEdge);
    return BB;
  }
  return nullptr;
}

/// If we inlined an invoke site, we need to convert calls
/// in the body of the inlined function into invokes.
///
/// II is the invoke instruction being inlined.  FirstNewBlock is the first
/// block of the inlined code (the last block is the end of the function),
/// and InlineCodeInfo is information about the code that got inlined.
static void HandleInlinedLandingPad(InvokeInst *II, BasicBlock *FirstNewBlock,
                                    ClonedCodeInfo &InlinedCodeInfo) {
  BasicBlock *InvokeDest = II->getUnwindDest();

  Function *Caller = FirstNewBlock->getParent();

  // The inlined code is currently at the end of the function, scan from the
  // start of the inlined code to its end, checking for stuff we need to
  // rewrite.
  LandingPadInliningInfo Invoke(II);

  // Get all of the inlined landing pad instructions.
  SmallPtrSet<LandingPadInst*, 16> InlinedLPads;
  for (Function::iterator I = FirstNewBlock->getIterator(), E = Caller->end();
       I != E; ++I)
    if (InvokeInst *II = dyn_cast<InvokeInst>(I->getTerminator()))
      InlinedLPads.insert(II->getLandingPadInst());

  // Append the clauses from the outer landing pad instruction into the inlined
  // landing pad instructions.
  LandingPadInst *OuterLPad = Invoke.getLandingPadInst();
  for (LandingPadInst *InlinedLPad : InlinedLPads) {
    unsigned OuterNum = OuterLPad->getNumClauses();
    InlinedLPad->reserveClauses(OuterNum);
    for (unsigned OuterIdx = 0; OuterIdx != OuterNum; ++OuterIdx)
      InlinedLPad->addClause(OuterLPad->getClause(OuterIdx));
    if (OuterLPad->isCleanup())
      InlinedLPad->setCleanup(true);
  }

  for (Function::iterator BB = FirstNewBlock->getIterator(), E = Caller->end();
       BB != E; ++BB) {
    if (InlinedCodeInfo.ContainsCalls)
      if (BasicBlock *NewBB = HandleCallsInBlockInlinedThroughInvoke(
              &*BB, Invoke.getOuterResumeDest()))
        // Update any PHI nodes in the exceptional block to indicate that there
        // is now a new entry in them.
        Invoke.addIncomingPHIValuesFor(NewBB);

    // Forward any resumes that are remaining here.
    if (ResumeInst *RI = dyn_cast<ResumeInst>(BB->getTerminator()))
      Invoke.forwardResume(RI, InlinedLPads);
  }

  // Now that everything is happy, we have one final detail.  The PHI nodes in
  // the exception destination block still have entries due to the original
  // invoke instruction. Eliminate these entries (which might even delete the
  // PHI node) now.
  InvokeDest->removePredecessor(II->getParent());
}

/// If we inlined an invoke site, we need to convert calls
/// in the body of the inlined function into invokes.
///
/// II is the invoke instruction being inlined.  FirstNewBlock is the first
/// block of the inlined code (the last block is the end of the function),
/// and InlineCodeInfo is information about the code that got inlined.
static void HandleInlinedEHPad(InvokeInst *II, BasicBlock *FirstNewBlock,
                               ClonedCodeInfo &InlinedCodeInfo) {
  BasicBlock *UnwindDest = II->getUnwindDest();
  Function *Caller = FirstNewBlock->getParent();

  assert(UnwindDest->getFirstNonPHI()->isEHPad() && "unexpected BasicBlock!");

  // If there are PHI nodes in the unwind destination block, we need to keep
  // track of which values came into them from the invoke before removing the
  // edge from this block.
  SmallVector<Value *, 8> UnwindDestPHIValues;
  BasicBlock *InvokeBB = II->getParent();
  for (PHINode &PHI : UnwindDest->phis()) {
    // Save the value to use for this edge.
    UnwindDestPHIValues.push_back(PHI.getIncomingValueForBlock(InvokeBB));
  }

  // Add incoming-PHI values to the unwind destination block for the given basic
  // block, using the values for the original invoke's source block.
  auto UpdatePHINodes = [&](BasicBlock *Src) {
    BasicBlock::iterator I = UnwindDest->begin();
    for (Value *V : UnwindDestPHIValues) {
      PHINode *PHI = cast<PHINode>(I);
      PHI->addIncoming(V, Src);
      ++I;
    }
  };

  // This connects all the instructions which 'unwind to caller' to the invoke
  // destination.
  UnwindDestMemoTy FuncletUnwindMap;
  for (Function::iterator BB = FirstNewBlock->getIterator(), E = Caller->end();
       BB != E; ++BB) {
    if (auto *CRI = dyn_cast<CleanupReturnInst>(BB->getTerminator())) {
      if (CRI->unwindsToCaller()) {
        auto *CleanupPad = CRI->getCleanupPad();
        CleanupReturnInst::Create(CleanupPad, UnwindDest, CRI->getIterator());
        CRI->eraseFromParent();
        UpdatePHINodes(&*BB);
        // Finding a cleanupret with an unwind destination would confuse
        // subsequent calls to getUnwindDestToken, so map the cleanuppad
        // to short-circuit any such calls and recognize this as an "unwind
        // to caller" cleanup.
        assert(!FuncletUnwindMap.count(CleanupPad) ||
               isa<ConstantTokenNone>(FuncletUnwindMap[CleanupPad]));
        FuncletUnwindMap[CleanupPad] =
            ConstantTokenNone::get(Caller->getContext());
      }
    }

    Instruction *I = BB->getFirstNonPHI();
    if (!I->isEHPad())
      continue;

    Instruction *Replacement = nullptr;
    if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(I)) {
      if (CatchSwitch->unwindsToCaller()) {
        Value *UnwindDestToken;
        if (auto *ParentPad =
                dyn_cast<Instruction>(CatchSwitch->getParentPad())) {
          // This catchswitch is nested inside another funclet.  If that
          // funclet has an unwind destination within the inlinee, then
          // unwinding out of this catchswitch would be UB.  Rewriting this
          // catchswitch to unwind to the inlined invoke's unwind dest would
          // give the parent funclet multiple unwind destinations, which is
          // something that subsequent EH table generation can't handle and
          // that the veirifer rejects.  So when we see such a call, leave it
          // as "unwind to caller".
          UnwindDestToken = getUnwindDestToken(ParentPad, FuncletUnwindMap);
          if (UnwindDestToken && !isa<ConstantTokenNone>(UnwindDestToken))
            continue;
        } else {
          // This catchswitch has no parent to inherit constraints from, and
          // none of its descendants can have an unwind edge that exits it and
          // targets another funclet in the inlinee.  It may or may not have a
          // descendant that definitively has an unwind to caller.  In either
          // case, we'll have to assume that any unwinds out of it may need to
          // be routed to the caller, so treat it as though it has a definitive
          // unwind to caller.
          UnwindDestToken = ConstantTokenNone::get(Caller->getContext());
        }
        auto *NewCatchSwitch = CatchSwitchInst::Create(
            CatchSwitch->getParentPad(), UnwindDest,
            CatchSwitch->getNumHandlers(), CatchSwitch->getName(),
            CatchSwitch->getIterator());
        for (BasicBlock *PadBB : CatchSwitch->handlers())
          NewCatchSwitch->addHandler(PadBB);
        // Propagate info for the old catchswitch over to the new one in
        // the unwind map.  This also serves to short-circuit any subsequent
        // checks for the unwind dest of this catchswitch, which would get
        // confused if they found the outer handler in the callee.
        FuncletUnwindMap[NewCatchSwitch] = UnwindDestToken;
        Replacement = NewCatchSwitch;
      }
    } else if (!isa<FuncletPadInst>(I)) {
      llvm_unreachable("unexpected EHPad!");
    }

    if (Replacement) {
      Replacement->takeName(I);
      I->replaceAllUsesWith(Replacement);
      I->eraseFromParent();
      UpdatePHINodes(&*BB);
    }
  }

  if (InlinedCodeInfo.ContainsCalls)
    for (Function::iterator BB = FirstNewBlock->getIterator(),
                            E = Caller->end();
         BB != E; ++BB)
      if (BasicBlock *NewBB = HandleCallsInBlockInlinedThroughInvoke(
              &*BB, UnwindDest, &FuncletUnwindMap))
        // Update any PHI nodes in the exceptional block to indicate that there
        // is now a new entry in them.
        UpdatePHINodes(NewBB);

  // Now that everything is happy, we have one final detail.  The PHI nodes in
  // the exception destination block still have entries due to the original
  // invoke instruction. Eliminate these entries (which might even delete the
  // PHI node) now.
  UnwindDest->removePredecessor(InvokeBB);
}

static bool haveCommonPrefix(MDNode *MIBStackContext,
                             MDNode *CallsiteStackContext) {
  assert(MIBStackContext->getNumOperands() > 0 &&
         CallsiteStackContext->getNumOperands() > 0);
  // Because of the context trimming performed during matching, the callsite
  // context could have more stack ids than the MIB. We match up to the end of
  // the shortest stack context.
  for (auto MIBStackIter = MIBStackContext->op_begin(),
            CallsiteStackIter = CallsiteStackContext->op_begin();
       MIBStackIter != MIBStackContext->op_end() &&
       CallsiteStackIter != CallsiteStackContext->op_end();
       MIBStackIter++, CallsiteStackIter++) {
    auto *Val1 = mdconst::dyn_extract<ConstantInt>(*MIBStackIter);
    auto *Val2 = mdconst::dyn_extract<ConstantInt>(*CallsiteStackIter);
    assert(Val1 && Val2);
    if (Val1->getZExtValue() != Val2->getZExtValue())
      return false;
  }
  return true;
}

static void removeMemProfMetadata(CallBase *Call) {
  Call->setMetadata(LLVMContext::MD_memprof, nullptr);
}

static void removeCallsiteMetadata(CallBase *Call) {
  Call->setMetadata(LLVMContext::MD_callsite, nullptr);
}

static void updateMemprofMetadata(CallBase *CI,
                                  const std::vector<Metadata *> &MIBList) {
  assert(!MIBList.empty());
  // Remove existing memprof, which will either be replaced or may not be needed
  // if we are able to use a single allocation type function attribute.
  removeMemProfMetadata(CI);
  CallStackTrie CallStack;
  for (Metadata *MIB : MIBList)
    CallStack.addCallStack(cast<MDNode>(MIB));
  bool MemprofMDAttached = CallStack.buildAndAttachMIBMetadata(CI);
  assert(MemprofMDAttached == CI->hasMetadata(LLVMContext::MD_memprof));
  if (!MemprofMDAttached)
    // If we used a function attribute remove the callsite metadata as well.
    removeCallsiteMetadata(CI);
}

// Update the metadata on the inlined copy ClonedCall of a call OrigCall in the
// inlined callee body, based on the callsite metadata InlinedCallsiteMD from
// the call that was inlined.
static void propagateMemProfHelper(const CallBase *OrigCall,
                                   CallBase *ClonedCall,
                                   MDNode *InlinedCallsiteMD) {
  MDNode *OrigCallsiteMD = ClonedCall->getMetadata(LLVMContext::MD_callsite);
  MDNode *ClonedCallsiteMD = nullptr;
  // Check if the call originally had callsite metadata, and update it for the
  // new call in the inlined body.
  if (OrigCallsiteMD) {
    // The cloned call's context is now the concatenation of the original call's
    // callsite metadata and the callsite metadata on the call where it was
    // inlined.
    ClonedCallsiteMD = MDNode::concatenate(OrigCallsiteMD, InlinedCallsiteMD);
    ClonedCall->setMetadata(LLVMContext::MD_callsite, ClonedCallsiteMD);
  }

  // Update any memprof metadata on the cloned call.
  MDNode *OrigMemProfMD = ClonedCall->getMetadata(LLVMContext::MD_memprof);
  if (!OrigMemProfMD)
    return;
  // We currently expect that allocations with memprof metadata also have
  // callsite metadata for the allocation's part of the context.
  assert(OrigCallsiteMD);

  // New call's MIB list.
  std::vector<Metadata *> NewMIBList;

  // For each MIB metadata, check if its call stack context starts with the
  // new clone's callsite metadata. If so, that MIB goes onto the cloned call in
  // the inlined body. If not, it stays on the out-of-line original call.
  for (auto &MIBOp : OrigMemProfMD->operands()) {
    MDNode *MIB = dyn_cast<MDNode>(MIBOp);
    // Stack is first operand of MIB.
    MDNode *StackMD = getMIBStackNode(MIB);
    assert(StackMD);
    // See if the new cloned callsite context matches this profiled context.
    if (haveCommonPrefix(StackMD, ClonedCallsiteMD))
      // Add it to the cloned call's MIB list.
      NewMIBList.push_back(MIB);
  }
  if (NewMIBList.empty()) {
    removeMemProfMetadata(ClonedCall);
    removeCallsiteMetadata(ClonedCall);
    return;
  }
  if (NewMIBList.size() < OrigMemProfMD->getNumOperands())
    updateMemprofMetadata(ClonedCall, NewMIBList);
}

// Update memprof related metadata (!memprof and !callsite) based on the
// inlining of Callee into the callsite at CB. The updates include merging the
// inlined callee's callsite metadata with that of the inlined call,
// and moving the subset of any memprof contexts to the inlined callee
// allocations if they match the new inlined call stack.
static void
propagateMemProfMetadata(Function *Callee, CallBase &CB,
                         bool ContainsMemProfMetadata,
                         const ValueMap<const Value *, WeakTrackingVH> &VMap) {
  MDNode *CallsiteMD = CB.getMetadata(LLVMContext::MD_callsite);
  // Only need to update if the inlined callsite had callsite metadata, or if
  // there was any memprof metadata inlined.
  if (!CallsiteMD && !ContainsMemProfMetadata)
    return;

  // Propagate metadata onto the cloned calls in the inlined callee.
  for (const auto &Entry : VMap) {
    // See if this is a call that has been inlined and remapped, and not
    // simplified away in the process.
    auto *OrigCall = dyn_cast_or_null<CallBase>(Entry.first);
    auto *ClonedCall = dyn_cast_or_null<CallBase>(Entry.second);
    if (!OrigCall || !ClonedCall)
      continue;
    // If the inlined callsite did not have any callsite metadata, then it isn't
    // involved in any profiled call contexts, and we can remove any memprof
    // metadata on the cloned call.
    if (!CallsiteMD) {
      removeMemProfMetadata(ClonedCall);
      removeCallsiteMetadata(ClonedCall);
      continue;
    }
    propagateMemProfHelper(OrigCall, ClonedCall, CallsiteMD);
  }
}

/// When inlining a call site that has !llvm.mem.parallel_loop_access,
/// !llvm.access.group, !alias.scope or !noalias metadata, that metadata should
/// be propagated to all memory-accessing cloned instructions.
static void PropagateCallSiteMetadata(CallBase &CB, Function::iterator FStart,
                                      Function::iterator FEnd) {
  MDNode *MemParallelLoopAccess =
      CB.getMetadata(LLVMContext::MD_mem_parallel_loop_access);
  MDNode *AccessGroup = CB.getMetadata(LLVMContext::MD_access_group);
  MDNode *AliasScope = CB.getMetadata(LLVMContext::MD_alias_scope);
  MDNode *NoAlias = CB.getMetadata(LLVMContext::MD_noalias);
  if (!MemParallelLoopAccess && !AccessGroup && !AliasScope && !NoAlias)
    return;

  for (BasicBlock &BB : make_range(FStart, FEnd)) {
    for (Instruction &I : BB) {
      // This metadata is only relevant for instructions that access memory.
      if (!I.mayReadOrWriteMemory())
        continue;

      if (MemParallelLoopAccess) {
        // TODO: This probably should not overwrite MemParalleLoopAccess.
        MemParallelLoopAccess = MDNode::concatenate(
            I.getMetadata(LLVMContext::MD_mem_parallel_loop_access),
            MemParallelLoopAccess);
        I.setMetadata(LLVMContext::MD_mem_parallel_loop_access,
                      MemParallelLoopAccess);
      }

      if (AccessGroup)
        I.setMetadata(LLVMContext::MD_access_group, uniteAccessGroups(
            I.getMetadata(LLVMContext::MD_access_group), AccessGroup));

      if (AliasScope)
        I.setMetadata(LLVMContext::MD_alias_scope, MDNode::concatenate(
            I.getMetadata(LLVMContext::MD_alias_scope), AliasScope));

      if (NoAlias)
        I.setMetadata(LLVMContext::MD_noalias, MDNode::concatenate(
            I.getMetadata(LLVMContext::MD_noalias), NoAlias));
    }
  }
}

/// Bundle operands of the inlined function must be added to inlined call sites.
static void PropagateOperandBundles(Function::iterator InlinedBB,
                                    Instruction *CallSiteEHPad) {
  for (Instruction &II : llvm::make_early_inc_range(*InlinedBB)) {
    CallBase *I = dyn_cast<CallBase>(&II);
    if (!I)
      continue;
    // Skip call sites which already have a "funclet" bundle.
    if (I->getOperandBundle(LLVMContext::OB_funclet))
      continue;
    // Skip call sites which are nounwind intrinsics (as long as they don't
    // lower into regular function calls in the course of IR transformations).
    auto *CalledFn =
        dyn_cast<Function>(I->getCalledOperand()->stripPointerCasts());
    if (CalledFn && CalledFn->isIntrinsic() && I->doesNotThrow() &&
        !IntrinsicInst::mayLowerToFunctionCall(CalledFn->getIntrinsicID()))
      continue;

    SmallVector<OperandBundleDef, 1> OpBundles;
    I->getOperandBundlesAsDefs(OpBundles);
    OpBundles.emplace_back("funclet", CallSiteEHPad);

    Instruction *NewInst = CallBase::Create(I, OpBundles, I->getIterator());
    NewInst->takeName(I);
    I->replaceAllUsesWith(NewInst);
    I->eraseFromParent();
  }
}

namespace {
/// Utility for cloning !noalias and !alias.scope metadata. When a code region
/// using scoped alias metadata is inlined, the aliasing relationships may not
/// hold between the two version. It is necessary to create a deep clone of the
/// metadata, putting the two versions in separate scope domains.
class ScopedAliasMetadataDeepCloner {
  using MetadataMap = DenseMap<const MDNode *, TrackingMDNodeRef>;
  SetVector<const MDNode *> MD;
  MetadataMap MDMap;
  void addRecursiveMetadataUses();

public:
  ScopedAliasMetadataDeepCloner(const Function *F);

  /// Create a new clone of the scoped alias metadata, which will be used by
  /// subsequent remap() calls.
  void clone();

  /// Remap instructions in the given range from the original to the cloned
  /// metadata.
  void remap(Function::iterator FStart, Function::iterator FEnd);
};
} // namespace

ScopedAliasMetadataDeepCloner::ScopedAliasMetadataDeepCloner(
    const Function *F) {
  for (const BasicBlock &BB : *F) {
    for (const Instruction &I : BB) {
      if (const MDNode *M = I.getMetadata(LLVMContext::MD_alias_scope))
        MD.insert(M);
      if (const MDNode *M = I.getMetadata(LLVMContext::MD_noalias))
        MD.insert(M);

      // We also need to clone the metadata in noalias intrinsics.
      if (const auto *Decl = dyn_cast<NoAliasScopeDeclInst>(&I))
        MD.insert(Decl->getScopeList());
    }
  }
  addRecursiveMetadataUses();
}

void ScopedAliasMetadataDeepCloner::addRecursiveMetadataUses() {
  SmallVector<const Metadata *, 16> Queue(MD.begin(), MD.end());
  while (!Queue.empty()) {
    const MDNode *M = cast<MDNode>(Queue.pop_back_val());
    for (const Metadata *Op : M->operands())
      if (const MDNode *OpMD = dyn_cast<MDNode>(Op))
        if (MD.insert(OpMD))
          Queue.push_back(OpMD);
  }
}

void ScopedAliasMetadataDeepCloner::clone() {
  assert(MDMap.empty() && "clone() already called ?");

  SmallVector<TempMDTuple, 16> DummyNodes;
  for (const MDNode *I : MD) {
    DummyNodes.push_back(MDTuple::getTemporary(I->getContext(), std::nullopt));
    MDMap[I].reset(DummyNodes.back().get());
  }

  // Create new metadata nodes to replace the dummy nodes, replacing old
  // metadata references with either a dummy node or an already-created new
  // node.
  SmallVector<Metadata *, 4> NewOps;
  for (const MDNode *I : MD) {
    for (const Metadata *Op : I->operands()) {
      if (const MDNode *M = dyn_cast<MDNode>(Op))
        NewOps.push_back(MDMap[M]);
      else
        NewOps.push_back(const_cast<Metadata *>(Op));
    }

    MDNode *NewM = MDNode::get(I->getContext(), NewOps);
    MDTuple *TempM = cast<MDTuple>(MDMap[I]);
    assert(TempM->isTemporary() && "Expected temporary node");

    TempM->replaceAllUsesWith(NewM);
    NewOps.clear();
  }
}

void ScopedAliasMetadataDeepCloner::remap(Function::iterator FStart,
                                          Function::iterator FEnd) {
  if (MDMap.empty())
    return; // Nothing to do.

  for (BasicBlock &BB : make_range(FStart, FEnd)) {
    for (Instruction &I : BB) {
      // TODO: The null checks for the MDMap.lookup() results should no longer
      // be necessary.
      if (MDNode *M = I.getMetadata(LLVMContext::MD_alias_scope))
        if (MDNode *MNew = MDMap.lookup(M))
          I.setMetadata(LLVMContext::MD_alias_scope, MNew);

      if (MDNode *M = I.getMetadata(LLVMContext::MD_noalias))
        if (MDNode *MNew = MDMap.lookup(M))
          I.setMetadata(LLVMContext::MD_noalias, MNew);

      if (auto *Decl = dyn_cast<NoAliasScopeDeclInst>(&I))
        if (MDNode *MNew = MDMap.lookup(Decl->getScopeList()))
          Decl->setScopeList(MNew);
    }
  }
}

/// If the inlined function has noalias arguments,
/// then add new alias scopes for each noalias argument, tag the mapped noalias
/// parameters with noalias metadata specifying the new scope, and tag all
/// non-derived loads, stores and memory intrinsics with the new alias scopes.
static void AddAliasScopeMetadata(CallBase &CB, ValueToValueMapTy &VMap,
                                  const DataLayout &DL, AAResults *CalleeAAR,
                                  ClonedCodeInfo &InlinedFunctionInfo) {
  if (!EnableNoAliasConversion)
    return;

  const Function *CalledFunc = CB.getCalledFunction();
  SmallVector<const Argument *, 4> NoAliasArgs;

  for (const Argument &Arg : CalledFunc->args())
    if (CB.paramHasAttr(Arg.getArgNo(), Attribute::NoAlias) && !Arg.use_empty())
      NoAliasArgs.push_back(&Arg);

  if (NoAliasArgs.empty())
    return;

  // To do a good job, if a noalias variable is captured, we need to know if
  // the capture point dominates the particular use we're considering.
  DominatorTree DT;
  DT.recalculate(const_cast<Function&>(*CalledFunc));

  // noalias indicates that pointer values based on the argument do not alias
  // pointer values which are not based on it. So we add a new "scope" for each
  // noalias function argument. Accesses using pointers based on that argument
  // become part of that alias scope, accesses using pointers not based on that
  // argument are tagged as noalias with that scope.

  DenseMap<const Argument *, MDNode *> NewScopes;
  MDBuilder MDB(CalledFunc->getContext());

  // Create a new scope domain for this function.
  MDNode *NewDomain =
    MDB.createAnonymousAliasScopeDomain(CalledFunc->getName());
  for (unsigned i = 0, e = NoAliasArgs.size(); i != e; ++i) {
    const Argument *A = NoAliasArgs[i];

    std::string Name = std::string(CalledFunc->getName());
    if (A->hasName()) {
      Name += ": %";
      Name += A->getName();
    } else {
      Name += ": argument ";
      Name += utostr(i);
    }

    // Note: We always create a new anonymous root here. This is true regardless
    // of the linkage of the callee because the aliasing "scope" is not just a
    // property of the callee, but also all control dependencies in the caller.
    MDNode *NewScope = MDB.createAnonymousAliasScope(NewDomain, Name);
    NewScopes.insert(std::make_pair(A, NewScope));

    if (UseNoAliasIntrinsic) {
      // Introduce a llvm.experimental.noalias.scope.decl for the noalias
      // argument.
      MDNode *AScopeList = MDNode::get(CalledFunc->getContext(), NewScope);
      auto *NoAliasDecl =
          IRBuilder<>(&CB).CreateNoAliasScopeDeclaration(AScopeList);
      // Ignore the result for now. The result will be used when the
      // llvm.noalias intrinsic is introduced.
      (void)NoAliasDecl;
    }
  }

  // Iterate over all new instructions in the map; for all memory-access
  // instructions, add the alias scope metadata.
  for (ValueToValueMapTy::iterator VMI = VMap.begin(), VMIE = VMap.end();
       VMI != VMIE; ++VMI) {
    if (const Instruction *I = dyn_cast<Instruction>(VMI->first)) {
      if (!VMI->second)
        continue;

      Instruction *NI = dyn_cast<Instruction>(VMI->second);
      if (!NI || InlinedFunctionInfo.isSimplified(I, NI))
        continue;

      bool IsArgMemOnlyCall = false, IsFuncCall = false;
      SmallVector<const Value *, 2> PtrArgs;

      if (const LoadInst *LI = dyn_cast<LoadInst>(I))
        PtrArgs.push_back(LI->getPointerOperand());
      else if (const StoreInst *SI = dyn_cast<StoreInst>(I))
        PtrArgs.push_back(SI->getPointerOperand());
      else if (const VAArgInst *VAAI = dyn_cast<VAArgInst>(I))
        PtrArgs.push_back(VAAI->getPointerOperand());
      else if (const AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(I))
        PtrArgs.push_back(CXI->getPointerOperand());
      else if (const AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(I))
        PtrArgs.push_back(RMWI->getPointerOperand());
      else if (const auto *Call = dyn_cast<CallBase>(I)) {
        // If we know that the call does not access memory, then we'll still
        // know that about the inlined clone of this call site, and we don't
        // need to add metadata.
        if (Call->doesNotAccessMemory())
          continue;

        IsFuncCall = true;
        if (CalleeAAR) {
          MemoryEffects ME = CalleeAAR->getMemoryEffects(Call);

          // We'll retain this knowledge without additional metadata.
          if (ME.onlyAccessesInaccessibleMem())
            continue;

          if (ME.onlyAccessesArgPointees())
            IsArgMemOnlyCall = true;
        }

        for (Value *Arg : Call->args()) {
          // Only care about pointer arguments. If a noalias argument is
          // accessed through a non-pointer argument, it must be captured
          // first (e.g. via ptrtoint), and we protect against captures below.
          if (!Arg->getType()->isPointerTy())
            continue;

          PtrArgs.push_back(Arg);
        }
      }

      // If we found no pointers, then this instruction is not suitable for
      // pairing with an instruction to receive aliasing metadata.
      // However, if this is a call, this we might just alias with none of the
      // noalias arguments.
      if (PtrArgs.empty() && !IsFuncCall)
        continue;

      // It is possible that there is only one underlying object, but you
      // need to go through several PHIs to see it, and thus could be
      // repeated in the Objects list.
      SmallPtrSet<const Value *, 4> ObjSet;
      SmallVector<Metadata *, 4> Scopes, NoAliases;

      for (const Value *V : PtrArgs) {
        SmallVector<const Value *, 4> Objects;
        getUnderlyingObjects(V, Objects, /* LI = */ nullptr);

        for (const Value *O : Objects)
          ObjSet.insert(O);
      }

      // Figure out if we're derived from anything that is not a noalias
      // argument.
      bool RequiresNoCaptureBefore = false, UsesAliasingPtr = false,
           UsesUnknownObject = false;
      for (const Value *V : ObjSet) {
        // Is this value a constant that cannot be derived from any pointer
        // value (we need to exclude constant expressions, for example, that
        // are formed from arithmetic on global symbols).
        bool IsNonPtrConst = isa<ConstantInt>(V) || isa<ConstantFP>(V) ||
                             isa<ConstantPointerNull>(V) ||
                             isa<ConstantDataVector>(V) || isa<UndefValue>(V);
        if (IsNonPtrConst)
          continue;

        // If this is anything other than a noalias argument, then we cannot
        // completely describe the aliasing properties using alias.scope
        // metadata (and, thus, won't add any).
        if (const Argument *A = dyn_cast<Argument>(V)) {
          if (!CB.paramHasAttr(A->getArgNo(), Attribute::NoAlias))
            UsesAliasingPtr = true;
        } else {
          UsesAliasingPtr = true;
        }

        if (isEscapeSource(V)) {
          // An escape source can only alias with a noalias argument if it has
          // been captured beforehand.
          RequiresNoCaptureBefore = true;
        } else if (!isa<Argument>(V) && !isIdentifiedObject(V)) {
          // If this is neither an escape source, nor some identified object
          // (which cannot directly alias a noalias argument), nor some other
          // argument (which, by definition, also cannot alias a noalias
          // argument), conservatively do not make any assumptions.
          UsesUnknownObject = true;
        }
      }

      // Nothing we can do if the used underlying object cannot be reliably
      // determined.
      if (UsesUnknownObject)
        continue;

      // A function call can always get captured noalias pointers (via other
      // parameters, globals, etc.).
      if (IsFuncCall && !IsArgMemOnlyCall)
        RequiresNoCaptureBefore = true;

      // First, we want to figure out all of the sets with which we definitely
      // don't alias. Iterate over all noalias set, and add those for which:
      //   1. The noalias argument is not in the set of objects from which we
      //      definitely derive.
      //   2. The noalias argument has not yet been captured.
      // An arbitrary function that might load pointers could see captured
      // noalias arguments via other noalias arguments or globals, and so we
      // must always check for prior capture.
      for (const Argument *A : NoAliasArgs) {
        if (ObjSet.contains(A))
          continue; // May be based on a noalias argument.

        // It might be tempting to skip the PointerMayBeCapturedBefore check if
        // A->hasNoCaptureAttr() is true, but this is incorrect because
        // nocapture only guarantees that no copies outlive the function, not
        // that the value cannot be locally captured.
        if (!RequiresNoCaptureBefore ||
            !PointerMayBeCapturedBefore(A, /* ReturnCaptures */ false,
                                        /* StoreCaptures */ false, I, &DT))
          NoAliases.push_back(NewScopes[A]);
      }

      if (!NoAliases.empty())
        NI->setMetadata(LLVMContext::MD_noalias,
                        MDNode::concatenate(
                            NI->getMetadata(LLVMContext::MD_noalias),
                            MDNode::get(CalledFunc->getContext(), NoAliases)));

      // Next, we want to figure out all of the sets to which we might belong.
      // We might belong to a set if the noalias argument is in the set of
      // underlying objects. If there is some non-noalias argument in our list
      // of underlying objects, then we cannot add a scope because the fact
      // that some access does not alias with any set of our noalias arguments
      // cannot itself guarantee that it does not alias with this access
      // (because there is some pointer of unknown origin involved and the
      // other access might also depend on this pointer). We also cannot add
      // scopes to arbitrary functions unless we know they don't access any
      // non-parameter pointer-values.
      bool CanAddScopes = !UsesAliasingPtr;
      if (CanAddScopes && IsFuncCall)
        CanAddScopes = IsArgMemOnlyCall;

      if (CanAddScopes)
        for (const Argument *A : NoAliasArgs) {
          if (ObjSet.count(A))
            Scopes.push_back(NewScopes[A]);
        }

      if (!Scopes.empty())
        NI->setMetadata(
            LLVMContext::MD_alias_scope,
            MDNode::concatenate(NI->getMetadata(LLVMContext::MD_alias_scope),
                                MDNode::get(CalledFunc->getContext(), Scopes)));
    }
  }
}

static bool MayContainThrowingOrExitingCallAfterCB(CallBase *Begin,
                                                   ReturnInst *End) {

  assert(Begin->getParent() == End->getParent() &&
         "Expected to be in same basic block!");
  auto BeginIt = Begin->getIterator();
  assert(BeginIt != End->getIterator() && "Non-empty BB has empty iterator");
  return !llvm::isGuaranteedToTransferExecutionToSuccessor(
      ++BeginIt, End->getIterator(), InlinerAttributeWindow + 1);
}

// Add attributes from CB params and Fn attributes that can always be propagated
// to the corresponding argument / inner callbases.
static void AddParamAndFnBasicAttributes(const CallBase &CB,
                                         ValueToValueMapTy &VMap,
                                         ClonedCodeInfo &InlinedFunctionInfo) {
  auto *CalledFunction = CB.getCalledFunction();
  auto &Context = CalledFunction->getContext();

  // Collect valid attributes for all params.
  SmallVector<AttrBuilder> ValidParamAttrs;
  bool HasAttrToPropagate = false;

  for (unsigned I = 0, E = CB.arg_size(); I < E; ++I) {
    ValidParamAttrs.emplace_back(AttrBuilder{CB.getContext()});
    // Access attributes can be propagated to any param with the same underlying
    // object as the argument.
    if (CB.paramHasAttr(I, Attribute::ReadNone))
      ValidParamAttrs.back().addAttribute(Attribute::ReadNone);
    if (CB.paramHasAttr(I, Attribute::ReadOnly))
      ValidParamAttrs.back().addAttribute(Attribute::ReadOnly);
    HasAttrToPropagate |= ValidParamAttrs.back().hasAttributes();
  }

  // Won't be able to propagate anything.
  if (!HasAttrToPropagate)
    return;

  for (BasicBlock &BB : *CalledFunction) {
    for (Instruction &Ins : BB) {
      const auto *InnerCB = dyn_cast<CallBase>(&Ins);
      if (!InnerCB)
        continue;
      auto *NewInnerCB = dyn_cast_or_null<CallBase>(VMap.lookup(InnerCB));
      if (!NewInnerCB)
        continue;
      // The InnerCB might have be simplified during the inlining
      // process which can make propagation incorrect.
      if (InlinedFunctionInfo.isSimplified(InnerCB, NewInnerCB))
        continue;

      AttributeList AL = NewInnerCB->getAttributes();
      for (unsigned I = 0, E = InnerCB->arg_size(); I < E; ++I) {
        // Check if the underlying value for the parameter is an argument.
        const Value *UnderlyingV =
            getUnderlyingObject(InnerCB->getArgOperand(I));
        const Argument *Arg = dyn_cast<Argument>(UnderlyingV);
        if (!Arg)
          continue;

        if (NewInnerCB->paramHasAttr(I, Attribute::ByVal))
          // It's unsound to propagate memory attributes to byval arguments.
          // Even if CalledFunction doesn't e.g. write to the argument,
          // the call to NewInnerCB may write to its by-value copy.
          continue;

        unsigned ArgNo = Arg->getArgNo();
        // If so, propagate its access attributes.
        AL = AL.addParamAttributes(Context, I, ValidParamAttrs[ArgNo]);
        // We can have conflicting attributes from the inner callsite and
        // to-be-inlined callsite. In that case, choose the most
        // restrictive.

        // readonly + writeonly means we can never deref so make readnone.
        if (AL.hasParamAttr(I, Attribute::ReadOnly) &&
            AL.hasParamAttr(I, Attribute::WriteOnly))
          AL = AL.addParamAttribute(Context, I, Attribute::ReadNone);

        // If have readnone, need to clear readonly/writeonly
        if (AL.hasParamAttr(I, Attribute::ReadNone)) {
          AL = AL.removeParamAttribute(Context, I, Attribute::ReadOnly);
          AL = AL.removeParamAttribute(Context, I, Attribute::WriteOnly);
        }

        // Writable cannot exist in conjunction w/ readonly/readnone
        if (AL.hasParamAttr(I, Attribute::ReadOnly) ||
            AL.hasParamAttr(I, Attribute::ReadNone))
          AL = AL.removeParamAttribute(Context, I, Attribute::Writable);
      }
      NewInnerCB->setAttributes(AL);
    }
  }
}

// Only allow these white listed attributes to be propagated back to the
// callee. This is because other attributes may only be valid on the call
// itself, i.e. attributes such as signext and zeroext.

// Attributes that are always okay to propagate as if they are violated its
// immediate UB.
static AttrBuilder IdentifyValidUBGeneratingAttributes(CallBase &CB) {
  AttrBuilder Valid(CB.getContext());
  if (auto DerefBytes = CB.getRetDereferenceableBytes())
    Valid.addDereferenceableAttr(DerefBytes);
  if (auto DerefOrNullBytes = CB.getRetDereferenceableOrNullBytes())
    Valid.addDereferenceableOrNullAttr(DerefOrNullBytes);
  if (CB.hasRetAttr(Attribute::NoAlias))
    Valid.addAttribute(Attribute::NoAlias);
  if (CB.hasRetAttr(Attribute::NoUndef))
    Valid.addAttribute(Attribute::NoUndef);
  return Valid;
}

// Attributes that need additional checks as propagating them may change
// behavior or cause new UB.
static AttrBuilder IdentifyValidPoisonGeneratingAttributes(CallBase &CB) {
  AttrBuilder Valid(CB.getContext());
  if (CB.hasRetAttr(Attribute::NonNull))
    Valid.addAttribute(Attribute::NonNull);
  if (CB.hasRetAttr(Attribute::Alignment))
    Valid.addAlignmentAttr(CB.getRetAlign());
  if (std::optional<ConstantRange> Range = CB.getRange())
    Valid.addRangeAttr(*Range);
  return Valid;
}

static void AddReturnAttributes(CallBase &CB, ValueToValueMapTy &VMap,
                                ClonedCodeInfo &InlinedFunctionInfo) {
  AttrBuilder ValidUB = IdentifyValidUBGeneratingAttributes(CB);
  AttrBuilder ValidPG = IdentifyValidPoisonGeneratingAttributes(CB);
  if (!ValidUB.hasAttributes() && !ValidPG.hasAttributes())
    return;
  auto *CalledFunction = CB.getCalledFunction();
  auto &Context = CalledFunction->getContext();

  for (auto &BB : *CalledFunction) {
    auto *RI = dyn_cast<ReturnInst>(BB.getTerminator());
    if (!RI || !isa<CallBase>(RI->getOperand(0)))
      continue;
    auto *RetVal = cast<CallBase>(RI->getOperand(0));
    // Check that the cloned RetVal exists and is a call, otherwise we cannot
    // add the attributes on the cloned RetVal. Simplification during inlining
    // could have transformed the cloned instruction.
    auto *NewRetVal = dyn_cast_or_null<CallBase>(VMap.lookup(RetVal));
    if (!NewRetVal)
      continue;

    // The RetVal might have be simplified during the inlining
    // process which can make propagation incorrect.
    if (InlinedFunctionInfo.isSimplified(RetVal, NewRetVal))
      continue;
    // Backward propagation of attributes to the returned value may be incorrect
    // if it is control flow dependent.
    // Consider:
    // @callee {
    //  %rv = call @foo()
    //  %rv2 = call @bar()
    //  if (%rv2 != null)
    //    return %rv2
    //  if (%rv == null)
    //    exit()
    //  return %rv
    // }
    // caller() {
    //   %val = call nonnull @callee()
    // }
    // Here we cannot add the nonnull attribute on either foo or bar. So, we
    // limit the check to both RetVal and RI are in the same basic block and
    // there are no throwing/exiting instructions between these instructions.
    if (RI->getParent() != RetVal->getParent() ||
        MayContainThrowingOrExitingCallAfterCB(RetVal, RI))
      continue;
    // Add to the existing attributes of NewRetVal, i.e. the cloned call
    // instruction.
    // NB! When we have the same attribute already existing on NewRetVal, but
    // with a differing value, the AttributeList's merge API honours the already
    // existing attribute value (i.e. attributes such as dereferenceable,
    // dereferenceable_or_null etc). See AttrBuilder::merge for more details.
    AttributeList AL = NewRetVal->getAttributes();
    if (ValidUB.getDereferenceableBytes() < AL.getRetDereferenceableBytes())
      ValidUB.removeAttribute(Attribute::Dereferenceable);
    if (ValidUB.getDereferenceableOrNullBytes() <
        AL.getRetDereferenceableOrNullBytes())
      ValidUB.removeAttribute(Attribute::DereferenceableOrNull);
    AttributeList NewAL = AL.addRetAttributes(Context, ValidUB);
    // Attributes that may generate poison returns are a bit tricky. If we
    // propagate them, other uses of the callsite might have their behavior
    // change or cause UB (if they have noundef) b.c of the new potential
    // poison.
    // Take the following three cases:
    //
    // 1)
    // define nonnull ptr @foo() {
    //   %p = call ptr @bar()
    //   call void @use(ptr %p) willreturn nounwind
    //   ret ptr %p
    // }
    //
    // 2)
    // define noundef nonnull ptr @foo() {
    //   %p = call ptr @bar()
    //   call void @use(ptr %p) willreturn nounwind
    //   ret ptr %p
    // }
    //
    // 3)
    // define nonnull ptr @foo() {
    //   %p = call noundef ptr @bar()
    //   ret ptr %p
    // }
    //
    // In case 1, we can't propagate nonnull because poison value in @use may
    // change behavior or trigger UB.
    // In case 2, we don't need to be concerned about propagating nonnull, as
    // any new poison at @use will trigger UB anyways.
    // In case 3, we can never propagate nonnull because it may create UB due to
    // the noundef on @bar.
    if (ValidPG.getAlignment().valueOrOne() < AL.getRetAlignment().valueOrOne())
      ValidPG.removeAttribute(Attribute::Alignment);
    if (ValidPG.hasAttributes()) {
      Attribute CBRange = ValidPG.getAttribute(Attribute::Range);
      if (CBRange.isValid()) {
        Attribute NewRange = AL.getRetAttr(Attribute::Range);
        if (NewRange.isValid()) {
          ValidPG.addRangeAttr(
              CBRange.getRange().intersectWith(NewRange.getRange()));
        }
      }
      // Three checks.
      // If the callsite has `noundef`, then a poison due to violating the
      // return attribute will create UB anyways so we can always propagate.
      // Otherwise, if the return value (callee to be inlined) has `noundef`, we
      // can't propagate as a new poison return will cause UB.
      // Finally, check if the return value has no uses whose behavior may
      // change/may cause UB if we potentially return poison. At the moment this
      // is implemented overly conservatively with a single-use check.
      // TODO: Update the single-use check to iterate through uses and only bail
      // if we have a potentially dangerous use.

      if (CB.hasRetAttr(Attribute::NoUndef) ||
          (RetVal->hasOneUse() && !RetVal->hasRetAttr(Attribute::NoUndef)))
        NewAL = NewAL.addRetAttributes(Context, ValidPG);
    }
    NewRetVal->setAttributes(NewAL);
  }
}

/// If the inlined function has non-byval align arguments, then
/// add @llvm.assume-based alignment assumptions to preserve this information.
static void AddAlignmentAssumptions(CallBase &CB, InlineFunctionInfo &IFI) {
  if (!PreserveAlignmentAssumptions || !IFI.GetAssumptionCache)
    return;

  AssumptionCache *AC = &IFI.GetAssumptionCache(*CB.getCaller());
  auto &DL = CB.getDataLayout();

  // To avoid inserting redundant assumptions, we should check for assumptions
  // already in the caller. To do this, we might need a DT of the caller.
  DominatorTree DT;
  bool DTCalculated = false;

  Function *CalledFunc = CB.getCalledFunction();
  for (Argument &Arg : CalledFunc->args()) {
    if (!Arg.getType()->isPointerTy() || Arg.hasPassPointeeByValueCopyAttr() ||
        Arg.hasNUses(0))
      continue;
    MaybeAlign Alignment = Arg.getParamAlign();
    if (!Alignment)
      continue;

    if (!DTCalculated) {
      DT.recalculate(*CB.getCaller());
      DTCalculated = true;
    }
    // If we can already prove the asserted alignment in the context of the
    // caller, then don't bother inserting the assumption.
    Value *ArgVal = CB.getArgOperand(Arg.getArgNo());
    if (getKnownAlignment(ArgVal, DL, &CB, AC, &DT) >= *Alignment)
      continue;

    CallInst *NewAsmp = IRBuilder<>(&CB).CreateAlignmentAssumption(
        DL, ArgVal, Alignment->value());
    AC->registerAssumption(cast<AssumeInst>(NewAsmp));
  }
}

static void HandleByValArgumentInit(Type *ByValType, Value *Dst, Value *Src,
                                    Module *M, BasicBlock *InsertBlock,
                                    InlineFunctionInfo &IFI,
                                    Function *CalledFunc) {
  IRBuilder<> Builder(InsertBlock, InsertBlock->begin());

  Value *Size =
      Builder.getInt64(M->getDataLayout().getTypeStoreSize(ByValType));

  // Always generate a memcpy of alignment 1 here because we don't know
  // the alignment of the src pointer.  Other optimizations can infer
  // better alignment.
  CallInst *CI = Builder.CreateMemCpy(Dst, /*DstAlign*/ Align(1), Src,
                                      /*SrcAlign*/ Align(1), Size);

  // The verifier requires that all calls of debug-info-bearing functions
  // from debug-info-bearing functions have a debug location (for inlining
  // purposes). Assign a dummy location to satisfy the constraint.
  if (!CI->getDebugLoc() && InsertBlock->getParent()->getSubprogram())
    if (DISubprogram *SP = CalledFunc->getSubprogram())
      CI->setDebugLoc(DILocation::get(SP->getContext(), 0, 0, SP));
}

/// When inlining a call site that has a byval argument,
/// we have to make the implicit memcpy explicit by adding it.
static Value *HandleByValArgument(Type *ByValType, Value *Arg,
                                  Instruction *TheCall,
                                  const Function *CalledFunc,
                                  InlineFunctionInfo &IFI,
                                  MaybeAlign ByValAlignment) {
  Function *Caller = TheCall->getFunction();
  const DataLayout &DL = Caller->getDataLayout();

  // If the called function is readonly, then it could not mutate the caller's
  // copy of the byval'd memory.  In this case, it is safe to elide the copy and
  // temporary.
  if (CalledFunc->onlyReadsMemory()) {
    // If the byval argument has a specified alignment that is greater than the
    // passed in pointer, then we either have to round up the input pointer or
    // give up on this transformation.
    if (ByValAlignment.valueOrOne() == 1)
      return Arg;

    AssumptionCache *AC =
        IFI.GetAssumptionCache ? &IFI.GetAssumptionCache(*Caller) : nullptr;

    // If the pointer is already known to be sufficiently aligned, or if we can
    // round it up to a larger alignment, then we don't need a temporary.
    if (getOrEnforceKnownAlignment(Arg, *ByValAlignment, DL, TheCall, AC) >=
        *ByValAlignment)
      return Arg;

    // Otherwise, we have to make a memcpy to get a safe alignment.  This is bad
    // for code quality, but rarely happens and is required for correctness.
  }

  // Create the alloca.  If we have DataLayout, use nice alignment.
  Align Alignment = DL.getPrefTypeAlign(ByValType);

  // If the byval had an alignment specified, we *must* use at least that
  // alignment, as it is required by the byval argument (and uses of the
  // pointer inside the callee).
  if (ByValAlignment)
    Alignment = std::max(Alignment, *ByValAlignment);

  AllocaInst *NewAlloca =
      new AllocaInst(ByValType, Arg->getType()->getPointerAddressSpace(),
                     nullptr, Alignment, Arg->getName());
  NewAlloca->insertBefore(Caller->begin()->begin());
  IFI.StaticAllocas.push_back(NewAlloca);

  // Uses of the argument in the function should use our new alloca
  // instead.
  return NewAlloca;
}

// Check whether this Value is used by a lifetime intrinsic.
static bool isUsedByLifetimeMarker(Value *V) {
  for (User *U : V->users())
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(U))
      if (II->isLifetimeStartOrEnd())
        return true;
  return false;
}

// Check whether the given alloca already has
// lifetime.start or lifetime.end intrinsics.
static bool hasLifetimeMarkers(AllocaInst *AI) {
  Type *Ty = AI->getType();
  Type *Int8PtrTy =
      PointerType::get(Ty->getContext(), Ty->getPointerAddressSpace());
  if (Ty == Int8PtrTy)
    return isUsedByLifetimeMarker(AI);

  // Do a scan to find all the casts to i8*.
  for (User *U : AI->users()) {
    if (U->getType() != Int8PtrTy) continue;
    if (U->stripPointerCasts() != AI) continue;
    if (isUsedByLifetimeMarker(U))
      return true;
  }
  return false;
}

/// Return the result of AI->isStaticAlloca() if AI were moved to the entry
/// block. Allocas used in inalloca calls and allocas of dynamic array size
/// cannot be static.
static bool allocaWouldBeStaticInEntry(const AllocaInst *AI ) {
  return isa<Constant>(AI->getArraySize()) && !AI->isUsedWithInAlloca();
}

/// Returns a DebugLoc for a new DILocation which is a clone of \p OrigDL
/// inlined at \p InlinedAt. \p IANodes is an inlined-at cache.
static DebugLoc inlineDebugLoc(DebugLoc OrigDL, DILocation *InlinedAt,
                               LLVMContext &Ctx,
                               DenseMap<const MDNode *, MDNode *> &IANodes) {
  auto IA = DebugLoc::appendInlinedAt(OrigDL, InlinedAt, Ctx, IANodes);
  return DILocation::get(Ctx, OrigDL.getLine(), OrigDL.getCol(),
                         OrigDL.getScope(), IA);
}

/// Update inlined instructions' line numbers to
/// to encode location where these instructions are inlined.
static void fixupLineNumbers(Function *Fn, Function::iterator FI,
                             Instruction *TheCall, bool CalleeHasDebugInfo) {
  const DebugLoc &TheCallDL = TheCall->getDebugLoc();
  if (!TheCallDL)
    return;

  auto &Ctx = Fn->getContext();
  DILocation *InlinedAtNode = TheCallDL;

  // Create a unique call site, not to be confused with any other call from the
  // same location.
  InlinedAtNode = DILocation::getDistinct(
      Ctx, InlinedAtNode->getLine(), InlinedAtNode->getColumn(),
      InlinedAtNode->getScope(), InlinedAtNode->getInlinedAt());

  // Cache the inlined-at nodes as they're built so they are reused, without
  // this every instruction's inlined-at chain would become distinct from each
  // other.
  DenseMap<const MDNode *, MDNode *> IANodes;

  // Check if we are not generating inline line tables and want to use
  // the call site location instead.
  bool NoInlineLineTables = Fn->hasFnAttribute("no-inline-line-tables");

  // Helper-util for updating the metadata attached to an instruction.
  auto UpdateInst = [&](Instruction &I) {
    // Loop metadata needs to be updated so that the start and end locs
    // reference inlined-at locations.
    auto updateLoopInfoLoc = [&Ctx, &InlinedAtNode,
                              &IANodes](Metadata *MD) -> Metadata * {
      if (auto *Loc = dyn_cast_or_null<DILocation>(MD))
        return inlineDebugLoc(Loc, InlinedAtNode, Ctx, IANodes).get();
      return MD;
    };
    updateLoopMetadataDebugLocations(I, updateLoopInfoLoc);

    if (!NoInlineLineTables)
      if (DebugLoc DL = I.getDebugLoc()) {
        DebugLoc IDL =
            inlineDebugLoc(DL, InlinedAtNode, I.getContext(), IANodes);
        I.setDebugLoc(IDL);
        return;
      }

    if (CalleeHasDebugInfo && !NoInlineLineTables)
      return;

    // If the inlined instruction has no line number, or if inline info
    // is not being generated, make it look as if it originates from the call
    // location. This is important for ((__always_inline, __nodebug__))
    // functions which must use caller location for all instructions in their
    // function body.

    // Don't update static allocas, as they may get moved later.
    if (auto *AI = dyn_cast<AllocaInst>(&I))
      if (allocaWouldBeStaticInEntry(AI))
        return;

    // Do not force a debug loc for pseudo probes, since they do not need to
    // be debuggable, and also they are expected to have a zero/null dwarf
    // discriminator at this point which could be violated otherwise.
    if (isa<PseudoProbeInst>(I))
      return;

    I.setDebugLoc(TheCallDL);
  };

  // Helper-util for updating debug-info records attached to instructions.
  auto UpdateDVR = [&](DbgRecord *DVR) {
    assert(DVR->getDebugLoc() && "Debug Value must have debug loc");
    if (NoInlineLineTables) {
      DVR->setDebugLoc(TheCallDL);
      return;
    }
    DebugLoc DL = DVR->getDebugLoc();
    DebugLoc IDL =
        inlineDebugLoc(DL, InlinedAtNode,
                       DVR->getMarker()->getParent()->getContext(), IANodes);
    DVR->setDebugLoc(IDL);
  };

  // Iterate over all instructions, updating metadata and debug-info records.
  for (; FI != Fn->end(); ++FI) {
    for (Instruction &I : *FI) {
      UpdateInst(I);
      for (DbgRecord &DVR : I.getDbgRecordRange()) {
        UpdateDVR(&DVR);
      }
    }

    // Remove debug info intrinsics if we're not keeping inline info.
    if (NoInlineLineTables) {
      BasicBlock::iterator BI = FI->begin();
      while (BI != FI->end()) {
        if (isa<DbgInfoIntrinsic>(BI)) {
          BI = BI->eraseFromParent();
          continue;
        } else {
          BI->dropDbgRecords();
        }
        ++BI;
      }
    }
  }
}

#undef DEBUG_TYPE
#define DEBUG_TYPE "assignment-tracking"
/// Find Alloca and linked DbgAssignIntrinsic for locals escaped by \p CB.
static at::StorageToVarsMap collectEscapedLocals(const DataLayout &DL,
                                                 const CallBase &CB) {
  at::StorageToVarsMap EscapedLocals;
  SmallPtrSet<const Value *, 4> SeenBases;

  LLVM_DEBUG(
      errs() << "# Finding caller local variables escaped by callee\n");
  for (const Value *Arg : CB.args()) {
    LLVM_DEBUG(errs() << "INSPECT: " << *Arg << "\n");
    if (!Arg->getType()->isPointerTy()) {
      LLVM_DEBUG(errs() << " | SKIP: Not a pointer\n");
      continue;
    }

    const Instruction *I = dyn_cast<Instruction>(Arg);
    if (!I) {
      LLVM_DEBUG(errs() << " | SKIP: Not result of instruction\n");
      continue;
    }

    // Walk back to the base storage.
    assert(Arg->getType()->isPtrOrPtrVectorTy());
    APInt TmpOffset(DL.getIndexTypeSizeInBits(Arg->getType()), 0, false);
    const AllocaInst *Base = dyn_cast<AllocaInst>(
        Arg->stripAndAccumulateConstantOffsets(DL, TmpOffset, true));
    if (!Base) {
      LLVM_DEBUG(errs() << " | SKIP: Couldn't walk back to base storage\n");
      continue;
    }

    assert(Base);
    LLVM_DEBUG(errs() << " | BASE: " << *Base << "\n");
    // We only need to process each base address once - skip any duplicates.
    if (!SeenBases.insert(Base).second)
      continue;

    // Find all local variables associated with the backing storage.
    auto CollectAssignsForStorage = [&](auto *DbgAssign) {
      // Skip variables from inlined functions - they are not local variables.
      if (DbgAssign->getDebugLoc().getInlinedAt())
        return;
      LLVM_DEBUG(errs() << " > DEF : " << *DbgAssign << "\n");
      EscapedLocals[Base].insert(at::VarRecord(DbgAssign));
    };
    for_each(at::getAssignmentMarkers(Base), CollectAssignsForStorage);
    for_each(at::getDVRAssignmentMarkers(Base), CollectAssignsForStorage);
  }
  return EscapedLocals;
}

static void trackInlinedStores(Function::iterator Start, Function::iterator End,
                               const CallBase &CB) {
  LLVM_DEBUG(errs() << "trackInlinedStores into "
                    << Start->getParent()->getName() << " from "
                    << CB.getCalledFunction()->getName() << "\n");
  std::unique_ptr<DataLayout> DL = std::make_unique<DataLayout>(CB.getModule());
  at::trackAssignments(Start, End, collectEscapedLocals(*DL, CB), *DL);
}

/// Update inlined instructions' DIAssignID metadata. We need to do this
/// otherwise a function inlined more than once into the same function
/// will cause DIAssignID to be shared by many instructions.
static void fixupAssignments(Function::iterator Start, Function::iterator End) {
  DenseMap<DIAssignID *, DIAssignID *> Map;
  // Loop over all the inlined instructions. If we find a DIAssignID
  // attachment or use, replace it with a new version.
  for (auto BBI = Start; BBI != End; ++BBI) {
    for (Instruction &I : *BBI)
      at::remapAssignID(Map, I);
  }
}
#undef DEBUG_TYPE
#define DEBUG_TYPE "inline-function"

/// Update the block frequencies of the caller after a callee has been inlined.
///
/// Each block cloned into the caller has its block frequency scaled by the
/// ratio of CallSiteFreq/CalleeEntryFreq. This ensures that the cloned copy of
/// callee's entry block gets the same frequency as the callsite block and the
/// relative frequencies of all cloned blocks remain the same after cloning.
static void updateCallerBFI(BasicBlock *CallSiteBlock,
                            const ValueToValueMapTy &VMap,
                            BlockFrequencyInfo *CallerBFI,
                            BlockFrequencyInfo *CalleeBFI,
                            const BasicBlock &CalleeEntryBlock) {
  SmallPtrSet<BasicBlock *, 16> ClonedBBs;
  for (auto Entry : VMap) {
    if (!isa<BasicBlock>(Entry.first) || !Entry.second)
      continue;
    auto *OrigBB = cast<BasicBlock>(Entry.first);
    auto *ClonedBB = cast<BasicBlock>(Entry.second);
    BlockFrequency Freq = CalleeBFI->getBlockFreq(OrigBB);
    if (!ClonedBBs.insert(ClonedBB).second) {
      // Multiple blocks in the callee might get mapped to one cloned block in
      // the caller since we prune the callee as we clone it. When that happens,
      // we want to use the maximum among the original blocks' frequencies.
      BlockFrequency NewFreq = CallerBFI->getBlockFreq(ClonedBB);
      if (NewFreq > Freq)
        Freq = NewFreq;
    }
    CallerBFI->setBlockFreq(ClonedBB, Freq);
  }
  BasicBlock *EntryClone = cast<BasicBlock>(VMap.lookup(&CalleeEntryBlock));
  CallerBFI->setBlockFreqAndScale(
      EntryClone, CallerBFI->getBlockFreq(CallSiteBlock), ClonedBBs);
}

/// Update the branch metadata for cloned call instructions.
static void updateCallProfile(Function *Callee, const ValueToValueMapTy &VMap,
                              const ProfileCount &CalleeEntryCount,
                              const CallBase &TheCall, ProfileSummaryInfo *PSI,
                              BlockFrequencyInfo *CallerBFI) {
  if (CalleeEntryCount.isSynthetic() || CalleeEntryCount.getCount() < 1)
    return;
  auto CallSiteCount =
      PSI ? PSI->getProfileCount(TheCall, CallerBFI) : std::nullopt;
  int64_t CallCount =
      std::min(CallSiteCount.value_or(0), CalleeEntryCount.getCount());
  updateProfileCallee(Callee, -CallCount, &VMap);
}

void llvm::updateProfileCallee(
    Function *Callee, int64_t EntryDelta,
    const ValueMap<const Value *, WeakTrackingVH> *VMap) {
  auto CalleeCount = Callee->getEntryCount();
  if (!CalleeCount)
    return;

  const uint64_t PriorEntryCount = CalleeCount->getCount();

  // Since CallSiteCount is an estimate, it could exceed the original callee
  // count and has to be set to 0 so guard against underflow.
  const uint64_t NewEntryCount =
      (EntryDelta < 0 && static_cast<uint64_t>(-EntryDelta) > PriorEntryCount)
          ? 0
          : PriorEntryCount + EntryDelta;

  auto updateVTableProfWeight = [](CallBase *CB, const uint64_t NewEntryCount,
                                   const uint64_t PriorEntryCount) {
    Instruction *VPtr = PGOIndirectCallVisitor::tryGetVTableInstruction(CB);
    if (VPtr)
      scaleProfData(*VPtr, NewEntryCount, PriorEntryCount);
  };

  // During inlining ?
  if (VMap) {
    uint64_t CloneEntryCount = PriorEntryCount - NewEntryCount;
    for (auto Entry : *VMap) {
      if (isa<CallInst>(Entry.first))
        if (auto *CI = dyn_cast_or_null<CallInst>(Entry.second)) {
          CI->updateProfWeight(CloneEntryCount, PriorEntryCount);
          updateVTableProfWeight(CI, CloneEntryCount, PriorEntryCount);
        }

      if (isa<InvokeInst>(Entry.first))
        if (auto *II = dyn_cast_or_null<InvokeInst>(Entry.second)) {
          II->updateProfWeight(CloneEntryCount, PriorEntryCount);
          updateVTableProfWeight(II, CloneEntryCount, PriorEntryCount);
        }
    }
  }

  if (EntryDelta) {
    Callee->setEntryCount(NewEntryCount);

    for (BasicBlock &BB : *Callee)
      // No need to update the callsite if it is pruned during inlining.
      if (!VMap || VMap->count(&BB))
        for (Instruction &I : BB) {
          if (CallInst *CI = dyn_cast<CallInst>(&I)) {
            CI->updateProfWeight(NewEntryCount, PriorEntryCount);
            updateVTableProfWeight(CI, NewEntryCount, PriorEntryCount);
          }
          if (InvokeInst *II = dyn_cast<InvokeInst>(&I)) {
            II->updateProfWeight(NewEntryCount, PriorEntryCount);
            updateVTableProfWeight(II, NewEntryCount, PriorEntryCount);
          }
        }
  }
}

/// An operand bundle "clang.arc.attachedcall" on a call indicates the call
/// result is implicitly consumed by a call to retainRV or claimRV immediately
/// after the call. This function inlines the retainRV/claimRV calls.
///
/// There are three cases to consider:
///
/// 1. If there is a call to autoreleaseRV that takes a pointer to the returned
///    object in the callee return block, the autoreleaseRV call and the
///    retainRV/claimRV call in the caller cancel out. If the call in the caller
///    is a claimRV call, a call to objc_release is emitted.
///
/// 2. If there is a call in the callee return block that doesn't have operand
///    bundle "clang.arc.attachedcall", the operand bundle on the original call
///    is transferred to the call in the callee.
///
/// 3. Otherwise, a call to objc_retain is inserted if the call in the caller is
///    a retainRV call.
static void
inlineRetainOrClaimRVCalls(CallBase &CB, objcarc::ARCInstKind RVCallKind,
                           const SmallVectorImpl<ReturnInst *> &Returns) {
  Module *Mod = CB.getModule();
  assert(objcarc::isRetainOrClaimRV(RVCallKind) && "unexpected ARC function");
  bool IsRetainRV = RVCallKind == objcarc::ARCInstKind::RetainRV,
       IsUnsafeClaimRV = !IsRetainRV;

  for (auto *RI : Returns) {
    Value *RetOpnd = objcarc::GetRCIdentityRoot(RI->getOperand(0));
    bool InsertRetainCall = IsRetainRV;
    IRBuilder<> Builder(RI->getContext());

    // Walk backwards through the basic block looking for either a matching
    // autoreleaseRV call or an unannotated call.
    auto InstRange = llvm::make_range(++(RI->getIterator().getReverse()),
                                      RI->getParent()->rend());
    for (Instruction &I : llvm::make_early_inc_range(InstRange)) {
      // Ignore casts.
      if (isa<CastInst>(I))
        continue;

      if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
        if (II->getIntrinsicID() != Intrinsic::objc_autoreleaseReturnValue ||
            !II->hasNUses(0) ||
            objcarc::GetRCIdentityRoot(II->getOperand(0)) != RetOpnd)
          break;

        // If we've found a matching authoreleaseRV call:
        // - If claimRV is attached to the call, insert a call to objc_release
        //   and erase the autoreleaseRV call.
        // - If retainRV is attached to the call, just erase the autoreleaseRV
        //   call.
        if (IsUnsafeClaimRV) {
          Builder.SetInsertPoint(II);
          Function *IFn =
              Intrinsic::getDeclaration(Mod, Intrinsic::objc_release);
          Builder.CreateCall(IFn, RetOpnd, "");
        }
        II->eraseFromParent();
        InsertRetainCall = false;
        break;
      }

      auto *CI = dyn_cast<CallInst>(&I);

      if (!CI)
        break;

      if (objcarc::GetRCIdentityRoot(CI) != RetOpnd ||
          objcarc::hasAttachedCallOpBundle(CI))
        break;

      // If we've found an unannotated call that defines RetOpnd, add a
      // "clang.arc.attachedcall" operand bundle.
      Value *BundleArgs[] = {*objcarc::getAttachedARCFunction(&CB)};
      OperandBundleDef OB("clang.arc.attachedcall", BundleArgs);
      auto *NewCall = CallBase::addOperandBundle(
          CI, LLVMContext::OB_clang_arc_attachedcall, OB, CI->getIterator());
      NewCall->copyMetadata(*CI);
      CI->replaceAllUsesWith(NewCall);
      CI->eraseFromParent();
      InsertRetainCall = false;
      break;
    }

    if (InsertRetainCall) {
      // The retainRV is attached to the call and we've failed to find a
      // matching autoreleaseRV or an annotated call in the callee. Emit a call
      // to objc_retain.
      Builder.SetInsertPoint(RI);
      Function *IFn = Intrinsic::getDeclaration(Mod, Intrinsic::objc_retain);
      Builder.CreateCall(IFn, RetOpnd, "");
    }
  }
}

/// This function inlines the called function into the basic block of the
/// caller. This returns false if it is not possible to inline this call.
/// The program is still in a well defined state if this occurs though.
///
/// Note that this only does one level of inlining.  For example, if the
/// instruction 'call B' is inlined, and 'B' calls 'C', then the call to 'C' now
/// exists in the instruction stream.  Similarly this will inline a recursive
/// function by one level.
llvm::InlineResult llvm::InlineFunction(CallBase &CB, InlineFunctionInfo &IFI,
                                        bool MergeAttributes,
                                        AAResults *CalleeAAR,
                                        bool InsertLifetime,
                                        Function *ForwardVarArgsTo) {
  assert(CB.getParent() && CB.getFunction() && "Instruction not in function!");

  // FIXME: we don't inline callbr yet.
  if (isa<CallBrInst>(CB))
    return InlineResult::failure("We don't inline callbr yet.");

  // If IFI has any state in it, zap it before we fill it in.
  IFI.reset();

  Function *CalledFunc = CB.getCalledFunction();
  if (!CalledFunc ||               // Can't inline external function or indirect
      CalledFunc->isDeclaration()) // call!
    return InlineResult::failure("external or indirect");

  // The inliner does not know how to inline through calls with operand bundles
  // in general ...
  Value *ConvergenceControlToken = nullptr;
  if (CB.hasOperandBundles()) {
    for (int i = 0, e = CB.getNumOperandBundles(); i != e; ++i) {
      auto OBUse = CB.getOperandBundleAt(i);
      uint32_t Tag = OBUse.getTagID();
      // ... but it knows how to inline through "deopt" operand bundles ...
      if (Tag == LLVMContext::OB_deopt)
        continue;
      // ... and "funclet" operand bundles.
      if (Tag == LLVMContext::OB_funclet)
        continue;
      if (Tag == LLVMContext::OB_clang_arc_attachedcall)
        continue;
      if (Tag == LLVMContext::OB_kcfi)
        continue;
      if (Tag == LLVMContext::OB_convergencectrl) {
        ConvergenceControlToken = OBUse.Inputs[0].get();
        continue;
      }

      return InlineResult::failure("unsupported operand bundle");
    }
  }

  // FIXME: The check below is redundant and incomplete. According to spec, if a
  // convergent call is missing a token, then the caller is using uncontrolled
  // convergence. If the callee has an entry intrinsic, then the callee is using
  // controlled convergence, and the call cannot be inlined. A proper
  // implemenation of this check requires a whole new analysis that identifies
  // convergence in every function. For now, we skip that and just do this one
  // cursory check. The underlying assumption is that in a compiler flow that
  // fully implements convergence control tokens, there is no mixing of
  // controlled and uncontrolled convergent operations in the whole program.
  if (CB.isConvergent()) {
    auto *I = CalledFunc->getEntryBlock().getFirstNonPHI();
    if (auto *IntrinsicCall = dyn_cast<IntrinsicInst>(I)) {
      if (IntrinsicCall->getIntrinsicID() ==
          Intrinsic::experimental_convergence_entry) {
        if (!ConvergenceControlToken) {
          return InlineResult::failure(
              "convergent call needs convergencectrl operand");
        }
      }
    }
  }

  // If the call to the callee cannot throw, set the 'nounwind' flag on any
  // calls that we inline.
  bool MarkNoUnwind = CB.doesNotThrow();

  BasicBlock *OrigBB = CB.getParent();
  Function *Caller = OrigBB->getParent();

  // GC poses two hazards to inlining, which only occur when the callee has GC:
  //  1. If the caller has no GC, then the callee's GC must be propagated to the
  //     caller.
  //  2. If the caller has a differing GC, it is invalid to inline.
  if (CalledFunc->hasGC()) {
    if (!Caller->hasGC())
      Caller->setGC(CalledFunc->getGC());
    else if (CalledFunc->getGC() != Caller->getGC())
      return InlineResult::failure("incompatible GC");
  }

  // Get the personality function from the callee if it contains a landing pad.
  Constant *CalledPersonality =
      CalledFunc->hasPersonalityFn()
          ? CalledFunc->getPersonalityFn()->stripPointerCasts()
          : nullptr;

  // Find the personality function used by the landing pads of the caller. If it
  // exists, then check to see that it matches the personality function used in
  // the callee.
  Constant *CallerPersonality =
      Caller->hasPersonalityFn()
          ? Caller->getPersonalityFn()->stripPointerCasts()
          : nullptr;
  if (CalledPersonality) {
    if (!CallerPersonality)
      Caller->setPersonalityFn(CalledPersonality);
    // If the personality functions match, then we can perform the
    // inlining. Otherwise, we can't inline.
    // TODO: This isn't 100% true. Some personality functions are proper
    //       supersets of others and can be used in place of the other.
    else if (CalledPersonality != CallerPersonality)
      return InlineResult::failure("incompatible personality");
  }

  // We need to figure out which funclet the callsite was in so that we may
  // properly nest the callee.
  Instruction *CallSiteEHPad = nullptr;
  if (CallerPersonality) {
    EHPersonality Personality = classifyEHPersonality(CallerPersonality);
    if (isScopedEHPersonality(Personality)) {
      std::optional<OperandBundleUse> ParentFunclet =
          CB.getOperandBundle(LLVMContext::OB_funclet);
      if (ParentFunclet)
        CallSiteEHPad = cast<FuncletPadInst>(ParentFunclet->Inputs.front());

      // OK, the inlining site is legal.  What about the target function?

      if (CallSiteEHPad) {
        if (Personality == EHPersonality::MSVC_CXX) {
          // The MSVC personality cannot tolerate catches getting inlined into
          // cleanup funclets.
          if (isa<CleanupPadInst>(CallSiteEHPad)) {
            // Ok, the call site is within a cleanuppad.  Let's check the callee
            // for catchpads.
            for (const BasicBlock &CalledBB : *CalledFunc) {
              if (isa<CatchSwitchInst>(CalledBB.getFirstNonPHI()))
                return InlineResult::failure("catch in cleanup funclet");
            }
          }
        } else if (isAsynchronousEHPersonality(Personality)) {
          // SEH is even less tolerant, there may not be any sort of exceptional
          // funclet in the callee.
          for (const BasicBlock &CalledBB : *CalledFunc) {
            if (CalledBB.isEHPad())
              return InlineResult::failure("SEH in cleanup funclet");
          }
        }
      }
    }
  }

  // Determine if we are dealing with a call in an EHPad which does not unwind
  // to caller.
  bool EHPadForCallUnwindsLocally = false;
  if (CallSiteEHPad && isa<CallInst>(CB)) {
    UnwindDestMemoTy FuncletUnwindMap;
    Value *CallSiteUnwindDestToken =
        getUnwindDestToken(CallSiteEHPad, FuncletUnwindMap);

    EHPadForCallUnwindsLocally =
        CallSiteUnwindDestToken &&
        !isa<ConstantTokenNone>(CallSiteUnwindDestToken);
  }

  // Get an iterator to the last basic block in the function, which will have
  // the new function inlined after it.
  Function::iterator LastBlock = --Caller->end();

  // Make sure to capture all of the return instructions from the cloned
  // function.
  SmallVector<ReturnInst*, 8> Returns;
  ClonedCodeInfo InlinedFunctionInfo;
  Function::iterator FirstNewBlock;

  { // Scope to destroy VMap after cloning.
    ValueToValueMapTy VMap;
    struct ByValInit {
      Value *Dst;
      Value *Src;
      Type *Ty;
    };
    // Keep a list of pair (dst, src) to emit byval initializations.
    SmallVector<ByValInit, 4> ByValInits;

    // When inlining a function that contains noalias scope metadata,
    // this metadata needs to be cloned so that the inlined blocks
    // have different "unique scopes" at every call site.
    // Track the metadata that must be cloned. Do this before other changes to
    // the function, so that we do not get in trouble when inlining caller ==
    // callee.
    ScopedAliasMetadataDeepCloner SAMetadataCloner(CB.getCalledFunction());

    auto &DL = Caller->getDataLayout();

    // Calculate the vector of arguments to pass into the function cloner, which
    // matches up the formal to the actual argument values.
    auto AI = CB.arg_begin();
    unsigned ArgNo = 0;
    for (Function::arg_iterator I = CalledFunc->arg_begin(),
         E = CalledFunc->arg_end(); I != E; ++I, ++AI, ++ArgNo) {
      Value *ActualArg = *AI;

      // When byval arguments actually inlined, we need to make the copy implied
      // by them explicit.  However, we don't do this if the callee is readonly
      // or readnone, because the copy would be unneeded: the callee doesn't
      // modify the struct.
      if (CB.isByValArgument(ArgNo)) {
        ActualArg = HandleByValArgument(CB.getParamByValType(ArgNo), ActualArg,
                                        &CB, CalledFunc, IFI,
                                        CalledFunc->getParamAlign(ArgNo));
        if (ActualArg != *AI)
          ByValInits.push_back(
              {ActualArg, (Value *)*AI, CB.getParamByValType(ArgNo)});
      }

      VMap[&*I] = ActualArg;
    }

    // TODO: Remove this when users have been updated to the assume bundles.
    // Add alignment assumptions if necessary. We do this before the inlined
    // instructions are actually cloned into the caller so that we can easily
    // check what will be known at the start of the inlined code.
    AddAlignmentAssumptions(CB, IFI);

    AssumptionCache *AC =
        IFI.GetAssumptionCache ? &IFI.GetAssumptionCache(*Caller) : nullptr;

    /// Preserve all attributes on of the call and its parameters.
    salvageKnowledge(&CB, AC);

    // We want the inliner to prune the code as it copies.  We would LOVE to
    // have no dead or constant instructions leftover after inlining occurs
    // (which can happen, e.g., because an argument was constant), but we'll be
    // happy with whatever the cloner can do.
    CloneAndPruneFunctionInto(Caller, CalledFunc, VMap,
                              /*ModuleLevelChanges=*/false, Returns, ".i",
                              &InlinedFunctionInfo);
    // Remember the first block that is newly cloned over.
    FirstNewBlock = LastBlock; ++FirstNewBlock;

    // Insert retainRV/clainRV runtime calls.
    objcarc::ARCInstKind RVCallKind = objcarc::getAttachedARCFunctionKind(&CB);
    if (RVCallKind != objcarc::ARCInstKind::None)
      inlineRetainOrClaimRVCalls(CB, RVCallKind, Returns);

    // Updated caller/callee profiles only when requested. For sample loader
    // inlining, the context-sensitive inlinee profile doesn't need to be
    // subtracted from callee profile, and the inlined clone also doesn't need
    // to be scaled based on call site count.
    if (IFI.UpdateProfile) {
      if (IFI.CallerBFI != nullptr && IFI.CalleeBFI != nullptr)
        // Update the BFI of blocks cloned into the caller.
        updateCallerBFI(OrigBB, VMap, IFI.CallerBFI, IFI.CalleeBFI,
                        CalledFunc->front());

      if (auto Profile = CalledFunc->getEntryCount())
        updateCallProfile(CalledFunc, VMap, *Profile, CB, IFI.PSI,
                          IFI.CallerBFI);
    }

    // Inject byval arguments initialization.
    for (ByValInit &Init : ByValInits)
      HandleByValArgumentInit(Init.Ty, Init.Dst, Init.Src, Caller->getParent(),
                              &*FirstNewBlock, IFI, CalledFunc);

    std::optional<OperandBundleUse> ParentDeopt =
        CB.getOperandBundle(LLVMContext::OB_deopt);
    if (ParentDeopt) {
      SmallVector<OperandBundleDef, 2> OpDefs;

      for (auto &VH : InlinedFunctionInfo.OperandBundleCallSites) {
        CallBase *ICS = dyn_cast_or_null<CallBase>(VH);
        if (!ICS)
          continue; // instruction was DCE'd or RAUW'ed to undef

        OpDefs.clear();

        OpDefs.reserve(ICS->getNumOperandBundles());

        for (unsigned COBi = 0, COBe = ICS->getNumOperandBundles(); COBi < COBe;
             ++COBi) {
          auto ChildOB = ICS->getOperandBundleAt(COBi);
          if (ChildOB.getTagID() != LLVMContext::OB_deopt) {
            // If the inlined call has other operand bundles, let them be
            OpDefs.emplace_back(ChildOB);
            continue;
          }

          // It may be useful to separate this logic (of handling operand
          // bundles) out to a separate "policy" component if this gets crowded.
          // Prepend the parent's deoptimization continuation to the newly
          // inlined call's deoptimization continuation.
          std::vector<Value *> MergedDeoptArgs;
          MergedDeoptArgs.reserve(ParentDeopt->Inputs.size() +
                                  ChildOB.Inputs.size());

          llvm::append_range(MergedDeoptArgs, ParentDeopt->Inputs);
          llvm::append_range(MergedDeoptArgs, ChildOB.Inputs);

          OpDefs.emplace_back("deopt", std::move(MergedDeoptArgs));
        }

        Instruction *NewI = CallBase::Create(ICS, OpDefs, ICS->getIterator());

        // Note: the RAUW does the appropriate fixup in VMap, so we need to do
        // this even if the call returns void.
        ICS->replaceAllUsesWith(NewI);

        VH = nullptr;
        ICS->eraseFromParent();
      }
    }

    // For 'nodebug' functions, the associated DISubprogram is always null.
    // Conservatively avoid propagating the callsite debug location to
    // instructions inlined from a function whose DISubprogram is not null.
    fixupLineNumbers(Caller, FirstNewBlock, &CB,
                     CalledFunc->getSubprogram() != nullptr);

    if (isAssignmentTrackingEnabled(*Caller->getParent())) {
      // Interpret inlined stores to caller-local variables as assignments.
      trackInlinedStores(FirstNewBlock, Caller->end(), CB);

      // Update DIAssignID metadata attachments and uses so that they are
      // unique to this inlined instance.
      fixupAssignments(FirstNewBlock, Caller->end());
    }

    // Now clone the inlined noalias scope metadata.
    SAMetadataCloner.clone();
    SAMetadataCloner.remap(FirstNewBlock, Caller->end());

    // Add noalias metadata if necessary.
    AddAliasScopeMetadata(CB, VMap, DL, CalleeAAR, InlinedFunctionInfo);

    // Clone return attributes on the callsite into the calls within the inlined
    // function which feed into its return value.
    AddReturnAttributes(CB, VMap, InlinedFunctionInfo);

    // Clone attributes on the params of the callsite to calls within the
    // inlined function which use the same param.
    AddParamAndFnBasicAttributes(CB, VMap, InlinedFunctionInfo);

    propagateMemProfMetadata(CalledFunc, CB,
                             InlinedFunctionInfo.ContainsMemProfMetadata, VMap);

    // Propagate metadata on the callsite if necessary.
    PropagateCallSiteMetadata(CB, FirstNewBlock, Caller->end());

    // Register any cloned assumptions.
    if (IFI.GetAssumptionCache)
      for (BasicBlock &NewBlock :
           make_range(FirstNewBlock->getIterator(), Caller->end()))
        for (Instruction &I : NewBlock)
          if (auto *II = dyn_cast<AssumeInst>(&I))
            IFI.GetAssumptionCache(*Caller).registerAssumption(II);
  }

  if (ConvergenceControlToken) {
    auto *I = FirstNewBlock->getFirstNonPHI();
    if (auto *IntrinsicCall = dyn_cast<IntrinsicInst>(I)) {
      if (IntrinsicCall->getIntrinsicID() ==
          Intrinsic::experimental_convergence_entry) {
        IntrinsicCall->replaceAllUsesWith(ConvergenceControlToken);
        IntrinsicCall->eraseFromParent();
      }
    }
  }

  // If there are any alloca instructions in the block that used to be the entry
  // block for the callee, move them to the entry block of the caller.  First
  // calculate which instruction they should be inserted before.  We insert the
  // instructions at the end of the current alloca list.
  {
    BasicBlock::iterator InsertPoint = Caller->begin()->begin();
    for (BasicBlock::iterator I = FirstNewBlock->begin(),
         E = FirstNewBlock->end(); I != E; ) {
      AllocaInst *AI = dyn_cast<AllocaInst>(I++);
      if (!AI) continue;

      // If the alloca is now dead, remove it.  This often occurs due to code
      // specialization.
      if (AI->use_empty()) {
        AI->eraseFromParent();
        continue;
      }

      if (!allocaWouldBeStaticInEntry(AI))
        continue;

      // Keep track of the static allocas that we inline into the caller.
      IFI.StaticAllocas.push_back(AI);

      // Scan for the block of allocas that we can move over, and move them
      // all at once.
      while (isa<AllocaInst>(I) &&
             !cast<AllocaInst>(I)->use_empty() &&
             allocaWouldBeStaticInEntry(cast<AllocaInst>(I))) {
        IFI.StaticAllocas.push_back(cast<AllocaInst>(I));
        ++I;
      }

      // Transfer all of the allocas over in a block.  Using splice means
      // that the instructions aren't removed from the symbol table, then
      // reinserted.
      I.setTailBit(true);
      Caller->getEntryBlock().splice(InsertPoint, &*FirstNewBlock,
                                     AI->getIterator(), I);
    }
  }

  SmallVector<Value*,4> VarArgsToForward;
  SmallVector<AttributeSet, 4> VarArgsAttrs;
  for (unsigned i = CalledFunc->getFunctionType()->getNumParams();
       i < CB.arg_size(); i++) {
    VarArgsToForward.push_back(CB.getArgOperand(i));
    VarArgsAttrs.push_back(CB.getAttributes().getParamAttrs(i));
  }

  bool InlinedMustTailCalls = false, InlinedDeoptimizeCalls = false;
  if (InlinedFunctionInfo.ContainsCalls) {
    CallInst::TailCallKind CallSiteTailKind = CallInst::TCK_None;
    if (CallInst *CI = dyn_cast<CallInst>(&CB))
      CallSiteTailKind = CI->getTailCallKind();

    // For inlining purposes, the "notail" marker is the same as no marker.
    if (CallSiteTailKind == CallInst::TCK_NoTail)
      CallSiteTailKind = CallInst::TCK_None;

    for (Function::iterator BB = FirstNewBlock, E = Caller->end(); BB != E;
         ++BB) {
      for (Instruction &I : llvm::make_early_inc_range(*BB)) {
        CallInst *CI = dyn_cast<CallInst>(&I);
        if (!CI)
          continue;

        // Forward varargs from inlined call site to calls to the
        // ForwardVarArgsTo function, if requested, and to musttail calls.
        if (!VarArgsToForward.empty() &&
            ((ForwardVarArgsTo &&
              CI->getCalledFunction() == ForwardVarArgsTo) ||
             CI->isMustTailCall())) {
          // Collect attributes for non-vararg parameters.
          AttributeList Attrs = CI->getAttributes();
          SmallVector<AttributeSet, 8> ArgAttrs;
          if (!Attrs.isEmpty() || !VarArgsAttrs.empty()) {
            for (unsigned ArgNo = 0;
                 ArgNo < CI->getFunctionType()->getNumParams(); ++ArgNo)
              ArgAttrs.push_back(Attrs.getParamAttrs(ArgNo));
          }

          // Add VarArg attributes.
          ArgAttrs.append(VarArgsAttrs.begin(), VarArgsAttrs.end());
          Attrs = AttributeList::get(CI->getContext(), Attrs.getFnAttrs(),
                                     Attrs.getRetAttrs(), ArgAttrs);
          // Add VarArgs to existing parameters.
          SmallVector<Value *, 6> Params(CI->args());
          Params.append(VarArgsToForward.begin(), VarArgsToForward.end());
          CallInst *NewCI = CallInst::Create(
              CI->getFunctionType(), CI->getCalledOperand(), Params, "", CI->getIterator());
          NewCI->setDebugLoc(CI->getDebugLoc());
          NewCI->setAttributes(Attrs);
          NewCI->setCallingConv(CI->getCallingConv());
          CI->replaceAllUsesWith(NewCI);
          CI->eraseFromParent();
          CI = NewCI;
        }

        if (Function *F = CI->getCalledFunction())
          InlinedDeoptimizeCalls |=
              F->getIntrinsicID() == Intrinsic::experimental_deoptimize;

        // We need to reduce the strength of any inlined tail calls.  For
        // musttail, we have to avoid introducing potential unbounded stack
        // growth.  For example, if functions 'f' and 'g' are mutually recursive
        // with musttail, we can inline 'g' into 'f' so long as we preserve
        // musttail on the cloned call to 'f'.  If either the inlined call site
        // or the cloned call site is *not* musttail, the program already has
        // one frame of stack growth, so it's safe to remove musttail.  Here is
        // a table of example transformations:
        //
        //    f -> musttail g -> musttail f  ==>  f -> musttail f
        //    f -> musttail g ->     tail f  ==>  f ->     tail f
        //    f ->          g -> musttail f  ==>  f ->          f
        //    f ->          g ->     tail f  ==>  f ->          f
        //
        // Inlined notail calls should remain notail calls.
        CallInst::TailCallKind ChildTCK = CI->getTailCallKind();
        if (ChildTCK != CallInst::TCK_NoTail)
          ChildTCK = std::min(CallSiteTailKind, ChildTCK);
        CI->setTailCallKind(ChildTCK);
        InlinedMustTailCalls |= CI->isMustTailCall();

        // Call sites inlined through a 'nounwind' call site should be
        // 'nounwind' as well. However, avoid marking call sites explicitly
        // where possible. This helps expose more opportunities for CSE after
        // inlining, commonly when the callee is an intrinsic.
        if (MarkNoUnwind && !CI->doesNotThrow())
          CI->setDoesNotThrow();
      }
    }
  }

  // Leave lifetime markers for the static alloca's, scoping them to the
  // function we just inlined.
  // We need to insert lifetime intrinsics even at O0 to avoid invalid
  // access caused by multithreaded coroutines. The check
  // `Caller->isPresplitCoroutine()` would affect AlwaysInliner at O0 only.
  if ((InsertLifetime || Caller->isPresplitCoroutine()) &&
      !IFI.StaticAllocas.empty()) {
    IRBuilder<> builder(&*FirstNewBlock, FirstNewBlock->begin());
    for (AllocaInst *AI : IFI.StaticAllocas) {
      // Don't mark swifterror allocas. They can't have bitcast uses.
      if (AI->isSwiftError())
        continue;

      // If the alloca is already scoped to something smaller than the whole
      // function then there's no need to add redundant, less accurate markers.
      if (hasLifetimeMarkers(AI))
        continue;

      // Try to determine the size of the allocation.
      ConstantInt *AllocaSize = nullptr;
      if (ConstantInt *AIArraySize =
          dyn_cast<ConstantInt>(AI->getArraySize())) {
        auto &DL = Caller->getDataLayout();
        Type *AllocaType = AI->getAllocatedType();
        TypeSize AllocaTypeSize = DL.getTypeAllocSize(AllocaType);
        uint64_t AllocaArraySize = AIArraySize->getLimitedValue();

        // Don't add markers for zero-sized allocas.
        if (AllocaArraySize == 0)
          continue;

        // Check that array size doesn't saturate uint64_t and doesn't
        // overflow when it's multiplied by type size.
        if (!AllocaTypeSize.isScalable() &&
            AllocaArraySize != std::numeric_limits<uint64_t>::max() &&
            std::numeric_limits<uint64_t>::max() / AllocaArraySize >=
                AllocaTypeSize.getFixedValue()) {
          AllocaSize = ConstantInt::get(Type::getInt64Ty(AI->getContext()),
                                        AllocaArraySize * AllocaTypeSize);
        }
      }

      builder.CreateLifetimeStart(AI, AllocaSize);
      for (ReturnInst *RI : Returns) {
        // Don't insert llvm.lifetime.end calls between a musttail or deoptimize
        // call and a return.  The return kills all local allocas.
        if (InlinedMustTailCalls &&
            RI->getParent()->getTerminatingMustTailCall())
          continue;
        if (InlinedDeoptimizeCalls &&
            RI->getParent()->getTerminatingDeoptimizeCall())
          continue;
        IRBuilder<>(RI).CreateLifetimeEnd(AI, AllocaSize);
      }
    }
  }

  // If the inlined code contained dynamic alloca instructions, wrap the inlined
  // code with llvm.stacksave/llvm.stackrestore intrinsics.
  if (InlinedFunctionInfo.ContainsDynamicAllocas) {
    // Insert the llvm.stacksave.
    CallInst *SavedPtr = IRBuilder<>(&*FirstNewBlock, FirstNewBlock->begin())
                             .CreateStackSave("savedstack");

    // Insert a call to llvm.stackrestore before any return instructions in the
    // inlined function.
    for (ReturnInst *RI : Returns) {
      // Don't insert llvm.stackrestore calls between a musttail or deoptimize
      // call and a return.  The return will restore the stack pointer.
      if (InlinedMustTailCalls && RI->getParent()->getTerminatingMustTailCall())
        continue;
      if (InlinedDeoptimizeCalls && RI->getParent()->getTerminatingDeoptimizeCall())
        continue;
      IRBuilder<>(RI).CreateStackRestore(SavedPtr);
    }
  }

  // If we are inlining for an invoke instruction, we must make sure to rewrite
  // any call instructions into invoke instructions.  This is sensitive to which
  // funclet pads were top-level in the inlinee, so must be done before
  // rewriting the "parent pad" links.
  if (auto *II = dyn_cast<InvokeInst>(&CB)) {
    BasicBlock *UnwindDest = II->getUnwindDest();
    Instruction *FirstNonPHI = UnwindDest->getFirstNonPHI();
    if (isa<LandingPadInst>(FirstNonPHI)) {
      HandleInlinedLandingPad(II, &*FirstNewBlock, InlinedFunctionInfo);
    } else {
      HandleInlinedEHPad(II, &*FirstNewBlock, InlinedFunctionInfo);
    }
  }

  // Update the lexical scopes of the new funclets and callsites.
  // Anything that had 'none' as its parent is now nested inside the callsite's
  // EHPad.
  if (CallSiteEHPad) {
    for (Function::iterator BB = FirstNewBlock->getIterator(),
                            E = Caller->end();
         BB != E; ++BB) {
      // Add bundle operands to inlined call sites.
      PropagateOperandBundles(BB, CallSiteEHPad);

      // It is problematic if the inlinee has a cleanupret which unwinds to
      // caller and we inline it into a call site which doesn't unwind but into
      // an EH pad that does.  Such an edge must be dynamically unreachable.
      // As such, we replace the cleanupret with unreachable.
      if (auto *CleanupRet = dyn_cast<CleanupReturnInst>(BB->getTerminator()))
        if (CleanupRet->unwindsToCaller() && EHPadForCallUnwindsLocally)
          changeToUnreachable(CleanupRet);

      Instruction *I = BB->getFirstNonPHI();
      if (!I->isEHPad())
        continue;

      if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(I)) {
        if (isa<ConstantTokenNone>(CatchSwitch->getParentPad()))
          CatchSwitch->setParentPad(CallSiteEHPad);
      } else {
        auto *FPI = cast<FuncletPadInst>(I);
        if (isa<ConstantTokenNone>(FPI->getParentPad()))
          FPI->setParentPad(CallSiteEHPad);
      }
    }
  }

  if (InlinedDeoptimizeCalls) {
    // We need to at least remove the deoptimizing returns from the Return set,
    // so that the control flow from those returns does not get merged into the
    // caller (but terminate it instead).  If the caller's return type does not
    // match the callee's return type, we also need to change the return type of
    // the intrinsic.
    if (Caller->getReturnType() == CB.getType()) {
      llvm::erase_if(Returns, [](ReturnInst *RI) {
        return RI->getParent()->getTerminatingDeoptimizeCall() != nullptr;
      });
    } else {
      SmallVector<ReturnInst *, 8> NormalReturns;
      Function *NewDeoptIntrinsic = Intrinsic::getDeclaration(
          Caller->getParent(), Intrinsic::experimental_deoptimize,
          {Caller->getReturnType()});

      for (ReturnInst *RI : Returns) {
        CallInst *DeoptCall = RI->getParent()->getTerminatingDeoptimizeCall();
        if (!DeoptCall) {
          NormalReturns.push_back(RI);
          continue;
        }

        // The calling convention on the deoptimize call itself may be bogus,
        // since the code we're inlining may have undefined behavior (and may
        // never actually execute at runtime); but all
        // @llvm.experimental.deoptimize declarations have to have the same
        // calling convention in a well-formed module.
        auto CallingConv = DeoptCall->getCalledFunction()->getCallingConv();
        NewDeoptIntrinsic->setCallingConv(CallingConv);
        auto *CurBB = RI->getParent();
        RI->eraseFromParent();

        SmallVector<Value *, 4> CallArgs(DeoptCall->args());

        SmallVector<OperandBundleDef, 1> OpBundles;
        DeoptCall->getOperandBundlesAsDefs(OpBundles);
        auto DeoptAttributes = DeoptCall->getAttributes();
        DeoptCall->eraseFromParent();
        assert(!OpBundles.empty() &&
               "Expected at least the deopt operand bundle");

        IRBuilder<> Builder(CurBB);
        CallInst *NewDeoptCall =
            Builder.CreateCall(NewDeoptIntrinsic, CallArgs, OpBundles);
        NewDeoptCall->setCallingConv(CallingConv);
        NewDeoptCall->setAttributes(DeoptAttributes);
        if (NewDeoptCall->getType()->isVoidTy())
          Builder.CreateRetVoid();
        else
          Builder.CreateRet(NewDeoptCall);
        // Since the ret type is changed, remove the incompatible attributes.
        NewDeoptCall->removeRetAttrs(
            AttributeFuncs::typeIncompatible(NewDeoptCall->getType()));
      }

      // Leave behind the normal returns so we can merge control flow.
      std::swap(Returns, NormalReturns);
    }
  }

  // Handle any inlined musttail call sites.  In order for a new call site to be
  // musttail, the source of the clone and the inlined call site must have been
  // musttail.  Therefore it's safe to return without merging control into the
  // phi below.
  if (InlinedMustTailCalls) {
    // Check if we need to bitcast the result of any musttail calls.
    Type *NewRetTy = Caller->getReturnType();
    bool NeedBitCast = !CB.use_empty() && CB.getType() != NewRetTy;

    // Handle the returns preceded by musttail calls separately.
    SmallVector<ReturnInst *, 8> NormalReturns;
    for (ReturnInst *RI : Returns) {
      CallInst *ReturnedMustTail =
          RI->getParent()->getTerminatingMustTailCall();
      if (!ReturnedMustTail) {
        NormalReturns.push_back(RI);
        continue;
      }
      if (!NeedBitCast)
        continue;

      // Delete the old return and any preceding bitcast.
      BasicBlock *CurBB = RI->getParent();
      auto *OldCast = dyn_cast_or_null<BitCastInst>(RI->getReturnValue());
      RI->eraseFromParent();
      if (OldCast)
        OldCast->eraseFromParent();

      // Insert a new bitcast and return with the right type.
      IRBuilder<> Builder(CurBB);
      Builder.CreateRet(Builder.CreateBitCast(ReturnedMustTail, NewRetTy));
    }

    // Leave behind the normal returns so we can merge control flow.
    std::swap(Returns, NormalReturns);
  }

  // Now that all of the transforms on the inlined code have taken place but
  // before we splice the inlined code into the CFG and lose track of which
  // blocks were actually inlined, collect the call sites. We only do this if
  // call graph updates weren't requested, as those provide value handle based
  // tracking of inlined call sites instead. Calls to intrinsics are not
  // collected because they are not inlineable.
  if (InlinedFunctionInfo.ContainsCalls) {
    // Otherwise just collect the raw call sites that were inlined.
    for (BasicBlock &NewBB :
         make_range(FirstNewBlock->getIterator(), Caller->end()))
      for (Instruction &I : NewBB)
        if (auto *CB = dyn_cast<CallBase>(&I))
          if (!(CB->getCalledFunction() &&
                CB->getCalledFunction()->isIntrinsic()))
            IFI.InlinedCallSites.push_back(CB);
  }

  // If we cloned in _exactly one_ basic block, and if that block ends in a
  // return instruction, we splice the body of the inlined callee directly into
  // the calling basic block.
  if (Returns.size() == 1 && std::distance(FirstNewBlock, Caller->end()) == 1) {
    // Move all of the instructions right before the call.
    OrigBB->splice(CB.getIterator(), &*FirstNewBlock, FirstNewBlock->begin(),
                   FirstNewBlock->end());
    // Remove the cloned basic block.
    Caller->back().eraseFromParent();

    // If the call site was an invoke instruction, add a branch to the normal
    // destination.
    if (InvokeInst *II = dyn_cast<InvokeInst>(&CB)) {
      BranchInst *NewBr = BranchInst::Create(II->getNormalDest(), CB.getIterator());
      NewBr->setDebugLoc(Returns[0]->getDebugLoc());
    }

    // If the return instruction returned a value, replace uses of the call with
    // uses of the returned value.
    if (!CB.use_empty()) {
      ReturnInst *R = Returns[0];
      if (&CB == R->getReturnValue())
        CB.replaceAllUsesWith(PoisonValue::get(CB.getType()));
      else
        CB.replaceAllUsesWith(R->getReturnValue());
    }
    // Since we are now done with the Call/Invoke, we can delete it.
    CB.eraseFromParent();

    // Since we are now done with the return instruction, delete it also.
    Returns[0]->eraseFromParent();

    if (MergeAttributes)
      AttributeFuncs::mergeAttributesForInlining(*Caller, *CalledFunc);

    // We are now done with the inlining.
    return InlineResult::success();
  }

  // Otherwise, we have the normal case, of more than one block to inline or
  // multiple return sites.

  // We want to clone the entire callee function into the hole between the
  // "starter" and "ender" blocks.  How we accomplish this depends on whether
  // this is an invoke instruction or a call instruction.
  BasicBlock *AfterCallBB;
  BranchInst *CreatedBranchToNormalDest = nullptr;
  if (InvokeInst *II = dyn_cast<InvokeInst>(&CB)) {

    // Add an unconditional branch to make this look like the CallInst case...
    CreatedBranchToNormalDest = BranchInst::Create(II->getNormalDest(), CB.getIterator());

    // Split the basic block.  This guarantees that no PHI nodes will have to be
    // updated due to new incoming edges, and make the invoke case more
    // symmetric to the call case.
    AfterCallBB =
        OrigBB->splitBasicBlock(CreatedBranchToNormalDest->getIterator(),
                                CalledFunc->getName() + ".exit");

  } else { // It's a call
    // If this is a call instruction, we need to split the basic block that
    // the call lives in.
    //
    AfterCallBB = OrigBB->splitBasicBlock(CB.getIterator(),
                                          CalledFunc->getName() + ".exit");
  }

  if (IFI.CallerBFI) {
    // Copy original BB's block frequency to AfterCallBB
    IFI.CallerBFI->setBlockFreq(AfterCallBB,
                                IFI.CallerBFI->getBlockFreq(OrigBB));
  }

  // Change the branch that used to go to AfterCallBB to branch to the first
  // basic block of the inlined function.
  //
  Instruction *Br = OrigBB->getTerminator();
  assert(Br && Br->getOpcode() == Instruction::Br &&
         "splitBasicBlock broken!");
  Br->setOperand(0, &*FirstNewBlock);

  // Now that the function is correct, make it a little bit nicer.  In
  // particular, move the basic blocks inserted from the end of the function
  // into the space made by splitting the source basic block.
  Caller->splice(AfterCallBB->getIterator(), Caller, FirstNewBlock,
                 Caller->end());

  // Handle all of the return instructions that we just cloned in, and eliminate
  // any users of the original call/invoke instruction.
  Type *RTy = CalledFunc->getReturnType();

  PHINode *PHI = nullptr;
  if (Returns.size() > 1) {
    // The PHI node should go at the front of the new basic block to merge all
    // possible incoming values.
    if (!CB.use_empty()) {
      PHI = PHINode::Create(RTy, Returns.size(), CB.getName());
      PHI->insertBefore(AfterCallBB->begin());
      // Anything that used the result of the function call should now use the
      // PHI node as their operand.
      CB.replaceAllUsesWith(PHI);
    }

    // Loop over all of the return instructions adding entries to the PHI node
    // as appropriate.
    if (PHI) {
      for (ReturnInst *RI : Returns) {
        assert(RI->getReturnValue()->getType() == PHI->getType() &&
               "Ret value not consistent in function!");
        PHI->addIncoming(RI->getReturnValue(), RI->getParent());
      }
    }

    // Add a branch to the merge points and remove return instructions.
    DebugLoc Loc;
    for (ReturnInst *RI : Returns) {
      BranchInst *BI = BranchInst::Create(AfterCallBB, RI->getIterator());
      Loc = RI->getDebugLoc();
      BI->setDebugLoc(Loc);
      RI->eraseFromParent();
    }
    // We need to set the debug location to *somewhere* inside the
    // inlined function. The line number may be nonsensical, but the
    // instruction will at least be associated with the right
    // function.
    if (CreatedBranchToNormalDest)
      CreatedBranchToNormalDest->setDebugLoc(Loc);
  } else if (!Returns.empty()) {
    // Otherwise, if there is exactly one return value, just replace anything
    // using the return value of the call with the computed value.
    if (!CB.use_empty()) {
      if (&CB == Returns[0]->getReturnValue())
        CB.replaceAllUsesWith(PoisonValue::get(CB.getType()));
      else
        CB.replaceAllUsesWith(Returns[0]->getReturnValue());
    }

    // Update PHI nodes that use the ReturnBB to use the AfterCallBB.
    BasicBlock *ReturnBB = Returns[0]->getParent();
    ReturnBB->replaceAllUsesWith(AfterCallBB);

    // Splice the code from the return block into the block that it will return
    // to, which contains the code that was after the call.
    AfterCallBB->splice(AfterCallBB->begin(), ReturnBB);

    if (CreatedBranchToNormalDest)
      CreatedBranchToNormalDest->setDebugLoc(Returns[0]->getDebugLoc());

    // Delete the return instruction now and empty ReturnBB now.
    Returns[0]->eraseFromParent();
    ReturnBB->eraseFromParent();
  } else if (!CB.use_empty()) {
    // No returns, but something is using the return value of the call.  Just
    // nuke the result.
    CB.replaceAllUsesWith(PoisonValue::get(CB.getType()));
  }

  // Since we are now done with the Call/Invoke, we can delete it.
  CB.eraseFromParent();

  // If we inlined any musttail calls and the original return is now
  // unreachable, delete it.  It can only contain a bitcast and ret.
  if (InlinedMustTailCalls && pred_empty(AfterCallBB))
    AfterCallBB->eraseFromParent();

  // We should always be able to fold the entry block of the function into the
  // single predecessor of the block...
  assert(cast<BranchInst>(Br)->isUnconditional() && "splitBasicBlock broken!");
  BasicBlock *CalleeEntry = cast<BranchInst>(Br)->getSuccessor(0);

  // Splice the code entry block into calling block, right before the
  // unconditional branch.
  CalleeEntry->replaceAllUsesWith(OrigBB);  // Update PHI nodes
  OrigBB->splice(Br->getIterator(), CalleeEntry);

  // Remove the unconditional branch.
  Br->eraseFromParent();

  // Now we can remove the CalleeEntry block, which is now empty.
  CalleeEntry->eraseFromParent();

  // If we inserted a phi node, check to see if it has a single value (e.g. all
  // the entries are the same or undef).  If so, remove the PHI so it doesn't
  // block other optimizations.
  if (PHI) {
    AssumptionCache *AC =
        IFI.GetAssumptionCache ? &IFI.GetAssumptionCache(*Caller) : nullptr;
    auto &DL = Caller->getDataLayout();
    if (Value *V = simplifyInstruction(PHI, {DL, nullptr, nullptr, AC})) {
      PHI->replaceAllUsesWith(V);
      PHI->eraseFromParent();
    }
  }

  if (MergeAttributes)
    AttributeFuncs::mergeAttributesForInlining(*Caller, *CalledFunc);

  return InlineResult::success();
}
