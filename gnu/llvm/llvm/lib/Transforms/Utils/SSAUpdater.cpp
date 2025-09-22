//===- SSAUpdater.cpp - Unstructured SSA Update Tool ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SSAUpdater class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/SSAUpdaterImpl.h"
#include <cassert>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "ssaupdater"

using AvailableValsTy = DenseMap<BasicBlock *, Value *>;

static AvailableValsTy &getAvailableVals(void *AV) {
  return *static_cast<AvailableValsTy*>(AV);
}

SSAUpdater::SSAUpdater(SmallVectorImpl<PHINode *> *NewPHI)
  : InsertedPHIs(NewPHI) {}

SSAUpdater::~SSAUpdater() {
  delete static_cast<AvailableValsTy*>(AV);
}

void SSAUpdater::Initialize(Type *Ty, StringRef Name) {
  if (!AV)
    AV = new AvailableValsTy();
  else
    getAvailableVals(AV).clear();
  ProtoType = Ty;
  ProtoName = std::string(Name);
}

bool SSAUpdater::HasValueForBlock(BasicBlock *BB) const {
  return getAvailableVals(AV).count(BB);
}

Value *SSAUpdater::FindValueForBlock(BasicBlock *BB) const {
  return getAvailableVals(AV).lookup(BB);
}

void SSAUpdater::AddAvailableValue(BasicBlock *BB, Value *V) {
  assert(ProtoType && "Need to initialize SSAUpdater");
  assert(ProtoType == V->getType() &&
         "All rewritten values must have the same type");
  getAvailableVals(AV)[BB] = V;
}

static bool IsEquivalentPHI(PHINode *PHI,
                        SmallDenseMap<BasicBlock *, Value *, 8> &ValueMapping) {
  unsigned PHINumValues = PHI->getNumIncomingValues();
  if (PHINumValues != ValueMapping.size())
    return false;

  // Scan the phi to see if it matches.
  for (unsigned i = 0, e = PHINumValues; i != e; ++i)
    if (ValueMapping[PHI->getIncomingBlock(i)] !=
        PHI->getIncomingValue(i)) {
      return false;
    }

  return true;
}

Value *SSAUpdater::GetValueAtEndOfBlock(BasicBlock *BB) {
  Value *Res = GetValueAtEndOfBlockInternal(BB);
  return Res;
}

Value *SSAUpdater::GetValueInMiddleOfBlock(BasicBlock *BB) {
  // If there is no definition of the renamed variable in this block, just use
  // GetValueAtEndOfBlock to do our work.
  if (!HasValueForBlock(BB))
    return GetValueAtEndOfBlock(BB);

  // Otherwise, we have the hard case.  Get the live-in values for each
  // predecessor.
  SmallVector<std::pair<BasicBlock *, Value *>, 8> PredValues;
  Value *SingularValue = nullptr;

  // We can get our predecessor info by walking the pred_iterator list, but it
  // is relatively slow.  If we already have PHI nodes in this block, walk one
  // of them to get the predecessor list instead.
  if (PHINode *SomePhi = dyn_cast<PHINode>(BB->begin())) {
    for (unsigned i = 0, e = SomePhi->getNumIncomingValues(); i != e; ++i) {
      BasicBlock *PredBB = SomePhi->getIncomingBlock(i);
      Value *PredVal = GetValueAtEndOfBlock(PredBB);
      PredValues.push_back(std::make_pair(PredBB, PredVal));

      // Compute SingularValue.
      if (i == 0)
        SingularValue = PredVal;
      else if (PredVal != SingularValue)
        SingularValue = nullptr;
    }
  } else {
    bool isFirstPred = true;
    for (BasicBlock *PredBB : predecessors(BB)) {
      Value *PredVal = GetValueAtEndOfBlock(PredBB);
      PredValues.push_back(std::make_pair(PredBB, PredVal));

      // Compute SingularValue.
      if (isFirstPred) {
        SingularValue = PredVal;
        isFirstPred = false;
      } else if (PredVal != SingularValue)
        SingularValue = nullptr;
    }
  }

  // If there are no predecessors, just return poison.
  if (PredValues.empty())
    return PoisonValue::get(ProtoType);

  // Otherwise, if all the merged values are the same, just use it.
  if (SingularValue)
    return SingularValue;

  // Otherwise, we do need a PHI: check to see if we already have one available
  // in this block that produces the right value.
  if (isa<PHINode>(BB->begin())) {
    SmallDenseMap<BasicBlock *, Value *, 8> ValueMapping(PredValues.begin(),
                                                         PredValues.end());
    for (PHINode &SomePHI : BB->phis()) {
      if (IsEquivalentPHI(&SomePHI, ValueMapping))
        return &SomePHI;
    }
  }

  // Ok, we have no way out, insert a new one now.
  PHINode *InsertedPHI =
      PHINode::Create(ProtoType, PredValues.size(), ProtoName);
  InsertedPHI->insertBefore(BB->begin());

  // Fill in all the predecessors of the PHI.
  for (const auto &PredValue : PredValues)
    InsertedPHI->addIncoming(PredValue.second, PredValue.first);

  // See if the PHI node can be merged to a single value.  This can happen in
  // loop cases when we get a PHI of itself and one other value.
  if (Value *V =
          simplifyInstruction(InsertedPHI, BB->getDataLayout())) {
    InsertedPHI->eraseFromParent();
    return V;
  }

  // Set the DebugLoc of the inserted PHI, if available.
  DebugLoc DL;
  if (const Instruction *I = BB->getFirstNonPHI())
      DL = I->getDebugLoc();
  InsertedPHI->setDebugLoc(DL);

  // If the client wants to know about all new instructions, tell it.
  if (InsertedPHIs) InsertedPHIs->push_back(InsertedPHI);

  LLVM_DEBUG(dbgs() << "  Inserted PHI: " << *InsertedPHI << "\n");
  return InsertedPHI;
}

void SSAUpdater::RewriteUse(Use &U) {
  Instruction *User = cast<Instruction>(U.getUser());

  Value *V;
  if (PHINode *UserPN = dyn_cast<PHINode>(User))
    V = GetValueAtEndOfBlock(UserPN->getIncomingBlock(U));
  else
    V = GetValueInMiddleOfBlock(User->getParent());

  U.set(V);
}

void SSAUpdater::UpdateDebugValues(Instruction *I) {
  SmallVector<DbgValueInst *, 4> DbgValues;
  SmallVector<DbgVariableRecord *, 4> DbgVariableRecords;
  llvm::findDbgValues(DbgValues, I, &DbgVariableRecords);
  for (auto &DbgValue : DbgValues) {
    if (DbgValue->getParent() == I->getParent())
      continue;
    UpdateDebugValue(I, DbgValue);
  }
  for (auto &DVR : DbgVariableRecords) {
    if (DVR->getParent() == I->getParent())
      continue;
    UpdateDebugValue(I, DVR);
  }
}

void SSAUpdater::UpdateDebugValues(Instruction *I,
                                   SmallVectorImpl<DbgValueInst *> &DbgValues) {
  for (auto &DbgValue : DbgValues) {
    UpdateDebugValue(I, DbgValue);
  }
}

void SSAUpdater::UpdateDebugValues(
    Instruction *I, SmallVectorImpl<DbgVariableRecord *> &DbgVariableRecords) {
  for (auto &DVR : DbgVariableRecords) {
    UpdateDebugValue(I, DVR);
  }
}

void SSAUpdater::UpdateDebugValue(Instruction *I, DbgValueInst *DbgValue) {
  BasicBlock *UserBB = DbgValue->getParent();
  if (HasValueForBlock(UserBB)) {
    Value *NewVal = GetValueAtEndOfBlock(UserBB);
    DbgValue->replaceVariableLocationOp(I, NewVal);
  } else
    DbgValue->setKillLocation();
}

void SSAUpdater::UpdateDebugValue(Instruction *I, DbgVariableRecord *DVR) {
  BasicBlock *UserBB = DVR->getParent();
  if (HasValueForBlock(UserBB)) {
    Value *NewVal = GetValueAtEndOfBlock(UserBB);
    DVR->replaceVariableLocationOp(I, NewVal);
  } else
    DVR->setKillLocation();
}

void SSAUpdater::RewriteUseAfterInsertions(Use &U) {
  Instruction *User = cast<Instruction>(U.getUser());

  Value *V;
  if (PHINode *UserPN = dyn_cast<PHINode>(User))
    V = GetValueAtEndOfBlock(UserPN->getIncomingBlock(U));
  else
    V = GetValueAtEndOfBlock(User->getParent());

  U.set(V);
}

namespace llvm {

template<>
class SSAUpdaterTraits<SSAUpdater> {
public:
  using BlkT = BasicBlock;
  using ValT = Value *;
  using PhiT = PHINode;
  using BlkSucc_iterator = succ_iterator;

  static BlkSucc_iterator BlkSucc_begin(BlkT *BB) { return succ_begin(BB); }
  static BlkSucc_iterator BlkSucc_end(BlkT *BB) { return succ_end(BB); }

  class PHI_iterator {
  private:
    PHINode *PHI;
    unsigned idx;

  public:
    explicit PHI_iterator(PHINode *P) // begin iterator
      : PHI(P), idx(0) {}
    PHI_iterator(PHINode *P, bool) // end iterator
      : PHI(P), idx(PHI->getNumIncomingValues()) {}

    PHI_iterator &operator++() { ++idx; return *this; }
    bool operator==(const PHI_iterator& x) const { return idx == x.idx; }
    bool operator!=(const PHI_iterator& x) const { return !operator==(x); }

    Value *getIncomingValue() { return PHI->getIncomingValue(idx); }
    BasicBlock *getIncomingBlock() { return PHI->getIncomingBlock(idx); }
  };

  static PHI_iterator PHI_begin(PhiT *PHI) { return PHI_iterator(PHI); }
  static PHI_iterator PHI_end(PhiT *PHI) {
    return PHI_iterator(PHI, true);
  }

  /// FindPredecessorBlocks - Put the predecessors of Info->BB into the Preds
  /// vector, set Info->NumPreds, and allocate space in Info->Preds.
  static void FindPredecessorBlocks(BasicBlock *BB,
                                    SmallVectorImpl<BasicBlock *> *Preds) {
    // We can get our predecessor info by walking the pred_iterator list,
    // but it is relatively slow.  If we already have PHI nodes in this
    // block, walk one of them to get the predecessor list instead.
    if (PHINode *SomePhi = dyn_cast<PHINode>(BB->begin()))
      append_range(*Preds, SomePhi->blocks());
    else
      append_range(*Preds, predecessors(BB));
  }

  /// GetPoisonVal - Get a poison value of the same type as the value
  /// being handled.
  static Value *GetPoisonVal(BasicBlock *BB, SSAUpdater *Updater) {
    return PoisonValue::get(Updater->ProtoType);
  }

  /// CreateEmptyPHI - Create a new PHI instruction in the specified block.
  /// Reserve space for the operands but do not fill them in yet.
  static Value *CreateEmptyPHI(BasicBlock *BB, unsigned NumPreds,
                               SSAUpdater *Updater) {
    PHINode *PHI =
        PHINode::Create(Updater->ProtoType, NumPreds, Updater->ProtoName);
    PHI->insertBefore(BB->begin());
    return PHI;
  }

  /// AddPHIOperand - Add the specified value as an operand of the PHI for
  /// the specified predecessor block.
  static void AddPHIOperand(PHINode *PHI, Value *Val, BasicBlock *Pred) {
    PHI->addIncoming(Val, Pred);
  }

  /// ValueIsPHI - Check if a value is a PHI.
  static PHINode *ValueIsPHI(Value *Val, SSAUpdater *Updater) {
    return dyn_cast<PHINode>(Val);
  }

  /// ValueIsNewPHI - Like ValueIsPHI but also check if the PHI has no source
  /// operands, i.e., it was just added.
  static PHINode *ValueIsNewPHI(Value *Val, SSAUpdater *Updater) {
    PHINode *PHI = ValueIsPHI(Val, Updater);
    if (PHI && PHI->getNumIncomingValues() == 0)
      return PHI;
    return nullptr;
  }

  /// GetPHIValue - For the specified PHI instruction, return the value
  /// that it defines.
  static Value *GetPHIValue(PHINode *PHI) {
    return PHI;
  }
};

} // end namespace llvm

/// Check to see if AvailableVals has an entry for the specified BB and if so,
/// return it.  If not, construct SSA form by first calculating the required
/// placement of PHIs and then inserting new PHIs where needed.
Value *SSAUpdater::GetValueAtEndOfBlockInternal(BasicBlock *BB) {
  AvailableValsTy &AvailableVals = getAvailableVals(AV);
  if (Value *V = AvailableVals[BB])
    return V;

  SSAUpdaterImpl<SSAUpdater> Impl(this, &AvailableVals, InsertedPHIs);
  return Impl.GetValue(BB);
}

//===----------------------------------------------------------------------===//
// LoadAndStorePromoter Implementation
//===----------------------------------------------------------------------===//

LoadAndStorePromoter::
LoadAndStorePromoter(ArrayRef<const Instruction *> Insts,
                     SSAUpdater &S, StringRef BaseName) : SSA(S) {
  if (Insts.empty()) return;

  const Value *SomeVal;
  if (const LoadInst *LI = dyn_cast<LoadInst>(Insts[0]))
    SomeVal = LI;
  else
    SomeVal = cast<StoreInst>(Insts[0])->getOperand(0);

  if (BaseName.empty())
    BaseName = SomeVal->getName();
  SSA.Initialize(SomeVal->getType(), BaseName);
}

void LoadAndStorePromoter::run(const SmallVectorImpl<Instruction *> &Insts) {
  // First step: bucket up uses of the alloca by the block they occur in.
  // This is important because we have to handle multiple defs/uses in a block
  // ourselves: SSAUpdater is purely for cross-block references.
  DenseMap<BasicBlock *, TinyPtrVector<Instruction *>> UsesByBlock;

  for (Instruction *User : Insts)
    UsesByBlock[User->getParent()].push_back(User);

  // Okay, now we can iterate over all the blocks in the function with uses,
  // processing them.  Keep track of which loads are loading a live-in value.
  // Walk the uses in the use-list order to be determinstic.
  SmallVector<LoadInst *, 32> LiveInLoads;
  DenseMap<Value *, Value *> ReplacedLoads;

  for (Instruction *User : Insts) {
    BasicBlock *BB = User->getParent();
    TinyPtrVector<Instruction *> &BlockUses = UsesByBlock[BB];

    // If this block has already been processed, ignore this repeat use.
    if (BlockUses.empty()) continue;

    // Okay, this is the first use in the block.  If this block just has a
    // single user in it, we can rewrite it trivially.
    if (BlockUses.size() == 1) {
      // If it is a store, it is a trivial def of the value in the block.
      if (StoreInst *SI = dyn_cast<StoreInst>(User)) {
        updateDebugInfo(SI);
        SSA.AddAvailableValue(BB, SI->getOperand(0));
      } else
        // Otherwise it is a load, queue it to rewrite as a live-in load.
        LiveInLoads.push_back(cast<LoadInst>(User));
      BlockUses.clear();
      continue;
    }

    // Otherwise, check to see if this block is all loads.
    bool HasStore = false;
    for (Instruction *I : BlockUses) {
      if (isa<StoreInst>(I)) {
        HasStore = true;
        break;
      }
    }

    // If so, we can queue them all as live in loads.  We don't have an
    // efficient way to tell which on is first in the block and don't want to
    // scan large blocks, so just add all loads as live ins.
    if (!HasStore) {
      for (Instruction *I : BlockUses)
        LiveInLoads.push_back(cast<LoadInst>(I));
      BlockUses.clear();
      continue;
    }

    // Otherwise, we have mixed loads and stores (or just a bunch of stores).
    // Since SSAUpdater is purely for cross-block values, we need to determine
    // the order of these instructions in the block.  If the first use in the
    // block is a load, then it uses the live in value.  The last store defines
    // the live out value.  We handle this by doing a linear scan of the block.
    Value *StoredValue = nullptr;
    for (Instruction &I : *BB) {
      if (LoadInst *L = dyn_cast<LoadInst>(&I)) {
        // If this is a load from an unrelated pointer, ignore it.
        if (!isInstInList(L, Insts)) continue;

        // If we haven't seen a store yet, this is a live in use, otherwise
        // use the stored value.
        if (StoredValue) {
          replaceLoadWithValue(L, StoredValue);
          L->replaceAllUsesWith(StoredValue);
          ReplacedLoads[L] = StoredValue;
        } else {
          LiveInLoads.push_back(L);
        }
        continue;
      }

      if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
        // If this is a store to an unrelated pointer, ignore it.
        if (!isInstInList(SI, Insts)) continue;
        updateDebugInfo(SI);

        // Remember that this is the active value in the block.
        StoredValue = SI->getOperand(0);
      }
    }

    // The last stored value that happened is the live-out for the block.
    assert(StoredValue && "Already checked that there is a store in block");
    SSA.AddAvailableValue(BB, StoredValue);
    BlockUses.clear();
  }

  // Okay, now we rewrite all loads that use live-in values in the loop,
  // inserting PHI nodes as necessary.
  for (LoadInst *ALoad : LiveInLoads) {
    Value *NewVal = SSA.GetValueInMiddleOfBlock(ALoad->getParent());
    replaceLoadWithValue(ALoad, NewVal);

    // Avoid assertions in unreachable code.
    if (NewVal == ALoad) NewVal = PoisonValue::get(NewVal->getType());
    ALoad->replaceAllUsesWith(NewVal);
    ReplacedLoads[ALoad] = NewVal;
  }

  // Allow the client to do stuff before we start nuking things.
  doExtraRewritesBeforeFinalDeletion();

  // Now that everything is rewritten, delete the old instructions from the
  // function.  They should all be dead now.
  for (Instruction *User : Insts) {
    if (!shouldDelete(User))
      continue;

    // If this is a load that still has uses, then the load must have been added
    // as a live value in the SSAUpdate data structure for a block (e.g. because
    // the loaded value was stored later).  In this case, we need to recursively
    // propagate the updates until we get to the real value.
    if (!User->use_empty()) {
      Value *NewVal = ReplacedLoads[User];
      assert(NewVal && "not a replaced load?");

      // Propagate down to the ultimate replacee.  The intermediately loads
      // could theoretically already have been deleted, so we don't want to
      // dereference the Value*'s.
      DenseMap<Value*, Value*>::iterator RLI = ReplacedLoads.find(NewVal);
      while (RLI != ReplacedLoads.end()) {
        NewVal = RLI->second;
        RLI = ReplacedLoads.find(NewVal);
      }

      replaceLoadWithValue(cast<LoadInst>(User), NewVal);
      User->replaceAllUsesWith(NewVal);
    }

    instructionDeleted(User);
    User->eraseFromParent();
  }
}

bool
LoadAndStorePromoter::isInstInList(Instruction *I,
                                   const SmallVectorImpl<Instruction *> &Insts)
                                   const {
  return is_contained(Insts, I);
}
