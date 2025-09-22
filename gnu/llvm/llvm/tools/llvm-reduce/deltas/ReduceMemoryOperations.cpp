//===- ReduceOpcodes.cpp - Specialized Delta Pass -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ReduceMemoryOperations.h"
#include "Delta.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;

static void removeVolatileInFunction(Oracle &O, Function &F) {
  LLVMContext &Ctx = F.getContext();
  for (Instruction &I : instructions(F)) {
    if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
      if (LI->isVolatile() && !O.shouldKeep())
        LI->setVolatile(false);
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
      if (SI->isVolatile() && !O.shouldKeep())
        SI->setVolatile(false);
    } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(&I)) {
      if (RMW->isVolatile() && !O.shouldKeep())
        RMW->setVolatile(false);
    } else if (AtomicCmpXchgInst *CmpXChg = dyn_cast<AtomicCmpXchgInst>(&I)) {
      if (CmpXChg->isVolatile() && !O.shouldKeep())
        CmpXChg->setVolatile(false);
    } else if (MemIntrinsic *MemIntrin = dyn_cast<MemIntrinsic>(&I)) {
      if (MemIntrin->isVolatile() && !O.shouldKeep())
        MemIntrin->setVolatile(ConstantInt::getFalse(Ctx));
    }
  }
}

static void removeVolatileInModule(Oracle &O, ReducerWorkItem &WorkItem) {
  for (Function &F : WorkItem.getModule())
    removeVolatileInFunction(O, F);
}

void llvm::reduceVolatileInstructionsDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, removeVolatileInModule, "Reducing Volatile Instructions");
}

static void reduceAtomicSyncScopesInFunction(Oracle &O, Function &F) {
  for (Instruction &I : instructions(F)) {
    if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
      if (LI->getSyncScopeID() != SyncScope::System && !O.shouldKeep())
        LI->setSyncScopeID(SyncScope::System);
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
      if (SI->getSyncScopeID() != SyncScope::System && !O.shouldKeep())
        SI->setSyncScopeID(SyncScope::System);
    } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(&I)) {
      if (RMW->getSyncScopeID() != SyncScope::System && !O.shouldKeep())
        RMW->setSyncScopeID(SyncScope::System);
    } else if (AtomicCmpXchgInst *CmpXChg = dyn_cast<AtomicCmpXchgInst>(&I)) {
      if (CmpXChg->getSyncScopeID() != SyncScope::System && !O.shouldKeep())
        CmpXChg->setSyncScopeID(SyncScope::System);
    } else if (FenceInst *Fence = dyn_cast<FenceInst>(&I)) {
      if (Fence->getSyncScopeID() != SyncScope::System && !O.shouldKeep())
        Fence->setSyncScopeID(SyncScope::System);
    }
  }
}

static void reduceAtomicSyncScopesInModule(Oracle &O,
                                           ReducerWorkItem &WorkItem) {
  for (Function &F : WorkItem.getModule())
    reduceAtomicSyncScopesInFunction(O, F);
}

void llvm::reduceAtomicSyncScopesDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, reduceAtomicSyncScopesInModule,
               "Reducing Atomic Sync Scopes");
}

// TODO: Might be helpful to incrementally relax orders
static void reduceAtomicOrderingInFunction(Oracle &O, Function &F) {
  for (Instruction &I : instructions(F)) {
    if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
      if (LI->getOrdering() != AtomicOrdering::NotAtomic && !O.shouldKeep())
        LI->setAtomic(AtomicOrdering::NotAtomic);
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
      if (SI->getOrdering() != AtomicOrdering::NotAtomic && !O.shouldKeep())
        SI->setAtomic(AtomicOrdering::NotAtomic);
    } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(&I)) {
      if (RMW->getOrdering() != AtomicOrdering::Monotonic && !O.shouldKeep())
        RMW->setOrdering(AtomicOrdering::Monotonic);
    } else if (AtomicCmpXchgInst *CmpXChg = dyn_cast<AtomicCmpXchgInst>(&I)) {
      if (CmpXChg->getSuccessOrdering() != AtomicOrdering::Monotonic &&
          !O.shouldKeep())
        CmpXChg->setSuccessOrdering(AtomicOrdering::Monotonic);
      if (CmpXChg->getFailureOrdering() != AtomicOrdering::Monotonic &&
          !O.shouldKeep())
        CmpXChg->setFailureOrdering(AtomicOrdering::Monotonic);
    }
  }
}

static void reduceAtomicOrderingInModule(Oracle &O, ReducerWorkItem &WorkItem) {
  for (Function &F : WorkItem.getModule())
    reduceAtomicOrderingInFunction(O, F);
}

void llvm::reduceAtomicOrderingDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, reduceAtomicOrderingInModule, "Reducing Atomic Ordering");
}
