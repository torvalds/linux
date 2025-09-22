//===-- AMDGPUPromoteAlloca.cpp - Promote Allocas -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Eliminates allocas by either converting them into vectors or by migrating
// them to local address space.
//
// Two passes are exposed by this file:
//    - "promote-alloca-to-vector", which runs early in the pipeline and only
//      promotes to vector. Promotion to vector is almost always profitable
//      except when the alloca is too big and the promotion would result in
//      very high register pressure.
//    - "promote-alloca", which does both promotion to vector and LDS and runs
//      much later in the pipeline. This runs after SROA because promoting to
//      LDS is of course less profitable than getting rid of the alloca or
//      vectorizing it, thus we only want to do it when the only alternative is
//      lowering the alloca to stack.
//
// Note that both of them exist for the old and new PMs. The new PM passes are
// declared in AMDGPU.h and the legacy PM ones are declared here.s
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/InstSimplifyFolder.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsR600.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"

#define DEBUG_TYPE "amdgpu-promote-alloca"

using namespace llvm;

namespace {

static cl::opt<bool>
    DisablePromoteAllocaToVector("disable-promote-alloca-to-vector",
                                 cl::desc("Disable promote alloca to vector"),
                                 cl::init(false));

static cl::opt<bool>
    DisablePromoteAllocaToLDS("disable-promote-alloca-to-lds",
                              cl::desc("Disable promote alloca to LDS"),
                              cl::init(false));

static cl::opt<unsigned> PromoteAllocaToVectorLimit(
    "amdgpu-promote-alloca-to-vector-limit",
    cl::desc("Maximum byte size to consider promote alloca to vector"),
    cl::init(0));

static cl::opt<unsigned>
    LoopUserWeight("promote-alloca-vector-loop-user-weight",
                   cl::desc("The bonus weight of users of allocas within loop "
                            "when sorting profitable allocas"),
                   cl::init(4));

// Shared implementation which can do both promotion to vector and to LDS.
class AMDGPUPromoteAllocaImpl {
private:
  const TargetMachine &TM;
  LoopInfo &LI;
  Module *Mod = nullptr;
  const DataLayout *DL = nullptr;

  // FIXME: This should be per-kernel.
  uint32_t LocalMemLimit = 0;
  uint32_t CurrentLocalMemUsage = 0;
  unsigned MaxVGPRs;

  bool IsAMDGCN = false;
  bool IsAMDHSA = false;

  std::pair<Value *, Value *> getLocalSizeYZ(IRBuilder<> &Builder);
  Value *getWorkitemID(IRBuilder<> &Builder, unsigned N);

  /// BaseAlloca is the alloca root the search started from.
  /// Val may be that alloca or a recursive user of it.
  bool collectUsesWithPtrTypes(Value *BaseAlloca, Value *Val,
                               std::vector<Value *> &WorkList) const;

  /// Val is a derived pointer from Alloca. OpIdx0/OpIdx1 are the operand
  /// indices to an instruction with 2 pointer inputs (e.g. select, icmp).
  /// Returns true if both operands are derived from the same alloca. Val should
  /// be the same value as one of the input operands of UseInst.
  bool binaryOpIsDerivedFromSameAlloca(Value *Alloca, Value *Val,
                                       Instruction *UseInst, int OpIdx0,
                                       int OpIdx1) const;

  /// Check whether we have enough local memory for promotion.
  bool hasSufficientLocalMem(const Function &F);

  bool tryPromoteAllocaToVector(AllocaInst &I);
  bool tryPromoteAllocaToLDS(AllocaInst &I, bool SufficientLDS);

  void sortAllocasToPromote(SmallVectorImpl<AllocaInst *> &Allocas);

public:
  AMDGPUPromoteAllocaImpl(TargetMachine &TM, LoopInfo &LI) : TM(TM), LI(LI) {

    const Triple &TT = TM.getTargetTriple();
    IsAMDGCN = TT.getArch() == Triple::amdgcn;
    IsAMDHSA = TT.getOS() == Triple::AMDHSA;
  }

  bool run(Function &F, bool PromoteToLDS);
};

// FIXME: This can create globals so should be a module pass.
class AMDGPUPromoteAlloca : public FunctionPass {
public:
  static char ID;

  AMDGPUPromoteAlloca() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;
    if (auto *TPC = getAnalysisIfAvailable<TargetPassConfig>())
      return AMDGPUPromoteAllocaImpl(
                 TPC->getTM<TargetMachine>(),
                 getAnalysis<LoopInfoWrapperPass>().getLoopInfo())
          .run(F, /*PromoteToLDS*/ true);
    return false;
  }

  StringRef getPassName() const override { return "AMDGPU Promote Alloca"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LoopInfoWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }
};

class AMDGPUPromoteAllocaToVector : public FunctionPass {
public:
  static char ID;

  AMDGPUPromoteAllocaToVector() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;
    if (auto *TPC = getAnalysisIfAvailable<TargetPassConfig>())
      return AMDGPUPromoteAllocaImpl(
                 TPC->getTM<TargetMachine>(),
                 getAnalysis<LoopInfoWrapperPass>().getLoopInfo())
          .run(F, /*PromoteToLDS*/ false);
    return false;
  }

  StringRef getPassName() const override {
    return "AMDGPU Promote Alloca to vector";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LoopInfoWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }
};

unsigned getMaxVGPRs(const TargetMachine &TM, const Function &F) {
  if (!TM.getTargetTriple().isAMDGCN())
    return 128;

  const GCNSubtarget &ST = TM.getSubtarget<GCNSubtarget>(F);
  unsigned MaxVGPRs = ST.getMaxNumVGPRs(ST.getWavesPerEU(F).first);

  // A non-entry function has only 32 caller preserved registers.
  // Do not promote alloca which will force spilling unless we know the function
  // will be inlined.
  if (!F.hasFnAttribute(Attribute::AlwaysInline) &&
      !AMDGPU::isEntryFunctionCC(F.getCallingConv()))
    MaxVGPRs = std::min(MaxVGPRs, 32u);
  return MaxVGPRs;
}

} // end anonymous namespace

char AMDGPUPromoteAlloca::ID = 0;
char AMDGPUPromoteAllocaToVector::ID = 0;

INITIALIZE_PASS_BEGIN(AMDGPUPromoteAlloca, DEBUG_TYPE,
                      "AMDGPU promote alloca to vector or LDS", false, false)
// Move LDS uses from functions to kernels before promote alloca for accurate
// estimation of LDS available
INITIALIZE_PASS_DEPENDENCY(AMDGPULowerModuleLDSLegacy)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(AMDGPUPromoteAlloca, DEBUG_TYPE,
                    "AMDGPU promote alloca to vector or LDS", false, false)

INITIALIZE_PASS_BEGIN(AMDGPUPromoteAllocaToVector, DEBUG_TYPE "-to-vector",
                      "AMDGPU promote alloca to vector", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(AMDGPUPromoteAllocaToVector, DEBUG_TYPE "-to-vector",
                    "AMDGPU promote alloca to vector", false, false)

char &llvm::AMDGPUPromoteAllocaID = AMDGPUPromoteAlloca::ID;
char &llvm::AMDGPUPromoteAllocaToVectorID = AMDGPUPromoteAllocaToVector::ID;

PreservedAnalyses AMDGPUPromoteAllocaPass::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  bool Changed = AMDGPUPromoteAllocaImpl(TM, LI).run(F, /*PromoteToLDS=*/true);
  if (Changed) {
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    return PA;
  }
  return PreservedAnalyses::all();
}

PreservedAnalyses
AMDGPUPromoteAllocaToVectorPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  bool Changed = AMDGPUPromoteAllocaImpl(TM, LI).run(F, /*PromoteToLDS=*/false);
  if (Changed) {
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    return PA;
  }
  return PreservedAnalyses::all();
}

FunctionPass *llvm::createAMDGPUPromoteAlloca() {
  return new AMDGPUPromoteAlloca();
}

FunctionPass *llvm::createAMDGPUPromoteAllocaToVector() {
  return new AMDGPUPromoteAllocaToVector();
}

static void collectAllocaUses(AllocaInst &Alloca,
                              SmallVectorImpl<Use *> &Uses) {
  SmallVector<Instruction *, 4> WorkList({&Alloca});
  while (!WorkList.empty()) {
    auto *Cur = WorkList.pop_back_val();
    for (auto &U : Cur->uses()) {
      Uses.push_back(&U);

      if (isa<GetElementPtrInst>(U.getUser()))
        WorkList.push_back(cast<Instruction>(U.getUser()));
    }
  }
}

void AMDGPUPromoteAllocaImpl::sortAllocasToPromote(
    SmallVectorImpl<AllocaInst *> &Allocas) {
  DenseMap<AllocaInst *, unsigned> Scores;

  for (auto *Alloca : Allocas) {
    LLVM_DEBUG(dbgs() << "Scoring: " << *Alloca << "\n");
    unsigned &Score = Scores[Alloca];
    // Increment score by one for each user + a bonus for users within loops.
    SmallVector<Use *, 8> Uses;
    collectAllocaUses(*Alloca, Uses);
    for (auto *U : Uses) {
      Instruction *Inst = cast<Instruction>(U->getUser());
      if (isa<GetElementPtrInst>(Inst))
        continue;
      unsigned UserScore =
          1 + (LoopUserWeight * LI.getLoopDepth(Inst->getParent()));
      LLVM_DEBUG(dbgs() << "  [+" << UserScore << "]:\t" << *Inst << "\n");
      Score += UserScore;
    }
    LLVM_DEBUG(dbgs() << "  => Final Score:" << Score << "\n");
  }

  stable_sort(Allocas, [&](AllocaInst *A, AllocaInst *B) {
    return Scores.at(A) > Scores.at(B);
  });

  // clang-format off
  LLVM_DEBUG(
    dbgs() << "Sorted Worklist:\n";
    for (auto *A: Allocas)
      dbgs() << "  " << *A << "\n";
  );
  // clang-format on
}

bool AMDGPUPromoteAllocaImpl::run(Function &F, bool PromoteToLDS) {
  Mod = F.getParent();
  DL = &Mod->getDataLayout();

  const AMDGPUSubtarget &ST = AMDGPUSubtarget::get(TM, F);
  if (!ST.isPromoteAllocaEnabled())
    return false;

  MaxVGPRs = getMaxVGPRs(TM, F);

  bool SufficientLDS = PromoteToLDS ? hasSufficientLocalMem(F) : false;

  // Use up to 1/4 of available register budget for vectorization.
  // FIXME: Increase the limit for whole function budgets? Perhaps x2?
  unsigned VectorizationBudget =
      (PromoteAllocaToVectorLimit ? PromoteAllocaToVectorLimit * 8
                                  : (MaxVGPRs * 32)) /
      4;

  SmallVector<AllocaInst *, 16> Allocas;
  for (Instruction &I : F.getEntryBlock()) {
    if (AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
      // Array allocations are probably not worth handling, since an allocation
      // of the array type is the canonical form.
      if (!AI->isStaticAlloca() || AI->isArrayAllocation())
        continue;
      Allocas.push_back(AI);
    }
  }

  sortAllocasToPromote(Allocas);

  bool Changed = false;
  for (AllocaInst *AI : Allocas) {
    const unsigned AllocaCost = DL->getTypeSizeInBits(AI->getAllocatedType());
    // First, check if we have enough budget to vectorize this alloca.
    if (AllocaCost <= VectorizationBudget) {
      // If we do, attempt vectorization, otherwise, fall through and try
      // promoting to LDS instead.
      if (tryPromoteAllocaToVector(*AI)) {
        Changed = true;
        assert((VectorizationBudget - AllocaCost) < VectorizationBudget &&
               "Underflow!");
        VectorizationBudget -= AllocaCost;
        LLVM_DEBUG(dbgs() << "  Remaining vectorization budget:"
                          << VectorizationBudget << "\n");
        continue;
      }
    } else {
      LLVM_DEBUG(dbgs() << "Alloca too big for vectorization (size:"
                        << AllocaCost << ", budget:" << VectorizationBudget
                        << "): " << *AI << "\n");
    }

    if (PromoteToLDS && tryPromoteAllocaToLDS(*AI, SufficientLDS))
      Changed = true;
  }

  // NOTE: tryPromoteAllocaToVector removes the alloca, so Allocas contains
  // dangling pointers. If we want to reuse it past this point, the loop above
  // would need to be updated to remove successfully promoted allocas.

  return Changed;
}

struct MemTransferInfo {
  ConstantInt *SrcIndex = nullptr;
  ConstantInt *DestIndex = nullptr;
};

// Checks if the instruction I is a memset user of the alloca AI that we can
// deal with. Currently, only non-volatile memsets that affect the whole alloca
// are handled.
static bool isSupportedMemset(MemSetInst *I, AllocaInst *AI,
                              const DataLayout &DL) {
  using namespace PatternMatch;
  // For now we only care about non-volatile memsets that affect the whole type
  // (start at index 0 and fill the whole alloca).
  //
  // TODO: Now that we moved to PromoteAlloca we could handle any memsets
  // (except maybe volatile ones?) - we just need to use shufflevector if it
  // only affects a subset of the vector.
  const unsigned Size = DL.getTypeStoreSize(AI->getAllocatedType());
  return I->getOperand(0) == AI &&
         match(I->getOperand(2), m_SpecificInt(Size)) && !I->isVolatile();
}

static Value *
calculateVectorIndex(Value *Ptr,
                     const std::map<GetElementPtrInst *, Value *> &GEPIdx) {
  auto *GEP = dyn_cast<GetElementPtrInst>(Ptr->stripPointerCasts());
  if (!GEP)
    return ConstantInt::getNullValue(Type::getInt32Ty(Ptr->getContext()));

  auto I = GEPIdx.find(GEP);
  assert(I != GEPIdx.end() && "Must have entry for GEP!");
  return I->second;
}

static Value *GEPToVectorIndex(GetElementPtrInst *GEP, AllocaInst *Alloca,
                               Type *VecElemTy, const DataLayout &DL) {
  // TODO: Extracting a "multiple of X" from a GEP might be a useful generic
  // helper.
  unsigned BW = DL.getIndexTypeSizeInBits(GEP->getType());
  MapVector<Value *, APInt> VarOffsets;
  APInt ConstOffset(BW, 0);
  if (GEP->getPointerOperand()->stripPointerCasts() != Alloca ||
      !GEP->collectOffset(DL, BW, VarOffsets, ConstOffset))
    return nullptr;

  unsigned VecElemSize = DL.getTypeAllocSize(VecElemTy);
  if (VarOffsets.size() > 1)
    return nullptr;

  if (VarOffsets.size() == 1) {
    // Only handle cases where we don't need to insert extra arithmetic
    // instructions.
    const auto &VarOffset = VarOffsets.front();
    if (!ConstOffset.isZero() || VarOffset.second != VecElemSize)
      return nullptr;
    return VarOffset.first;
  }

  APInt Quot;
  uint64_t Rem;
  APInt::udivrem(ConstOffset, VecElemSize, Quot, Rem);
  if (Rem != 0)
    return nullptr;

  return ConstantInt::get(GEP->getContext(), Quot);
}

/// Promotes a single user of the alloca to a vector form.
///
/// \param Inst           Instruction to be promoted.
/// \param DL             Module Data Layout.
/// \param VectorTy       Vectorized Type.
/// \param VecStoreSize   Size of \p VectorTy in bytes.
/// \param ElementSize    Size of \p VectorTy element type in bytes.
/// \param TransferInfo   MemTransferInst info map.
/// \param GEPVectorIdx   GEP -> VectorIdx cache.
/// \param CurVal         Current value of the vector (e.g. last stored value)
/// \param[out]  DeferredLoads \p Inst is added to this vector if it can't
///              be promoted now. This happens when promoting requires \p
///              CurVal, but \p CurVal is nullptr.
/// \return the stored value if \p Inst would have written to the alloca, or
///         nullptr otherwise.
static Value *promoteAllocaUserToVector(
    Instruction *Inst, const DataLayout &DL, FixedVectorType *VectorTy,
    unsigned VecStoreSize, unsigned ElementSize,
    DenseMap<MemTransferInst *, MemTransferInfo> &TransferInfo,
    std::map<GetElementPtrInst *, Value *> &GEPVectorIdx, Value *CurVal,
    SmallVectorImpl<LoadInst *> &DeferredLoads) {
  // Note: we use InstSimplifyFolder because it can leverage the DataLayout
  // to do more folding, especially in the case of vector splats.
  IRBuilder<InstSimplifyFolder> Builder(Inst->getContext(),
                                        InstSimplifyFolder(DL));
  Builder.SetInsertPoint(Inst);

  const auto GetOrLoadCurrentVectorValue = [&]() -> Value * {
    if (CurVal)
      return CurVal;

    // If the current value is not known, insert a dummy load and lower it on
    // the second pass.
    LoadInst *Dummy =
        Builder.CreateLoad(VectorTy, PoisonValue::get(Builder.getPtrTy()),
                           "promotealloca.dummyload");
    DeferredLoads.push_back(Dummy);
    return Dummy;
  };

  const auto CreateTempPtrIntCast = [&Builder, DL](Value *Val,
                                                   Type *PtrTy) -> Value * {
    assert(DL.getTypeStoreSize(Val->getType()) == DL.getTypeStoreSize(PtrTy));
    const unsigned Size = DL.getTypeStoreSizeInBits(PtrTy);
    if (!PtrTy->isVectorTy())
      return Builder.CreateBitOrPointerCast(Val, Builder.getIntNTy(Size));
    const unsigned NumPtrElts = cast<FixedVectorType>(PtrTy)->getNumElements();
    // If we want to cast to cast, e.g. a <2 x ptr> into a <4 x i32>, we need to
    // first cast the ptr vector to <2 x i64>.
    assert((Size % NumPtrElts == 0) && "Vector size not divisble");
    Type *EltTy = Builder.getIntNTy(Size / NumPtrElts);
    return Builder.CreateBitOrPointerCast(
        Val, FixedVectorType::get(EltTy, NumPtrElts));
  };

  Type *VecEltTy = VectorTy->getElementType();

  switch (Inst->getOpcode()) {
  case Instruction::Load: {
    // Loads can only be lowered if the value is known.
    if (!CurVal) {
      DeferredLoads.push_back(cast<LoadInst>(Inst));
      return nullptr;
    }

    Value *Index = calculateVectorIndex(
        cast<LoadInst>(Inst)->getPointerOperand(), GEPVectorIdx);

    // We're loading the full vector.
    Type *AccessTy = Inst->getType();
    TypeSize AccessSize = DL.getTypeStoreSize(AccessTy);
    if (Constant *CI = dyn_cast<Constant>(Index)) {
      if (CI->isZeroValue() && AccessSize == VecStoreSize) {
        if (AccessTy->isPtrOrPtrVectorTy())
          CurVal = CreateTempPtrIntCast(CurVal, AccessTy);
        else if (CurVal->getType()->isPtrOrPtrVectorTy())
          CurVal = CreateTempPtrIntCast(CurVal, CurVal->getType());
        Value *NewVal = Builder.CreateBitOrPointerCast(CurVal, AccessTy);
        Inst->replaceAllUsesWith(NewVal);
        return nullptr;
      }
    }

    // Loading a subvector.
    if (isa<FixedVectorType>(AccessTy)) {
      assert(AccessSize.isKnownMultipleOf(DL.getTypeStoreSize(VecEltTy)));
      const unsigned NumLoadedElts = AccessSize / DL.getTypeStoreSize(VecEltTy);
      auto *SubVecTy = FixedVectorType::get(VecEltTy, NumLoadedElts);
      assert(DL.getTypeStoreSize(SubVecTy) == DL.getTypeStoreSize(AccessTy));

      Value *SubVec = PoisonValue::get(SubVecTy);
      for (unsigned K = 0; K < NumLoadedElts; ++K) {
        Value *CurIdx =
            Builder.CreateAdd(Index, ConstantInt::get(Index->getType(), K));
        SubVec = Builder.CreateInsertElement(
            SubVec, Builder.CreateExtractElement(CurVal, CurIdx), K);
      }

      if (AccessTy->isPtrOrPtrVectorTy())
        SubVec = CreateTempPtrIntCast(SubVec, AccessTy);
      else if (SubVecTy->isPtrOrPtrVectorTy())
        SubVec = CreateTempPtrIntCast(SubVec, SubVecTy);

      SubVec = Builder.CreateBitOrPointerCast(SubVec, AccessTy);
      Inst->replaceAllUsesWith(SubVec);
      return nullptr;
    }

    // We're loading one element.
    Value *ExtractElement = Builder.CreateExtractElement(CurVal, Index);
    if (AccessTy != VecEltTy)
      ExtractElement = Builder.CreateBitOrPointerCast(ExtractElement, AccessTy);

    Inst->replaceAllUsesWith(ExtractElement);
    return nullptr;
  }
  case Instruction::Store: {
    // For stores, it's a bit trickier and it depends on whether we're storing
    // the full vector or not. If we're storing the full vector, we don't need
    // to know the current value. If this is a store of a single element, we
    // need to know the value.
    StoreInst *SI = cast<StoreInst>(Inst);
    Value *Index = calculateVectorIndex(SI->getPointerOperand(), GEPVectorIdx);
    Value *Val = SI->getValueOperand();

    // We're storing the full vector, we can handle this without knowing CurVal.
    Type *AccessTy = Val->getType();
    TypeSize AccessSize = DL.getTypeStoreSize(AccessTy);
    if (Constant *CI = dyn_cast<Constant>(Index)) {
      if (CI->isZeroValue() && AccessSize == VecStoreSize) {
        if (AccessTy->isPtrOrPtrVectorTy())
          Val = CreateTempPtrIntCast(Val, AccessTy);
        else if (VectorTy->isPtrOrPtrVectorTy())
          Val = CreateTempPtrIntCast(Val, VectorTy);
        return Builder.CreateBitOrPointerCast(Val, VectorTy);
      }
    }

    // Storing a subvector.
    if (isa<FixedVectorType>(AccessTy)) {
      assert(AccessSize.isKnownMultipleOf(DL.getTypeStoreSize(VecEltTy)));
      const unsigned NumWrittenElts =
          AccessSize / DL.getTypeStoreSize(VecEltTy);
      const unsigned NumVecElts = VectorTy->getNumElements();
      auto *SubVecTy = FixedVectorType::get(VecEltTy, NumWrittenElts);
      assert(DL.getTypeStoreSize(SubVecTy) == DL.getTypeStoreSize(AccessTy));

      if (SubVecTy->isPtrOrPtrVectorTy())
        Val = CreateTempPtrIntCast(Val, SubVecTy);
      else if (AccessTy->isPtrOrPtrVectorTy())
        Val = CreateTempPtrIntCast(Val, AccessTy);

      Val = Builder.CreateBitOrPointerCast(Val, SubVecTy);

      Value *CurVec = GetOrLoadCurrentVectorValue();
      for (unsigned K = 0, NumElts = std::min(NumWrittenElts, NumVecElts);
           K < NumElts; ++K) {
        Value *CurIdx =
            Builder.CreateAdd(Index, ConstantInt::get(Index->getType(), K));
        CurVec = Builder.CreateInsertElement(
            CurVec, Builder.CreateExtractElement(Val, K), CurIdx);
      }
      return CurVec;
    }

    if (Val->getType() != VecEltTy)
      Val = Builder.CreateBitOrPointerCast(Val, VecEltTy);
    return Builder.CreateInsertElement(GetOrLoadCurrentVectorValue(), Val,
                                       Index);
  }
  case Instruction::Call: {
    if (auto *MTI = dyn_cast<MemTransferInst>(Inst)) {
      // For memcpy, we need to know curval.
      ConstantInt *Length = cast<ConstantInt>(MTI->getLength());
      unsigned NumCopied = Length->getZExtValue() / ElementSize;
      MemTransferInfo *TI = &TransferInfo[MTI];
      unsigned SrcBegin = TI->SrcIndex->getZExtValue();
      unsigned DestBegin = TI->DestIndex->getZExtValue();

      SmallVector<int> Mask;
      for (unsigned Idx = 0; Idx < VectorTy->getNumElements(); ++Idx) {
        if (Idx >= DestBegin && Idx < DestBegin + NumCopied) {
          Mask.push_back(SrcBegin++);
        } else {
          Mask.push_back(Idx);
        }
      }

      return Builder.CreateShuffleVector(GetOrLoadCurrentVectorValue(), Mask);
    }

    if (auto *MSI = dyn_cast<MemSetInst>(Inst)) {
      // For memset, we don't need to know the previous value because we
      // currently only allow memsets that cover the whole alloca.
      Value *Elt = MSI->getOperand(1);
      const unsigned BytesPerElt = DL.getTypeStoreSize(VecEltTy);
      if (BytesPerElt > 1) {
        Value *EltBytes = Builder.CreateVectorSplat(BytesPerElt, Elt);

        // If the element type of the vector is a pointer, we need to first cast
        // to an integer, then use a PtrCast.
        if (VecEltTy->isPointerTy()) {
          Type *PtrInt = Builder.getIntNTy(BytesPerElt * 8);
          Elt = Builder.CreateBitCast(EltBytes, PtrInt);
          Elt = Builder.CreateIntToPtr(Elt, VecEltTy);
        } else
          Elt = Builder.CreateBitCast(EltBytes, VecEltTy);
      }

      return Builder.CreateVectorSplat(VectorTy->getElementCount(), Elt);
    }

    if (auto *Intr = dyn_cast<IntrinsicInst>(Inst)) {
      if (Intr->getIntrinsicID() == Intrinsic::objectsize) {
        Intr->replaceAllUsesWith(
            Builder.getIntN(Intr->getType()->getIntegerBitWidth(),
                            DL.getTypeAllocSize(VectorTy)));
        return nullptr;
      }
    }

    llvm_unreachable("Unsupported call when promoting alloca to vector");
  }

  default:
    llvm_unreachable("Inconsistency in instructions promotable to vector");
  }

  llvm_unreachable("Did not return after promoting instruction!");
}

static bool isSupportedAccessType(FixedVectorType *VecTy, Type *AccessTy,
                                  const DataLayout &DL) {
  // Access as a vector type can work if the size of the access vector is a
  // multiple of the size of the alloca's vector element type.
  //
  // Examples:
  //    - VecTy = <8 x float>, AccessTy = <4 x float> -> OK
  //    - VecTy = <4 x double>, AccessTy = <2 x float> -> OK
  //    - VecTy = <4 x double>, AccessTy = <3 x float> -> NOT OK
  //        - 3*32 is not a multiple of 64
  //
  // We could handle more complicated cases, but it'd make things a lot more
  // complicated.
  if (isa<FixedVectorType>(AccessTy)) {
    TypeSize AccTS = DL.getTypeStoreSize(AccessTy);
    TypeSize VecTS = DL.getTypeStoreSize(VecTy->getElementType());
    return AccTS.isKnownMultipleOf(VecTS);
  }

  return CastInst::isBitOrNoopPointerCastable(VecTy->getElementType(), AccessTy,
                                              DL);
}

/// Iterates over an instruction worklist that may contain multiple instructions
/// from the same basic block, but in a different order.
template <typename InstContainer>
static void forEachWorkListItem(const InstContainer &WorkList,
                                std::function<void(Instruction *)> Fn) {
  // Bucket up uses of the alloca by the block they occur in.
  // This is important because we have to handle multiple defs/uses in a block
  // ourselves: SSAUpdater is purely for cross-block references.
  DenseMap<BasicBlock *, SmallDenseSet<Instruction *>> UsesByBlock;
  for (Instruction *User : WorkList)
    UsesByBlock[User->getParent()].insert(User);

  for (Instruction *User : WorkList) {
    BasicBlock *BB = User->getParent();
    auto &BlockUses = UsesByBlock[BB];

    // Already processed, skip.
    if (BlockUses.empty())
      continue;

    // Only user in the block, directly process it.
    if (BlockUses.size() == 1) {
      Fn(User);
      continue;
    }

    // Multiple users in the block, do a linear scan to see users in order.
    for (Instruction &Inst : *BB) {
      if (!BlockUses.contains(&Inst))
        continue;

      Fn(&Inst);
    }

    // Clear the block so we know it's been processed.
    BlockUses.clear();
  }
}

// FIXME: Should try to pick the most likely to be profitable allocas first.
bool AMDGPUPromoteAllocaImpl::tryPromoteAllocaToVector(AllocaInst &Alloca) {
  LLVM_DEBUG(dbgs() << "Trying to promote to vector: " << Alloca << '\n');

  if (DisablePromoteAllocaToVector) {
    LLVM_DEBUG(dbgs() << "  Promote alloca to vector is disabled\n");
    return false;
  }

  Type *AllocaTy = Alloca.getAllocatedType();
  auto *VectorTy = dyn_cast<FixedVectorType>(AllocaTy);
  if (auto *ArrayTy = dyn_cast<ArrayType>(AllocaTy)) {
    if (VectorType::isValidElementType(ArrayTy->getElementType()) &&
        ArrayTy->getNumElements() > 0)
      VectorTy = FixedVectorType::get(ArrayTy->getElementType(),
                                      ArrayTy->getNumElements());
  }

  // FIXME: There is no reason why we can't support larger arrays, we
  // are just being conservative for now.
  // FIXME: We also reject alloca's of the form [ 2 x [ 2 x i32 ]] or
  // equivalent. Potentially these could also be promoted but we don't currently
  // handle this case
  if (!VectorTy) {
    LLVM_DEBUG(dbgs() << "  Cannot convert type to vector\n");
    return false;
  }

  if (VectorTy->getNumElements() > 16 || VectorTy->getNumElements() < 2) {
    LLVM_DEBUG(dbgs() << "  " << *VectorTy
                      << " has an unsupported number of elements\n");
    return false;
  }

  std::map<GetElementPtrInst *, Value *> GEPVectorIdx;
  SmallVector<Instruction *> WorkList;
  SmallVector<Instruction *> UsersToRemove;
  SmallVector<Instruction *> DeferredInsts;
  DenseMap<MemTransferInst *, MemTransferInfo> TransferInfo;

  const auto RejectUser = [&](Instruction *Inst, Twine Msg) {
    LLVM_DEBUG(dbgs() << "  Cannot promote alloca to vector: " << Msg << "\n"
                      << "    " << *Inst << "\n");
    return false;
  };

  SmallVector<Use *, 8> Uses;
  collectAllocaUses(Alloca, Uses);

  LLVM_DEBUG(dbgs() << "  Attempting promotion to: " << *VectorTy << "\n");

  Type *VecEltTy = VectorTy->getElementType();
  unsigned ElementSize = DL->getTypeSizeInBits(VecEltTy) / 8;
  for (auto *U : Uses) {
    Instruction *Inst = cast<Instruction>(U->getUser());

    if (Value *Ptr = getLoadStorePointerOperand(Inst)) {
      // This is a store of the pointer, not to the pointer.
      if (isa<StoreInst>(Inst) &&
          U->getOperandNo() != StoreInst::getPointerOperandIndex())
        return RejectUser(Inst, "pointer is being stored");

      Type *AccessTy = getLoadStoreType(Inst);
      if (AccessTy->isAggregateType())
        return RejectUser(Inst, "unsupported load/store as aggregate");
      assert(!AccessTy->isAggregateType() || AccessTy->isArrayTy());

      // Check that this is a simple access of a vector element.
      bool IsSimple = isa<LoadInst>(Inst) ? cast<LoadInst>(Inst)->isSimple()
                                          : cast<StoreInst>(Inst)->isSimple();
      if (!IsSimple)
        return RejectUser(Inst, "not a simple load or store");

      Ptr = Ptr->stripPointerCasts();

      // Alloca already accessed as vector.
      if (Ptr == &Alloca && DL->getTypeStoreSize(Alloca.getAllocatedType()) ==
                                DL->getTypeStoreSize(AccessTy)) {
        WorkList.push_back(Inst);
        continue;
      }

      if (!isSupportedAccessType(VectorTy, AccessTy, *DL))
        return RejectUser(Inst, "not a supported access type");

      WorkList.push_back(Inst);
      continue;
    }

    if (auto *GEP = dyn_cast<GetElementPtrInst>(Inst)) {
      // If we can't compute a vector index from this GEP, then we can't
      // promote this alloca to vector.
      Value *Index = GEPToVectorIndex(GEP, &Alloca, VecEltTy, *DL);
      if (!Index)
        return RejectUser(Inst, "cannot compute vector index for GEP");

      GEPVectorIdx[GEP] = Index;
      UsersToRemove.push_back(Inst);
      continue;
    }

    if (MemSetInst *MSI = dyn_cast<MemSetInst>(Inst);
        MSI && isSupportedMemset(MSI, &Alloca, *DL)) {
      WorkList.push_back(Inst);
      continue;
    }

    if (MemTransferInst *TransferInst = dyn_cast<MemTransferInst>(Inst)) {
      if (TransferInst->isVolatile())
        return RejectUser(Inst, "mem transfer inst is volatile");

      ConstantInt *Len = dyn_cast<ConstantInt>(TransferInst->getLength());
      if (!Len || (Len->getZExtValue() % ElementSize))
        return RejectUser(Inst, "mem transfer inst length is non-constant or "
                                "not a multiple of the vector element size");

      if (!TransferInfo.count(TransferInst)) {
        DeferredInsts.push_back(Inst);
        WorkList.push_back(Inst);
        TransferInfo[TransferInst] = MemTransferInfo();
      }

      auto getPointerIndexOfAlloca = [&](Value *Ptr) -> ConstantInt * {
        GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr);
        if (Ptr != &Alloca && !GEPVectorIdx.count(GEP))
          return nullptr;

        return dyn_cast<ConstantInt>(calculateVectorIndex(Ptr, GEPVectorIdx));
      };

      unsigned OpNum = U->getOperandNo();
      MemTransferInfo *TI = &TransferInfo[TransferInst];
      if (OpNum == 0) {
        Value *Dest = TransferInst->getDest();
        ConstantInt *Index = getPointerIndexOfAlloca(Dest);
        if (!Index)
          return RejectUser(Inst, "could not calculate constant dest index");
        TI->DestIndex = Index;
      } else {
        assert(OpNum == 1);
        Value *Src = TransferInst->getSource();
        ConstantInt *Index = getPointerIndexOfAlloca(Src);
        if (!Index)
          return RejectUser(Inst, "could not calculate constant src index");
        TI->SrcIndex = Index;
      }
      continue;
    }

    if (auto *Intr = dyn_cast<IntrinsicInst>(Inst)) {
      if (Intr->getIntrinsicID() == Intrinsic::objectsize) {
        WorkList.push_back(Inst);
        continue;
      }
    }

    // Ignore assume-like intrinsics and comparisons used in assumes.
    if (isAssumeLikeIntrinsic(Inst)) {
      if (!Inst->use_empty())
        return RejectUser(Inst, "assume-like intrinsic cannot have any users");
      UsersToRemove.push_back(Inst);
      continue;
    }

    if (isa<ICmpInst>(Inst) && all_of(Inst->users(), [](User *U) {
          return isAssumeLikeIntrinsic(cast<Instruction>(U));
        })) {
      UsersToRemove.push_back(Inst);
      continue;
    }

    return RejectUser(Inst, "unhandled alloca user");
  }

  while (!DeferredInsts.empty()) {
    Instruction *Inst = DeferredInsts.pop_back_val();
    MemTransferInst *TransferInst = cast<MemTransferInst>(Inst);
    // TODO: Support the case if the pointers are from different alloca or
    // from different address spaces.
    MemTransferInfo &Info = TransferInfo[TransferInst];
    if (!Info.SrcIndex || !Info.DestIndex)
      return RejectUser(
          Inst, "mem transfer inst is missing constant src and/or dst index");
  }

  LLVM_DEBUG(dbgs() << "  Converting alloca to vector " << *AllocaTy << " -> "
                    << *VectorTy << '\n');
  const unsigned VecStoreSize = DL->getTypeStoreSize(VectorTy);

  // Alloca is uninitialized memory. Imitate that by making the first value
  // undef.
  SSAUpdater Updater;
  Updater.Initialize(VectorTy, "promotealloca");
  Updater.AddAvailableValue(Alloca.getParent(), UndefValue::get(VectorTy));

  // First handle the initial worklist.
  SmallVector<LoadInst *, 4> DeferredLoads;
  forEachWorkListItem(WorkList, [&](Instruction *I) {
    BasicBlock *BB = I->getParent();
    // On the first pass, we only take values that are trivially known, i.e.
    // where AddAvailableValue was already called in this block.
    Value *Result = promoteAllocaUserToVector(
        I, *DL, VectorTy, VecStoreSize, ElementSize, TransferInfo, GEPVectorIdx,
        Updater.FindValueForBlock(BB), DeferredLoads);
    if (Result)
      Updater.AddAvailableValue(BB, Result);
  });

  // Then handle deferred loads.
  forEachWorkListItem(DeferredLoads, [&](Instruction *I) {
    SmallVector<LoadInst *, 0> NewDLs;
    BasicBlock *BB = I->getParent();
    // On the second pass, we use GetValueInMiddleOfBlock to guarantee we always
    // get a value, inserting PHIs as needed.
    Value *Result = promoteAllocaUserToVector(
        I, *DL, VectorTy, VecStoreSize, ElementSize, TransferInfo, GEPVectorIdx,
        Updater.GetValueInMiddleOfBlock(I->getParent()), NewDLs);
    if (Result)
      Updater.AddAvailableValue(BB, Result);
    assert(NewDLs.empty() && "No more deferred loads should be queued!");
  });

  // Delete all instructions. On the first pass, new dummy loads may have been
  // added so we need to collect them too.
  DenseSet<Instruction *> InstsToDelete(WorkList.begin(), WorkList.end());
  InstsToDelete.insert(DeferredLoads.begin(), DeferredLoads.end());
  for (Instruction *I : InstsToDelete) {
    assert(I->use_empty());
    I->eraseFromParent();
  }

  // Delete all the users that are known to be removeable.
  for (Instruction *I : reverse(UsersToRemove)) {
    I->dropDroppableUses();
    assert(I->use_empty());
    I->eraseFromParent();
  }

  // Alloca should now be dead too.
  assert(Alloca.use_empty());
  Alloca.eraseFromParent();
  return true;
}

std::pair<Value *, Value *>
AMDGPUPromoteAllocaImpl::getLocalSizeYZ(IRBuilder<> &Builder) {
  Function &F = *Builder.GetInsertBlock()->getParent();
  const AMDGPUSubtarget &ST = AMDGPUSubtarget::get(TM, F);

  if (!IsAMDHSA) {
    Function *LocalSizeYFn =
        Intrinsic::getDeclaration(Mod, Intrinsic::r600_read_local_size_y);
    Function *LocalSizeZFn =
        Intrinsic::getDeclaration(Mod, Intrinsic::r600_read_local_size_z);

    CallInst *LocalSizeY = Builder.CreateCall(LocalSizeYFn, {});
    CallInst *LocalSizeZ = Builder.CreateCall(LocalSizeZFn, {});

    ST.makeLIDRangeMetadata(LocalSizeY);
    ST.makeLIDRangeMetadata(LocalSizeZ);

    return std::pair(LocalSizeY, LocalSizeZ);
  }

  // We must read the size out of the dispatch pointer.
  assert(IsAMDGCN);

  // We are indexing into this struct, and want to extract the workgroup_size_*
  // fields.
  //
  //   typedef struct hsa_kernel_dispatch_packet_s {
  //     uint16_t header;
  //     uint16_t setup;
  //     uint16_t workgroup_size_x ;
  //     uint16_t workgroup_size_y;
  //     uint16_t workgroup_size_z;
  //     uint16_t reserved0;
  //     uint32_t grid_size_x ;
  //     uint32_t grid_size_y ;
  //     uint32_t grid_size_z;
  //
  //     uint32_t private_segment_size;
  //     uint32_t group_segment_size;
  //     uint64_t kernel_object;
  //
  // #ifdef HSA_LARGE_MODEL
  //     void *kernarg_address;
  // #elif defined HSA_LITTLE_ENDIAN
  //     void *kernarg_address;
  //     uint32_t reserved1;
  // #else
  //     uint32_t reserved1;
  //     void *kernarg_address;
  // #endif
  //     uint64_t reserved2;
  //     hsa_signal_t completion_signal; // uint64_t wrapper
  //   } hsa_kernel_dispatch_packet_t
  //
  Function *DispatchPtrFn =
      Intrinsic::getDeclaration(Mod, Intrinsic::amdgcn_dispatch_ptr);

  CallInst *DispatchPtr = Builder.CreateCall(DispatchPtrFn, {});
  DispatchPtr->addRetAttr(Attribute::NoAlias);
  DispatchPtr->addRetAttr(Attribute::NonNull);
  F.removeFnAttr("amdgpu-no-dispatch-ptr");

  // Size of the dispatch packet struct.
  DispatchPtr->addDereferenceableRetAttr(64);

  Type *I32Ty = Type::getInt32Ty(Mod->getContext());
  Value *CastDispatchPtr = Builder.CreateBitCast(
      DispatchPtr, PointerType::get(I32Ty, AMDGPUAS::CONSTANT_ADDRESS));

  // We could do a single 64-bit load here, but it's likely that the basic
  // 32-bit and extract sequence is already present, and it is probably easier
  // to CSE this. The loads should be mergeable later anyway.
  Value *GEPXY = Builder.CreateConstInBoundsGEP1_64(I32Ty, CastDispatchPtr, 1);
  LoadInst *LoadXY = Builder.CreateAlignedLoad(I32Ty, GEPXY, Align(4));

  Value *GEPZU = Builder.CreateConstInBoundsGEP1_64(I32Ty, CastDispatchPtr, 2);
  LoadInst *LoadZU = Builder.CreateAlignedLoad(I32Ty, GEPZU, Align(4));

  MDNode *MD = MDNode::get(Mod->getContext(), std::nullopt);
  LoadXY->setMetadata(LLVMContext::MD_invariant_load, MD);
  LoadZU->setMetadata(LLVMContext::MD_invariant_load, MD);
  ST.makeLIDRangeMetadata(LoadZU);

  // Extract y component. Upper half of LoadZU should be zero already.
  Value *Y = Builder.CreateLShr(LoadXY, 16);

  return std::pair(Y, LoadZU);
}

Value *AMDGPUPromoteAllocaImpl::getWorkitemID(IRBuilder<> &Builder,
                                              unsigned N) {
  Function *F = Builder.GetInsertBlock()->getParent();
  const AMDGPUSubtarget &ST = AMDGPUSubtarget::get(TM, *F);
  Intrinsic::ID IntrID = Intrinsic::not_intrinsic;
  StringRef AttrName;

  switch (N) {
  case 0:
    IntrID = IsAMDGCN ? (Intrinsic::ID)Intrinsic::amdgcn_workitem_id_x
                      : (Intrinsic::ID)Intrinsic::r600_read_tidig_x;
    AttrName = "amdgpu-no-workitem-id-x";
    break;
  case 1:
    IntrID = IsAMDGCN ? (Intrinsic::ID)Intrinsic::amdgcn_workitem_id_y
                      : (Intrinsic::ID)Intrinsic::r600_read_tidig_y;
    AttrName = "amdgpu-no-workitem-id-y";
    break;

  case 2:
    IntrID = IsAMDGCN ? (Intrinsic::ID)Intrinsic::amdgcn_workitem_id_z
                      : (Intrinsic::ID)Intrinsic::r600_read_tidig_z;
    AttrName = "amdgpu-no-workitem-id-z";
    break;
  default:
    llvm_unreachable("invalid dimension");
  }

  Function *WorkitemIdFn = Intrinsic::getDeclaration(Mod, IntrID);
  CallInst *CI = Builder.CreateCall(WorkitemIdFn);
  ST.makeLIDRangeMetadata(CI);
  F->removeFnAttr(AttrName);

  return CI;
}

static bool isCallPromotable(CallInst *CI) {
  IntrinsicInst *II = dyn_cast<IntrinsicInst>(CI);
  if (!II)
    return false;

  switch (II->getIntrinsicID()) {
  case Intrinsic::memcpy:
  case Intrinsic::memmove:
  case Intrinsic::memset:
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
  case Intrinsic::invariant_start:
  case Intrinsic::invariant_end:
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group:
  case Intrinsic::objectsize:
    return true;
  default:
    return false;
  }
}

bool AMDGPUPromoteAllocaImpl::binaryOpIsDerivedFromSameAlloca(
    Value *BaseAlloca, Value *Val, Instruction *Inst, int OpIdx0,
    int OpIdx1) const {
  // Figure out which operand is the one we might not be promoting.
  Value *OtherOp = Inst->getOperand(OpIdx0);
  if (Val == OtherOp)
    OtherOp = Inst->getOperand(OpIdx1);

  if (isa<ConstantPointerNull>(OtherOp))
    return true;

  Value *OtherObj = getUnderlyingObject(OtherOp);
  if (!isa<AllocaInst>(OtherObj))
    return false;

  // TODO: We should be able to replace undefs with the right pointer type.

  // TODO: If we know the other base object is another promotable
  // alloca, not necessarily this alloca, we can do this. The
  // important part is both must have the same address space at
  // the end.
  if (OtherObj != BaseAlloca) {
    LLVM_DEBUG(
        dbgs() << "Found a binary instruction with another alloca object\n");
    return false;
  }

  return true;
}

bool AMDGPUPromoteAllocaImpl::collectUsesWithPtrTypes(
    Value *BaseAlloca, Value *Val, std::vector<Value *> &WorkList) const {

  for (User *User : Val->users()) {
    if (is_contained(WorkList, User))
      continue;

    if (CallInst *CI = dyn_cast<CallInst>(User)) {
      if (!isCallPromotable(CI))
        return false;

      WorkList.push_back(User);
      continue;
    }

    Instruction *UseInst = cast<Instruction>(User);
    if (UseInst->getOpcode() == Instruction::PtrToInt)
      return false;

    if (LoadInst *LI = dyn_cast<LoadInst>(UseInst)) {
      if (LI->isVolatile())
        return false;

      continue;
    }

    if (StoreInst *SI = dyn_cast<StoreInst>(UseInst)) {
      if (SI->isVolatile())
        return false;

      // Reject if the stored value is not the pointer operand.
      if (SI->getPointerOperand() != Val)
        return false;
    } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(UseInst)) {
      if (RMW->isVolatile())
        return false;
    } else if (AtomicCmpXchgInst *CAS = dyn_cast<AtomicCmpXchgInst>(UseInst)) {
      if (CAS->isVolatile())
        return false;
    }

    // Only promote a select if we know that the other select operand
    // is from another pointer that will also be promoted.
    if (ICmpInst *ICmp = dyn_cast<ICmpInst>(UseInst)) {
      if (!binaryOpIsDerivedFromSameAlloca(BaseAlloca, Val, ICmp, 0, 1))
        return false;

      // May need to rewrite constant operands.
      WorkList.push_back(ICmp);
    }

    if (UseInst->getOpcode() == Instruction::AddrSpaceCast) {
      // Give up if the pointer may be captured.
      if (PointerMayBeCaptured(UseInst, true, true))
        return false;
      // Don't collect the users of this.
      WorkList.push_back(User);
      continue;
    }

    // Do not promote vector/aggregate type instructions. It is hard to track
    // their users.
    if (isa<InsertValueInst>(User) || isa<InsertElementInst>(User))
      return false;

    if (!User->getType()->isPointerTy())
      continue;

    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(UseInst)) {
      // Be conservative if an address could be computed outside the bounds of
      // the alloca.
      if (!GEP->isInBounds())
        return false;
    }

    // Only promote a select if we know that the other select operand is from
    // another pointer that will also be promoted.
    if (SelectInst *SI = dyn_cast<SelectInst>(UseInst)) {
      if (!binaryOpIsDerivedFromSameAlloca(BaseAlloca, Val, SI, 1, 2))
        return false;
    }

    // Repeat for phis.
    if (PHINode *Phi = dyn_cast<PHINode>(UseInst)) {
      // TODO: Handle more complex cases. We should be able to replace loops
      // over arrays.
      switch (Phi->getNumIncomingValues()) {
      case 1:
        break;
      case 2:
        if (!binaryOpIsDerivedFromSameAlloca(BaseAlloca, Val, Phi, 0, 1))
          return false;
        break;
      default:
        return false;
      }
    }

    WorkList.push_back(User);
    if (!collectUsesWithPtrTypes(BaseAlloca, User, WorkList))
      return false;
  }

  return true;
}

bool AMDGPUPromoteAllocaImpl::hasSufficientLocalMem(const Function &F) {

  FunctionType *FTy = F.getFunctionType();
  const AMDGPUSubtarget &ST = AMDGPUSubtarget::get(TM, F);

  // If the function has any arguments in the local address space, then it's
  // possible these arguments require the entire local memory space, so
  // we cannot use local memory in the pass.
  for (Type *ParamTy : FTy->params()) {
    PointerType *PtrTy = dyn_cast<PointerType>(ParamTy);
    if (PtrTy && PtrTy->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS) {
      LocalMemLimit = 0;
      LLVM_DEBUG(dbgs() << "Function has local memory argument. Promoting to "
                           "local memory disabled.\n");
      return false;
    }
  }

  LocalMemLimit = ST.getAddressableLocalMemorySize();
  if (LocalMemLimit == 0)
    return false;

  SmallVector<const Constant *, 16> Stack;
  SmallPtrSet<const Constant *, 8> VisitedConstants;
  SmallPtrSet<const GlobalVariable *, 8> UsedLDS;

  auto visitUsers = [&](const GlobalVariable *GV, const Constant *Val) -> bool {
    for (const User *U : Val->users()) {
      if (const Instruction *Use = dyn_cast<Instruction>(U)) {
        if (Use->getParent()->getParent() == &F)
          return true;
      } else {
        const Constant *C = cast<Constant>(U);
        if (VisitedConstants.insert(C).second)
          Stack.push_back(C);
      }
    }

    return false;
  };

  for (GlobalVariable &GV : Mod->globals()) {
    if (GV.getAddressSpace() != AMDGPUAS::LOCAL_ADDRESS)
      continue;

    if (visitUsers(&GV, &GV)) {
      UsedLDS.insert(&GV);
      Stack.clear();
      continue;
    }

    // For any ConstantExpr uses, we need to recursively search the users until
    // we see a function.
    while (!Stack.empty()) {
      const Constant *C = Stack.pop_back_val();
      if (visitUsers(&GV, C)) {
        UsedLDS.insert(&GV);
        Stack.clear();
        break;
      }
    }
  }

  const DataLayout &DL = Mod->getDataLayout();
  SmallVector<std::pair<uint64_t, Align>, 16> AllocatedSizes;
  AllocatedSizes.reserve(UsedLDS.size());

  for (const GlobalVariable *GV : UsedLDS) {
    Align Alignment =
        DL.getValueOrABITypeAlignment(GV->getAlign(), GV->getValueType());
    uint64_t AllocSize = DL.getTypeAllocSize(GV->getValueType());

    // HIP uses an extern unsized array in local address space for dynamically
    // allocated shared memory.  In that case, we have to disable the promotion.
    if (GV->hasExternalLinkage() && AllocSize == 0) {
      LocalMemLimit = 0;
      LLVM_DEBUG(dbgs() << "Function has a reference to externally allocated "
                           "local memory. Promoting to local memory "
                           "disabled.\n");
      return false;
    }

    AllocatedSizes.emplace_back(AllocSize, Alignment);
  }

  // Sort to try to estimate the worst case alignment padding
  //
  // FIXME: We should really do something to fix the addresses to a more optimal
  // value instead
  llvm::sort(AllocatedSizes, llvm::less_second());

  // Check how much local memory is being used by global objects
  CurrentLocalMemUsage = 0;

  // FIXME: Try to account for padding here. The real padding and address is
  // currently determined from the inverse order of uses in the function when
  // legalizing, which could also potentially change. We try to estimate the
  // worst case here, but we probably should fix the addresses earlier.
  for (auto Alloc : AllocatedSizes) {
    CurrentLocalMemUsage = alignTo(CurrentLocalMemUsage, Alloc.second);
    CurrentLocalMemUsage += Alloc.first;
  }

  unsigned MaxOccupancy =
      ST.getOccupancyWithLocalMemSize(CurrentLocalMemUsage, F);

  // Restrict local memory usage so that we don't drastically reduce occupancy,
  // unless it is already significantly reduced.

  // TODO: Have some sort of hint or other heuristics to guess occupancy based
  // on other factors..
  unsigned OccupancyHint = ST.getWavesPerEU(F).second;
  if (OccupancyHint == 0)
    OccupancyHint = 7;

  // Clamp to max value.
  OccupancyHint = std::min(OccupancyHint, ST.getMaxWavesPerEU());

  // Check the hint but ignore it if it's obviously wrong from the existing LDS
  // usage.
  MaxOccupancy = std::min(OccupancyHint, MaxOccupancy);

  // Round up to the next tier of usage.
  unsigned MaxSizeWithWaveCount =
      ST.getMaxLocalMemSizeWithWaveCount(MaxOccupancy, F);

  // Program is possibly broken by using more local mem than available.
  if (CurrentLocalMemUsage > MaxSizeWithWaveCount)
    return false;

  LocalMemLimit = MaxSizeWithWaveCount;

  LLVM_DEBUG(dbgs() << F.getName() << " uses " << CurrentLocalMemUsage
                    << " bytes of LDS\n"
                    << "  Rounding size to " << MaxSizeWithWaveCount
                    << " with a maximum occupancy of " << MaxOccupancy << '\n'
                    << " and " << (LocalMemLimit - CurrentLocalMemUsage)
                    << " available for promotion\n");

  return true;
}

// FIXME: Should try to pick the most likely to be profitable allocas first.
bool AMDGPUPromoteAllocaImpl::tryPromoteAllocaToLDS(AllocaInst &I,
                                                    bool SufficientLDS) {
  LLVM_DEBUG(dbgs() << "Trying to promote to LDS: " << I << '\n');

  if (DisablePromoteAllocaToLDS) {
    LLVM_DEBUG(dbgs() << "  Promote alloca to LDS is disabled\n");
    return false;
  }

  const DataLayout &DL = Mod->getDataLayout();
  IRBuilder<> Builder(&I);

  const Function &ContainingFunction = *I.getParent()->getParent();
  CallingConv::ID CC = ContainingFunction.getCallingConv();

  // Don't promote the alloca to LDS for shader calling conventions as the work
  // item ID intrinsics are not supported for these calling conventions.
  // Furthermore not all LDS is available for some of the stages.
  switch (CC) {
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
    break;
  default:
    LLVM_DEBUG(
        dbgs()
        << " promote alloca to LDS not supported with calling convention.\n");
    return false;
  }

  // Not likely to have sufficient local memory for promotion.
  if (!SufficientLDS)
    return false;

  const AMDGPUSubtarget &ST = AMDGPUSubtarget::get(TM, ContainingFunction);
  unsigned WorkGroupSize = ST.getFlatWorkGroupSizes(ContainingFunction).second;

  Align Alignment =
      DL.getValueOrABITypeAlignment(I.getAlign(), I.getAllocatedType());

  // FIXME: This computed padding is likely wrong since it depends on inverse
  // usage order.
  //
  // FIXME: It is also possible that if we're allowed to use all of the memory
  // could end up using more than the maximum due to alignment padding.

  uint32_t NewSize = alignTo(CurrentLocalMemUsage, Alignment);
  uint32_t AllocSize =
      WorkGroupSize * DL.getTypeAllocSize(I.getAllocatedType());
  NewSize += AllocSize;

  if (NewSize > LocalMemLimit) {
    LLVM_DEBUG(dbgs() << "  " << AllocSize
                      << " bytes of local memory not available to promote\n");
    return false;
  }

  CurrentLocalMemUsage = NewSize;

  std::vector<Value *> WorkList;

  if (!collectUsesWithPtrTypes(&I, &I, WorkList)) {
    LLVM_DEBUG(dbgs() << " Do not know how to convert all uses\n");
    return false;
  }

  LLVM_DEBUG(dbgs() << "Promoting alloca to local memory\n");

  Function *F = I.getParent()->getParent();

  Type *GVTy = ArrayType::get(I.getAllocatedType(), WorkGroupSize);
  GlobalVariable *GV = new GlobalVariable(
      *Mod, GVTy, false, GlobalValue::InternalLinkage, PoisonValue::get(GVTy),
      Twine(F->getName()) + Twine('.') + I.getName(), nullptr,
      GlobalVariable::NotThreadLocal, AMDGPUAS::LOCAL_ADDRESS);
  GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(I.getAlign());

  Value *TCntY, *TCntZ;

  std::tie(TCntY, TCntZ) = getLocalSizeYZ(Builder);
  Value *TIdX = getWorkitemID(Builder, 0);
  Value *TIdY = getWorkitemID(Builder, 1);
  Value *TIdZ = getWorkitemID(Builder, 2);

  Value *Tmp0 = Builder.CreateMul(TCntY, TCntZ, "", true, true);
  Tmp0 = Builder.CreateMul(Tmp0, TIdX);
  Value *Tmp1 = Builder.CreateMul(TIdY, TCntZ, "", true, true);
  Value *TID = Builder.CreateAdd(Tmp0, Tmp1);
  TID = Builder.CreateAdd(TID, TIdZ);

  LLVMContext &Context = Mod->getContext();
  Value *Indices[] = {Constant::getNullValue(Type::getInt32Ty(Context)), TID};

  Value *Offset = Builder.CreateInBoundsGEP(GVTy, GV, Indices);
  I.mutateType(Offset->getType());
  I.replaceAllUsesWith(Offset);
  I.eraseFromParent();

  SmallVector<IntrinsicInst *> DeferredIntrs;

  for (Value *V : WorkList) {
    CallInst *Call = dyn_cast<CallInst>(V);
    if (!Call) {
      if (ICmpInst *CI = dyn_cast<ICmpInst>(V)) {
        PointerType *NewTy = PointerType::get(Context, AMDGPUAS::LOCAL_ADDRESS);

        if (isa<ConstantPointerNull>(CI->getOperand(0)))
          CI->setOperand(0, ConstantPointerNull::get(NewTy));

        if (isa<ConstantPointerNull>(CI->getOperand(1)))
          CI->setOperand(1, ConstantPointerNull::get(NewTy));

        continue;
      }

      // The operand's value should be corrected on its own and we don't want to
      // touch the users.
      if (isa<AddrSpaceCastInst>(V))
        continue;

      PointerType *NewTy = PointerType::get(Context, AMDGPUAS::LOCAL_ADDRESS);

      // FIXME: It doesn't really make sense to try to do this for all
      // instructions.
      V->mutateType(NewTy);

      // Adjust the types of any constant operands.
      if (SelectInst *SI = dyn_cast<SelectInst>(V)) {
        if (isa<ConstantPointerNull>(SI->getOperand(1)))
          SI->setOperand(1, ConstantPointerNull::get(NewTy));

        if (isa<ConstantPointerNull>(SI->getOperand(2)))
          SI->setOperand(2, ConstantPointerNull::get(NewTy));
      } else if (PHINode *Phi = dyn_cast<PHINode>(V)) {
        for (unsigned I = 0, E = Phi->getNumIncomingValues(); I != E; ++I) {
          if (isa<ConstantPointerNull>(Phi->getIncomingValue(I)))
            Phi->setIncomingValue(I, ConstantPointerNull::get(NewTy));
        }
      }

      continue;
    }

    IntrinsicInst *Intr = cast<IntrinsicInst>(Call);
    Builder.SetInsertPoint(Intr);
    switch (Intr->getIntrinsicID()) {
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
      // These intrinsics are for address space 0 only
      Intr->eraseFromParent();
      continue;
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
      // These have 2 pointer operands. In case if second pointer also needs
      // to be replaced we defer processing of these intrinsics until all
      // other values are processed.
      DeferredIntrs.push_back(Intr);
      continue;
    case Intrinsic::memset: {
      MemSetInst *MemSet = cast<MemSetInst>(Intr);
      Builder.CreateMemSet(MemSet->getRawDest(), MemSet->getValue(),
                           MemSet->getLength(), MemSet->getDestAlign(),
                           MemSet->isVolatile());
      Intr->eraseFromParent();
      continue;
    }
    case Intrinsic::invariant_start:
    case Intrinsic::invariant_end:
    case Intrinsic::launder_invariant_group:
    case Intrinsic::strip_invariant_group:
      Intr->eraseFromParent();
      // FIXME: I think the invariant marker should still theoretically apply,
      // but the intrinsics need to be changed to accept pointers with any
      // address space.
      continue;
    case Intrinsic::objectsize: {
      Value *Src = Intr->getOperand(0);
      Function *ObjectSize = Intrinsic::getDeclaration(
          Mod, Intrinsic::objectsize,
          {Intr->getType(),
           PointerType::get(Context, AMDGPUAS::LOCAL_ADDRESS)});

      CallInst *NewCall = Builder.CreateCall(
          ObjectSize,
          {Src, Intr->getOperand(1), Intr->getOperand(2), Intr->getOperand(3)});
      Intr->replaceAllUsesWith(NewCall);
      Intr->eraseFromParent();
      continue;
    }
    default:
      Intr->print(errs());
      llvm_unreachable("Don't know how to promote alloca intrinsic use.");
    }
  }

  for (IntrinsicInst *Intr : DeferredIntrs) {
    Builder.SetInsertPoint(Intr);
    Intrinsic::ID ID = Intr->getIntrinsicID();
    assert(ID == Intrinsic::memcpy || ID == Intrinsic::memmove);

    MemTransferInst *MI = cast<MemTransferInst>(Intr);
    auto *B = Builder.CreateMemTransferInst(
        ID, MI->getRawDest(), MI->getDestAlign(), MI->getRawSource(),
        MI->getSourceAlign(), MI->getLength(), MI->isVolatile());

    for (unsigned I = 0; I != 2; ++I) {
      if (uint64_t Bytes = Intr->getParamDereferenceableBytes(I)) {
        B->addDereferenceableParamAttr(I, Bytes);
      }
    }

    Intr->eraseFromParent();
  }

  return true;
}
