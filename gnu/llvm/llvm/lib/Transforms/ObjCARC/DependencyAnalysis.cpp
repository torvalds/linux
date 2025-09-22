//===- DependencyAnalysis.cpp - ObjC ARC Optimization ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines special dependency analysis routines used in Objective C
/// ARC Optimizations.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#include "DependencyAnalysis.h"
#include "ObjCARC.h"
#include "ProvenanceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/CFG.h"

using namespace llvm;
using namespace llvm::objcarc;

#define DEBUG_TYPE "objc-arc-dependency"

/// Test whether the given instruction can result in a reference count
/// modification (positive or negative) for the pointer's object.
bool llvm::objcarc::CanAlterRefCount(const Instruction *Inst, const Value *Ptr,
                                     ProvenanceAnalysis &PA,
                                     ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::User:
    // These operations never directly modify a reference count.
    return false;
  default: break;
  }

  const auto *Call = cast<CallBase>(Inst);

  // See if AliasAnalysis can help us with the call.
  MemoryEffects ME = PA.getAA()->getMemoryEffects(Call);
  if (ME.onlyReadsMemory())
    return false;
  if (ME.onlyAccessesArgPointees()) {
    for (const Value *Op : Call->args()) {
      if (IsPotentialRetainableObjPtr(Op, *PA.getAA()) && PA.related(Ptr, Op))
        return true;
    }
    return false;
  }

  // Assume the worst.
  return true;
}

bool llvm::objcarc::CanDecrementRefCount(const Instruction *Inst,
                                         const Value *Ptr,
                                         ProvenanceAnalysis &PA,
                                         ARCInstKind Class) {
  // First perform a quick check if Class can not touch ref counts.
  if (!CanDecrementRefCount(Class))
    return false;

  // Otherwise, just use CanAlterRefCount for now.
  return CanAlterRefCount(Inst, Ptr, PA, Class);
}

/// Test whether the given instruction can "use" the given pointer's object in a
/// way that requires the reference count to be positive.
bool llvm::objcarc::CanUse(const Instruction *Inst, const Value *Ptr,
                           ProvenanceAnalysis &PA, ARCInstKind Class) {
  // ARCInstKind::Call operations (as opposed to
  // ARCInstKind::CallOrUser) never "use" objc pointers.
  if (Class == ARCInstKind::Call)
    return false;

  // Consider various instructions which may have pointer arguments which are
  // not "uses".
  if (const ICmpInst *ICI = dyn_cast<ICmpInst>(Inst)) {
    // Comparing a pointer with null, or any other constant, isn't really a use,
    // because we don't care what the pointer points to, or about the values
    // of any other dynamic reference-counted pointers.
    if (!IsPotentialRetainableObjPtr(ICI->getOperand(1), *PA.getAA()))
      return false;
  } else if (const auto *CS = dyn_cast<CallBase>(Inst)) {
    // For calls, just check the arguments (and not the callee operand).
    for (const Value *Op : CS->args())
      if (IsPotentialRetainableObjPtr(Op, *PA.getAA()) && PA.related(Ptr, Op))
        return true;
    return false;
  } else if (const StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
    // Special-case stores, because we don't care about the stored value, just
    // the store address.
    const Value *Op = GetUnderlyingObjCPtr(SI->getPointerOperand());
    // If we can't tell what the underlying object was, assume there is a
    // dependence.
    return IsPotentialRetainableObjPtr(Op, *PA.getAA()) && PA.related(Op, Ptr);
  }

  // Check each operand for a match.
  for (const Use &U : Inst->operands()) {
    const Value *Op = U;
    if (IsPotentialRetainableObjPtr(Op, *PA.getAA()) && PA.related(Ptr, Op))
      return true;
  }
  return false;
}

/// Test if there can be dependencies on Inst through Arg. This function only
/// tests dependencies relevant for removing pairs of calls.
bool
llvm::objcarc::Depends(DependenceKind Flavor, Instruction *Inst,
                       const Value *Arg, ProvenanceAnalysis &PA) {
  // If we've reached the definition of Arg, stop.
  if (Inst == Arg)
    return true;

  switch (Flavor) {
  case NeedsPositiveRetainCount: {
    ARCInstKind Class = GetARCInstKind(Inst);
    switch (Class) {
    case ARCInstKind::AutoreleasepoolPop:
    case ARCInstKind::AutoreleasepoolPush:
    case ARCInstKind::None:
      return false;
    default:
      return CanUse(Inst, Arg, PA, Class);
    }
  }

  case AutoreleasePoolBoundary: {
    ARCInstKind Class = GetARCInstKind(Inst);
    switch (Class) {
    case ARCInstKind::AutoreleasepoolPop:
    case ARCInstKind::AutoreleasepoolPush:
      // These mark the end and begin of an autorelease pool scope.
      return true;
    default:
      // Nothing else does this.
      return false;
    }
  }

  case CanChangeRetainCount: {
    ARCInstKind Class = GetARCInstKind(Inst);
    switch (Class) {
    case ARCInstKind::AutoreleasepoolPop:
      // Conservatively assume this can decrement any count.
      return true;
    case ARCInstKind::AutoreleasepoolPush:
    case ARCInstKind::None:
      return false;
    default:
      return CanAlterRefCount(Inst, Arg, PA, Class);
    }
  }

  case RetainAutoreleaseDep:
    switch (GetBasicARCInstKind(Inst)) {
    case ARCInstKind::AutoreleasepoolPop:
    case ARCInstKind::AutoreleasepoolPush:
      // Don't merge an objc_autorelease with an objc_retain inside a different
      // autoreleasepool scope.
      return true;
    case ARCInstKind::Retain:
    case ARCInstKind::RetainRV:
      // Check for a retain of the same pointer for merging.
      return GetArgRCIdentityRoot(Inst) == Arg;
    default:
      // Nothing else matters for objc_retainAutorelease formation.
      return false;
    }

  case RetainAutoreleaseRVDep: {
    ARCInstKind Class = GetBasicARCInstKind(Inst);
    switch (Class) {
    case ARCInstKind::Retain:
    case ARCInstKind::RetainRV:
      // Check for a retain of the same pointer for merging.
      return GetArgRCIdentityRoot(Inst) == Arg;
    default:
      // Anything that can autorelease interrupts
      // retainAutoreleaseReturnValue formation.
      return CanInterruptRV(Class);
    }
  }
  }

  llvm_unreachable("Invalid dependence flavor");
}

/// Walk up the CFG from StartPos (which is in StartBB) and find local and
/// non-local dependencies on Arg.
///
/// TODO: Cache results?
static bool findDependencies(DependenceKind Flavor, const Value *Arg,
                             BasicBlock *StartBB, Instruction *StartInst,
                             SmallPtrSetImpl<Instruction *> &DependingInsts,
                             ProvenanceAnalysis &PA) {
  BasicBlock::iterator StartPos = StartInst->getIterator();

  SmallPtrSet<const BasicBlock *, 4> Visited;
  SmallVector<std::pair<BasicBlock *, BasicBlock::iterator>, 4> Worklist;
  Worklist.push_back(std::make_pair(StartBB, StartPos));
  do {
    std::pair<BasicBlock *, BasicBlock::iterator> Pair =
      Worklist.pop_back_val();
    BasicBlock *LocalStartBB = Pair.first;
    BasicBlock::iterator LocalStartPos = Pair.second;
    BasicBlock::iterator StartBBBegin = LocalStartBB->begin();
    for (;;) {
      if (LocalStartPos == StartBBBegin) {
        if (pred_empty(LocalStartBB))
          // Return if we've reached the function entry.
          return false;
        // Add the predecessors to the worklist.
        for (BasicBlock *PredBB : predecessors(LocalStartBB))
          if (Visited.insert(PredBB).second)
            Worklist.push_back(std::make_pair(PredBB, PredBB->end()));
        break;
      }

      Instruction *Inst = &*--LocalStartPos;
      if (Depends(Flavor, Inst, Arg, PA)) {
        DependingInsts.insert(Inst);
        break;
      }
    }
  } while (!Worklist.empty());

  // Determine whether the original StartBB post-dominates all of the blocks we
  // visited. If not, insert a sentinel indicating that most optimizations are
  // not safe.
  for (const BasicBlock *BB : Visited) {
    if (BB == StartBB)
      continue;
    for (const BasicBlock *Succ : successors(BB))
      if (Succ != StartBB && !Visited.count(Succ))
        return false;
  }

  return true;
}

llvm::Instruction *llvm::objcarc::findSingleDependency(DependenceKind Flavor,
                                                       const Value *Arg,
                                                       BasicBlock *StartBB,
                                                       Instruction *StartInst,
                                                       ProvenanceAnalysis &PA) {
  SmallPtrSet<Instruction *, 4> DependingInsts;

  if (!findDependencies(Flavor, Arg, StartBB, StartInst, DependingInsts, PA) ||
      DependingInsts.size() != 1)
    return nullptr;
  return *DependingInsts.begin();
}
