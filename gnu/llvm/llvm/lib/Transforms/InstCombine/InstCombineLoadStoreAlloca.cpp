//===- InstCombineLoadStoreAlloca.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the visit functions for load, store and alloca.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include "llvm/Transforms/Utils/Local.h"
using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "instcombine"

STATISTIC(NumDeadStore, "Number of dead stores eliminated");
STATISTIC(NumGlobalCopies, "Number of allocas copied from constant global");

static cl::opt<unsigned> MaxCopiedFromConstantUsers(
    "instcombine-max-copied-from-constant-users", cl::init(300),
    cl::desc("Maximum users to visit in copy from constant transform"),
    cl::Hidden);

namespace llvm {
cl::opt<bool> EnableInferAlignmentPass(
    "enable-infer-alignment-pass", cl::init(true), cl::Hidden, cl::ZeroOrMore,
    cl::desc("Enable the InferAlignment pass, disabling alignment inference in "
             "InstCombine"));
}

/// isOnlyCopiedFromConstantMemory - Recursively walk the uses of a (derived)
/// pointer to an alloca.  Ignore any reads of the pointer, return false if we
/// see any stores or other unknown uses.  If we see pointer arithmetic, keep
/// track of whether it moves the pointer (with IsOffset) but otherwise traverse
/// the uses.  If we see a memcpy/memmove that targets an unoffseted pointer to
/// the alloca, and if the source pointer is a pointer to a constant memory
/// location, we can optimize this.
static bool
isOnlyCopiedFromConstantMemory(AAResults *AA, AllocaInst *V,
                               MemTransferInst *&TheCopy,
                               SmallVectorImpl<Instruction *> &ToDelete) {
  // We track lifetime intrinsics as we encounter them.  If we decide to go
  // ahead and replace the value with the memory location, this lets the caller
  // quickly eliminate the markers.

  using ValueAndIsOffset = PointerIntPair<Value *, 1, bool>;
  SmallVector<ValueAndIsOffset, 32> Worklist;
  SmallPtrSet<ValueAndIsOffset, 32> Visited;
  Worklist.emplace_back(V, false);
  while (!Worklist.empty()) {
    ValueAndIsOffset Elem = Worklist.pop_back_val();
    if (!Visited.insert(Elem).second)
      continue;
    if (Visited.size() > MaxCopiedFromConstantUsers)
      return false;

    const auto [Value, IsOffset] = Elem;
    for (auto &U : Value->uses()) {
      auto *I = cast<Instruction>(U.getUser());

      if (auto *LI = dyn_cast<LoadInst>(I)) {
        // Ignore non-volatile loads, they are always ok.
        if (!LI->isSimple()) return false;
        continue;
      }

      if (isa<PHINode, SelectInst>(I)) {
        // We set IsOffset=true, to forbid the memcpy from occurring after the
        // phi: If one of the phi operands is not based on the alloca, we
        // would incorrectly omit a write.
        Worklist.emplace_back(I, true);
        continue;
      }
      if (isa<BitCastInst, AddrSpaceCastInst>(I)) {
        // If uses of the bitcast are ok, we are ok.
        Worklist.emplace_back(I, IsOffset);
        continue;
      }
      if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
        // If the GEP has all zero indices, it doesn't offset the pointer. If it
        // doesn't, it does.
        Worklist.emplace_back(I, IsOffset || !GEP->hasAllZeroIndices());
        continue;
      }

      if (auto *Call = dyn_cast<CallBase>(I)) {
        // If this is the function being called then we treat it like a load and
        // ignore it.
        if (Call->isCallee(&U))
          continue;

        unsigned DataOpNo = Call->getDataOperandNo(&U);
        bool IsArgOperand = Call->isArgOperand(&U);

        // Inalloca arguments are clobbered by the call.
        if (IsArgOperand && Call->isInAllocaArgument(DataOpNo))
          return false;

        // If this call site doesn't modify the memory, then we know it is just
        // a load (but one that potentially returns the value itself), so we can
        // ignore it if we know that the value isn't captured.
        bool NoCapture = Call->doesNotCapture(DataOpNo);
        if ((Call->onlyReadsMemory() && (Call->use_empty() || NoCapture)) ||
            (Call->onlyReadsMemory(DataOpNo) && NoCapture))
          continue;

        // If this is being passed as a byval argument, the caller is making a
        // copy, so it is only a read of the alloca.
        if (IsArgOperand && Call->isByValArgument(DataOpNo))
          continue;
      }

      // Lifetime intrinsics can be handled by the caller.
      if (I->isLifetimeStartOrEnd()) {
        assert(I->use_empty() && "Lifetime markers have no result to use!");
        ToDelete.push_back(I);
        continue;
      }

      // If this is isn't our memcpy/memmove, reject it as something we can't
      // handle.
      MemTransferInst *MI = dyn_cast<MemTransferInst>(I);
      if (!MI)
        return false;

      // If the transfer is volatile, reject it.
      if (MI->isVolatile())
        return false;

      // If the transfer is using the alloca as a source of the transfer, then
      // ignore it since it is a load (unless the transfer is volatile).
      if (U.getOperandNo() == 1)
        continue;

      // If we already have seen a copy, reject the second one.
      if (TheCopy) return false;

      // If the pointer has been offset from the start of the alloca, we can't
      // safely handle this.
      if (IsOffset) return false;

      // If the memintrinsic isn't using the alloca as the dest, reject it.
      if (U.getOperandNo() != 0) return false;

      // If the source of the memcpy/move is not constant, reject it.
      if (isModSet(AA->getModRefInfoMask(MI->getSource())))
        return false;

      // Otherwise, the transform is safe.  Remember the copy instruction.
      TheCopy = MI;
    }
  }
  return true;
}

/// isOnlyCopiedFromConstantMemory - Return true if the specified alloca is only
/// modified by a copy from a constant memory location. If we can prove this, we
/// can replace any uses of the alloca with uses of the memory location
/// directly.
static MemTransferInst *
isOnlyCopiedFromConstantMemory(AAResults *AA,
                               AllocaInst *AI,
                               SmallVectorImpl<Instruction *> &ToDelete) {
  MemTransferInst *TheCopy = nullptr;
  if (isOnlyCopiedFromConstantMemory(AA, AI, TheCopy, ToDelete))
    return TheCopy;
  return nullptr;
}

/// Returns true if V is dereferenceable for size of alloca.
static bool isDereferenceableForAllocaSize(const Value *V, const AllocaInst *AI,
                                           const DataLayout &DL) {
  if (AI->isArrayAllocation())
    return false;
  uint64_t AllocaSize = DL.getTypeStoreSize(AI->getAllocatedType());
  if (!AllocaSize)
    return false;
  return isDereferenceableAndAlignedPointer(V, AI->getAlign(),
                                            APInt(64, AllocaSize), DL);
}

static Instruction *simplifyAllocaArraySize(InstCombinerImpl &IC,
                                            AllocaInst &AI, DominatorTree &DT) {
  // Check for array size of 1 (scalar allocation).
  if (!AI.isArrayAllocation()) {
    // i32 1 is the canonical array size for scalar allocations.
    if (AI.getArraySize()->getType()->isIntegerTy(32))
      return nullptr;

    // Canonicalize it.
    return IC.replaceOperand(AI, 0, IC.Builder.getInt32(1));
  }

  // Convert: alloca Ty, C - where C is a constant != 1 into: alloca [C x Ty], 1
  if (const ConstantInt *C = dyn_cast<ConstantInt>(AI.getArraySize())) {
    if (C->getValue().getActiveBits() <= 64) {
      Type *NewTy = ArrayType::get(AI.getAllocatedType(), C->getZExtValue());
      AllocaInst *New = IC.Builder.CreateAlloca(NewTy, AI.getAddressSpace(),
                                                nullptr, AI.getName());
      New->setAlignment(AI.getAlign());
      New->setUsedWithInAlloca(AI.isUsedWithInAlloca());

      replaceAllDbgUsesWith(AI, *New, *New, DT);
      return IC.replaceInstUsesWith(AI, New);
    }
  }

  if (isa<UndefValue>(AI.getArraySize()))
    return IC.replaceInstUsesWith(AI, Constant::getNullValue(AI.getType()));

  // Ensure that the alloca array size argument has type equal to the offset
  // size of the alloca() pointer, which, in the tyical case, is intptr_t,
  // so that any casting is exposed early.
  Type *PtrIdxTy = IC.getDataLayout().getIndexType(AI.getType());
  if (AI.getArraySize()->getType() != PtrIdxTy) {
    Value *V = IC.Builder.CreateIntCast(AI.getArraySize(), PtrIdxTy, false);
    return IC.replaceOperand(AI, 0, V);
  }

  return nullptr;
}

namespace {
// If I and V are pointers in different address space, it is not allowed to
// use replaceAllUsesWith since I and V have different types. A
// non-target-specific transformation should not use addrspacecast on V since
// the two address space may be disjoint depending on target.
//
// This class chases down uses of the old pointer until reaching the load
// instructions, then replaces the old pointer in the load instructions with
// the new pointer. If during the chasing it sees bitcast or GEP, it will
// create new bitcast or GEP with the new pointer and use them in the load
// instruction.
class PointerReplacer {
public:
  PointerReplacer(InstCombinerImpl &IC, Instruction &Root, unsigned SrcAS)
      : IC(IC), Root(Root), FromAS(SrcAS) {}

  bool collectUsers();
  void replacePointer(Value *V);

private:
  bool collectUsersRecursive(Instruction &I);
  void replace(Instruction *I);
  Value *getReplacement(Value *I);
  bool isAvailable(Instruction *I) const {
    return I == &Root || Worklist.contains(I);
  }

  bool isEqualOrValidAddrSpaceCast(const Instruction *I,
                                   unsigned FromAS) const {
    const auto *ASC = dyn_cast<AddrSpaceCastInst>(I);
    if (!ASC)
      return false;
    unsigned ToAS = ASC->getDestAddressSpace();
    return (FromAS == ToAS) || IC.isValidAddrSpaceCast(FromAS, ToAS);
  }

  SmallPtrSet<Instruction *, 32> ValuesToRevisit;
  SmallSetVector<Instruction *, 4> Worklist;
  MapVector<Value *, Value *> WorkMap;
  InstCombinerImpl &IC;
  Instruction &Root;
  unsigned FromAS;
};
} // end anonymous namespace

bool PointerReplacer::collectUsers() {
  if (!collectUsersRecursive(Root))
    return false;

  // Ensure that all outstanding (indirect) users of I
  // are inserted into the Worklist. Return false
  // otherwise.
  for (auto *Inst : ValuesToRevisit)
    if (!Worklist.contains(Inst))
      return false;
  return true;
}

bool PointerReplacer::collectUsersRecursive(Instruction &I) {
  for (auto *U : I.users()) {
    auto *Inst = cast<Instruction>(&*U);
    if (auto *Load = dyn_cast<LoadInst>(Inst)) {
      if (Load->isVolatile())
        return false;
      Worklist.insert(Load);
    } else if (auto *PHI = dyn_cast<PHINode>(Inst)) {
      // All incoming values must be instructions for replacability
      if (any_of(PHI->incoming_values(),
                 [](Value *V) { return !isa<Instruction>(V); }))
        return false;

      // If at least one incoming value of the PHI is not in Worklist,
      // store the PHI for revisiting and skip this iteration of the
      // loop.
      if (any_of(PHI->incoming_values(), [this](Value *V) {
            return !isAvailable(cast<Instruction>(V));
          })) {
        ValuesToRevisit.insert(Inst);
        continue;
      }

      Worklist.insert(PHI);
      if (!collectUsersRecursive(*PHI))
        return false;
    } else if (auto *SI = dyn_cast<SelectInst>(Inst)) {
      if (!isa<Instruction>(SI->getTrueValue()) ||
          !isa<Instruction>(SI->getFalseValue()))
        return false;

      if (!isAvailable(cast<Instruction>(SI->getTrueValue())) ||
          !isAvailable(cast<Instruction>(SI->getFalseValue()))) {
        ValuesToRevisit.insert(Inst);
        continue;
      }
      Worklist.insert(SI);
      if (!collectUsersRecursive(*SI))
        return false;
    } else if (isa<GetElementPtrInst>(Inst)) {
      Worklist.insert(Inst);
      if (!collectUsersRecursive(*Inst))
        return false;
    } else if (auto *MI = dyn_cast<MemTransferInst>(Inst)) {
      if (MI->isVolatile())
        return false;
      Worklist.insert(Inst);
    } else if (isEqualOrValidAddrSpaceCast(Inst, FromAS)) {
      Worklist.insert(Inst);
      if (!collectUsersRecursive(*Inst))
        return false;
    } else if (Inst->isLifetimeStartOrEnd()) {
      continue;
    } else {
      // TODO: For arbitrary uses with address space mismatches, should we check
      // if we can introduce a valid addrspacecast?
      LLVM_DEBUG(dbgs() << "Cannot handle pointer user: " << *U << '\n');
      return false;
    }
  }

  return true;
}

Value *PointerReplacer::getReplacement(Value *V) { return WorkMap.lookup(V); }

void PointerReplacer::replace(Instruction *I) {
  if (getReplacement(I))
    return;

  if (auto *LT = dyn_cast<LoadInst>(I)) {
    auto *V = getReplacement(LT->getPointerOperand());
    assert(V && "Operand not replaced");
    auto *NewI = new LoadInst(LT->getType(), V, "", LT->isVolatile(),
                              LT->getAlign(), LT->getOrdering(),
                              LT->getSyncScopeID());
    NewI->takeName(LT);
    copyMetadataForLoad(*NewI, *LT);

    IC.InsertNewInstWith(NewI, LT->getIterator());
    IC.replaceInstUsesWith(*LT, NewI);
    WorkMap[LT] = NewI;
  } else if (auto *PHI = dyn_cast<PHINode>(I)) {
    Type *NewTy = getReplacement(PHI->getIncomingValue(0))->getType();
    auto *NewPHI = PHINode::Create(NewTy, PHI->getNumIncomingValues(),
                                   PHI->getName(), PHI->getIterator());
    for (unsigned int I = 0; I < PHI->getNumIncomingValues(); ++I)
      NewPHI->addIncoming(getReplacement(PHI->getIncomingValue(I)),
                          PHI->getIncomingBlock(I));
    WorkMap[PHI] = NewPHI;
  } else if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
    auto *V = getReplacement(GEP->getPointerOperand());
    assert(V && "Operand not replaced");
    SmallVector<Value *, 8> Indices(GEP->indices());
    auto *NewI =
        GetElementPtrInst::Create(GEP->getSourceElementType(), V, Indices);
    IC.InsertNewInstWith(NewI, GEP->getIterator());
    NewI->takeName(GEP);
    NewI->setNoWrapFlags(GEP->getNoWrapFlags());
    WorkMap[GEP] = NewI;
  } else if (auto *SI = dyn_cast<SelectInst>(I)) {
    Value *TrueValue = SI->getTrueValue();
    Value *FalseValue = SI->getFalseValue();
    if (Value *Replacement = getReplacement(TrueValue))
      TrueValue = Replacement;
    if (Value *Replacement = getReplacement(FalseValue))
      FalseValue = Replacement;
    auto *NewSI = SelectInst::Create(SI->getCondition(), TrueValue, FalseValue,
                                     SI->getName(), nullptr, SI);
    IC.InsertNewInstWith(NewSI, SI->getIterator());
    NewSI->takeName(SI);
    WorkMap[SI] = NewSI;
  } else if (auto *MemCpy = dyn_cast<MemTransferInst>(I)) {
    auto *DestV = MemCpy->getRawDest();
    auto *SrcV = MemCpy->getRawSource();

    if (auto *DestReplace = getReplacement(DestV))
      DestV = DestReplace;
    if (auto *SrcReplace = getReplacement(SrcV))
      SrcV = SrcReplace;

    IC.Builder.SetInsertPoint(MemCpy);
    auto *NewI = IC.Builder.CreateMemTransferInst(
        MemCpy->getIntrinsicID(), DestV, MemCpy->getDestAlign(), SrcV,
        MemCpy->getSourceAlign(), MemCpy->getLength(), MemCpy->isVolatile());
    AAMDNodes AAMD = MemCpy->getAAMetadata();
    if (AAMD)
      NewI->setAAMetadata(AAMD);

    IC.eraseInstFromFunction(*MemCpy);
    WorkMap[MemCpy] = NewI;
  } else if (auto *ASC = dyn_cast<AddrSpaceCastInst>(I)) {
    auto *V = getReplacement(ASC->getPointerOperand());
    assert(V && "Operand not replaced");
    assert(isEqualOrValidAddrSpaceCast(
               ASC, V->getType()->getPointerAddressSpace()) &&
           "Invalid address space cast!");

    if (V->getType()->getPointerAddressSpace() !=
        ASC->getType()->getPointerAddressSpace()) {
      auto *NewI = new AddrSpaceCastInst(V, ASC->getType(), "");
      NewI->takeName(ASC);
      IC.InsertNewInstWith(NewI, ASC->getIterator());
      WorkMap[ASC] = NewI;
    } else {
      WorkMap[ASC] = V;
    }

  } else {
    llvm_unreachable("should never reach here");
  }
}

void PointerReplacer::replacePointer(Value *V) {
#ifndef NDEBUG
  auto *PT = cast<PointerType>(Root.getType());
  auto *NT = cast<PointerType>(V->getType());
  assert(PT != NT && "Invalid usage");
#endif
  WorkMap[&Root] = V;

  for (Instruction *Workitem : Worklist)
    replace(Workitem);
}

Instruction *InstCombinerImpl::visitAllocaInst(AllocaInst &AI) {
  if (auto *I = simplifyAllocaArraySize(*this, AI, DT))
    return I;

  if (AI.getAllocatedType()->isSized()) {
    // Move all alloca's of zero byte objects to the entry block and merge them
    // together.  Note that we only do this for alloca's, because malloc should
    // allocate and return a unique pointer, even for a zero byte allocation.
    if (DL.getTypeAllocSize(AI.getAllocatedType()).getKnownMinValue() == 0) {
      // For a zero sized alloca there is no point in doing an array allocation.
      // This is helpful if the array size is a complicated expression not used
      // elsewhere.
      if (AI.isArrayAllocation())
        return replaceOperand(AI, 0,
            ConstantInt::get(AI.getArraySize()->getType(), 1));

      // Get the first instruction in the entry block.
      BasicBlock &EntryBlock = AI.getParent()->getParent()->getEntryBlock();
      Instruction *FirstInst = EntryBlock.getFirstNonPHIOrDbg();
      if (FirstInst != &AI) {
        // If the entry block doesn't start with a zero-size alloca then move
        // this one to the start of the entry block.  There is no problem with
        // dominance as the array size was forced to a constant earlier already.
        AllocaInst *EntryAI = dyn_cast<AllocaInst>(FirstInst);
        if (!EntryAI || !EntryAI->getAllocatedType()->isSized() ||
            DL.getTypeAllocSize(EntryAI->getAllocatedType())
                    .getKnownMinValue() != 0) {
          AI.moveBefore(FirstInst);
          return &AI;
        }

        // Replace this zero-sized alloca with the one at the start of the entry
        // block after ensuring that the address will be aligned enough for both
        // types.
        const Align MaxAlign = std::max(EntryAI->getAlign(), AI.getAlign());
        EntryAI->setAlignment(MaxAlign);
        return replaceInstUsesWith(AI, EntryAI);
      }
    }
  }

  // Check to see if this allocation is only modified by a memcpy/memmove from
  // a memory location whose alignment is equal to or exceeds that of the
  // allocation. If this is the case, we can change all users to use the
  // constant memory location instead.  This is commonly produced by the CFE by
  // constructs like "void foo() { int A[] = {1,2,3,4,5,6,7,8,9...}; }" if 'A'
  // is only subsequently read.
  SmallVector<Instruction *, 4> ToDelete;
  if (MemTransferInst *Copy = isOnlyCopiedFromConstantMemory(AA, &AI, ToDelete)) {
    Value *TheSrc = Copy->getSource();
    Align AllocaAlign = AI.getAlign();
    Align SourceAlign = getOrEnforceKnownAlignment(
      TheSrc, AllocaAlign, DL, &AI, &AC, &DT);
    if (AllocaAlign <= SourceAlign &&
        isDereferenceableForAllocaSize(TheSrc, &AI, DL) &&
        !isa<Instruction>(TheSrc)) {
      // FIXME: Can we sink instructions without violating dominance when TheSrc
      // is an instruction instead of a constant or argument?
      LLVM_DEBUG(dbgs() << "Found alloca equal to global: " << AI << '\n');
      LLVM_DEBUG(dbgs() << "  memcpy = " << *Copy << '\n');
      unsigned SrcAddrSpace = TheSrc->getType()->getPointerAddressSpace();
      if (AI.getAddressSpace() == SrcAddrSpace) {
        for (Instruction *Delete : ToDelete)
          eraseInstFromFunction(*Delete);

        Instruction *NewI = replaceInstUsesWith(AI, TheSrc);
        eraseInstFromFunction(*Copy);
        ++NumGlobalCopies;
        return NewI;
      }

      PointerReplacer PtrReplacer(*this, AI, SrcAddrSpace);
      if (PtrReplacer.collectUsers()) {
        for (Instruction *Delete : ToDelete)
          eraseInstFromFunction(*Delete);

        PtrReplacer.replacePointer(TheSrc);
        ++NumGlobalCopies;
      }
    }
  }

  // At last, use the generic allocation site handler to aggressively remove
  // unused allocas.
  return visitAllocSite(AI);
}

// Are we allowed to form a atomic load or store of this type?
static bool isSupportedAtomicType(Type *Ty) {
  return Ty->isIntOrPtrTy() || Ty->isFloatingPointTy();
}

/// Helper to combine a load to a new type.
///
/// This just does the work of combining a load to a new type. It handles
/// metadata, etc., and returns the new instruction. The \c NewTy should be the
/// loaded *value* type. This will convert it to a pointer, cast the operand to
/// that pointer type, load it, etc.
///
/// Note that this will create all of the instructions with whatever insert
/// point the \c InstCombinerImpl currently is using.
LoadInst *InstCombinerImpl::combineLoadToNewType(LoadInst &LI, Type *NewTy,
                                                 const Twine &Suffix) {
  assert((!LI.isAtomic() || isSupportedAtomicType(NewTy)) &&
         "can't fold an atomic load to requested type");

  LoadInst *NewLoad =
      Builder.CreateAlignedLoad(NewTy, LI.getPointerOperand(), LI.getAlign(),
                                LI.isVolatile(), LI.getName() + Suffix);
  NewLoad->setAtomic(LI.getOrdering(), LI.getSyncScopeID());
  copyMetadataForLoad(*NewLoad, LI);
  return NewLoad;
}

/// Combine a store to a new type.
///
/// Returns the newly created store instruction.
static StoreInst *combineStoreToNewValue(InstCombinerImpl &IC, StoreInst &SI,
                                         Value *V) {
  assert((!SI.isAtomic() || isSupportedAtomicType(V->getType())) &&
         "can't fold an atomic store of requested type");

  Value *Ptr = SI.getPointerOperand();
  SmallVector<std::pair<unsigned, MDNode *>, 8> MD;
  SI.getAllMetadata(MD);

  StoreInst *NewStore =
      IC.Builder.CreateAlignedStore(V, Ptr, SI.getAlign(), SI.isVolatile());
  NewStore->setAtomic(SI.getOrdering(), SI.getSyncScopeID());
  for (const auto &MDPair : MD) {
    unsigned ID = MDPair.first;
    MDNode *N = MDPair.second;
    // Note, essentially every kind of metadata should be preserved here! This
    // routine is supposed to clone a store instruction changing *only its
    // type*. The only metadata it makes sense to drop is metadata which is
    // invalidated when the pointer type changes. This should essentially
    // never be the case in LLVM, but we explicitly switch over only known
    // metadata to be conservatively correct. If you are adding metadata to
    // LLVM which pertains to stores, you almost certainly want to add it
    // here.
    switch (ID) {
    case LLVMContext::MD_dbg:
    case LLVMContext::MD_DIAssignID:
    case LLVMContext::MD_tbaa:
    case LLVMContext::MD_prof:
    case LLVMContext::MD_fpmath:
    case LLVMContext::MD_tbaa_struct:
    case LLVMContext::MD_alias_scope:
    case LLVMContext::MD_noalias:
    case LLVMContext::MD_nontemporal:
    case LLVMContext::MD_mem_parallel_loop_access:
    case LLVMContext::MD_access_group:
      // All of these directly apply.
      NewStore->setMetadata(ID, N);
      break;
    case LLVMContext::MD_invariant_load:
    case LLVMContext::MD_nonnull:
    case LLVMContext::MD_noundef:
    case LLVMContext::MD_range:
    case LLVMContext::MD_align:
    case LLVMContext::MD_dereferenceable:
    case LLVMContext::MD_dereferenceable_or_null:
      // These don't apply for stores.
      break;
    }
  }

  return NewStore;
}

/// Combine loads to match the type of their uses' value after looking
/// through intervening bitcasts.
///
/// The core idea here is that if the result of a load is used in an operation,
/// we should load the type most conducive to that operation. For example, when
/// loading an integer and converting that immediately to a pointer, we should
/// instead directly load a pointer.
///
/// However, this routine must never change the width of a load or the number of
/// loads as that would introduce a semantic change. This combine is expected to
/// be a semantic no-op which just allows loads to more closely model the types
/// of their consuming operations.
///
/// Currently, we also refuse to change the precise type used for an atomic load
/// or a volatile load. This is debatable, and might be reasonable to change
/// later. However, it is risky in case some backend or other part of LLVM is
/// relying on the exact type loaded to select appropriate atomic operations.
static Instruction *combineLoadToOperationType(InstCombinerImpl &IC,
                                               LoadInst &Load) {
  // FIXME: We could probably with some care handle both volatile and ordered
  // atomic loads here but it isn't clear that this is important.
  if (!Load.isUnordered())
    return nullptr;

  if (Load.use_empty())
    return nullptr;

  // swifterror values can't be bitcasted.
  if (Load.getPointerOperand()->isSwiftError())
    return nullptr;

  // Fold away bit casts of the loaded value by loading the desired type.
  // Note that we should not do this for pointer<->integer casts,
  // because that would result in type punning.
  if (Load.hasOneUse()) {
    // Don't transform when the type is x86_amx, it makes the pass that lower
    // x86_amx type happy.
    Type *LoadTy = Load.getType();
    if (auto *BC = dyn_cast<BitCastInst>(Load.user_back())) {
      assert(!LoadTy->isX86_AMXTy() && "Load from x86_amx* should not happen!");
      if (BC->getType()->isX86_AMXTy())
        return nullptr;
    }

    if (auto *CastUser = dyn_cast<CastInst>(Load.user_back())) {
      Type *DestTy = CastUser->getDestTy();
      if (CastUser->isNoopCast(IC.getDataLayout()) &&
          LoadTy->isPtrOrPtrVectorTy() == DestTy->isPtrOrPtrVectorTy() &&
          (!Load.isAtomic() || isSupportedAtomicType(DestTy))) {
        LoadInst *NewLoad = IC.combineLoadToNewType(Load, DestTy);
        CastUser->replaceAllUsesWith(NewLoad);
        IC.eraseInstFromFunction(*CastUser);
        return &Load;
      }
    }
  }

  // FIXME: We should also canonicalize loads of vectors when their elements are
  // cast to other types.
  return nullptr;
}

static Instruction *unpackLoadToAggregate(InstCombinerImpl &IC, LoadInst &LI) {
  // FIXME: We could probably with some care handle both volatile and atomic
  // stores here but it isn't clear that this is important.
  if (!LI.isSimple())
    return nullptr;

  Type *T = LI.getType();
  if (!T->isAggregateType())
    return nullptr;

  StringRef Name = LI.getName();

  if (auto *ST = dyn_cast<StructType>(T)) {
    // If the struct only have one element, we unpack.
    auto NumElements = ST->getNumElements();
    if (NumElements == 1) {
      LoadInst *NewLoad = IC.combineLoadToNewType(LI, ST->getTypeAtIndex(0U),
                                                  ".unpack");
      NewLoad->setAAMetadata(LI.getAAMetadata());
      return IC.replaceInstUsesWith(LI, IC.Builder.CreateInsertValue(
        PoisonValue::get(T), NewLoad, 0, Name));
    }

    // We don't want to break loads with padding here as we'd loose
    // the knowledge that padding exists for the rest of the pipeline.
    const DataLayout &DL = IC.getDataLayout();
    auto *SL = DL.getStructLayout(ST);

    // Don't unpack for structure with scalable vector.
    if (SL->getSizeInBits().isScalable())
      return nullptr;

    if (SL->hasPadding())
      return nullptr;

    const auto Align = LI.getAlign();
    auto *Addr = LI.getPointerOperand();
    auto *IdxType = Type::getInt32Ty(T->getContext());
    auto *Zero = ConstantInt::get(IdxType, 0);

    Value *V = PoisonValue::get(T);
    for (unsigned i = 0; i < NumElements; i++) {
      Value *Indices[2] = {
        Zero,
        ConstantInt::get(IdxType, i),
      };
      auto *Ptr = IC.Builder.CreateInBoundsGEP(ST, Addr, ArrayRef(Indices),
                                               Name + ".elt");
      auto *L = IC.Builder.CreateAlignedLoad(
          ST->getElementType(i), Ptr,
          commonAlignment(Align, SL->getElementOffset(i)), Name + ".unpack");
      // Propagate AA metadata. It'll still be valid on the narrowed load.
      L->setAAMetadata(LI.getAAMetadata());
      V = IC.Builder.CreateInsertValue(V, L, i);
    }

    V->setName(Name);
    return IC.replaceInstUsesWith(LI, V);
  }

  if (auto *AT = dyn_cast<ArrayType>(T)) {
    auto *ET = AT->getElementType();
    auto NumElements = AT->getNumElements();
    if (NumElements == 1) {
      LoadInst *NewLoad = IC.combineLoadToNewType(LI, ET, ".unpack");
      NewLoad->setAAMetadata(LI.getAAMetadata());
      return IC.replaceInstUsesWith(LI, IC.Builder.CreateInsertValue(
        PoisonValue::get(T), NewLoad, 0, Name));
    }

    // Bail out if the array is too large. Ideally we would like to optimize
    // arrays of arbitrary size but this has a terrible impact on compile time.
    // The threshold here is chosen arbitrarily, maybe needs a little bit of
    // tuning.
    if (NumElements > IC.MaxArraySizeForCombine)
      return nullptr;

    const DataLayout &DL = IC.getDataLayout();
    TypeSize EltSize = DL.getTypeAllocSize(ET);
    const auto Align = LI.getAlign();

    auto *Addr = LI.getPointerOperand();
    auto *IdxType = Type::getInt64Ty(T->getContext());
    auto *Zero = ConstantInt::get(IdxType, 0);

    Value *V = PoisonValue::get(T);
    TypeSize Offset = TypeSize::getZero();
    for (uint64_t i = 0; i < NumElements; i++) {
      Value *Indices[2] = {
        Zero,
        ConstantInt::get(IdxType, i),
      };
      auto *Ptr = IC.Builder.CreateInBoundsGEP(AT, Addr, ArrayRef(Indices),
                                               Name + ".elt");
      auto EltAlign = commonAlignment(Align, Offset.getKnownMinValue());
      auto *L = IC.Builder.CreateAlignedLoad(AT->getElementType(), Ptr,
                                             EltAlign, Name + ".unpack");
      L->setAAMetadata(LI.getAAMetadata());
      V = IC.Builder.CreateInsertValue(V, L, i);
      Offset += EltSize;
    }

    V->setName(Name);
    return IC.replaceInstUsesWith(LI, V);
  }

  return nullptr;
}

// If we can determine that all possible objects pointed to by the provided
// pointer value are, not only dereferenceable, but also definitively less than
// or equal to the provided maximum size, then return true. Otherwise, return
// false (constant global values and allocas fall into this category).
//
// FIXME: This should probably live in ValueTracking (or similar).
static bool isObjectSizeLessThanOrEq(Value *V, uint64_t MaxSize,
                                     const DataLayout &DL) {
  SmallPtrSet<Value *, 4> Visited;
  SmallVector<Value *, 4> Worklist(1, V);

  do {
    Value *P = Worklist.pop_back_val();
    P = P->stripPointerCasts();

    if (!Visited.insert(P).second)
      continue;

    if (SelectInst *SI = dyn_cast<SelectInst>(P)) {
      Worklist.push_back(SI->getTrueValue());
      Worklist.push_back(SI->getFalseValue());
      continue;
    }

    if (PHINode *PN = dyn_cast<PHINode>(P)) {
      append_range(Worklist, PN->incoming_values());
      continue;
    }

    if (GlobalAlias *GA = dyn_cast<GlobalAlias>(P)) {
      if (GA->isInterposable())
        return false;
      Worklist.push_back(GA->getAliasee());
      continue;
    }

    // If we know how big this object is, and it is less than MaxSize, continue
    // searching. Otherwise, return false.
    if (AllocaInst *AI = dyn_cast<AllocaInst>(P)) {
      if (!AI->getAllocatedType()->isSized())
        return false;

      ConstantInt *CS = dyn_cast<ConstantInt>(AI->getArraySize());
      if (!CS)
        return false;

      TypeSize TS = DL.getTypeAllocSize(AI->getAllocatedType());
      if (TS.isScalable())
        return false;
      // Make sure that, even if the multiplication below would wrap as an
      // uint64_t, we still do the right thing.
      if ((CS->getValue().zext(128) * APInt(128, TS.getFixedValue()))
              .ugt(MaxSize))
        return false;
      continue;
    }

    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(P)) {
      if (!GV->hasDefinitiveInitializer() || !GV->isConstant())
        return false;

      uint64_t InitSize = DL.getTypeAllocSize(GV->getValueType());
      if (InitSize > MaxSize)
        return false;
      continue;
    }

    return false;
  } while (!Worklist.empty());

  return true;
}

// If we're indexing into an object of a known size, and the outer index is
// not a constant, but having any value but zero would lead to undefined
// behavior, replace it with zero.
//
// For example, if we have:
// @f.a = private unnamed_addr constant [1 x i32] [i32 12], align 4
// ...
// %arrayidx = getelementptr inbounds [1 x i32]* @f.a, i64 0, i64 %x
// ... = load i32* %arrayidx, align 4
// Then we know that we can replace %x in the GEP with i64 0.
//
// FIXME: We could fold any GEP index to zero that would cause UB if it were
// not zero. Currently, we only handle the first such index. Also, we could
// also search through non-zero constant indices if we kept track of the
// offsets those indices implied.
static bool canReplaceGEPIdxWithZero(InstCombinerImpl &IC,
                                     GetElementPtrInst *GEPI, Instruction *MemI,
                                     unsigned &Idx) {
  if (GEPI->getNumOperands() < 2)
    return false;

  // Find the first non-zero index of a GEP. If all indices are zero, return
  // one past the last index.
  auto FirstNZIdx = [](const GetElementPtrInst *GEPI) {
    unsigned I = 1;
    for (unsigned IE = GEPI->getNumOperands(); I != IE; ++I) {
      Value *V = GEPI->getOperand(I);
      if (const ConstantInt *CI = dyn_cast<ConstantInt>(V))
        if (CI->isZero())
          continue;

      break;
    }

    return I;
  };

  // Skip through initial 'zero' indices, and find the corresponding pointer
  // type. See if the next index is not a constant.
  Idx = FirstNZIdx(GEPI);
  if (Idx == GEPI->getNumOperands())
    return false;
  if (isa<Constant>(GEPI->getOperand(Idx)))
    return false;

  SmallVector<Value *, 4> Ops(GEPI->idx_begin(), GEPI->idx_begin() + Idx);
  Type *SourceElementType = GEPI->getSourceElementType();
  // Size information about scalable vectors is not available, so we cannot
  // deduce whether indexing at n is undefined behaviour or not. Bail out.
  if (SourceElementType->isScalableTy())
    return false;

  Type *AllocTy = GetElementPtrInst::getIndexedType(SourceElementType, Ops);
  if (!AllocTy || !AllocTy->isSized())
    return false;
  const DataLayout &DL = IC.getDataLayout();
  uint64_t TyAllocSize = DL.getTypeAllocSize(AllocTy).getFixedValue();

  // If there are more indices after the one we might replace with a zero, make
  // sure they're all non-negative. If any of them are negative, the overall
  // address being computed might be before the base address determined by the
  // first non-zero index.
  auto IsAllNonNegative = [&]() {
    for (unsigned i = Idx+1, e = GEPI->getNumOperands(); i != e; ++i) {
      KnownBits Known = IC.computeKnownBits(GEPI->getOperand(i), 0, MemI);
      if (Known.isNonNegative())
        continue;
      return false;
    }

    return true;
  };

  // FIXME: If the GEP is not inbounds, and there are extra indices after the
  // one we'll replace, those could cause the address computation to wrap
  // (rendering the IsAllNonNegative() check below insufficient). We can do
  // better, ignoring zero indices (and other indices we can prove small
  // enough not to wrap).
  if (Idx+1 != GEPI->getNumOperands() && !GEPI->isInBounds())
    return false;

  // Note that isObjectSizeLessThanOrEq will return true only if the pointer is
  // also known to be dereferenceable.
  return isObjectSizeLessThanOrEq(GEPI->getOperand(0), TyAllocSize, DL) &&
         IsAllNonNegative();
}

// If we're indexing into an object with a variable index for the memory
// access, but the object has only one element, we can assume that the index
// will always be zero. If we replace the GEP, return it.
static Instruction *replaceGEPIdxWithZero(InstCombinerImpl &IC, Value *Ptr,
                                          Instruction &MemI) {
  if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(Ptr)) {
    unsigned Idx;
    if (canReplaceGEPIdxWithZero(IC, GEPI, &MemI, Idx)) {
      Instruction *NewGEPI = GEPI->clone();
      NewGEPI->setOperand(Idx,
        ConstantInt::get(GEPI->getOperand(Idx)->getType(), 0));
      IC.InsertNewInstBefore(NewGEPI, GEPI->getIterator());
      return NewGEPI;
    }
  }

  return nullptr;
}

static bool canSimplifyNullStoreOrGEP(StoreInst &SI) {
  if (NullPointerIsDefined(SI.getFunction(), SI.getPointerAddressSpace()))
    return false;

  auto *Ptr = SI.getPointerOperand();
  if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(Ptr))
    Ptr = GEPI->getOperand(0);
  return (isa<ConstantPointerNull>(Ptr) &&
          !NullPointerIsDefined(SI.getFunction(), SI.getPointerAddressSpace()));
}

static bool canSimplifyNullLoadOrGEP(LoadInst &LI, Value *Op) {
  if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(Op)) {
    const Value *GEPI0 = GEPI->getOperand(0);
    if (isa<ConstantPointerNull>(GEPI0) &&
        !NullPointerIsDefined(LI.getFunction(), GEPI->getPointerAddressSpace()))
      return true;
  }
  if (isa<UndefValue>(Op) ||
      (isa<ConstantPointerNull>(Op) &&
       !NullPointerIsDefined(LI.getFunction(), LI.getPointerAddressSpace())))
    return true;
  return false;
}

Instruction *InstCombinerImpl::visitLoadInst(LoadInst &LI) {
  Value *Op = LI.getOperand(0);
  if (Value *Res = simplifyLoadInst(&LI, Op, SQ.getWithInstruction(&LI)))
    return replaceInstUsesWith(LI, Res);

  // Try to canonicalize the loaded type.
  if (Instruction *Res = combineLoadToOperationType(*this, LI))
    return Res;

  if (!EnableInferAlignmentPass) {
    // Attempt to improve the alignment.
    Align KnownAlign = getOrEnforceKnownAlignment(
        Op, DL.getPrefTypeAlign(LI.getType()), DL, &LI, &AC, &DT);
    if (KnownAlign > LI.getAlign())
      LI.setAlignment(KnownAlign);
  }

  // Replace GEP indices if possible.
  if (Instruction *NewGEPI = replaceGEPIdxWithZero(*this, Op, LI))
    return replaceOperand(LI, 0, NewGEPI);

  if (Instruction *Res = unpackLoadToAggregate(*this, LI))
    return Res;

  // Do really simple store-to-load forwarding and load CSE, to catch cases
  // where there are several consecutive memory accesses to the same location,
  // separated by a few arithmetic operations.
  bool IsLoadCSE = false;
  BatchAAResults BatchAA(*AA);
  if (Value *AvailableVal = FindAvailableLoadedValue(&LI, BatchAA, &IsLoadCSE)) {
    if (IsLoadCSE)
      combineMetadataForCSE(cast<LoadInst>(AvailableVal), &LI, false);

    return replaceInstUsesWith(
        LI, Builder.CreateBitOrPointerCast(AvailableVal, LI.getType(),
                                           LI.getName() + ".cast"));
  }

  // None of the following transforms are legal for volatile/ordered atomic
  // loads.  Most of them do apply for unordered atomics.
  if (!LI.isUnordered()) return nullptr;

  // load(gep null, ...) -> unreachable
  // load null/undef -> unreachable
  // TODO: Consider a target hook for valid address spaces for this xforms.
  if (canSimplifyNullLoadOrGEP(LI, Op)) {
    CreateNonTerminatorUnreachable(&LI);
    return replaceInstUsesWith(LI, PoisonValue::get(LI.getType()));
  }

  if (Op->hasOneUse()) {
    // Change select and PHI nodes to select values instead of addresses: this
    // helps alias analysis out a lot, allows many others simplifications, and
    // exposes redundancy in the code.
    //
    // Note that we cannot do the transformation unless we know that the
    // introduced loads cannot trap!  Something like this is valid as long as
    // the condition is always false: load (select bool %C, int* null, int* %G),
    // but it would not be valid if we transformed it to load from null
    // unconditionally.
    //
    if (SelectInst *SI = dyn_cast<SelectInst>(Op)) {
      // load (select (Cond, &V1, &V2))  --> select(Cond, load &V1, load &V2).
      Align Alignment = LI.getAlign();
      if (isSafeToLoadUnconditionally(SI->getOperand(1), LI.getType(),
                                      Alignment, DL, SI) &&
          isSafeToLoadUnconditionally(SI->getOperand(2), LI.getType(),
                                      Alignment, DL, SI)) {
        LoadInst *V1 =
            Builder.CreateLoad(LI.getType(), SI->getOperand(1),
                               SI->getOperand(1)->getName() + ".val");
        LoadInst *V2 =
            Builder.CreateLoad(LI.getType(), SI->getOperand(2),
                               SI->getOperand(2)->getName() + ".val");
        assert(LI.isUnordered() && "implied by above");
        V1->setAlignment(Alignment);
        V1->setAtomic(LI.getOrdering(), LI.getSyncScopeID());
        V2->setAlignment(Alignment);
        V2->setAtomic(LI.getOrdering(), LI.getSyncScopeID());
        return SelectInst::Create(SI->getCondition(), V1, V2);
      }

      // load (select (cond, null, P)) -> load P
      if (isa<ConstantPointerNull>(SI->getOperand(1)) &&
          !NullPointerIsDefined(SI->getFunction(),
                                LI.getPointerAddressSpace()))
        return replaceOperand(LI, 0, SI->getOperand(2));

      // load (select (cond, P, null)) -> load P
      if (isa<ConstantPointerNull>(SI->getOperand(2)) &&
          !NullPointerIsDefined(SI->getFunction(),
                                LI.getPointerAddressSpace()))
        return replaceOperand(LI, 0, SI->getOperand(1));
    }
  }
  return nullptr;
}

/// Look for extractelement/insertvalue sequence that acts like a bitcast.
///
/// \returns underlying value that was "cast", or nullptr otherwise.
///
/// For example, if we have:
///
///     %E0 = extractelement <2 x double> %U, i32 0
///     %V0 = insertvalue [2 x double] undef, double %E0, 0
///     %E1 = extractelement <2 x double> %U, i32 1
///     %V1 = insertvalue [2 x double] %V0, double %E1, 1
///
/// and the layout of a <2 x double> is isomorphic to a [2 x double],
/// then %V1 can be safely approximated by a conceptual "bitcast" of %U.
/// Note that %U may contain non-undef values where %V1 has undef.
static Value *likeBitCastFromVector(InstCombinerImpl &IC, Value *V) {
  Value *U = nullptr;
  while (auto *IV = dyn_cast<InsertValueInst>(V)) {
    auto *E = dyn_cast<ExtractElementInst>(IV->getInsertedValueOperand());
    if (!E)
      return nullptr;
    auto *W = E->getVectorOperand();
    if (!U)
      U = W;
    else if (U != W)
      return nullptr;
    auto *CI = dyn_cast<ConstantInt>(E->getIndexOperand());
    if (!CI || IV->getNumIndices() != 1 || CI->getZExtValue() != *IV->idx_begin())
      return nullptr;
    V = IV->getAggregateOperand();
  }
  if (!match(V, m_Undef()) || !U)
    return nullptr;

  auto *UT = cast<VectorType>(U->getType());
  auto *VT = V->getType();
  // Check that types UT and VT are bitwise isomorphic.
  const auto &DL = IC.getDataLayout();
  if (DL.getTypeStoreSizeInBits(UT) != DL.getTypeStoreSizeInBits(VT)) {
    return nullptr;
  }
  if (auto *AT = dyn_cast<ArrayType>(VT)) {
    if (AT->getNumElements() != cast<FixedVectorType>(UT)->getNumElements())
      return nullptr;
  } else {
    auto *ST = cast<StructType>(VT);
    if (ST->getNumElements() != cast<FixedVectorType>(UT)->getNumElements())
      return nullptr;
    for (const auto *EltT : ST->elements()) {
      if (EltT != UT->getElementType())
        return nullptr;
    }
  }
  return U;
}

/// Combine stores to match the type of value being stored.
///
/// The core idea here is that the memory does not have any intrinsic type and
/// where we can we should match the type of a store to the type of value being
/// stored.
///
/// However, this routine must never change the width of a store or the number of
/// stores as that would introduce a semantic change. This combine is expected to
/// be a semantic no-op which just allows stores to more closely model the types
/// of their incoming values.
///
/// Currently, we also refuse to change the precise type used for an atomic or
/// volatile store. This is debatable, and might be reasonable to change later.
/// However, it is risky in case some backend or other part of LLVM is relying
/// on the exact type stored to select appropriate atomic operations.
///
/// \returns true if the store was successfully combined away. This indicates
/// the caller must erase the store instruction. We have to let the caller erase
/// the store instruction as otherwise there is no way to signal whether it was
/// combined or not: IC.EraseInstFromFunction returns a null pointer.
static bool combineStoreToValueType(InstCombinerImpl &IC, StoreInst &SI) {
  // FIXME: We could probably with some care handle both volatile and ordered
  // atomic stores here but it isn't clear that this is important.
  if (!SI.isUnordered())
    return false;

  // swifterror values can't be bitcasted.
  if (SI.getPointerOperand()->isSwiftError())
    return false;

  Value *V = SI.getValueOperand();

  // Fold away bit casts of the stored value by storing the original type.
  if (auto *BC = dyn_cast<BitCastInst>(V)) {
    assert(!BC->getType()->isX86_AMXTy() &&
           "store to x86_amx* should not happen!");
    V = BC->getOperand(0);
    // Don't transform when the type is x86_amx, it makes the pass that lower
    // x86_amx type happy.
    if (V->getType()->isX86_AMXTy())
      return false;
    if (!SI.isAtomic() || isSupportedAtomicType(V->getType())) {
      combineStoreToNewValue(IC, SI, V);
      return true;
    }
  }

  if (Value *U = likeBitCastFromVector(IC, V))
    if (!SI.isAtomic() || isSupportedAtomicType(U->getType())) {
      combineStoreToNewValue(IC, SI, U);
      return true;
    }

  // FIXME: We should also canonicalize stores of vectors when their elements
  // are cast to other types.
  return false;
}

static bool unpackStoreToAggregate(InstCombinerImpl &IC, StoreInst &SI) {
  // FIXME: We could probably with some care handle both volatile and atomic
  // stores here but it isn't clear that this is important.
  if (!SI.isSimple())
    return false;

  Value *V = SI.getValueOperand();
  Type *T = V->getType();

  if (!T->isAggregateType())
    return false;

  if (auto *ST = dyn_cast<StructType>(T)) {
    // If the struct only have one element, we unpack.
    unsigned Count = ST->getNumElements();
    if (Count == 1) {
      V = IC.Builder.CreateExtractValue(V, 0);
      combineStoreToNewValue(IC, SI, V);
      return true;
    }

    // We don't want to break loads with padding here as we'd loose
    // the knowledge that padding exists for the rest of the pipeline.
    const DataLayout &DL = IC.getDataLayout();
    auto *SL = DL.getStructLayout(ST);

    // Don't unpack for structure with scalable vector.
    if (SL->getSizeInBits().isScalable())
      return false;

    if (SL->hasPadding())
      return false;

    const auto Align = SI.getAlign();

    SmallString<16> EltName = V->getName();
    EltName += ".elt";
    auto *Addr = SI.getPointerOperand();
    SmallString<16> AddrName = Addr->getName();
    AddrName += ".repack";

    auto *IdxType = Type::getInt32Ty(ST->getContext());
    auto *Zero = ConstantInt::get(IdxType, 0);
    for (unsigned i = 0; i < Count; i++) {
      Value *Indices[2] = {
        Zero,
        ConstantInt::get(IdxType, i),
      };
      auto *Ptr =
          IC.Builder.CreateInBoundsGEP(ST, Addr, ArrayRef(Indices), AddrName);
      auto *Val = IC.Builder.CreateExtractValue(V, i, EltName);
      auto EltAlign = commonAlignment(Align, SL->getElementOffset(i));
      llvm::Instruction *NS = IC.Builder.CreateAlignedStore(Val, Ptr, EltAlign);
      NS->setAAMetadata(SI.getAAMetadata());
    }

    return true;
  }

  if (auto *AT = dyn_cast<ArrayType>(T)) {
    // If the array only have one element, we unpack.
    auto NumElements = AT->getNumElements();
    if (NumElements == 1) {
      V = IC.Builder.CreateExtractValue(V, 0);
      combineStoreToNewValue(IC, SI, V);
      return true;
    }

    // Bail out if the array is too large. Ideally we would like to optimize
    // arrays of arbitrary size but this has a terrible impact on compile time.
    // The threshold here is chosen arbitrarily, maybe needs a little bit of
    // tuning.
    if (NumElements > IC.MaxArraySizeForCombine)
      return false;

    const DataLayout &DL = IC.getDataLayout();
    TypeSize EltSize = DL.getTypeAllocSize(AT->getElementType());
    const auto Align = SI.getAlign();

    SmallString<16> EltName = V->getName();
    EltName += ".elt";
    auto *Addr = SI.getPointerOperand();
    SmallString<16> AddrName = Addr->getName();
    AddrName += ".repack";

    auto *IdxType = Type::getInt64Ty(T->getContext());
    auto *Zero = ConstantInt::get(IdxType, 0);

    TypeSize Offset = TypeSize::getZero();
    for (uint64_t i = 0; i < NumElements; i++) {
      Value *Indices[2] = {
        Zero,
        ConstantInt::get(IdxType, i),
      };
      auto *Ptr =
          IC.Builder.CreateInBoundsGEP(AT, Addr, ArrayRef(Indices), AddrName);
      auto *Val = IC.Builder.CreateExtractValue(V, i, EltName);
      auto EltAlign = commonAlignment(Align, Offset.getKnownMinValue());
      Instruction *NS = IC.Builder.CreateAlignedStore(Val, Ptr, EltAlign);
      NS->setAAMetadata(SI.getAAMetadata());
      Offset += EltSize;
    }

    return true;
  }

  return false;
}

/// equivalentAddressValues - Test if A and B will obviously have the same
/// value. This includes recognizing that %t0 and %t1 will have the same
/// value in code like this:
///   %t0 = getelementptr \@a, 0, 3
///   store i32 0, i32* %t0
///   %t1 = getelementptr \@a, 0, 3
///   %t2 = load i32* %t1
///
static bool equivalentAddressValues(Value *A, Value *B) {
  // Test if the values are trivially equivalent.
  if (A == B) return true;

  // Test if the values come form identical arithmetic instructions.
  // This uses isIdenticalToWhenDefined instead of isIdenticalTo because
  // its only used to compare two uses within the same basic block, which
  // means that they'll always either have the same value or one of them
  // will have an undefined value.
  if (isa<BinaryOperator>(A) ||
      isa<CastInst>(A) ||
      isa<PHINode>(A) ||
      isa<GetElementPtrInst>(A))
    if (Instruction *BI = dyn_cast<Instruction>(B))
      if (cast<Instruction>(A)->isIdenticalToWhenDefined(BI))
        return true;

  // Otherwise they may not be equivalent.
  return false;
}

Instruction *InstCombinerImpl::visitStoreInst(StoreInst &SI) {
  Value *Val = SI.getOperand(0);
  Value *Ptr = SI.getOperand(1);

  // Try to canonicalize the stored type.
  if (combineStoreToValueType(*this, SI))
    return eraseInstFromFunction(SI);

  if (!EnableInferAlignmentPass) {
    // Attempt to improve the alignment.
    const Align KnownAlign = getOrEnforceKnownAlignment(
        Ptr, DL.getPrefTypeAlign(Val->getType()), DL, &SI, &AC, &DT);
    if (KnownAlign > SI.getAlign())
      SI.setAlignment(KnownAlign);
  }

  // Try to canonicalize the stored type.
  if (unpackStoreToAggregate(*this, SI))
    return eraseInstFromFunction(SI);

  // Replace GEP indices if possible.
  if (Instruction *NewGEPI = replaceGEPIdxWithZero(*this, Ptr, SI))
    return replaceOperand(SI, 1, NewGEPI);

  // Don't hack volatile/ordered stores.
  // FIXME: Some bits are legal for ordered atomic stores; needs refactoring.
  if (!SI.isUnordered()) return nullptr;

  // If the RHS is an alloca with a single use, zapify the store, making the
  // alloca dead.
  if (Ptr->hasOneUse()) {
    if (isa<AllocaInst>(Ptr))
      return eraseInstFromFunction(SI);
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
      if (isa<AllocaInst>(GEP->getOperand(0))) {
        if (GEP->getOperand(0)->hasOneUse())
          return eraseInstFromFunction(SI);
      }
    }
  }

  // If we have a store to a location which is known constant, we can conclude
  // that the store must be storing the constant value (else the memory
  // wouldn't be constant), and this must be a noop.
  if (!isModSet(AA->getModRefInfoMask(Ptr)))
    return eraseInstFromFunction(SI);

  // Do really simple DSE, to catch cases where there are several consecutive
  // stores to the same location, separated by a few arithmetic operations. This
  // situation often occurs with bitfield accesses.
  BasicBlock::iterator BBI(SI);
  for (unsigned ScanInsts = 6; BBI != SI.getParent()->begin() && ScanInsts;
       --ScanInsts) {
    --BBI;
    // Don't count debug info directives, lest they affect codegen,
    // and we skip pointer-to-pointer bitcasts, which are NOPs.
    if (BBI->isDebugOrPseudoInst()) {
      ScanInsts++;
      continue;
    }

    if (StoreInst *PrevSI = dyn_cast<StoreInst>(BBI)) {
      // Prev store isn't volatile, and stores to the same location?
      if (PrevSI->isUnordered() &&
          equivalentAddressValues(PrevSI->getOperand(1), SI.getOperand(1)) &&
          PrevSI->getValueOperand()->getType() ==
              SI.getValueOperand()->getType()) {
        ++NumDeadStore;
        // Manually add back the original store to the worklist now, so it will
        // be processed after the operands of the removed store, as this may
        // expose additional DSE opportunities.
        Worklist.push(&SI);
        eraseInstFromFunction(*PrevSI);
        return nullptr;
      }
      break;
    }

    // If this is a load, we have to stop.  However, if the loaded value is from
    // the pointer we're loading and is producing the pointer we're storing,
    // then *this* store is dead (X = load P; store X -> P).
    if (LoadInst *LI = dyn_cast<LoadInst>(BBI)) {
      if (LI == Val && equivalentAddressValues(LI->getOperand(0), Ptr)) {
        assert(SI.isUnordered() && "can't eliminate ordering operation");
        return eraseInstFromFunction(SI);
      }

      // Otherwise, this is a load from some other location.  Stores before it
      // may not be dead.
      break;
    }

    // Don't skip over loads, throws or things that can modify memory.
    if (BBI->mayWriteToMemory() || BBI->mayReadFromMemory() || BBI->mayThrow())
      break;
  }

  // store X, null    -> turns into 'unreachable' in SimplifyCFG
  // store X, GEP(null, Y) -> turns into 'unreachable' in SimplifyCFG
  if (canSimplifyNullStoreOrGEP(SI)) {
    if (!isa<PoisonValue>(Val))
      return replaceOperand(SI, 0, PoisonValue::get(Val->getType()));
    return nullptr;  // Do not modify these!
  }

  // This is a non-terminator unreachable marker. Don't remove it.
  if (isa<UndefValue>(Ptr)) {
    // Remove guaranteed-to-transfer instructions before the marker.
    if (removeInstructionsBeforeUnreachable(SI))
      return &SI;

    // Remove all instructions after the marker and handle dead blocks this
    // implies.
    SmallVector<BasicBlock *> Worklist;
    handleUnreachableFrom(SI.getNextNode(), Worklist);
    handlePotentiallyDeadBlocks(Worklist);
    return nullptr;
  }

  // store undef, Ptr -> noop
  // FIXME: This is technically incorrect because it might overwrite a poison
  // value. Change to PoisonValue once #52930 is resolved.
  if (isa<UndefValue>(Val))
    return eraseInstFromFunction(SI);

  return nullptr;
}

/// Try to transform:
///   if () { *P = v1; } else { *P = v2 }
/// or:
///   *P = v1; if () { *P = v2; }
/// into a phi node with a store in the successor.
bool InstCombinerImpl::mergeStoreIntoSuccessor(StoreInst &SI) {
  if (!SI.isUnordered())
    return false; // This code has not been audited for volatile/ordered case.

  // Check if the successor block has exactly 2 incoming edges.
  BasicBlock *StoreBB = SI.getParent();
  BasicBlock *DestBB = StoreBB->getTerminator()->getSuccessor(0);
  if (!DestBB->hasNPredecessors(2))
    return false;

  // Capture the other block (the block that doesn't contain our store).
  pred_iterator PredIter = pred_begin(DestBB);
  if (*PredIter == StoreBB)
    ++PredIter;
  BasicBlock *OtherBB = *PredIter;

  // Bail out if all of the relevant blocks aren't distinct. This can happen,
  // for example, if SI is in an infinite loop.
  if (StoreBB == DestBB || OtherBB == DestBB)
    return false;

  // Verify that the other block ends in a branch and is not otherwise empty.
  BasicBlock::iterator BBI(OtherBB->getTerminator());
  BranchInst *OtherBr = dyn_cast<BranchInst>(BBI);
  if (!OtherBr || BBI == OtherBB->begin())
    return false;

  auto OtherStoreIsMergeable = [&](StoreInst *OtherStore) -> bool {
    if (!OtherStore ||
        OtherStore->getPointerOperand() != SI.getPointerOperand())
      return false;

    auto *SIVTy = SI.getValueOperand()->getType();
    auto *OSVTy = OtherStore->getValueOperand()->getType();
    return CastInst::isBitOrNoopPointerCastable(OSVTy, SIVTy, DL) &&
           SI.hasSameSpecialState(OtherStore);
  };

  // If the other block ends in an unconditional branch, check for the 'if then
  // else' case. There is an instruction before the branch.
  StoreInst *OtherStore = nullptr;
  if (OtherBr->isUnconditional()) {
    --BBI;
    // Skip over debugging info and pseudo probes.
    while (BBI->isDebugOrPseudoInst()) {
      if (BBI==OtherBB->begin())
        return false;
      --BBI;
    }
    // If this isn't a store, isn't a store to the same location, or is not the
    // right kind of store, bail out.
    OtherStore = dyn_cast<StoreInst>(BBI);
    if (!OtherStoreIsMergeable(OtherStore))
      return false;
  } else {
    // Otherwise, the other block ended with a conditional branch. If one of the
    // destinations is StoreBB, then we have the if/then case.
    if (OtherBr->getSuccessor(0) != StoreBB &&
        OtherBr->getSuccessor(1) != StoreBB)
      return false;

    // Okay, we know that OtherBr now goes to Dest and StoreBB, so this is an
    // if/then triangle. See if there is a store to the same ptr as SI that
    // lives in OtherBB.
    for (;; --BBI) {
      // Check to see if we find the matching store.
      OtherStore = dyn_cast<StoreInst>(BBI);
      if (OtherStoreIsMergeable(OtherStore))
        break;

      // If we find something that may be using or overwriting the stored
      // value, or if we run out of instructions, we can't do the transform.
      if (BBI->mayReadFromMemory() || BBI->mayThrow() ||
          BBI->mayWriteToMemory() || BBI == OtherBB->begin())
        return false;
    }

    // In order to eliminate the store in OtherBr, we have to make sure nothing
    // reads or overwrites the stored value in StoreBB.
    for (BasicBlock::iterator I = StoreBB->begin(); &*I != &SI; ++I) {
      // FIXME: This should really be AA driven.
      if (I->mayReadFromMemory() || I->mayThrow() || I->mayWriteToMemory())
        return false;
    }
  }

  // Insert a PHI node now if we need it.
  Value *MergedVal = OtherStore->getValueOperand();
  // The debug locations of the original instructions might differ. Merge them.
  DebugLoc MergedLoc = DILocation::getMergedLocation(SI.getDebugLoc(),
                                                     OtherStore->getDebugLoc());
  if (MergedVal != SI.getValueOperand()) {
    PHINode *PN =
        PHINode::Create(SI.getValueOperand()->getType(), 2, "storemerge");
    PN->addIncoming(SI.getValueOperand(), SI.getParent());
    Builder.SetInsertPoint(OtherStore);
    PN->addIncoming(Builder.CreateBitOrPointerCast(MergedVal, PN->getType()),
                    OtherBB);
    MergedVal = InsertNewInstBefore(PN, DestBB->begin());
    PN->setDebugLoc(MergedLoc);
  }

  // Advance to a place where it is safe to insert the new store and insert it.
  BBI = DestBB->getFirstInsertionPt();
  StoreInst *NewSI =
      new StoreInst(MergedVal, SI.getOperand(1), SI.isVolatile(), SI.getAlign(),
                    SI.getOrdering(), SI.getSyncScopeID());
  InsertNewInstBefore(NewSI, BBI);
  NewSI->setDebugLoc(MergedLoc);
  NewSI->mergeDIAssignID({&SI, OtherStore});

  // If the two stores had AA tags, merge them.
  AAMDNodes AATags = SI.getAAMetadata();
  if (AATags)
    NewSI->setAAMetadata(AATags.merge(OtherStore->getAAMetadata()));

  // Nuke the old stores.
  eraseInstFromFunction(SI);
  eraseInstFromFunction(*OtherStore);
  return true;
}
