//===-- Operator.cpp - Implement the LLVM operators -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the non-inline methods for the LLVM Operator classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Operator.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/Instructions.h"

#include "ConstantsContext.h"

namespace llvm {
bool Operator::hasPoisonGeneratingFlags() const {
  switch (getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::Shl: {
    auto *OBO = cast<OverflowingBinaryOperator>(this);
    return OBO->hasNoUnsignedWrap() || OBO->hasNoSignedWrap();
  }
  case Instruction::Trunc: {
    if (auto *TI = dyn_cast<TruncInst>(this))
      return TI->hasNoUnsignedWrap() || TI->hasNoSignedWrap();
    return false;
  }
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::AShr:
  case Instruction::LShr:
    return cast<PossiblyExactOperator>(this)->isExact();
  case Instruction::Or:
    return cast<PossiblyDisjointInst>(this)->isDisjoint();
  case Instruction::GetElementPtr: {
    auto *GEP = cast<GEPOperator>(this);
    // Note: inrange exists on constexpr only
    return GEP->getNoWrapFlags() != GEPNoWrapFlags::none() ||
           GEP->getInRange() != std::nullopt;
  }
  case Instruction::UIToFP:
  case Instruction::ZExt:
    if (auto *NNI = dyn_cast<PossiblyNonNegInst>(this))
      return NNI->hasNonNeg();
    return false;
  default:
    if (const auto *FP = dyn_cast<FPMathOperator>(this))
      return FP->hasNoNaNs() || FP->hasNoInfs();
    return false;
  }
}

bool Operator::hasPoisonGeneratingAnnotations() const {
  if (hasPoisonGeneratingFlags())
    return true;
  auto *I = dyn_cast<Instruction>(this);
  return I && (I->hasPoisonGeneratingReturnAttributes() ||
               I->hasPoisonGeneratingMetadata());
}

Type *GEPOperator::getSourceElementType() const {
  if (auto *I = dyn_cast<GetElementPtrInst>(this))
    return I->getSourceElementType();
  return cast<GetElementPtrConstantExpr>(this)->getSourceElementType();
}

Type *GEPOperator::getResultElementType() const {
  if (auto *I = dyn_cast<GetElementPtrInst>(this))
    return I->getResultElementType();
  return cast<GetElementPtrConstantExpr>(this)->getResultElementType();
}

std::optional<ConstantRange> GEPOperator::getInRange() const {
  if (auto *CE = dyn_cast<GetElementPtrConstantExpr>(this))
    return CE->getInRange();
  return std::nullopt;
}

Align GEPOperator::getMaxPreservedAlignment(const DataLayout &DL) const {
  /// compute the worse possible offset for every level of the GEP et accumulate
  /// the minimum alignment into Result.

  Align Result = Align(llvm::Value::MaximumAlignment);
  for (gep_type_iterator GTI = gep_type_begin(this), GTE = gep_type_end(this);
       GTI != GTE; ++GTI) {
    uint64_t Offset;
    ConstantInt *OpC = dyn_cast<ConstantInt>(GTI.getOperand());

    if (StructType *STy = GTI.getStructTypeOrNull()) {
      const StructLayout *SL = DL.getStructLayout(STy);
      Offset = SL->getElementOffset(OpC->getZExtValue());
    } else {
      assert(GTI.isSequential() && "should be sequencial");
      /// If the index isn't known, we take 1 because it is the index that will
      /// give the worse alignment of the offset.
      const uint64_t ElemCount = OpC ? OpC->getZExtValue() : 1;
      Offset = GTI.getSequentialElementStride(DL) * ElemCount;
    }
    Result = Align(MinAlign(Offset, Result.value()));
  }
  return Result;
}

bool GEPOperator::accumulateConstantOffset(
    const DataLayout &DL, APInt &Offset,
    function_ref<bool(Value &, APInt &)> ExternalAnalysis) const {
  assert(Offset.getBitWidth() ==
             DL.getIndexSizeInBits(getPointerAddressSpace()) &&
         "The offset bit width does not match DL specification.");
  SmallVector<const Value *> Index(llvm::drop_begin(operand_values()));
  return GEPOperator::accumulateConstantOffset(getSourceElementType(), Index,
                                               DL, Offset, ExternalAnalysis);
}

bool GEPOperator::accumulateConstantOffset(
    Type *SourceType, ArrayRef<const Value *> Index, const DataLayout &DL,
    APInt &Offset, function_ref<bool(Value &, APInt &)> ExternalAnalysis) {
  // Fast path for canonical getelementptr i8 form.
  if (SourceType->isIntegerTy(8) && !ExternalAnalysis) {
    if (auto *CI = dyn_cast<ConstantInt>(Index.front())) {
      Offset += CI->getValue().sextOrTrunc(Offset.getBitWidth());
      return true;
    }
    return false;
  }

  bool UsedExternalAnalysis = false;
  auto AccumulateOffset = [&](APInt Index, uint64_t Size) -> bool {
    Index = Index.sextOrTrunc(Offset.getBitWidth());
    APInt IndexedSize = APInt(Offset.getBitWidth(), Size);
    // For array or vector indices, scale the index by the size of the type.
    if (!UsedExternalAnalysis) {
      Offset += Index * IndexedSize;
    } else {
      // External Analysis can return a result higher/lower than the value
      // represents. We need to detect overflow/underflow.
      bool Overflow = false;
      APInt OffsetPlus = Index.smul_ov(IndexedSize, Overflow);
      if (Overflow)
        return false;
      Offset = Offset.sadd_ov(OffsetPlus, Overflow);
      if (Overflow)
        return false;
    }
    return true;
  };
  auto begin = generic_gep_type_iterator<decltype(Index.begin())>::begin(
      SourceType, Index.begin());
  auto end = generic_gep_type_iterator<decltype(Index.end())>::end(Index.end());
  for (auto GTI = begin, GTE = end; GTI != GTE; ++GTI) {
    // Scalable vectors are multiplied by a runtime constant.
    bool ScalableType = GTI.getIndexedType()->isScalableTy();

    Value *V = GTI.getOperand();
    StructType *STy = GTI.getStructTypeOrNull();
    // Handle ConstantInt if possible.
    if (auto ConstOffset = dyn_cast<ConstantInt>(V)) {
      if (ConstOffset->isZero())
        continue;
      // if the type is scalable and the constant is not zero (vscale * n * 0 =
      // 0) bailout.
      if (ScalableType)
        return false;
      // Handle a struct index, which adds its field offset to the pointer.
      if (STy) {
        unsigned ElementIdx = ConstOffset->getZExtValue();
        const StructLayout *SL = DL.getStructLayout(STy);
        // Element offset is in bytes.
        if (!AccumulateOffset(
                APInt(Offset.getBitWidth(), SL->getElementOffset(ElementIdx)),
                1))
          return false;
        continue;
      }
      if (!AccumulateOffset(ConstOffset->getValue(),
                            GTI.getSequentialElementStride(DL)))
        return false;
      continue;
    }

    // The operand is not constant, check if an external analysis was provided.
    // External analsis is not applicable to a struct type.
    if (!ExternalAnalysis || STy || ScalableType)
      return false;
    APInt AnalysisIndex;
    if (!ExternalAnalysis(*V, AnalysisIndex))
      return false;
    UsedExternalAnalysis = true;
    if (!AccumulateOffset(AnalysisIndex, GTI.getSequentialElementStride(DL)))
      return false;
  }
  return true;
}

bool GEPOperator::collectOffset(
    const DataLayout &DL, unsigned BitWidth,
    MapVector<Value *, APInt> &VariableOffsets,
    APInt &ConstantOffset) const {
  assert(BitWidth == DL.getIndexSizeInBits(getPointerAddressSpace()) &&
         "The offset bit width does not match DL specification.");

  auto CollectConstantOffset = [&](APInt Index, uint64_t Size) {
    Index = Index.sextOrTrunc(BitWidth);
    APInt IndexedSize = APInt(BitWidth, Size);
    ConstantOffset += Index * IndexedSize;
  };

  for (gep_type_iterator GTI = gep_type_begin(this), GTE = gep_type_end(this);
       GTI != GTE; ++GTI) {
    // Scalable vectors are multiplied by a runtime constant.
    bool ScalableType = GTI.getIndexedType()->isScalableTy();

    Value *V = GTI.getOperand();
    StructType *STy = GTI.getStructTypeOrNull();
    // Handle ConstantInt if possible.
    if (auto ConstOffset = dyn_cast<ConstantInt>(V)) {
      if (ConstOffset->isZero())
        continue;
      // If the type is scalable and the constant is not zero (vscale * n * 0 =
      // 0) bailout.
      // TODO: If the runtime value is accessible at any point before DWARF
      // emission, then we could potentially keep a forward reference to it
      // in the debug value to be filled in later.
      if (ScalableType)
        return false;
      // Handle a struct index, which adds its field offset to the pointer.
      if (STy) {
        unsigned ElementIdx = ConstOffset->getZExtValue();
        const StructLayout *SL = DL.getStructLayout(STy);
        // Element offset is in bytes.
        CollectConstantOffset(APInt(BitWidth, SL->getElementOffset(ElementIdx)),
                              1);
        continue;
      }
      CollectConstantOffset(ConstOffset->getValue(),
                            GTI.getSequentialElementStride(DL));
      continue;
    }

    if (STy || ScalableType)
      return false;
    APInt IndexedSize = APInt(BitWidth, GTI.getSequentialElementStride(DL));
    // Insert an initial offset of 0 for V iff none exists already, then
    // increment the offset by IndexedSize.
    if (!IndexedSize.isZero()) {
      auto *It = VariableOffsets.insert({V, APInt(BitWidth, 0)}).first;
      It->second += IndexedSize;
    }
  }
  return true;
}

void FastMathFlags::print(raw_ostream &O) const {
  if (all())
    O << " fast";
  else {
    if (allowReassoc())
      O << " reassoc";
    if (noNaNs())
      O << " nnan";
    if (noInfs())
      O << " ninf";
    if (noSignedZeros())
      O << " nsz";
    if (allowReciprocal())
      O << " arcp";
    if (allowContract())
      O << " contract";
    if (approxFunc())
      O << " afn";
  }
}
} // namespace llvm
