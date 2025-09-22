//===- TypeMetadataUtils.cpp - Utilities related to type metadata ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains functions that make it easier to manipulate type metadata
// for devirtualization.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"

using namespace llvm;

// Search for virtual calls that call FPtr and add them to DevirtCalls.
static void
findCallsAtConstantOffset(SmallVectorImpl<DevirtCallSite> &DevirtCalls,
                          bool *HasNonCallUses, Value *FPtr, uint64_t Offset,
                          const CallInst *CI, DominatorTree &DT) {
  for (const Use &U : FPtr->uses()) {
    Instruction *User = cast<Instruction>(U.getUser());
    // Ignore this instruction if it is not dominated by the type intrinsic
    // being analyzed. Otherwise we may transform a call sharing the same
    // vtable pointer incorrectly. Specifically, this situation can arise
    // after indirect call promotion and inlining, where we may have uses
    // of the vtable pointer guarded by a function pointer check, and a fallback
    // indirect call.
    if (!DT.dominates(CI, User))
      continue;
    if (isa<BitCastInst>(User)) {
      findCallsAtConstantOffset(DevirtCalls, HasNonCallUses, User, Offset, CI,
                                DT);
    } else if (auto *CI = dyn_cast<CallInst>(User)) {
      DevirtCalls.push_back({Offset, *CI});
    } else if (auto *II = dyn_cast<InvokeInst>(User)) {
      DevirtCalls.push_back({Offset, *II});
    } else if (HasNonCallUses) {
      *HasNonCallUses = true;
    }
  }
}

// Search for virtual calls that load from VPtr and add them to DevirtCalls.
static void findLoadCallsAtConstantOffset(
    const Module *M, SmallVectorImpl<DevirtCallSite> &DevirtCalls, Value *VPtr,
    int64_t Offset, const CallInst *CI, DominatorTree &DT) {
  for (const Use &U : VPtr->uses()) {
    Value *User = U.getUser();
    if (isa<BitCastInst>(User)) {
      findLoadCallsAtConstantOffset(M, DevirtCalls, User, Offset, CI, DT);
    } else if (isa<LoadInst>(User)) {
      findCallsAtConstantOffset(DevirtCalls, nullptr, User, Offset, CI, DT);
    } else if (auto GEP = dyn_cast<GetElementPtrInst>(User)) {
      // Take into account the GEP offset.
      if (VPtr == GEP->getPointerOperand() && GEP->hasAllConstantIndices()) {
        SmallVector<Value *, 8> Indices(drop_begin(GEP->operands()));
        int64_t GEPOffset = M->getDataLayout().getIndexedOffsetInType(
            GEP->getSourceElementType(), Indices);
        findLoadCallsAtConstantOffset(M, DevirtCalls, User, Offset + GEPOffset,
                                      CI, DT);
      }
    } else if (auto *Call = dyn_cast<CallInst>(User)) {
      if (Call->getIntrinsicID() == llvm::Intrinsic::load_relative) {
        if (auto *LoadOffset = dyn_cast<ConstantInt>(Call->getOperand(1))) {
          findCallsAtConstantOffset(DevirtCalls, nullptr, User,
                                    Offset + LoadOffset->getSExtValue(), CI,
                                    DT);
        }
      }
    }
  }
}

void llvm::findDevirtualizableCallsForTypeTest(
    SmallVectorImpl<DevirtCallSite> &DevirtCalls,
    SmallVectorImpl<CallInst *> &Assumes, const CallInst *CI,
    DominatorTree &DT) {
  assert(CI->getCalledFunction()->getIntrinsicID() == Intrinsic::type_test ||
         CI->getCalledFunction()->getIntrinsicID() ==
             Intrinsic::public_type_test);

  const Module *M = CI->getParent()->getParent()->getParent();

  // Find llvm.assume intrinsics for this llvm.type.test call.
  for (const Use &CIU : CI->uses())
    if (auto *Assume = dyn_cast<AssumeInst>(CIU.getUser()))
      Assumes.push_back(Assume);

  // If we found any, search for virtual calls based on %p and add them to
  // DevirtCalls.
  if (!Assumes.empty())
    findLoadCallsAtConstantOffset(
        M, DevirtCalls, CI->getArgOperand(0)->stripPointerCasts(), 0, CI, DT);
}

void llvm::findDevirtualizableCallsForTypeCheckedLoad(
    SmallVectorImpl<DevirtCallSite> &DevirtCalls,
    SmallVectorImpl<Instruction *> &LoadedPtrs,
    SmallVectorImpl<Instruction *> &Preds, bool &HasNonCallUses,
    const CallInst *CI, DominatorTree &DT) {
  assert(CI->getCalledFunction()->getIntrinsicID() ==
             Intrinsic::type_checked_load ||
         CI->getCalledFunction()->getIntrinsicID() ==
             Intrinsic::type_checked_load_relative);

  auto *Offset = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  if (!Offset) {
    HasNonCallUses = true;
    return;
  }

  for (const Use &U : CI->uses()) {
    auto CIU = U.getUser();
    if (auto EVI = dyn_cast<ExtractValueInst>(CIU)) {
      if (EVI->getNumIndices() == 1 && EVI->getIndices()[0] == 0) {
        LoadedPtrs.push_back(EVI);
        continue;
      }
      if (EVI->getNumIndices() == 1 && EVI->getIndices()[0] == 1) {
        Preds.push_back(EVI);
        continue;
      }
    }
    HasNonCallUses = true;
  }

  for (Value *LoadedPtr : LoadedPtrs)
    findCallsAtConstantOffset(DevirtCalls, &HasNonCallUses, LoadedPtr,
                              Offset->getZExtValue(), CI, DT);
}

Constant *llvm::getPointerAtOffset(Constant *I, uint64_t Offset, Module &M,
                                   Constant *TopLevelGlobal) {
  // TODO: Ideally it would be the caller who knows if it's appropriate to strip
  // the DSOLocalEquicalent. More generally, it would feel more appropriate to
  // have two functions that handle absolute and relative pointers separately.
  if (auto *Equiv = dyn_cast<DSOLocalEquivalent>(I))
    I = Equiv->getGlobalValue();

  if (I->getType()->isPointerTy()) {
    if (Offset == 0)
      return I;
    return nullptr;
  }

  const DataLayout &DL = M.getDataLayout();

  if (auto *C = dyn_cast<ConstantStruct>(I)) {
    const StructLayout *SL = DL.getStructLayout(C->getType());
    if (Offset >= SL->getSizeInBytes())
      return nullptr;

    unsigned Op = SL->getElementContainingOffset(Offset);
    return getPointerAtOffset(cast<Constant>(I->getOperand(Op)),
                              Offset - SL->getElementOffset(Op), M,
                              TopLevelGlobal);
  }
  if (auto *C = dyn_cast<ConstantArray>(I)) {
    ArrayType *VTableTy = C->getType();
    uint64_t ElemSize = DL.getTypeAllocSize(VTableTy->getElementType());

    unsigned Op = Offset / ElemSize;
    if (Op >= C->getNumOperands())
      return nullptr;

    return getPointerAtOffset(cast<Constant>(I->getOperand(Op)),
                              Offset % ElemSize, M, TopLevelGlobal);
  }

  // Relative-pointer support starts here.
  if (auto *CI = dyn_cast<ConstantInt>(I)) {
    if (Offset == 0 && CI->isZero()) {
      return I;
    }
  }
  if (auto *C = dyn_cast<ConstantExpr>(I)) {
    switch (C->getOpcode()) {
    case Instruction::Trunc:
    case Instruction::PtrToInt:
      return getPointerAtOffset(cast<Constant>(C->getOperand(0)), Offset, M,
                                TopLevelGlobal);
    case Instruction::Sub: {
      auto *Operand0 = cast<Constant>(C->getOperand(0));
      auto *Operand1 = cast<Constant>(C->getOperand(1));

      auto StripGEP = [](Constant *C) {
        auto *CE = dyn_cast<ConstantExpr>(C);
        if (!CE)
          return C;
        if (CE->getOpcode() != Instruction::GetElementPtr)
          return C;
        return CE->getOperand(0);
      };
      auto *Operand1TargetGlobal = StripGEP(getPointerAtOffset(Operand1, 0, M));

      // Check that in the "sub (@a, @b)" expression, @b points back to the top
      // level global (or a GEP thereof) that we're processing. Otherwise bail.
      if (Operand1TargetGlobal != TopLevelGlobal)
        return nullptr;

      return getPointerAtOffset(Operand0, Offset, M, TopLevelGlobal);
    }
    default:
      return nullptr;
    }
  }
  return nullptr;
}

std::pair<Function *, Constant *>
llvm::getFunctionAtVTableOffset(GlobalVariable *GV, uint64_t Offset,
                                Module &M) {
  Constant *Ptr = getPointerAtOffset(GV->getInitializer(), Offset, M, GV);
  if (!Ptr)
    return std::pair<Function *, Constant *>(nullptr, nullptr);

  auto C = Ptr->stripPointerCasts();
  // Make sure this is a function or alias to a function.
  auto Fn = dyn_cast<Function>(C);
  auto A = dyn_cast<GlobalAlias>(C);
  if (!Fn && A)
    Fn = dyn_cast<Function>(A->getAliasee());

  if (!Fn)
    return std::pair<Function *, Constant *>(nullptr, nullptr);

  return std::pair<Function *, Constant *>(Fn, C);
}

static void replaceRelativePointerUserWithZero(User *U) {
  auto *PtrExpr = dyn_cast<ConstantExpr>(U);
  if (!PtrExpr || PtrExpr->getOpcode() != Instruction::PtrToInt)
    return;

  for (auto *PtrToIntUser : PtrExpr->users()) {
    auto *SubExpr = dyn_cast<ConstantExpr>(PtrToIntUser);
    if (!SubExpr || SubExpr->getOpcode() != Instruction::Sub)
      return;

    SubExpr->replaceNonMetadataUsesWith(
        ConstantInt::get(SubExpr->getType(), 0));
  }
}

void llvm::replaceRelativePointerUsersWithZero(Constant *C) {
  for (auto *U : C->users()) {
    if (auto *Equiv = dyn_cast<DSOLocalEquivalent>(U))
      replaceRelativePointerUsersWithZero(Equiv);
    else
      replaceRelativePointerUserWithZero(U);
  }
}
