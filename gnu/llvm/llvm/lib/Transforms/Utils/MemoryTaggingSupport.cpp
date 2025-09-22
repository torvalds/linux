//== MemoryTaggingSupport.cpp - helpers for memory tagging implementations ===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares common infrastructure for HWAddressSanitizer and
// Aarch64StackTagging.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/MemoryTaggingSupport.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

namespace llvm {
namespace memtag {
namespace {
bool maybeReachableFromEachOther(const SmallVectorImpl<IntrinsicInst *> &Insts,
                                 const DominatorTree *DT, const LoopInfo *LI,
                                 size_t MaxLifetimes) {
  // If we have too many lifetime ends, give up, as the algorithm below is N^2.
  if (Insts.size() > MaxLifetimes)
    return true;
  for (size_t I = 0; I < Insts.size(); ++I) {
    for (size_t J = 0; J < Insts.size(); ++J) {
      if (I == J)
        continue;
      if (isPotentiallyReachable(Insts[I], Insts[J], nullptr, DT, LI))
        return true;
    }
  }
  return false;
}
} // namespace

bool forAllReachableExits(const DominatorTree &DT, const PostDominatorTree &PDT,
                          const LoopInfo &LI, const Instruction *Start,
                          const SmallVectorImpl<IntrinsicInst *> &Ends,
                          const SmallVectorImpl<Instruction *> &RetVec,
                          llvm::function_ref<void(Instruction *)> Callback) {
  if (Ends.size() == 1 && PDT.dominates(Ends[0], Start)) {
    Callback(Ends[0]);
    return true;
  }
  SmallPtrSet<BasicBlock *, 2> EndBlocks;
  for (auto *End : Ends) {
    EndBlocks.insert(End->getParent());
  }
  SmallVector<Instruction *, 8> ReachableRetVec;
  unsigned NumCoveredExits = 0;
  for (auto *RI : RetVec) {
    if (!isPotentiallyReachable(Start, RI, nullptr, &DT, &LI))
      continue;
    ReachableRetVec.push_back(RI);
    // If there is an end in the same basic block as the return, we know for
    // sure that the return is covered. Otherwise, we can check whether there
    // is a way to reach the RI from the start of the lifetime without passing
    // through an end.
    if (EndBlocks.contains(RI->getParent()) ||
        !isPotentiallyReachable(Start, RI, &EndBlocks, &DT, &LI)) {
      ++NumCoveredExits;
    }
  }
  if (NumCoveredExits == ReachableRetVec.size()) {
    for_each(Ends, Callback);
  } else {
    // If there's a mix of covered and non-covered exits, just put the untag
    // on exits, so we avoid the redundancy of untagging twice.
    for_each(ReachableRetVec, Callback);
    // We may have inserted untag outside of the lifetime interval.
    // Signal the caller to remove the lifetime end call for this alloca.
    return false;
  }
  return true;
}

bool isStandardLifetime(const SmallVectorImpl<IntrinsicInst *> &LifetimeStart,
                        const SmallVectorImpl<IntrinsicInst *> &LifetimeEnd,
                        const DominatorTree *DT, const LoopInfo *LI,
                        size_t MaxLifetimes) {
  // An alloca that has exactly one start and end in every possible execution.
  // If it has multiple ends, they have to be unreachable from each other, so
  // at most one of them is actually used for each execution of the function.
  return LifetimeStart.size() == 1 &&
         (LifetimeEnd.size() == 1 ||
          (LifetimeEnd.size() > 0 &&
           !maybeReachableFromEachOther(LifetimeEnd, DT, LI, MaxLifetimes)));
}

Instruction *getUntagLocationIfFunctionExit(Instruction &Inst) {
  if (isa<ReturnInst>(Inst)) {
    if (CallInst *CI = Inst.getParent()->getTerminatingMustTailCall())
      return CI;
    return &Inst;
  }
  if (isa<ResumeInst, CleanupReturnInst>(Inst)) {
    return &Inst;
  }
  return nullptr;
}

void StackInfoBuilder::visit(Instruction &Inst) {
  // Visit non-intrinsic debug-info records attached to Inst.
  for (DbgVariableRecord &DVR : filterDbgVars(Inst.getDbgRecordRange())) {
    auto AddIfInteresting = [&](Value *V) {
      if (auto *AI = dyn_cast_or_null<AllocaInst>(V)) {
        if (!isInterestingAlloca(*AI))
          return;
        AllocaInfo &AInfo = Info.AllocasToInstrument[AI];
        auto &DVRVec = AInfo.DbgVariableRecords;
        if (DVRVec.empty() || DVRVec.back() != &DVR)
          DVRVec.push_back(&DVR);
      }
    };

    for_each(DVR.location_ops(), AddIfInteresting);
    if (DVR.isDbgAssign())
      AddIfInteresting(DVR.getAddress());
  }

  if (CallInst *CI = dyn_cast<CallInst>(&Inst)) {
    if (CI->canReturnTwice()) {
      Info.CallsReturnTwice = true;
    }
  }
  if (AllocaInst *AI = dyn_cast<AllocaInst>(&Inst)) {
    if (isInterestingAlloca(*AI)) {
      Info.AllocasToInstrument[AI].AI = AI;
    }
    return;
  }
  auto *II = dyn_cast<IntrinsicInst>(&Inst);
  if (II && (II->getIntrinsicID() == Intrinsic::lifetime_start ||
             II->getIntrinsicID() == Intrinsic::lifetime_end)) {
    AllocaInst *AI = findAllocaForValue(II->getArgOperand(1));
    if (!AI) {
      Info.UnrecognizedLifetimes.push_back(&Inst);
      return;
    }
    if (!isInterestingAlloca(*AI))
      return;
    if (II->getIntrinsicID() == Intrinsic::lifetime_start)
      Info.AllocasToInstrument[AI].LifetimeStart.push_back(II);
    else
      Info.AllocasToInstrument[AI].LifetimeEnd.push_back(II);
    return;
  }
  if (auto *DVI = dyn_cast<DbgVariableIntrinsic>(&Inst)) {
    auto AddIfInteresting = [&](Value *V) {
      if (auto *AI = dyn_cast_or_null<AllocaInst>(V)) {
        if (!isInterestingAlloca(*AI))
          return;
        AllocaInfo &AInfo = Info.AllocasToInstrument[AI];
        auto &DVIVec = AInfo.DbgVariableIntrinsics;
        if (DVIVec.empty() || DVIVec.back() != DVI)
          DVIVec.push_back(DVI);
      }
    };
    for_each(DVI->location_ops(), AddIfInteresting);
    if (auto *DAI = dyn_cast<DbgAssignIntrinsic>(DVI))
      AddIfInteresting(DAI->getAddress());
  }

  Instruction *ExitUntag = getUntagLocationIfFunctionExit(Inst);
  if (ExitUntag)
    Info.RetVec.push_back(ExitUntag);
}

bool StackInfoBuilder::isInterestingAlloca(const AllocaInst &AI) {
  return (AI.getAllocatedType()->isSized() &&
          // FIXME: support vscale.
          !AI.getAllocatedType()->isScalableTy() &&
          // FIXME: instrument dynamic allocas, too
          AI.isStaticAlloca() &&
          // alloca() may be called with 0 size, ignore it.
          memtag::getAllocaSizeInBytes(AI) > 0 &&
          // We are only interested in allocas not promotable to registers.
          // Promotable allocas are common under -O0.
          !isAllocaPromotable(&AI) &&
          // inalloca allocas are not treated as static, and we don't want
          // dynamic alloca instrumentation for them as well.
          !AI.isUsedWithInAlloca() &&
          // swifterror allocas are register promoted by ISel
          !AI.isSwiftError()) &&
         // safe allocas are not interesting
         !(SSI && SSI->isSafe(AI));
}

uint64_t getAllocaSizeInBytes(const AllocaInst &AI) {
  auto DL = AI.getDataLayout();
  return *AI.getAllocationSize(DL);
}

void alignAndPadAlloca(memtag::AllocaInfo &Info, llvm::Align Alignment) {
  const Align NewAlignment = std::max(Info.AI->getAlign(), Alignment);
  Info.AI->setAlignment(NewAlignment);
  auto &Ctx = Info.AI->getFunction()->getContext();

  uint64_t Size = getAllocaSizeInBytes(*Info.AI);
  uint64_t AlignedSize = alignTo(Size, Alignment);
  if (Size == AlignedSize)
    return;

  // Add padding to the alloca.
  Type *AllocatedType =
      Info.AI->isArrayAllocation()
          ? ArrayType::get(
                Info.AI->getAllocatedType(),
                cast<ConstantInt>(Info.AI->getArraySize())->getZExtValue())
          : Info.AI->getAllocatedType();
  Type *PaddingType = ArrayType::get(Type::getInt8Ty(Ctx), AlignedSize - Size);
  Type *TypeWithPadding = StructType::get(AllocatedType, PaddingType);
  auto *NewAI = new AllocaInst(TypeWithPadding, Info.AI->getAddressSpace(),
                               nullptr, "", Info.AI->getIterator());
  NewAI->takeName(Info.AI);
  NewAI->setAlignment(Info.AI->getAlign());
  NewAI->setUsedWithInAlloca(Info.AI->isUsedWithInAlloca());
  NewAI->setSwiftError(Info.AI->isSwiftError());
  NewAI->copyMetadata(*Info.AI);

  Value *NewPtr = NewAI;

  // TODO: Remove when typed pointers dropped
  if (Info.AI->getType() != NewAI->getType())
    NewPtr = new BitCastInst(NewAI, Info.AI->getType(), "", Info.AI->getIterator());

  Info.AI->replaceAllUsesWith(NewPtr);
  Info.AI->eraseFromParent();
  Info.AI = NewAI;
}

bool isLifetimeIntrinsic(Value *V) {
  auto *II = dyn_cast<IntrinsicInst>(V);
  return II && II->isLifetimeStartOrEnd();
}

Value *readRegister(IRBuilder<> &IRB, StringRef Name) {
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  Function *ReadRegister = Intrinsic::getDeclaration(
      M, Intrinsic::read_register, IRB.getIntPtrTy(M->getDataLayout()));
  MDNode *MD =
      MDNode::get(M->getContext(), {MDString::get(M->getContext(), Name)});
  Value *Args[] = {MetadataAsValue::get(M->getContext(), MD)};
  return IRB.CreateCall(ReadRegister, Args);
}

Value *getPC(const Triple &TargetTriple, IRBuilder<> &IRB) {
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  if (TargetTriple.getArch() == Triple::aarch64)
    return memtag::readRegister(IRB, "pc");
  return IRB.CreatePtrToInt(IRB.GetInsertBlock()->getParent(),
                            IRB.getIntPtrTy(M->getDataLayout()));
}

Value *getFP(IRBuilder<> &IRB) {
  Function *F = IRB.GetInsertBlock()->getParent();
  Module *M = F->getParent();
  auto *GetStackPointerFn = Intrinsic::getDeclaration(
      M, Intrinsic::frameaddress,
      IRB.getPtrTy(M->getDataLayout().getAllocaAddrSpace()));
  return IRB.CreatePtrToInt(
      IRB.CreateCall(GetStackPointerFn,
                     {Constant::getNullValue(IRB.getInt32Ty())}),
      IRB.getIntPtrTy(M->getDataLayout()));
}

Value *getAndroidSlotPtr(IRBuilder<> &IRB, int Slot) {
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  // Android provides a fixed TLS slot for sanitizers. See TLS_SLOT_SANITIZER
  // in Bionic's libc/private/bionic_tls.h.
  Function *ThreadPointerFunc =
      Intrinsic::getDeclaration(M, Intrinsic::thread_pointer);
  return IRB.CreateConstGEP1_32(IRB.getInt8Ty(),
                                IRB.CreateCall(ThreadPointerFunc), 8 * Slot);
}

static DbgAssignIntrinsic *DynCastToDbgAssign(DbgVariableIntrinsic *DVI) {
  return dyn_cast<DbgAssignIntrinsic>(DVI);
}

static DbgVariableRecord *DynCastToDbgAssign(DbgVariableRecord *DVR) {
  return DVR->isDbgAssign() ? DVR : nullptr;
}

void annotateDebugRecords(AllocaInfo &Info, unsigned int Tag) {
  // Helper utility for adding DW_OP_LLVM_tag_offset to debug-info records,
  // abstracted over whether they're intrinsic-stored or DbgVariableRecord
  // stored.
  auto AnnotateDbgRecord = [&](auto *DPtr) {
    // Prepend "tag_offset, N" to the dwarf expression.
    // Tag offset logically applies to the alloca pointer, and it makes sense
    // to put it at the beginning of the expression.
    SmallVector<uint64_t, 8> NewOps = {dwarf::DW_OP_LLVM_tag_offset, Tag};
    for (size_t LocNo = 0; LocNo < DPtr->getNumVariableLocationOps(); ++LocNo)
      if (DPtr->getVariableLocationOp(LocNo) == Info.AI)
        DPtr->setExpression(
            DIExpression::appendOpsToArg(DPtr->getExpression(), NewOps, LocNo));
    if (auto *DAI = DynCastToDbgAssign(DPtr)) {
      if (DAI->getAddress() == Info.AI)
        DAI->setAddressExpression(
            DIExpression::prependOpcodes(DAI->getAddressExpression(), NewOps));
    }
  };

  llvm::for_each(Info.DbgVariableIntrinsics, AnnotateDbgRecord);
  llvm::for_each(Info.DbgVariableRecords, AnnotateDbgRecord);
}

} // namespace memtag
} // namespace llvm
