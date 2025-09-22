//===- ObjCARCContract.cpp - ObjC ARC Optimization ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines late ObjC ARC optimizations. ARC stands for Automatic
/// Reference Counting and is a system for managing reference counts for objects
/// in Objective C.
///
/// This specific file mainly deals with ``contracting'' multiple lower level
/// operations into singular higher level operations through pattern matching.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

// TODO: ObjCARCContract could insert PHI nodes when uses aren't
// dominated by single calls.

#include "ARCRuntimeEntryPoints.h"
#include "DependencyAnalysis.h"
#include "ObjCARC.h"
#include "ProvenanceAnalysis.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ObjCARCUtil.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/ObjCARC.h"

using namespace llvm;
using namespace llvm::objcarc;

#define DEBUG_TYPE "objc-arc-contract"

STATISTIC(NumPeeps,       "Number of calls peephole-optimized");
STATISTIC(NumStoreStrongs, "Number objc_storeStrong calls formed");

//===----------------------------------------------------------------------===//
//                                Declarations
//===----------------------------------------------------------------------===//

namespace {
/// Late ARC optimizations
///
/// These change the IR in a way that makes it difficult to be analyzed by
/// ObjCARCOpt, so it's run late.

class ObjCARCContract {
  bool Changed;
  bool CFGChanged;
  AAResults *AA;
  DominatorTree *DT;
  ProvenanceAnalysis PA;
  ARCRuntimeEntryPoints EP;
  BundledRetainClaimRVs *BundledInsts = nullptr;

  /// The inline asm string to insert between calls and RetainRV calls to make
  /// the optimization work on targets which need it.
  const MDString *RVInstMarker;

  /// The set of inserted objc_storeStrong calls. If at the end of walking the
  /// function we have found no alloca instructions, these calls can be marked
  /// "tail".
  SmallPtrSet<CallInst *, 8> StoreStrongCalls;

  /// Returns true if we eliminated Inst.
  bool tryToPeepholeInstruction(
      Function &F, Instruction *Inst, inst_iterator &Iter,
      bool &TailOkForStoreStrong,
      const DenseMap<BasicBlock *, ColorVector> &BlockColors);

  bool optimizeRetainCall(Function &F, Instruction *Retain);

  bool contractAutorelease(Function &F, Instruction *Autorelease,
                           ARCInstKind Class);

  void tryToContractReleaseIntoStoreStrong(
      Instruction *Release, inst_iterator &Iter,
      const DenseMap<BasicBlock *, ColorVector> &BlockColors);

public:
  bool init(Module &M);
  bool run(Function &F, AAResults *AA, DominatorTree *DT);
  bool hasCFGChanged() const { return CFGChanged; }
};

class ObjCARCContractLegacyPass : public FunctionPass {
public:
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;

  static char ID;
  ObjCARCContractLegacyPass() : FunctionPass(ID) {
    initializeObjCARCContractLegacyPassPass(*PassRegistry::getPassRegistry());
  }
};
}

//===----------------------------------------------------------------------===//
//                               Implementation
//===----------------------------------------------------------------------===//

/// Turn objc_retain into objc_retainAutoreleasedReturnValue if the operand is a
/// return value. We do this late so we do not disrupt the dataflow analysis in
/// ObjCARCOpt.
bool ObjCARCContract::optimizeRetainCall(Function &F, Instruction *Retain) {
  const auto *Call = dyn_cast<CallBase>(GetArgRCIdentityRoot(Retain));
  if (!Call)
    return false;
  if (Call->getParent() != Retain->getParent())
    return false;

  // Check that the call is next to the retain.
  BasicBlock::const_iterator I = ++Call->getIterator();
  while (IsNoopInstruction(&*I))
    ++I;
  if (&*I != Retain)
    return false;

  // Turn it to an objc_retainAutoreleasedReturnValue.
  Changed = true;
  ++NumPeeps;

  LLVM_DEBUG(
      dbgs() << "Transforming objc_retain => "
                "objc_retainAutoreleasedReturnValue since the operand is a "
                "return value.\nOld: "
             << *Retain << "\n");

  // We do not have to worry about tail calls/does not throw since
  // retain/retainRV have the same properties.
  Function *Decl = EP.get(ARCRuntimeEntryPointKind::RetainRV);
  cast<CallInst>(Retain)->setCalledFunction(Decl);

  LLVM_DEBUG(dbgs() << "New: " << *Retain << "\n");
  return true;
}

/// Merge an autorelease with a retain into a fused call.
bool ObjCARCContract::contractAutorelease(Function &F, Instruction *Autorelease,
                                          ARCInstKind Class) {
  const Value *Arg = GetArgRCIdentityRoot(Autorelease);

  // Check that there are no instructions between the retain and the autorelease
  // (such as an autorelease_pop) which may change the count.
  DependenceKind DK = Class == ARCInstKind::AutoreleaseRV
                          ? RetainAutoreleaseRVDep
                          : RetainAutoreleaseDep;
  auto *Retain = dyn_cast_or_null<CallInst>(
      findSingleDependency(DK, Arg, Autorelease->getParent(), Autorelease, PA));

  if (!Retain || GetBasicARCInstKind(Retain) != ARCInstKind::Retain ||
      GetArgRCIdentityRoot(Retain) != Arg)
    return false;

  Changed = true;
  ++NumPeeps;

  LLVM_DEBUG(dbgs() << "    Fusing retain/autorelease!\n"
                       "        Autorelease:"
                    << *Autorelease
                    << "\n"
                       "        Retain: "
                    << *Retain << "\n");

  Function *Decl = EP.get(Class == ARCInstKind::AutoreleaseRV
                              ? ARCRuntimeEntryPointKind::RetainAutoreleaseRV
                              : ARCRuntimeEntryPointKind::RetainAutorelease);
  Retain->setCalledFunction(Decl);

  LLVM_DEBUG(dbgs() << "        New RetainAutorelease: " << *Retain << "\n");

  EraseInstruction(Autorelease);
  return true;
}

static StoreInst *findSafeStoreForStoreStrongContraction(LoadInst *Load,
                                                         Instruction *Release,
                                                         ProvenanceAnalysis &PA,
                                                         AAResults *AA) {
  StoreInst *Store = nullptr;
  bool SawRelease = false;

  // Get the location associated with Load.
  MemoryLocation Loc = MemoryLocation::get(Load);
  auto *LocPtr = Loc.Ptr->stripPointerCasts();

  // Walk down to find the store and the release, which may be in either order.
  for (auto I = std::next(BasicBlock::iterator(Load)),
            E = Load->getParent()->end();
       I != E; ++I) {
    // If we found the store we were looking for and saw the release,
    // break. There is no more work to be done.
    if (Store && SawRelease)
      break;

    // Now we know that we have not seen either the store or the release. If I
    // is the release, mark that we saw the release and continue.
    Instruction *Inst = &*I;
    if (Inst == Release) {
      SawRelease = true;
      continue;
    }

    // Otherwise, we check if Inst is a "good" store. Grab the instruction class
    // of Inst.
    ARCInstKind Class = GetBasicARCInstKind(Inst);

    // If we have seen the store, but not the release...
    if (Store) {
      // We need to make sure that it is safe to move the release from its
      // current position to the store. This implies proving that any
      // instruction in between Store and the Release conservatively can not use
      // the RCIdentityRoot of Release. If we can prove we can ignore Inst, so
      // continue...
      if (!CanUse(Inst, Load, PA, Class)) {
        continue;
      }

      // Otherwise, be conservative and return nullptr.
      return nullptr;
    }

    // Ok, now we know we have not seen a store yet.

    // If Inst is a retain, we don't care about it as it doesn't prevent moving
    // the load to the store.
    //
    // TODO: This is one area where the optimization could be made more
    // aggressive.
    if (IsRetain(Class))
      continue;

    // See if Inst can write to our load location, if it can not, just ignore
    // the instruction.
    if (!isModSet(AA->getModRefInfo(Inst, Loc)))
      continue;

    Store = dyn_cast<StoreInst>(Inst);

    // If Inst can, then check if Inst is a simple store. If Inst is not a
    // store or a store that is not simple, then we have some we do not
    // understand writing to this memory implying we can not move the load
    // over the write to any subsequent store that we may find.
    if (!Store || !Store->isSimple())
      return nullptr;

    // Then make sure that the pointer we are storing to is Ptr. If so, we
    // found our Store!
    if (Store->getPointerOperand()->stripPointerCasts() == LocPtr)
      continue;

    // Otherwise, we have an unknown store to some other ptr that clobbers
    // Loc.Ptr. Bail!
    return nullptr;
  }

  // If we did not find the store or did not see the release, fail.
  if (!Store || !SawRelease)
    return nullptr;

  // We succeeded!
  return Store;
}

static Instruction *
findRetainForStoreStrongContraction(Value *New, StoreInst *Store,
                                    Instruction *Release,
                                    ProvenanceAnalysis &PA) {
  // Walk up from the Store to find the retain.
  BasicBlock::iterator I = Store->getIterator();
  BasicBlock::iterator Begin = Store->getParent()->begin();
  while (I != Begin && GetBasicARCInstKind(&*I) != ARCInstKind::Retain) {
    Instruction *Inst = &*I;

    // It is only safe to move the retain to the store if we can prove
    // conservatively that nothing besides the release can decrement reference
    // counts in between the retain and the store.
    if (CanDecrementRefCount(Inst, New, PA) && Inst != Release)
      return nullptr;
    --I;
  }
  Instruction *Retain = &*I;
  if (GetBasicARCInstKind(Retain) != ARCInstKind::Retain)
    return nullptr;
  if (GetArgRCIdentityRoot(Retain) != New)
    return nullptr;
  return Retain;
}

/// Attempt to merge an objc_release with a store, load, and objc_retain to form
/// an objc_storeStrong. An objc_storeStrong:
///
///   objc_storeStrong(i8** %old_ptr, i8* new_value)
///
/// is equivalent to the following IR sequence:
///
///   ; Load old value.
///   %old_value = load i8** %old_ptr               (1)
///
///   ; Increment the new value and then release the old value. This must occur
///   ; in order in case old_value releases new_value in its destructor causing
///   ; us to potentially have a dangling ptr.
///   tail call i8* @objc_retain(i8* %new_value)    (2)
///   tail call void @objc_release(i8* %old_value)  (3)
///
///   ; Store the new_value into old_ptr
///   store i8* %new_value, i8** %old_ptr           (4)
///
/// The safety of this optimization is based around the following
/// considerations:
///
///  1. We are forming the store strong at the store. Thus to perform this
///     optimization it must be safe to move the retain, load, and release to
///     (4).
///  2. We need to make sure that any re-orderings of (1), (2), (3), (4) are
///     safe.
void ObjCARCContract::tryToContractReleaseIntoStoreStrong(
    Instruction *Release, inst_iterator &Iter,
    const DenseMap<BasicBlock *, ColorVector> &BlockColors) {
  // See if we are releasing something that we just loaded.
  auto *Load = dyn_cast<LoadInst>(GetArgRCIdentityRoot(Release));
  if (!Load || !Load->isSimple())
    return;

  // For now, require everything to be in one basic block.
  BasicBlock *BB = Release->getParent();
  if (Load->getParent() != BB)
    return;

  // First scan down the BB from Load, looking for a store of the RCIdentityRoot
  // of Load's
  StoreInst *Store =
      findSafeStoreForStoreStrongContraction(Load, Release, PA, AA);
  // If we fail, bail.
  if (!Store)
    return;

  // Then find what new_value's RCIdentity Root is.
  Value *New = GetRCIdentityRoot(Store->getValueOperand());

  // Then walk up the BB and look for a retain on New without any intervening
  // instructions which conservatively might decrement ref counts.
  Instruction *Retain =
      findRetainForStoreStrongContraction(New, Store, Release, PA);

  // If we fail, bail.
  if (!Retain)
    return;

  Changed = true;
  ++NumStoreStrongs;

  LLVM_DEBUG(
      llvm::dbgs() << "    Contracting retain, release into objc_storeStrong.\n"
                   << "        Old:\n"
                   << "            Store:   " << *Store << "\n"
                   << "            Release: " << *Release << "\n"
                   << "            Retain:  " << *Retain << "\n"
                   << "            Load:    " << *Load << "\n");

  LLVMContext &C = Release->getContext();
  Type *I8X = PointerType::getUnqual(Type::getInt8Ty(C));
  Type *I8XX = PointerType::getUnqual(I8X);

  Value *Args[] = { Load->getPointerOperand(), New };
  if (Args[0]->getType() != I8XX)
    Args[0] = new BitCastInst(Args[0], I8XX, "", Store->getIterator());
  if (Args[1]->getType() != I8X)
    Args[1] = new BitCastInst(Args[1], I8X, "", Store->getIterator());
  Function *Decl = EP.get(ARCRuntimeEntryPointKind::StoreStrong);
  CallInst *StoreStrong = objcarc::createCallInstWithColors(
      Decl, Args, "", Store->getIterator(), BlockColors);
  StoreStrong->setDoesNotThrow();
  StoreStrong->setDebugLoc(Store->getDebugLoc());

  // We can't set the tail flag yet, because we haven't yet determined
  // whether there are any escaping allocas. Remember this call, so that
  // we can set the tail flag once we know it's safe.
  StoreStrongCalls.insert(StoreStrong);

  LLVM_DEBUG(llvm::dbgs() << "        New Store Strong: " << *StoreStrong
                          << "\n");

  if (&*Iter == Retain) ++Iter;
  if (&*Iter == Store) ++Iter;
  Store->eraseFromParent();
  Release->eraseFromParent();
  EraseInstruction(Retain);
  if (Load->use_empty())
    Load->eraseFromParent();
}

bool ObjCARCContract::tryToPeepholeInstruction(
    Function &F, Instruction *Inst, inst_iterator &Iter,
    bool &TailOkForStoreStrongs,
    const DenseMap<BasicBlock *, ColorVector> &BlockColors) {
  // Only these library routines return their argument. In particular,
  // objc_retainBlock does not necessarily return its argument.
  ARCInstKind Class = GetBasicARCInstKind(Inst);
  switch (Class) {
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
    return false;
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
    return contractAutorelease(F, Inst, Class);
  case ARCInstKind::Retain:
    // Attempt to convert retains to retainrvs if they are next to function
    // calls.
    if (!optimizeRetainCall(F, Inst))
      return false;
    // If we succeed in our optimization, fall through.
    [[fallthrough]];
  case ARCInstKind::RetainRV:
  case ARCInstKind::UnsafeClaimRV: {
    // Return true if this is a bundled retainRV/claimRV call, which is always
    // redundant with the attachedcall in the bundle, and is going to be erased
    // at the end of this pass.  This avoids undoing objc-arc-expand and
    // replacing uses of the retainRV/claimRV call's argument with its result.
    if (BundledInsts->contains(Inst))
      return true;

    // If this isn't a bundled call, and the target doesn't need a special
    // inline-asm marker, we're done: return now, and undo objc-arc-expand.
    if (!RVInstMarker)
      return false;

    // The target needs a special inline-asm marker.  Insert it.

    BasicBlock::iterator BBI = Inst->getIterator();
    BasicBlock *InstParent = Inst->getParent();

    // Step up to see if the call immediately precedes the RV call.
    // If it's an invoke, we have to cross a block boundary. And we have
    // to carefully dodge no-op instructions.
    do {
      if (BBI == InstParent->begin()) {
        BasicBlock *Pred = InstParent->getSinglePredecessor();
        if (!Pred)
          goto decline_rv_optimization;
        BBI = Pred->getTerminator()->getIterator();
        break;
      }
      --BBI;
    } while (IsNoopInstruction(&*BBI));

    if (GetRCIdentityRoot(&*BBI) == GetArgRCIdentityRoot(Inst)) {
      LLVM_DEBUG(dbgs() << "Adding inline asm marker for the return value "
                           "optimization.\n");
      Changed = true;
      InlineAsm *IA =
          InlineAsm::get(FunctionType::get(Type::getVoidTy(Inst->getContext()),
                                           /*isVarArg=*/false),
                         RVInstMarker->getString(),
                         /*Constraints=*/"", /*hasSideEffects=*/true);

      objcarc::createCallInstWithColors(IA, std::nullopt, "",
                                        Inst->getIterator(), BlockColors);
    }
  decline_rv_optimization:
    return false;
  }
  case ARCInstKind::InitWeak: {
    // objc_initWeak(p, null) => *p = null
    CallInst *CI = cast<CallInst>(Inst);
    if (IsNullOrUndef(CI->getArgOperand(1))) {
      Value *Null = ConstantPointerNull::get(cast<PointerType>(CI->getType()));
      Changed = true;
      new StoreInst(Null, CI->getArgOperand(0), CI->getIterator());

      LLVM_DEBUG(dbgs() << "OBJCARCContract: Old = " << *CI << "\n"
                        << "                 New = " << *Null << "\n");

      CI->replaceAllUsesWith(Null);
      CI->eraseFromParent();
    }
    return true;
  }
  case ARCInstKind::Release:
    // Try to form an objc store strong from our release. If we fail, there is
    // nothing further to do below, so continue.
    tryToContractReleaseIntoStoreStrong(Inst, Iter, BlockColors);
    return true;
  case ARCInstKind::User:
    // Be conservative if the function has any alloca instructions.
    // Technically we only care about escaping alloca instructions,
    // but this is sufficient to handle some interesting cases.
    if (isa<AllocaInst>(Inst))
      TailOkForStoreStrongs = false;
    return true;
  case ARCInstKind::IntrinsicUser:
    // Remove calls to @llvm.objc.clang.arc.use(...).
    Changed = true;
    Inst->eraseFromParent();
    return true;
  default:
    if (auto *CI = dyn_cast<CallInst>(Inst))
      if (CI->getIntrinsicID() == Intrinsic::objc_clang_arc_noop_use) {
        // Remove calls to @llvm.objc.clang.arc.noop.use(...).
        Changed = true;
        CI->eraseFromParent();
      }
    return true;
  }
}

//===----------------------------------------------------------------------===//
//                              Top Level Driver
//===----------------------------------------------------------------------===//

bool ObjCARCContract::init(Module &M) {
  EP.init(&M);

  // Initialize RVInstMarker.
  RVInstMarker = getRVInstMarker(M);

  return false;
}

bool ObjCARCContract::run(Function &F, AAResults *A, DominatorTree *D) {
  if (!EnableARCOpts)
    return false;

  Changed = CFGChanged = false;
  AA = A;
  DT = D;
  PA.setAA(A);
  BundledRetainClaimRVs BRV(/*ContractPass=*/true);
  BundledInsts = &BRV;

  std::pair<bool, bool> R = BundledInsts->insertAfterInvokes(F, DT);
  Changed |= R.first;
  CFGChanged |= R.second;

  DenseMap<BasicBlock *, ColorVector> BlockColors;
  if (F.hasPersonalityFn() &&
      isScopedEHPersonality(classifyEHPersonality(F.getPersonalityFn())))
    BlockColors = colorEHFunclets(F);

  LLVM_DEBUG(llvm::dbgs() << "**** ObjCARC Contract ****\n");

  // Track whether it's ok to mark objc_storeStrong calls with the "tail"
  // keyword. Be conservative if the function has variadic arguments.
  // It seems that functions which "return twice" are also unsafe for the
  // "tail" argument, because they are setjmp, which could need to
  // return to an earlier stack state.
  bool TailOkForStoreStrongs =
      !F.isVarArg() && !F.callsFunctionThatReturnsTwice();

  // For ObjC library calls which return their argument, replace uses of the
  // argument with uses of the call return value, if it dominates the use. This
  // reduces register pressure.
  for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E;) {
    Instruction *Inst = &*I++;

    LLVM_DEBUG(dbgs() << "Visiting: " << *Inst << "\n");

    if (auto *CI = dyn_cast<CallInst>(Inst))
      if (objcarc::hasAttachedCallOpBundle(CI)) {
        BundledInsts->insertRVCallWithColors(I->getIterator(), CI, BlockColors);
        --I;
        Changed = true;
      }

    // First try to peephole Inst. If there is nothing further we can do in
    // terms of undoing objc-arc-expand, process the next inst.
    if (tryToPeepholeInstruction(F, Inst, I, TailOkForStoreStrongs,
                                 BlockColors))
      continue;

    // Otherwise, try to undo objc-arc-expand.

    // Don't use GetArgRCIdentityRoot because we don't want to look through bitcasts
    // and such; to do the replacement, the argument must have type i8*.

    // Function for replacing uses of Arg dominated by Inst.
    auto ReplaceArgUses = [Inst, this](Value *Arg) {
      // If we're compiling bugpointed code, don't get in trouble.
      if (!isa<Instruction>(Arg) && !isa<Argument>(Arg))
        return;

      // Look through the uses of the pointer.
      for (Value::use_iterator UI = Arg->use_begin(), UE = Arg->use_end();
           UI != UE; ) {
        // Increment UI now, because we may unlink its element.
        Use &U = *UI++;
        unsigned OperandNo = U.getOperandNo();

        // If the call's return value dominates a use of the call's argument
        // value, rewrite the use to use the return value. We check for
        // reachability here because an unreachable call is considered to
        // trivially dominate itself, which would lead us to rewriting its
        // argument in terms of its return value, which would lead to
        // infinite loops in GetArgRCIdentityRoot.
        if (!DT->isReachableFromEntry(U) || !DT->dominates(Inst, U))
          continue;

        Changed = true;
        Instruction *Replacement = Inst;
        Type *UseTy = U.get()->getType();
        if (PHINode *PHI = dyn_cast<PHINode>(U.getUser())) {
          // For PHI nodes, insert the bitcast in the predecessor block.
          unsigned ValNo = PHINode::getIncomingValueNumForOperand(OperandNo);
          BasicBlock *IncomingBB = PHI->getIncomingBlock(ValNo);
          if (Replacement->getType() != UseTy) {
            // A catchswitch is both a pad and a terminator, meaning a basic
            // block with a catchswitch has no insertion point. Keep going up
            // the dominator tree until we find a non-catchswitch.
            BasicBlock *InsertBB = IncomingBB;
            while (isa<CatchSwitchInst>(InsertBB->getFirstNonPHI())) {
              InsertBB = DT->getNode(InsertBB)->getIDom()->getBlock();
            }

            assert(DT->dominates(Inst, &InsertBB->back()) &&
                   "Invalid insertion point for bitcast");
            Replacement = new BitCastInst(Replacement, UseTy, "",
                                          InsertBB->back().getIterator());
          }

          // While we're here, rewrite all edges for this PHI, rather
          // than just one use at a time, to minimize the number of
          // bitcasts we emit.
          for (unsigned i = 0, e = PHI->getNumIncomingValues(); i != e; ++i)
            if (PHI->getIncomingBlock(i) == IncomingBB) {
              // Keep the UI iterator valid.
              if (UI != UE &&
                  &PHI->getOperandUse(
                      PHINode::getOperandNumForIncomingValue(i)) == &*UI)
                ++UI;
              PHI->setIncomingValue(i, Replacement);
            }
        } else {
          if (Replacement->getType() != UseTy)
            Replacement =
                new BitCastInst(Replacement, UseTy, "",
                                cast<Instruction>(U.getUser())->getIterator());
          U.set(Replacement);
        }
      }
    };

    Value *Arg = cast<CallInst>(Inst)->getArgOperand(0);
    Value *OrigArg = Arg;

    // TODO: Change this to a do-while.
    for (;;) {
      ReplaceArgUses(Arg);

      // If Arg is a no-op casted pointer, strip one level of casts and iterate.
      if (const BitCastInst *BI = dyn_cast<BitCastInst>(Arg))
        Arg = BI->getOperand(0);
      else if (isa<GEPOperator>(Arg) &&
               cast<GEPOperator>(Arg)->hasAllZeroIndices())
        Arg = cast<GEPOperator>(Arg)->getPointerOperand();
      else if (isa<GlobalAlias>(Arg) &&
               !cast<GlobalAlias>(Arg)->isInterposable())
        Arg = cast<GlobalAlias>(Arg)->getAliasee();
      else {
        // If Arg is a PHI node, get PHIs that are equivalent to it and replace
        // their uses.
        if (PHINode *PN = dyn_cast<PHINode>(Arg)) {
          SmallVector<Value *, 1> PHIList;
          getEquivalentPHIs(*PN, PHIList);
          for (Value *PHI : PHIList)
            ReplaceArgUses(PHI);
        }
        break;
      }
    }

    // Replace bitcast users of Arg that are dominated by Inst.
    SmallVector<BitCastInst *, 2> BitCastUsers;

    // Add all bitcast users of the function argument first.
    for (User *U : OrigArg->users())
      if (auto *BC = dyn_cast<BitCastInst>(U))
        BitCastUsers.push_back(BC);

    // Replace the bitcasts with the call return. Iterate until list is empty.
    while (!BitCastUsers.empty()) {
      auto *BC = BitCastUsers.pop_back_val();
      for (User *U : BC->users())
        if (auto *B = dyn_cast<BitCastInst>(U))
          BitCastUsers.push_back(B);

      ReplaceArgUses(BC);
    }
  }

  // If this function has no escaping allocas or suspicious vararg usage,
  // objc_storeStrong calls can be marked with the "tail" keyword.
  if (TailOkForStoreStrongs)
    for (CallInst *CI : StoreStrongCalls)
      CI->setTailCall();
  StoreStrongCalls.clear();

  return Changed;
}

//===----------------------------------------------------------------------===//
//                             Misc Pass Manager
//===----------------------------------------------------------------------===//

char ObjCARCContractLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(ObjCARCContractLegacyPass, "objc-arc-contract",
                      "ObjC ARC contraction", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(ObjCARCContractLegacyPass, "objc-arc-contract",
                    "ObjC ARC contraction", false, false)

void ObjCARCContractLegacyPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AAResultsWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
}

Pass *llvm::createObjCARCContractPass() {
  return new ObjCARCContractLegacyPass();
}

bool ObjCARCContractLegacyPass::runOnFunction(Function &F) {
  ObjCARCContract OCARCC;
  OCARCC.init(*F.getParent());
  auto *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  auto *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  return OCARCC.run(F, AA, DT);
}

PreservedAnalyses ObjCARCContractPass::run(Function &F,
                                           FunctionAnalysisManager &AM) {
  ObjCARCContract OCAC;
  OCAC.init(*F.getParent());

  bool Changed = OCAC.run(F, &AM.getResult<AAManager>(F),
                          &AM.getResult<DominatorTreeAnalysis>(F));
  bool CFGChanged = OCAC.hasCFGChanged();
  if (Changed) {
    PreservedAnalyses PA;
    if (!CFGChanged)
      PA.preserveSet<CFGAnalyses>();
    return PA;
  }
  return PreservedAnalyses::all();
}
