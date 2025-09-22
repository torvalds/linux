//===- ScalarizeMaskedMemIntrin.cpp - Scalarize unsupported masked mem ----===//
//                                    intrinsics
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass replaces masked memory intrinsics - when unsupported by the target
// - with a chain of basic blocks, that deal with the elements one-by-one if the
// appropriate mask bit is set.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/ScalarizeMaskedMemIntrin.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <cassert>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "scalarize-masked-mem-intrin"

namespace {

class ScalarizeMaskedMemIntrinLegacyPass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  explicit ScalarizeMaskedMemIntrinLegacyPass() : FunctionPass(ID) {
    initializeScalarizeMaskedMemIntrinLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override {
    return "Scalarize Masked Memory Intrinsics";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
  }
};

} // end anonymous namespace

static bool optimizeBlock(BasicBlock &BB, bool &ModifiedDT,
                          const TargetTransformInfo &TTI, const DataLayout &DL,
                          DomTreeUpdater *DTU);
static bool optimizeCallInst(CallInst *CI, bool &ModifiedDT,
                             const TargetTransformInfo &TTI,
                             const DataLayout &DL, DomTreeUpdater *DTU);

char ScalarizeMaskedMemIntrinLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(ScalarizeMaskedMemIntrinLegacyPass, DEBUG_TYPE,
                      "Scalarize unsupported masked memory intrinsics", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(ScalarizeMaskedMemIntrinLegacyPass, DEBUG_TYPE,
                    "Scalarize unsupported masked memory intrinsics", false,
                    false)

FunctionPass *llvm::createScalarizeMaskedMemIntrinLegacyPass() {
  return new ScalarizeMaskedMemIntrinLegacyPass();
}

static bool isConstantIntVector(Value *Mask) {
  Constant *C = dyn_cast<Constant>(Mask);
  if (!C)
    return false;

  unsigned NumElts = cast<FixedVectorType>(Mask->getType())->getNumElements();
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *CElt = C->getAggregateElement(i);
    if (!CElt || !isa<ConstantInt>(CElt))
      return false;
  }

  return true;
}

static unsigned adjustForEndian(const DataLayout &DL, unsigned VectorWidth,
                                unsigned Idx) {
  return DL.isBigEndian() ? VectorWidth - 1 - Idx : Idx;
}

// Translate a masked load intrinsic like
// <16 x i32 > @llvm.masked.load( <16 x i32>* %addr, i32 align,
//                               <16 x i1> %mask, <16 x i32> %passthru)
// to a chain of basic blocks, with loading element one-by-one if
// the appropriate mask bit is set
//
//  %1 = bitcast i8* %addr to i32*
//  %2 = extractelement <16 x i1> %mask, i32 0
//  br i1 %2, label %cond.load, label %else
//
// cond.load:                                        ; preds = %0
//  %3 = getelementptr i32* %1, i32 0
//  %4 = load i32* %3
//  %5 = insertelement <16 x i32> %passthru, i32 %4, i32 0
//  br label %else
//
// else:                                             ; preds = %0, %cond.load
//  %res.phi.else = phi <16 x i32> [ %5, %cond.load ], [ poison, %0 ]
//  %6 = extractelement <16 x i1> %mask, i32 1
//  br i1 %6, label %cond.load1, label %else2
//
// cond.load1:                                       ; preds = %else
//  %7 = getelementptr i32* %1, i32 1
//  %8 = load i32* %7
//  %9 = insertelement <16 x i32> %res.phi.else, i32 %8, i32 1
//  br label %else2
//
// else2:                                          ; preds = %else, %cond.load1
//  %res.phi.else3 = phi <16 x i32> [ %9, %cond.load1 ], [ %res.phi.else, %else ]
//  %10 = extractelement <16 x i1> %mask, i32 2
//  br i1 %10, label %cond.load4, label %else5
//
static void scalarizeMaskedLoad(const DataLayout &DL, CallInst *CI,
                                DomTreeUpdater *DTU, bool &ModifiedDT) {
  Value *Ptr = CI->getArgOperand(0);
  Value *Alignment = CI->getArgOperand(1);
  Value *Mask = CI->getArgOperand(2);
  Value *Src0 = CI->getArgOperand(3);

  const Align AlignVal = cast<ConstantInt>(Alignment)->getAlignValue();
  VectorType *VecType = cast<FixedVectorType>(CI->getType());

  Type *EltTy = VecType->getElementType();

  IRBuilder<> Builder(CI->getContext());
  Instruction *InsertPt = CI;
  BasicBlock *IfBlock = CI->getParent();

  Builder.SetInsertPoint(InsertPt);
  Builder.SetCurrentDebugLocation(CI->getDebugLoc());

  // Short-cut if the mask is all-true.
  if (isa<Constant>(Mask) && cast<Constant>(Mask)->isAllOnesValue()) {
    Value *NewI = Builder.CreateAlignedLoad(VecType, Ptr, AlignVal);
    CI->replaceAllUsesWith(NewI);
    CI->eraseFromParent();
    return;
  }

  // Adjust alignment for the scalar instruction.
  const Align AdjustedAlignVal =
      commonAlignment(AlignVal, EltTy->getPrimitiveSizeInBits() / 8);
  unsigned VectorWidth = cast<FixedVectorType>(VecType)->getNumElements();

  // The result vector
  Value *VResult = Src0;

  if (isConstantIntVector(Mask)) {
    for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
      if (cast<Constant>(Mask)->getAggregateElement(Idx)->isNullValue())
        continue;
      Value *Gep = Builder.CreateConstInBoundsGEP1_32(EltTy, Ptr, Idx);
      LoadInst *Load = Builder.CreateAlignedLoad(EltTy, Gep, AdjustedAlignVal);
      VResult = Builder.CreateInsertElement(VResult, Load, Idx);
    }
    CI->replaceAllUsesWith(VResult);
    CI->eraseFromParent();
    return;
  }

  // If the mask is not v1i1, use scalar bit test operations. This generates
  // better results on X86 at least.
  Value *SclrMask;
  if (VectorWidth != 1) {
    Type *SclrMaskTy = Builder.getIntNTy(VectorWidth);
    SclrMask = Builder.CreateBitCast(Mask, SclrMaskTy, "scalar_mask");
  }

  for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
    // Fill the "else" block, created in the previous iteration
    //
    //  %res.phi.else3 = phi <16 x i32> [ %11, %cond.load1 ], [ %res.phi.else, %else ]
    //  %mask_1 = and i16 %scalar_mask, i32 1 << Idx
    //  %cond = icmp ne i16 %mask_1, 0
    //  br i1 %mask_1, label %cond.load, label %else
    //
    Value *Predicate;
    if (VectorWidth != 1) {
      Value *Mask = Builder.getInt(APInt::getOneBitSet(
          VectorWidth, adjustForEndian(DL, VectorWidth, Idx)));
      Predicate = Builder.CreateICmpNE(Builder.CreateAnd(SclrMask, Mask),
                                       Builder.getIntN(VectorWidth, 0));
    } else {
      Predicate = Builder.CreateExtractElement(Mask, Idx);
    }

    // Create "cond" block
    //
    //  %EltAddr = getelementptr i32* %1, i32 0
    //  %Elt = load i32* %EltAddr
    //  VResult = insertelement <16 x i32> VResult, i32 %Elt, i32 Idx
    //
    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(Predicate, InsertPt, /*Unreachable=*/false,
                                  /*BranchWeights=*/nullptr, DTU);

    BasicBlock *CondBlock = ThenTerm->getParent();
    CondBlock->setName("cond.load");

    Builder.SetInsertPoint(CondBlock->getTerminator());
    Value *Gep = Builder.CreateConstInBoundsGEP1_32(EltTy, Ptr, Idx);
    LoadInst *Load = Builder.CreateAlignedLoad(EltTy, Gep, AdjustedAlignVal);
    Value *NewVResult = Builder.CreateInsertElement(VResult, Load, Idx);

    // Create "else" block, fill it in the next iteration
    BasicBlock *NewIfBlock = ThenTerm->getSuccessor(0);
    NewIfBlock->setName("else");
    BasicBlock *PrevIfBlock = IfBlock;
    IfBlock = NewIfBlock;

    // Create the phi to join the new and previous value.
    Builder.SetInsertPoint(NewIfBlock, NewIfBlock->begin());
    PHINode *Phi = Builder.CreatePHI(VecType, 2, "res.phi.else");
    Phi->addIncoming(NewVResult, CondBlock);
    Phi->addIncoming(VResult, PrevIfBlock);
    VResult = Phi;
  }

  CI->replaceAllUsesWith(VResult);
  CI->eraseFromParent();

  ModifiedDT = true;
}

// Translate a masked store intrinsic, like
// void @llvm.masked.store(<16 x i32> %src, <16 x i32>* %addr, i32 align,
//                               <16 x i1> %mask)
// to a chain of basic blocks, that stores element one-by-one if
// the appropriate mask bit is set
//
//   %1 = bitcast i8* %addr to i32*
//   %2 = extractelement <16 x i1> %mask, i32 0
//   br i1 %2, label %cond.store, label %else
//
// cond.store:                                       ; preds = %0
//   %3 = extractelement <16 x i32> %val, i32 0
//   %4 = getelementptr i32* %1, i32 0
//   store i32 %3, i32* %4
//   br label %else
//
// else:                                             ; preds = %0, %cond.store
//   %5 = extractelement <16 x i1> %mask, i32 1
//   br i1 %5, label %cond.store1, label %else2
//
// cond.store1:                                      ; preds = %else
//   %6 = extractelement <16 x i32> %val, i32 1
//   %7 = getelementptr i32* %1, i32 1
//   store i32 %6, i32* %7
//   br label %else2
//   . . .
static void scalarizeMaskedStore(const DataLayout &DL, CallInst *CI,
                                 DomTreeUpdater *DTU, bool &ModifiedDT) {
  Value *Src = CI->getArgOperand(0);
  Value *Ptr = CI->getArgOperand(1);
  Value *Alignment = CI->getArgOperand(2);
  Value *Mask = CI->getArgOperand(3);

  const Align AlignVal = cast<ConstantInt>(Alignment)->getAlignValue();
  auto *VecType = cast<VectorType>(Src->getType());

  Type *EltTy = VecType->getElementType();

  IRBuilder<> Builder(CI->getContext());
  Instruction *InsertPt = CI;
  Builder.SetInsertPoint(InsertPt);
  Builder.SetCurrentDebugLocation(CI->getDebugLoc());

  // Short-cut if the mask is all-true.
  if (isa<Constant>(Mask) && cast<Constant>(Mask)->isAllOnesValue()) {
    Builder.CreateAlignedStore(Src, Ptr, AlignVal);
    CI->eraseFromParent();
    return;
  }

  // Adjust alignment for the scalar instruction.
  const Align AdjustedAlignVal =
      commonAlignment(AlignVal, EltTy->getPrimitiveSizeInBits() / 8);
  unsigned VectorWidth = cast<FixedVectorType>(VecType)->getNumElements();

  if (isConstantIntVector(Mask)) {
    for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
      if (cast<Constant>(Mask)->getAggregateElement(Idx)->isNullValue())
        continue;
      Value *OneElt = Builder.CreateExtractElement(Src, Idx);
      Value *Gep = Builder.CreateConstInBoundsGEP1_32(EltTy, Ptr, Idx);
      Builder.CreateAlignedStore(OneElt, Gep, AdjustedAlignVal);
    }
    CI->eraseFromParent();
    return;
  }

  // If the mask is not v1i1, use scalar bit test operations. This generates
  // better results on X86 at least.
  Value *SclrMask;
  if (VectorWidth != 1) {
    Type *SclrMaskTy = Builder.getIntNTy(VectorWidth);
    SclrMask = Builder.CreateBitCast(Mask, SclrMaskTy, "scalar_mask");
  }

  for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
    // Fill the "else" block, created in the previous iteration
    //
    //  %mask_1 = and i16 %scalar_mask, i32 1 << Idx
    //  %cond = icmp ne i16 %mask_1, 0
    //  br i1 %mask_1, label %cond.store, label %else
    //
    Value *Predicate;
    if (VectorWidth != 1) {
      Value *Mask = Builder.getInt(APInt::getOneBitSet(
          VectorWidth, adjustForEndian(DL, VectorWidth, Idx)));
      Predicate = Builder.CreateICmpNE(Builder.CreateAnd(SclrMask, Mask),
                                       Builder.getIntN(VectorWidth, 0));
    } else {
      Predicate = Builder.CreateExtractElement(Mask, Idx);
    }

    // Create "cond" block
    //
    //  %OneElt = extractelement <16 x i32> %Src, i32 Idx
    //  %EltAddr = getelementptr i32* %1, i32 0
    //  %store i32 %OneElt, i32* %EltAddr
    //
    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(Predicate, InsertPt, /*Unreachable=*/false,
                                  /*BranchWeights=*/nullptr, DTU);

    BasicBlock *CondBlock = ThenTerm->getParent();
    CondBlock->setName("cond.store");

    Builder.SetInsertPoint(CondBlock->getTerminator());
    Value *OneElt = Builder.CreateExtractElement(Src, Idx);
    Value *Gep = Builder.CreateConstInBoundsGEP1_32(EltTy, Ptr, Idx);
    Builder.CreateAlignedStore(OneElt, Gep, AdjustedAlignVal);

    // Create "else" block, fill it in the next iteration
    BasicBlock *NewIfBlock = ThenTerm->getSuccessor(0);
    NewIfBlock->setName("else");

    Builder.SetInsertPoint(NewIfBlock, NewIfBlock->begin());
  }
  CI->eraseFromParent();

  ModifiedDT = true;
}

// Translate a masked gather intrinsic like
// <16 x i32 > @llvm.masked.gather.v16i32( <16 x i32*> %Ptrs, i32 4,
//                               <16 x i1> %Mask, <16 x i32> %Src)
// to a chain of basic blocks, with loading element one-by-one if
// the appropriate mask bit is set
//
// %Ptrs = getelementptr i32, i32* %base, <16 x i64> %ind
// %Mask0 = extractelement <16 x i1> %Mask, i32 0
// br i1 %Mask0, label %cond.load, label %else
//
// cond.load:
// %Ptr0 = extractelement <16 x i32*> %Ptrs, i32 0
// %Load0 = load i32, i32* %Ptr0, align 4
// %Res0 = insertelement <16 x i32> poison, i32 %Load0, i32 0
// br label %else
//
// else:
// %res.phi.else = phi <16 x i32>[%Res0, %cond.load], [poison, %0]
// %Mask1 = extractelement <16 x i1> %Mask, i32 1
// br i1 %Mask1, label %cond.load1, label %else2
//
// cond.load1:
// %Ptr1 = extractelement <16 x i32*> %Ptrs, i32 1
// %Load1 = load i32, i32* %Ptr1, align 4
// %Res1 = insertelement <16 x i32> %res.phi.else, i32 %Load1, i32 1
// br label %else2
// . . .
// %Result = select <16 x i1> %Mask, <16 x i32> %res.phi.select, <16 x i32> %Src
// ret <16 x i32> %Result
static void scalarizeMaskedGather(const DataLayout &DL, CallInst *CI,
                                  DomTreeUpdater *DTU, bool &ModifiedDT) {
  Value *Ptrs = CI->getArgOperand(0);
  Value *Alignment = CI->getArgOperand(1);
  Value *Mask = CI->getArgOperand(2);
  Value *Src0 = CI->getArgOperand(3);

  auto *VecType = cast<FixedVectorType>(CI->getType());
  Type *EltTy = VecType->getElementType();

  IRBuilder<> Builder(CI->getContext());
  Instruction *InsertPt = CI;
  BasicBlock *IfBlock = CI->getParent();
  Builder.SetInsertPoint(InsertPt);
  MaybeAlign AlignVal = cast<ConstantInt>(Alignment)->getMaybeAlignValue();

  Builder.SetCurrentDebugLocation(CI->getDebugLoc());

  // The result vector
  Value *VResult = Src0;
  unsigned VectorWidth = VecType->getNumElements();

  // Shorten the way if the mask is a vector of constants.
  if (isConstantIntVector(Mask)) {
    for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
      if (cast<Constant>(Mask)->getAggregateElement(Idx)->isNullValue())
        continue;
      Value *Ptr = Builder.CreateExtractElement(Ptrs, Idx, "Ptr" + Twine(Idx));
      LoadInst *Load =
          Builder.CreateAlignedLoad(EltTy, Ptr, AlignVal, "Load" + Twine(Idx));
      VResult =
          Builder.CreateInsertElement(VResult, Load, Idx, "Res" + Twine(Idx));
    }
    CI->replaceAllUsesWith(VResult);
    CI->eraseFromParent();
    return;
  }

  // If the mask is not v1i1, use scalar bit test operations. This generates
  // better results on X86 at least.
  Value *SclrMask;
  if (VectorWidth != 1) {
    Type *SclrMaskTy = Builder.getIntNTy(VectorWidth);
    SclrMask = Builder.CreateBitCast(Mask, SclrMaskTy, "scalar_mask");
  }

  for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
    // Fill the "else" block, created in the previous iteration
    //
    //  %Mask1 = and i16 %scalar_mask, i32 1 << Idx
    //  %cond = icmp ne i16 %mask_1, 0
    //  br i1 %Mask1, label %cond.load, label %else
    //

    Value *Predicate;
    if (VectorWidth != 1) {
      Value *Mask = Builder.getInt(APInt::getOneBitSet(
          VectorWidth, adjustForEndian(DL, VectorWidth, Idx)));
      Predicate = Builder.CreateICmpNE(Builder.CreateAnd(SclrMask, Mask),
                                       Builder.getIntN(VectorWidth, 0));
    } else {
      Predicate = Builder.CreateExtractElement(Mask, Idx, "Mask" + Twine(Idx));
    }

    // Create "cond" block
    //
    //  %EltAddr = getelementptr i32* %1, i32 0
    //  %Elt = load i32* %EltAddr
    //  VResult = insertelement <16 x i32> VResult, i32 %Elt, i32 Idx
    //
    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(Predicate, InsertPt, /*Unreachable=*/false,
                                  /*BranchWeights=*/nullptr, DTU);

    BasicBlock *CondBlock = ThenTerm->getParent();
    CondBlock->setName("cond.load");

    Builder.SetInsertPoint(CondBlock->getTerminator());
    Value *Ptr = Builder.CreateExtractElement(Ptrs, Idx, "Ptr" + Twine(Idx));
    LoadInst *Load =
        Builder.CreateAlignedLoad(EltTy, Ptr, AlignVal, "Load" + Twine(Idx));
    Value *NewVResult =
        Builder.CreateInsertElement(VResult, Load, Idx, "Res" + Twine(Idx));

    // Create "else" block, fill it in the next iteration
    BasicBlock *NewIfBlock = ThenTerm->getSuccessor(0);
    NewIfBlock->setName("else");
    BasicBlock *PrevIfBlock = IfBlock;
    IfBlock = NewIfBlock;

    // Create the phi to join the new and previous value.
    Builder.SetInsertPoint(NewIfBlock, NewIfBlock->begin());
    PHINode *Phi = Builder.CreatePHI(VecType, 2, "res.phi.else");
    Phi->addIncoming(NewVResult, CondBlock);
    Phi->addIncoming(VResult, PrevIfBlock);
    VResult = Phi;
  }

  CI->replaceAllUsesWith(VResult);
  CI->eraseFromParent();

  ModifiedDT = true;
}

// Translate a masked scatter intrinsic, like
// void @llvm.masked.scatter.v16i32(<16 x i32> %Src, <16 x i32*>* %Ptrs, i32 4,
//                                  <16 x i1> %Mask)
// to a chain of basic blocks, that stores element one-by-one if
// the appropriate mask bit is set.
//
// %Ptrs = getelementptr i32, i32* %ptr, <16 x i64> %ind
// %Mask0 = extractelement <16 x i1> %Mask, i32 0
// br i1 %Mask0, label %cond.store, label %else
//
// cond.store:
// %Elt0 = extractelement <16 x i32> %Src, i32 0
// %Ptr0 = extractelement <16 x i32*> %Ptrs, i32 0
// store i32 %Elt0, i32* %Ptr0, align 4
// br label %else
//
// else:
// %Mask1 = extractelement <16 x i1> %Mask, i32 1
// br i1 %Mask1, label %cond.store1, label %else2
//
// cond.store1:
// %Elt1 = extractelement <16 x i32> %Src, i32 1
// %Ptr1 = extractelement <16 x i32*> %Ptrs, i32 1
// store i32 %Elt1, i32* %Ptr1, align 4
// br label %else2
//   . . .
static void scalarizeMaskedScatter(const DataLayout &DL, CallInst *CI,
                                   DomTreeUpdater *DTU, bool &ModifiedDT) {
  Value *Src = CI->getArgOperand(0);
  Value *Ptrs = CI->getArgOperand(1);
  Value *Alignment = CI->getArgOperand(2);
  Value *Mask = CI->getArgOperand(3);

  auto *SrcFVTy = cast<FixedVectorType>(Src->getType());

  assert(
      isa<VectorType>(Ptrs->getType()) &&
      isa<PointerType>(cast<VectorType>(Ptrs->getType())->getElementType()) &&
      "Vector of pointers is expected in masked scatter intrinsic");

  IRBuilder<> Builder(CI->getContext());
  Instruction *InsertPt = CI;
  Builder.SetInsertPoint(InsertPt);
  Builder.SetCurrentDebugLocation(CI->getDebugLoc());

  MaybeAlign AlignVal = cast<ConstantInt>(Alignment)->getMaybeAlignValue();
  unsigned VectorWidth = SrcFVTy->getNumElements();

  // Shorten the way if the mask is a vector of constants.
  if (isConstantIntVector(Mask)) {
    for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
      if (cast<Constant>(Mask)->getAggregateElement(Idx)->isNullValue())
        continue;
      Value *OneElt =
          Builder.CreateExtractElement(Src, Idx, "Elt" + Twine(Idx));
      Value *Ptr = Builder.CreateExtractElement(Ptrs, Idx, "Ptr" + Twine(Idx));
      Builder.CreateAlignedStore(OneElt, Ptr, AlignVal);
    }
    CI->eraseFromParent();
    return;
  }

  // If the mask is not v1i1, use scalar bit test operations. This generates
  // better results on X86 at least.
  Value *SclrMask;
  if (VectorWidth != 1) {
    Type *SclrMaskTy = Builder.getIntNTy(VectorWidth);
    SclrMask = Builder.CreateBitCast(Mask, SclrMaskTy, "scalar_mask");
  }

  for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
    // Fill the "else" block, created in the previous iteration
    //
    //  %Mask1 = and i16 %scalar_mask, i32 1 << Idx
    //  %cond = icmp ne i16 %mask_1, 0
    //  br i1 %Mask1, label %cond.store, label %else
    //
    Value *Predicate;
    if (VectorWidth != 1) {
      Value *Mask = Builder.getInt(APInt::getOneBitSet(
          VectorWidth, adjustForEndian(DL, VectorWidth, Idx)));
      Predicate = Builder.CreateICmpNE(Builder.CreateAnd(SclrMask, Mask),
                                       Builder.getIntN(VectorWidth, 0));
    } else {
      Predicate = Builder.CreateExtractElement(Mask, Idx, "Mask" + Twine(Idx));
    }

    // Create "cond" block
    //
    //  %Elt1 = extractelement <16 x i32> %Src, i32 1
    //  %Ptr1 = extractelement <16 x i32*> %Ptrs, i32 1
    //  %store i32 %Elt1, i32* %Ptr1
    //
    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(Predicate, InsertPt, /*Unreachable=*/false,
                                  /*BranchWeights=*/nullptr, DTU);

    BasicBlock *CondBlock = ThenTerm->getParent();
    CondBlock->setName("cond.store");

    Builder.SetInsertPoint(CondBlock->getTerminator());
    Value *OneElt = Builder.CreateExtractElement(Src, Idx, "Elt" + Twine(Idx));
    Value *Ptr = Builder.CreateExtractElement(Ptrs, Idx, "Ptr" + Twine(Idx));
    Builder.CreateAlignedStore(OneElt, Ptr, AlignVal);

    // Create "else" block, fill it in the next iteration
    BasicBlock *NewIfBlock = ThenTerm->getSuccessor(0);
    NewIfBlock->setName("else");

    Builder.SetInsertPoint(NewIfBlock, NewIfBlock->begin());
  }
  CI->eraseFromParent();

  ModifiedDT = true;
}

static void scalarizeMaskedExpandLoad(const DataLayout &DL, CallInst *CI,
                                      DomTreeUpdater *DTU, bool &ModifiedDT) {
  Value *Ptr = CI->getArgOperand(0);
  Value *Mask = CI->getArgOperand(1);
  Value *PassThru = CI->getArgOperand(2);
  Align Alignment = CI->getParamAlign(0).valueOrOne();

  auto *VecType = cast<FixedVectorType>(CI->getType());

  Type *EltTy = VecType->getElementType();

  IRBuilder<> Builder(CI->getContext());
  Instruction *InsertPt = CI;
  BasicBlock *IfBlock = CI->getParent();

  Builder.SetInsertPoint(InsertPt);
  Builder.SetCurrentDebugLocation(CI->getDebugLoc());

  unsigned VectorWidth = VecType->getNumElements();

  // The result vector
  Value *VResult = PassThru;

  // Adjust alignment for the scalar instruction.
  const Align AdjustedAlignment =
      commonAlignment(Alignment, EltTy->getPrimitiveSizeInBits() / 8);

  // Shorten the way if the mask is a vector of constants.
  // Create a build_vector pattern, with loads/poisons as necessary and then
  // shuffle blend with the pass through value.
  if (isConstantIntVector(Mask)) {
    unsigned MemIndex = 0;
    VResult = PoisonValue::get(VecType);
    SmallVector<int, 16> ShuffleMask(VectorWidth, PoisonMaskElem);
    for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
      Value *InsertElt;
      if (cast<Constant>(Mask)->getAggregateElement(Idx)->isNullValue()) {
        InsertElt = PoisonValue::get(EltTy);
        ShuffleMask[Idx] = Idx + VectorWidth;
      } else {
        Value *NewPtr =
            Builder.CreateConstInBoundsGEP1_32(EltTy, Ptr, MemIndex);
        InsertElt = Builder.CreateAlignedLoad(EltTy, NewPtr, AdjustedAlignment,
                                              "Load" + Twine(Idx));
        ShuffleMask[Idx] = Idx;
        ++MemIndex;
      }
      VResult = Builder.CreateInsertElement(VResult, InsertElt, Idx,
                                            "Res" + Twine(Idx));
    }
    VResult = Builder.CreateShuffleVector(VResult, PassThru, ShuffleMask);
    CI->replaceAllUsesWith(VResult);
    CI->eraseFromParent();
    return;
  }

  // If the mask is not v1i1, use scalar bit test operations. This generates
  // better results on X86 at least.
  Value *SclrMask;
  if (VectorWidth != 1) {
    Type *SclrMaskTy = Builder.getIntNTy(VectorWidth);
    SclrMask = Builder.CreateBitCast(Mask, SclrMaskTy, "scalar_mask");
  }

  for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
    // Fill the "else" block, created in the previous iteration
    //
    //  %res.phi.else3 = phi <16 x i32> [ %11, %cond.load1 ], [ %res.phi.else, %else ]
    //  %mask_1 = extractelement <16 x i1> %mask, i32 Idx
    //  br i1 %mask_1, label %cond.load, label %else
    //

    Value *Predicate;
    if (VectorWidth != 1) {
      Value *Mask = Builder.getInt(APInt::getOneBitSet(
          VectorWidth, adjustForEndian(DL, VectorWidth, Idx)));
      Predicate = Builder.CreateICmpNE(Builder.CreateAnd(SclrMask, Mask),
                                       Builder.getIntN(VectorWidth, 0));
    } else {
      Predicate = Builder.CreateExtractElement(Mask, Idx, "Mask" + Twine(Idx));
    }

    // Create "cond" block
    //
    //  %EltAddr = getelementptr i32* %1, i32 0
    //  %Elt = load i32* %EltAddr
    //  VResult = insertelement <16 x i32> VResult, i32 %Elt, i32 Idx
    //
    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(Predicate, InsertPt, /*Unreachable=*/false,
                                  /*BranchWeights=*/nullptr, DTU);

    BasicBlock *CondBlock = ThenTerm->getParent();
    CondBlock->setName("cond.load");

    Builder.SetInsertPoint(CondBlock->getTerminator());
    LoadInst *Load = Builder.CreateAlignedLoad(EltTy, Ptr, AdjustedAlignment);
    Value *NewVResult = Builder.CreateInsertElement(VResult, Load, Idx);

    // Move the pointer if there are more blocks to come.
    Value *NewPtr;
    if ((Idx + 1) != VectorWidth)
      NewPtr = Builder.CreateConstInBoundsGEP1_32(EltTy, Ptr, 1);

    // Create "else" block, fill it in the next iteration
    BasicBlock *NewIfBlock = ThenTerm->getSuccessor(0);
    NewIfBlock->setName("else");
    BasicBlock *PrevIfBlock = IfBlock;
    IfBlock = NewIfBlock;

    // Create the phi to join the new and previous value.
    Builder.SetInsertPoint(NewIfBlock, NewIfBlock->begin());
    PHINode *ResultPhi = Builder.CreatePHI(VecType, 2, "res.phi.else");
    ResultPhi->addIncoming(NewVResult, CondBlock);
    ResultPhi->addIncoming(VResult, PrevIfBlock);
    VResult = ResultPhi;

    // Add a PHI for the pointer if this isn't the last iteration.
    if ((Idx + 1) != VectorWidth) {
      PHINode *PtrPhi = Builder.CreatePHI(Ptr->getType(), 2, "ptr.phi.else");
      PtrPhi->addIncoming(NewPtr, CondBlock);
      PtrPhi->addIncoming(Ptr, PrevIfBlock);
      Ptr = PtrPhi;
    }
  }

  CI->replaceAllUsesWith(VResult);
  CI->eraseFromParent();

  ModifiedDT = true;
}

static void scalarizeMaskedCompressStore(const DataLayout &DL, CallInst *CI,
                                         DomTreeUpdater *DTU,
                                         bool &ModifiedDT) {
  Value *Src = CI->getArgOperand(0);
  Value *Ptr = CI->getArgOperand(1);
  Value *Mask = CI->getArgOperand(2);
  Align Alignment = CI->getParamAlign(1).valueOrOne();

  auto *VecType = cast<FixedVectorType>(Src->getType());

  IRBuilder<> Builder(CI->getContext());
  Instruction *InsertPt = CI;
  BasicBlock *IfBlock = CI->getParent();

  Builder.SetInsertPoint(InsertPt);
  Builder.SetCurrentDebugLocation(CI->getDebugLoc());

  Type *EltTy = VecType->getElementType();

  // Adjust alignment for the scalar instruction.
  const Align AdjustedAlignment =
      commonAlignment(Alignment, EltTy->getPrimitiveSizeInBits() / 8);

  unsigned VectorWidth = VecType->getNumElements();

  // Shorten the way if the mask is a vector of constants.
  if (isConstantIntVector(Mask)) {
    unsigned MemIndex = 0;
    for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
      if (cast<Constant>(Mask)->getAggregateElement(Idx)->isNullValue())
        continue;
      Value *OneElt =
          Builder.CreateExtractElement(Src, Idx, "Elt" + Twine(Idx));
      Value *NewPtr = Builder.CreateConstInBoundsGEP1_32(EltTy, Ptr, MemIndex);
      Builder.CreateAlignedStore(OneElt, NewPtr, AdjustedAlignment);
      ++MemIndex;
    }
    CI->eraseFromParent();
    return;
  }

  // If the mask is not v1i1, use scalar bit test operations. This generates
  // better results on X86 at least.
  Value *SclrMask;
  if (VectorWidth != 1) {
    Type *SclrMaskTy = Builder.getIntNTy(VectorWidth);
    SclrMask = Builder.CreateBitCast(Mask, SclrMaskTy, "scalar_mask");
  }

  for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
    // Fill the "else" block, created in the previous iteration
    //
    //  %mask_1 = extractelement <16 x i1> %mask, i32 Idx
    //  br i1 %mask_1, label %cond.store, label %else
    //
    Value *Predicate;
    if (VectorWidth != 1) {
      Value *Mask = Builder.getInt(APInt::getOneBitSet(
          VectorWidth, adjustForEndian(DL, VectorWidth, Idx)));
      Predicate = Builder.CreateICmpNE(Builder.CreateAnd(SclrMask, Mask),
                                       Builder.getIntN(VectorWidth, 0));
    } else {
      Predicate = Builder.CreateExtractElement(Mask, Idx, "Mask" + Twine(Idx));
    }

    // Create "cond" block
    //
    //  %OneElt = extractelement <16 x i32> %Src, i32 Idx
    //  %EltAddr = getelementptr i32* %1, i32 0
    //  %store i32 %OneElt, i32* %EltAddr
    //
    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(Predicate, InsertPt, /*Unreachable=*/false,
                                  /*BranchWeights=*/nullptr, DTU);

    BasicBlock *CondBlock = ThenTerm->getParent();
    CondBlock->setName("cond.store");

    Builder.SetInsertPoint(CondBlock->getTerminator());
    Value *OneElt = Builder.CreateExtractElement(Src, Idx);
    Builder.CreateAlignedStore(OneElt, Ptr, AdjustedAlignment);

    // Move the pointer if there are more blocks to come.
    Value *NewPtr;
    if ((Idx + 1) != VectorWidth)
      NewPtr = Builder.CreateConstInBoundsGEP1_32(EltTy, Ptr, 1);

    // Create "else" block, fill it in the next iteration
    BasicBlock *NewIfBlock = ThenTerm->getSuccessor(0);
    NewIfBlock->setName("else");
    BasicBlock *PrevIfBlock = IfBlock;
    IfBlock = NewIfBlock;

    Builder.SetInsertPoint(NewIfBlock, NewIfBlock->begin());

    // Add a PHI for the pointer if this isn't the last iteration.
    if ((Idx + 1) != VectorWidth) {
      PHINode *PtrPhi = Builder.CreatePHI(Ptr->getType(), 2, "ptr.phi.else");
      PtrPhi->addIncoming(NewPtr, CondBlock);
      PtrPhi->addIncoming(Ptr, PrevIfBlock);
      Ptr = PtrPhi;
    }
  }
  CI->eraseFromParent();

  ModifiedDT = true;
}

static void scalarizeMaskedVectorHistogram(const DataLayout &DL, CallInst *CI,
                                           DomTreeUpdater *DTU,
                                           bool &ModifiedDT) {
  // If we extend histogram to return a result someday (like the updated vector)
  // then we'll need to support it here.
  assert(CI->getType()->isVoidTy() && "Histogram with non-void return.");
  Value *Ptrs = CI->getArgOperand(0);
  Value *Inc = CI->getArgOperand(1);
  Value *Mask = CI->getArgOperand(2);

  auto *AddrType = cast<FixedVectorType>(Ptrs->getType());
  Type *EltTy = Inc->getType();

  IRBuilder<> Builder(CI->getContext());
  Instruction *InsertPt = CI;
  Builder.SetInsertPoint(InsertPt);

  Builder.SetCurrentDebugLocation(CI->getDebugLoc());

  // FIXME: Do we need to add an alignment parameter to the intrinsic?
  unsigned VectorWidth = AddrType->getNumElements();

  // Shorten the way if the mask is a vector of constants.
  if (isConstantIntVector(Mask)) {
    for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
      if (cast<Constant>(Mask)->getAggregateElement(Idx)->isNullValue())
        continue;
      Value *Ptr = Builder.CreateExtractElement(Ptrs, Idx, "Ptr" + Twine(Idx));
      LoadInst *Load = Builder.CreateLoad(EltTy, Ptr, "Load" + Twine(Idx));
      Value *Add = Builder.CreateAdd(Load, Inc);
      Builder.CreateStore(Add, Ptr);
    }
    CI->eraseFromParent();
    return;
  }

  for (unsigned Idx = 0; Idx < VectorWidth; ++Idx) {
    Value *Predicate =
        Builder.CreateExtractElement(Mask, Idx, "Mask" + Twine(Idx));

    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(Predicate, InsertPt, /*Unreachable=*/false,
                                  /*BranchWeights=*/nullptr, DTU);

    BasicBlock *CondBlock = ThenTerm->getParent();
    CondBlock->setName("cond.histogram.update");

    Builder.SetInsertPoint(CondBlock->getTerminator());
    Value *Ptr = Builder.CreateExtractElement(Ptrs, Idx, "Ptr" + Twine(Idx));
    LoadInst *Load = Builder.CreateLoad(EltTy, Ptr, "Load" + Twine(Idx));
    Value *Add = Builder.CreateAdd(Load, Inc);
    Builder.CreateStore(Add, Ptr);

    // Create "else" block, fill it in the next iteration
    BasicBlock *NewIfBlock = ThenTerm->getSuccessor(0);
    NewIfBlock->setName("else");
    Builder.SetInsertPoint(NewIfBlock, NewIfBlock->begin());
  }

  CI->eraseFromParent();
  ModifiedDT = true;
}

static bool runImpl(Function &F, const TargetTransformInfo &TTI,
                    DominatorTree *DT) {
  std::optional<DomTreeUpdater> DTU;
  if (DT)
    DTU.emplace(DT, DomTreeUpdater::UpdateStrategy::Lazy);

  bool EverMadeChange = false;
  bool MadeChange = true;
  auto &DL = F.getDataLayout();
  while (MadeChange) {
    MadeChange = false;
    for (BasicBlock &BB : llvm::make_early_inc_range(F)) {
      bool ModifiedDTOnIteration = false;
      MadeChange |= optimizeBlock(BB, ModifiedDTOnIteration, TTI, DL,
                                  DTU ? &*DTU : nullptr);

      // Restart BB iteration if the dominator tree of the Function was changed
      if (ModifiedDTOnIteration)
        break;
    }

    EverMadeChange |= MadeChange;
  }
  return EverMadeChange;
}

bool ScalarizeMaskedMemIntrinLegacyPass::runOnFunction(Function &F) {
  auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  DominatorTree *DT = nullptr;
  if (auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>())
    DT = &DTWP->getDomTree();
  return runImpl(F, TTI, DT);
}

PreservedAnalyses
ScalarizeMaskedMemIntrinPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  auto *DT = AM.getCachedResult<DominatorTreeAnalysis>(F);
  if (!runImpl(F, TTI, DT))
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<TargetIRAnalysis>();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

static bool optimizeBlock(BasicBlock &BB, bool &ModifiedDT,
                          const TargetTransformInfo &TTI, const DataLayout &DL,
                          DomTreeUpdater *DTU) {
  bool MadeChange = false;

  BasicBlock::iterator CurInstIterator = BB.begin();
  while (CurInstIterator != BB.end()) {
    if (CallInst *CI = dyn_cast<CallInst>(&*CurInstIterator++))
      MadeChange |= optimizeCallInst(CI, ModifiedDT, TTI, DL, DTU);
    if (ModifiedDT)
      return true;
  }

  return MadeChange;
}

static bool optimizeCallInst(CallInst *CI, bool &ModifiedDT,
                             const TargetTransformInfo &TTI,
                             const DataLayout &DL, DomTreeUpdater *DTU) {
  IntrinsicInst *II = dyn_cast<IntrinsicInst>(CI);
  if (II) {
    // The scalarization code below does not work for scalable vectors.
    if (isa<ScalableVectorType>(II->getType()) ||
        any_of(II->args(),
               [](Value *V) { return isa<ScalableVectorType>(V->getType()); }))
      return false;

    switch (II->getIntrinsicID()) {
    default:
      break;
    case Intrinsic::experimental_vector_histogram_add:
      if (TTI.isLegalMaskedVectorHistogram(CI->getArgOperand(0)->getType(),
                                           CI->getArgOperand(1)->getType()))
        return false;
      scalarizeMaskedVectorHistogram(DL, CI, DTU, ModifiedDT);
      return true;
    case Intrinsic::masked_load:
      // Scalarize unsupported vector masked load
      if (TTI.isLegalMaskedLoad(
              CI->getType(),
              cast<ConstantInt>(CI->getArgOperand(1))->getAlignValue()))
        return false;
      scalarizeMaskedLoad(DL, CI, DTU, ModifiedDT);
      return true;
    case Intrinsic::masked_store:
      if (TTI.isLegalMaskedStore(
              CI->getArgOperand(0)->getType(),
              cast<ConstantInt>(CI->getArgOperand(2))->getAlignValue()))
        return false;
      scalarizeMaskedStore(DL, CI, DTU, ModifiedDT);
      return true;
    case Intrinsic::masked_gather: {
      MaybeAlign MA =
          cast<ConstantInt>(CI->getArgOperand(1))->getMaybeAlignValue();
      Type *LoadTy = CI->getType();
      Align Alignment = DL.getValueOrABITypeAlignment(MA,
                                                      LoadTy->getScalarType());
      if (TTI.isLegalMaskedGather(LoadTy, Alignment) &&
          !TTI.forceScalarizeMaskedGather(cast<VectorType>(LoadTy), Alignment))
        return false;
      scalarizeMaskedGather(DL, CI, DTU, ModifiedDT);
      return true;
    }
    case Intrinsic::masked_scatter: {
      MaybeAlign MA =
          cast<ConstantInt>(CI->getArgOperand(2))->getMaybeAlignValue();
      Type *StoreTy = CI->getArgOperand(0)->getType();
      Align Alignment = DL.getValueOrABITypeAlignment(MA,
                                                      StoreTy->getScalarType());
      if (TTI.isLegalMaskedScatter(StoreTy, Alignment) &&
          !TTI.forceScalarizeMaskedScatter(cast<VectorType>(StoreTy),
                                           Alignment))
        return false;
      scalarizeMaskedScatter(DL, CI, DTU, ModifiedDT);
      return true;
    }
    case Intrinsic::masked_expandload:
      if (TTI.isLegalMaskedExpandLoad(
              CI->getType(),
              CI->getAttributes().getParamAttrs(0).getAlignment().valueOrOne()))
        return false;
      scalarizeMaskedExpandLoad(DL, CI, DTU, ModifiedDT);
      return true;
    case Intrinsic::masked_compressstore:
      if (TTI.isLegalMaskedCompressStore(
              CI->getArgOperand(0)->getType(),
              CI->getAttributes().getParamAttrs(1).getAlignment().valueOrOne()))
        return false;
      scalarizeMaskedCompressStore(DL, CI, DTU, ModifiedDT);
      return true;
    }
  }

  return false;
}
