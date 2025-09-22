//===- Local.cpp - Functions to perform local transformations -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform various local transformations to the
// program.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/Local.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsWebAssembly.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <utility>

using namespace llvm;
using namespace llvm::PatternMatch;

extern cl::opt<bool> UseNewDbgInfoFormat;

#define DEBUG_TYPE "local"

STATISTIC(NumRemoved, "Number of unreachable basic blocks removed");
STATISTIC(NumPHICSEs, "Number of PHI's that got CSE'd");

static cl::opt<bool> PHICSEDebugHash(
    "phicse-debug-hash",
#ifdef EXPENSIVE_CHECKS
    cl::init(true),
#else
    cl::init(false),
#endif
    cl::Hidden,
    cl::desc("Perform extra assertion checking to verify that PHINodes's hash "
             "function is well-behaved w.r.t. its isEqual predicate"));

static cl::opt<unsigned> PHICSENumPHISmallSize(
    "phicse-num-phi-smallsize", cl::init(32), cl::Hidden,
    cl::desc(
        "When the basic block contains not more than this number of PHI nodes, "
        "perform a (faster!) exhaustive search instead of set-driven one."));

// Max recursion depth for collectBitParts used when detecting bswap and
// bitreverse idioms.
static const unsigned BitPartRecursionMaxDepth = 48;

//===----------------------------------------------------------------------===//
//  Local constant propagation.
//

/// ConstantFoldTerminator - If a terminator instruction is predicated on a
/// constant value, convert it into an unconditional branch to the constant
/// destination.  This is a nontrivial operation because the successors of this
/// basic block must have their PHI nodes updated.
/// Also calls RecursivelyDeleteTriviallyDeadInstructions() on any branch/switch
/// conditions and indirectbr addresses this might make dead if
/// DeleteDeadConditions is true.
bool llvm::ConstantFoldTerminator(BasicBlock *BB, bool DeleteDeadConditions,
                                  const TargetLibraryInfo *TLI,
                                  DomTreeUpdater *DTU) {
  Instruction *T = BB->getTerminator();
  IRBuilder<> Builder(T);

  // Branch - See if we are conditional jumping on constant
  if (auto *BI = dyn_cast<BranchInst>(T)) {
    if (BI->isUnconditional()) return false;  // Can't optimize uncond branch

    BasicBlock *Dest1 = BI->getSuccessor(0);
    BasicBlock *Dest2 = BI->getSuccessor(1);

    if (Dest2 == Dest1) {       // Conditional branch to same location?
      // This branch matches something like this:
      //     br bool %cond, label %Dest, label %Dest
      // and changes it into:  br label %Dest

      // Let the basic block know that we are letting go of one copy of it.
      assert(BI->getParent() && "Terminator not inserted in block!");
      Dest1->removePredecessor(BI->getParent());

      // Replace the conditional branch with an unconditional one.
      BranchInst *NewBI = Builder.CreateBr(Dest1);

      // Transfer the metadata to the new branch instruction.
      NewBI->copyMetadata(*BI, {LLVMContext::MD_loop, LLVMContext::MD_dbg,
                                LLVMContext::MD_annotation});

      Value *Cond = BI->getCondition();
      BI->eraseFromParent();
      if (DeleteDeadConditions)
        RecursivelyDeleteTriviallyDeadInstructions(Cond, TLI);
      return true;
    }

    if (auto *Cond = dyn_cast<ConstantInt>(BI->getCondition())) {
      // Are we branching on constant?
      // YES.  Change to unconditional branch...
      BasicBlock *Destination = Cond->getZExtValue() ? Dest1 : Dest2;
      BasicBlock *OldDest = Cond->getZExtValue() ? Dest2 : Dest1;

      // Let the basic block know that we are letting go of it.  Based on this,
      // it will adjust it's PHI nodes.
      OldDest->removePredecessor(BB);

      // Replace the conditional branch with an unconditional one.
      BranchInst *NewBI = Builder.CreateBr(Destination);

      // Transfer the metadata to the new branch instruction.
      NewBI->copyMetadata(*BI, {LLVMContext::MD_loop, LLVMContext::MD_dbg,
                                LLVMContext::MD_annotation});

      BI->eraseFromParent();
      if (DTU)
        DTU->applyUpdates({{DominatorTree::Delete, BB, OldDest}});
      return true;
    }

    return false;
  }

  if (auto *SI = dyn_cast<SwitchInst>(T)) {
    // If we are switching on a constant, we can convert the switch to an
    // unconditional branch.
    auto *CI = dyn_cast<ConstantInt>(SI->getCondition());
    BasicBlock *DefaultDest = SI->getDefaultDest();
    BasicBlock *TheOnlyDest = DefaultDest;

    // If the default is unreachable, ignore it when searching for TheOnlyDest.
    if (isa<UnreachableInst>(DefaultDest->getFirstNonPHIOrDbg()) &&
        SI->getNumCases() > 0) {
      TheOnlyDest = SI->case_begin()->getCaseSuccessor();
    }

    bool Changed = false;

    // Figure out which case it goes to.
    for (auto It = SI->case_begin(), End = SI->case_end(); It != End;) {
      // Found case matching a constant operand?
      if (It->getCaseValue() == CI) {
        TheOnlyDest = It->getCaseSuccessor();
        break;
      }

      // Check to see if this branch is going to the same place as the default
      // dest.  If so, eliminate it as an explicit compare.
      if (It->getCaseSuccessor() == DefaultDest) {
        MDNode *MD = getValidBranchWeightMDNode(*SI);
        unsigned NCases = SI->getNumCases();
        // Fold the case metadata into the default if there will be any branches
        // left, unless the metadata doesn't match the switch.
        if (NCases > 1 && MD) {
          // Collect branch weights into a vector.
          SmallVector<uint32_t, 8> Weights;
          extractBranchWeights(MD, Weights);

          // Merge weight of this case to the default weight.
          unsigned Idx = It->getCaseIndex();
          // TODO: Add overflow check.
          Weights[0] += Weights[Idx + 1];
          // Remove weight for this case.
          std::swap(Weights[Idx + 1], Weights.back());
          Weights.pop_back();
          setBranchWeights(*SI, Weights, hasBranchWeightOrigin(MD));
        }
        // Remove this entry.
        BasicBlock *ParentBB = SI->getParent();
        DefaultDest->removePredecessor(ParentBB);
        It = SI->removeCase(It);
        End = SI->case_end();

        // Removing this case may have made the condition constant. In that
        // case, update CI and restart iteration through the cases.
        if (auto *NewCI = dyn_cast<ConstantInt>(SI->getCondition())) {
          CI = NewCI;
          It = SI->case_begin();
        }

        Changed = true;
        continue;
      }

      // Otherwise, check to see if the switch only branches to one destination.
      // We do this by reseting "TheOnlyDest" to null when we find two non-equal
      // destinations.
      if (It->getCaseSuccessor() != TheOnlyDest)
        TheOnlyDest = nullptr;

      // Increment this iterator as we haven't removed the case.
      ++It;
    }

    if (CI && !TheOnlyDest) {
      // Branching on a constant, but not any of the cases, go to the default
      // successor.
      TheOnlyDest = SI->getDefaultDest();
    }

    // If we found a single destination that we can fold the switch into, do so
    // now.
    if (TheOnlyDest) {
      // Insert the new branch.
      Builder.CreateBr(TheOnlyDest);
      BasicBlock *BB = SI->getParent();

      SmallSet<BasicBlock *, 8> RemovedSuccessors;

      // Remove entries from PHI nodes which we no longer branch to...
      BasicBlock *SuccToKeep = TheOnlyDest;
      for (BasicBlock *Succ : successors(SI)) {
        if (DTU && Succ != TheOnlyDest)
          RemovedSuccessors.insert(Succ);
        // Found case matching a constant operand?
        if (Succ == SuccToKeep) {
          SuccToKeep = nullptr; // Don't modify the first branch to TheOnlyDest
        } else {
          Succ->removePredecessor(BB);
        }
      }

      // Delete the old switch.
      Value *Cond = SI->getCondition();
      SI->eraseFromParent();
      if (DeleteDeadConditions)
        RecursivelyDeleteTriviallyDeadInstructions(Cond, TLI);
      if (DTU) {
        std::vector<DominatorTree::UpdateType> Updates;
        Updates.reserve(RemovedSuccessors.size());
        for (auto *RemovedSuccessor : RemovedSuccessors)
          Updates.push_back({DominatorTree::Delete, BB, RemovedSuccessor});
        DTU->applyUpdates(Updates);
      }
      return true;
    }

    if (SI->getNumCases() == 1) {
      // Otherwise, we can fold this switch into a conditional branch
      // instruction if it has only one non-default destination.
      auto FirstCase = *SI->case_begin();
      Value *Cond = Builder.CreateICmpEQ(SI->getCondition(),
          FirstCase.getCaseValue(), "cond");

      // Insert the new branch.
      BranchInst *NewBr = Builder.CreateCondBr(Cond,
                                               FirstCase.getCaseSuccessor(),
                                               SI->getDefaultDest());
      SmallVector<uint32_t> Weights;
      if (extractBranchWeights(*SI, Weights) && Weights.size() == 2) {
        uint32_t DefWeight = Weights[0];
        uint32_t CaseWeight = Weights[1];
        // The TrueWeight should be the weight for the single case of SI.
        NewBr->setMetadata(LLVMContext::MD_prof,
                           MDBuilder(BB->getContext())
                               .createBranchWeights(CaseWeight, DefWeight));
      }

      // Update make.implicit metadata to the newly-created conditional branch.
      MDNode *MakeImplicitMD = SI->getMetadata(LLVMContext::MD_make_implicit);
      if (MakeImplicitMD)
        NewBr->setMetadata(LLVMContext::MD_make_implicit, MakeImplicitMD);

      // Delete the old switch.
      SI->eraseFromParent();
      return true;
    }
    return Changed;
  }

  if (auto *IBI = dyn_cast<IndirectBrInst>(T)) {
    // indirectbr blockaddress(@F, @BB) -> br label @BB
    if (auto *BA =
          dyn_cast<BlockAddress>(IBI->getAddress()->stripPointerCasts())) {
      BasicBlock *TheOnlyDest = BA->getBasicBlock();
      SmallSet<BasicBlock *, 8> RemovedSuccessors;

      // Insert the new branch.
      Builder.CreateBr(TheOnlyDest);

      BasicBlock *SuccToKeep = TheOnlyDest;
      for (unsigned i = 0, e = IBI->getNumDestinations(); i != e; ++i) {
        BasicBlock *DestBB = IBI->getDestination(i);
        if (DTU && DestBB != TheOnlyDest)
          RemovedSuccessors.insert(DestBB);
        if (IBI->getDestination(i) == SuccToKeep) {
          SuccToKeep = nullptr;
        } else {
          DestBB->removePredecessor(BB);
        }
      }
      Value *Address = IBI->getAddress();
      IBI->eraseFromParent();
      if (DeleteDeadConditions)
        // Delete pointer cast instructions.
        RecursivelyDeleteTriviallyDeadInstructions(Address, TLI);

      // Also zap the blockaddress constant if there are no users remaining,
      // otherwise the destination is still marked as having its address taken.
      if (BA->use_empty())
        BA->destroyConstant();

      // If we didn't find our destination in the IBI successor list, then we
      // have undefined behavior.  Replace the unconditional branch with an
      // 'unreachable' instruction.
      if (SuccToKeep) {
        BB->getTerminator()->eraseFromParent();
        new UnreachableInst(BB->getContext(), BB);
      }

      if (DTU) {
        std::vector<DominatorTree::UpdateType> Updates;
        Updates.reserve(RemovedSuccessors.size());
        for (auto *RemovedSuccessor : RemovedSuccessors)
          Updates.push_back({DominatorTree::Delete, BB, RemovedSuccessor});
        DTU->applyUpdates(Updates);
      }
      return true;
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
//  Local dead code elimination.
//

/// isInstructionTriviallyDead - Return true if the result produced by the
/// instruction is not used, and the instruction has no side effects.
///
bool llvm::isInstructionTriviallyDead(Instruction *I,
                                      const TargetLibraryInfo *TLI) {
  if (!I->use_empty())
    return false;
  return wouldInstructionBeTriviallyDead(I, TLI);
}

bool llvm::wouldInstructionBeTriviallyDeadOnUnusedPaths(
    Instruction *I, const TargetLibraryInfo *TLI) {
  // Instructions that are "markers" and have implied meaning on code around
  // them (without explicit uses), are not dead on unused paths.
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I))
    if (II->getIntrinsicID() == Intrinsic::stacksave ||
        II->getIntrinsicID() == Intrinsic::launder_invariant_group ||
        II->isLifetimeStartOrEnd())
      return false;
  return wouldInstructionBeTriviallyDead(I, TLI);
}

bool llvm::wouldInstructionBeTriviallyDead(const Instruction *I,
                                           const TargetLibraryInfo *TLI) {
  if (I->isTerminator())
    return false;

  // We don't want the landingpad-like instructions removed by anything this
  // general.
  if (I->isEHPad())
    return false;

  // We don't want debug info removed by anything this general.
  if (isa<DbgVariableIntrinsic>(I))
    return false;

  if (const DbgLabelInst *DLI = dyn_cast<DbgLabelInst>(I)) {
    if (DLI->getLabel())
      return false;
    return true;
  }

  if (auto *CB = dyn_cast<CallBase>(I))
    if (isRemovableAlloc(CB, TLI))
      return true;

  if (!I->willReturn()) {
    auto *II = dyn_cast<IntrinsicInst>(I);
    if (!II)
      return false;

    switch (II->getIntrinsicID()) {
    case Intrinsic::experimental_guard: {
      // Guards on true are operationally no-ops.  In the future we can
      // consider more sophisticated tradeoffs for guards considering potential
      // for check widening, but for now we keep things simple.
      auto *Cond = dyn_cast<ConstantInt>(II->getArgOperand(0));
      return Cond && Cond->isOne();
    }
    // TODO: These intrinsics are not safe to remove, because this may remove
    // a well-defined trap.
    case Intrinsic::wasm_trunc_signed:
    case Intrinsic::wasm_trunc_unsigned:
    case Intrinsic::ptrauth_auth:
    case Intrinsic::ptrauth_resign:
      return true;
    default:
      return false;
    }
  }

  if (!I->mayHaveSideEffects())
    return true;

  // Special case intrinsics that "may have side effects" but can be deleted
  // when dead.
  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    // Safe to delete llvm.stacksave and launder.invariant.group if dead.
    if (II->getIntrinsicID() == Intrinsic::stacksave ||
        II->getIntrinsicID() == Intrinsic::launder_invariant_group)
      return true;

    // Intrinsics declare sideeffects to prevent them from moving, but they are
    // nops without users.
    if (II->getIntrinsicID() == Intrinsic::allow_runtime_check ||
        II->getIntrinsicID() == Intrinsic::allow_ubsan_check)
      return true;

    if (II->isLifetimeStartOrEnd()) {
      auto *Arg = II->getArgOperand(1);
      // Lifetime intrinsics are dead when their right-hand is undef.
      if (isa<UndefValue>(Arg))
        return true;
      // If the right-hand is an alloc, global, or argument and the only uses
      // are lifetime intrinsics then the intrinsics are dead.
      if (isa<AllocaInst>(Arg) || isa<GlobalValue>(Arg) || isa<Argument>(Arg))
        return llvm::all_of(Arg->uses(), [](Use &Use) {
          if (IntrinsicInst *IntrinsicUse =
                  dyn_cast<IntrinsicInst>(Use.getUser()))
            return IntrinsicUse->isLifetimeStartOrEnd();
          return false;
        });
      return false;
    }

    // Assumptions are dead if their condition is trivially true.
    if (II->getIntrinsicID() == Intrinsic::assume &&
        isAssumeWithEmptyBundle(cast<AssumeInst>(*II))) {
      if (ConstantInt *Cond = dyn_cast<ConstantInt>(II->getArgOperand(0)))
        return !Cond->isZero();

      return false;
    }

    if (auto *FPI = dyn_cast<ConstrainedFPIntrinsic>(I)) {
      std::optional<fp::ExceptionBehavior> ExBehavior =
          FPI->getExceptionBehavior();
      return *ExBehavior != fp::ebStrict;
    }
  }

  if (auto *Call = dyn_cast<CallBase>(I)) {
    if (Value *FreedOp = getFreedOperand(Call, TLI))
      if (Constant *C = dyn_cast<Constant>(FreedOp))
        return C->isNullValue() || isa<UndefValue>(C);
    if (isMathLibCallNoop(Call, TLI))
      return true;
  }

  // Non-volatile atomic loads from constants can be removed.
  if (auto *LI = dyn_cast<LoadInst>(I))
    if (auto *GV = dyn_cast<GlobalVariable>(
            LI->getPointerOperand()->stripPointerCasts()))
      if (!LI->isVolatile() && GV->isConstant())
        return true;

  return false;
}

/// RecursivelyDeleteTriviallyDeadInstructions - If the specified value is a
/// trivially dead instruction, delete it.  If that makes any of its operands
/// trivially dead, delete them too, recursively.  Return true if any
/// instructions were deleted.
bool llvm::RecursivelyDeleteTriviallyDeadInstructions(
    Value *V, const TargetLibraryInfo *TLI, MemorySSAUpdater *MSSAU,
    std::function<void(Value *)> AboutToDeleteCallback) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I || !isInstructionTriviallyDead(I, TLI))
    return false;

  SmallVector<WeakTrackingVH, 16> DeadInsts;
  DeadInsts.push_back(I);
  RecursivelyDeleteTriviallyDeadInstructions(DeadInsts, TLI, MSSAU,
                                             AboutToDeleteCallback);

  return true;
}

bool llvm::RecursivelyDeleteTriviallyDeadInstructionsPermissive(
    SmallVectorImpl<WeakTrackingVH> &DeadInsts, const TargetLibraryInfo *TLI,
    MemorySSAUpdater *MSSAU,
    std::function<void(Value *)> AboutToDeleteCallback) {
  unsigned S = 0, E = DeadInsts.size(), Alive = 0;
  for (; S != E; ++S) {
    auto *I = dyn_cast_or_null<Instruction>(DeadInsts[S]);
    if (!I || !isInstructionTriviallyDead(I)) {
      DeadInsts[S] = nullptr;
      ++Alive;
    }
  }
  if (Alive == E)
    return false;
  RecursivelyDeleteTriviallyDeadInstructions(DeadInsts, TLI, MSSAU,
                                             AboutToDeleteCallback);
  return true;
}

void llvm::RecursivelyDeleteTriviallyDeadInstructions(
    SmallVectorImpl<WeakTrackingVH> &DeadInsts, const TargetLibraryInfo *TLI,
    MemorySSAUpdater *MSSAU,
    std::function<void(Value *)> AboutToDeleteCallback) {
  // Process the dead instruction list until empty.
  while (!DeadInsts.empty()) {
    Value *V = DeadInsts.pop_back_val();
    Instruction *I = cast_or_null<Instruction>(V);
    if (!I)
      continue;
    assert(isInstructionTriviallyDead(I, TLI) &&
           "Live instruction found in dead worklist!");
    assert(I->use_empty() && "Instructions with uses are not dead.");

    // Don't lose the debug info while deleting the instructions.
    salvageDebugInfo(*I);

    if (AboutToDeleteCallback)
      AboutToDeleteCallback(I);

    // Null out all of the instruction's operands to see if any operand becomes
    // dead as we go.
    for (Use &OpU : I->operands()) {
      Value *OpV = OpU.get();
      OpU.set(nullptr);

      if (!OpV->use_empty())
        continue;

      // If the operand is an instruction that became dead as we nulled out the
      // operand, and if it is 'trivially' dead, delete it in a future loop
      // iteration.
      if (Instruction *OpI = dyn_cast<Instruction>(OpV))
        if (isInstructionTriviallyDead(OpI, TLI))
          DeadInsts.push_back(OpI);
    }
    if (MSSAU)
      MSSAU->removeMemoryAccess(I);

    I->eraseFromParent();
  }
}

bool llvm::replaceDbgUsesWithUndef(Instruction *I) {
  SmallVector<DbgVariableIntrinsic *, 1> DbgUsers;
  SmallVector<DbgVariableRecord *, 1> DPUsers;
  findDbgUsers(DbgUsers, I, &DPUsers);
  for (auto *DII : DbgUsers)
    DII->setKillLocation();
  for (auto *DVR : DPUsers)
    DVR->setKillLocation();
  return !DbgUsers.empty() || !DPUsers.empty();
}

/// areAllUsesEqual - Check whether the uses of a value are all the same.
/// This is similar to Instruction::hasOneUse() except this will also return
/// true when there are no uses or multiple uses that all refer to the same
/// value.
static bool areAllUsesEqual(Instruction *I) {
  Value::user_iterator UI = I->user_begin();
  Value::user_iterator UE = I->user_end();
  if (UI == UE)
    return true;

  User *TheUse = *UI;
  for (++UI; UI != UE; ++UI) {
    if (*UI != TheUse)
      return false;
  }
  return true;
}

/// RecursivelyDeleteDeadPHINode - If the specified value is an effectively
/// dead PHI node, due to being a def-use chain of single-use nodes that
/// either forms a cycle or is terminated by a trivially dead instruction,
/// delete it.  If that makes any of its operands trivially dead, delete them
/// too, recursively.  Return true if a change was made.
bool llvm::RecursivelyDeleteDeadPHINode(PHINode *PN,
                                        const TargetLibraryInfo *TLI,
                                        llvm::MemorySSAUpdater *MSSAU) {
  SmallPtrSet<Instruction*, 4> Visited;
  for (Instruction *I = PN; areAllUsesEqual(I) && !I->mayHaveSideEffects();
       I = cast<Instruction>(*I->user_begin())) {
    if (I->use_empty())
      return RecursivelyDeleteTriviallyDeadInstructions(I, TLI, MSSAU);

    // If we find an instruction more than once, we're on a cycle that
    // won't prove fruitful.
    if (!Visited.insert(I).second) {
      // Break the cycle and delete the instruction and its operands.
      I->replaceAllUsesWith(PoisonValue::get(I->getType()));
      (void)RecursivelyDeleteTriviallyDeadInstructions(I, TLI, MSSAU);
      return true;
    }
  }
  return false;
}

static bool
simplifyAndDCEInstruction(Instruction *I,
                          SmallSetVector<Instruction *, 16> &WorkList,
                          const DataLayout &DL,
                          const TargetLibraryInfo *TLI) {
  if (isInstructionTriviallyDead(I, TLI)) {
    salvageDebugInfo(*I);

    // Null out all of the instruction's operands to see if any operand becomes
    // dead as we go.
    for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i) {
      Value *OpV = I->getOperand(i);
      I->setOperand(i, nullptr);

      if (!OpV->use_empty() || I == OpV)
        continue;

      // If the operand is an instruction that became dead as we nulled out the
      // operand, and if it is 'trivially' dead, delete it in a future loop
      // iteration.
      if (Instruction *OpI = dyn_cast<Instruction>(OpV))
        if (isInstructionTriviallyDead(OpI, TLI))
          WorkList.insert(OpI);
    }

    I->eraseFromParent();

    return true;
  }

  if (Value *SimpleV = simplifyInstruction(I, DL)) {
    // Add the users to the worklist. CAREFUL: an instruction can use itself,
    // in the case of a phi node.
    for (User *U : I->users()) {
      if (U != I) {
        WorkList.insert(cast<Instruction>(U));
      }
    }

    // Replace the instruction with its simplified value.
    bool Changed = false;
    if (!I->use_empty()) {
      I->replaceAllUsesWith(SimpleV);
      Changed = true;
    }
    if (isInstructionTriviallyDead(I, TLI)) {
      I->eraseFromParent();
      Changed = true;
    }
    return Changed;
  }
  return false;
}

/// SimplifyInstructionsInBlock - Scan the specified basic block and try to
/// simplify any instructions in it and recursively delete dead instructions.
///
/// This returns true if it changed the code, note that it can delete
/// instructions in other blocks as well in this block.
bool llvm::SimplifyInstructionsInBlock(BasicBlock *BB,
                                       const TargetLibraryInfo *TLI) {
  bool MadeChange = false;
  const DataLayout &DL = BB->getDataLayout();

#ifndef NDEBUG
  // In debug builds, ensure that the terminator of the block is never replaced
  // or deleted by these simplifications. The idea of simplification is that it
  // cannot introduce new instructions, and there is no way to replace the
  // terminator of a block without introducing a new instruction.
  AssertingVH<Instruction> TerminatorVH(&BB->back());
#endif

  SmallSetVector<Instruction *, 16> WorkList;
  // Iterate over the original function, only adding insts to the worklist
  // if they actually need to be revisited. This avoids having to pre-init
  // the worklist with the entire function's worth of instructions.
  for (BasicBlock::iterator BI = BB->begin(), E = std::prev(BB->end());
       BI != E;) {
    assert(!BI->isTerminator());
    Instruction *I = &*BI;
    ++BI;

    // We're visiting this instruction now, so make sure it's not in the
    // worklist from an earlier visit.
    if (!WorkList.count(I))
      MadeChange |= simplifyAndDCEInstruction(I, WorkList, DL, TLI);
  }

  while (!WorkList.empty()) {
    Instruction *I = WorkList.pop_back_val();
    MadeChange |= simplifyAndDCEInstruction(I, WorkList, DL, TLI);
  }
  return MadeChange;
}

//===----------------------------------------------------------------------===//
//  Control Flow Graph Restructuring.
//

void llvm::MergeBasicBlockIntoOnlyPred(BasicBlock *DestBB,
                                       DomTreeUpdater *DTU) {

  // If BB has single-entry PHI nodes, fold them.
  while (PHINode *PN = dyn_cast<PHINode>(DestBB->begin())) {
    Value *NewVal = PN->getIncomingValue(0);
    // Replace self referencing PHI with poison, it must be dead.
    if (NewVal == PN) NewVal = PoisonValue::get(PN->getType());
    PN->replaceAllUsesWith(NewVal);
    PN->eraseFromParent();
  }

  BasicBlock *PredBB = DestBB->getSinglePredecessor();
  assert(PredBB && "Block doesn't have a single predecessor!");

  bool ReplaceEntryBB = PredBB->isEntryBlock();

  // DTU updates: Collect all the edges that enter
  // PredBB. These dominator edges will be redirected to DestBB.
  SmallVector<DominatorTree::UpdateType, 32> Updates;

  if (DTU) {
    // To avoid processing the same predecessor more than once.
    SmallPtrSet<BasicBlock *, 2> SeenPreds;
    Updates.reserve(Updates.size() + 2 * pred_size(PredBB) + 1);
    for (BasicBlock *PredOfPredBB : predecessors(PredBB))
      // This predecessor of PredBB may already have DestBB as a successor.
      if (PredOfPredBB != PredBB)
        if (SeenPreds.insert(PredOfPredBB).second)
          Updates.push_back({DominatorTree::Insert, PredOfPredBB, DestBB});
    SeenPreds.clear();
    for (BasicBlock *PredOfPredBB : predecessors(PredBB))
      if (SeenPreds.insert(PredOfPredBB).second)
        Updates.push_back({DominatorTree::Delete, PredOfPredBB, PredBB});
    Updates.push_back({DominatorTree::Delete, PredBB, DestBB});
  }

  // Zap anything that took the address of DestBB.  Not doing this will give the
  // address an invalid value.
  if (DestBB->hasAddressTaken()) {
    BlockAddress *BA = BlockAddress::get(DestBB);
    Constant *Replacement =
      ConstantInt::get(Type::getInt32Ty(BA->getContext()), 1);
    BA->replaceAllUsesWith(ConstantExpr::getIntToPtr(Replacement,
                                                     BA->getType()));
    BA->destroyConstant();
  }

  // Anything that branched to PredBB now branches to DestBB.
  PredBB->replaceAllUsesWith(DestBB);

  // Splice all the instructions from PredBB to DestBB.
  PredBB->getTerminator()->eraseFromParent();
  DestBB->splice(DestBB->begin(), PredBB);
  new UnreachableInst(PredBB->getContext(), PredBB);

  // If the PredBB is the entry block of the function, move DestBB up to
  // become the entry block after we erase PredBB.
  if (ReplaceEntryBB)
    DestBB->moveAfter(PredBB);

  if (DTU) {
    assert(PredBB->size() == 1 &&
           isa<UnreachableInst>(PredBB->getTerminator()) &&
           "The successor list of PredBB isn't empty before "
           "applying corresponding DTU updates.");
    DTU->applyUpdatesPermissive(Updates);
    DTU->deleteBB(PredBB);
    // Recalculation of DomTree is needed when updating a forward DomTree and
    // the Entry BB is replaced.
    if (ReplaceEntryBB && DTU->hasDomTree()) {
      // The entry block was removed and there is no external interface for
      // the dominator tree to be notified of this change. In this corner-case
      // we recalculate the entire tree.
      DTU->recalculate(*(DestBB->getParent()));
    }
  }

  else {
    PredBB->eraseFromParent(); // Nuke BB if DTU is nullptr.
  }
}

/// Return true if we can choose one of these values to use in place of the
/// other. Note that we will always choose the non-undef value to keep.
static bool CanMergeValues(Value *First, Value *Second) {
  return First == Second || isa<UndefValue>(First) || isa<UndefValue>(Second);
}

/// Return true if we can fold BB, an almost-empty BB ending in an unconditional
/// branch to Succ, into Succ.
///
/// Assumption: Succ is the single successor for BB.
static bool
CanPropagatePredecessorsForPHIs(BasicBlock *BB, BasicBlock *Succ,
                                const SmallPtrSetImpl<BasicBlock *> &BBPreds) {
  assert(*succ_begin(BB) == Succ && "Succ is not successor of BB!");

  LLVM_DEBUG(dbgs() << "Looking to fold " << BB->getName() << " into "
                    << Succ->getName() << "\n");
  // Shortcut, if there is only a single predecessor it must be BB and merging
  // is always safe
  if (Succ->getSinglePredecessor())
    return true;

  // Look at all the phi nodes in Succ, to see if they present a conflict when
  // merging these blocks
  for (BasicBlock::iterator I = Succ->begin(); isa<PHINode>(I); ++I) {
    PHINode *PN = cast<PHINode>(I);

    // If the incoming value from BB is again a PHINode in
    // BB which has the same incoming value for *PI as PN does, we can
    // merge the phi nodes and then the blocks can still be merged
    PHINode *BBPN = dyn_cast<PHINode>(PN->getIncomingValueForBlock(BB));
    if (BBPN && BBPN->getParent() == BB) {
      for (unsigned PI = 0, PE = PN->getNumIncomingValues(); PI != PE; ++PI) {
        BasicBlock *IBB = PN->getIncomingBlock(PI);
        if (BBPreds.count(IBB) &&
            !CanMergeValues(BBPN->getIncomingValueForBlock(IBB),
                            PN->getIncomingValue(PI))) {
          LLVM_DEBUG(dbgs()
                     << "Can't fold, phi node " << PN->getName() << " in "
                     << Succ->getName() << " is conflicting with "
                     << BBPN->getName() << " with regard to common predecessor "
                     << IBB->getName() << "\n");
          return false;
        }
      }
    } else {
      Value* Val = PN->getIncomingValueForBlock(BB);
      for (unsigned PI = 0, PE = PN->getNumIncomingValues(); PI != PE; ++PI) {
        // See if the incoming value for the common predecessor is equal to the
        // one for BB, in which case this phi node will not prevent the merging
        // of the block.
        BasicBlock *IBB = PN->getIncomingBlock(PI);
        if (BBPreds.count(IBB) &&
            !CanMergeValues(Val, PN->getIncomingValue(PI))) {
          LLVM_DEBUG(dbgs() << "Can't fold, phi node " << PN->getName()
                            << " in " << Succ->getName()
                            << " is conflicting with regard to common "
                            << "predecessor " << IBB->getName() << "\n");
          return false;
        }
      }
    }
  }

  return true;
}

using PredBlockVector = SmallVector<BasicBlock *, 16>;
using IncomingValueMap = DenseMap<BasicBlock *, Value *>;

/// Determines the value to use as the phi node input for a block.
///
/// Select between \p OldVal any value that we know flows from \p BB
/// to a particular phi on the basis of which one (if either) is not
/// undef. Update IncomingValues based on the selected value.
///
/// \param OldVal The value we are considering selecting.
/// \param BB The block that the value flows in from.
/// \param IncomingValues A map from block-to-value for other phi inputs
/// that we have examined.
///
/// \returns the selected value.
static Value *selectIncomingValueForBlock(Value *OldVal, BasicBlock *BB,
                                          IncomingValueMap &IncomingValues) {
  if (!isa<UndefValue>(OldVal)) {
    assert((!IncomingValues.count(BB) ||
            IncomingValues.find(BB)->second == OldVal) &&
           "Expected OldVal to match incoming value from BB!");

    IncomingValues.insert(std::make_pair(BB, OldVal));
    return OldVal;
  }

  IncomingValueMap::const_iterator It = IncomingValues.find(BB);
  if (It != IncomingValues.end()) return It->second;

  return OldVal;
}

/// Create a map from block to value for the operands of a
/// given phi.
///
/// Create a map from block to value for each non-undef value flowing
/// into \p PN.
///
/// \param PN The phi we are collecting the map for.
/// \param IncomingValues [out] The map from block to value for this phi.
static void gatherIncomingValuesToPhi(PHINode *PN,
                                      IncomingValueMap &IncomingValues) {
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    BasicBlock *BB = PN->getIncomingBlock(i);
    Value *V = PN->getIncomingValue(i);

    if (!isa<UndefValue>(V))
      IncomingValues.insert(std::make_pair(BB, V));
  }
}

/// Replace the incoming undef values to a phi with the values
/// from a block-to-value map.
///
/// \param PN The phi we are replacing the undefs in.
/// \param IncomingValues A map from block to value.
static void replaceUndefValuesInPhi(PHINode *PN,
                                    const IncomingValueMap &IncomingValues) {
  SmallVector<unsigned> TrueUndefOps;
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    Value *V = PN->getIncomingValue(i);

    if (!isa<UndefValue>(V)) continue;

    BasicBlock *BB = PN->getIncomingBlock(i);
    IncomingValueMap::const_iterator It = IncomingValues.find(BB);

    // Keep track of undef/poison incoming values. Those must match, so we fix
    // them up below if needed.
    // Note: this is conservatively correct, but we could try harder and group
    // the undef values per incoming basic block.
    if (It == IncomingValues.end()) {
      TrueUndefOps.push_back(i);
      continue;
    }

    // There is a defined value for this incoming block, so map this undef
    // incoming value to the defined value.
    PN->setIncomingValue(i, It->second);
  }

  // If there are both undef and poison values incoming, then convert those
  // values to undef. It is invalid to have different values for the same
  // incoming block.
  unsigned PoisonCount = count_if(TrueUndefOps, [&](unsigned i) {
    return isa<PoisonValue>(PN->getIncomingValue(i));
  });
  if (PoisonCount != 0 && PoisonCount != TrueUndefOps.size()) {
    for (unsigned i : TrueUndefOps)
      PN->setIncomingValue(i, UndefValue::get(PN->getType()));
  }
}

// Only when they shares a single common predecessor, return true.
// Only handles cases when BB can't be merged while its predecessors can be
// redirected.
static bool
CanRedirectPredsOfEmptyBBToSucc(BasicBlock *BB, BasicBlock *Succ,
                                const SmallPtrSetImpl<BasicBlock *> &BBPreds,
                                const SmallPtrSetImpl<BasicBlock *> &SuccPreds,
                                BasicBlock *&CommonPred) {

  // There must be phis in BB, otherwise BB will be merged into Succ directly
  if (BB->phis().empty() || Succ->phis().empty())
    return false;

  // BB must have predecessors not shared that can be redirected to Succ
  if (!BB->hasNPredecessorsOrMore(2))
    return false;

  if (any_of(BBPreds, [](const BasicBlock *Pred) {
        return isa<IndirectBrInst>(Pred->getTerminator());
      }))
    return false;

  // Get the single common predecessor of both BB and Succ. Return false
  // when there are more than one common predecessors.
  for (BasicBlock *SuccPred : SuccPreds) {
    if (BBPreds.count(SuccPred)) {
      if (CommonPred)
        return false;
      CommonPred = SuccPred;
    }
  }

  return true;
}

/// Replace a value flowing from a block to a phi with
/// potentially multiple instances of that value flowing from the
/// block's predecessors to the phi.
///
/// \param BB The block with the value flowing into the phi.
/// \param BBPreds The predecessors of BB.
/// \param PN The phi that we are updating.
/// \param CommonPred The common predecessor of BB and PN's BasicBlock
static void redirectValuesFromPredecessorsToPhi(BasicBlock *BB,
                                                const PredBlockVector &BBPreds,
                                                PHINode *PN,
                                                BasicBlock *CommonPred) {
  Value *OldVal = PN->removeIncomingValue(BB, false);
  assert(OldVal && "No entry in PHI for Pred BB!");

  IncomingValueMap IncomingValues;

  // We are merging two blocks - BB, and the block containing PN - and
  // as a result we need to redirect edges from the predecessors of BB
  // to go to the block containing PN, and update PN
  // accordingly. Since we allow merging blocks in the case where the
  // predecessor and successor blocks both share some predecessors,
  // and where some of those common predecessors might have undef
  // values flowing into PN, we want to rewrite those values to be
  // consistent with the non-undef values.

  gatherIncomingValuesToPhi(PN, IncomingValues);

  // If this incoming value is one of the PHI nodes in BB, the new entries
  // in the PHI node are the entries from the old PHI.
  if (isa<PHINode>(OldVal) && cast<PHINode>(OldVal)->getParent() == BB) {
    PHINode *OldValPN = cast<PHINode>(OldVal);
    for (unsigned i = 0, e = OldValPN->getNumIncomingValues(); i != e; ++i) {
      // Note that, since we are merging phi nodes and BB and Succ might
      // have common predecessors, we could end up with a phi node with
      // identical incoming branches. This will be cleaned up later (and
      // will trigger asserts if we try to clean it up now, without also
      // simplifying the corresponding conditional branch).
      BasicBlock *PredBB = OldValPN->getIncomingBlock(i);

      if (PredBB == CommonPred)
        continue;

      Value *PredVal = OldValPN->getIncomingValue(i);
      Value *Selected =
          selectIncomingValueForBlock(PredVal, PredBB, IncomingValues);

      // And add a new incoming value for this predecessor for the
      // newly retargeted branch.
      PN->addIncoming(Selected, PredBB);
    }
    if (CommonPred)
      PN->addIncoming(OldValPN->getIncomingValueForBlock(CommonPred), BB);

  } else {
    for (BasicBlock *PredBB : BBPreds) {
      // Update existing incoming values in PN for this
      // predecessor of BB.
      if (PredBB == CommonPred)
        continue;

      Value *Selected =
          selectIncomingValueForBlock(OldVal, PredBB, IncomingValues);

      // And add a new incoming value for this predecessor for the
      // newly retargeted branch.
      PN->addIncoming(Selected, PredBB);
    }
    if (CommonPred)
      PN->addIncoming(OldVal, BB);
  }

  replaceUndefValuesInPhi(PN, IncomingValues);
}

bool llvm::TryToSimplifyUncondBranchFromEmptyBlock(BasicBlock *BB,
                                                   DomTreeUpdater *DTU) {
  assert(BB != &BB->getParent()->getEntryBlock() &&
         "TryToSimplifyUncondBranchFromEmptyBlock called on entry block!");

  // We can't simplify infinite loops.
  BasicBlock *Succ = cast<BranchInst>(BB->getTerminator())->getSuccessor(0);
  if (BB == Succ)
    return false;

  SmallPtrSet<BasicBlock *, 16> BBPreds(pred_begin(BB), pred_end(BB));
  SmallPtrSet<BasicBlock *, 16> SuccPreds(pred_begin(Succ), pred_end(Succ));

  // The single common predecessor of BB and Succ when BB cannot be killed
  BasicBlock *CommonPred = nullptr;

  bool BBKillable = CanPropagatePredecessorsForPHIs(BB, Succ, BBPreds);

  // Even if we can not fold BB into Succ, we may be able to redirect the
  // predecessors of BB to Succ.
  bool BBPhisMergeable =
      BBKillable ||
      CanRedirectPredsOfEmptyBBToSucc(BB, Succ, BBPreds, SuccPreds, CommonPred);

  if (!BBKillable && !BBPhisMergeable)
    return false;

  // Check to see if merging these blocks/phis would cause conflicts for any of
  // the phi nodes in BB or Succ. If not, we can safely merge.

  // Check for cases where Succ has multiple predecessors and a PHI node in BB
  // has uses which will not disappear when the PHI nodes are merged.  It is
  // possible to handle such cases, but difficult: it requires checking whether
  // BB dominates Succ, which is non-trivial to calculate in the case where
  // Succ has multiple predecessors.  Also, it requires checking whether
  // constructing the necessary self-referential PHI node doesn't introduce any
  // conflicts; this isn't too difficult, but the previous code for doing this
  // was incorrect.
  //
  // Note that if this check finds a live use, BB dominates Succ, so BB is
  // something like a loop pre-header (or rarely, a part of an irreducible CFG);
  // folding the branch isn't profitable in that case anyway.
  if (!Succ->getSinglePredecessor()) {
    BasicBlock::iterator BBI = BB->begin();
    while (isa<PHINode>(*BBI)) {
      for (Use &U : BBI->uses()) {
        if (PHINode* PN = dyn_cast<PHINode>(U.getUser())) {
          if (PN->getIncomingBlock(U) != BB)
            return false;
        } else {
          return false;
        }
      }
      ++BBI;
    }
  }

  if (BBPhisMergeable && CommonPred)
    LLVM_DEBUG(dbgs() << "Found Common Predecessor between: " << BB->getName()
                      << " and " << Succ->getName() << " : "
                      << CommonPred->getName() << "\n");

  // 'BB' and 'BB->Pred' are loop latches, bail out to presrve inner loop
  // metadata.
  //
  // FIXME: This is a stop-gap solution to preserve inner-loop metadata given
  // current status (that loop metadata is implemented as metadata attached to
  // the branch instruction in the loop latch block). To quote from review
  // comments, "the current representation of loop metadata (using a loop latch
  // terminator attachment) is known to be fundamentally broken. Loop latches
  // are not uniquely associated with loops (both in that a latch can be part of
  // multiple loops and a loop may have multiple latches). Loop headers are. The
  // solution to this problem is also known: Add support for basic block
  // metadata, and attach loop metadata to the loop header."
  //
  // Why bail out:
  // In this case, we expect 'BB' is the latch for outer-loop and 'BB->Pred' is
  // the latch for inner-loop (see reason below), so bail out to prerserve
  // inner-loop metadata rather than eliminating 'BB' and attaching its metadata
  // to this inner-loop.
  // - The reason we believe 'BB' and 'BB->Pred' have different inner-most
  // loops: assuming 'BB' and 'BB->Pred' are from the same inner-most loop L,
  // then 'BB' is the header and latch of 'L' and thereby 'L' must consist of
  // one self-looping basic block, which is contradictory with the assumption.
  //
  // To illustrate how inner-loop metadata is dropped:
  //
  // CFG Before
  //
  // BB is while.cond.exit, attached with loop metdata md2.
  // BB->Pred is for.body, attached with loop metadata md1.
  //
  //      entry
  //        |
  //        v
  // ---> while.cond   ------------->  while.end
  // |       |
  // |       v
  // |   while.body
  // |       |
  // |       v
  // |    for.body <---- (md1)
  // |       |  |______|
  // |       v
  // |    while.cond.exit (md2)
  // |       |
  // |_______|
  //
  // CFG After
  //
  // while.cond1 is the merge of while.cond.exit and while.cond above.
  // for.body is attached with md2, and md1 is dropped.
  // If LoopSimplify runs later (as a part of loop pass), it could create
  // dedicated exits for inner-loop (essentially adding `while.cond.exit`
  // back), but won't it won't see 'md1' nor restore it for the inner-loop.
  //
  //       entry
  //         |
  //         v
  // ---> while.cond1  ------------->  while.end
  // |       |
  // |       v
  // |   while.body
  // |       |
  // |       v
  // |    for.body <---- (md2)
  // |_______|  |______|
  if (Instruction *TI = BB->getTerminator())
    if (TI->hasMetadata(LLVMContext::MD_loop))
      for (BasicBlock *Pred : predecessors(BB))
        if (Instruction *PredTI = Pred->getTerminator())
          if (PredTI->hasMetadata(LLVMContext::MD_loop))
            return false;

  if (BBKillable)
    LLVM_DEBUG(dbgs() << "Killing Trivial BB: \n" << *BB);
  else if (BBPhisMergeable)
    LLVM_DEBUG(dbgs() << "Merge Phis in Trivial BB: \n" << *BB);

  SmallVector<DominatorTree::UpdateType, 32> Updates;

  if (DTU) {
    // To avoid processing the same predecessor more than once.
    SmallPtrSet<BasicBlock *, 8> SeenPreds;
    // All predecessors of BB (except the common predecessor) will be moved to
    // Succ.
    Updates.reserve(Updates.size() + 2 * pred_size(BB) + 1);

    for (auto *PredOfBB : predecessors(BB)) {
      // Do not modify those common predecessors of BB and Succ
      if (!SuccPreds.contains(PredOfBB))
        if (SeenPreds.insert(PredOfBB).second)
          Updates.push_back({DominatorTree::Insert, PredOfBB, Succ});
    }

    SeenPreds.clear();

    for (auto *PredOfBB : predecessors(BB))
      // When BB cannot be killed, do not remove the edge between BB and
      // CommonPred.
      if (SeenPreds.insert(PredOfBB).second && PredOfBB != CommonPred)
        Updates.push_back({DominatorTree::Delete, PredOfBB, BB});

    if (BBKillable)
      Updates.push_back({DominatorTree::Delete, BB, Succ});
  }

  if (isa<PHINode>(Succ->begin())) {
    // If there is more than one pred of succ, and there are PHI nodes in
    // the successor, then we need to add incoming edges for the PHI nodes
    //
    const PredBlockVector BBPreds(predecessors(BB));

    // Loop over all of the PHI nodes in the successor of BB.
    for (BasicBlock::iterator I = Succ->begin(); isa<PHINode>(I); ++I) {
      PHINode *PN = cast<PHINode>(I);
      redirectValuesFromPredecessorsToPhi(BB, BBPreds, PN, CommonPred);
    }
  }

  if (Succ->getSinglePredecessor()) {
    // BB is the only predecessor of Succ, so Succ will end up with exactly
    // the same predecessors BB had.
    // Copy over any phi, debug or lifetime instruction.
    BB->getTerminator()->eraseFromParent();
    Succ->splice(Succ->getFirstNonPHIIt(), BB);
  } else {
    while (PHINode *PN = dyn_cast<PHINode>(&BB->front())) {
      // We explicitly check for such uses for merging phis.
      assert(PN->use_empty() && "There shouldn't be any uses here!");
      PN->eraseFromParent();
    }
  }

  // If the unconditional branch we replaced contains llvm.loop metadata, we
  // add the metadata to the branch instructions in the predecessors.
  if (Instruction *TI = BB->getTerminator())
    if (MDNode *LoopMD = TI->getMetadata(LLVMContext::MD_loop))
      for (BasicBlock *Pred : predecessors(BB))
        Pred->getTerminator()->setMetadata(LLVMContext::MD_loop, LoopMD);

  if (BBKillable) {
    // Everything that jumped to BB now goes to Succ.
    BB->replaceAllUsesWith(Succ);

    if (!Succ->hasName())
      Succ->takeName(BB);

    // Clear the successor list of BB to match updates applying to DTU later.
    if (BB->getTerminator())
      BB->back().eraseFromParent();

    new UnreachableInst(BB->getContext(), BB);
    assert(succ_empty(BB) && "The successor list of BB isn't empty before "
                             "applying corresponding DTU updates.");
  } else if (BBPhisMergeable) {
    //  Everything except CommonPred that jumped to BB now goes to Succ.
    BB->replaceUsesWithIf(Succ, [BBPreds, CommonPred](Use &U) -> bool {
      if (Instruction *UseInst = dyn_cast<Instruction>(U.getUser()))
        return UseInst->getParent() != CommonPred &&
               BBPreds.contains(UseInst->getParent());
      return false;
    });
  }

  if (DTU)
    DTU->applyUpdates(Updates);

  if (BBKillable)
    DeleteDeadBlock(BB, DTU);

  return true;
}

static bool
EliminateDuplicatePHINodesNaiveImpl(BasicBlock *BB,
                                    SmallPtrSetImpl<PHINode *> &ToRemove) {
  // This implementation doesn't currently consider undef operands
  // specially. Theoretically, two phis which are identical except for
  // one having an undef where the other doesn't could be collapsed.

  bool Changed = false;

  // Examine each PHI.
  // Note that increment of I must *NOT* be in the iteration_expression, since
  // we don't want to immediately advance when we restart from the beginning.
  for (auto I = BB->begin(); PHINode *PN = dyn_cast<PHINode>(I);) {
    ++I;
    // Is there an identical PHI node in this basic block?
    // Note that we only look in the upper square's triangle,
    // we already checked that the lower triangle PHI's aren't identical.
    for (auto J = I; PHINode *DuplicatePN = dyn_cast<PHINode>(J); ++J) {
      if (ToRemove.contains(DuplicatePN))
        continue;
      if (!DuplicatePN->isIdenticalToWhenDefined(PN))
        continue;
      // A duplicate. Replace this PHI with the base PHI.
      ++NumPHICSEs;
      DuplicatePN->replaceAllUsesWith(PN);
      ToRemove.insert(DuplicatePN);
      Changed = true;

      // The RAUW can change PHIs that we already visited.
      I = BB->begin();
      break; // Start over from the beginning.
    }
  }
  return Changed;
}

static bool
EliminateDuplicatePHINodesSetBasedImpl(BasicBlock *BB,
                                       SmallPtrSetImpl<PHINode *> &ToRemove) {
  // This implementation doesn't currently consider undef operands
  // specially. Theoretically, two phis which are identical except for
  // one having an undef where the other doesn't could be collapsed.

  struct PHIDenseMapInfo {
    static PHINode *getEmptyKey() {
      return DenseMapInfo<PHINode *>::getEmptyKey();
    }

    static PHINode *getTombstoneKey() {
      return DenseMapInfo<PHINode *>::getTombstoneKey();
    }

    static bool isSentinel(PHINode *PN) {
      return PN == getEmptyKey() || PN == getTombstoneKey();
    }

    // WARNING: this logic must be kept in sync with
    //          Instruction::isIdenticalToWhenDefined()!
    static unsigned getHashValueImpl(PHINode *PN) {
      // Compute a hash value on the operands. Instcombine will likely have
      // sorted them, which helps expose duplicates, but we have to check all
      // the operands to be safe in case instcombine hasn't run.
      return static_cast<unsigned>(hash_combine(
          hash_combine_range(PN->value_op_begin(), PN->value_op_end()),
          hash_combine_range(PN->block_begin(), PN->block_end())));
    }

    static unsigned getHashValue(PHINode *PN) {
#ifndef NDEBUG
      // If -phicse-debug-hash was specified, return a constant -- this
      // will force all hashing to collide, so we'll exhaustively search
      // the table for a match, and the assertion in isEqual will fire if
      // there's a bug causing equal keys to hash differently.
      if (PHICSEDebugHash)
        return 0;
#endif
      return getHashValueImpl(PN);
    }

    static bool isEqualImpl(PHINode *LHS, PHINode *RHS) {
      if (isSentinel(LHS) || isSentinel(RHS))
        return LHS == RHS;
      return LHS->isIdenticalTo(RHS);
    }

    static bool isEqual(PHINode *LHS, PHINode *RHS) {
      // These comparisons are nontrivial, so assert that equality implies
      // hash equality (DenseMap demands this as an invariant).
      bool Result = isEqualImpl(LHS, RHS);
      assert(!Result || (isSentinel(LHS) && LHS == RHS) ||
             getHashValueImpl(LHS) == getHashValueImpl(RHS));
      return Result;
    }
  };

  // Set of unique PHINodes.
  DenseSet<PHINode *, PHIDenseMapInfo> PHISet;
  PHISet.reserve(4 * PHICSENumPHISmallSize);

  // Examine each PHI.
  bool Changed = false;
  for (auto I = BB->begin(); PHINode *PN = dyn_cast<PHINode>(I++);) {
    if (ToRemove.contains(PN))
      continue;
    auto Inserted = PHISet.insert(PN);
    if (!Inserted.second) {
      // A duplicate. Replace this PHI with its duplicate.
      ++NumPHICSEs;
      PN->replaceAllUsesWith(*Inserted.first);
      ToRemove.insert(PN);
      Changed = true;

      // The RAUW can change PHIs that we already visited. Start over from the
      // beginning.
      PHISet.clear();
      I = BB->begin();
    }
  }

  return Changed;
}

bool llvm::EliminateDuplicatePHINodes(BasicBlock *BB,
                                      SmallPtrSetImpl<PHINode *> &ToRemove) {
  if (
#ifndef NDEBUG
      !PHICSEDebugHash &&
#endif
      hasNItemsOrLess(BB->phis(), PHICSENumPHISmallSize))
    return EliminateDuplicatePHINodesNaiveImpl(BB, ToRemove);
  return EliminateDuplicatePHINodesSetBasedImpl(BB, ToRemove);
}

bool llvm::EliminateDuplicatePHINodes(BasicBlock *BB) {
  SmallPtrSet<PHINode *, 8> ToRemove;
  bool Changed = EliminateDuplicatePHINodes(BB, ToRemove);
  for (PHINode *PN : ToRemove)
    PN->eraseFromParent();
  return Changed;
}

Align llvm::tryEnforceAlignment(Value *V, Align PrefAlign,
                                const DataLayout &DL) {
  V = V->stripPointerCasts();

  if (AllocaInst *AI = dyn_cast<AllocaInst>(V)) {
    // TODO: Ideally, this function would not be called if PrefAlign is smaller
    // than the current alignment, as the known bits calculation should have
    // already taken it into account. However, this is not always the case,
    // as computeKnownBits() has a depth limit, while stripPointerCasts()
    // doesn't.
    Align CurrentAlign = AI->getAlign();
    if (PrefAlign <= CurrentAlign)
      return CurrentAlign;

    // If the preferred alignment is greater than the natural stack alignment
    // then don't round up. This avoids dynamic stack realignment.
    if (DL.exceedsNaturalStackAlignment(PrefAlign))
      return CurrentAlign;
    AI->setAlignment(PrefAlign);
    return PrefAlign;
  }

  if (auto *GO = dyn_cast<GlobalObject>(V)) {
    // TODO: as above, this shouldn't be necessary.
    Align CurrentAlign = GO->getPointerAlignment(DL);
    if (PrefAlign <= CurrentAlign)
      return CurrentAlign;

    // If there is a large requested alignment and we can, bump up the alignment
    // of the global.  If the memory we set aside for the global may not be the
    // memory used by the final program then it is impossible for us to reliably
    // enforce the preferred alignment.
    if (!GO->canIncreaseAlignment())
      return CurrentAlign;

    if (GO->isThreadLocal()) {
      unsigned MaxTLSAlign = GO->getParent()->getMaxTLSAlignment() / CHAR_BIT;
      if (MaxTLSAlign && PrefAlign > Align(MaxTLSAlign))
        PrefAlign = Align(MaxTLSAlign);
    }

    GO->setAlignment(PrefAlign);
    return PrefAlign;
  }

  return Align(1);
}

Align llvm::getOrEnforceKnownAlignment(Value *V, MaybeAlign PrefAlign,
                                       const DataLayout &DL,
                                       const Instruction *CxtI,
                                       AssumptionCache *AC,
                                       const DominatorTree *DT) {
  assert(V->getType()->isPointerTy() &&
         "getOrEnforceKnownAlignment expects a pointer!");

  KnownBits Known = computeKnownBits(V, DL, 0, AC, CxtI, DT);
  unsigned TrailZ = Known.countMinTrailingZeros();

  // Avoid trouble with ridiculously large TrailZ values, such as
  // those computed from a null pointer.
  // LLVM doesn't support alignments larger than (1 << MaxAlignmentExponent).
  TrailZ = std::min(TrailZ, +Value::MaxAlignmentExponent);

  Align Alignment = Align(1ull << std::min(Known.getBitWidth() - 1, TrailZ));

  if (PrefAlign && *PrefAlign > Alignment)
    Alignment = std::max(Alignment, tryEnforceAlignment(V, *PrefAlign, DL));

  // We don't need to make any adjustment.
  return Alignment;
}

///===---------------------------------------------------------------------===//
///  Dbg Intrinsic utilities
///

/// See if there is a dbg.value intrinsic for DIVar for the PHI node.
static bool PhiHasDebugValue(DILocalVariable *DIVar,
                             DIExpression *DIExpr,
                             PHINode *APN) {
  // Since we can't guarantee that the original dbg.declare intrinsic
  // is removed by LowerDbgDeclare(), we need to make sure that we are
  // not inserting the same dbg.value intrinsic over and over.
  SmallVector<DbgValueInst *, 1> DbgValues;
  SmallVector<DbgVariableRecord *, 1> DbgVariableRecords;
  findDbgValues(DbgValues, APN, &DbgVariableRecords);
  for (auto *DVI : DbgValues) {
    assert(is_contained(DVI->getValues(), APN));
    if ((DVI->getVariable() == DIVar) && (DVI->getExpression() == DIExpr))
      return true;
  }
  for (auto *DVR : DbgVariableRecords) {
    assert(is_contained(DVR->location_ops(), APN));
    if ((DVR->getVariable() == DIVar) && (DVR->getExpression() == DIExpr))
      return true;
  }
  return false;
}

/// Check if the alloc size of \p ValTy is large enough to cover the variable
/// (or fragment of the variable) described by \p DII.
///
/// This is primarily intended as a helper for the different
/// ConvertDebugDeclareToDebugValue functions. The dbg.declare that is converted
/// describes an alloca'd variable, so we need to use the alloc size of the
/// value when doing the comparison. E.g. an i1 value will be identified as
/// covering an n-bit fragment, if the store size of i1 is at least n bits.
static bool valueCoversEntireFragment(Type *ValTy, DbgVariableIntrinsic *DII) {
  const DataLayout &DL = DII->getDataLayout();
  TypeSize ValueSize = DL.getTypeAllocSizeInBits(ValTy);
  if (std::optional<uint64_t> FragmentSize =
          DII->getExpression()->getActiveBits(DII->getVariable()))
    return TypeSize::isKnownGE(ValueSize, TypeSize::getFixed(*FragmentSize));

  // We can't always calculate the size of the DI variable (e.g. if it is a
  // VLA). Try to use the size of the alloca that the dbg intrinsic describes
  // intead.
  if (DII->isAddressOfVariable()) {
    // DII should have exactly 1 location when it is an address.
    assert(DII->getNumVariableLocationOps() == 1 &&
           "address of variable must have exactly 1 location operand.");
    if (auto *AI =
            dyn_cast_or_null<AllocaInst>(DII->getVariableLocationOp(0))) {
      if (std::optional<TypeSize> FragmentSize =
              AI->getAllocationSizeInBits(DL)) {
        return TypeSize::isKnownGE(ValueSize, *FragmentSize);
      }
    }
  }
  // Could not determine size of variable. Conservatively return false.
  return false;
}
// RemoveDIs: duplicate implementation of the above, using DbgVariableRecords,
// the replacement for dbg.values.
static bool valueCoversEntireFragment(Type *ValTy, DbgVariableRecord *DVR) {
  const DataLayout &DL = DVR->getModule()->getDataLayout();
  TypeSize ValueSize = DL.getTypeAllocSizeInBits(ValTy);
  if (std::optional<uint64_t> FragmentSize =
          DVR->getExpression()->getActiveBits(DVR->getVariable()))
    return TypeSize::isKnownGE(ValueSize, TypeSize::getFixed(*FragmentSize));

  // We can't always calculate the size of the DI variable (e.g. if it is a
  // VLA). Try to use the size of the alloca that the dbg intrinsic describes
  // intead.
  if (DVR->isAddressOfVariable()) {
    // DVR should have exactly 1 location when it is an address.
    assert(DVR->getNumVariableLocationOps() == 1 &&
           "address of variable must have exactly 1 location operand.");
    if (auto *AI =
            dyn_cast_or_null<AllocaInst>(DVR->getVariableLocationOp(0))) {
      if (std::optional<TypeSize> FragmentSize = AI->getAllocationSizeInBits(DL)) {
        return TypeSize::isKnownGE(ValueSize, *FragmentSize);
      }
    }
  }
  // Could not determine size of variable. Conservatively return false.
  return false;
}

static void insertDbgValueOrDbgVariableRecord(DIBuilder &Builder, Value *DV,
                                              DILocalVariable *DIVar,
                                              DIExpression *DIExpr,
                                              const DebugLoc &NewLoc,
                                              BasicBlock::iterator Instr) {
  if (!UseNewDbgInfoFormat) {
    auto DbgVal = Builder.insertDbgValueIntrinsic(DV, DIVar, DIExpr, NewLoc,
                                                  (Instruction *)nullptr);
    DbgVal.get<Instruction *>()->insertBefore(Instr);
  } else {
    // RemoveDIs: if we're using the new debug-info format, allocate a
    // DbgVariableRecord directly instead of a dbg.value intrinsic.
    ValueAsMetadata *DVAM = ValueAsMetadata::get(DV);
    DbgVariableRecord *DV =
        new DbgVariableRecord(DVAM, DIVar, DIExpr, NewLoc.get());
    Instr->getParent()->insertDbgRecordBefore(DV, Instr);
  }
}

static void insertDbgValueOrDbgVariableRecordAfter(
    DIBuilder &Builder, Value *DV, DILocalVariable *DIVar, DIExpression *DIExpr,
    const DebugLoc &NewLoc, BasicBlock::iterator Instr) {
  if (!UseNewDbgInfoFormat) {
    auto DbgVal = Builder.insertDbgValueIntrinsic(DV, DIVar, DIExpr, NewLoc,
                                                  (Instruction *)nullptr);
    DbgVal.get<Instruction *>()->insertAfter(&*Instr);
  } else {
    // RemoveDIs: if we're using the new debug-info format, allocate a
    // DbgVariableRecord directly instead of a dbg.value intrinsic.
    ValueAsMetadata *DVAM = ValueAsMetadata::get(DV);
    DbgVariableRecord *DV =
        new DbgVariableRecord(DVAM, DIVar, DIExpr, NewLoc.get());
    Instr->getParent()->insertDbgRecordAfter(DV, &*Instr);
  }
}

/// Inserts a llvm.dbg.value intrinsic before a store to an alloca'd value
/// that has an associated llvm.dbg.declare intrinsic.
void llvm::ConvertDebugDeclareToDebugValue(DbgVariableIntrinsic *DII,
                                           StoreInst *SI, DIBuilder &Builder) {
  assert(DII->isAddressOfVariable() || isa<DbgAssignIntrinsic>(DII));
  auto *DIVar = DII->getVariable();
  assert(DIVar && "Missing variable");
  auto *DIExpr = DII->getExpression();
  Value *DV = SI->getValueOperand();

  DebugLoc NewLoc = getDebugValueLoc(DII);

  // If the alloca describes the variable itself, i.e. the expression in the
  // dbg.declare doesn't start with a dereference, we can perform the
  // conversion if the value covers the entire fragment of DII.
  // If the alloca describes the *address* of DIVar, i.e. DIExpr is
  // *just* a DW_OP_deref, we use DV as is for the dbg.value.
  // We conservatively ignore other dereferences, because the following two are
  // not equivalent:
  //     dbg.declare(alloca, ..., !Expr(deref, plus_uconstant, 2))
  //     dbg.value(DV, ..., !Expr(deref, plus_uconstant, 2))
  // The former is adding 2 to the address of the variable, whereas the latter
  // is adding 2 to the value of the variable. As such, we insist on just a
  // deref expression.
  bool CanConvert =
      DIExpr->isDeref() || (!DIExpr->startsWithDeref() &&
                            valueCoversEntireFragment(DV->getType(), DII));
  if (CanConvert) {
    insertDbgValueOrDbgVariableRecord(Builder, DV, DIVar, DIExpr, NewLoc,
                                      SI->getIterator());
    return;
  }

  // FIXME: If storing to a part of the variable described by the dbg.declare,
  // then we want to insert a dbg.value for the corresponding fragment.
  LLVM_DEBUG(dbgs() << "Failed to convert dbg.declare to dbg.value: " << *DII
                    << '\n');
  // For now, when there is a store to parts of the variable (but we do not
  // know which part) we insert an dbg.value intrinsic to indicate that we
  // know nothing about the variable's content.
  DV = UndefValue::get(DV->getType());
  insertDbgValueOrDbgVariableRecord(Builder, DV, DIVar, DIExpr, NewLoc,
                                    SI->getIterator());
}

/// Inserts a llvm.dbg.value intrinsic before a load of an alloca'd value
/// that has an associated llvm.dbg.declare intrinsic.
void llvm::ConvertDebugDeclareToDebugValue(DbgVariableIntrinsic *DII,
                                           LoadInst *LI, DIBuilder &Builder) {
  auto *DIVar = DII->getVariable();
  auto *DIExpr = DII->getExpression();
  assert(DIVar && "Missing variable");

  if (!valueCoversEntireFragment(LI->getType(), DII)) {
    // FIXME: If only referring to a part of the variable described by the
    // dbg.declare, then we want to insert a dbg.value for the corresponding
    // fragment.
    LLVM_DEBUG(dbgs() << "Failed to convert dbg.declare to dbg.value: "
                      << *DII << '\n');
    return;
  }

  DebugLoc NewLoc = getDebugValueLoc(DII);

  // We are now tracking the loaded value instead of the address. In the
  // future if multi-location support is added to the IR, it might be
  // preferable to keep tracking both the loaded value and the original
  // address in case the alloca can not be elided.
  insertDbgValueOrDbgVariableRecordAfter(Builder, LI, DIVar, DIExpr, NewLoc,
                                         LI->getIterator());
}

void llvm::ConvertDebugDeclareToDebugValue(DbgVariableRecord *DVR,
                                           StoreInst *SI, DIBuilder &Builder) {
  assert(DVR->isAddressOfVariable() || DVR->isDbgAssign());
  auto *DIVar = DVR->getVariable();
  assert(DIVar && "Missing variable");
  auto *DIExpr = DVR->getExpression();
  Value *DV = SI->getValueOperand();

  DebugLoc NewLoc = getDebugValueLoc(DVR);

  // If the alloca describes the variable itself, i.e. the expression in the
  // dbg.declare doesn't start with a dereference, we can perform the
  // conversion if the value covers the entire fragment of DII.
  // If the alloca describes the *address* of DIVar, i.e. DIExpr is
  // *just* a DW_OP_deref, we use DV as is for the dbg.value.
  // We conservatively ignore other dereferences, because the following two are
  // not equivalent:
  //     dbg.declare(alloca, ..., !Expr(deref, plus_uconstant, 2))
  //     dbg.value(DV, ..., !Expr(deref, plus_uconstant, 2))
  // The former is adding 2 to the address of the variable, whereas the latter
  // is adding 2 to the value of the variable. As such, we insist on just a
  // deref expression.
  bool CanConvert =
      DIExpr->isDeref() || (!DIExpr->startsWithDeref() &&
                            valueCoversEntireFragment(DV->getType(), DVR));
  if (CanConvert) {
    insertDbgValueOrDbgVariableRecord(Builder, DV, DIVar, DIExpr, NewLoc,
                                      SI->getIterator());
    return;
  }

  // FIXME: If storing to a part of the variable described by the dbg.declare,
  // then we want to insert a dbg.value for the corresponding fragment.
  LLVM_DEBUG(dbgs() << "Failed to convert dbg.declare to dbg.value: " << *DVR
                    << '\n');
  assert(UseNewDbgInfoFormat);

  // For now, when there is a store to parts of the variable (but we do not
  // know which part) we insert an dbg.value intrinsic to indicate that we
  // know nothing about the variable's content.
  DV = UndefValue::get(DV->getType());
  ValueAsMetadata *DVAM = ValueAsMetadata::get(DV);
  DbgVariableRecord *NewDVR =
      new DbgVariableRecord(DVAM, DIVar, DIExpr, NewLoc.get());
  SI->getParent()->insertDbgRecordBefore(NewDVR, SI->getIterator());
}

/// Inserts a llvm.dbg.value intrinsic after a phi that has an associated
/// llvm.dbg.declare intrinsic.
void llvm::ConvertDebugDeclareToDebugValue(DbgVariableIntrinsic *DII,
                                           PHINode *APN, DIBuilder &Builder) {
  auto *DIVar = DII->getVariable();
  auto *DIExpr = DII->getExpression();
  assert(DIVar && "Missing variable");

  if (PhiHasDebugValue(DIVar, DIExpr, APN))
    return;

  if (!valueCoversEntireFragment(APN->getType(), DII)) {
    // FIXME: If only referring to a part of the variable described by the
    // dbg.declare, then we want to insert a dbg.value for the corresponding
    // fragment.
    LLVM_DEBUG(dbgs() << "Failed to convert dbg.declare to dbg.value: "
                      << *DII << '\n');
    return;
  }

  BasicBlock *BB = APN->getParent();
  auto InsertionPt = BB->getFirstInsertionPt();

  DebugLoc NewLoc = getDebugValueLoc(DII);

  // The block may be a catchswitch block, which does not have a valid
  // insertion point.
  // FIXME: Insert dbg.value markers in the successors when appropriate.
  if (InsertionPt != BB->end()) {
    insertDbgValueOrDbgVariableRecord(Builder, APN, DIVar, DIExpr, NewLoc,
                                      InsertionPt);
  }
}

void llvm::ConvertDebugDeclareToDebugValue(DbgVariableRecord *DVR, LoadInst *LI,
                                           DIBuilder &Builder) {
  auto *DIVar = DVR->getVariable();
  auto *DIExpr = DVR->getExpression();
  assert(DIVar && "Missing variable");

  if (!valueCoversEntireFragment(LI->getType(), DVR)) {
    // FIXME: If only referring to a part of the variable described by the
    // dbg.declare, then we want to insert a DbgVariableRecord for the
    // corresponding fragment.
    LLVM_DEBUG(dbgs() << "Failed to convert dbg.declare to DbgVariableRecord: "
                      << *DVR << '\n');
    return;
  }

  DebugLoc NewLoc = getDebugValueLoc(DVR);

  // We are now tracking the loaded value instead of the address. In the
  // future if multi-location support is added to the IR, it might be
  // preferable to keep tracking both the loaded value and the original
  // address in case the alloca can not be elided.
  assert(UseNewDbgInfoFormat);

  // Create a DbgVariableRecord directly and insert.
  ValueAsMetadata *LIVAM = ValueAsMetadata::get(LI);
  DbgVariableRecord *DV =
      new DbgVariableRecord(LIVAM, DIVar, DIExpr, NewLoc.get());
  LI->getParent()->insertDbgRecordAfter(DV, LI);
}

/// Determine whether this alloca is either a VLA or an array.
static bool isArray(AllocaInst *AI) {
  return AI->isArrayAllocation() ||
         (AI->getAllocatedType() && AI->getAllocatedType()->isArrayTy());
}

/// Determine whether this alloca is a structure.
static bool isStructure(AllocaInst *AI) {
  return AI->getAllocatedType() && AI->getAllocatedType()->isStructTy();
}
void llvm::ConvertDebugDeclareToDebugValue(DbgVariableRecord *DVR, PHINode *APN,
                                           DIBuilder &Builder) {
  auto *DIVar = DVR->getVariable();
  auto *DIExpr = DVR->getExpression();
  assert(DIVar && "Missing variable");

  if (PhiHasDebugValue(DIVar, DIExpr, APN))
    return;

  if (!valueCoversEntireFragment(APN->getType(), DVR)) {
    // FIXME: If only referring to a part of the variable described by the
    // dbg.declare, then we want to insert a DbgVariableRecord for the
    // corresponding fragment.
    LLVM_DEBUG(dbgs() << "Failed to convert dbg.declare to DbgVariableRecord: "
                      << *DVR << '\n');
    return;
  }

  BasicBlock *BB = APN->getParent();
  auto InsertionPt = BB->getFirstInsertionPt();

  DebugLoc NewLoc = getDebugValueLoc(DVR);

  // The block may be a catchswitch block, which does not have a valid
  // insertion point.
  // FIXME: Insert DbgVariableRecord markers in the successors when appropriate.
  if (InsertionPt != BB->end()) {
    insertDbgValueOrDbgVariableRecord(Builder, APN, DIVar, DIExpr, NewLoc,
                                      InsertionPt);
  }
}

/// LowerDbgDeclare - Lowers llvm.dbg.declare intrinsics into appropriate set
/// of llvm.dbg.value intrinsics.
bool llvm::LowerDbgDeclare(Function &F) {
  bool Changed = false;
  DIBuilder DIB(*F.getParent(), /*AllowUnresolved*/ false);
  SmallVector<DbgDeclareInst *, 4> Dbgs;
  SmallVector<DbgVariableRecord *> DVRs;
  for (auto &FI : F) {
    for (Instruction &BI : FI) {
      if (auto *DDI = dyn_cast<DbgDeclareInst>(&BI))
        Dbgs.push_back(DDI);
      for (DbgVariableRecord &DVR : filterDbgVars(BI.getDbgRecordRange())) {
        if (DVR.getType() == DbgVariableRecord::LocationType::Declare)
          DVRs.push_back(&DVR);
      }
    }
  }

  if (Dbgs.empty() && DVRs.empty())
    return Changed;

  auto LowerOne = [&](auto *DDI) {
    AllocaInst *AI =
        dyn_cast_or_null<AllocaInst>(DDI->getVariableLocationOp(0));
    // If this is an alloca for a scalar variable, insert a dbg.value
    // at each load and store to the alloca and erase the dbg.declare.
    // The dbg.values allow tracking a variable even if it is not
    // stored on the stack, while the dbg.declare can only describe
    // the stack slot (and at a lexical-scope granularity). Later
    // passes will attempt to elide the stack slot.
    if (!AI || isArray(AI) || isStructure(AI))
      return;

    // A volatile load/store means that the alloca can't be elided anyway.
    if (llvm::any_of(AI->users(), [](User *U) -> bool {
          if (LoadInst *LI = dyn_cast<LoadInst>(U))
            return LI->isVolatile();
          if (StoreInst *SI = dyn_cast<StoreInst>(U))
            return SI->isVolatile();
          return false;
        }))
      return;

    SmallVector<const Value *, 8> WorkList;
    WorkList.push_back(AI);
    while (!WorkList.empty()) {
      const Value *V = WorkList.pop_back_val();
      for (const auto &AIUse : V->uses()) {
        User *U = AIUse.getUser();
        if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
          if (AIUse.getOperandNo() == 1)
            ConvertDebugDeclareToDebugValue(DDI, SI, DIB);
        } else if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
          ConvertDebugDeclareToDebugValue(DDI, LI, DIB);
        } else if (CallInst *CI = dyn_cast<CallInst>(U)) {
          // This is a call by-value or some other instruction that takes a
          // pointer to the variable. Insert a *value* intrinsic that describes
          // the variable by dereferencing the alloca.
          if (!CI->isLifetimeStartOrEnd()) {
            DebugLoc NewLoc = getDebugValueLoc(DDI);
            auto *DerefExpr =
                DIExpression::append(DDI->getExpression(), dwarf::DW_OP_deref);
            insertDbgValueOrDbgVariableRecord(DIB, AI, DDI->getVariable(),
                                              DerefExpr, NewLoc,
                                              CI->getIterator());
          }
        } else if (BitCastInst *BI = dyn_cast<BitCastInst>(U)) {
          if (BI->getType()->isPointerTy())
            WorkList.push_back(BI);
        }
      }
    }
    DDI->eraseFromParent();
    Changed = true;
  };

  for_each(Dbgs, LowerOne);
  for_each(DVRs, LowerOne);

  if (Changed)
    for (BasicBlock &BB : F)
      RemoveRedundantDbgInstrs(&BB);

  return Changed;
}

// RemoveDIs: re-implementation of insertDebugValuesForPHIs, but which pulls the
// debug-info out of the block's DbgVariableRecords rather than dbg.value
// intrinsics.
static void
insertDbgVariableRecordsForPHIs(BasicBlock *BB,
                                SmallVectorImpl<PHINode *> &InsertedPHIs) {
  assert(BB && "No BasicBlock to clone DbgVariableRecord(s) from.");
  if (InsertedPHIs.size() == 0)
    return;

  // Map existing PHI nodes to their DbgVariableRecords.
  DenseMap<Value *, DbgVariableRecord *> DbgValueMap;
  for (auto &I : *BB) {
    for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
      for (Value *V : DVR.location_ops())
        if (auto *Loc = dyn_cast_or_null<PHINode>(V))
          DbgValueMap.insert({Loc, &DVR});
    }
  }
  if (DbgValueMap.size() == 0)
    return;

  // Map a pair of the destination BB and old DbgVariableRecord to the new
  // DbgVariableRecord, so that if a DbgVariableRecord is being rewritten to use
  // more than one of the inserted PHIs in the same destination BB, we can
  // update the same DbgVariableRecord with all the new PHIs instead of creating
  // one copy for each.
  MapVector<std::pair<BasicBlock *, DbgVariableRecord *>, DbgVariableRecord *>
      NewDbgValueMap;
  // Then iterate through the new PHIs and look to see if they use one of the
  // previously mapped PHIs. If so, create a new DbgVariableRecord that will
  // propagate the info through the new PHI. If we use more than one new PHI in
  // a single destination BB with the same old dbg.value, merge the updates so
  // that we get a single new DbgVariableRecord with all the new PHIs.
  for (auto PHI : InsertedPHIs) {
    BasicBlock *Parent = PHI->getParent();
    // Avoid inserting a debug-info record into an EH block.
    if (Parent->getFirstNonPHI()->isEHPad())
      continue;
    for (auto VI : PHI->operand_values()) {
      auto V = DbgValueMap.find(VI);
      if (V != DbgValueMap.end()) {
        DbgVariableRecord *DbgII = cast<DbgVariableRecord>(V->second);
        auto NewDI = NewDbgValueMap.find({Parent, DbgII});
        if (NewDI == NewDbgValueMap.end()) {
          DbgVariableRecord *NewDbgII = DbgII->clone();
          NewDI = NewDbgValueMap.insert({{Parent, DbgII}, NewDbgII}).first;
        }
        DbgVariableRecord *NewDbgII = NewDI->second;
        // If PHI contains VI as an operand more than once, we may
        // replaced it in NewDbgII; confirm that it is present.
        if (is_contained(NewDbgII->location_ops(), VI))
          NewDbgII->replaceVariableLocationOp(VI, PHI);
      }
    }
  }
  // Insert the new DbgVariableRecords into their destination blocks.
  for (auto DI : NewDbgValueMap) {
    BasicBlock *Parent = DI.first.first;
    DbgVariableRecord *NewDbgII = DI.second;
    auto InsertionPt = Parent->getFirstInsertionPt();
    assert(InsertionPt != Parent->end() && "Ill-formed basic block");

    Parent->insertDbgRecordBefore(NewDbgII, InsertionPt);
  }
}

/// Propagate dbg.value intrinsics through the newly inserted PHIs.
void llvm::insertDebugValuesForPHIs(BasicBlock *BB,
                                    SmallVectorImpl<PHINode *> &InsertedPHIs) {
  assert(BB && "No BasicBlock to clone dbg.value(s) from.");
  if (InsertedPHIs.size() == 0)
    return;

  insertDbgVariableRecordsForPHIs(BB, InsertedPHIs);

  // Map existing PHI nodes to their dbg.values.
  ValueToValueMapTy DbgValueMap;
  for (auto &I : *BB) {
    if (auto DbgII = dyn_cast<DbgVariableIntrinsic>(&I)) {
      for (Value *V : DbgII->location_ops())
        if (auto *Loc = dyn_cast_or_null<PHINode>(V))
          DbgValueMap.insert({Loc, DbgII});
    }
  }
  if (DbgValueMap.size() == 0)
    return;

  // Map a pair of the destination BB and old dbg.value to the new dbg.value,
  // so that if a dbg.value is being rewritten to use more than one of the
  // inserted PHIs in the same destination BB, we can update the same dbg.value
  // with all the new PHIs instead of creating one copy for each.
  MapVector<std::pair<BasicBlock *, DbgVariableIntrinsic *>,
            DbgVariableIntrinsic *>
      NewDbgValueMap;
  // Then iterate through the new PHIs and look to see if they use one of the
  // previously mapped PHIs. If so, create a new dbg.value intrinsic that will
  // propagate the info through the new PHI. If we use more than one new PHI in
  // a single destination BB with the same old dbg.value, merge the updates so
  // that we get a single new dbg.value with all the new PHIs.
  for (auto *PHI : InsertedPHIs) {
    BasicBlock *Parent = PHI->getParent();
    // Avoid inserting an intrinsic into an EH block.
    if (Parent->getFirstNonPHI()->isEHPad())
      continue;
    for (auto *VI : PHI->operand_values()) {
      auto V = DbgValueMap.find(VI);
      if (V != DbgValueMap.end()) {
        auto *DbgII = cast<DbgVariableIntrinsic>(V->second);
        auto NewDI = NewDbgValueMap.find({Parent, DbgII});
        if (NewDI == NewDbgValueMap.end()) {
          auto *NewDbgII = cast<DbgVariableIntrinsic>(DbgII->clone());
          NewDI = NewDbgValueMap.insert({{Parent, DbgII}, NewDbgII}).first;
        }
        DbgVariableIntrinsic *NewDbgII = NewDI->second;
        // If PHI contains VI as an operand more than once, we may
        // replaced it in NewDbgII; confirm that it is present.
        if (is_contained(NewDbgII->location_ops(), VI))
          NewDbgII->replaceVariableLocationOp(VI, PHI);
      }
    }
  }
  // Insert thew new dbg.values into their destination blocks.
  for (auto DI : NewDbgValueMap) {
    BasicBlock *Parent = DI.first.first;
    auto *NewDbgII = DI.second;
    auto InsertionPt = Parent->getFirstInsertionPt();
    assert(InsertionPt != Parent->end() && "Ill-formed basic block");
    NewDbgII->insertBefore(&*InsertionPt);
  }
}

bool llvm::replaceDbgDeclare(Value *Address, Value *NewAddress,
                             DIBuilder &Builder, uint8_t DIExprFlags,
                             int Offset) {
  TinyPtrVector<DbgDeclareInst *> DbgDeclares = findDbgDeclares(Address);
  TinyPtrVector<DbgVariableRecord *> DVRDeclares = findDVRDeclares(Address);

  auto ReplaceOne = [&](auto *DII) {
    assert(DII->getVariable() && "Missing variable");
    auto *DIExpr = DII->getExpression();
    DIExpr = DIExpression::prepend(DIExpr, DIExprFlags, Offset);
    DII->setExpression(DIExpr);
    DII->replaceVariableLocationOp(Address, NewAddress);
  };

  for_each(DbgDeclares, ReplaceOne);
  for_each(DVRDeclares, ReplaceOne);

  return !DbgDeclares.empty() || !DVRDeclares.empty();
}

static void updateOneDbgValueForAlloca(const DebugLoc &Loc,
                                       DILocalVariable *DIVar,
                                       DIExpression *DIExpr, Value *NewAddress,
                                       DbgValueInst *DVI,
                                       DbgVariableRecord *DVR,
                                       DIBuilder &Builder, int Offset) {
  assert(DIVar && "Missing variable");

  // This is an alloca-based dbg.value/DbgVariableRecord. The first thing it
  // should do with the alloca pointer is dereference it. Otherwise we don't
  // know how to handle it and give up.
  if (!DIExpr || DIExpr->getNumElements() < 1 ||
      DIExpr->getElement(0) != dwarf::DW_OP_deref)
    return;

  // Insert the offset before the first deref.
  if (Offset)
    DIExpr = DIExpression::prepend(DIExpr, 0, Offset);

  if (DVI) {
    DVI->setExpression(DIExpr);
    DVI->replaceVariableLocationOp(0u, NewAddress);
  } else {
    assert(DVR);
    DVR->setExpression(DIExpr);
    DVR->replaceVariableLocationOp(0u, NewAddress);
  }
}

void llvm::replaceDbgValueForAlloca(AllocaInst *AI, Value *NewAllocaAddress,
                                    DIBuilder &Builder, int Offset) {
  SmallVector<DbgValueInst *, 1> DbgUsers;
  SmallVector<DbgVariableRecord *, 1> DPUsers;
  findDbgValues(DbgUsers, AI, &DPUsers);

  // Attempt to replace dbg.values that use this alloca.
  for (auto *DVI : DbgUsers)
    updateOneDbgValueForAlloca(DVI->getDebugLoc(), DVI->getVariable(),
                               DVI->getExpression(), NewAllocaAddress, DVI,
                               nullptr, Builder, Offset);

  // Replace any DbgVariableRecords that use this alloca.
  for (DbgVariableRecord *DVR : DPUsers)
    updateOneDbgValueForAlloca(DVR->getDebugLoc(), DVR->getVariable(),
                               DVR->getExpression(), NewAllocaAddress, nullptr,
                               DVR, Builder, Offset);
}

/// Where possible to salvage debug information for \p I do so.
/// If not possible mark undef.
void llvm::salvageDebugInfo(Instruction &I) {
  SmallVector<DbgVariableIntrinsic *, 1> DbgUsers;
  SmallVector<DbgVariableRecord *, 1> DPUsers;
  findDbgUsers(DbgUsers, &I, &DPUsers);
  salvageDebugInfoForDbgValues(I, DbgUsers, DPUsers);
}

template <typename T> static void salvageDbgAssignAddress(T *Assign) {
  Instruction *I = dyn_cast<Instruction>(Assign->getAddress());
  // Only instructions can be salvaged at the moment.
  if (!I)
    return;

  assert(!Assign->getAddressExpression()->getFragmentInfo().has_value() &&
         "address-expression shouldn't have fragment info");

  // The address component of a dbg.assign cannot be variadic.
  uint64_t CurrentLocOps = 0;
  SmallVector<Value *, 4> AdditionalValues;
  SmallVector<uint64_t, 16> Ops;
  Value *NewV = salvageDebugInfoImpl(*I, CurrentLocOps, Ops, AdditionalValues);

  // Check if the salvage failed.
  if (!NewV)
    return;

  DIExpression *SalvagedExpr = DIExpression::appendOpsToArg(
      Assign->getAddressExpression(), Ops, 0, /*StackValue=*/false);
  assert(!SalvagedExpr->getFragmentInfo().has_value() &&
         "address-expression shouldn't have fragment info");

  SalvagedExpr = SalvagedExpr->foldConstantMath();

  // Salvage succeeds if no additional values are required.
  if (AdditionalValues.empty()) {
    Assign->setAddress(NewV);
    Assign->setAddressExpression(SalvagedExpr);
  } else {
    Assign->setKillAddress();
  }
}

void llvm::salvageDebugInfoForDbgValues(
    Instruction &I, ArrayRef<DbgVariableIntrinsic *> DbgUsers,
    ArrayRef<DbgVariableRecord *> DPUsers) {
  // These are arbitrary chosen limits on the maximum number of values and the
  // maximum size of a debug expression we can salvage up to, used for
  // performance reasons.
  const unsigned MaxDebugArgs = 16;
  const unsigned MaxExpressionSize = 128;
  bool Salvaged = false;

  for (auto *DII : DbgUsers) {
    if (auto *DAI = dyn_cast<DbgAssignIntrinsic>(DII)) {
      if (DAI->getAddress() == &I) {
        salvageDbgAssignAddress(DAI);
        Salvaged = true;
      }
      if (DAI->getValue() != &I)
        continue;
    }

    // Do not add DW_OP_stack_value for DbgDeclare, because they are implicitly
    // pointing out the value as a DWARF memory location description.
    bool StackValue = isa<DbgValueInst>(DII);
    auto DIILocation = DII->location_ops();
    assert(
        is_contained(DIILocation, &I) &&
        "DbgVariableIntrinsic must use salvaged instruction as its location");
    SmallVector<Value *, 4> AdditionalValues;
    // `I` may appear more than once in DII's location ops, and each use of `I`
    // must be updated in the DIExpression and potentially have additional
    // values added; thus we call salvageDebugInfoImpl for each `I` instance in
    // DIILocation.
    Value *Op0 = nullptr;
    DIExpression *SalvagedExpr = DII->getExpression();
    auto LocItr = find(DIILocation, &I);
    while (SalvagedExpr && LocItr != DIILocation.end()) {
      SmallVector<uint64_t, 16> Ops;
      unsigned LocNo = std::distance(DIILocation.begin(), LocItr);
      uint64_t CurrentLocOps = SalvagedExpr->getNumLocationOperands();
      Op0 = salvageDebugInfoImpl(I, CurrentLocOps, Ops, AdditionalValues);
      if (!Op0)
        break;
      SalvagedExpr =
          DIExpression::appendOpsToArg(SalvagedExpr, Ops, LocNo, StackValue);
      LocItr = std::find(++LocItr, DIILocation.end(), &I);
    }
    // salvageDebugInfoImpl should fail on examining the first element of
    // DbgUsers, or none of them.
    if (!Op0)
      break;

    SalvagedExpr = SalvagedExpr->foldConstantMath();
    DII->replaceVariableLocationOp(&I, Op0);
    bool IsValidSalvageExpr = SalvagedExpr->getNumElements() <= MaxExpressionSize;
    if (AdditionalValues.empty() && IsValidSalvageExpr) {
      DII->setExpression(SalvagedExpr);
    } else if (isa<DbgValueInst>(DII) && IsValidSalvageExpr &&
               DII->getNumVariableLocationOps() + AdditionalValues.size() <=
                   MaxDebugArgs) {
      DII->addVariableLocationOps(AdditionalValues, SalvagedExpr);
    } else {
      // Do not salvage using DIArgList for dbg.declare, as it is not currently
      // supported in those instructions. Also do not salvage if the resulting
      // DIArgList would contain an unreasonably large number of values.
      DII->setKillLocation();
    }
    LLVM_DEBUG(dbgs() << "SALVAGE: " << *DII << '\n');
    Salvaged = true;
  }
  // Duplicate of above block for DbgVariableRecords.
  for (auto *DVR : DPUsers) {
    if (DVR->isDbgAssign()) {
      if (DVR->getAddress() == &I) {
        salvageDbgAssignAddress(DVR);
        Salvaged = true;
      }
      if (DVR->getValue() != &I)
        continue;
    }

    // Do not add DW_OP_stack_value for DbgDeclare and DbgAddr, because they
    // are implicitly pointing out the value as a DWARF memory location
    // description.
    bool StackValue =
        DVR->getType() != DbgVariableRecord::LocationType::Declare;
    auto DVRLocation = DVR->location_ops();
    assert(
        is_contained(DVRLocation, &I) &&
        "DbgVariableIntrinsic must use salvaged instruction as its location");
    SmallVector<Value *, 4> AdditionalValues;
    // 'I' may appear more than once in DVR's location ops, and each use of 'I'
    // must be updated in the DIExpression and potentially have additional
    // values added; thus we call salvageDebugInfoImpl for each 'I' instance in
    // DVRLocation.
    Value *Op0 = nullptr;
    DIExpression *SalvagedExpr = DVR->getExpression();
    auto LocItr = find(DVRLocation, &I);
    while (SalvagedExpr && LocItr != DVRLocation.end()) {
      SmallVector<uint64_t, 16> Ops;
      unsigned LocNo = std::distance(DVRLocation.begin(), LocItr);
      uint64_t CurrentLocOps = SalvagedExpr->getNumLocationOperands();
      Op0 = salvageDebugInfoImpl(I, CurrentLocOps, Ops, AdditionalValues);
      if (!Op0)
        break;
      SalvagedExpr =
          DIExpression::appendOpsToArg(SalvagedExpr, Ops, LocNo, StackValue);
      LocItr = std::find(++LocItr, DVRLocation.end(), &I);
    }
    // salvageDebugInfoImpl should fail on examining the first element of
    // DbgUsers, or none of them.
    if (!Op0)
      break;

    SalvagedExpr = SalvagedExpr->foldConstantMath();
    DVR->replaceVariableLocationOp(&I, Op0);
    bool IsValidSalvageExpr =
        SalvagedExpr->getNumElements() <= MaxExpressionSize;
    if (AdditionalValues.empty() && IsValidSalvageExpr) {
      DVR->setExpression(SalvagedExpr);
    } else if (DVR->getType() != DbgVariableRecord::LocationType::Declare &&
               IsValidSalvageExpr &&
               DVR->getNumVariableLocationOps() + AdditionalValues.size() <=
                   MaxDebugArgs) {
      DVR->addVariableLocationOps(AdditionalValues, SalvagedExpr);
    } else {
      // Do not salvage using DIArgList for dbg.addr/dbg.declare, as it is
      // currently only valid for stack value expressions.
      // Also do not salvage if the resulting DIArgList would contain an
      // unreasonably large number of values.
      DVR->setKillLocation();
    }
    LLVM_DEBUG(dbgs() << "SALVAGE: " << DVR << '\n');
    Salvaged = true;
  }

  if (Salvaged)
    return;

  for (auto *DII : DbgUsers)
    DII->setKillLocation();

  for (auto *DVR : DPUsers)
    DVR->setKillLocation();
}

Value *getSalvageOpsForGEP(GetElementPtrInst *GEP, const DataLayout &DL,
                           uint64_t CurrentLocOps,
                           SmallVectorImpl<uint64_t> &Opcodes,
                           SmallVectorImpl<Value *> &AdditionalValues) {
  unsigned BitWidth = DL.getIndexSizeInBits(GEP->getPointerAddressSpace());
  // Rewrite a GEP into a DIExpression.
  MapVector<Value *, APInt> VariableOffsets;
  APInt ConstantOffset(BitWidth, 0);
  if (!GEP->collectOffset(DL, BitWidth, VariableOffsets, ConstantOffset))
    return nullptr;
  if (!VariableOffsets.empty() && !CurrentLocOps) {
    Opcodes.insert(Opcodes.begin(), {dwarf::DW_OP_LLVM_arg, 0});
    CurrentLocOps = 1;
  }
  for (const auto &Offset : VariableOffsets) {
    AdditionalValues.push_back(Offset.first);
    assert(Offset.second.isStrictlyPositive() &&
           "Expected strictly positive multiplier for offset.");
    Opcodes.append({dwarf::DW_OP_LLVM_arg, CurrentLocOps++, dwarf::DW_OP_constu,
                    Offset.second.getZExtValue(), dwarf::DW_OP_mul,
                    dwarf::DW_OP_plus});
  }
  DIExpression::appendOffset(Opcodes, ConstantOffset.getSExtValue());
  return GEP->getOperand(0);
}

uint64_t getDwarfOpForBinOp(Instruction::BinaryOps Opcode) {
  switch (Opcode) {
  case Instruction::Add:
    return dwarf::DW_OP_plus;
  case Instruction::Sub:
    return dwarf::DW_OP_minus;
  case Instruction::Mul:
    return dwarf::DW_OP_mul;
  case Instruction::SDiv:
    return dwarf::DW_OP_div;
  case Instruction::SRem:
    return dwarf::DW_OP_mod;
  case Instruction::Or:
    return dwarf::DW_OP_or;
  case Instruction::And:
    return dwarf::DW_OP_and;
  case Instruction::Xor:
    return dwarf::DW_OP_xor;
  case Instruction::Shl:
    return dwarf::DW_OP_shl;
  case Instruction::LShr:
    return dwarf::DW_OP_shr;
  case Instruction::AShr:
    return dwarf::DW_OP_shra;
  default:
    // TODO: Salvage from each kind of binop we know about.
    return 0;
  }
}

static void handleSSAValueOperands(uint64_t CurrentLocOps,
                                   SmallVectorImpl<uint64_t> &Opcodes,
                                   SmallVectorImpl<Value *> &AdditionalValues,
                                   Instruction *I) {
  if (!CurrentLocOps) {
    Opcodes.append({dwarf::DW_OP_LLVM_arg, 0});
    CurrentLocOps = 1;
  }
  Opcodes.append({dwarf::DW_OP_LLVM_arg, CurrentLocOps});
  AdditionalValues.push_back(I->getOperand(1));
}

Value *getSalvageOpsForBinOp(BinaryOperator *BI, uint64_t CurrentLocOps,
                             SmallVectorImpl<uint64_t> &Opcodes,
                             SmallVectorImpl<Value *> &AdditionalValues) {
  // Handle binary operations with constant integer operands as a special case.
  auto *ConstInt = dyn_cast<ConstantInt>(BI->getOperand(1));
  // Values wider than 64 bits cannot be represented within a DIExpression.
  if (ConstInt && ConstInt->getBitWidth() > 64)
    return nullptr;

  Instruction::BinaryOps BinOpcode = BI->getOpcode();
  // Push any Constant Int operand onto the expression stack.
  if (ConstInt) {
    uint64_t Val = ConstInt->getSExtValue();
    // Add or Sub Instructions with a constant operand can potentially be
    // simplified.
    if (BinOpcode == Instruction::Add || BinOpcode == Instruction::Sub) {
      uint64_t Offset = BinOpcode == Instruction::Add ? Val : -int64_t(Val);
      DIExpression::appendOffset(Opcodes, Offset);
      return BI->getOperand(0);
    }
    Opcodes.append({dwarf::DW_OP_constu, Val});
  } else {
    handleSSAValueOperands(CurrentLocOps, Opcodes, AdditionalValues, BI);
  }

  // Add salvaged binary operator to expression stack, if it has a valid
  // representation in a DIExpression.
  uint64_t DwarfBinOp = getDwarfOpForBinOp(BinOpcode);
  if (!DwarfBinOp)
    return nullptr;
  Opcodes.push_back(DwarfBinOp);
  return BI->getOperand(0);
}

uint64_t getDwarfOpForIcmpPred(CmpInst::Predicate Pred) {
  // The signedness of the operation is implicit in the typed stack, signed and
  // unsigned instructions map to the same DWARF opcode.
  switch (Pred) {
  case CmpInst::ICMP_EQ:
    return dwarf::DW_OP_eq;
  case CmpInst::ICMP_NE:
    return dwarf::DW_OP_ne;
  case CmpInst::ICMP_UGT:
  case CmpInst::ICMP_SGT:
    return dwarf::DW_OP_gt;
  case CmpInst::ICMP_UGE:
  case CmpInst::ICMP_SGE:
    return dwarf::DW_OP_ge;
  case CmpInst::ICMP_ULT:
  case CmpInst::ICMP_SLT:
    return dwarf::DW_OP_lt;
  case CmpInst::ICMP_ULE:
  case CmpInst::ICMP_SLE:
    return dwarf::DW_OP_le;
  default:
    return 0;
  }
}

Value *getSalvageOpsForIcmpOp(ICmpInst *Icmp, uint64_t CurrentLocOps,
                              SmallVectorImpl<uint64_t> &Opcodes,
                              SmallVectorImpl<Value *> &AdditionalValues) {
  // Handle icmp operations with constant integer operands as a special case.
  auto *ConstInt = dyn_cast<ConstantInt>(Icmp->getOperand(1));
  // Values wider than 64 bits cannot be represented within a DIExpression.
  if (ConstInt && ConstInt->getBitWidth() > 64)
    return nullptr;
  // Push any Constant Int operand onto the expression stack.
  if (ConstInt) {
    if (Icmp->isSigned())
      Opcodes.push_back(dwarf::DW_OP_consts);
    else
      Opcodes.push_back(dwarf::DW_OP_constu);
    uint64_t Val = ConstInt->getSExtValue();
    Opcodes.push_back(Val);
  } else {
    handleSSAValueOperands(CurrentLocOps, Opcodes, AdditionalValues, Icmp);
  }

  // Add salvaged binary operator to expression stack, if it has a valid
  // representation in a DIExpression.
  uint64_t DwarfIcmpOp = getDwarfOpForIcmpPred(Icmp->getPredicate());
  if (!DwarfIcmpOp)
    return nullptr;
  Opcodes.push_back(DwarfIcmpOp);
  return Icmp->getOperand(0);
}

Value *llvm::salvageDebugInfoImpl(Instruction &I, uint64_t CurrentLocOps,
                                  SmallVectorImpl<uint64_t> &Ops,
                                  SmallVectorImpl<Value *> &AdditionalValues) {
  auto &M = *I.getModule();
  auto &DL = M.getDataLayout();

  if (auto *CI = dyn_cast<CastInst>(&I)) {
    Value *FromValue = CI->getOperand(0);
    // No-op casts are irrelevant for debug info.
    if (CI->isNoopCast(DL)) {
      return FromValue;
    }

    Type *Type = CI->getType();
    if (Type->isPointerTy())
      Type = DL.getIntPtrType(Type);
    // Casts other than Trunc, SExt, or ZExt to scalar types cannot be salvaged.
    if (Type->isVectorTy() ||
        !(isa<TruncInst>(&I) || isa<SExtInst>(&I) || isa<ZExtInst>(&I) ||
          isa<IntToPtrInst>(&I) || isa<PtrToIntInst>(&I)))
      return nullptr;

    llvm::Type *FromType = FromValue->getType();
    if (FromType->isPointerTy())
      FromType = DL.getIntPtrType(FromType);

    unsigned FromTypeBitSize = FromType->getScalarSizeInBits();
    unsigned ToTypeBitSize = Type->getScalarSizeInBits();

    auto ExtOps = DIExpression::getExtOps(FromTypeBitSize, ToTypeBitSize,
                                          isa<SExtInst>(&I));
    Ops.append(ExtOps.begin(), ExtOps.end());
    return FromValue;
  }

  if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
    return getSalvageOpsForGEP(GEP, DL, CurrentLocOps, Ops, AdditionalValues);
  if (auto *BI = dyn_cast<BinaryOperator>(&I))
    return getSalvageOpsForBinOp(BI, CurrentLocOps, Ops, AdditionalValues);
  if (auto *IC = dyn_cast<ICmpInst>(&I))
    return getSalvageOpsForIcmpOp(IC, CurrentLocOps, Ops, AdditionalValues);

  // *Not* to do: we should not attempt to salvage load instructions,
  // because the validity and lifetime of a dbg.value containing
  // DW_OP_deref becomes difficult to analyze. See PR40628 for examples.
  return nullptr;
}

/// A replacement for a dbg.value expression.
using DbgValReplacement = std::optional<DIExpression *>;

/// Point debug users of \p From to \p To using exprs given by \p RewriteExpr,
/// possibly moving/undefing users to prevent use-before-def. Returns true if
/// changes are made.
static bool rewriteDebugUsers(
    Instruction &From, Value &To, Instruction &DomPoint, DominatorTree &DT,
    function_ref<DbgValReplacement(DbgVariableIntrinsic &DII)> RewriteExpr,
    function_ref<DbgValReplacement(DbgVariableRecord &DVR)> RewriteDVRExpr) {
  // Find debug users of From.
  SmallVector<DbgVariableIntrinsic *, 1> Users;
  SmallVector<DbgVariableRecord *, 1> DPUsers;
  findDbgUsers(Users, &From, &DPUsers);
  if (Users.empty() && DPUsers.empty())
    return false;

  // Prevent use-before-def of To.
  bool Changed = false;

  SmallPtrSet<DbgVariableIntrinsic *, 1> UndefOrSalvage;
  SmallPtrSet<DbgVariableRecord *, 1> UndefOrSalvageDVR;
  if (isa<Instruction>(&To)) {
    bool DomPointAfterFrom = From.getNextNonDebugInstruction() == &DomPoint;

    for (auto *DII : Users) {
      // It's common to see a debug user between From and DomPoint. Move it
      // after DomPoint to preserve the variable update without any reordering.
      if (DomPointAfterFrom && DII->getNextNonDebugInstruction() == &DomPoint) {
        LLVM_DEBUG(dbgs() << "MOVE:  " << *DII << '\n');
        DII->moveAfter(&DomPoint);
        Changed = true;

      // Users which otherwise aren't dominated by the replacement value must
      // be salvaged or deleted.
      } else if (!DT.dominates(&DomPoint, DII)) {
        UndefOrSalvage.insert(DII);
      }
    }

    // DbgVariableRecord implementation of the above.
    for (auto *DVR : DPUsers) {
      Instruction *MarkedInstr = DVR->getMarker()->MarkedInstr;
      Instruction *NextNonDebug = MarkedInstr;
      // The next instruction might still be a dbg.declare, skip over it.
      if (isa<DbgVariableIntrinsic>(NextNonDebug))
        NextNonDebug = NextNonDebug->getNextNonDebugInstruction();

      if (DomPointAfterFrom && NextNonDebug == &DomPoint) {
        LLVM_DEBUG(dbgs() << "MOVE:  " << *DVR << '\n');
        DVR->removeFromParent();
        // Ensure there's a marker.
        DomPoint.getParent()->insertDbgRecordAfter(DVR, &DomPoint);
        Changed = true;
      } else if (!DT.dominates(&DomPoint, MarkedInstr)) {
        UndefOrSalvageDVR.insert(DVR);
      }
    }
  }

  // Update debug users without use-before-def risk.
  for (auto *DII : Users) {
    if (UndefOrSalvage.count(DII))
      continue;

    DbgValReplacement DVRepl = RewriteExpr(*DII);
    if (!DVRepl)
      continue;

    DII->replaceVariableLocationOp(&From, &To);
    DII->setExpression(*DVRepl);
    LLVM_DEBUG(dbgs() << "REWRITE:  " << *DII << '\n');
    Changed = true;
  }
  for (auto *DVR : DPUsers) {
    if (UndefOrSalvageDVR.count(DVR))
      continue;

    DbgValReplacement DVRepl = RewriteDVRExpr(*DVR);
    if (!DVRepl)
      continue;

    DVR->replaceVariableLocationOp(&From, &To);
    DVR->setExpression(*DVRepl);
    LLVM_DEBUG(dbgs() << "REWRITE:  " << DVR << '\n');
    Changed = true;
  }

  if (!UndefOrSalvage.empty() || !UndefOrSalvageDVR.empty()) {
    // Try to salvage the remaining debug users.
    salvageDebugInfo(From);
    Changed = true;
  }

  return Changed;
}

/// Check if a bitcast between a value of type \p FromTy to type \p ToTy would
/// losslessly preserve the bits and semantics of the value. This predicate is
/// symmetric, i.e swapping \p FromTy and \p ToTy should give the same result.
///
/// Note that Type::canLosslesslyBitCastTo is not suitable here because it
/// allows semantically unequivalent bitcasts, such as <2 x i64> -> <4 x i32>,
/// and also does not allow lossless pointer <-> integer conversions.
static bool isBitCastSemanticsPreserving(const DataLayout &DL, Type *FromTy,
                                         Type *ToTy) {
  // Trivially compatible types.
  if (FromTy == ToTy)
    return true;

  // Handle compatible pointer <-> integer conversions.
  if (FromTy->isIntOrPtrTy() && ToTy->isIntOrPtrTy()) {
    bool SameSize = DL.getTypeSizeInBits(FromTy) == DL.getTypeSizeInBits(ToTy);
    bool LosslessConversion = !DL.isNonIntegralPointerType(FromTy) &&
                              !DL.isNonIntegralPointerType(ToTy);
    return SameSize && LosslessConversion;
  }

  // TODO: This is not exhaustive.
  return false;
}

bool llvm::replaceAllDbgUsesWith(Instruction &From, Value &To,
                                 Instruction &DomPoint, DominatorTree &DT) {
  // Exit early if From has no debug users.
  if (!From.isUsedByMetadata())
    return false;

  assert(&From != &To && "Can't replace something with itself");

  Type *FromTy = From.getType();
  Type *ToTy = To.getType();

  auto Identity = [&](DbgVariableIntrinsic &DII) -> DbgValReplacement {
    return DII.getExpression();
  };
  auto IdentityDVR = [&](DbgVariableRecord &DVR) -> DbgValReplacement {
    return DVR.getExpression();
  };

  // Handle no-op conversions.
  Module &M = *From.getModule();
  const DataLayout &DL = M.getDataLayout();
  if (isBitCastSemanticsPreserving(DL, FromTy, ToTy))
    return rewriteDebugUsers(From, To, DomPoint, DT, Identity, IdentityDVR);

  // Handle integer-to-integer widening and narrowing.
  // FIXME: Use DW_OP_convert when it's available everywhere.
  if (FromTy->isIntegerTy() && ToTy->isIntegerTy()) {
    uint64_t FromBits = FromTy->getPrimitiveSizeInBits();
    uint64_t ToBits = ToTy->getPrimitiveSizeInBits();
    assert(FromBits != ToBits && "Unexpected no-op conversion");

    // When the width of the result grows, assume that a debugger will only
    // access the low `FromBits` bits when inspecting the source variable.
    if (FromBits < ToBits)
      return rewriteDebugUsers(From, To, DomPoint, DT, Identity, IdentityDVR);

    // The width of the result has shrunk. Use sign/zero extension to describe
    // the source variable's high bits.
    auto SignOrZeroExt = [&](DbgVariableIntrinsic &DII) -> DbgValReplacement {
      DILocalVariable *Var = DII.getVariable();

      // Without knowing signedness, sign/zero extension isn't possible.
      auto Signedness = Var->getSignedness();
      if (!Signedness)
        return std::nullopt;

      bool Signed = *Signedness == DIBasicType::Signedness::Signed;
      return DIExpression::appendExt(DII.getExpression(), ToBits, FromBits,
                                     Signed);
    };
    // RemoveDIs: duplicate implementation working on DbgVariableRecords rather
    // than on dbg.value intrinsics.
    auto SignOrZeroExtDVR = [&](DbgVariableRecord &DVR) -> DbgValReplacement {
      DILocalVariable *Var = DVR.getVariable();

      // Without knowing signedness, sign/zero extension isn't possible.
      auto Signedness = Var->getSignedness();
      if (!Signedness)
        return std::nullopt;

      bool Signed = *Signedness == DIBasicType::Signedness::Signed;
      return DIExpression::appendExt(DVR.getExpression(), ToBits, FromBits,
                                     Signed);
    };
    return rewriteDebugUsers(From, To, DomPoint, DT, SignOrZeroExt,
                             SignOrZeroExtDVR);
  }

  // TODO: Floating-point conversions, vectors.
  return false;
}

bool llvm::handleUnreachableTerminator(
    Instruction *I, SmallVectorImpl<Value *> &PoisonedValues) {
  bool Changed = false;
  // RemoveDIs: erase debug-info on this instruction manually.
  I->dropDbgRecords();
  for (Use &U : I->operands()) {
    Value *Op = U.get();
    if (isa<Instruction>(Op) && !Op->getType()->isTokenTy()) {
      U.set(PoisonValue::get(Op->getType()));
      PoisonedValues.push_back(Op);
      Changed = true;
    }
  }

  return Changed;
}

std::pair<unsigned, unsigned>
llvm::removeAllNonTerminatorAndEHPadInstructions(BasicBlock *BB) {
  unsigned NumDeadInst = 0;
  unsigned NumDeadDbgInst = 0;
  // Delete the instructions backwards, as it has a reduced likelihood of
  // having to update as many def-use and use-def chains.
  Instruction *EndInst = BB->getTerminator(); // Last not to be deleted.
  SmallVector<Value *> Uses;
  handleUnreachableTerminator(EndInst, Uses);

  while (EndInst != &BB->front()) {
    // Delete the next to last instruction.
    Instruction *Inst = &*--EndInst->getIterator();
    if (!Inst->use_empty() && !Inst->getType()->isTokenTy())
      Inst->replaceAllUsesWith(PoisonValue::get(Inst->getType()));
    if (Inst->isEHPad() || Inst->getType()->isTokenTy()) {
      // EHPads can't have DbgVariableRecords attached to them, but it might be
      // possible for things with token type.
      Inst->dropDbgRecords();
      EndInst = Inst;
      continue;
    }
    if (isa<DbgInfoIntrinsic>(Inst))
      ++NumDeadDbgInst;
    else
      ++NumDeadInst;
    // RemoveDIs: erasing debug-info must be done manually.
    Inst->dropDbgRecords();
    Inst->eraseFromParent();
  }
  return {NumDeadInst, NumDeadDbgInst};
}

unsigned llvm::changeToUnreachable(Instruction *I, bool PreserveLCSSA,
                                   DomTreeUpdater *DTU,
                                   MemorySSAUpdater *MSSAU) {
  BasicBlock *BB = I->getParent();

  if (MSSAU)
    MSSAU->changeToUnreachable(I);

  SmallSet<BasicBlock *, 8> UniqueSuccessors;

  // Loop over all of the successors, removing BB's entry from any PHI
  // nodes.
  for (BasicBlock *Successor : successors(BB)) {
    Successor->removePredecessor(BB, PreserveLCSSA);
    if (DTU)
      UniqueSuccessors.insert(Successor);
  }
  auto *UI = new UnreachableInst(I->getContext(), I->getIterator());
  UI->setDebugLoc(I->getDebugLoc());

  // All instructions after this are dead.
  unsigned NumInstrsRemoved = 0;
  BasicBlock::iterator BBI = I->getIterator(), BBE = BB->end();
  while (BBI != BBE) {
    if (!BBI->use_empty())
      BBI->replaceAllUsesWith(PoisonValue::get(BBI->getType()));
    BBI++->eraseFromParent();
    ++NumInstrsRemoved;
  }
  if (DTU) {
    SmallVector<DominatorTree::UpdateType, 8> Updates;
    Updates.reserve(UniqueSuccessors.size());
    for (BasicBlock *UniqueSuccessor : UniqueSuccessors)
      Updates.push_back({DominatorTree::Delete, BB, UniqueSuccessor});
    DTU->applyUpdates(Updates);
  }
  BB->flushTerminatorDbgRecords();
  return NumInstrsRemoved;
}

CallInst *llvm::createCallMatchingInvoke(InvokeInst *II) {
  SmallVector<Value *, 8> Args(II->args());
  SmallVector<OperandBundleDef, 1> OpBundles;
  II->getOperandBundlesAsDefs(OpBundles);
  CallInst *NewCall = CallInst::Create(II->getFunctionType(),
                                       II->getCalledOperand(), Args, OpBundles);
  NewCall->setCallingConv(II->getCallingConv());
  NewCall->setAttributes(II->getAttributes());
  NewCall->setDebugLoc(II->getDebugLoc());
  NewCall->copyMetadata(*II);

  // If the invoke had profile metadata, try converting them for CallInst.
  uint64_t TotalWeight;
  if (NewCall->extractProfTotalWeight(TotalWeight)) {
    // Set the total weight if it fits into i32, otherwise reset.
    MDBuilder MDB(NewCall->getContext());
    auto NewWeights = uint32_t(TotalWeight) != TotalWeight
                          ? nullptr
                          : MDB.createBranchWeights({uint32_t(TotalWeight)});
    NewCall->setMetadata(LLVMContext::MD_prof, NewWeights);
  }

  return NewCall;
}

// changeToCall - Convert the specified invoke into a normal call.
CallInst *llvm::changeToCall(InvokeInst *II, DomTreeUpdater *DTU) {
  CallInst *NewCall = createCallMatchingInvoke(II);
  NewCall->takeName(II);
  NewCall->insertBefore(II);
  II->replaceAllUsesWith(NewCall);

  // Follow the call by a branch to the normal destination.
  BasicBlock *NormalDestBB = II->getNormalDest();
  BranchInst::Create(NormalDestBB, II->getIterator());

  // Update PHI nodes in the unwind destination
  BasicBlock *BB = II->getParent();
  BasicBlock *UnwindDestBB = II->getUnwindDest();
  UnwindDestBB->removePredecessor(BB);
  II->eraseFromParent();
  if (DTU)
    DTU->applyUpdates({{DominatorTree::Delete, BB, UnwindDestBB}});
  return NewCall;
}

BasicBlock *llvm::changeToInvokeAndSplitBasicBlock(CallInst *CI,
                                                   BasicBlock *UnwindEdge,
                                                   DomTreeUpdater *DTU) {
  BasicBlock *BB = CI->getParent();

  // Convert this function call into an invoke instruction.  First, split the
  // basic block.
  BasicBlock *Split = SplitBlock(BB, CI, DTU, /*LI=*/nullptr, /*MSSAU*/ nullptr,
                                 CI->getName() + ".noexc");

  // Delete the unconditional branch inserted by SplitBlock
  BB->back().eraseFromParent();

  // Create the new invoke instruction.
  SmallVector<Value *, 8> InvokeArgs(CI->args());
  SmallVector<OperandBundleDef, 1> OpBundles;

  CI->getOperandBundlesAsDefs(OpBundles);

  // Note: we're round tripping operand bundles through memory here, and that
  // can potentially be avoided with a cleverer API design that we do not have
  // as of this time.

  InvokeInst *II =
      InvokeInst::Create(CI->getFunctionType(), CI->getCalledOperand(), Split,
                         UnwindEdge, InvokeArgs, OpBundles, CI->getName(), BB);
  II->setDebugLoc(CI->getDebugLoc());
  II->setCallingConv(CI->getCallingConv());
  II->setAttributes(CI->getAttributes());
  II->setMetadata(LLVMContext::MD_prof, CI->getMetadata(LLVMContext::MD_prof));

  if (DTU)
    DTU->applyUpdates({{DominatorTree::Insert, BB, UnwindEdge}});

  // Make sure that anything using the call now uses the invoke!  This also
  // updates the CallGraph if present, because it uses a WeakTrackingVH.
  CI->replaceAllUsesWith(II);

  // Delete the original call
  Split->front().eraseFromParent();
  return Split;
}

static bool markAliveBlocks(Function &F,
                            SmallPtrSetImpl<BasicBlock *> &Reachable,
                            DomTreeUpdater *DTU = nullptr) {
  SmallVector<BasicBlock*, 128> Worklist;
  BasicBlock *BB = &F.front();
  Worklist.push_back(BB);
  Reachable.insert(BB);
  bool Changed = false;
  do {
    BB = Worklist.pop_back_val();

    // Do a quick scan of the basic block, turning any obviously unreachable
    // instructions into LLVM unreachable insts.  The instruction combining pass
    // canonicalizes unreachable insts into stores to null or undef.
    for (Instruction &I : *BB) {
      if (auto *CI = dyn_cast<CallInst>(&I)) {
        Value *Callee = CI->getCalledOperand();
        // Handle intrinsic calls.
        if (Function *F = dyn_cast<Function>(Callee)) {
          auto IntrinsicID = F->getIntrinsicID();
          // Assumptions that are known to be false are equivalent to
          // unreachable. Also, if the condition is undefined, then we make the
          // choice most beneficial to the optimizer, and choose that to also be
          // unreachable.
          if (IntrinsicID == Intrinsic::assume) {
            if (match(CI->getArgOperand(0), m_CombineOr(m_Zero(), m_Undef()))) {
              // Don't insert a call to llvm.trap right before the unreachable.
              changeToUnreachable(CI, false, DTU);
              Changed = true;
              break;
            }
          } else if (IntrinsicID == Intrinsic::experimental_guard) {
            // A call to the guard intrinsic bails out of the current
            // compilation unit if the predicate passed to it is false. If the
            // predicate is a constant false, then we know the guard will bail
            // out of the current compile unconditionally, so all code following
            // it is dead.
            //
            // Note: unlike in llvm.assume, it is not "obviously profitable" for
            // guards to treat `undef` as `false` since a guard on `undef` can
            // still be useful for widening.
            if (match(CI->getArgOperand(0), m_Zero()))
              if (!isa<UnreachableInst>(CI->getNextNode())) {
                changeToUnreachable(CI->getNextNode(), false, DTU);
                Changed = true;
                break;
              }
          }
        } else if ((isa<ConstantPointerNull>(Callee) &&
                    !NullPointerIsDefined(CI->getFunction(),
                                          cast<PointerType>(Callee->getType())
                                              ->getAddressSpace())) ||
                   isa<UndefValue>(Callee)) {
          changeToUnreachable(CI, false, DTU);
          Changed = true;
          break;
        }
        if (CI->doesNotReturn() && !CI->isMustTailCall()) {
          // If we found a call to a no-return function, insert an unreachable
          // instruction after it.  Make sure there isn't *already* one there
          // though.
          if (!isa<UnreachableInst>(CI->getNextNonDebugInstruction())) {
            // Don't insert a call to llvm.trap right before the unreachable.
            changeToUnreachable(CI->getNextNonDebugInstruction(), false, DTU);
            Changed = true;
          }
          break;
        }
      } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        // Store to undef and store to null are undefined and used to signal
        // that they should be changed to unreachable by passes that can't
        // modify the CFG.

        // Don't touch volatile stores.
        if (SI->isVolatile()) continue;

        Value *Ptr = SI->getOperand(1);

        if (isa<UndefValue>(Ptr) ||
            (isa<ConstantPointerNull>(Ptr) &&
             !NullPointerIsDefined(SI->getFunction(),
                                   SI->getPointerAddressSpace()))) {
          changeToUnreachable(SI, false, DTU);
          Changed = true;
          break;
        }
      }
    }

    Instruction *Terminator = BB->getTerminator();
    if (auto *II = dyn_cast<InvokeInst>(Terminator)) {
      // Turn invokes that call 'nounwind' functions into ordinary calls.
      Value *Callee = II->getCalledOperand();
      if ((isa<ConstantPointerNull>(Callee) &&
           !NullPointerIsDefined(BB->getParent())) ||
          isa<UndefValue>(Callee)) {
        changeToUnreachable(II, false, DTU);
        Changed = true;
      } else {
        if (II->doesNotReturn() &&
            !isa<UnreachableInst>(II->getNormalDest()->front())) {
          // If we found an invoke of a no-return function,
          // create a new empty basic block with an `unreachable` terminator,
          // and set it as the normal destination for the invoke,
          // unless that is already the case.
          // Note that the original normal destination could have other uses.
          BasicBlock *OrigNormalDest = II->getNormalDest();
          OrigNormalDest->removePredecessor(II->getParent());
          LLVMContext &Ctx = II->getContext();
          BasicBlock *UnreachableNormalDest = BasicBlock::Create(
              Ctx, OrigNormalDest->getName() + ".unreachable",
              II->getFunction(), OrigNormalDest);
          new UnreachableInst(Ctx, UnreachableNormalDest);
          II->setNormalDest(UnreachableNormalDest);
          if (DTU)
            DTU->applyUpdates(
                {{DominatorTree::Delete, BB, OrigNormalDest},
                 {DominatorTree::Insert, BB, UnreachableNormalDest}});
          Changed = true;
        }
        if (II->doesNotThrow() && canSimplifyInvokeNoUnwind(&F)) {
          if (II->use_empty() && !II->mayHaveSideEffects()) {
            // jump to the normal destination branch.
            BasicBlock *NormalDestBB = II->getNormalDest();
            BasicBlock *UnwindDestBB = II->getUnwindDest();
            BranchInst::Create(NormalDestBB, II->getIterator());
            UnwindDestBB->removePredecessor(II->getParent());
            II->eraseFromParent();
            if (DTU)
              DTU->applyUpdates({{DominatorTree::Delete, BB, UnwindDestBB}});
          } else
            changeToCall(II, DTU);
          Changed = true;
        }
      }
    } else if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(Terminator)) {
      // Remove catchpads which cannot be reached.
      struct CatchPadDenseMapInfo {
        static CatchPadInst *getEmptyKey() {
          return DenseMapInfo<CatchPadInst *>::getEmptyKey();
        }

        static CatchPadInst *getTombstoneKey() {
          return DenseMapInfo<CatchPadInst *>::getTombstoneKey();
        }

        static unsigned getHashValue(CatchPadInst *CatchPad) {
          return static_cast<unsigned>(hash_combine_range(
              CatchPad->value_op_begin(), CatchPad->value_op_end()));
        }

        static bool isEqual(CatchPadInst *LHS, CatchPadInst *RHS) {
          if (LHS == getEmptyKey() || LHS == getTombstoneKey() ||
              RHS == getEmptyKey() || RHS == getTombstoneKey())
            return LHS == RHS;
          return LHS->isIdenticalTo(RHS);
        }
      };

      SmallDenseMap<BasicBlock *, int, 8> NumPerSuccessorCases;
      // Set of unique CatchPads.
      SmallDenseMap<CatchPadInst *, detail::DenseSetEmpty, 4,
                    CatchPadDenseMapInfo, detail::DenseSetPair<CatchPadInst *>>
          HandlerSet;
      detail::DenseSetEmpty Empty;
      for (CatchSwitchInst::handler_iterator I = CatchSwitch->handler_begin(),
                                             E = CatchSwitch->handler_end();
           I != E; ++I) {
        BasicBlock *HandlerBB = *I;
        if (DTU)
          ++NumPerSuccessorCases[HandlerBB];
        auto *CatchPad = cast<CatchPadInst>(HandlerBB->getFirstNonPHI());
        if (!HandlerSet.insert({CatchPad, Empty}).second) {
          if (DTU)
            --NumPerSuccessorCases[HandlerBB];
          CatchSwitch->removeHandler(I);
          --I;
          --E;
          Changed = true;
        }
      }
      if (DTU) {
        std::vector<DominatorTree::UpdateType> Updates;
        for (const std::pair<BasicBlock *, int> &I : NumPerSuccessorCases)
          if (I.second == 0)
            Updates.push_back({DominatorTree::Delete, BB, I.first});
        DTU->applyUpdates(Updates);
      }
    }

    Changed |= ConstantFoldTerminator(BB, true, nullptr, DTU);
    for (BasicBlock *Successor : successors(BB))
      if (Reachable.insert(Successor).second)
        Worklist.push_back(Successor);
  } while (!Worklist.empty());
  return Changed;
}

Instruction *llvm::removeUnwindEdge(BasicBlock *BB, DomTreeUpdater *DTU) {
  Instruction *TI = BB->getTerminator();

  if (auto *II = dyn_cast<InvokeInst>(TI))
    return changeToCall(II, DTU);

  Instruction *NewTI;
  BasicBlock *UnwindDest;

  if (auto *CRI = dyn_cast<CleanupReturnInst>(TI)) {
    NewTI = CleanupReturnInst::Create(CRI->getCleanupPad(), nullptr, CRI->getIterator());
    UnwindDest = CRI->getUnwindDest();
  } else if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(TI)) {
    auto *NewCatchSwitch = CatchSwitchInst::Create(
        CatchSwitch->getParentPad(), nullptr, CatchSwitch->getNumHandlers(),
        CatchSwitch->getName(), CatchSwitch->getIterator());
    for (BasicBlock *PadBB : CatchSwitch->handlers())
      NewCatchSwitch->addHandler(PadBB);

    NewTI = NewCatchSwitch;
    UnwindDest = CatchSwitch->getUnwindDest();
  } else {
    llvm_unreachable("Could not find unwind successor");
  }

  NewTI->takeName(TI);
  NewTI->setDebugLoc(TI->getDebugLoc());
  UnwindDest->removePredecessor(BB);
  TI->replaceAllUsesWith(NewTI);
  TI->eraseFromParent();
  if (DTU)
    DTU->applyUpdates({{DominatorTree::Delete, BB, UnwindDest}});
  return NewTI;
}

/// removeUnreachableBlocks - Remove blocks that are not reachable, even
/// if they are in a dead cycle.  Return true if a change was made, false
/// otherwise.
bool llvm::removeUnreachableBlocks(Function &F, DomTreeUpdater *DTU,
                                   MemorySSAUpdater *MSSAU) {
  SmallPtrSet<BasicBlock *, 16> Reachable;
  bool Changed = markAliveBlocks(F, Reachable, DTU);

  // If there are unreachable blocks in the CFG...
  if (Reachable.size() == F.size())
    return Changed;

  assert(Reachable.size() < F.size());

  // Are there any blocks left to actually delete?
  SmallSetVector<BasicBlock *, 8> BlocksToRemove;
  for (BasicBlock &BB : F) {
    // Skip reachable basic blocks
    if (Reachable.count(&BB))
      continue;
    // Skip already-deleted blocks
    if (DTU && DTU->isBBPendingDeletion(&BB))
      continue;
    BlocksToRemove.insert(&BB);
  }

  if (BlocksToRemove.empty())
    return Changed;

  Changed = true;
  NumRemoved += BlocksToRemove.size();

  if (MSSAU)
    MSSAU->removeBlocks(BlocksToRemove);

  DeleteDeadBlocks(BlocksToRemove.takeVector(), DTU);

  return Changed;
}

void llvm::combineMetadata(Instruction *K, const Instruction *J,
                           ArrayRef<unsigned> KnownIDs, bool DoesKMove) {
  SmallVector<std::pair<unsigned, MDNode *>, 4> Metadata;
  K->dropUnknownNonDebugMetadata(KnownIDs);
  K->getAllMetadataOtherThanDebugLoc(Metadata);
  for (const auto &MD : Metadata) {
    unsigned Kind = MD.first;
    MDNode *JMD = J->getMetadata(Kind);
    MDNode *KMD = MD.second;

    switch (Kind) {
      default:
        K->setMetadata(Kind, nullptr); // Remove unknown metadata
        break;
      case LLVMContext::MD_dbg:
        llvm_unreachable("getAllMetadataOtherThanDebugLoc returned a MD_dbg");
      case LLVMContext::MD_DIAssignID:
        K->mergeDIAssignID(J);
        break;
      case LLVMContext::MD_tbaa:
        K->setMetadata(Kind, MDNode::getMostGenericTBAA(JMD, KMD));
        break;
      case LLVMContext::MD_alias_scope:
        K->setMetadata(Kind, MDNode::getMostGenericAliasScope(JMD, KMD));
        break;
      case LLVMContext::MD_noalias:
      case LLVMContext::MD_mem_parallel_loop_access:
        K->setMetadata(Kind, MDNode::intersect(JMD, KMD));
        break;
      case LLVMContext::MD_access_group:
        K->setMetadata(LLVMContext::MD_access_group,
                       intersectAccessGroups(K, J));
        break;
      case LLVMContext::MD_range:
        if (DoesKMove || !K->hasMetadata(LLVMContext::MD_noundef))
          K->setMetadata(Kind, MDNode::getMostGenericRange(JMD, KMD));
        break;
      case LLVMContext::MD_fpmath:
        K->setMetadata(Kind, MDNode::getMostGenericFPMath(JMD, KMD));
        break;
      case LLVMContext::MD_invariant_load:
        // If K moves, only set the !invariant.load if it is present in both
        // instructions.
        if (DoesKMove)
          K->setMetadata(Kind, JMD);
        break;
      case LLVMContext::MD_nonnull:
        if (DoesKMove || !K->hasMetadata(LLVMContext::MD_noundef))
          K->setMetadata(Kind, JMD);
        break;
      case LLVMContext::MD_invariant_group:
        // Preserve !invariant.group in K.
        break;
      case LLVMContext::MD_mmra:
        // Combine MMRAs
        break;
      case LLVMContext::MD_align:
        if (DoesKMove || !K->hasMetadata(LLVMContext::MD_noundef))
          K->setMetadata(
              Kind, MDNode::getMostGenericAlignmentOrDereferenceable(JMD, KMD));
        break;
      case LLVMContext::MD_dereferenceable:
      case LLVMContext::MD_dereferenceable_or_null:
        if (DoesKMove)
          K->setMetadata(Kind,
            MDNode::getMostGenericAlignmentOrDereferenceable(JMD, KMD));
        break;
      case LLVMContext::MD_preserve_access_index:
        // Preserve !preserve.access.index in K.
        break;
      case LLVMContext::MD_noundef:
        // If K does move, keep noundef if it is present in both instructions.
        if (DoesKMove)
          K->setMetadata(Kind, JMD);
        break;
      case LLVMContext::MD_nontemporal:
        // Preserve !nontemporal if it is present on both instructions.
        K->setMetadata(Kind, JMD);
        break;
      case LLVMContext::MD_prof:
        if (DoesKMove)
          K->setMetadata(Kind, MDNode::getMergedProfMetadata(KMD, JMD, K, J));
        break;
    }
  }
  // Set !invariant.group from J if J has it. If both instructions have it
  // then we will just pick it from J - even when they are different.
  // Also make sure that K is load or store - f.e. combining bitcast with load
  // could produce bitcast with invariant.group metadata, which is invalid.
  // FIXME: we should try to preserve both invariant.group md if they are
  // different, but right now instruction can only have one invariant.group.
  if (auto *JMD = J->getMetadata(LLVMContext::MD_invariant_group))
    if (isa<LoadInst>(K) || isa<StoreInst>(K))
      K->setMetadata(LLVMContext::MD_invariant_group, JMD);

  // Merge MMRAs.
  // This is handled separately because we also want to handle cases where K
  // doesn't have tags but J does.
  auto JMMRA = J->getMetadata(LLVMContext::MD_mmra);
  auto KMMRA = K->getMetadata(LLVMContext::MD_mmra);
  if (JMMRA || KMMRA) {
    K->setMetadata(LLVMContext::MD_mmra,
                   MMRAMetadata::combine(K->getContext(), JMMRA, KMMRA));
  }
}

void llvm::combineMetadataForCSE(Instruction *K, const Instruction *J,
                                 bool KDominatesJ) {
  unsigned KnownIDs[] = {LLVMContext::MD_tbaa,
                         LLVMContext::MD_alias_scope,
                         LLVMContext::MD_noalias,
                         LLVMContext::MD_range,
                         LLVMContext::MD_fpmath,
                         LLVMContext::MD_invariant_load,
                         LLVMContext::MD_nonnull,
                         LLVMContext::MD_invariant_group,
                         LLVMContext::MD_align,
                         LLVMContext::MD_dereferenceable,
                         LLVMContext::MD_dereferenceable_or_null,
                         LLVMContext::MD_access_group,
                         LLVMContext::MD_preserve_access_index,
                         LLVMContext::MD_prof,
                         LLVMContext::MD_nontemporal,
                         LLVMContext::MD_noundef,
                         LLVMContext::MD_mmra};
  combineMetadata(K, J, KnownIDs, KDominatesJ);
}

void llvm::copyMetadataForLoad(LoadInst &Dest, const LoadInst &Source) {
  SmallVector<std::pair<unsigned, MDNode *>, 8> MD;
  Source.getAllMetadata(MD);
  MDBuilder MDB(Dest.getContext());
  Type *NewType = Dest.getType();
  const DataLayout &DL = Source.getDataLayout();
  for (const auto &MDPair : MD) {
    unsigned ID = MDPair.first;
    MDNode *N = MDPair.second;
    // Note, essentially every kind of metadata should be preserved here! This
    // routine is supposed to clone a load instruction changing *only its type*.
    // The only metadata it makes sense to drop is metadata which is invalidated
    // when the pointer type changes. This should essentially never be the case
    // in LLVM, but we explicitly switch over only known metadata to be
    // conservatively correct. If you are adding metadata to LLVM which pertains
    // to loads, you almost certainly want to add it here.
    switch (ID) {
    case LLVMContext::MD_dbg:
    case LLVMContext::MD_tbaa:
    case LLVMContext::MD_prof:
    case LLVMContext::MD_fpmath:
    case LLVMContext::MD_tbaa_struct:
    case LLVMContext::MD_invariant_load:
    case LLVMContext::MD_alias_scope:
    case LLVMContext::MD_noalias:
    case LLVMContext::MD_nontemporal:
    case LLVMContext::MD_mem_parallel_loop_access:
    case LLVMContext::MD_access_group:
    case LLVMContext::MD_noundef:
      // All of these directly apply.
      Dest.setMetadata(ID, N);
      break;

    case LLVMContext::MD_nonnull:
      copyNonnullMetadata(Source, N, Dest);
      break;

    case LLVMContext::MD_align:
    case LLVMContext::MD_dereferenceable:
    case LLVMContext::MD_dereferenceable_or_null:
      // These only directly apply if the new type is also a pointer.
      if (NewType->isPointerTy())
        Dest.setMetadata(ID, N);
      break;

    case LLVMContext::MD_range:
      copyRangeMetadata(DL, Source, N, Dest);
      break;
    }
  }
}

void llvm::patchReplacementInstruction(Instruction *I, Value *Repl) {
  auto *ReplInst = dyn_cast<Instruction>(Repl);
  if (!ReplInst)
    return;

  // Patch the replacement so that it is not more restrictive than the value
  // being replaced.
  WithOverflowInst *UnusedWO;
  // When replacing the result of a llvm.*.with.overflow intrinsic with a
  // overflowing binary operator, nuw/nsw flags may no longer hold.
  if (isa<OverflowingBinaryOperator>(ReplInst) &&
      match(I, m_ExtractValue<0>(m_WithOverflowInst(UnusedWO))))
    ReplInst->dropPoisonGeneratingFlags();
  // Note that if 'I' is a load being replaced by some operation,
  // for example, by an arithmetic operation, then andIRFlags()
  // would just erase all math flags from the original arithmetic
  // operation, which is clearly not wanted and not needed.
  else if (!isa<LoadInst>(I))
    ReplInst->andIRFlags(I);

  // FIXME: If both the original and replacement value are part of the
  // same control-flow region (meaning that the execution of one
  // guarantees the execution of the other), then we can combine the
  // noalias scopes here and do better than the general conservative
  // answer used in combineMetadata().

  // In general, GVN unifies expressions over different control-flow
  // regions, and so we need a conservative combination of the noalias
  // scopes.
  combineMetadataForCSE(ReplInst, I, false);
}

template <typename RootType, typename ShouldReplaceFn>
static unsigned replaceDominatedUsesWith(Value *From, Value *To,
                                         const RootType &Root,
                                         const ShouldReplaceFn &ShouldReplace) {
  assert(From->getType() == To->getType());

  unsigned Count = 0;
  for (Use &U : llvm::make_early_inc_range(From->uses())) {
    if (!ShouldReplace(Root, U))
      continue;
    LLVM_DEBUG(dbgs() << "Replace dominated use of '";
               From->printAsOperand(dbgs());
               dbgs() << "' with " << *To << " in " << *U.getUser() << "\n");
    U.set(To);
    ++Count;
  }
  return Count;
}

unsigned llvm::replaceNonLocalUsesWith(Instruction *From, Value *To) {
   assert(From->getType() == To->getType());
   auto *BB = From->getParent();
   unsigned Count = 0;

   for (Use &U : llvm::make_early_inc_range(From->uses())) {
    auto *I = cast<Instruction>(U.getUser());
    if (I->getParent() == BB)
      continue;
    U.set(To);
    ++Count;
  }
  return Count;
}

unsigned llvm::replaceDominatedUsesWith(Value *From, Value *To,
                                        DominatorTree &DT,
                                        const BasicBlockEdge &Root) {
  auto Dominates = [&DT](const BasicBlockEdge &Root, const Use &U) {
    return DT.dominates(Root, U);
  };
  return ::replaceDominatedUsesWith(From, To, Root, Dominates);
}

unsigned llvm::replaceDominatedUsesWith(Value *From, Value *To,
                                        DominatorTree &DT,
                                        const BasicBlock *BB) {
  auto Dominates = [&DT](const BasicBlock *BB, const Use &U) {
    return DT.dominates(BB, U);
  };
  return ::replaceDominatedUsesWith(From, To, BB, Dominates);
}

unsigned llvm::replaceDominatedUsesWithIf(
    Value *From, Value *To, DominatorTree &DT, const BasicBlockEdge &Root,
    function_ref<bool(const Use &U, const Value *To)> ShouldReplace) {
  auto DominatesAndShouldReplace =
      [&DT, &ShouldReplace, To](const BasicBlockEdge &Root, const Use &U) {
        return DT.dominates(Root, U) && ShouldReplace(U, To);
      };
  return ::replaceDominatedUsesWith(From, To, Root, DominatesAndShouldReplace);
}

unsigned llvm::replaceDominatedUsesWithIf(
    Value *From, Value *To, DominatorTree &DT, const BasicBlock *BB,
    function_ref<bool(const Use &U, const Value *To)> ShouldReplace) {
  auto DominatesAndShouldReplace = [&DT, &ShouldReplace,
                                    To](const BasicBlock *BB, const Use &U) {
    return DT.dominates(BB, U) && ShouldReplace(U, To);
  };
  return ::replaceDominatedUsesWith(From, To, BB, DominatesAndShouldReplace);
}

bool llvm::callsGCLeafFunction(const CallBase *Call,
                               const TargetLibraryInfo &TLI) {
  // Check if the function is specifically marked as a gc leaf function.
  if (Call->hasFnAttr("gc-leaf-function"))
    return true;
  if (const Function *F = Call->getCalledFunction()) {
    if (F->hasFnAttribute("gc-leaf-function"))
      return true;

    if (auto IID = F->getIntrinsicID()) {
      // Most LLVM intrinsics do not take safepoints.
      return IID != Intrinsic::experimental_gc_statepoint &&
             IID != Intrinsic::experimental_deoptimize &&
             IID != Intrinsic::memcpy_element_unordered_atomic &&
             IID != Intrinsic::memmove_element_unordered_atomic;
    }
  }

  // Lib calls can be materialized by some passes, and won't be
  // marked as 'gc-leaf-function.' All available Libcalls are
  // GC-leaf.
  LibFunc LF;
  if (TLI.getLibFunc(*Call, LF)) {
    return TLI.has(LF);
  }

  return false;
}

void llvm::copyNonnullMetadata(const LoadInst &OldLI, MDNode *N,
                               LoadInst &NewLI) {
  auto *NewTy = NewLI.getType();

  // This only directly applies if the new type is also a pointer.
  if (NewTy->isPointerTy()) {
    NewLI.setMetadata(LLVMContext::MD_nonnull, N);
    return;
  }

  // The only other translation we can do is to integral loads with !range
  // metadata.
  if (!NewTy->isIntegerTy())
    return;

  MDBuilder MDB(NewLI.getContext());
  const Value *Ptr = OldLI.getPointerOperand();
  auto *ITy = cast<IntegerType>(NewTy);
  auto *NullInt = ConstantExpr::getPtrToInt(
      ConstantPointerNull::get(cast<PointerType>(Ptr->getType())), ITy);
  auto *NonNullInt = ConstantExpr::getAdd(NullInt, ConstantInt::get(ITy, 1));
  NewLI.setMetadata(LLVMContext::MD_range,
                    MDB.createRange(NonNullInt, NullInt));
}

void llvm::copyRangeMetadata(const DataLayout &DL, const LoadInst &OldLI,
                             MDNode *N, LoadInst &NewLI) {
  auto *NewTy = NewLI.getType();
  // Simply copy the metadata if the type did not change.
  if (NewTy == OldLI.getType()) {
    NewLI.setMetadata(LLVMContext::MD_range, N);
    return;
  }

  // Give up unless it is converted to a pointer where there is a single very
  // valuable mapping we can do reliably.
  // FIXME: It would be nice to propagate this in more ways, but the type
  // conversions make it hard.
  if (!NewTy->isPointerTy())
    return;

  unsigned BitWidth = DL.getPointerTypeSizeInBits(NewTy);
  if (BitWidth == OldLI.getType()->getScalarSizeInBits() &&
      !getConstantRangeFromMetadata(*N).contains(APInt(BitWidth, 0))) {
    MDNode *NN = MDNode::get(OldLI.getContext(), std::nullopt);
    NewLI.setMetadata(LLVMContext::MD_nonnull, NN);
  }
}

void llvm::dropDebugUsers(Instruction &I) {
  SmallVector<DbgVariableIntrinsic *, 1> DbgUsers;
  SmallVector<DbgVariableRecord *, 1> DPUsers;
  findDbgUsers(DbgUsers, &I, &DPUsers);
  for (auto *DII : DbgUsers)
    DII->eraseFromParent();
  for (auto *DVR : DPUsers)
    DVR->eraseFromParent();
}

void llvm::hoistAllInstructionsInto(BasicBlock *DomBlock, Instruction *InsertPt,
                                    BasicBlock *BB) {
  // Since we are moving the instructions out of its basic block, we do not
  // retain their original debug locations (DILocations) and debug intrinsic
  // instructions.
  //
  // Doing so would degrade the debugging experience and adversely affect the
  // accuracy of profiling information.
  //
  // Currently, when hoisting the instructions, we take the following actions:
  // - Remove their debug intrinsic instructions.
  // - Set their debug locations to the values from the insertion point.
  //
  // As per PR39141 (comment #8), the more fundamental reason why the dbg.values
  // need to be deleted, is because there will not be any instructions with a
  // DILocation in either branch left after performing the transformation. We
  // can only insert a dbg.value after the two branches are joined again.
  //
  // See PR38762, PR39243 for more details.
  //
  // TODO: Extend llvm.dbg.value to take more than one SSA Value (PR39141) to
  // encode predicated DIExpressions that yield different results on different
  // code paths.

  for (BasicBlock::iterator II = BB->begin(), IE = BB->end(); II != IE;) {
    Instruction *I = &*II;
    I->dropUBImplyingAttrsAndMetadata();
    if (I->isUsedByMetadata())
      dropDebugUsers(*I);
    // RemoveDIs: drop debug-info too as the following code does.
    I->dropDbgRecords();
    if (I->isDebugOrPseudoInst()) {
      // Remove DbgInfo and pseudo probe Intrinsics.
      II = I->eraseFromParent();
      continue;
    }
    I->setDebugLoc(InsertPt->getDebugLoc());
    ++II;
  }
  DomBlock->splice(InsertPt->getIterator(), BB, BB->begin(),
                   BB->getTerminator()->getIterator());
}

DIExpression *llvm::getExpressionForConstant(DIBuilder &DIB, const Constant &C,
                                             Type &Ty) {
  // Create integer constant expression.
  auto createIntegerExpression = [&DIB](const Constant &CV) -> DIExpression * {
    const APInt &API = cast<ConstantInt>(&CV)->getValue();
    std::optional<int64_t> InitIntOpt = API.trySExtValue();
    return InitIntOpt ? DIB.createConstantValueExpression(
                            static_cast<uint64_t>(*InitIntOpt))
                      : nullptr;
  };

  if (isa<ConstantInt>(C))
    return createIntegerExpression(C);

  auto *FP = dyn_cast<ConstantFP>(&C);
  if (FP && Ty.isFloatingPointTy() && Ty.getScalarSizeInBits() <= 64) {
    const APFloat &APF = FP->getValueAPF();
    APInt const &API = APF.bitcastToAPInt();
    if (auto Temp = API.getZExtValue())
      return DIB.createConstantValueExpression(static_cast<uint64_t>(Temp));
    return DIB.createConstantValueExpression(*API.getRawData());
  }

  if (!Ty.isPointerTy())
    return nullptr;

  if (isa<ConstantPointerNull>(C))
    return DIB.createConstantValueExpression(0);

  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(&C))
    if (CE->getOpcode() == Instruction::IntToPtr) {
      const Value *V = CE->getOperand(0);
      if (auto CI = dyn_cast_or_null<ConstantInt>(V))
        return createIntegerExpression(*CI);
    }
  return nullptr;
}

void llvm::remapDebugVariable(ValueToValueMapTy &Mapping, Instruction *Inst) {
  auto RemapDebugOperands = [&Mapping](auto *DV, auto Set) {
    for (auto *Op : Set) {
      auto I = Mapping.find(Op);
      if (I != Mapping.end())
        DV->replaceVariableLocationOp(Op, I->second, /*AllowEmpty=*/true);
    }
  };
  auto RemapAssignAddress = [&Mapping](auto *DA) {
    auto I = Mapping.find(DA->getAddress());
    if (I != Mapping.end())
      DA->setAddress(I->second);
  };
  if (auto DVI = dyn_cast<DbgVariableIntrinsic>(Inst))
    RemapDebugOperands(DVI, DVI->location_ops());
  if (auto DAI = dyn_cast<DbgAssignIntrinsic>(Inst))
    RemapAssignAddress(DAI);
  for (DbgVariableRecord &DVR : filterDbgVars(Inst->getDbgRecordRange())) {
    RemapDebugOperands(&DVR, DVR.location_ops());
    if (DVR.isDbgAssign())
      RemapAssignAddress(&DVR);
  }
}

namespace {

/// A potential constituent of a bitreverse or bswap expression. See
/// collectBitParts for a fuller explanation.
struct BitPart {
  BitPart(Value *P, unsigned BW) : Provider(P) {
    Provenance.resize(BW);
  }

  /// The Value that this is a bitreverse/bswap of.
  Value *Provider;

  /// The "provenance" of each bit. Provenance[A] = B means that bit A
  /// in Provider becomes bit B in the result of this expression.
  SmallVector<int8_t, 32> Provenance; // int8_t means max size is i128.

  enum { Unset = -1 };
};

} // end anonymous namespace

/// Analyze the specified subexpression and see if it is capable of providing
/// pieces of a bswap or bitreverse. The subexpression provides a potential
/// piece of a bswap or bitreverse if it can be proved that each non-zero bit in
/// the output of the expression came from a corresponding bit in some other
/// value. This function is recursive, and the end result is a mapping of
/// bitnumber to bitnumber. It is the caller's responsibility to validate that
/// the bitnumber to bitnumber mapping is correct for a bswap or bitreverse.
///
/// For example, if the current subexpression if "(shl i32 %X, 24)" then we know
/// that the expression deposits the low byte of %X into the high byte of the
/// result and that all other bits are zero. This expression is accepted and a
/// BitPart is returned with Provider set to %X and Provenance[24-31] set to
/// [0-7].
///
/// For vector types, all analysis is performed at the per-element level. No
/// cross-element analysis is supported (shuffle/insertion/reduction), and all
/// constant masks must be splatted across all elements.
///
/// To avoid revisiting values, the BitPart results are memoized into the
/// provided map. To avoid unnecessary copying of BitParts, BitParts are
/// constructed in-place in the \c BPS map. Because of this \c BPS needs to
/// store BitParts objects, not pointers. As we need the concept of a nullptr
/// BitParts (Value has been analyzed and the analysis failed), we an Optional
/// type instead to provide the same functionality.
///
/// Because we pass around references into \c BPS, we must use a container that
/// does not invalidate internal references (std::map instead of DenseMap).
static const std::optional<BitPart> &
collectBitParts(Value *V, bool MatchBSwaps, bool MatchBitReversals,
                std::map<Value *, std::optional<BitPart>> &BPS, int Depth,
                bool &FoundRoot) {
  auto I = BPS.find(V);
  if (I != BPS.end())
    return I->second;

  auto &Result = BPS[V] = std::nullopt;
  auto BitWidth = V->getType()->getScalarSizeInBits();

  // Can't do integer/elements > 128 bits.
  if (BitWidth > 128)
    return Result;

  // Prevent stack overflow by limiting the recursion depth
  if (Depth == BitPartRecursionMaxDepth) {
    LLVM_DEBUG(dbgs() << "collectBitParts max recursion depth reached.\n");
    return Result;
  }

  if (auto *I = dyn_cast<Instruction>(V)) {
    Value *X, *Y;
    const APInt *C;

    // If this is an or instruction, it may be an inner node of the bswap.
    if (match(V, m_Or(m_Value(X), m_Value(Y)))) {
      // Check we have both sources and they are from the same provider.
      const auto &A = collectBitParts(X, MatchBSwaps, MatchBitReversals, BPS,
                                      Depth + 1, FoundRoot);
      if (!A || !A->Provider)
        return Result;

      const auto &B = collectBitParts(Y, MatchBSwaps, MatchBitReversals, BPS,
                                      Depth + 1, FoundRoot);
      if (!B || A->Provider != B->Provider)
        return Result;

      // Try and merge the two together.
      Result = BitPart(A->Provider, BitWidth);
      for (unsigned BitIdx = 0; BitIdx < BitWidth; ++BitIdx) {
        if (A->Provenance[BitIdx] != BitPart::Unset &&
            B->Provenance[BitIdx] != BitPart::Unset &&
            A->Provenance[BitIdx] != B->Provenance[BitIdx])
          return Result = std::nullopt;

        if (A->Provenance[BitIdx] == BitPart::Unset)
          Result->Provenance[BitIdx] = B->Provenance[BitIdx];
        else
          Result->Provenance[BitIdx] = A->Provenance[BitIdx];
      }

      return Result;
    }

    // If this is a logical shift by a constant, recurse then shift the result.
    if (match(V, m_LogicalShift(m_Value(X), m_APInt(C)))) {
      const APInt &BitShift = *C;

      // Ensure the shift amount is defined.
      if (BitShift.uge(BitWidth))
        return Result;

      // For bswap-only, limit shift amounts to whole bytes, for an early exit.
      if (!MatchBitReversals && (BitShift.getZExtValue() % 8) != 0)
        return Result;

      const auto &Res = collectBitParts(X, MatchBSwaps, MatchBitReversals, BPS,
                                        Depth + 1, FoundRoot);
      if (!Res)
        return Result;
      Result = Res;

      // Perform the "shift" on BitProvenance.
      auto &P = Result->Provenance;
      if (I->getOpcode() == Instruction::Shl) {
        P.erase(std::prev(P.end(), BitShift.getZExtValue()), P.end());
        P.insert(P.begin(), BitShift.getZExtValue(), BitPart::Unset);
      } else {
        P.erase(P.begin(), std::next(P.begin(), BitShift.getZExtValue()));
        P.insert(P.end(), BitShift.getZExtValue(), BitPart::Unset);
      }

      return Result;
    }

    // If this is a logical 'and' with a mask that clears bits, recurse then
    // unset the appropriate bits.
    if (match(V, m_And(m_Value(X), m_APInt(C)))) {
      const APInt &AndMask = *C;

      // Check that the mask allows a multiple of 8 bits for a bswap, for an
      // early exit.
      unsigned NumMaskedBits = AndMask.popcount();
      if (!MatchBitReversals && (NumMaskedBits % 8) != 0)
        return Result;

      const auto &Res = collectBitParts(X, MatchBSwaps, MatchBitReversals, BPS,
                                        Depth + 1, FoundRoot);
      if (!Res)
        return Result;
      Result = Res;

      for (unsigned BitIdx = 0; BitIdx < BitWidth; ++BitIdx)
        // If the AndMask is zero for this bit, clear the bit.
        if (AndMask[BitIdx] == 0)
          Result->Provenance[BitIdx] = BitPart::Unset;
      return Result;
    }

    // If this is a zext instruction zero extend the result.
    if (match(V, m_ZExt(m_Value(X)))) {
      const auto &Res = collectBitParts(X, MatchBSwaps, MatchBitReversals, BPS,
                                        Depth + 1, FoundRoot);
      if (!Res)
        return Result;

      Result = BitPart(Res->Provider, BitWidth);
      auto NarrowBitWidth = X->getType()->getScalarSizeInBits();
      for (unsigned BitIdx = 0; BitIdx < NarrowBitWidth; ++BitIdx)
        Result->Provenance[BitIdx] = Res->Provenance[BitIdx];
      for (unsigned BitIdx = NarrowBitWidth; BitIdx < BitWidth; ++BitIdx)
        Result->Provenance[BitIdx] = BitPart::Unset;
      return Result;
    }

    // If this is a truncate instruction, extract the lower bits.
    if (match(V, m_Trunc(m_Value(X)))) {
      const auto &Res = collectBitParts(X, MatchBSwaps, MatchBitReversals, BPS,
                                        Depth + 1, FoundRoot);
      if (!Res)
        return Result;

      Result = BitPart(Res->Provider, BitWidth);
      for (unsigned BitIdx = 0; BitIdx < BitWidth; ++BitIdx)
        Result->Provenance[BitIdx] = Res->Provenance[BitIdx];
      return Result;
    }

    // BITREVERSE - most likely due to us previous matching a partial
    // bitreverse.
    if (match(V, m_BitReverse(m_Value(X)))) {
      const auto &Res = collectBitParts(X, MatchBSwaps, MatchBitReversals, BPS,
                                        Depth + 1, FoundRoot);
      if (!Res)
        return Result;

      Result = BitPart(Res->Provider, BitWidth);
      for (unsigned BitIdx = 0; BitIdx < BitWidth; ++BitIdx)
        Result->Provenance[(BitWidth - 1) - BitIdx] = Res->Provenance[BitIdx];
      return Result;
    }

    // BSWAP - most likely due to us previous matching a partial bswap.
    if (match(V, m_BSwap(m_Value(X)))) {
      const auto &Res = collectBitParts(X, MatchBSwaps, MatchBitReversals, BPS,
                                        Depth + 1, FoundRoot);
      if (!Res)
        return Result;

      unsigned ByteWidth = BitWidth / 8;
      Result = BitPart(Res->Provider, BitWidth);
      for (unsigned ByteIdx = 0; ByteIdx < ByteWidth; ++ByteIdx) {
        unsigned ByteBitOfs = ByteIdx * 8;
        for (unsigned BitIdx = 0; BitIdx < 8; ++BitIdx)
          Result->Provenance[(BitWidth - 8 - ByteBitOfs) + BitIdx] =
              Res->Provenance[ByteBitOfs + BitIdx];
      }
      return Result;
    }

    // Funnel 'double' shifts take 3 operands, 2 inputs and the shift
    // amount (modulo).
    // fshl(X,Y,Z): (X << (Z % BW)) | (Y >> (BW - (Z % BW)))
    // fshr(X,Y,Z): (X << (BW - (Z % BW))) | (Y >> (Z % BW))
    if (match(V, m_FShl(m_Value(X), m_Value(Y), m_APInt(C))) ||
        match(V, m_FShr(m_Value(X), m_Value(Y), m_APInt(C)))) {
      // We can treat fshr as a fshl by flipping the modulo amount.
      unsigned ModAmt = C->urem(BitWidth);
      if (cast<IntrinsicInst>(I)->getIntrinsicID() == Intrinsic::fshr)
        ModAmt = BitWidth - ModAmt;

      // For bswap-only, limit shift amounts to whole bytes, for an early exit.
      if (!MatchBitReversals && (ModAmt % 8) != 0)
        return Result;

      // Check we have both sources and they are from the same provider.
      const auto &LHS = collectBitParts(X, MatchBSwaps, MatchBitReversals, BPS,
                                        Depth + 1, FoundRoot);
      if (!LHS || !LHS->Provider)
        return Result;

      const auto &RHS = collectBitParts(Y, MatchBSwaps, MatchBitReversals, BPS,
                                        Depth + 1, FoundRoot);
      if (!RHS || LHS->Provider != RHS->Provider)
        return Result;

      unsigned StartBitRHS = BitWidth - ModAmt;
      Result = BitPart(LHS->Provider, BitWidth);
      for (unsigned BitIdx = 0; BitIdx < StartBitRHS; ++BitIdx)
        Result->Provenance[BitIdx + ModAmt] = LHS->Provenance[BitIdx];
      for (unsigned BitIdx = 0; BitIdx < ModAmt; ++BitIdx)
        Result->Provenance[BitIdx] = RHS->Provenance[BitIdx + StartBitRHS];
      return Result;
    }
  }

  // If we've already found a root input value then we're never going to merge
  // these back together.
  if (FoundRoot)
    return Result;

  // Okay, we got to something that isn't a shift, 'or', 'and', etc. This must
  // be the root input value to the bswap/bitreverse.
  FoundRoot = true;
  Result = BitPart(V, BitWidth);
  for (unsigned BitIdx = 0; BitIdx < BitWidth; ++BitIdx)
    Result->Provenance[BitIdx] = BitIdx;
  return Result;
}

static bool bitTransformIsCorrectForBSwap(unsigned From, unsigned To,
                                          unsigned BitWidth) {
  if (From % 8 != To % 8)
    return false;
  // Convert from bit indices to byte indices and check for a byte reversal.
  From >>= 3;
  To >>= 3;
  BitWidth >>= 3;
  return From == BitWidth - To - 1;
}

static bool bitTransformIsCorrectForBitReverse(unsigned From, unsigned To,
                                               unsigned BitWidth) {
  return From == BitWidth - To - 1;
}

bool llvm::recognizeBSwapOrBitReverseIdiom(
    Instruction *I, bool MatchBSwaps, bool MatchBitReversals,
    SmallVectorImpl<Instruction *> &InsertedInsts) {
  if (!match(I, m_Or(m_Value(), m_Value())) &&
      !match(I, m_FShl(m_Value(), m_Value(), m_Value())) &&
      !match(I, m_FShr(m_Value(), m_Value(), m_Value())) &&
      !match(I, m_BSwap(m_Value())))
    return false;
  if (!MatchBSwaps && !MatchBitReversals)
    return false;
  Type *ITy = I->getType();
  if (!ITy->isIntOrIntVectorTy() || ITy->getScalarSizeInBits() > 128)
    return false;  // Can't do integer/elements > 128 bits.

  // Try to find all the pieces corresponding to the bswap.
  bool FoundRoot = false;
  std::map<Value *, std::optional<BitPart>> BPS;
  const auto &Res =
      collectBitParts(I, MatchBSwaps, MatchBitReversals, BPS, 0, FoundRoot);
  if (!Res)
    return false;
  ArrayRef<int8_t> BitProvenance = Res->Provenance;
  assert(all_of(BitProvenance,
                [](int8_t I) { return I == BitPart::Unset || 0 <= I; }) &&
         "Illegal bit provenance index");

  // If the upper bits are zero, then attempt to perform as a truncated op.
  Type *DemandedTy = ITy;
  if (BitProvenance.back() == BitPart::Unset) {
    while (!BitProvenance.empty() && BitProvenance.back() == BitPart::Unset)
      BitProvenance = BitProvenance.drop_back();
    if (BitProvenance.empty())
      return false; // TODO - handle null value?
    DemandedTy = Type::getIntNTy(I->getContext(), BitProvenance.size());
    if (auto *IVecTy = dyn_cast<VectorType>(ITy))
      DemandedTy = VectorType::get(DemandedTy, IVecTy);
  }

  // Check BitProvenance hasn't found a source larger than the result type.
  unsigned DemandedBW = DemandedTy->getScalarSizeInBits();
  if (DemandedBW > ITy->getScalarSizeInBits())
    return false;

  // Now, is the bit permutation correct for a bswap or a bitreverse? We can
  // only byteswap values with an even number of bytes.
  APInt DemandedMask = APInt::getAllOnes(DemandedBW);
  bool OKForBSwap = MatchBSwaps && (DemandedBW % 16) == 0;
  bool OKForBitReverse = MatchBitReversals;
  for (unsigned BitIdx = 0;
       (BitIdx < DemandedBW) && (OKForBSwap || OKForBitReverse); ++BitIdx) {
    if (BitProvenance[BitIdx] == BitPart::Unset) {
      DemandedMask.clearBit(BitIdx);
      continue;
    }
    OKForBSwap &= bitTransformIsCorrectForBSwap(BitProvenance[BitIdx], BitIdx,
                                                DemandedBW);
    OKForBitReverse &= bitTransformIsCorrectForBitReverse(BitProvenance[BitIdx],
                                                          BitIdx, DemandedBW);
  }

  Intrinsic::ID Intrin;
  if (OKForBSwap)
    Intrin = Intrinsic::bswap;
  else if (OKForBitReverse)
    Intrin = Intrinsic::bitreverse;
  else
    return false;

  Function *F = Intrinsic::getDeclaration(I->getModule(), Intrin, DemandedTy);
  Value *Provider = Res->Provider;

  // We may need to truncate the provider.
  if (DemandedTy != Provider->getType()) {
    auto *Trunc =
        CastInst::CreateIntegerCast(Provider, DemandedTy, false, "trunc", I->getIterator());
    InsertedInsts.push_back(Trunc);
    Provider = Trunc;
  }

  Instruction *Result = CallInst::Create(F, Provider, "rev", I->getIterator());
  InsertedInsts.push_back(Result);

  if (!DemandedMask.isAllOnes()) {
    auto *Mask = ConstantInt::get(DemandedTy, DemandedMask);
    Result = BinaryOperator::Create(Instruction::And, Result, Mask, "mask", I->getIterator());
    InsertedInsts.push_back(Result);
  }

  // We may need to zeroextend back to the result type.
  if (ITy != Result->getType()) {
    auto *ExtInst = CastInst::CreateIntegerCast(Result, ITy, false, "zext", I->getIterator());
    InsertedInsts.push_back(ExtInst);
  }

  return true;
}

// CodeGen has special handling for some string functions that may replace
// them with target-specific intrinsics.  Since that'd skip our interceptors
// in ASan/MSan/TSan/DFSan, and thus make us miss some memory accesses,
// we mark affected calls as NoBuiltin, which will disable optimization
// in CodeGen.
void llvm::maybeMarkSanitizerLibraryCallNoBuiltin(
    CallInst *CI, const TargetLibraryInfo *TLI) {
  Function *F = CI->getCalledFunction();
  LibFunc Func;
  if (F && !F->hasLocalLinkage() && F->hasName() &&
      TLI->getLibFunc(F->getName(), Func) && TLI->hasOptimizedCodeGen(Func) &&
      !F->doesNotAccessMemory())
    CI->addFnAttr(Attribute::NoBuiltin);
}

bool llvm::canReplaceOperandWithVariable(const Instruction *I, unsigned OpIdx) {
  // We can't have a PHI with a metadata type.
  if (I->getOperand(OpIdx)->getType()->isMetadataTy())
    return false;

  // Early exit.
  if (!isa<Constant>(I->getOperand(OpIdx)))
    return true;

  switch (I->getOpcode()) {
  default:
    return true;
  case Instruction::Call:
  case Instruction::Invoke: {
    const auto &CB = cast<CallBase>(*I);

    // Can't handle inline asm. Skip it.
    if (CB.isInlineAsm())
      return false;

    // Constant bundle operands may need to retain their constant-ness for
    // correctness.
    if (CB.isBundleOperand(OpIdx))
      return false;

    if (OpIdx < CB.arg_size()) {
      // Some variadic intrinsics require constants in the variadic arguments,
      // which currently aren't markable as immarg.
      if (isa<IntrinsicInst>(CB) &&
          OpIdx >= CB.getFunctionType()->getNumParams()) {
        // This is known to be OK for stackmap.
        return CB.getIntrinsicID() == Intrinsic::experimental_stackmap;
      }

      // gcroot is a special case, since it requires a constant argument which
      // isn't also required to be a simple ConstantInt.
      if (CB.getIntrinsicID() == Intrinsic::gcroot)
        return false;

      // Some intrinsic operands are required to be immediates.
      return !CB.paramHasAttr(OpIdx, Attribute::ImmArg);
    }

    // It is never allowed to replace the call argument to an intrinsic, but it
    // may be possible for a call.
    return !isa<IntrinsicInst>(CB);
  }
  case Instruction::ShuffleVector:
    // Shufflevector masks are constant.
    return OpIdx != 2;
  case Instruction::Switch:
  case Instruction::ExtractValue:
    // All operands apart from the first are constant.
    return OpIdx == 0;
  case Instruction::InsertValue:
    // All operands apart from the first and the second are constant.
    return OpIdx < 2;
  case Instruction::Alloca:
    // Static allocas (constant size in the entry block) are handled by
    // prologue/epilogue insertion so they're free anyway. We definitely don't
    // want to make them non-constant.
    return !cast<AllocaInst>(I)->isStaticAlloca();
  case Instruction::GetElementPtr:
    if (OpIdx == 0)
      return true;
    gep_type_iterator It = gep_type_begin(I);
    for (auto E = std::next(It, OpIdx); It != E; ++It)
      if (It.isStruct())
        return false;
    return true;
  }
}

Value *llvm::invertCondition(Value *Condition) {
  // First: Check if it's a constant
  if (Constant *C = dyn_cast<Constant>(Condition))
    return ConstantExpr::getNot(C);

  // Second: If the condition is already inverted, return the original value
  Value *NotCondition;
  if (match(Condition, m_Not(m_Value(NotCondition))))
    return NotCondition;

  BasicBlock *Parent = nullptr;
  Instruction *Inst = dyn_cast<Instruction>(Condition);
  if (Inst)
    Parent = Inst->getParent();
  else if (Argument *Arg = dyn_cast<Argument>(Condition))
    Parent = &Arg->getParent()->getEntryBlock();
  assert(Parent && "Unsupported condition to invert");

  // Third: Check all the users for an invert
  for (User *U : Condition->users())
    if (Instruction *I = dyn_cast<Instruction>(U))
      if (I->getParent() == Parent && match(I, m_Not(m_Specific(Condition))))
        return I;

  // Last option: Create a new instruction
  auto *Inverted =
      BinaryOperator::CreateNot(Condition, Condition->getName() + ".inv");
  if (Inst && !isa<PHINode>(Inst))
    Inverted->insertAfter(Inst);
  else
    Inverted->insertBefore(&*Parent->getFirstInsertionPt());
  return Inverted;
}

bool llvm::inferAttributesFromOthers(Function &F) {
  // Note: We explicitly check for attributes rather than using cover functions
  // because some of the cover functions include the logic being implemented.

  bool Changed = false;
  // readnone + not convergent implies nosync
  if (!F.hasFnAttribute(Attribute::NoSync) &&
      F.doesNotAccessMemory() && !F.isConvergent()) {
    F.setNoSync();
    Changed = true;
  }

  // readonly implies nofree
  if (!F.hasFnAttribute(Attribute::NoFree) && F.onlyReadsMemory()) {
    F.setDoesNotFreeMemory();
    Changed = true;
  }

  // willreturn implies mustprogress
  if (!F.hasFnAttribute(Attribute::MustProgress) && F.willReturn()) {
    F.setMustProgress();
    Changed = true;
  }

  // TODO: There are a bunch of cases of restrictive memory effects we
  // can infer by inspecting arguments of argmemonly-ish functions.

  return Changed;
}
