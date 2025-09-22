//===- AtomicExpandPass.cpp - Expand atomic instructions ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass (at IR level) to replace atomic instructions with
// __atomic_* library calls, or target specific instruction which implement the
// same semantics in a way which better fits the target backend.  This can
// include the use of (intrinsic-based) load-linked/store-conditional loops,
// AtomicCmpXchg, or type coercions.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/InstSimplifyFolder.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/AtomicExpand.h"
#include "llvm/CodeGen/AtomicExpandUtils.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/LowerAtomic.h"
#include <cassert>
#include <cstdint>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "atomic-expand"

namespace {

class AtomicExpandImpl {
  const TargetLowering *TLI = nullptr;
  const DataLayout *DL = nullptr;

private:
  bool bracketInstWithFences(Instruction *I, AtomicOrdering Order);
  IntegerType *getCorrespondingIntegerType(Type *T, const DataLayout &DL);
  LoadInst *convertAtomicLoadToIntegerType(LoadInst *LI);
  bool tryExpandAtomicLoad(LoadInst *LI);
  bool expandAtomicLoadToLL(LoadInst *LI);
  bool expandAtomicLoadToCmpXchg(LoadInst *LI);
  StoreInst *convertAtomicStoreToIntegerType(StoreInst *SI);
  bool tryExpandAtomicStore(StoreInst *SI);
  void expandAtomicStore(StoreInst *SI);
  bool tryExpandAtomicRMW(AtomicRMWInst *AI);
  AtomicRMWInst *convertAtomicXchgToIntegerType(AtomicRMWInst *RMWI);
  Value *
  insertRMWLLSCLoop(IRBuilderBase &Builder, Type *ResultTy, Value *Addr,
                    Align AddrAlign, AtomicOrdering MemOpOrder,
                    function_ref<Value *(IRBuilderBase &, Value *)> PerformOp);
  void expandAtomicOpToLLSC(
      Instruction *I, Type *ResultTy, Value *Addr, Align AddrAlign,
      AtomicOrdering MemOpOrder,
      function_ref<Value *(IRBuilderBase &, Value *)> PerformOp);
  void expandPartwordAtomicRMW(
      AtomicRMWInst *I, TargetLoweringBase::AtomicExpansionKind ExpansionKind);
  AtomicRMWInst *widenPartwordAtomicRMW(AtomicRMWInst *AI);
  bool expandPartwordCmpXchg(AtomicCmpXchgInst *I);
  void expandAtomicRMWToMaskedIntrinsic(AtomicRMWInst *AI);
  void expandAtomicCmpXchgToMaskedIntrinsic(AtomicCmpXchgInst *CI);

  AtomicCmpXchgInst *convertCmpXchgToIntegerType(AtomicCmpXchgInst *CI);
  static Value *insertRMWCmpXchgLoop(
      IRBuilderBase &Builder, Type *ResultType, Value *Addr, Align AddrAlign,
      AtomicOrdering MemOpOrder, SyncScope::ID SSID,
      function_ref<Value *(IRBuilderBase &, Value *)> PerformOp,
      CreateCmpXchgInstFun CreateCmpXchg);
  bool tryExpandAtomicCmpXchg(AtomicCmpXchgInst *CI);

  bool expandAtomicCmpXchg(AtomicCmpXchgInst *CI);
  bool isIdempotentRMW(AtomicRMWInst *RMWI);
  bool simplifyIdempotentRMW(AtomicRMWInst *RMWI);

  bool expandAtomicOpToLibcall(Instruction *I, unsigned Size, Align Alignment,
                               Value *PointerOperand, Value *ValueOperand,
                               Value *CASExpected, AtomicOrdering Ordering,
                               AtomicOrdering Ordering2,
                               ArrayRef<RTLIB::Libcall> Libcalls);
  void expandAtomicLoadToLibcall(LoadInst *LI);
  void expandAtomicStoreToLibcall(StoreInst *LI);
  void expandAtomicRMWToLibcall(AtomicRMWInst *I);
  void expandAtomicCASToLibcall(AtomicCmpXchgInst *I);

  friend bool
  llvm::expandAtomicRMWToCmpXchg(AtomicRMWInst *AI,
                                 CreateCmpXchgInstFun CreateCmpXchg);

public:
  bool run(Function &F, const TargetMachine *TM);
};

class AtomicExpandLegacy : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  AtomicExpandLegacy() : FunctionPass(ID) {
    initializeAtomicExpandLegacyPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;
};

// IRBuilder to be used for replacement atomic instructions.
struct ReplacementIRBuilder
    : IRBuilder<InstSimplifyFolder, IRBuilderCallbackInserter> {
  MDNode *MMRAMD = nullptr;

  // Preserves the DebugLoc from I, and preserves still valid metadata.
  // Enable StrictFP builder mode when appropriate.
  explicit ReplacementIRBuilder(Instruction *I, const DataLayout &DL)
      : IRBuilder(I->getContext(), DL,
                  IRBuilderCallbackInserter(
                      [this](Instruction *I) { addMMRAMD(I); })) {
    SetInsertPoint(I);
    this->CollectMetadataToCopy(I, {LLVMContext::MD_pcsections});
    if (BB->getParent()->getAttributes().hasFnAttr(Attribute::StrictFP))
      this->setIsFPConstrained(true);

    MMRAMD = I->getMetadata(LLVMContext::MD_mmra);
  }

  void addMMRAMD(Instruction *I) {
    if (canInstructionHaveMMRAs(*I))
      I->setMetadata(LLVMContext::MD_mmra, MMRAMD);
  }
};

} // end anonymous namespace

char AtomicExpandLegacy::ID = 0;

char &llvm::AtomicExpandID = AtomicExpandLegacy::ID;

INITIALIZE_PASS_BEGIN(AtomicExpandLegacy, DEBUG_TYPE,
                      "Expand Atomic instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(AtomicExpandLegacy, DEBUG_TYPE,
                    "Expand Atomic instructions", false, false)

// Helper functions to retrieve the size of atomic instructions.
static unsigned getAtomicOpSize(LoadInst *LI) {
  const DataLayout &DL = LI->getDataLayout();
  return DL.getTypeStoreSize(LI->getType());
}

static unsigned getAtomicOpSize(StoreInst *SI) {
  const DataLayout &DL = SI->getDataLayout();
  return DL.getTypeStoreSize(SI->getValueOperand()->getType());
}

static unsigned getAtomicOpSize(AtomicRMWInst *RMWI) {
  const DataLayout &DL = RMWI->getDataLayout();
  return DL.getTypeStoreSize(RMWI->getValOperand()->getType());
}

static unsigned getAtomicOpSize(AtomicCmpXchgInst *CASI) {
  const DataLayout &DL = CASI->getDataLayout();
  return DL.getTypeStoreSize(CASI->getCompareOperand()->getType());
}

// Determine if a particular atomic operation has a supported size,
// and is of appropriate alignment, to be passed through for target
// lowering. (Versus turning into a __atomic libcall)
template <typename Inst>
static bool atomicSizeSupported(const TargetLowering *TLI, Inst *I) {
  unsigned Size = getAtomicOpSize(I);
  Align Alignment = I->getAlign();
  return Alignment >= Size &&
         Size <= TLI->getMaxAtomicSizeInBitsSupported() / 8;
}

bool AtomicExpandImpl::run(Function &F, const TargetMachine *TM) {
  const auto *Subtarget = TM->getSubtargetImpl(F);
  if (!Subtarget->enableAtomicExpand())
    return false;
  TLI = Subtarget->getTargetLowering();
  DL = &F.getDataLayout();

  SmallVector<Instruction *, 1> AtomicInsts;

  // Changing control-flow while iterating through it is a bad idea, so gather a
  // list of all atomic instructions before we start.
  for (Instruction &I : instructions(F))
    if (I.isAtomic() && !isa<FenceInst>(&I))
      AtomicInsts.push_back(&I);

  bool MadeChange = false;
  for (auto *I : AtomicInsts) {
    auto LI = dyn_cast<LoadInst>(I);
    auto SI = dyn_cast<StoreInst>(I);
    auto RMWI = dyn_cast<AtomicRMWInst>(I);
    auto CASI = dyn_cast<AtomicCmpXchgInst>(I);
    assert((LI || SI || RMWI || CASI) && "Unknown atomic instruction");

    // If the Size/Alignment is not supported, replace with a libcall.
    if (LI) {
      if (!atomicSizeSupported(TLI, LI)) {
        expandAtomicLoadToLibcall(LI);
        MadeChange = true;
        continue;
      }
    } else if (SI) {
      if (!atomicSizeSupported(TLI, SI)) {
        expandAtomicStoreToLibcall(SI);
        MadeChange = true;
        continue;
      }
    } else if (RMWI) {
      if (!atomicSizeSupported(TLI, RMWI)) {
        expandAtomicRMWToLibcall(RMWI);
        MadeChange = true;
        continue;
      }
    } else if (CASI) {
      if (!atomicSizeSupported(TLI, CASI)) {
        expandAtomicCASToLibcall(CASI);
        MadeChange = true;
        continue;
      }
    }

    if (LI && TLI->shouldCastAtomicLoadInIR(LI) ==
                  TargetLoweringBase::AtomicExpansionKind::CastToInteger) {
      I = LI = convertAtomicLoadToIntegerType(LI);
      MadeChange = true;
    } else if (SI &&
               TLI->shouldCastAtomicStoreInIR(SI) ==
                   TargetLoweringBase::AtomicExpansionKind::CastToInteger) {
      I = SI = convertAtomicStoreToIntegerType(SI);
      MadeChange = true;
    } else if (RMWI &&
               TLI->shouldCastAtomicRMWIInIR(RMWI) ==
                   TargetLoweringBase::AtomicExpansionKind::CastToInteger) {
      I = RMWI = convertAtomicXchgToIntegerType(RMWI);
      MadeChange = true;
    } else if (CASI) {
      // TODO: when we're ready to make the change at the IR level, we can
      // extend convertCmpXchgToInteger for floating point too.
      if (CASI->getCompareOperand()->getType()->isPointerTy()) {
        // TODO: add a TLI hook to control this so that each target can
        // convert to lowering the original type one at a time.
        I = CASI = convertCmpXchgToIntegerType(CASI);
        MadeChange = true;
      }
    }

    if (TLI->shouldInsertFencesForAtomic(I)) {
      auto FenceOrdering = AtomicOrdering::Monotonic;
      if (LI && isAcquireOrStronger(LI->getOrdering())) {
        FenceOrdering = LI->getOrdering();
        LI->setOrdering(AtomicOrdering::Monotonic);
      } else if (SI && isReleaseOrStronger(SI->getOrdering())) {
        FenceOrdering = SI->getOrdering();
        SI->setOrdering(AtomicOrdering::Monotonic);
      } else if (RMWI && (isReleaseOrStronger(RMWI->getOrdering()) ||
                          isAcquireOrStronger(RMWI->getOrdering()))) {
        FenceOrdering = RMWI->getOrdering();
        RMWI->setOrdering(AtomicOrdering::Monotonic);
      } else if (CASI &&
                 TLI->shouldExpandAtomicCmpXchgInIR(CASI) ==
                     TargetLoweringBase::AtomicExpansionKind::None &&
                 (isReleaseOrStronger(CASI->getSuccessOrdering()) ||
                  isAcquireOrStronger(CASI->getSuccessOrdering()) ||
                  isAcquireOrStronger(CASI->getFailureOrdering()))) {
        // If a compare and swap is lowered to LL/SC, we can do smarter fence
        // insertion, with a stronger one on the success path than on the
        // failure path. As a result, fence insertion is directly done by
        // expandAtomicCmpXchg in that case.
        FenceOrdering = CASI->getMergedOrdering();
        CASI->setSuccessOrdering(AtomicOrdering::Monotonic);
        CASI->setFailureOrdering(AtomicOrdering::Monotonic);
      }

      if (FenceOrdering != AtomicOrdering::Monotonic) {
        MadeChange |= bracketInstWithFences(I, FenceOrdering);
      }
    } else if (I->hasAtomicStore() &&
               TLI->shouldInsertTrailingFenceForAtomicStore(I)) {
      auto FenceOrdering = AtomicOrdering::Monotonic;
      if (SI)
        FenceOrdering = SI->getOrdering();
      else if (RMWI)
        FenceOrdering = RMWI->getOrdering();
      else if (CASI && TLI->shouldExpandAtomicCmpXchgInIR(CASI) !=
                           TargetLoweringBase::AtomicExpansionKind::LLSC)
        // LLSC is handled in expandAtomicCmpXchg().
        FenceOrdering = CASI->getSuccessOrdering();

      IRBuilder Builder(I);
      if (auto TrailingFence =
              TLI->emitTrailingFence(Builder, I, FenceOrdering)) {
        TrailingFence->moveAfter(I);
        MadeChange = true;
      }
    }

    if (LI)
      MadeChange |= tryExpandAtomicLoad(LI);
    else if (SI)
      MadeChange |= tryExpandAtomicStore(SI);
    else if (RMWI) {
      // There are two different ways of expanding RMW instructions:
      // - into a load if it is idempotent
      // - into a Cmpxchg/LL-SC loop otherwise
      // we try them in that order.

      if (isIdempotentRMW(RMWI) && simplifyIdempotentRMW(RMWI)) {
        MadeChange = true;
      } else {
        MadeChange |= tryExpandAtomicRMW(RMWI);
      }
    } else if (CASI)
      MadeChange |= tryExpandAtomicCmpXchg(CASI);
  }
  return MadeChange;
}

bool AtomicExpandLegacy::runOnFunction(Function &F) {

  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;
  auto *TM = &TPC->getTM<TargetMachine>();
  AtomicExpandImpl AE;
  return AE.run(F, TM);
}

FunctionPass *llvm::createAtomicExpandLegacyPass() {
  return new AtomicExpandLegacy();
}

PreservedAnalyses AtomicExpandPass::run(Function &F,
                                        FunctionAnalysisManager &AM) {
  AtomicExpandImpl AE;

  bool Changed = AE.run(F, TM);
  if (!Changed)
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

bool AtomicExpandImpl::bracketInstWithFences(Instruction *I,
                                             AtomicOrdering Order) {
  ReplacementIRBuilder Builder(I, *DL);

  auto LeadingFence = TLI->emitLeadingFence(Builder, I, Order);

  auto TrailingFence = TLI->emitTrailingFence(Builder, I, Order);
  // We have a guard here because not every atomic operation generates a
  // trailing fence.
  if (TrailingFence)
    TrailingFence->moveAfter(I);

  return (LeadingFence || TrailingFence);
}

/// Get the iX type with the same bitwidth as T.
IntegerType *
AtomicExpandImpl::getCorrespondingIntegerType(Type *T, const DataLayout &DL) {
  EVT VT = TLI->getMemValueType(DL, T);
  unsigned BitWidth = VT.getStoreSizeInBits();
  assert(BitWidth == VT.getSizeInBits() && "must be a power of two");
  return IntegerType::get(T->getContext(), BitWidth);
}

/// Convert an atomic load of a non-integral type to an integer load of the
/// equivalent bitwidth.  See the function comment on
/// convertAtomicStoreToIntegerType for background.
LoadInst *AtomicExpandImpl::convertAtomicLoadToIntegerType(LoadInst *LI) {
  auto *M = LI->getModule();
  Type *NewTy = getCorrespondingIntegerType(LI->getType(), M->getDataLayout());

  ReplacementIRBuilder Builder(LI, *DL);

  Value *Addr = LI->getPointerOperand();

  auto *NewLI = Builder.CreateLoad(NewTy, Addr);
  NewLI->setAlignment(LI->getAlign());
  NewLI->setVolatile(LI->isVolatile());
  NewLI->setAtomic(LI->getOrdering(), LI->getSyncScopeID());
  LLVM_DEBUG(dbgs() << "Replaced " << *LI << " with " << *NewLI << "\n");

  Value *NewVal = Builder.CreateBitCast(NewLI, LI->getType());
  LI->replaceAllUsesWith(NewVal);
  LI->eraseFromParent();
  return NewLI;
}

AtomicRMWInst *
AtomicExpandImpl::convertAtomicXchgToIntegerType(AtomicRMWInst *RMWI) {
  auto *M = RMWI->getModule();
  Type *NewTy =
      getCorrespondingIntegerType(RMWI->getType(), M->getDataLayout());

  ReplacementIRBuilder Builder(RMWI, *DL);

  Value *Addr = RMWI->getPointerOperand();
  Value *Val = RMWI->getValOperand();
  Value *NewVal = Val->getType()->isPointerTy()
                      ? Builder.CreatePtrToInt(Val, NewTy)
                      : Builder.CreateBitCast(Val, NewTy);

  auto *NewRMWI = Builder.CreateAtomicRMW(AtomicRMWInst::Xchg, Addr, NewVal,
                                          RMWI->getAlign(), RMWI->getOrdering(),
                                          RMWI->getSyncScopeID());
  NewRMWI->setVolatile(RMWI->isVolatile());
  LLVM_DEBUG(dbgs() << "Replaced " << *RMWI << " with " << *NewRMWI << "\n");

  Value *NewRVal = RMWI->getType()->isPointerTy()
                       ? Builder.CreateIntToPtr(NewRMWI, RMWI->getType())
                       : Builder.CreateBitCast(NewRMWI, RMWI->getType());
  RMWI->replaceAllUsesWith(NewRVal);
  RMWI->eraseFromParent();
  return NewRMWI;
}

bool AtomicExpandImpl::tryExpandAtomicLoad(LoadInst *LI) {
  switch (TLI->shouldExpandAtomicLoadInIR(LI)) {
  case TargetLoweringBase::AtomicExpansionKind::None:
    return false;
  case TargetLoweringBase::AtomicExpansionKind::LLSC:
    expandAtomicOpToLLSC(
        LI, LI->getType(), LI->getPointerOperand(), LI->getAlign(),
        LI->getOrdering(),
        [](IRBuilderBase &Builder, Value *Loaded) { return Loaded; });
    return true;
  case TargetLoweringBase::AtomicExpansionKind::LLOnly:
    return expandAtomicLoadToLL(LI);
  case TargetLoweringBase::AtomicExpansionKind::CmpXChg:
    return expandAtomicLoadToCmpXchg(LI);
  case TargetLoweringBase::AtomicExpansionKind::NotAtomic:
    LI->setAtomic(AtomicOrdering::NotAtomic);
    return true;
  default:
    llvm_unreachable("Unhandled case in tryExpandAtomicLoad");
  }
}

bool AtomicExpandImpl::tryExpandAtomicStore(StoreInst *SI) {
  switch (TLI->shouldExpandAtomicStoreInIR(SI)) {
  case TargetLoweringBase::AtomicExpansionKind::None:
    return false;
  case TargetLoweringBase::AtomicExpansionKind::Expand:
    expandAtomicStore(SI);
    return true;
  case TargetLoweringBase::AtomicExpansionKind::NotAtomic:
    SI->setAtomic(AtomicOrdering::NotAtomic);
    return true;
  default:
    llvm_unreachable("Unhandled case in tryExpandAtomicStore");
  }
}

bool AtomicExpandImpl::expandAtomicLoadToLL(LoadInst *LI) {
  ReplacementIRBuilder Builder(LI, *DL);

  // On some architectures, load-linked instructions are atomic for larger
  // sizes than normal loads. For example, the only 64-bit load guaranteed
  // to be single-copy atomic by ARM is an ldrexd (A3.5.3).
  Value *Val = TLI->emitLoadLinked(Builder, LI->getType(),
                                   LI->getPointerOperand(), LI->getOrdering());
  TLI->emitAtomicCmpXchgNoStoreLLBalance(Builder);

  LI->replaceAllUsesWith(Val);
  LI->eraseFromParent();

  return true;
}

bool AtomicExpandImpl::expandAtomicLoadToCmpXchg(LoadInst *LI) {
  ReplacementIRBuilder Builder(LI, *DL);
  AtomicOrdering Order = LI->getOrdering();
  if (Order == AtomicOrdering::Unordered)
    Order = AtomicOrdering::Monotonic;

  Value *Addr = LI->getPointerOperand();
  Type *Ty = LI->getType();
  Constant *DummyVal = Constant::getNullValue(Ty);

  Value *Pair = Builder.CreateAtomicCmpXchg(
      Addr, DummyVal, DummyVal, LI->getAlign(), Order,
      AtomicCmpXchgInst::getStrongestFailureOrdering(Order));
  Value *Loaded = Builder.CreateExtractValue(Pair, 0, "loaded");

  LI->replaceAllUsesWith(Loaded);
  LI->eraseFromParent();

  return true;
}

/// Convert an atomic store of a non-integral type to an integer store of the
/// equivalent bitwidth.  We used to not support floating point or vector
/// atomics in the IR at all.  The backends learned to deal with the bitcast
/// idiom because that was the only way of expressing the notion of a atomic
/// float or vector store.  The long term plan is to teach each backend to
/// instruction select from the original atomic store, but as a migration
/// mechanism, we convert back to the old format which the backends understand.
/// Each backend will need individual work to recognize the new format.
StoreInst *AtomicExpandImpl::convertAtomicStoreToIntegerType(StoreInst *SI) {
  ReplacementIRBuilder Builder(SI, *DL);
  auto *M = SI->getModule();
  Type *NewTy = getCorrespondingIntegerType(SI->getValueOperand()->getType(),
                                            M->getDataLayout());
  Value *NewVal = Builder.CreateBitCast(SI->getValueOperand(), NewTy);

  Value *Addr = SI->getPointerOperand();

  StoreInst *NewSI = Builder.CreateStore(NewVal, Addr);
  NewSI->setAlignment(SI->getAlign());
  NewSI->setVolatile(SI->isVolatile());
  NewSI->setAtomic(SI->getOrdering(), SI->getSyncScopeID());
  LLVM_DEBUG(dbgs() << "Replaced " << *SI << " with " << *NewSI << "\n");
  SI->eraseFromParent();
  return NewSI;
}

void AtomicExpandImpl::expandAtomicStore(StoreInst *SI) {
  // This function is only called on atomic stores that are too large to be
  // atomic if implemented as a native store. So we replace them by an
  // atomic swap, that can be implemented for example as a ldrex/strex on ARM
  // or lock cmpxchg8/16b on X86, as these are atomic for larger sizes.
  // It is the responsibility of the target to only signal expansion via
  // shouldExpandAtomicRMW in cases where this is required and possible.
  ReplacementIRBuilder Builder(SI, *DL);
  AtomicOrdering Ordering = SI->getOrdering();
  assert(Ordering != AtomicOrdering::NotAtomic);
  AtomicOrdering RMWOrdering = Ordering == AtomicOrdering::Unordered
                                   ? AtomicOrdering::Monotonic
                                   : Ordering;
  AtomicRMWInst *AI = Builder.CreateAtomicRMW(
      AtomicRMWInst::Xchg, SI->getPointerOperand(), SI->getValueOperand(),
      SI->getAlign(), RMWOrdering);
  SI->eraseFromParent();

  // Now we have an appropriate swap instruction, lower it as usual.
  tryExpandAtomicRMW(AI);
}

static void createCmpXchgInstFun(IRBuilderBase &Builder, Value *Addr,
                                 Value *Loaded, Value *NewVal, Align AddrAlign,
                                 AtomicOrdering MemOpOrder, SyncScope::ID SSID,
                                 Value *&Success, Value *&NewLoaded) {
  Type *OrigTy = NewVal->getType();

  // This code can go away when cmpxchg supports FP and vector types.
  assert(!OrigTy->isPointerTy());
  bool NeedBitcast = OrigTy->isFloatingPointTy() || OrigTy->isVectorTy();
  if (NeedBitcast) {
    IntegerType *IntTy = Builder.getIntNTy(OrigTy->getPrimitiveSizeInBits());
    NewVal = Builder.CreateBitCast(NewVal, IntTy);
    Loaded = Builder.CreateBitCast(Loaded, IntTy);
  }

  Value *Pair = Builder.CreateAtomicCmpXchg(
      Addr, Loaded, NewVal, AddrAlign, MemOpOrder,
      AtomicCmpXchgInst::getStrongestFailureOrdering(MemOpOrder), SSID);
  Success = Builder.CreateExtractValue(Pair, 1, "success");
  NewLoaded = Builder.CreateExtractValue(Pair, 0, "newloaded");

  if (NeedBitcast)
    NewLoaded = Builder.CreateBitCast(NewLoaded, OrigTy);
}

bool AtomicExpandImpl::tryExpandAtomicRMW(AtomicRMWInst *AI) {
  LLVMContext &Ctx = AI->getModule()->getContext();
  TargetLowering::AtomicExpansionKind Kind = TLI->shouldExpandAtomicRMWInIR(AI);
  switch (Kind) {
  case TargetLoweringBase::AtomicExpansionKind::None:
    return false;
  case TargetLoweringBase::AtomicExpansionKind::LLSC: {
    unsigned MinCASSize = TLI->getMinCmpXchgSizeInBits() / 8;
    unsigned ValueSize = getAtomicOpSize(AI);
    if (ValueSize < MinCASSize) {
      expandPartwordAtomicRMW(AI,
                              TargetLoweringBase::AtomicExpansionKind::LLSC);
    } else {
      auto PerformOp = [&](IRBuilderBase &Builder, Value *Loaded) {
        return buildAtomicRMWValue(AI->getOperation(), Builder, Loaded,
                                   AI->getValOperand());
      };
      expandAtomicOpToLLSC(AI, AI->getType(), AI->getPointerOperand(),
                           AI->getAlign(), AI->getOrdering(), PerformOp);
    }
    return true;
  }
  case TargetLoweringBase::AtomicExpansionKind::CmpXChg: {
    unsigned MinCASSize = TLI->getMinCmpXchgSizeInBits() / 8;
    unsigned ValueSize = getAtomicOpSize(AI);
    if (ValueSize < MinCASSize) {
      expandPartwordAtomicRMW(AI,
                              TargetLoweringBase::AtomicExpansionKind::CmpXChg);
    } else {
      SmallVector<StringRef> SSNs;
      Ctx.getSyncScopeNames(SSNs);
      auto MemScope = SSNs[AI->getSyncScopeID()].empty()
                          ? "system"
                          : SSNs[AI->getSyncScopeID()];
      OptimizationRemarkEmitter ORE(AI->getFunction());
      ORE.emit([&]() {
        return OptimizationRemark(DEBUG_TYPE, "Passed", AI)
               << "A compare and swap loop was generated for an atomic "
               << AI->getOperationName(AI->getOperation()) << " operation at "
               << MemScope << " memory scope";
      });
      expandAtomicRMWToCmpXchg(AI, createCmpXchgInstFun);
    }
    return true;
  }
  case TargetLoweringBase::AtomicExpansionKind::MaskedIntrinsic: {
    unsigned MinCASSize = TLI->getMinCmpXchgSizeInBits() / 8;
    unsigned ValueSize = getAtomicOpSize(AI);
    if (ValueSize < MinCASSize) {
      AtomicRMWInst::BinOp Op = AI->getOperation();
      // Widen And/Or/Xor and give the target another chance at expanding it.
      if (Op == AtomicRMWInst::Or || Op == AtomicRMWInst::Xor ||
          Op == AtomicRMWInst::And) {
        tryExpandAtomicRMW(widenPartwordAtomicRMW(AI));
        return true;
      }
    }
    expandAtomicRMWToMaskedIntrinsic(AI);
    return true;
  }
  case TargetLoweringBase::AtomicExpansionKind::BitTestIntrinsic: {
    TLI->emitBitTestAtomicRMWIntrinsic(AI);
    return true;
  }
  case TargetLoweringBase::AtomicExpansionKind::CmpArithIntrinsic: {
    TLI->emitCmpArithAtomicRMWIntrinsic(AI);
    return true;
  }
  case TargetLoweringBase::AtomicExpansionKind::NotAtomic:
    return lowerAtomicRMWInst(AI);
  case TargetLoweringBase::AtomicExpansionKind::Expand:
    TLI->emitExpandAtomicRMW(AI);
    return true;
  default:
    llvm_unreachable("Unhandled case in tryExpandAtomicRMW");
  }
}

namespace {

struct PartwordMaskValues {
  // These three fields are guaranteed to be set by createMaskInstrs.
  Type *WordType = nullptr;
  Type *ValueType = nullptr;
  Type *IntValueType = nullptr;
  Value *AlignedAddr = nullptr;
  Align AlignedAddrAlignment;
  // The remaining fields can be null.
  Value *ShiftAmt = nullptr;
  Value *Mask = nullptr;
  Value *Inv_Mask = nullptr;
};

LLVM_ATTRIBUTE_UNUSED
raw_ostream &operator<<(raw_ostream &O, const PartwordMaskValues &PMV) {
  auto PrintObj = [&O](auto *V) {
    if (V)
      O << *V;
    else
      O << "nullptr";
    O << '\n';
  };
  O << "PartwordMaskValues {\n";
  O << "  WordType: ";
  PrintObj(PMV.WordType);
  O << "  ValueType: ";
  PrintObj(PMV.ValueType);
  O << "  AlignedAddr: ";
  PrintObj(PMV.AlignedAddr);
  O << "  AlignedAddrAlignment: " << PMV.AlignedAddrAlignment.value() << '\n';
  O << "  ShiftAmt: ";
  PrintObj(PMV.ShiftAmt);
  O << "  Mask: ";
  PrintObj(PMV.Mask);
  O << "  Inv_Mask: ";
  PrintObj(PMV.Inv_Mask);
  O << "}\n";
  return O;
}

} // end anonymous namespace

/// This is a helper function which builds instructions to provide
/// values necessary for partword atomic operations. It takes an
/// incoming address, Addr, and ValueType, and constructs the address,
/// shift-amounts and masks needed to work with a larger value of size
/// WordSize.
///
/// AlignedAddr: Addr rounded down to a multiple of WordSize
///
/// ShiftAmt: Number of bits to right-shift a WordSize value loaded
///           from AlignAddr for it to have the same value as if
///           ValueType was loaded from Addr.
///
/// Mask: Value to mask with the value loaded from AlignAddr to
///       include only the part that would've been loaded from Addr.
///
/// Inv_Mask: The inverse of Mask.
static PartwordMaskValues createMaskInstrs(IRBuilderBase &Builder,
                                           Instruction *I, Type *ValueType,
                                           Value *Addr, Align AddrAlign,
                                           unsigned MinWordSize) {
  PartwordMaskValues PMV;

  Module *M = I->getModule();
  LLVMContext &Ctx = M->getContext();
  const DataLayout &DL = M->getDataLayout();
  unsigned ValueSize = DL.getTypeStoreSize(ValueType);

  PMV.ValueType = PMV.IntValueType = ValueType;
  if (PMV.ValueType->isFloatingPointTy() || PMV.ValueType->isVectorTy())
    PMV.IntValueType =
        Type::getIntNTy(Ctx, ValueType->getPrimitiveSizeInBits());

  PMV.WordType = MinWordSize > ValueSize ? Type::getIntNTy(Ctx, MinWordSize * 8)
                                         : ValueType;
  if (PMV.ValueType == PMV.WordType) {
    PMV.AlignedAddr = Addr;
    PMV.AlignedAddrAlignment = AddrAlign;
    PMV.ShiftAmt = ConstantInt::get(PMV.ValueType, 0);
    PMV.Mask = ConstantInt::get(PMV.ValueType, ~0, /*isSigned*/ true);
    return PMV;
  }

  PMV.AlignedAddrAlignment = Align(MinWordSize);

  assert(ValueSize < MinWordSize);

  PointerType *PtrTy = cast<PointerType>(Addr->getType());
  IntegerType *IntTy = DL.getIndexType(Ctx, PtrTy->getAddressSpace());
  Value *PtrLSB;

  if (AddrAlign < MinWordSize) {
    PMV.AlignedAddr = Builder.CreateIntrinsic(
        Intrinsic::ptrmask, {PtrTy, IntTy},
        {Addr, ConstantInt::get(IntTy, ~(uint64_t)(MinWordSize - 1))}, nullptr,
        "AlignedAddr");

    Value *AddrInt = Builder.CreatePtrToInt(Addr, IntTy);
    PtrLSB = Builder.CreateAnd(AddrInt, MinWordSize - 1, "PtrLSB");
  } else {
    // If the alignment is high enough, the LSB are known 0.
    PMV.AlignedAddr = Addr;
    PtrLSB = ConstantInt::getNullValue(IntTy);
  }

  if (DL.isLittleEndian()) {
    // turn bytes into bits
    PMV.ShiftAmt = Builder.CreateShl(PtrLSB, 3);
  } else {
    // turn bytes into bits, and count from the other side.
    PMV.ShiftAmt = Builder.CreateShl(
        Builder.CreateXor(PtrLSB, MinWordSize - ValueSize), 3);
  }

  PMV.ShiftAmt = Builder.CreateTrunc(PMV.ShiftAmt, PMV.WordType, "ShiftAmt");
  PMV.Mask = Builder.CreateShl(
      ConstantInt::get(PMV.WordType, (1 << (ValueSize * 8)) - 1), PMV.ShiftAmt,
      "Mask");

  PMV.Inv_Mask = Builder.CreateNot(PMV.Mask, "Inv_Mask");

  return PMV;
}

static Value *extractMaskedValue(IRBuilderBase &Builder, Value *WideWord,
                                 const PartwordMaskValues &PMV) {
  assert(WideWord->getType() == PMV.WordType && "Widened type mismatch");
  if (PMV.WordType == PMV.ValueType)
    return WideWord;

  Value *Shift = Builder.CreateLShr(WideWord, PMV.ShiftAmt, "shifted");
  Value *Trunc = Builder.CreateTrunc(Shift, PMV.IntValueType, "extracted");
  return Builder.CreateBitCast(Trunc, PMV.ValueType);
}

static Value *insertMaskedValue(IRBuilderBase &Builder, Value *WideWord,
                                Value *Updated, const PartwordMaskValues &PMV) {
  assert(WideWord->getType() == PMV.WordType && "Widened type mismatch");
  assert(Updated->getType() == PMV.ValueType && "Value type mismatch");
  if (PMV.WordType == PMV.ValueType)
    return Updated;

  Updated = Builder.CreateBitCast(Updated, PMV.IntValueType);

  Value *ZExt = Builder.CreateZExt(Updated, PMV.WordType, "extended");
  Value *Shift =
      Builder.CreateShl(ZExt, PMV.ShiftAmt, "shifted", /*HasNUW*/ true);
  Value *And = Builder.CreateAnd(WideWord, PMV.Inv_Mask, "unmasked");
  Value *Or = Builder.CreateOr(And, Shift, "inserted");
  return Or;
}

/// Emit IR to implement a masked version of a given atomicrmw
/// operation. (That is, only the bits under the Mask should be
/// affected by the operation)
static Value *performMaskedAtomicOp(AtomicRMWInst::BinOp Op,
                                    IRBuilderBase &Builder, Value *Loaded,
                                    Value *Shifted_Inc, Value *Inc,
                                    const PartwordMaskValues &PMV) {
  // TODO: update to use
  // https://graphics.stanford.edu/~seander/bithacks.html#MaskedMerge in order
  // to merge bits from two values without requiring PMV.Inv_Mask.
  switch (Op) {
  case AtomicRMWInst::Xchg: {
    Value *Loaded_MaskOut = Builder.CreateAnd(Loaded, PMV.Inv_Mask);
    Value *FinalVal = Builder.CreateOr(Loaded_MaskOut, Shifted_Inc);
    return FinalVal;
  }
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
  case AtomicRMWInst::And:
    llvm_unreachable("Or/Xor/And handled by widenPartwordAtomicRMW");
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::Nand: {
    // The other arithmetic ops need to be masked into place.
    Value *NewVal = buildAtomicRMWValue(Op, Builder, Loaded, Shifted_Inc);
    Value *NewVal_Masked = Builder.CreateAnd(NewVal, PMV.Mask);
    Value *Loaded_MaskOut = Builder.CreateAnd(Loaded, PMV.Inv_Mask);
    Value *FinalVal = Builder.CreateOr(Loaded_MaskOut, NewVal_Masked);
    return FinalVal;
  }
  case AtomicRMWInst::Max:
  case AtomicRMWInst::Min:
  case AtomicRMWInst::UMax:
  case AtomicRMWInst::UMin:
  case AtomicRMWInst::FAdd:
  case AtomicRMWInst::FSub:
  case AtomicRMWInst::FMin:
  case AtomicRMWInst::FMax:
  case AtomicRMWInst::UIncWrap:
  case AtomicRMWInst::UDecWrap: {
    // Finally, other ops will operate on the full value, so truncate down to
    // the original size, and expand out again after doing the
    // operation. Bitcasts will be inserted for FP values.
    Value *Loaded_Extract = extractMaskedValue(Builder, Loaded, PMV);
    Value *NewVal = buildAtomicRMWValue(Op, Builder, Loaded_Extract, Inc);
    Value *FinalVal = insertMaskedValue(Builder, Loaded, NewVal, PMV);
    return FinalVal;
  }
  default:
    llvm_unreachable("Unknown atomic op");
  }
}

/// Expand a sub-word atomicrmw operation into an appropriate
/// word-sized operation.
///
/// It will create an LL/SC or cmpxchg loop, as appropriate, the same
/// way as a typical atomicrmw expansion. The only difference here is
/// that the operation inside of the loop may operate upon only a
/// part of the value.
void AtomicExpandImpl::expandPartwordAtomicRMW(
    AtomicRMWInst *AI, TargetLoweringBase::AtomicExpansionKind ExpansionKind) {
  // Widen And/Or/Xor and give the target another chance at expanding it.
  AtomicRMWInst::BinOp Op = AI->getOperation();
  if (Op == AtomicRMWInst::Or || Op == AtomicRMWInst::Xor ||
      Op == AtomicRMWInst::And) {
    tryExpandAtomicRMW(widenPartwordAtomicRMW(AI));
    return;
  }
  AtomicOrdering MemOpOrder = AI->getOrdering();
  SyncScope::ID SSID = AI->getSyncScopeID();

  ReplacementIRBuilder Builder(AI, *DL);

  PartwordMaskValues PMV =
      createMaskInstrs(Builder, AI, AI->getType(), AI->getPointerOperand(),
                       AI->getAlign(), TLI->getMinCmpXchgSizeInBits() / 8);

  Value *ValOperand_Shifted = nullptr;
  if (Op == AtomicRMWInst::Xchg || Op == AtomicRMWInst::Add ||
      Op == AtomicRMWInst::Sub || Op == AtomicRMWInst::Nand) {
    Value *ValOp = Builder.CreateBitCast(AI->getValOperand(), PMV.IntValueType);
    ValOperand_Shifted =
        Builder.CreateShl(Builder.CreateZExt(ValOp, PMV.WordType), PMV.ShiftAmt,
                          "ValOperand_Shifted");
  }

  auto PerformPartwordOp = [&](IRBuilderBase &Builder, Value *Loaded) {
    return performMaskedAtomicOp(Op, Builder, Loaded, ValOperand_Shifted,
                                 AI->getValOperand(), PMV);
  };

  Value *OldResult;
  if (ExpansionKind == TargetLoweringBase::AtomicExpansionKind::CmpXChg) {
    OldResult = insertRMWCmpXchgLoop(Builder, PMV.WordType, PMV.AlignedAddr,
                                     PMV.AlignedAddrAlignment, MemOpOrder, SSID,
                                     PerformPartwordOp, createCmpXchgInstFun);
  } else {
    assert(ExpansionKind == TargetLoweringBase::AtomicExpansionKind::LLSC);
    OldResult = insertRMWLLSCLoop(Builder, PMV.WordType, PMV.AlignedAddr,
                                  PMV.AlignedAddrAlignment, MemOpOrder,
                                  PerformPartwordOp);
  }

  Value *FinalOldResult = extractMaskedValue(Builder, OldResult, PMV);
  AI->replaceAllUsesWith(FinalOldResult);
  AI->eraseFromParent();
}

/// Copy metadata that's safe to preserve when widening atomics.
static void copyMetadataForAtomic(Instruction &Dest,
                                  const Instruction &Source) {
  SmallVector<std::pair<unsigned, MDNode *>, 8> MD;
  Source.getAllMetadata(MD);
  LLVMContext &Ctx = Dest.getContext();
  MDBuilder MDB(Ctx);

  for (auto [ID, N] : MD) {
    switch (ID) {
    case LLVMContext::MD_dbg:
    case LLVMContext::MD_tbaa:
    case LLVMContext::MD_tbaa_struct:
    case LLVMContext::MD_alias_scope:
    case LLVMContext::MD_noalias:
    case LLVMContext::MD_access_group:
    case LLVMContext::MD_mmra:
      Dest.setMetadata(ID, N);
      break;
    default:
      if (ID == Ctx.getMDKindID("amdgpu.no.remote.memory"))
        Dest.setMetadata(ID, N);
      else if (ID == Ctx.getMDKindID("amdgpu.no.fine.grained.memory"))
        Dest.setMetadata(ID, N);

      break;
    }
  }
}

// Widen the bitwise atomicrmw (or/xor/and) to the minimum supported width.
AtomicRMWInst *AtomicExpandImpl::widenPartwordAtomicRMW(AtomicRMWInst *AI) {
  ReplacementIRBuilder Builder(AI, *DL);
  AtomicRMWInst::BinOp Op = AI->getOperation();

  assert((Op == AtomicRMWInst::Or || Op == AtomicRMWInst::Xor ||
          Op == AtomicRMWInst::And) &&
         "Unable to widen operation");

  PartwordMaskValues PMV =
      createMaskInstrs(Builder, AI, AI->getType(), AI->getPointerOperand(),
                       AI->getAlign(), TLI->getMinCmpXchgSizeInBits() / 8);

  Value *ValOperand_Shifted =
      Builder.CreateShl(Builder.CreateZExt(AI->getValOperand(), PMV.WordType),
                        PMV.ShiftAmt, "ValOperand_Shifted");

  Value *NewOperand;

  if (Op == AtomicRMWInst::And)
    NewOperand =
        Builder.CreateOr(ValOperand_Shifted, PMV.Inv_Mask, "AndOperand");
  else
    NewOperand = ValOperand_Shifted;

  AtomicRMWInst *NewAI = Builder.CreateAtomicRMW(
      Op, PMV.AlignedAddr, NewOperand, PMV.AlignedAddrAlignment,
      AI->getOrdering(), AI->getSyncScopeID());

  copyMetadataForAtomic(*NewAI, *AI);

  Value *FinalOldResult = extractMaskedValue(Builder, NewAI, PMV);
  AI->replaceAllUsesWith(FinalOldResult);
  AI->eraseFromParent();
  return NewAI;
}

bool AtomicExpandImpl::expandPartwordCmpXchg(AtomicCmpXchgInst *CI) {
  // The basic idea here is that we're expanding a cmpxchg of a
  // smaller memory size up to a word-sized cmpxchg. To do this, we
  // need to add a retry-loop for strong cmpxchg, so that
  // modifications to other parts of the word don't cause a spurious
  // failure.

  // This generates code like the following:
  //     [[Setup mask values PMV.*]]
  //     %NewVal_Shifted = shl i32 %NewVal, %PMV.ShiftAmt
  //     %Cmp_Shifted = shl i32 %Cmp, %PMV.ShiftAmt
  //     %InitLoaded = load i32* %addr
  //     %InitLoaded_MaskOut = and i32 %InitLoaded, %PMV.Inv_Mask
  //     br partword.cmpxchg.loop
  // partword.cmpxchg.loop:
  //     %Loaded_MaskOut = phi i32 [ %InitLoaded_MaskOut, %entry ],
  //        [ %OldVal_MaskOut, %partword.cmpxchg.failure ]
  //     %FullWord_NewVal = or i32 %Loaded_MaskOut, %NewVal_Shifted
  //     %FullWord_Cmp = or i32 %Loaded_MaskOut, %Cmp_Shifted
  //     %NewCI = cmpxchg i32* %PMV.AlignedAddr, i32 %FullWord_Cmp,
  //        i32 %FullWord_NewVal success_ordering failure_ordering
  //     %OldVal = extractvalue { i32, i1 } %NewCI, 0
  //     %Success = extractvalue { i32, i1 } %NewCI, 1
  //     br i1 %Success, label %partword.cmpxchg.end,
  //        label %partword.cmpxchg.failure
  // partword.cmpxchg.failure:
  //     %OldVal_MaskOut = and i32 %OldVal, %PMV.Inv_Mask
  //     %ShouldContinue = icmp ne i32 %Loaded_MaskOut, %OldVal_MaskOut
  //     br i1 %ShouldContinue, label %partword.cmpxchg.loop,
  //         label %partword.cmpxchg.end
  // partword.cmpxchg.end:
  //    %tmp1 = lshr i32 %OldVal, %PMV.ShiftAmt
  //    %FinalOldVal = trunc i32 %tmp1 to i8
  //    %tmp2 = insertvalue { i8, i1 } undef, i8 %FinalOldVal, 0
  //    %Res = insertvalue { i8, i1 } %25, i1 %Success, 1

  Value *Addr = CI->getPointerOperand();
  Value *Cmp = CI->getCompareOperand();
  Value *NewVal = CI->getNewValOperand();

  BasicBlock *BB = CI->getParent();
  Function *F = BB->getParent();
  ReplacementIRBuilder Builder(CI, *DL);
  LLVMContext &Ctx = Builder.getContext();

  BasicBlock *EndBB =
      BB->splitBasicBlock(CI->getIterator(), "partword.cmpxchg.end");
  auto FailureBB =
      BasicBlock::Create(Ctx, "partword.cmpxchg.failure", F, EndBB);
  auto LoopBB = BasicBlock::Create(Ctx, "partword.cmpxchg.loop", F, FailureBB);

  // The split call above "helpfully" added a branch at the end of BB
  // (to the wrong place).
  std::prev(BB->end())->eraseFromParent();
  Builder.SetInsertPoint(BB);

  PartwordMaskValues PMV =
      createMaskInstrs(Builder, CI, CI->getCompareOperand()->getType(), Addr,
                       CI->getAlign(), TLI->getMinCmpXchgSizeInBits() / 8);

  // Shift the incoming values over, into the right location in the word.
  Value *NewVal_Shifted =
      Builder.CreateShl(Builder.CreateZExt(NewVal, PMV.WordType), PMV.ShiftAmt);
  Value *Cmp_Shifted =
      Builder.CreateShl(Builder.CreateZExt(Cmp, PMV.WordType), PMV.ShiftAmt);

  // Load the entire current word, and mask into place the expected and new
  // values
  LoadInst *InitLoaded = Builder.CreateLoad(PMV.WordType, PMV.AlignedAddr);
  InitLoaded->setVolatile(CI->isVolatile());
  Value *InitLoaded_MaskOut = Builder.CreateAnd(InitLoaded, PMV.Inv_Mask);
  Builder.CreateBr(LoopBB);

  // partword.cmpxchg.loop:
  Builder.SetInsertPoint(LoopBB);
  PHINode *Loaded_MaskOut = Builder.CreatePHI(PMV.WordType, 2);
  Loaded_MaskOut->addIncoming(InitLoaded_MaskOut, BB);

  // Mask/Or the expected and new values into place in the loaded word.
  Value *FullWord_NewVal = Builder.CreateOr(Loaded_MaskOut, NewVal_Shifted);
  Value *FullWord_Cmp = Builder.CreateOr(Loaded_MaskOut, Cmp_Shifted);
  AtomicCmpXchgInst *NewCI = Builder.CreateAtomicCmpXchg(
      PMV.AlignedAddr, FullWord_Cmp, FullWord_NewVal, PMV.AlignedAddrAlignment,
      CI->getSuccessOrdering(), CI->getFailureOrdering(), CI->getSyncScopeID());
  NewCI->setVolatile(CI->isVolatile());
  // When we're building a strong cmpxchg, we need a loop, so you
  // might think we could use a weak cmpxchg inside. But, using strong
  // allows the below comparison for ShouldContinue, and we're
  // expecting the underlying cmpxchg to be a machine instruction,
  // which is strong anyways.
  NewCI->setWeak(CI->isWeak());

  Value *OldVal = Builder.CreateExtractValue(NewCI, 0);
  Value *Success = Builder.CreateExtractValue(NewCI, 1);

  if (CI->isWeak())
    Builder.CreateBr(EndBB);
  else
    Builder.CreateCondBr(Success, EndBB, FailureBB);

  // partword.cmpxchg.failure:
  Builder.SetInsertPoint(FailureBB);
  // Upon failure, verify that the masked-out part of the loaded value
  // has been modified.  If it didn't, abort the cmpxchg, since the
  // masked-in part must've.
  Value *OldVal_MaskOut = Builder.CreateAnd(OldVal, PMV.Inv_Mask);
  Value *ShouldContinue = Builder.CreateICmpNE(Loaded_MaskOut, OldVal_MaskOut);
  Builder.CreateCondBr(ShouldContinue, LoopBB, EndBB);

  // Add the second value to the phi from above
  Loaded_MaskOut->addIncoming(OldVal_MaskOut, FailureBB);

  // partword.cmpxchg.end:
  Builder.SetInsertPoint(CI);

  Value *FinalOldVal = extractMaskedValue(Builder, OldVal, PMV);
  Value *Res = PoisonValue::get(CI->getType());
  Res = Builder.CreateInsertValue(Res, FinalOldVal, 0);
  Res = Builder.CreateInsertValue(Res, Success, 1);

  CI->replaceAllUsesWith(Res);
  CI->eraseFromParent();
  return true;
}

void AtomicExpandImpl::expandAtomicOpToLLSC(
    Instruction *I, Type *ResultType, Value *Addr, Align AddrAlign,
    AtomicOrdering MemOpOrder,
    function_ref<Value *(IRBuilderBase &, Value *)> PerformOp) {
  ReplacementIRBuilder Builder(I, *DL);
  Value *Loaded = insertRMWLLSCLoop(Builder, ResultType, Addr, AddrAlign,
                                    MemOpOrder, PerformOp);

  I->replaceAllUsesWith(Loaded);
  I->eraseFromParent();
}

void AtomicExpandImpl::expandAtomicRMWToMaskedIntrinsic(AtomicRMWInst *AI) {
  ReplacementIRBuilder Builder(AI, *DL);

  PartwordMaskValues PMV =
      createMaskInstrs(Builder, AI, AI->getType(), AI->getPointerOperand(),
                       AI->getAlign(), TLI->getMinCmpXchgSizeInBits() / 8);

  // The value operand must be sign-extended for signed min/max so that the
  // target's signed comparison instructions can be used. Otherwise, just
  // zero-ext.
  Instruction::CastOps CastOp = Instruction::ZExt;
  AtomicRMWInst::BinOp RMWOp = AI->getOperation();
  if (RMWOp == AtomicRMWInst::Max || RMWOp == AtomicRMWInst::Min)
    CastOp = Instruction::SExt;

  Value *ValOperand_Shifted = Builder.CreateShl(
      Builder.CreateCast(CastOp, AI->getValOperand(), PMV.WordType),
      PMV.ShiftAmt, "ValOperand_Shifted");
  Value *OldResult = TLI->emitMaskedAtomicRMWIntrinsic(
      Builder, AI, PMV.AlignedAddr, ValOperand_Shifted, PMV.Mask, PMV.ShiftAmt,
      AI->getOrdering());
  Value *FinalOldResult = extractMaskedValue(Builder, OldResult, PMV);
  AI->replaceAllUsesWith(FinalOldResult);
  AI->eraseFromParent();
}

void AtomicExpandImpl::expandAtomicCmpXchgToMaskedIntrinsic(
    AtomicCmpXchgInst *CI) {
  ReplacementIRBuilder Builder(CI, *DL);

  PartwordMaskValues PMV = createMaskInstrs(
      Builder, CI, CI->getCompareOperand()->getType(), CI->getPointerOperand(),
      CI->getAlign(), TLI->getMinCmpXchgSizeInBits() / 8);

  Value *CmpVal_Shifted = Builder.CreateShl(
      Builder.CreateZExt(CI->getCompareOperand(), PMV.WordType), PMV.ShiftAmt,
      "CmpVal_Shifted");
  Value *NewVal_Shifted = Builder.CreateShl(
      Builder.CreateZExt(CI->getNewValOperand(), PMV.WordType), PMV.ShiftAmt,
      "NewVal_Shifted");
  Value *OldVal = TLI->emitMaskedAtomicCmpXchgIntrinsic(
      Builder, CI, PMV.AlignedAddr, CmpVal_Shifted, NewVal_Shifted, PMV.Mask,
      CI->getMergedOrdering());
  Value *FinalOldVal = extractMaskedValue(Builder, OldVal, PMV);
  Value *Res = PoisonValue::get(CI->getType());
  Res = Builder.CreateInsertValue(Res, FinalOldVal, 0);
  Value *Success = Builder.CreateICmpEQ(
      CmpVal_Shifted, Builder.CreateAnd(OldVal, PMV.Mask), "Success");
  Res = Builder.CreateInsertValue(Res, Success, 1);

  CI->replaceAllUsesWith(Res);
  CI->eraseFromParent();
}

Value *AtomicExpandImpl::insertRMWLLSCLoop(
    IRBuilderBase &Builder, Type *ResultTy, Value *Addr, Align AddrAlign,
    AtomicOrdering MemOpOrder,
    function_ref<Value *(IRBuilderBase &, Value *)> PerformOp) {
  LLVMContext &Ctx = Builder.getContext();
  BasicBlock *BB = Builder.GetInsertBlock();
  Function *F = BB->getParent();

  assert(AddrAlign >=
             F->getDataLayout().getTypeStoreSize(ResultTy) &&
         "Expected at least natural alignment at this point.");

  // Given: atomicrmw some_op iN* %addr, iN %incr ordering
  //
  // The standard expansion we produce is:
  //     [...]
  // atomicrmw.start:
  //     %loaded = @load.linked(%addr)
  //     %new = some_op iN %loaded, %incr
  //     %stored = @store_conditional(%new, %addr)
  //     %try_again = icmp i32 ne %stored, 0
  //     br i1 %try_again, label %loop, label %atomicrmw.end
  // atomicrmw.end:
  //     [...]
  BasicBlock *ExitBB =
      BB->splitBasicBlock(Builder.GetInsertPoint(), "atomicrmw.end");
  BasicBlock *LoopBB = BasicBlock::Create(Ctx, "atomicrmw.start", F, ExitBB);

  // The split call above "helpfully" added a branch at the end of BB (to the
  // wrong place).
  std::prev(BB->end())->eraseFromParent();
  Builder.SetInsertPoint(BB);
  Builder.CreateBr(LoopBB);

  // Start the main loop block now that we've taken care of the preliminaries.
  Builder.SetInsertPoint(LoopBB);
  Value *Loaded = TLI->emitLoadLinked(Builder, ResultTy, Addr, MemOpOrder);

  Value *NewVal = PerformOp(Builder, Loaded);

  Value *StoreSuccess =
      TLI->emitStoreConditional(Builder, NewVal, Addr, MemOpOrder);
  Value *TryAgain = Builder.CreateICmpNE(
      StoreSuccess, ConstantInt::get(IntegerType::get(Ctx, 32), 0), "tryagain");
  Builder.CreateCondBr(TryAgain, LoopBB, ExitBB);

  Builder.SetInsertPoint(ExitBB, ExitBB->begin());
  return Loaded;
}

/// Convert an atomic cmpxchg of a non-integral type to an integer cmpxchg of
/// the equivalent bitwidth.  We used to not support pointer cmpxchg in the
/// IR.  As a migration step, we convert back to what use to be the standard
/// way to represent a pointer cmpxchg so that we can update backends one by
/// one.
AtomicCmpXchgInst *
AtomicExpandImpl::convertCmpXchgToIntegerType(AtomicCmpXchgInst *CI) {
  auto *M = CI->getModule();
  Type *NewTy = getCorrespondingIntegerType(CI->getCompareOperand()->getType(),
                                            M->getDataLayout());

  ReplacementIRBuilder Builder(CI, *DL);

  Value *Addr = CI->getPointerOperand();

  Value *NewCmp = Builder.CreatePtrToInt(CI->getCompareOperand(), NewTy);
  Value *NewNewVal = Builder.CreatePtrToInt(CI->getNewValOperand(), NewTy);

  auto *NewCI = Builder.CreateAtomicCmpXchg(
      Addr, NewCmp, NewNewVal, CI->getAlign(), CI->getSuccessOrdering(),
      CI->getFailureOrdering(), CI->getSyncScopeID());
  NewCI->setVolatile(CI->isVolatile());
  NewCI->setWeak(CI->isWeak());
  LLVM_DEBUG(dbgs() << "Replaced " << *CI << " with " << *NewCI << "\n");

  Value *OldVal = Builder.CreateExtractValue(NewCI, 0);
  Value *Succ = Builder.CreateExtractValue(NewCI, 1);

  OldVal = Builder.CreateIntToPtr(OldVal, CI->getCompareOperand()->getType());

  Value *Res = PoisonValue::get(CI->getType());
  Res = Builder.CreateInsertValue(Res, OldVal, 0);
  Res = Builder.CreateInsertValue(Res, Succ, 1);

  CI->replaceAllUsesWith(Res);
  CI->eraseFromParent();
  return NewCI;
}

bool AtomicExpandImpl::expandAtomicCmpXchg(AtomicCmpXchgInst *CI) {
  AtomicOrdering SuccessOrder = CI->getSuccessOrdering();
  AtomicOrdering FailureOrder = CI->getFailureOrdering();
  Value *Addr = CI->getPointerOperand();
  BasicBlock *BB = CI->getParent();
  Function *F = BB->getParent();
  LLVMContext &Ctx = F->getContext();
  // If shouldInsertFencesForAtomic() returns true, then the target does not
  // want to deal with memory orders, and emitLeading/TrailingFence should take
  // care of everything. Otherwise, emitLeading/TrailingFence are no-op and we
  // should preserve the ordering.
  bool ShouldInsertFencesForAtomic = TLI->shouldInsertFencesForAtomic(CI);
  AtomicOrdering MemOpOrder = ShouldInsertFencesForAtomic
                                  ? AtomicOrdering::Monotonic
                                  : CI->getMergedOrdering();

  // In implementations which use a barrier to achieve release semantics, we can
  // delay emitting this barrier until we know a store is actually going to be
  // attempted. The cost of this delay is that we need 2 copies of the block
  // emitting the load-linked, affecting code size.
  //
  // Ideally, this logic would be unconditional except for the minsize check
  // since in other cases the extra blocks naturally collapse down to the
  // minimal loop. Unfortunately, this puts too much stress on later
  // optimisations so we avoid emitting the extra logic in those cases too.
  bool HasReleasedLoadBB = !CI->isWeak() && ShouldInsertFencesForAtomic &&
                           SuccessOrder != AtomicOrdering::Monotonic &&
                           SuccessOrder != AtomicOrdering::Acquire &&
                           !F->hasMinSize();

  // There's no overhead for sinking the release barrier in a weak cmpxchg, so
  // do it even on minsize.
  bool UseUnconditionalReleaseBarrier = F->hasMinSize() && !CI->isWeak();

  // Given: cmpxchg some_op iN* %addr, iN %desired, iN %new success_ord fail_ord
  //
  // The full expansion we produce is:
  //     [...]
  // %aligned.addr = ...
  // cmpxchg.start:
  //     %unreleasedload = @load.linked(%aligned.addr)
  //     %unreleasedload.extract = extract value from %unreleasedload
  //     %should_store = icmp eq %unreleasedload.extract, %desired
  //     br i1 %should_store, label %cmpxchg.releasingstore,
  //                          label %cmpxchg.nostore
  // cmpxchg.releasingstore:
  //     fence?
  //     br label cmpxchg.trystore
  // cmpxchg.trystore:
  //     %loaded.trystore = phi [%unreleasedload, %cmpxchg.releasingstore],
  //                            [%releasedload, %cmpxchg.releasedload]
  //     %updated.new = insert %new into %loaded.trystore
  //     %stored = @store_conditional(%updated.new, %aligned.addr)
  //     %success = icmp eq i32 %stored, 0
  //     br i1 %success, label %cmpxchg.success,
  //                     label %cmpxchg.releasedload/%cmpxchg.failure
  // cmpxchg.releasedload:
  //     %releasedload = @load.linked(%aligned.addr)
  //     %releasedload.extract = extract value from %releasedload
  //     %should_store = icmp eq %releasedload.extract, %desired
  //     br i1 %should_store, label %cmpxchg.trystore,
  //                          label %cmpxchg.failure
  // cmpxchg.success:
  //     fence?
  //     br label %cmpxchg.end
  // cmpxchg.nostore:
  //     %loaded.nostore = phi [%unreleasedload, %cmpxchg.start],
  //                           [%releasedload,
  //                               %cmpxchg.releasedload/%cmpxchg.trystore]
  //     @load_linked_fail_balance()?
  //     br label %cmpxchg.failure
  // cmpxchg.failure:
  //     fence?
  //     br label %cmpxchg.end
  // cmpxchg.end:
  //     %loaded.exit = phi [%loaded.nostore, %cmpxchg.failure],
  //                        [%loaded.trystore, %cmpxchg.trystore]
  //     %success = phi i1 [true, %cmpxchg.success], [false, %cmpxchg.failure]
  //     %loaded = extract value from %loaded.exit
  //     %restmp = insertvalue { iN, i1 } undef, iN %loaded, 0
  //     %res = insertvalue { iN, i1 } %restmp, i1 %success, 1
  //     [...]
  BasicBlock *ExitBB = BB->splitBasicBlock(CI->getIterator(), "cmpxchg.end");
  auto FailureBB = BasicBlock::Create(Ctx, "cmpxchg.failure", F, ExitBB);
  auto NoStoreBB = BasicBlock::Create(Ctx, "cmpxchg.nostore", F, FailureBB);
  auto SuccessBB = BasicBlock::Create(Ctx, "cmpxchg.success", F, NoStoreBB);
  auto ReleasedLoadBB =
      BasicBlock::Create(Ctx, "cmpxchg.releasedload", F, SuccessBB);
  auto TryStoreBB =
      BasicBlock::Create(Ctx, "cmpxchg.trystore", F, ReleasedLoadBB);
  auto ReleasingStoreBB =
      BasicBlock::Create(Ctx, "cmpxchg.fencedstore", F, TryStoreBB);
  auto StartBB = BasicBlock::Create(Ctx, "cmpxchg.start", F, ReleasingStoreBB);

  ReplacementIRBuilder Builder(CI, *DL);

  // The split call above "helpfully" added a branch at the end of BB (to the
  // wrong place), but we might want a fence too. It's easiest to just remove
  // the branch entirely.
  std::prev(BB->end())->eraseFromParent();
  Builder.SetInsertPoint(BB);
  if (ShouldInsertFencesForAtomic && UseUnconditionalReleaseBarrier)
    TLI->emitLeadingFence(Builder, CI, SuccessOrder);

  PartwordMaskValues PMV =
      createMaskInstrs(Builder, CI, CI->getCompareOperand()->getType(), Addr,
                       CI->getAlign(), TLI->getMinCmpXchgSizeInBits() / 8);
  Builder.CreateBr(StartBB);

  // Start the main loop block now that we've taken care of the preliminaries.
  Builder.SetInsertPoint(StartBB);
  Value *UnreleasedLoad =
      TLI->emitLoadLinked(Builder, PMV.WordType, PMV.AlignedAddr, MemOpOrder);
  Value *UnreleasedLoadExtract =
      extractMaskedValue(Builder, UnreleasedLoad, PMV);
  Value *ShouldStore = Builder.CreateICmpEQ(
      UnreleasedLoadExtract, CI->getCompareOperand(), "should_store");

  // If the cmpxchg doesn't actually need any ordering when it fails, we can
  // jump straight past that fence instruction (if it exists).
  Builder.CreateCondBr(ShouldStore, ReleasingStoreBB, NoStoreBB);

  Builder.SetInsertPoint(ReleasingStoreBB);
  if (ShouldInsertFencesForAtomic && !UseUnconditionalReleaseBarrier)
    TLI->emitLeadingFence(Builder, CI, SuccessOrder);
  Builder.CreateBr(TryStoreBB);

  Builder.SetInsertPoint(TryStoreBB);
  PHINode *LoadedTryStore =
      Builder.CreatePHI(PMV.WordType, 2, "loaded.trystore");
  LoadedTryStore->addIncoming(UnreleasedLoad, ReleasingStoreBB);
  Value *NewValueInsert =
      insertMaskedValue(Builder, LoadedTryStore, CI->getNewValOperand(), PMV);
  Value *StoreSuccess = TLI->emitStoreConditional(Builder, NewValueInsert,
                                                  PMV.AlignedAddr, MemOpOrder);
  StoreSuccess = Builder.CreateICmpEQ(
      StoreSuccess, ConstantInt::get(Type::getInt32Ty(Ctx), 0), "success");
  BasicBlock *RetryBB = HasReleasedLoadBB ? ReleasedLoadBB : StartBB;
  Builder.CreateCondBr(StoreSuccess, SuccessBB,
                       CI->isWeak() ? FailureBB : RetryBB);

  Builder.SetInsertPoint(ReleasedLoadBB);
  Value *SecondLoad;
  if (HasReleasedLoadBB) {
    SecondLoad =
        TLI->emitLoadLinked(Builder, PMV.WordType, PMV.AlignedAddr, MemOpOrder);
    Value *SecondLoadExtract = extractMaskedValue(Builder, SecondLoad, PMV);
    ShouldStore = Builder.CreateICmpEQ(SecondLoadExtract,
                                       CI->getCompareOperand(), "should_store");

    // If the cmpxchg doesn't actually need any ordering when it fails, we can
    // jump straight past that fence instruction (if it exists).
    Builder.CreateCondBr(ShouldStore, TryStoreBB, NoStoreBB);
    // Update PHI node in TryStoreBB.
    LoadedTryStore->addIncoming(SecondLoad, ReleasedLoadBB);
  } else
    Builder.CreateUnreachable();

  // Make sure later instructions don't get reordered with a fence if
  // necessary.
  Builder.SetInsertPoint(SuccessBB);
  if (ShouldInsertFencesForAtomic ||
      TLI->shouldInsertTrailingFenceForAtomicStore(CI))
    TLI->emitTrailingFence(Builder, CI, SuccessOrder);
  Builder.CreateBr(ExitBB);

  Builder.SetInsertPoint(NoStoreBB);
  PHINode *LoadedNoStore =
      Builder.CreatePHI(UnreleasedLoad->getType(), 2, "loaded.nostore");
  LoadedNoStore->addIncoming(UnreleasedLoad, StartBB);
  if (HasReleasedLoadBB)
    LoadedNoStore->addIncoming(SecondLoad, ReleasedLoadBB);

  // In the failing case, where we don't execute the store-conditional, the
  // target might want to balance out the load-linked with a dedicated
  // instruction (e.g., on ARM, clearing the exclusive monitor).
  TLI->emitAtomicCmpXchgNoStoreLLBalance(Builder);
  Builder.CreateBr(FailureBB);

  Builder.SetInsertPoint(FailureBB);
  PHINode *LoadedFailure =
      Builder.CreatePHI(UnreleasedLoad->getType(), 2, "loaded.failure");
  LoadedFailure->addIncoming(LoadedNoStore, NoStoreBB);
  if (CI->isWeak())
    LoadedFailure->addIncoming(LoadedTryStore, TryStoreBB);
  if (ShouldInsertFencesForAtomic)
    TLI->emitTrailingFence(Builder, CI, FailureOrder);
  Builder.CreateBr(ExitBB);

  // Finally, we have control-flow based knowledge of whether the cmpxchg
  // succeeded or not. We expose this to later passes by converting any
  // subsequent "icmp eq/ne %loaded, %oldval" into a use of an appropriate
  // PHI.
  Builder.SetInsertPoint(ExitBB, ExitBB->begin());
  PHINode *LoadedExit =
      Builder.CreatePHI(UnreleasedLoad->getType(), 2, "loaded.exit");
  LoadedExit->addIncoming(LoadedTryStore, SuccessBB);
  LoadedExit->addIncoming(LoadedFailure, FailureBB);
  PHINode *Success = Builder.CreatePHI(Type::getInt1Ty(Ctx), 2, "success");
  Success->addIncoming(ConstantInt::getTrue(Ctx), SuccessBB);
  Success->addIncoming(ConstantInt::getFalse(Ctx), FailureBB);

  // This is the "exit value" from the cmpxchg expansion. It may be of
  // a type wider than the one in the cmpxchg instruction.
  Value *LoadedFull = LoadedExit;

  Builder.SetInsertPoint(ExitBB, std::next(Success->getIterator()));
  Value *Loaded = extractMaskedValue(Builder, LoadedFull, PMV);

  // Look for any users of the cmpxchg that are just comparing the loaded value
  // against the desired one, and replace them with the CFG-derived version.
  SmallVector<ExtractValueInst *, 2> PrunedInsts;
  for (auto *User : CI->users()) {
    ExtractValueInst *EV = dyn_cast<ExtractValueInst>(User);
    if (!EV)
      continue;

    assert(EV->getNumIndices() == 1 && EV->getIndices()[0] <= 1 &&
           "weird extraction from { iN, i1 }");

    if (EV->getIndices()[0] == 0)
      EV->replaceAllUsesWith(Loaded);
    else
      EV->replaceAllUsesWith(Success);

    PrunedInsts.push_back(EV);
  }

  // We can remove the instructions now we're no longer iterating through them.
  for (auto *EV : PrunedInsts)
    EV->eraseFromParent();

  if (!CI->use_empty()) {
    // Some use of the full struct return that we don't understand has happened,
    // so we've got to reconstruct it properly.
    Value *Res;
    Res = Builder.CreateInsertValue(PoisonValue::get(CI->getType()), Loaded, 0);
    Res = Builder.CreateInsertValue(Res, Success, 1);

    CI->replaceAllUsesWith(Res);
  }

  CI->eraseFromParent();
  return true;
}

bool AtomicExpandImpl::isIdempotentRMW(AtomicRMWInst *RMWI) {
  auto C = dyn_cast<ConstantInt>(RMWI->getValOperand());
  if (!C)
    return false;

  AtomicRMWInst::BinOp Op = RMWI->getOperation();
  switch (Op) {
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
    return C->isZero();
  case AtomicRMWInst::And:
    return C->isMinusOne();
  // FIXME: we could also treat Min/Max/UMin/UMax by the INT_MIN/INT_MAX/...
  default:
    return false;
  }
}

bool AtomicExpandImpl::simplifyIdempotentRMW(AtomicRMWInst *RMWI) {
  if (auto ResultingLoad = TLI->lowerIdempotentRMWIntoFencedLoad(RMWI)) {
    tryExpandAtomicLoad(ResultingLoad);
    return true;
  }
  return false;
}

Value *AtomicExpandImpl::insertRMWCmpXchgLoop(
    IRBuilderBase &Builder, Type *ResultTy, Value *Addr, Align AddrAlign,
    AtomicOrdering MemOpOrder, SyncScope::ID SSID,
    function_ref<Value *(IRBuilderBase &, Value *)> PerformOp,
    CreateCmpXchgInstFun CreateCmpXchg) {
  LLVMContext &Ctx = Builder.getContext();
  BasicBlock *BB = Builder.GetInsertBlock();
  Function *F = BB->getParent();

  // Given: atomicrmw some_op iN* %addr, iN %incr ordering
  //
  // The standard expansion we produce is:
  //     [...]
  //     %init_loaded = load atomic iN* %addr
  //     br label %loop
  // loop:
  //     %loaded = phi iN [ %init_loaded, %entry ], [ %new_loaded, %loop ]
  //     %new = some_op iN %loaded, %incr
  //     %pair = cmpxchg iN* %addr, iN %loaded, iN %new
  //     %new_loaded = extractvalue { iN, i1 } %pair, 0
  //     %success = extractvalue { iN, i1 } %pair, 1
  //     br i1 %success, label %atomicrmw.end, label %loop
  // atomicrmw.end:
  //     [...]
  BasicBlock *ExitBB =
      BB->splitBasicBlock(Builder.GetInsertPoint(), "atomicrmw.end");
  BasicBlock *LoopBB = BasicBlock::Create(Ctx, "atomicrmw.start", F, ExitBB);

  // The split call above "helpfully" added a branch at the end of BB (to the
  // wrong place), but we want a load. It's easiest to just remove
  // the branch entirely.
  std::prev(BB->end())->eraseFromParent();
  Builder.SetInsertPoint(BB);
  LoadInst *InitLoaded = Builder.CreateAlignedLoad(ResultTy, Addr, AddrAlign);
  Builder.CreateBr(LoopBB);

  // Start the main loop block now that we've taken care of the preliminaries.
  Builder.SetInsertPoint(LoopBB);
  PHINode *Loaded = Builder.CreatePHI(ResultTy, 2, "loaded");
  Loaded->addIncoming(InitLoaded, BB);

  Value *NewVal = PerformOp(Builder, Loaded);

  Value *NewLoaded = nullptr;
  Value *Success = nullptr;

  CreateCmpXchg(Builder, Addr, Loaded, NewVal, AddrAlign,
                MemOpOrder == AtomicOrdering::Unordered
                    ? AtomicOrdering::Monotonic
                    : MemOpOrder,
                SSID, Success, NewLoaded);
  assert(Success && NewLoaded);

  Loaded->addIncoming(NewLoaded, LoopBB);

  Builder.CreateCondBr(Success, ExitBB, LoopBB);

  Builder.SetInsertPoint(ExitBB, ExitBB->begin());
  return NewLoaded;
}

bool AtomicExpandImpl::tryExpandAtomicCmpXchg(AtomicCmpXchgInst *CI) {
  unsigned MinCASSize = TLI->getMinCmpXchgSizeInBits() / 8;
  unsigned ValueSize = getAtomicOpSize(CI);

  switch (TLI->shouldExpandAtomicCmpXchgInIR(CI)) {
  default:
    llvm_unreachable("Unhandled case in tryExpandAtomicCmpXchg");
  case TargetLoweringBase::AtomicExpansionKind::None:
    if (ValueSize < MinCASSize)
      return expandPartwordCmpXchg(CI);
    return false;
  case TargetLoweringBase::AtomicExpansionKind::LLSC: {
    return expandAtomicCmpXchg(CI);
  }
  case TargetLoweringBase::AtomicExpansionKind::MaskedIntrinsic:
    expandAtomicCmpXchgToMaskedIntrinsic(CI);
    return true;
  case TargetLoweringBase::AtomicExpansionKind::NotAtomic:
    return lowerAtomicCmpXchgInst(CI);
  }
}

// Note: This function is exposed externally by AtomicExpandUtils.h
bool llvm::expandAtomicRMWToCmpXchg(AtomicRMWInst *AI,
                                    CreateCmpXchgInstFun CreateCmpXchg) {
  ReplacementIRBuilder Builder(AI, AI->getDataLayout());
  Builder.setIsFPConstrained(
      AI->getFunction()->hasFnAttribute(Attribute::StrictFP));

  // FIXME: If FP exceptions are observable, we should force them off for the
  // loop for the FP atomics.
  Value *Loaded = AtomicExpandImpl::insertRMWCmpXchgLoop(
      Builder, AI->getType(), AI->getPointerOperand(), AI->getAlign(),
      AI->getOrdering(), AI->getSyncScopeID(),
      [&](IRBuilderBase &Builder, Value *Loaded) {
        return buildAtomicRMWValue(AI->getOperation(), Builder, Loaded,
                                   AI->getValOperand());
      },
      CreateCmpXchg);

  AI->replaceAllUsesWith(Loaded);
  AI->eraseFromParent();
  return true;
}

// In order to use one of the sized library calls such as
// __atomic_fetch_add_4, the alignment must be sufficient, the size
// must be one of the potentially-specialized sizes, and the value
// type must actually exist in C on the target (otherwise, the
// function wouldn't actually be defined.)
static bool canUseSizedAtomicCall(unsigned Size, Align Alignment,
                                  const DataLayout &DL) {
  // TODO: "LargestSize" is an approximation for "largest type that
  // you can express in C". It seems to be the case that int128 is
  // supported on all 64-bit platforms, otherwise only up to 64-bit
  // integers are supported. If we get this wrong, then we'll try to
  // call a sized libcall that doesn't actually exist. There should
  // really be some more reliable way in LLVM of determining integer
  // sizes which are valid in the target's C ABI...
  unsigned LargestSize = DL.getLargestLegalIntTypeSizeInBits() >= 64 ? 16 : 8;
  return Alignment >= Size &&
         (Size == 1 || Size == 2 || Size == 4 || Size == 8 || Size == 16) &&
         Size <= LargestSize;
}

void AtomicExpandImpl::expandAtomicLoadToLibcall(LoadInst *I) {
  static const RTLIB::Libcall Libcalls[6] = {
      RTLIB::ATOMIC_LOAD,   RTLIB::ATOMIC_LOAD_1, RTLIB::ATOMIC_LOAD_2,
      RTLIB::ATOMIC_LOAD_4, RTLIB::ATOMIC_LOAD_8, RTLIB::ATOMIC_LOAD_16};
  unsigned Size = getAtomicOpSize(I);

  bool expanded = expandAtomicOpToLibcall(
      I, Size, I->getAlign(), I->getPointerOperand(), nullptr, nullptr,
      I->getOrdering(), AtomicOrdering::NotAtomic, Libcalls);
  if (!expanded)
    report_fatal_error("expandAtomicOpToLibcall shouldn't fail for Load");
}

void AtomicExpandImpl::expandAtomicStoreToLibcall(StoreInst *I) {
  static const RTLIB::Libcall Libcalls[6] = {
      RTLIB::ATOMIC_STORE,   RTLIB::ATOMIC_STORE_1, RTLIB::ATOMIC_STORE_2,
      RTLIB::ATOMIC_STORE_4, RTLIB::ATOMIC_STORE_8, RTLIB::ATOMIC_STORE_16};
  unsigned Size = getAtomicOpSize(I);

  bool expanded = expandAtomicOpToLibcall(
      I, Size, I->getAlign(), I->getPointerOperand(), I->getValueOperand(),
      nullptr, I->getOrdering(), AtomicOrdering::NotAtomic, Libcalls);
  if (!expanded)
    report_fatal_error("expandAtomicOpToLibcall shouldn't fail for Store");
}

void AtomicExpandImpl::expandAtomicCASToLibcall(AtomicCmpXchgInst *I) {
  static const RTLIB::Libcall Libcalls[6] = {
      RTLIB::ATOMIC_COMPARE_EXCHANGE,   RTLIB::ATOMIC_COMPARE_EXCHANGE_1,
      RTLIB::ATOMIC_COMPARE_EXCHANGE_2, RTLIB::ATOMIC_COMPARE_EXCHANGE_4,
      RTLIB::ATOMIC_COMPARE_EXCHANGE_8, RTLIB::ATOMIC_COMPARE_EXCHANGE_16};
  unsigned Size = getAtomicOpSize(I);

  bool expanded = expandAtomicOpToLibcall(
      I, Size, I->getAlign(), I->getPointerOperand(), I->getNewValOperand(),
      I->getCompareOperand(), I->getSuccessOrdering(), I->getFailureOrdering(),
      Libcalls);
  if (!expanded)
    report_fatal_error("expandAtomicOpToLibcall shouldn't fail for CAS");
}

static ArrayRef<RTLIB::Libcall> GetRMWLibcall(AtomicRMWInst::BinOp Op) {
  static const RTLIB::Libcall LibcallsXchg[6] = {
      RTLIB::ATOMIC_EXCHANGE,   RTLIB::ATOMIC_EXCHANGE_1,
      RTLIB::ATOMIC_EXCHANGE_2, RTLIB::ATOMIC_EXCHANGE_4,
      RTLIB::ATOMIC_EXCHANGE_8, RTLIB::ATOMIC_EXCHANGE_16};
  static const RTLIB::Libcall LibcallsAdd[6] = {
      RTLIB::UNKNOWN_LIBCALL,    RTLIB::ATOMIC_FETCH_ADD_1,
      RTLIB::ATOMIC_FETCH_ADD_2, RTLIB::ATOMIC_FETCH_ADD_4,
      RTLIB::ATOMIC_FETCH_ADD_8, RTLIB::ATOMIC_FETCH_ADD_16};
  static const RTLIB::Libcall LibcallsSub[6] = {
      RTLIB::UNKNOWN_LIBCALL,    RTLIB::ATOMIC_FETCH_SUB_1,
      RTLIB::ATOMIC_FETCH_SUB_2, RTLIB::ATOMIC_FETCH_SUB_4,
      RTLIB::ATOMIC_FETCH_SUB_8, RTLIB::ATOMIC_FETCH_SUB_16};
  static const RTLIB::Libcall LibcallsAnd[6] = {
      RTLIB::UNKNOWN_LIBCALL,    RTLIB::ATOMIC_FETCH_AND_1,
      RTLIB::ATOMIC_FETCH_AND_2, RTLIB::ATOMIC_FETCH_AND_4,
      RTLIB::ATOMIC_FETCH_AND_8, RTLIB::ATOMIC_FETCH_AND_16};
  static const RTLIB::Libcall LibcallsOr[6] = {
      RTLIB::UNKNOWN_LIBCALL,   RTLIB::ATOMIC_FETCH_OR_1,
      RTLIB::ATOMIC_FETCH_OR_2, RTLIB::ATOMIC_FETCH_OR_4,
      RTLIB::ATOMIC_FETCH_OR_8, RTLIB::ATOMIC_FETCH_OR_16};
  static const RTLIB::Libcall LibcallsXor[6] = {
      RTLIB::UNKNOWN_LIBCALL,    RTLIB::ATOMIC_FETCH_XOR_1,
      RTLIB::ATOMIC_FETCH_XOR_2, RTLIB::ATOMIC_FETCH_XOR_4,
      RTLIB::ATOMIC_FETCH_XOR_8, RTLIB::ATOMIC_FETCH_XOR_16};
  static const RTLIB::Libcall LibcallsNand[6] = {
      RTLIB::UNKNOWN_LIBCALL,     RTLIB::ATOMIC_FETCH_NAND_1,
      RTLIB::ATOMIC_FETCH_NAND_2, RTLIB::ATOMIC_FETCH_NAND_4,
      RTLIB::ATOMIC_FETCH_NAND_8, RTLIB::ATOMIC_FETCH_NAND_16};

  switch (Op) {
  case AtomicRMWInst::BAD_BINOP:
    llvm_unreachable("Should not have BAD_BINOP.");
  case AtomicRMWInst::Xchg:
    return ArrayRef(LibcallsXchg);
  case AtomicRMWInst::Add:
    return ArrayRef(LibcallsAdd);
  case AtomicRMWInst::Sub:
    return ArrayRef(LibcallsSub);
  case AtomicRMWInst::And:
    return ArrayRef(LibcallsAnd);
  case AtomicRMWInst::Or:
    return ArrayRef(LibcallsOr);
  case AtomicRMWInst::Xor:
    return ArrayRef(LibcallsXor);
  case AtomicRMWInst::Nand:
    return ArrayRef(LibcallsNand);
  case AtomicRMWInst::Max:
  case AtomicRMWInst::Min:
  case AtomicRMWInst::UMax:
  case AtomicRMWInst::UMin:
  case AtomicRMWInst::FMax:
  case AtomicRMWInst::FMin:
  case AtomicRMWInst::FAdd:
  case AtomicRMWInst::FSub:
  case AtomicRMWInst::UIncWrap:
  case AtomicRMWInst::UDecWrap:
    // No atomic libcalls are available for max/min/umax/umin.
    return {};
  }
  llvm_unreachable("Unexpected AtomicRMW operation.");
}

void AtomicExpandImpl::expandAtomicRMWToLibcall(AtomicRMWInst *I) {
  ArrayRef<RTLIB::Libcall> Libcalls = GetRMWLibcall(I->getOperation());

  unsigned Size = getAtomicOpSize(I);

  bool Success = false;
  if (!Libcalls.empty())
    Success = expandAtomicOpToLibcall(
        I, Size, I->getAlign(), I->getPointerOperand(), I->getValOperand(),
        nullptr, I->getOrdering(), AtomicOrdering::NotAtomic, Libcalls);

  // The expansion failed: either there were no libcalls at all for
  // the operation (min/max), or there were only size-specialized
  // libcalls (add/sub/etc) and we needed a generic. So, expand to a
  // CAS libcall, via a CAS loop, instead.
  if (!Success) {
    expandAtomicRMWToCmpXchg(
        I, [this](IRBuilderBase &Builder, Value *Addr, Value *Loaded,
                  Value *NewVal, Align Alignment, AtomicOrdering MemOpOrder,
                  SyncScope::ID SSID, Value *&Success, Value *&NewLoaded) {
          // Create the CAS instruction normally...
          AtomicCmpXchgInst *Pair = Builder.CreateAtomicCmpXchg(
              Addr, Loaded, NewVal, Alignment, MemOpOrder,
              AtomicCmpXchgInst::getStrongestFailureOrdering(MemOpOrder), SSID);
          Success = Builder.CreateExtractValue(Pair, 1, "success");
          NewLoaded = Builder.CreateExtractValue(Pair, 0, "newloaded");

          // ...and then expand the CAS into a libcall.
          expandAtomicCASToLibcall(Pair);
        });
  }
}

// A helper routine for the above expandAtomic*ToLibcall functions.
//
// 'Libcalls' contains an array of enum values for the particular
// ATOMIC libcalls to be emitted. All of the other arguments besides
// 'I' are extracted from the Instruction subclass by the
// caller. Depending on the particular call, some will be null.
bool AtomicExpandImpl::expandAtomicOpToLibcall(
    Instruction *I, unsigned Size, Align Alignment, Value *PointerOperand,
    Value *ValueOperand, Value *CASExpected, AtomicOrdering Ordering,
    AtomicOrdering Ordering2, ArrayRef<RTLIB::Libcall> Libcalls) {
  assert(Libcalls.size() == 6);

  LLVMContext &Ctx = I->getContext();
  Module *M = I->getModule();
  const DataLayout &DL = M->getDataLayout();
  IRBuilder<> Builder(I);
  IRBuilder<> AllocaBuilder(&I->getFunction()->getEntryBlock().front());

  bool UseSizedLibcall = canUseSizedAtomicCall(Size, Alignment, DL);
  Type *SizedIntTy = Type::getIntNTy(Ctx, Size * 8);

  const Align AllocaAlignment = DL.getPrefTypeAlign(SizedIntTy);

  // TODO: the "order" argument type is "int", not int32. So
  // getInt32Ty may be wrong if the arch uses e.g. 16-bit ints.
  ConstantInt *SizeVal64 = ConstantInt::get(Type::getInt64Ty(Ctx), Size);
  assert(Ordering != AtomicOrdering::NotAtomic && "expect atomic MO");
  Constant *OrderingVal =
      ConstantInt::get(Type::getInt32Ty(Ctx), (int)toCABI(Ordering));
  Constant *Ordering2Val = nullptr;
  if (CASExpected) {
    assert(Ordering2 != AtomicOrdering::NotAtomic && "expect atomic MO");
    Ordering2Val =
        ConstantInt::get(Type::getInt32Ty(Ctx), (int)toCABI(Ordering2));
  }
  bool HasResult = I->getType() != Type::getVoidTy(Ctx);

  RTLIB::Libcall RTLibType;
  if (UseSizedLibcall) {
    switch (Size) {
    case 1:
      RTLibType = Libcalls[1];
      break;
    case 2:
      RTLibType = Libcalls[2];
      break;
    case 4:
      RTLibType = Libcalls[3];
      break;
    case 8:
      RTLibType = Libcalls[4];
      break;
    case 16:
      RTLibType = Libcalls[5];
      break;
    }
  } else if (Libcalls[0] != RTLIB::UNKNOWN_LIBCALL) {
    RTLibType = Libcalls[0];
  } else {
    // Can't use sized function, and there's no generic for this
    // operation, so give up.
    return false;
  }

  if (!TLI->getLibcallName(RTLibType)) {
    // This target does not implement the requested atomic libcall so give up.
    return false;
  }

  // Build up the function call. There's two kinds. First, the sized
  // variants.  These calls are going to be one of the following (with
  // N=1,2,4,8,16):
  //  iN    __atomic_load_N(iN *ptr, int ordering)
  //  void  __atomic_store_N(iN *ptr, iN val, int ordering)
  //  iN    __atomic_{exchange|fetch_*}_N(iN *ptr, iN val, int ordering)
  //  bool  __atomic_compare_exchange_N(iN *ptr, iN *expected, iN desired,
  //                                    int success_order, int failure_order)
  //
  // Note that these functions can be used for non-integer atomic
  // operations, the values just need to be bitcast to integers on the
  // way in and out.
  //
  // And, then, the generic variants. They look like the following:
  //  void  __atomic_load(size_t size, void *ptr, void *ret, int ordering)
  //  void  __atomic_store(size_t size, void *ptr, void *val, int ordering)
  //  void  __atomic_exchange(size_t size, void *ptr, void *val, void *ret,
  //                          int ordering)
  //  bool  __atomic_compare_exchange(size_t size, void *ptr, void *expected,
  //                                  void *desired, int success_order,
  //                                  int failure_order)
  //
  // The different signatures are built up depending on the
  // 'UseSizedLibcall', 'CASExpected', 'ValueOperand', and 'HasResult'
  // variables.

  AllocaInst *AllocaCASExpected = nullptr;
  AllocaInst *AllocaValue = nullptr;
  AllocaInst *AllocaResult = nullptr;

  Type *ResultTy;
  SmallVector<Value *, 6> Args;
  AttributeList Attr;

  // 'size' argument.
  if (!UseSizedLibcall) {
    // Note, getIntPtrType is assumed equivalent to size_t.
    Args.push_back(ConstantInt::get(DL.getIntPtrType(Ctx), Size));
  }

  // 'ptr' argument.
  // note: This assumes all address spaces share a common libfunc
  // implementation and that addresses are convertable.  For systems without
  // that property, we'd need to extend this mechanism to support AS-specific
  // families of atomic intrinsics.
  Value *PtrVal = PointerOperand;
  PtrVal = Builder.CreateAddrSpaceCast(PtrVal, PointerType::getUnqual(Ctx));
  Args.push_back(PtrVal);

  // 'expected' argument, if present.
  if (CASExpected) {
    AllocaCASExpected = AllocaBuilder.CreateAlloca(CASExpected->getType());
    AllocaCASExpected->setAlignment(AllocaAlignment);
    Builder.CreateLifetimeStart(AllocaCASExpected, SizeVal64);
    Builder.CreateAlignedStore(CASExpected, AllocaCASExpected, AllocaAlignment);
    Args.push_back(AllocaCASExpected);
  }

  // 'val' argument ('desired' for cas), if present.
  if (ValueOperand) {
    if (UseSizedLibcall) {
      Value *IntValue =
          Builder.CreateBitOrPointerCast(ValueOperand, SizedIntTy);
      Args.push_back(IntValue);
    } else {
      AllocaValue = AllocaBuilder.CreateAlloca(ValueOperand->getType());
      AllocaValue->setAlignment(AllocaAlignment);
      Builder.CreateLifetimeStart(AllocaValue, SizeVal64);
      Builder.CreateAlignedStore(ValueOperand, AllocaValue, AllocaAlignment);
      Args.push_back(AllocaValue);
    }
  }

  // 'ret' argument.
  if (!CASExpected && HasResult && !UseSizedLibcall) {
    AllocaResult = AllocaBuilder.CreateAlloca(I->getType());
    AllocaResult->setAlignment(AllocaAlignment);
    Builder.CreateLifetimeStart(AllocaResult, SizeVal64);
    Args.push_back(AllocaResult);
  }

  // 'ordering' ('success_order' for cas) argument.
  Args.push_back(OrderingVal);

  // 'failure_order' argument, if present.
  if (Ordering2Val)
    Args.push_back(Ordering2Val);

  // Now, the return type.
  if (CASExpected) {
    ResultTy = Type::getInt1Ty(Ctx);
    Attr = Attr.addRetAttribute(Ctx, Attribute::ZExt);
  } else if (HasResult && UseSizedLibcall)
    ResultTy = SizedIntTy;
  else
    ResultTy = Type::getVoidTy(Ctx);

  // Done with setting up arguments and return types, create the call:
  SmallVector<Type *, 6> ArgTys;
  for (Value *Arg : Args)
    ArgTys.push_back(Arg->getType());
  FunctionType *FnType = FunctionType::get(ResultTy, ArgTys, false);
  FunctionCallee LibcallFn =
      M->getOrInsertFunction(TLI->getLibcallName(RTLibType), FnType, Attr);
  CallInst *Call = Builder.CreateCall(LibcallFn, Args);
  Call->setAttributes(Attr);
  Value *Result = Call;

  // And then, extract the results...
  if (ValueOperand && !UseSizedLibcall)
    Builder.CreateLifetimeEnd(AllocaValue, SizeVal64);

  if (CASExpected) {
    // The final result from the CAS is {load of 'expected' alloca, bool result
    // from call}
    Type *FinalResultTy = I->getType();
    Value *V = PoisonValue::get(FinalResultTy);
    Value *ExpectedOut = Builder.CreateAlignedLoad(
        CASExpected->getType(), AllocaCASExpected, AllocaAlignment);
    Builder.CreateLifetimeEnd(AllocaCASExpected, SizeVal64);
    V = Builder.CreateInsertValue(V, ExpectedOut, 0);
    V = Builder.CreateInsertValue(V, Result, 1);
    I->replaceAllUsesWith(V);
  } else if (HasResult) {
    Value *V;
    if (UseSizedLibcall)
      V = Builder.CreateBitOrPointerCast(Result, I->getType());
    else {
      V = Builder.CreateAlignedLoad(I->getType(), AllocaResult,
                                    AllocaAlignment);
      Builder.CreateLifetimeEnd(AllocaResult, SizeVal64);
    }
    I->replaceAllUsesWith(V);
  }
  I->eraseFromParent();
  return true;
}
