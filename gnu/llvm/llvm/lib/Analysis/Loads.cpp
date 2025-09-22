//===- Loads.cpp - Local load analysis ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines simple local analyses for load instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"

using namespace llvm;

static bool isAligned(const Value *Base, const APInt &Offset, Align Alignment,
                      const DataLayout &DL) {
  Align BA = Base->getPointerAlignment(DL);
  return BA >= Alignment && Offset.isAligned(BA);
}

/// Test if V is always a pointer to allocated and suitably aligned memory for
/// a simple load or store.
static bool isDereferenceableAndAlignedPointer(
    const Value *V, Align Alignment, const APInt &Size, const DataLayout &DL,
    const Instruction *CtxI, AssumptionCache *AC, const DominatorTree *DT,
    const TargetLibraryInfo *TLI, SmallPtrSetImpl<const Value *> &Visited,
    unsigned MaxDepth) {
  assert(V->getType()->isPointerTy() && "Base must be pointer");

  // Recursion limit.
  if (MaxDepth-- == 0)
    return false;

  // Already visited?  Bail out, we've likely hit unreachable code.
  if (!Visited.insert(V).second)
    return false;

  // Note that it is not safe to speculate into a malloc'd region because
  // malloc may return null.

  // For GEPs, determine if the indexing lands within the allocated object.
  if (const GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
    const Value *Base = GEP->getPointerOperand();

    APInt Offset(DL.getIndexTypeSizeInBits(GEP->getType()), 0);
    if (!GEP->accumulateConstantOffset(DL, Offset) || Offset.isNegative() ||
        !Offset.urem(APInt(Offset.getBitWidth(), Alignment.value()))
             .isMinValue())
      return false;

    // If the base pointer is dereferenceable for Offset+Size bytes, then the
    // GEP (== Base + Offset) is dereferenceable for Size bytes.  If the base
    // pointer is aligned to Align bytes, and the Offset is divisible by Align
    // then the GEP (== Base + Offset == k_0 * Align + k_1 * Align) is also
    // aligned to Align bytes.

    // Offset and Size may have different bit widths if we have visited an
    // addrspacecast, so we can't do arithmetic directly on the APInt values.
    return isDereferenceableAndAlignedPointer(
        Base, Alignment, Offset + Size.sextOrTrunc(Offset.getBitWidth()), DL,
        CtxI, AC, DT, TLI, Visited, MaxDepth);
  }

  // bitcast instructions are no-ops as far as dereferenceability is concerned.
  if (const BitCastOperator *BC = dyn_cast<BitCastOperator>(V)) {
    if (BC->getSrcTy()->isPointerTy())
      return isDereferenceableAndAlignedPointer(
        BC->getOperand(0), Alignment, Size, DL, CtxI, AC, DT, TLI,
          Visited, MaxDepth);
  }

  // Recurse into both hands of select.
  if (const SelectInst *Sel = dyn_cast<SelectInst>(V)) {
    return isDereferenceableAndAlignedPointer(Sel->getTrueValue(), Alignment,
                                              Size, DL, CtxI, AC, DT, TLI,
                                              Visited, MaxDepth) &&
           isDereferenceableAndAlignedPointer(Sel->getFalseValue(), Alignment,
                                              Size, DL, CtxI, AC, DT, TLI,
                                              Visited, MaxDepth);
  }

  bool CheckForNonNull, CheckForFreed;
  APInt KnownDerefBytes(Size.getBitWidth(),
                        V->getPointerDereferenceableBytes(DL, CheckForNonNull,
                                                          CheckForFreed));
  if (KnownDerefBytes.getBoolValue() && KnownDerefBytes.uge(Size) &&
      !CheckForFreed)
    if (!CheckForNonNull ||
        isKnownNonZero(V, SimplifyQuery(DL, DT, AC, CtxI))) {
      // As we recursed through GEPs to get here, we've incrementally checked
      // that each step advanced by a multiple of the alignment. If our base is
      // properly aligned, then the original offset accessed must also be.
      APInt Offset(DL.getTypeStoreSizeInBits(V->getType()), 0);
      return isAligned(V, Offset, Alignment, DL);
    }

  /// TODO refactor this function to be able to search independently for
  /// Dereferencability and Alignment requirements.


  if (const auto *Call = dyn_cast<CallBase>(V)) {
    if (auto *RP = getArgumentAliasingToReturnedPointer(Call, true))
      return isDereferenceableAndAlignedPointer(RP, Alignment, Size, DL, CtxI,
                                                AC, DT, TLI, Visited, MaxDepth);

    // If we have a call we can't recurse through, check to see if this is an
    // allocation function for which we can establish an minimum object size.
    // Such a minimum object size is analogous to a deref_or_null attribute in
    // that we still need to prove the result non-null at point of use.
    // NOTE: We can only use the object size as a base fact as we a) need to
    // prove alignment too, and b) don't want the compile time impact of a
    // separate recursive walk.
    ObjectSizeOpts Opts;
    // TODO: It may be okay to round to align, but that would imply that
    // accessing slightly out of bounds was legal, and we're currently
    // inconsistent about that.  For the moment, be conservative.
    Opts.RoundToAlign = false;
    Opts.NullIsUnknownSize = true;
    uint64_t ObjSize;
    if (getObjectSize(V, ObjSize, DL, TLI, Opts)) {
      APInt KnownDerefBytes(Size.getBitWidth(), ObjSize);
      if (KnownDerefBytes.getBoolValue() && KnownDerefBytes.uge(Size) &&
          isKnownNonZero(V, SimplifyQuery(DL, DT, AC, CtxI)) &&
          !V->canBeFreed()) {
        // As we recursed through GEPs to get here, we've incrementally
        // checked that each step advanced by a multiple of the alignment. If
        // our base is properly aligned, then the original offset accessed
        // must also be.
        APInt Offset(DL.getTypeStoreSizeInBits(V->getType()), 0);
        return isAligned(V, Offset, Alignment, DL);
      }
    }
  }

  // For gc.relocate, look through relocations
  if (const GCRelocateInst *RelocateInst = dyn_cast<GCRelocateInst>(V))
    return isDereferenceableAndAlignedPointer(RelocateInst->getDerivedPtr(),
                                              Alignment, Size, DL, CtxI, AC, DT,
                                              TLI, Visited, MaxDepth);

  if (const AddrSpaceCastOperator *ASC = dyn_cast<AddrSpaceCastOperator>(V))
    return isDereferenceableAndAlignedPointer(ASC->getOperand(0), Alignment,
                                              Size, DL, CtxI, AC, DT, TLI,
                                              Visited, MaxDepth);

  if (CtxI) {
    /// Look through assumes to see if both dereferencability and alignment can
    /// be provent by an assume
    RetainedKnowledge AlignRK;
    RetainedKnowledge DerefRK;
    if (getKnowledgeForValue(
            V, {Attribute::Dereferenceable, Attribute::Alignment}, AC,
            [&](RetainedKnowledge RK, Instruction *Assume, auto) {
              if (!isValidAssumeForContext(Assume, CtxI, DT))
                return false;
              if (RK.AttrKind == Attribute::Alignment)
                AlignRK = std::max(AlignRK, RK);
              if (RK.AttrKind == Attribute::Dereferenceable)
                DerefRK = std::max(DerefRK, RK);
              if (AlignRK && DerefRK && AlignRK.ArgValue >= Alignment.value() &&
                  DerefRK.ArgValue >= Size.getZExtValue())
                return true; // We have found what we needed so we stop looking
              return false;  // Other assumes may have better information. so
                             // keep looking
            }))
      return true;
  }

  // If we don't know, assume the worst.
  return false;
}

bool llvm::isDereferenceableAndAlignedPointer(
    const Value *V, Align Alignment, const APInt &Size, const DataLayout &DL,
    const Instruction *CtxI, AssumptionCache *AC, const DominatorTree *DT,
    const TargetLibraryInfo *TLI) {
  // Note: At the moment, Size can be zero.  This ends up being interpreted as
  // a query of whether [Base, V] is dereferenceable and V is aligned (since
  // that's what the implementation happened to do).  It's unclear if this is
  // the desired semantic, but at least SelectionDAG does exercise this case.

  SmallPtrSet<const Value *, 32> Visited;
  return ::isDereferenceableAndAlignedPointer(V, Alignment, Size, DL, CtxI, AC,
                                              DT, TLI, Visited, 16);
}

bool llvm::isDereferenceableAndAlignedPointer(
    const Value *V, Type *Ty, Align Alignment, const DataLayout &DL,
    const Instruction *CtxI, AssumptionCache *AC, const DominatorTree *DT,
    const TargetLibraryInfo *TLI) {
  // For unsized types or scalable vectors we don't know exactly how many bytes
  // are dereferenced, so bail out.
  if (!Ty->isSized() || Ty->isScalableTy())
    return false;

  // When dereferenceability information is provided by a dereferenceable
  // attribute, we know exactly how many bytes are dereferenceable. If we can
  // determine the exact offset to the attributed variable, we can use that
  // information here.

  APInt AccessSize(DL.getPointerTypeSizeInBits(V->getType()),
                   DL.getTypeStoreSize(Ty));
  return isDereferenceableAndAlignedPointer(V, Alignment, AccessSize, DL, CtxI,
                                            AC, DT, TLI);
}

bool llvm::isDereferenceablePointer(const Value *V, Type *Ty,
                                    const DataLayout &DL,
                                    const Instruction *CtxI,
                                    AssumptionCache *AC,
                                    const DominatorTree *DT,
                                    const TargetLibraryInfo *TLI) {
  return isDereferenceableAndAlignedPointer(V, Ty, Align(1), DL, CtxI, AC, DT,
                                            TLI);
}

/// Test if A and B will obviously have the same value.
///
/// This includes recognizing that %t0 and %t1 will have the same
/// value in code like this:
/// \code
///   %t0 = getelementptr \@a, 0, 3
///   store i32 0, i32* %t0
///   %t1 = getelementptr \@a, 0, 3
///   %t2 = load i32* %t1
/// \endcode
///
static bool AreEquivalentAddressValues(const Value *A, const Value *B) {
  // Test if the values are trivially equivalent.
  if (A == B)
    return true;

  // Test if the values come from identical arithmetic instructions.
  // Use isIdenticalToWhenDefined instead of isIdenticalTo because
  // this function is only used when one address use dominates the
  // other, which means that they'll always either have the same
  // value or one of them will have an undefined value.
  if (isa<BinaryOperator>(A) || isa<CastInst>(A) || isa<PHINode>(A) ||
      isa<GetElementPtrInst>(A))
    if (const Instruction *BI = dyn_cast<Instruction>(B))
      if (cast<Instruction>(A)->isIdenticalToWhenDefined(BI))
        return true;

  // Otherwise they may not be equivalent.
  return false;
}

bool llvm::isDereferenceableAndAlignedInLoop(LoadInst *LI, Loop *L,
                                             ScalarEvolution &SE,
                                             DominatorTree &DT,
                                             AssumptionCache *AC) {
  auto &DL = LI->getDataLayout();
  Value *Ptr = LI->getPointerOperand();

  APInt EltSize(DL.getIndexTypeSizeInBits(Ptr->getType()),
                DL.getTypeStoreSize(LI->getType()).getFixedValue());
  const Align Alignment = LI->getAlign();

  Instruction *HeaderFirstNonPHI = L->getHeader()->getFirstNonPHI();

  // If given a uniform (i.e. non-varying) address, see if we can prove the
  // access is safe within the loop w/o needing predication.
  if (L->isLoopInvariant(Ptr))
    return isDereferenceableAndAlignedPointer(Ptr, Alignment, EltSize, DL,
                                              HeaderFirstNonPHI, AC, &DT);

  // Otherwise, check to see if we have a repeating access pattern where we can
  // prove that all accesses are well aligned and dereferenceable.
  auto *AddRec = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(Ptr));
  if (!AddRec || AddRec->getLoop() != L || !AddRec->isAffine())
    return false;
  auto* Step = dyn_cast<SCEVConstant>(AddRec->getStepRecurrence(SE));
  if (!Step)
    return false;

  auto TC = SE.getSmallConstantMaxTripCount(L);
  if (!TC)
    return false;

  // TODO: Handle overlapping accesses.
  // We should be computing AccessSize as (TC - 1) * Step + EltSize.
  if (EltSize.sgt(Step->getAPInt()))
    return false;

  // Compute the total access size for access patterns with unit stride and
  // patterns with gaps. For patterns with unit stride, Step and EltSize are the
  // same.
  // For patterns with gaps (i.e. non unit stride), we are
  // accessing EltSize bytes at every Step.
  APInt AccessSize = TC * Step->getAPInt();

  assert(SE.isLoopInvariant(AddRec->getStart(), L) &&
         "implied by addrec definition");
  Value *Base = nullptr;
  if (auto *StartS = dyn_cast<SCEVUnknown>(AddRec->getStart())) {
    Base = StartS->getValue();
  } else if (auto *StartS = dyn_cast<SCEVAddExpr>(AddRec->getStart())) {
    // Handle (NewBase + offset) as start value.
    const auto *Offset = dyn_cast<SCEVConstant>(StartS->getOperand(0));
    const auto *NewBase = dyn_cast<SCEVUnknown>(StartS->getOperand(1));
    if (StartS->getNumOperands() == 2 && Offset && NewBase) {
      // The following code below assumes the offset is unsigned, but GEP
      // offsets are treated as signed so we can end up with a signed value
      // here too. For example, suppose the initial PHI value is (i8 255),
      // the offset will be treated as (i8 -1) and sign-extended to (i64 -1).
      if (Offset->getAPInt().isNegative())
        return false;

      // For the moment, restrict ourselves to the case where the offset is a
      // multiple of the requested alignment and the base is aligned.
      // TODO: generalize if a case found which warrants
      if (Offset->getAPInt().urem(Alignment.value()) != 0)
        return false;
      Base = NewBase->getValue();
      bool Overflow = false;
      AccessSize = AccessSize.uadd_ov(Offset->getAPInt(), Overflow);
      if (Overflow)
        return false;
    }
  }

  if (!Base)
    return false;

  // For the moment, restrict ourselves to the case where the access size is a
  // multiple of the requested alignment and the base is aligned.
  // TODO: generalize if a case found which warrants
  if (EltSize.urem(Alignment.value()) != 0)
    return false;
  return isDereferenceableAndAlignedPointer(Base, Alignment, AccessSize, DL,
                                            HeaderFirstNonPHI, AC, &DT);
}

/// Check if executing a load of this pointer value cannot trap.
///
/// If DT and ScanFrom are specified this method performs context-sensitive
/// analysis and returns true if it is safe to load immediately before ScanFrom.
///
/// If it is not obviously safe to load from the specified pointer, we do
/// a quick local scan of the basic block containing \c ScanFrom, to determine
/// if the address is already accessed.
///
/// This uses the pointee type to determine how many bytes need to be safe to
/// load from the pointer.
bool llvm::isSafeToLoadUnconditionally(Value *V, Align Alignment, const APInt &Size,
                                       const DataLayout &DL,
                                       Instruction *ScanFrom,
                                       AssumptionCache *AC,
                                       const DominatorTree *DT,
                                       const TargetLibraryInfo *TLI) {
  // If DT is not specified we can't make context-sensitive query
  const Instruction* CtxI = DT ? ScanFrom : nullptr;
  if (isDereferenceableAndAlignedPointer(V, Alignment, Size, DL, CtxI, AC, DT,
                                         TLI))
    return true;

  if (!ScanFrom)
    return false;

  if (Size.getBitWidth() > 64)
    return false;
  const TypeSize LoadSize = TypeSize::getFixed(Size.getZExtValue());

  // Otherwise, be a little bit aggressive by scanning the local block where we
  // want to check to see if the pointer is already being loaded or stored
  // from/to.  If so, the previous load or store would have already trapped,
  // so there is no harm doing an extra load (also, CSE will later eliminate
  // the load entirely).
  BasicBlock::iterator BBI = ScanFrom->getIterator(),
                       E = ScanFrom->getParent()->begin();

  // We can at least always strip pointer casts even though we can't use the
  // base here.
  V = V->stripPointerCasts();

  while (BBI != E) {
    --BBI;

    // If we see a free or a call which may write to memory (i.e. which might do
    // a free) the pointer could be marked invalid.
    if (isa<CallInst>(BBI) && BBI->mayWriteToMemory() &&
        !isa<LifetimeIntrinsic>(BBI) && !isa<DbgInfoIntrinsic>(BBI))
      return false;

    Value *AccessedPtr;
    Type *AccessedTy;
    Align AccessedAlign;
    if (LoadInst *LI = dyn_cast<LoadInst>(BBI)) {
      // Ignore volatile loads. The execution of a volatile load cannot
      // be used to prove an address is backed by regular memory; it can,
      // for example, point to an MMIO register.
      if (LI->isVolatile())
        continue;
      AccessedPtr = LI->getPointerOperand();
      AccessedTy = LI->getType();
      AccessedAlign = LI->getAlign();
    } else if (StoreInst *SI = dyn_cast<StoreInst>(BBI)) {
      // Ignore volatile stores (see comment for loads).
      if (SI->isVolatile())
        continue;
      AccessedPtr = SI->getPointerOperand();
      AccessedTy = SI->getValueOperand()->getType();
      AccessedAlign = SI->getAlign();
    } else
      continue;

    if (AccessedAlign < Alignment)
      continue;

    // Handle trivial cases.
    if (AccessedPtr == V &&
        TypeSize::isKnownLE(LoadSize, DL.getTypeStoreSize(AccessedTy)))
      return true;

    if (AreEquivalentAddressValues(AccessedPtr->stripPointerCasts(), V) &&
        TypeSize::isKnownLE(LoadSize, DL.getTypeStoreSize(AccessedTy)))
      return true;
  }
  return false;
}

bool llvm::isSafeToLoadUnconditionally(Value *V, Type *Ty, Align Alignment,
                                       const DataLayout &DL,
                                       Instruction *ScanFrom,
                                       AssumptionCache *AC,
                                       const DominatorTree *DT,
                                       const TargetLibraryInfo *TLI) {
  TypeSize TySize = DL.getTypeStoreSize(Ty);
  if (TySize.isScalable())
    return false;
  APInt Size(DL.getIndexTypeSizeInBits(V->getType()), TySize.getFixedValue());
  return isSafeToLoadUnconditionally(V, Alignment, Size, DL, ScanFrom, AC, DT,
                                     TLI);
}

/// DefMaxInstsToScan - the default number of maximum instructions
/// to scan in the block, used by FindAvailableLoadedValue().
/// FindAvailableLoadedValue() was introduced in r60148, to improve jump
/// threading in part by eliminating partially redundant loads.
/// At that point, the value of MaxInstsToScan was already set to '6'
/// without documented explanation.
cl::opt<unsigned>
llvm::DefMaxInstsToScan("available-load-scan-limit", cl::init(6), cl::Hidden,
  cl::desc("Use this to specify the default maximum number of instructions "
           "to scan backward from a given instruction, when searching for "
           "available loaded value"));

Value *llvm::FindAvailableLoadedValue(LoadInst *Load, BasicBlock *ScanBB,
                                      BasicBlock::iterator &ScanFrom,
                                      unsigned MaxInstsToScan,
                                      BatchAAResults *AA, bool *IsLoad,
                                      unsigned *NumScanedInst) {
  // Don't CSE load that is volatile or anything stronger than unordered.
  if (!Load->isUnordered())
    return nullptr;

  MemoryLocation Loc = MemoryLocation::get(Load);
  return findAvailablePtrLoadStore(Loc, Load->getType(), Load->isAtomic(),
                                   ScanBB, ScanFrom, MaxInstsToScan, AA, IsLoad,
                                   NumScanedInst);
}

// Check if the load and the store have the same base, constant offsets and
// non-overlapping access ranges.
static bool areNonOverlapSameBaseLoadAndStore(const Value *LoadPtr,
                                              Type *LoadTy,
                                              const Value *StorePtr,
                                              Type *StoreTy,
                                              const DataLayout &DL) {
  APInt LoadOffset(DL.getIndexTypeSizeInBits(LoadPtr->getType()), 0);
  APInt StoreOffset(DL.getIndexTypeSizeInBits(StorePtr->getType()), 0);
  const Value *LoadBase = LoadPtr->stripAndAccumulateConstantOffsets(
      DL, LoadOffset, /* AllowNonInbounds */ false);
  const Value *StoreBase = StorePtr->stripAndAccumulateConstantOffsets(
      DL, StoreOffset, /* AllowNonInbounds */ false);
  if (LoadBase != StoreBase)
    return false;
  auto LoadAccessSize = LocationSize::precise(DL.getTypeStoreSize(LoadTy));
  auto StoreAccessSize = LocationSize::precise(DL.getTypeStoreSize(StoreTy));
  ConstantRange LoadRange(LoadOffset,
                          LoadOffset + LoadAccessSize.toRaw());
  ConstantRange StoreRange(StoreOffset,
                           StoreOffset + StoreAccessSize.toRaw());
  return LoadRange.intersectWith(StoreRange).isEmptySet();
}

static Value *getAvailableLoadStore(Instruction *Inst, const Value *Ptr,
                                    Type *AccessTy, bool AtLeastAtomic,
                                    const DataLayout &DL, bool *IsLoadCSE) {
  // If this is a load of Ptr, the loaded value is available.
  // (This is true even if the load is volatile or atomic, although
  // those cases are unlikely.)
  if (LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
    // We can value forward from an atomic to a non-atomic, but not the
    // other way around.
    if (LI->isAtomic() < AtLeastAtomic)
      return nullptr;

    Value *LoadPtr = LI->getPointerOperand()->stripPointerCasts();
    if (!AreEquivalentAddressValues(LoadPtr, Ptr))
      return nullptr;

    if (CastInst::isBitOrNoopPointerCastable(LI->getType(), AccessTy, DL)) {
      if (IsLoadCSE)
        *IsLoadCSE = true;
      return LI;
    }
  }

  // If this is a store through Ptr, the value is available!
  // (This is true even if the store is volatile or atomic, although
  // those cases are unlikely.)
  if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
    // We can value forward from an atomic to a non-atomic, but not the
    // other way around.
    if (SI->isAtomic() < AtLeastAtomic)
      return nullptr;

    Value *StorePtr = SI->getPointerOperand()->stripPointerCasts();
    if (!AreEquivalentAddressValues(StorePtr, Ptr))
      return nullptr;

    if (IsLoadCSE)
      *IsLoadCSE = false;

    Value *Val = SI->getValueOperand();
    if (CastInst::isBitOrNoopPointerCastable(Val->getType(), AccessTy, DL))
      return Val;

    TypeSize StoreSize = DL.getTypeSizeInBits(Val->getType());
    TypeSize LoadSize = DL.getTypeSizeInBits(AccessTy);
    if (TypeSize::isKnownLE(LoadSize, StoreSize))
      if (auto *C = dyn_cast<Constant>(Val))
        return ConstantFoldLoadFromConst(C, AccessTy, DL);
  }

  if (auto *MSI = dyn_cast<MemSetInst>(Inst)) {
    // Don't forward from (non-atomic) memset to atomic load.
    if (AtLeastAtomic)
      return nullptr;

    // Only handle constant memsets.
    auto *Val = dyn_cast<ConstantInt>(MSI->getValue());
    auto *Len = dyn_cast<ConstantInt>(MSI->getLength());
    if (!Val || !Len)
      return nullptr;

    // TODO: Handle offsets.
    Value *Dst = MSI->getDest();
    if (!AreEquivalentAddressValues(Dst, Ptr))
      return nullptr;

    if (IsLoadCSE)
      *IsLoadCSE = false;

    TypeSize LoadTypeSize = DL.getTypeSizeInBits(AccessTy);
    if (LoadTypeSize.isScalable())
      return nullptr;

    // Make sure the read bytes are contained in the memset.
    uint64_t LoadSize = LoadTypeSize.getFixedValue();
    if ((Len->getValue() * 8).ult(LoadSize))
      return nullptr;

    APInt Splat = LoadSize >= 8 ? APInt::getSplat(LoadSize, Val->getValue())
                                : Val->getValue().trunc(LoadSize);
    ConstantInt *SplatC = ConstantInt::get(MSI->getContext(), Splat);
    if (CastInst::isBitOrNoopPointerCastable(SplatC->getType(), AccessTy, DL))
      return SplatC;

    return nullptr;
  }

  return nullptr;
}

Value *llvm::findAvailablePtrLoadStore(
    const MemoryLocation &Loc, Type *AccessTy, bool AtLeastAtomic,
    BasicBlock *ScanBB, BasicBlock::iterator &ScanFrom, unsigned MaxInstsToScan,
    BatchAAResults *AA, bool *IsLoadCSE, unsigned *NumScanedInst) {
  if (MaxInstsToScan == 0)
    MaxInstsToScan = ~0U;

  const DataLayout &DL = ScanBB->getDataLayout();
  const Value *StrippedPtr = Loc.Ptr->stripPointerCasts();

  while (ScanFrom != ScanBB->begin()) {
    // We must ignore debug info directives when counting (otherwise they
    // would affect codegen).
    Instruction *Inst = &*--ScanFrom;
    if (Inst->isDebugOrPseudoInst())
      continue;

    // Restore ScanFrom to expected value in case next test succeeds
    ScanFrom++;

    if (NumScanedInst)
      ++(*NumScanedInst);

    // Don't scan huge blocks.
    if (MaxInstsToScan-- == 0)
      return nullptr;

    --ScanFrom;

    if (Value *Available = getAvailableLoadStore(Inst, StrippedPtr, AccessTy,
                                                 AtLeastAtomic, DL, IsLoadCSE))
      return Available;

    // Try to get the store size for the type.
    if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
      Value *StorePtr = SI->getPointerOperand()->stripPointerCasts();

      // If both StrippedPtr and StorePtr reach all the way to an alloca or
      // global and they are different, ignore the store. This is a trivial form
      // of alias analysis that is important for reg2mem'd code.
      if ((isa<AllocaInst>(StrippedPtr) || isa<GlobalVariable>(StrippedPtr)) &&
          (isa<AllocaInst>(StorePtr) || isa<GlobalVariable>(StorePtr)) &&
          StrippedPtr != StorePtr)
        continue;

      if (!AA) {
        // When AA isn't available, but if the load and the store have the same
        // base, constant offsets and non-overlapping access ranges, ignore the
        // store. This is a simple form of alias analysis that is used by the
        // inliner. FIXME: use BasicAA if possible.
        if (areNonOverlapSameBaseLoadAndStore(
                Loc.Ptr, AccessTy, SI->getPointerOperand(),
                SI->getValueOperand()->getType(), DL))
          continue;
      } else {
        // If we have alias analysis and it says the store won't modify the
        // loaded value, ignore the store.
        if (!isModSet(AA->getModRefInfo(SI, Loc)))
          continue;
      }

      // Otherwise the store that may or may not alias the pointer, bail out.
      ++ScanFrom;
      return nullptr;
    }

    // If this is some other instruction that may clobber Ptr, bail out.
    if (Inst->mayWriteToMemory()) {
      // If alias analysis claims that it really won't modify the load,
      // ignore it.
      if (AA && !isModSet(AA->getModRefInfo(Inst, Loc)))
        continue;

      // May modify the pointer, bail out.
      ++ScanFrom;
      return nullptr;
    }
  }

  // Got to the start of the block, we didn't find it, but are done for this
  // block.
  return nullptr;
}

Value *llvm::FindAvailableLoadedValue(LoadInst *Load, BatchAAResults &AA,
                                      bool *IsLoadCSE,
                                      unsigned MaxInstsToScan) {
  const DataLayout &DL = Load->getDataLayout();
  Value *StrippedPtr = Load->getPointerOperand()->stripPointerCasts();
  BasicBlock *ScanBB = Load->getParent();
  Type *AccessTy = Load->getType();
  bool AtLeastAtomic = Load->isAtomic();

  if (!Load->isUnordered())
    return nullptr;

  // Try to find an available value first, and delay expensive alias analysis
  // queries until later.
  Value *Available = nullptr;
  SmallVector<Instruction *> MustNotAliasInsts;
  for (Instruction &Inst : make_range(++Load->getReverseIterator(),
                                      ScanBB->rend())) {
    if (Inst.isDebugOrPseudoInst())
      continue;

    if (MaxInstsToScan-- == 0)
      return nullptr;

    Available = getAvailableLoadStore(&Inst, StrippedPtr, AccessTy,
                                      AtLeastAtomic, DL, IsLoadCSE);
    if (Available)
      break;

    if (Inst.mayWriteToMemory())
      MustNotAliasInsts.push_back(&Inst);
  }

  // If we found an available value, ensure that the instructions in between
  // did not modify the memory location.
  if (Available) {
    MemoryLocation Loc = MemoryLocation::get(Load);
    for (Instruction *Inst : MustNotAliasInsts)
      if (isModSet(AA.getModRefInfo(Inst, Loc)))
        return nullptr;
  }

  return Available;
}

// Returns true if a use is either in an ICmp/PtrToInt or a Phi/Select that only
// feeds into them.
static bool isPointerUseReplacable(const Use &U) {
  unsigned Limit = 40;
  SmallVector<const User *> Worklist({U.getUser()});
  SmallPtrSet<const User *, 8> Visited;

  while (!Worklist.empty() && --Limit) {
    auto *User = Worklist.pop_back_val();
    if (!Visited.insert(User).second)
      continue;
    if (isa<ICmpInst, PtrToIntInst>(User))
      continue;
    if (isa<PHINode, SelectInst>(User))
      Worklist.append(User->user_begin(), User->user_end());
    else
      return false;
  }

  return Limit != 0;
}

// Returns true if `To` is a null pointer, constant dereferenceable pointer or
// both pointers have the same underlying objects.
static bool isPointerAlwaysReplaceable(const Value *From, const Value *To,
                                       const DataLayout &DL) {
  // This is not strictly correct, but we do it for now to retain important
  // optimizations.
  if (isa<ConstantPointerNull>(To))
    return true;
  if (isa<Constant>(To) &&
      isDereferenceablePointer(To, Type::getInt8Ty(To->getContext()), DL))
    return true;
  return getUnderlyingObjectAggressive(From) ==
         getUnderlyingObjectAggressive(To);
}

bool llvm::canReplacePointersInUseIfEqual(const Use &U, const Value *To,
                                          const DataLayout &DL) {
  assert(U->getType() == To->getType() && "values must have matching types");
  // Not a pointer, just return true.
  if (!To->getType()->isPointerTy())
    return true;

  if (isPointerAlwaysReplaceable(&*U, To, DL))
    return true;
  return isPointerUseReplacable(U);
}

bool llvm::canReplacePointersIfEqual(const Value *From, const Value *To,
                                     const DataLayout &DL) {
  assert(From->getType() == To->getType() && "values must have matching types");
  // Not a pointer, just return true.
  if (!From->getType()->isPointerTy())
    return true;

  return isPointerAlwaysReplaceable(From, To, DL);
}

bool llvm::isDereferenceableReadOnlyLoop(Loop *L, ScalarEvolution *SE,
                                         DominatorTree *DT,
                                         AssumptionCache *AC) {
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        if (!isDereferenceableAndAlignedInLoop(LI, L, *SE, *DT, AC))
          return false;
      } else if (I.mayReadFromMemory() || I.mayWriteToMemory() || I.mayThrow())
        return false;
    }
  }
  return true;
}
