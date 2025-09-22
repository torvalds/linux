//===- LowerAtomic.cpp - Lower atomic intrinsics --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers atomic intrinsics to non-atomic form for use in a known
// non-preemptible environment.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LowerAtomic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "loweratomic"

bool llvm::lowerAtomicCmpXchgInst(AtomicCmpXchgInst *CXI) {
  IRBuilder<> Builder(CXI);
  Value *Ptr = CXI->getPointerOperand();
  Value *Cmp = CXI->getCompareOperand();
  Value *Val = CXI->getNewValOperand();

  LoadInst *Orig = Builder.CreateLoad(Val->getType(), Ptr);
  Value *Equal = Builder.CreateICmpEQ(Orig, Cmp);
  Value *Res = Builder.CreateSelect(Equal, Val, Orig);
  Builder.CreateStore(Res, Ptr);

  Res = Builder.CreateInsertValue(PoisonValue::get(CXI->getType()), Orig, 0);
  Res = Builder.CreateInsertValue(Res, Equal, 1);

  CXI->replaceAllUsesWith(Res);
  CXI->eraseFromParent();
  return true;
}

Value *llvm::buildAtomicRMWValue(AtomicRMWInst::BinOp Op,
                                 IRBuilderBase &Builder, Value *Loaded,
                                 Value *Val) {
  Value *NewVal;
  switch (Op) {
  case AtomicRMWInst::Xchg:
    return Val;
  case AtomicRMWInst::Add:
    return Builder.CreateAdd(Loaded, Val, "new");
  case AtomicRMWInst::Sub:
    return Builder.CreateSub(Loaded, Val, "new");
  case AtomicRMWInst::And:
    return Builder.CreateAnd(Loaded, Val, "new");
  case AtomicRMWInst::Nand:
    return Builder.CreateNot(Builder.CreateAnd(Loaded, Val), "new");
  case AtomicRMWInst::Or:
    return Builder.CreateOr(Loaded, Val, "new");
  case AtomicRMWInst::Xor:
    return Builder.CreateXor(Loaded, Val, "new");
  case AtomicRMWInst::Max:
    NewVal = Builder.CreateICmpSGT(Loaded, Val);
    return Builder.CreateSelect(NewVal, Loaded, Val, "new");
  case AtomicRMWInst::Min:
    NewVal = Builder.CreateICmpSLE(Loaded, Val);
    return Builder.CreateSelect(NewVal, Loaded, Val, "new");
  case AtomicRMWInst::UMax:
    NewVal = Builder.CreateICmpUGT(Loaded, Val);
    return Builder.CreateSelect(NewVal, Loaded, Val, "new");
  case AtomicRMWInst::UMin:
    NewVal = Builder.CreateICmpULE(Loaded, Val);
    return Builder.CreateSelect(NewVal, Loaded, Val, "new");
  case AtomicRMWInst::FAdd:
    return Builder.CreateFAdd(Loaded, Val, "new");
  case AtomicRMWInst::FSub:
    return Builder.CreateFSub(Loaded, Val, "new");
  case AtomicRMWInst::FMax:
    return Builder.CreateMaxNum(Loaded, Val);
  case AtomicRMWInst::FMin:
    return Builder.CreateMinNum(Loaded, Val);
  case AtomicRMWInst::UIncWrap: {
    Constant *One = ConstantInt::get(Loaded->getType(), 1);
    Value *Inc = Builder.CreateAdd(Loaded, One);
    Value *Cmp = Builder.CreateICmpUGE(Loaded, Val);
    Constant *Zero = ConstantInt::get(Loaded->getType(), 0);
    return Builder.CreateSelect(Cmp, Zero, Inc, "new");
  }
  case AtomicRMWInst::UDecWrap: {
    Constant *Zero = ConstantInt::get(Loaded->getType(), 0);
    Constant *One = ConstantInt::get(Loaded->getType(), 1);

    Value *Dec = Builder.CreateSub(Loaded, One);
    Value *CmpEq0 = Builder.CreateICmpEQ(Loaded, Zero);
    Value *CmpOldGtVal = Builder.CreateICmpUGT(Loaded, Val);
    Value *Or = Builder.CreateOr(CmpEq0, CmpOldGtVal);
    return Builder.CreateSelect(Or, Val, Dec, "new");
  }
  default:
    llvm_unreachable("Unknown atomic op");
  }
}

bool llvm::lowerAtomicRMWInst(AtomicRMWInst *RMWI) {
  IRBuilder<> Builder(RMWI);
  Builder.setIsFPConstrained(
      RMWI->getFunction()->hasFnAttribute(Attribute::StrictFP));

  Value *Ptr = RMWI->getPointerOperand();
  Value *Val = RMWI->getValOperand();

  LoadInst *Orig = Builder.CreateLoad(Val->getType(), Ptr);
  Value *Res = buildAtomicRMWValue(RMWI->getOperation(), Builder, Orig, Val);
  Builder.CreateStore(Res, Ptr);
  RMWI->replaceAllUsesWith(Orig);
  RMWI->eraseFromParent();
  return true;
}
