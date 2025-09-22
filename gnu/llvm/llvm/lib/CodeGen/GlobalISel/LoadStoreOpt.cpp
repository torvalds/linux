//===- LoadStoreOpt.cpp ----------- Generic memory optimizations -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the LoadStoreOpt optimization pass.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/LoadStoreOpt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/LowLevelTypeUtils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>

#define DEBUG_TYPE "loadstore-opt"

using namespace llvm;
using namespace ore;
using namespace MIPatternMatch;

STATISTIC(NumStoresMerged, "Number of stores merged");

const unsigned MaxStoreSizeToForm = 128;

char LoadStoreOpt::ID = 0;
INITIALIZE_PASS_BEGIN(LoadStoreOpt, DEBUG_TYPE, "Generic memory optimizations",
                      false, false)
INITIALIZE_PASS_END(LoadStoreOpt, DEBUG_TYPE, "Generic memory optimizations",
                    false, false)

LoadStoreOpt::LoadStoreOpt(std::function<bool(const MachineFunction &)> F)
    : MachineFunctionPass(ID), DoNotRunPass(F) {}

LoadStoreOpt::LoadStoreOpt()
    : LoadStoreOpt([](const MachineFunction &) { return false; }) {}

void LoadStoreOpt::init(MachineFunction &MF) {
  this->MF = &MF;
  MRI = &MF.getRegInfo();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  TLI = MF.getSubtarget().getTargetLowering();
  LI = MF.getSubtarget().getLegalizerInfo();
  Builder.setMF(MF);
  IsPreLegalizer = !MF.getProperties().hasProperty(
      MachineFunctionProperties::Property::Legalized);
  InstsToErase.clear();
}

void LoadStoreOpt::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AAResultsWrapperPass>();
  AU.setPreservesAll();
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

BaseIndexOffset GISelAddressing::getPointerInfo(Register Ptr,
                                                MachineRegisterInfo &MRI) {
  BaseIndexOffset Info;
  Register PtrAddRHS;
  Register BaseReg;
  if (!mi_match(Ptr, MRI, m_GPtrAdd(m_Reg(BaseReg), m_Reg(PtrAddRHS)))) {
    Info.setBase(Ptr);
    Info.setOffset(0);
    return Info;
  }
  Info.setBase(BaseReg);
  auto RHSCst = getIConstantVRegValWithLookThrough(PtrAddRHS, MRI);
  if (RHSCst)
    Info.setOffset(RHSCst->Value.getSExtValue());

  // Just recognize a simple case for now. In future we'll need to match
  // indexing patterns for base + index + constant.
  Info.setIndex(PtrAddRHS);
  return Info;
}

bool GISelAddressing::aliasIsKnownForLoadStore(const MachineInstr &MI1,
                                               const MachineInstr &MI2,
                                               bool &IsAlias,
                                               MachineRegisterInfo &MRI) {
  auto *LdSt1 = dyn_cast<GLoadStore>(&MI1);
  auto *LdSt2 = dyn_cast<GLoadStore>(&MI2);
  if (!LdSt1 || !LdSt2)
    return false;

  BaseIndexOffset BasePtr0 = getPointerInfo(LdSt1->getPointerReg(), MRI);
  BaseIndexOffset BasePtr1 = getPointerInfo(LdSt2->getPointerReg(), MRI);

  if (!BasePtr0.getBase().isValid() || !BasePtr1.getBase().isValid())
    return false;

  LocationSize Size1 = LdSt1->getMemSize();
  LocationSize Size2 = LdSt2->getMemSize();

  int64_t PtrDiff;
  if (BasePtr0.getBase() == BasePtr1.getBase() && BasePtr0.hasValidOffset() &&
      BasePtr1.hasValidOffset()) {
    PtrDiff = BasePtr1.getOffset() - BasePtr0.getOffset();
    // If the size of memory access is unknown, do not use it to do analysis.
    // One example of unknown size memory access is to load/store scalable
    // vector objects on the stack.
    // BasePtr1 is PtrDiff away from BasePtr0. They alias if none of the
    // following situations arise:
    if (PtrDiff >= 0 && Size1.hasValue() && !Size1.isScalable()) {
      // [----BasePtr0----]
      //                         [---BasePtr1--]
      // ========PtrDiff========>
      IsAlias = !((int64_t)Size1.getValue() <= PtrDiff);
      return true;
    }
    if (PtrDiff < 0 && Size2.hasValue() && !Size2.isScalable()) {
      //                     [----BasePtr0----]
      // [---BasePtr1--]
      // =====(-PtrDiff)====>
      IsAlias = !((PtrDiff + (int64_t)Size2.getValue()) <= 0);
      return true;
    }
    return false;
  }

  // If both BasePtr0 and BasePtr1 are FrameIndexes, we will not be
  // able to calculate their relative offset if at least one arises
  // from an alloca. However, these allocas cannot overlap and we
  // can infer there is no alias.
  auto *Base0Def = getDefIgnoringCopies(BasePtr0.getBase(), MRI);
  auto *Base1Def = getDefIgnoringCopies(BasePtr1.getBase(), MRI);
  if (!Base0Def || !Base1Def)
    return false; // Couldn't tell anything.


  if (Base0Def->getOpcode() != Base1Def->getOpcode())
    return false;

  if (Base0Def->getOpcode() == TargetOpcode::G_FRAME_INDEX) {
    MachineFrameInfo &MFI = Base0Def->getMF()->getFrameInfo();
    // If the bases have the same frame index but we couldn't find a
    // constant offset, (indices are different) be conservative.
    if (Base0Def != Base1Def &&
        (!MFI.isFixedObjectIndex(Base0Def->getOperand(1).getIndex()) ||
         !MFI.isFixedObjectIndex(Base1Def->getOperand(1).getIndex()))) {
      IsAlias = false;
      return true;
    }
  }

  // This implementation is a lot more primitive than the SDAG one for now.
  // FIXME: what about constant pools?
  if (Base0Def->getOpcode() == TargetOpcode::G_GLOBAL_VALUE) {
    auto GV0 = Base0Def->getOperand(1).getGlobal();
    auto GV1 = Base1Def->getOperand(1).getGlobal();
    if (GV0 != GV1) {
      IsAlias = false;
      return true;
    }
  }

  // Can't tell anything about aliasing.
  return false;
}

bool GISelAddressing::instMayAlias(const MachineInstr &MI,
                                   const MachineInstr &Other,
                                   MachineRegisterInfo &MRI,
                                   AliasAnalysis *AA) {
  struct MemUseCharacteristics {
    bool IsVolatile;
    bool IsAtomic;
    Register BasePtr;
    int64_t Offset;
    LocationSize NumBytes;
    MachineMemOperand *MMO;
  };

  auto getCharacteristics =
      [&](const MachineInstr *MI) -> MemUseCharacteristics {
    if (const auto *LS = dyn_cast<GLoadStore>(MI)) {
      Register BaseReg;
      int64_t Offset = 0;
      // No pre/post-inc addressing modes are considered here, unlike in SDAG.
      if (!mi_match(LS->getPointerReg(), MRI,
                    m_GPtrAdd(m_Reg(BaseReg), m_ICst(Offset)))) {
        BaseReg = LS->getPointerReg();
        Offset = 0;
      }

      LocationSize Size = LS->getMMO().getSize();
      return {LS->isVolatile(),       LS->isAtomic(), BaseReg,
              Offset /*base offset*/, Size,           &LS->getMMO()};
    }
    // FIXME: support recognizing lifetime instructions.
    // Default.
    return {false /*isvolatile*/,
            /*isAtomic*/ false,
            Register(),
            (int64_t)0 /*offset*/,
            LocationSize::beforeOrAfterPointer() /*size*/,
            (MachineMemOperand *)nullptr};
  };
  MemUseCharacteristics MUC0 = getCharacteristics(&MI),
                        MUC1 = getCharacteristics(&Other);

  // If they are to the same address, then they must be aliases.
  if (MUC0.BasePtr.isValid() && MUC0.BasePtr == MUC1.BasePtr &&
      MUC0.Offset == MUC1.Offset)
    return true;

  // If they are both volatile then they cannot be reordered.
  if (MUC0.IsVolatile && MUC1.IsVolatile)
    return true;

  // Be conservative about atomics for the moment
  // TODO: This is way overconservative for unordered atomics (see D66309)
  if (MUC0.IsAtomic && MUC1.IsAtomic)
    return true;

  // If one operation reads from invariant memory, and the other may store, they
  // cannot alias.
  if (MUC0.MMO && MUC1.MMO) {
    if ((MUC0.MMO->isInvariant() && MUC1.MMO->isStore()) ||
        (MUC1.MMO->isInvariant() && MUC0.MMO->isStore()))
      return false;
  }

  // If NumBytes is scalable and offset is not 0, conservatively return may
  // alias
  if ((MUC0.NumBytes.isScalable() && MUC0.Offset != 0) ||
      (MUC1.NumBytes.isScalable() && MUC1.Offset != 0))
    return true;

  const bool BothNotScalable =
      !MUC0.NumBytes.isScalable() && !MUC1.NumBytes.isScalable();

  // Try to prove that there is aliasing, or that there is no aliasing. Either
  // way, we can return now. If nothing can be proved, proceed with more tests.
  bool IsAlias;
  if (BothNotScalable &&
      GISelAddressing::aliasIsKnownForLoadStore(MI, Other, IsAlias, MRI))
    return IsAlias;

  // The following all rely on MMO0 and MMO1 being valid.
  if (!MUC0.MMO || !MUC1.MMO)
    return true;

  // FIXME: port the alignment based alias analysis from SDAG's isAlias().
  int64_t SrcValOffset0 = MUC0.MMO->getOffset();
  int64_t SrcValOffset1 = MUC1.MMO->getOffset();
  LocationSize Size0 = MUC0.NumBytes;
  LocationSize Size1 = MUC1.NumBytes;
  if (AA && MUC0.MMO->getValue() && MUC1.MMO->getValue() && Size0.hasValue() &&
      Size1.hasValue()) {
    // Use alias analysis information.
    int64_t MinOffset = std::min(SrcValOffset0, SrcValOffset1);
    int64_t Overlap0 =
        Size0.getValue().getKnownMinValue() + SrcValOffset0 - MinOffset;
    int64_t Overlap1 =
        Size1.getValue().getKnownMinValue() + SrcValOffset1 - MinOffset;
    LocationSize Loc0 =
        Size0.isScalable() ? Size0 : LocationSize::precise(Overlap0);
    LocationSize Loc1 =
        Size1.isScalable() ? Size1 : LocationSize::precise(Overlap1);

    if (AA->isNoAlias(
            MemoryLocation(MUC0.MMO->getValue(), Loc0, MUC0.MMO->getAAInfo()),
            MemoryLocation(MUC1.MMO->getValue(), Loc1, MUC1.MMO->getAAInfo())))
      return false;
  }

  // Otherwise we have to assume they alias.
  return true;
}

/// Returns true if the instruction creates an unavoidable hazard that
/// forces a boundary between store merge candidates.
static bool isInstHardMergeHazard(MachineInstr &MI) {
  return MI.hasUnmodeledSideEffects() || MI.hasOrderedMemoryRef();
}

bool LoadStoreOpt::mergeStores(SmallVectorImpl<GStore *> &StoresToMerge) {
  // Try to merge all the stores in the vector, splitting into separate segments
  // as necessary.
  assert(StoresToMerge.size() > 1 && "Expected multiple stores to merge");
  LLT OrigTy = MRI->getType(StoresToMerge[0]->getValueReg());
  LLT PtrTy = MRI->getType(StoresToMerge[0]->getPointerReg());
  unsigned AS = PtrTy.getAddressSpace();
  // Ensure the legal store info is computed for this address space.
  initializeStoreMergeTargetInfo(AS);
  const auto &LegalSizes = LegalStoreSizes[AS];

#ifndef NDEBUG
  for (auto *StoreMI : StoresToMerge)
    assert(MRI->getType(StoreMI->getValueReg()) == OrigTy);
#endif

  const auto &DL = MF->getFunction().getDataLayout();
  bool AnyMerged = false;
  do {
    unsigned NumPow2 = llvm::bit_floor(StoresToMerge.size());
    unsigned MaxSizeBits = NumPow2 * OrigTy.getSizeInBits().getFixedValue();
    // Compute the biggest store we can generate to handle the number of stores.
    unsigned MergeSizeBits;
    for (MergeSizeBits = MaxSizeBits; MergeSizeBits > 1; MergeSizeBits /= 2) {
      LLT StoreTy = LLT::scalar(MergeSizeBits);
      EVT StoreEVT =
          getApproximateEVTForLLT(StoreTy, DL, MF->getFunction().getContext());
      if (LegalSizes.size() > MergeSizeBits && LegalSizes[MergeSizeBits] &&
          TLI->canMergeStoresTo(AS, StoreEVT, *MF) &&
          (TLI->isTypeLegal(StoreEVT)))
        break; // We can generate a MergeSize bits store.
    }
    if (MergeSizeBits <= OrigTy.getSizeInBits())
      return AnyMerged; // No greater merge.

    unsigned NumStoresToMerge = MergeSizeBits / OrigTy.getSizeInBits();
    // Perform the actual merging.
    SmallVector<GStore *, 8> SingleMergeStores(
        StoresToMerge.begin(), StoresToMerge.begin() + NumStoresToMerge);
    AnyMerged |= doSingleStoreMerge(SingleMergeStores);
    StoresToMerge.erase(StoresToMerge.begin(),
                        StoresToMerge.begin() + NumStoresToMerge);
  } while (StoresToMerge.size() > 1);
  return AnyMerged;
}

bool LoadStoreOpt::isLegalOrBeforeLegalizer(const LegalityQuery &Query,
                                            MachineFunction &MF) const {
  auto Action = LI->getAction(Query).Action;
  // If the instruction is unsupported, it can't be legalized at all.
  if (Action == LegalizeActions::Unsupported)
    return false;
  return IsPreLegalizer || Action == LegalizeAction::Legal;
}

bool LoadStoreOpt::doSingleStoreMerge(SmallVectorImpl<GStore *> &Stores) {
  assert(Stores.size() > 1);
  // We know that all the stores are consecutive and there are no aliasing
  // operations in the range. However, the values that are being stored may be
  // generated anywhere before each store. To ensure we have the values
  // available, we materialize the wide value and new store at the place of the
  // final store in the merge sequence.
  GStore *FirstStore = Stores[0];
  const unsigned NumStores = Stores.size();
  LLT SmallTy = MRI->getType(FirstStore->getValueReg());
  LLT WideValueTy =
      LLT::scalar(NumStores * SmallTy.getSizeInBits().getFixedValue());

  // For each store, compute pairwise merged debug locs.
  DebugLoc MergedLoc = Stores.front()->getDebugLoc();
  for (auto *Store : drop_begin(Stores))
    MergedLoc = DILocation::getMergedLocation(MergedLoc, Store->getDebugLoc());

  Builder.setInstr(*Stores.back());
  Builder.setDebugLoc(MergedLoc);

  // If all of the store values are constants, then create a wide constant
  // directly. Otherwise, we need to generate some instructions to merge the
  // existing values together into a wider type.
  SmallVector<APInt, 8> ConstantVals;
  for (auto *Store : Stores) {
    auto MaybeCst =
        getIConstantVRegValWithLookThrough(Store->getValueReg(), *MRI);
    if (!MaybeCst) {
      ConstantVals.clear();
      break;
    }
    ConstantVals.emplace_back(MaybeCst->Value);
  }

  Register WideReg;
  auto *WideMMO =
      MF->getMachineMemOperand(&FirstStore->getMMO(), 0, WideValueTy);
  if (ConstantVals.empty()) {
    // Mimic the SDAG behaviour here and don't try to do anything for unknown
    // values. In future, we should also support the cases of loads and
    // extracted vector elements.
    return false;
  }

  assert(ConstantVals.size() == NumStores);
  // Check if our wide constant is legal.
  if (!isLegalOrBeforeLegalizer({TargetOpcode::G_CONSTANT, {WideValueTy}}, *MF))
    return false;
  APInt WideConst(WideValueTy.getSizeInBits(), 0);
  for (unsigned Idx = 0; Idx < ConstantVals.size(); ++Idx) {
    // Insert the smaller constant into the corresponding position in the
    // wider one.
    WideConst.insertBits(ConstantVals[Idx], Idx * SmallTy.getSizeInBits());
  }
  WideReg = Builder.buildConstant(WideValueTy, WideConst).getReg(0);
  auto NewStore =
      Builder.buildStore(WideReg, FirstStore->getPointerReg(), *WideMMO);
  (void) NewStore;
  LLVM_DEBUG(dbgs() << "Merged " << Stores.size()
                    << " stores into merged store: " << *NewStore);
  LLVM_DEBUG(for (auto *MI : Stores) dbgs() << "  " << *MI;);
  NumStoresMerged += Stores.size();

  MachineOptimizationRemarkEmitter MORE(*MF, nullptr);
  MORE.emit([&]() {
    MachineOptimizationRemark R(DEBUG_TYPE, "MergedStore",
                                FirstStore->getDebugLoc(),
                                FirstStore->getParent());
    R << "Merged " << NV("NumMerged", Stores.size()) << " stores of "
      << NV("OrigWidth", SmallTy.getSizeInBytes())
      << " bytes into a single store of "
      << NV("NewWidth", WideValueTy.getSizeInBytes()) << " bytes";
    return R;
  });

  for (auto *MI : Stores)
    InstsToErase.insert(MI);
  return true;
}

bool LoadStoreOpt::processMergeCandidate(StoreMergeCandidate &C) {
  if (C.Stores.size() < 2) {
    C.reset();
    return false;
  }

  LLVM_DEBUG(dbgs() << "Checking store merge candidate with " << C.Stores.size()
                    << " stores, starting with " << *C.Stores[0]);
  // We know that the stores in the candidate are adjacent.
  // Now we need to check if any potential aliasing instructions recorded
  // during the search alias with load/stores added to the candidate after.
  // For example, if we have the candidate:
  //   C.Stores = [ST1, ST2, ST3, ST4]
  // and after seeing ST2 we saw a load LD1, which did not alias with ST1 or
  // ST2, then we would have recorded it into the PotentialAliases structure
  // with the associated index value of "1". Then we see ST3 and ST4 and add
  // them to the candidate group. We know that LD1 does not alias with ST1 or
  // ST2, since we already did that check. However we don't yet know if it
  // may alias ST3 and ST4, so we perform those checks now.
  SmallVector<GStore *> StoresToMerge;

  auto DoesStoreAliasWithPotential = [&](unsigned Idx, GStore &CheckStore) {
    for (auto AliasInfo : reverse(C.PotentialAliases)) {
      MachineInstr *PotentialAliasOp = AliasInfo.first;
      unsigned PreCheckedIdx = AliasInfo.second;
      if (static_cast<unsigned>(Idx) < PreCheckedIdx) {
        // Once our store index is lower than the index associated with the
        // potential alias, we know that we've already checked for this alias
        // and all of the earlier potential aliases too.
        return false;
      }
      // Need to check this alias.
      if (GISelAddressing::instMayAlias(CheckStore, *PotentialAliasOp, *MRI,
                                        AA)) {
        LLVM_DEBUG(dbgs() << "Potential alias " << *PotentialAliasOp
                          << " detected\n");
        return true;
      }
    }
    return false;
  };
  // Start from the last store in the group, and check if it aliases with any
  // of the potential aliasing operations in the list.
  for (int StoreIdx = C.Stores.size() - 1; StoreIdx >= 0; --StoreIdx) {
    auto *CheckStore = C.Stores[StoreIdx];
    if (DoesStoreAliasWithPotential(StoreIdx, *CheckStore))
      continue;
    StoresToMerge.emplace_back(CheckStore);
  }

  LLVM_DEBUG(dbgs() << StoresToMerge.size()
                    << " stores remaining after alias checks. Merging...\n");

  // Now we've checked for aliasing hazards, merge any stores left.
  C.reset();
  if (StoresToMerge.size() < 2)
    return false;
  return mergeStores(StoresToMerge);
}

bool LoadStoreOpt::operationAliasesWithCandidate(MachineInstr &MI,
                                                 StoreMergeCandidate &C) {
  if (C.Stores.empty())
    return false;
  return llvm::any_of(C.Stores, [&](MachineInstr *OtherMI) {
    return instMayAlias(MI, *OtherMI, *MRI, AA);
  });
}

void LoadStoreOpt::StoreMergeCandidate::addPotentialAlias(MachineInstr &MI) {
  PotentialAliases.emplace_back(std::make_pair(&MI, Stores.size() - 1));
}

bool LoadStoreOpt::addStoreToCandidate(GStore &StoreMI,
                                       StoreMergeCandidate &C) {
  // Check if the given store writes to an adjacent address, and other
  // requirements.
  LLT ValueTy = MRI->getType(StoreMI.getValueReg());
  LLT PtrTy = MRI->getType(StoreMI.getPointerReg());

  // Only handle scalars.
  if (!ValueTy.isScalar())
    return false;

  // Don't allow truncating stores for now.
  if (StoreMI.getMemSizeInBits() != ValueTy.getSizeInBits())
    return false;

  // Avoid adding volatile or ordered stores to the candidate. We already have a
  // check for this in instMayAlias() but that only get's called later between
  // potential aliasing hazards.
  if (!StoreMI.isSimple())
    return false;

  Register StoreAddr = StoreMI.getPointerReg();
  auto BIO = getPointerInfo(StoreAddr, *MRI);
  Register StoreBase = BIO.getBase();
  if (C.Stores.empty()) {
    C.BasePtr = StoreBase;
    if (!BIO.hasValidOffset()) {
      C.CurrentLowestOffset = 0;
    } else {
      C.CurrentLowestOffset = BIO.getOffset();
    }
    // This is the first store of the candidate.
    // If the offset can't possibly allow for a lower addressed store with the
    // same base, don't bother adding it.
    if (BIO.hasValidOffset() &&
        BIO.getOffset() < static_cast<int64_t>(ValueTy.getSizeInBytes()))
      return false;
    C.Stores.emplace_back(&StoreMI);
    LLVM_DEBUG(dbgs() << "Starting a new merge candidate group with: "
                      << StoreMI);
    return true;
  }

  // Check the store is the same size as the existing ones in the candidate.
  if (MRI->getType(C.Stores[0]->getValueReg()).getSizeInBits() !=
      ValueTy.getSizeInBits())
    return false;

  if (MRI->getType(C.Stores[0]->getPointerReg()).getAddressSpace() !=
      PtrTy.getAddressSpace())
    return false;

  // There are other stores in the candidate. Check that the store address
  // writes to the next lowest adjacent address.
  if (C.BasePtr != StoreBase)
    return false;
  // If we don't have a valid offset, we can't guarantee to be an adjacent
  // offset.
  if (!BIO.hasValidOffset())
    return false;
  if ((C.CurrentLowestOffset -
       static_cast<int64_t>(ValueTy.getSizeInBytes())) != BIO.getOffset())
    return false;

  // This writes to an adjacent address. Allow it.
  C.Stores.emplace_back(&StoreMI);
  C.CurrentLowestOffset = C.CurrentLowestOffset - ValueTy.getSizeInBytes();
  LLVM_DEBUG(dbgs() << "Candidate added store: " << StoreMI);
  return true;
}

bool LoadStoreOpt::mergeBlockStores(MachineBasicBlock &MBB) {
  bool Changed = false;
  // Walk through the block bottom-up, looking for merging candidates.
  StoreMergeCandidate Candidate;
  for (MachineInstr &MI : llvm::reverse(MBB)) {
    if (InstsToErase.contains(&MI))
      continue;

    if (auto *StoreMI = dyn_cast<GStore>(&MI)) {
      // We have a G_STORE. Add it to the candidate if it writes to an adjacent
      // address.
      if (!addStoreToCandidate(*StoreMI, Candidate)) {
        // Store wasn't eligible to be added. May need to record it as a
        // potential alias.
        if (operationAliasesWithCandidate(*StoreMI, Candidate)) {
          Changed |= processMergeCandidate(Candidate);
          continue;
        }
        Candidate.addPotentialAlias(*StoreMI);
      }
      continue;
    }

    // If we don't have any stores yet, this instruction can't pose a problem.
    if (Candidate.Stores.empty())
      continue;

    // We're dealing with some other kind of instruction.
    if (isInstHardMergeHazard(MI)) {
      Changed |= processMergeCandidate(Candidate);
      Candidate.Stores.clear();
      continue;
    }

    if (!MI.mayLoadOrStore())
      continue;

    if (operationAliasesWithCandidate(MI, Candidate)) {
      // We have a potential alias, so process the current candidate if we can
      // and then continue looking for a new candidate.
      Changed |= processMergeCandidate(Candidate);
      continue;
    }

    // Record this instruction as a potential alias for future stores that are
    // added to the candidate.
    Candidate.addPotentialAlias(MI);
  }

  // Process any candidate left after finishing searching the entire block.
  Changed |= processMergeCandidate(Candidate);

  // Erase instructions now that we're no longer iterating over the block.
  for (auto *MI : InstsToErase)
    MI->eraseFromParent();
  InstsToErase.clear();
  return Changed;
}

/// Check if the store \p Store is a truncstore that can be merged. That is,
/// it's a store of a shifted value of \p SrcVal. If \p SrcVal is an empty
/// Register then it does not need to match and SrcVal is set to the source
/// value found.
/// On match, returns the start byte offset of the \p SrcVal that is being
/// stored.
static std::optional<int64_t>
getTruncStoreByteOffset(GStore &Store, Register &SrcVal,
                        MachineRegisterInfo &MRI) {
  Register TruncVal;
  if (!mi_match(Store.getValueReg(), MRI, m_GTrunc(m_Reg(TruncVal))))
    return std::nullopt;

  // The shift amount must be a constant multiple of the narrow type.
  // It is translated to the offset address in the wide source value "y".
  //
  // x = G_LSHR y, ShiftAmtC
  // s8 z = G_TRUNC x
  // store z, ...
  Register FoundSrcVal;
  int64_t ShiftAmt;
  if (!mi_match(TruncVal, MRI,
                m_any_of(m_GLShr(m_Reg(FoundSrcVal), m_ICst(ShiftAmt)),
                         m_GAShr(m_Reg(FoundSrcVal), m_ICst(ShiftAmt))))) {
    if (!SrcVal.isValid() || TruncVal == SrcVal) {
      if (!SrcVal.isValid())
        SrcVal = TruncVal;
      return 0; // If it's the lowest index store.
    }
    return std::nullopt;
  }

  unsigned NarrowBits = Store.getMMO().getMemoryType().getScalarSizeInBits();
  if (ShiftAmt % NarrowBits != 0)
    return std::nullopt;
  const unsigned Offset = ShiftAmt / NarrowBits;

  if (SrcVal.isValid() && FoundSrcVal != SrcVal)
    return std::nullopt;

  if (!SrcVal.isValid())
    SrcVal = FoundSrcVal;
  else if (MRI.getType(SrcVal) != MRI.getType(FoundSrcVal))
    return std::nullopt;
  return Offset;
}

/// Match a pattern where a wide type scalar value is stored by several narrow
/// stores. Fold it into a single store or a BSWAP and a store if the targets
/// supports it.
///
/// Assuming little endian target:
///  i8 *p = ...
///  i32 val = ...
///  p[0] = (val >> 0) & 0xFF;
///  p[1] = (val >> 8) & 0xFF;
///  p[2] = (val >> 16) & 0xFF;
///  p[3] = (val >> 24) & 0xFF;
/// =>
///  *((i32)p) = val;
///
///  i8 *p = ...
///  i32 val = ...
///  p[0] = (val >> 24) & 0xFF;
///  p[1] = (val >> 16) & 0xFF;
///  p[2] = (val >> 8) & 0xFF;
///  p[3] = (val >> 0) & 0xFF;
/// =>
///  *((i32)p) = BSWAP(val);
bool LoadStoreOpt::mergeTruncStore(GStore &StoreMI,
                                   SmallPtrSetImpl<GStore *> &DeletedStores) {
  LLT MemTy = StoreMI.getMMO().getMemoryType();

  // We only handle merging simple stores of 1-4 bytes.
  if (!MemTy.isScalar())
    return false;
  switch (MemTy.getSizeInBits()) {
  case 8:
  case 16:
  case 32:
    break;
  default:
    return false;
  }
  if (!StoreMI.isSimple())
    return false;

  // We do a simple search for mergeable stores prior to this one.
  // Any potential alias hazard along the way terminates the search.
  SmallVector<GStore *> FoundStores;

  // We're looking for:
  // 1) a (store(trunc(...)))
  // 2) of an LSHR/ASHR of a single wide value, by the appropriate shift to get
  //    the partial value stored.
  // 3) where the offsets form either a little or big-endian sequence.

  auto &LastStore = StoreMI;

  // The single base pointer that all stores must use.
  Register BaseReg;
  int64_t LastOffset;
  if (!mi_match(LastStore.getPointerReg(), *MRI,
                m_GPtrAdd(m_Reg(BaseReg), m_ICst(LastOffset)))) {
    BaseReg = LastStore.getPointerReg();
    LastOffset = 0;
  }

  GStore *LowestIdxStore = &LastStore;
  int64_t LowestIdxOffset = LastOffset;

  Register WideSrcVal;
  auto LowestShiftAmt = getTruncStoreByteOffset(LastStore, WideSrcVal, *MRI);
  if (!LowestShiftAmt)
    return false; // Didn't match a trunc.
  assert(WideSrcVal.isValid());

  LLT WideStoreTy = MRI->getType(WideSrcVal);
  // The wide type might not be a multiple of the memory type, e.g. s48 and s32.
  if (WideStoreTy.getSizeInBits() % MemTy.getSizeInBits() != 0)
    return false;
  const unsigned NumStoresRequired =
      WideStoreTy.getSizeInBits() / MemTy.getSizeInBits();

  SmallVector<int64_t, 8> OffsetMap(NumStoresRequired, INT64_MAX);
  OffsetMap[*LowestShiftAmt] = LastOffset;
  FoundStores.emplace_back(&LastStore);

  const int MaxInstsToCheck = 10;
  int NumInstsChecked = 0;
  for (auto II = ++LastStore.getReverseIterator();
       II != LastStore.getParent()->rend() && NumInstsChecked < MaxInstsToCheck;
       ++II) {
    NumInstsChecked++;
    GStore *NewStore;
    if ((NewStore = dyn_cast<GStore>(&*II))) {
      if (NewStore->getMMO().getMemoryType() != MemTy || !NewStore->isSimple())
        break;
    } else if (II->isLoadFoldBarrier() || II->mayLoad()) {
      break;
    } else {
      continue; // This is a safe instruction we can look past.
    }

    Register NewBaseReg;
    int64_t MemOffset;
    // Check we're storing to the same base + some offset.
    if (!mi_match(NewStore->getPointerReg(), *MRI,
                  m_GPtrAdd(m_Reg(NewBaseReg), m_ICst(MemOffset)))) {
      NewBaseReg = NewStore->getPointerReg();
      MemOffset = 0;
    }
    if (BaseReg != NewBaseReg)
      break;

    auto ShiftByteOffset = getTruncStoreByteOffset(*NewStore, WideSrcVal, *MRI);
    if (!ShiftByteOffset)
      break;
    if (MemOffset < LowestIdxOffset) {
      LowestIdxOffset = MemOffset;
      LowestIdxStore = NewStore;
    }

    // Map the offset in the store and the offset in the combined value, and
    // early return if it has been set before.
    if (*ShiftByteOffset < 0 || *ShiftByteOffset >= NumStoresRequired ||
        OffsetMap[*ShiftByteOffset] != INT64_MAX)
      break;
    OffsetMap[*ShiftByteOffset] = MemOffset;

    FoundStores.emplace_back(NewStore);
    // Reset counter since we've found a matching inst.
    NumInstsChecked = 0;
    if (FoundStores.size() == NumStoresRequired)
      break;
  }

  if (FoundStores.size() != NumStoresRequired) {
    if (FoundStores.size() == 1)
      return false;
    // We didn't find enough stores to merge into the size of the original
    // source value, but we may be able to generate a smaller store if we
    // truncate the source value.
    WideStoreTy = LLT::scalar(FoundStores.size() * MemTy.getScalarSizeInBits());
  }

  unsigned NumStoresFound = FoundStores.size();

  const auto &DL = LastStore.getMF()->getDataLayout();
  auto &C = LastStore.getMF()->getFunction().getContext();
  // Check that a store of the wide type is both allowed and fast on the target
  unsigned Fast = 0;
  bool Allowed = TLI->allowsMemoryAccess(
      C, DL, WideStoreTy, LowestIdxStore->getMMO(), &Fast);
  if (!Allowed || !Fast)
    return false;

  // Check if the pieces of the value are going to the expected places in memory
  // to merge the stores.
  unsigned NarrowBits = MemTy.getScalarSizeInBits();
  auto checkOffsets = [&](bool MatchLittleEndian) {
    if (MatchLittleEndian) {
      for (unsigned i = 0; i != NumStoresFound; ++i)
        if (OffsetMap[i] != i * (NarrowBits / 8) + LowestIdxOffset)
          return false;
    } else { // MatchBigEndian by reversing loop counter.
      for (unsigned i = 0, j = NumStoresFound - 1; i != NumStoresFound;
           ++i, --j)
        if (OffsetMap[j] != i * (NarrowBits / 8) + LowestIdxOffset)
          return false;
    }
    return true;
  };

  // Check if the offsets line up for the native data layout of this target.
  bool NeedBswap = false;
  bool NeedRotate = false;
  if (!checkOffsets(DL.isLittleEndian())) {
    // Special-case: check if byte offsets line up for the opposite endian.
    if (NarrowBits == 8 && checkOffsets(DL.isBigEndian()))
      NeedBswap = true;
    else if (NumStoresFound == 2 && checkOffsets(DL.isBigEndian()))
      NeedRotate = true;
    else
      return false;
  }

  if (NeedBswap &&
      !isLegalOrBeforeLegalizer({TargetOpcode::G_BSWAP, {WideStoreTy}}, *MF))
    return false;
  if (NeedRotate &&
      !isLegalOrBeforeLegalizer(
          {TargetOpcode::G_ROTR, {WideStoreTy, WideStoreTy}}, *MF))
    return false;

  Builder.setInstrAndDebugLoc(StoreMI);

  if (WideStoreTy != MRI->getType(WideSrcVal))
    WideSrcVal = Builder.buildTrunc(WideStoreTy, WideSrcVal).getReg(0);

  if (NeedBswap) {
    WideSrcVal = Builder.buildBSwap(WideStoreTy, WideSrcVal).getReg(0);
  } else if (NeedRotate) {
    assert(WideStoreTy.getSizeInBits() % 2 == 0 &&
           "Unexpected type for rotate");
    auto RotAmt =
        Builder.buildConstant(WideStoreTy, WideStoreTy.getSizeInBits() / 2);
    WideSrcVal =
        Builder.buildRotateRight(WideStoreTy, WideSrcVal, RotAmt).getReg(0);
  }

  Builder.buildStore(WideSrcVal, LowestIdxStore->getPointerReg(),
                     LowestIdxStore->getMMO().getPointerInfo(),
                     LowestIdxStore->getMMO().getAlign());

  // Erase the old stores.
  for (auto *ST : FoundStores) {
    ST->eraseFromParent();
    DeletedStores.insert(ST);
  }
  return true;
}

bool LoadStoreOpt::mergeTruncStoresBlock(MachineBasicBlock &BB) {
  bool Changed = false;
  SmallVector<GStore *, 16> Stores;
  SmallPtrSet<GStore *, 8> DeletedStores;
  // Walk up the block so we can see the most eligible stores.
  for (MachineInstr &MI : llvm::reverse(BB))
    if (auto *StoreMI = dyn_cast<GStore>(&MI))
      Stores.emplace_back(StoreMI);

  for (auto *StoreMI : Stores) {
    if (DeletedStores.count(StoreMI))
      continue;
    if (mergeTruncStore(*StoreMI, DeletedStores))
      Changed = true;
  }
  return Changed;
}

bool LoadStoreOpt::mergeFunctionStores(MachineFunction &MF) {
  bool Changed = false;
  for (auto &BB : MF){
    Changed |= mergeBlockStores(BB);
    Changed |= mergeTruncStoresBlock(BB);
  }

  // Erase all dead instructions left over by the merging.
  if (Changed) {
    for (auto &BB : MF) {
      for (auto &I : make_early_inc_range(make_range(BB.rbegin(), BB.rend()))) {
        if (isTriviallyDead(I, *MRI))
          I.eraseFromParent();
      }
    }
  }

  return Changed;
}

void LoadStoreOpt::initializeStoreMergeTargetInfo(unsigned AddrSpace) {
  // Query the legalizer info to record what store types are legal.
  // We record this because we don't want to bother trying to merge stores into
  // illegal ones, which would just result in being split again.

  if (LegalStoreSizes.count(AddrSpace)) {
    assert(LegalStoreSizes[AddrSpace].any());
    return; // Already cached sizes for this address space.
  }

  // Need to reserve at least MaxStoreSizeToForm + 1 bits.
  BitVector LegalSizes(MaxStoreSizeToForm * 2);
  const auto &LI = *MF->getSubtarget().getLegalizerInfo();
  const auto &DL = MF->getFunction().getDataLayout();
  Type *IRPtrTy = PointerType::get(MF->getFunction().getContext(), AddrSpace);
  LLT PtrTy = getLLTForType(*IRPtrTy, DL);
  // We assume that we're not going to be generating any stores wider than
  // MaxStoreSizeToForm bits for now.
  for (unsigned Size = 2; Size <= MaxStoreSizeToForm; Size *= 2) {
    LLT Ty = LLT::scalar(Size);
    SmallVector<LegalityQuery::MemDesc, 2> MemDescrs(
        {{Ty, Ty.getSizeInBits(), AtomicOrdering::NotAtomic}});
    SmallVector<LLT> StoreTys({Ty, PtrTy});
    LegalityQuery Q(TargetOpcode::G_STORE, StoreTys, MemDescrs);
    LegalizeActionStep ActionStep = LI.getAction(Q);
    if (ActionStep.Action == LegalizeActions::Legal)
      LegalSizes.set(Size);
  }
  assert(LegalSizes.any() && "Expected some store sizes to be legal!");
  LegalStoreSizes[AddrSpace] = LegalSizes;
}

bool LoadStoreOpt::runOnMachineFunction(MachineFunction &MF) {
  // If the ISel pipeline failed, do not bother running that pass.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;

  LLVM_DEBUG(dbgs() << "Begin memory optimizations for: " << MF.getName()
                    << '\n');

  init(MF);
  bool Changed = false;
  Changed |= mergeFunctionStores(MF);

  LegalStoreSizes.clear();
  return Changed;
}
