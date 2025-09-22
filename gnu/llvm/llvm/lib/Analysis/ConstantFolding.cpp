//===-- ConstantFolding.cpp - Fold instructions into constants ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines routines for folding instructions into constants.
//
// Also, to supplement the basic IR ConstantExpr simplifications,
// this file defines some additional folding routines that can make use of
// DataLayout information. These functions cannot go in IR due to library
// dependency issues.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/Config/config.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantFold.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/IntrinsicsWebAssembly.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstdint>

using namespace llvm;

namespace {

//===----------------------------------------------------------------------===//
// Constant Folding internal helper functions
//===----------------------------------------------------------------------===//

static Constant *foldConstVectorToAPInt(APInt &Result, Type *DestTy,
                                        Constant *C, Type *SrcEltTy,
                                        unsigned NumSrcElts,
                                        const DataLayout &DL) {
  // Now that we know that the input value is a vector of integers, just shift
  // and insert them into our result.
  unsigned BitShift = DL.getTypeSizeInBits(SrcEltTy);
  for (unsigned i = 0; i != NumSrcElts; ++i) {
    Constant *Element;
    if (DL.isLittleEndian())
      Element = C->getAggregateElement(NumSrcElts - i - 1);
    else
      Element = C->getAggregateElement(i);

    if (Element && isa<UndefValue>(Element)) {
      Result <<= BitShift;
      continue;
    }

    auto *ElementCI = dyn_cast_or_null<ConstantInt>(Element);
    if (!ElementCI)
      return ConstantExpr::getBitCast(C, DestTy);

    Result <<= BitShift;
    Result |= ElementCI->getValue().zext(Result.getBitWidth());
  }

  return nullptr;
}

/// Constant fold bitcast, symbolically evaluating it with DataLayout.
/// This always returns a non-null constant, but it may be a
/// ConstantExpr if unfoldable.
Constant *FoldBitCast(Constant *C, Type *DestTy, const DataLayout &DL) {
  assert(CastInst::castIsValid(Instruction::BitCast, C, DestTy) &&
         "Invalid constantexpr bitcast!");

  // Catch the obvious splat cases.
  if (Constant *Res = ConstantFoldLoadFromUniformValue(C, DestTy, DL))
    return Res;

  if (auto *VTy = dyn_cast<VectorType>(C->getType())) {
    // Handle a vector->scalar integer/fp cast.
    if (isa<IntegerType>(DestTy) || DestTy->isFloatingPointTy()) {
      unsigned NumSrcElts = cast<FixedVectorType>(VTy)->getNumElements();
      Type *SrcEltTy = VTy->getElementType();

      // If the vector is a vector of floating point, convert it to vector of int
      // to simplify things.
      if (SrcEltTy->isFloatingPointTy()) {
        unsigned FPWidth = SrcEltTy->getPrimitiveSizeInBits();
        auto *SrcIVTy = FixedVectorType::get(
            IntegerType::get(C->getContext(), FPWidth), NumSrcElts);
        // Ask IR to do the conversion now that #elts line up.
        C = ConstantExpr::getBitCast(C, SrcIVTy);
      }

      APInt Result(DL.getTypeSizeInBits(DestTy), 0);
      if (Constant *CE = foldConstVectorToAPInt(Result, DestTy, C,
                                                SrcEltTy, NumSrcElts, DL))
        return CE;

      if (isa<IntegerType>(DestTy))
        return ConstantInt::get(DestTy, Result);

      APFloat FP(DestTy->getFltSemantics(), Result);
      return ConstantFP::get(DestTy->getContext(), FP);
    }
  }

  // The code below only handles casts to vectors currently.
  auto *DestVTy = dyn_cast<VectorType>(DestTy);
  if (!DestVTy)
    return ConstantExpr::getBitCast(C, DestTy);

  // If this is a scalar -> vector cast, convert the input into a <1 x scalar>
  // vector so the code below can handle it uniformly.
  if (isa<ConstantFP>(C) || isa<ConstantInt>(C)) {
    Constant *Ops = C; // don't take the address of C!
    return FoldBitCast(ConstantVector::get(Ops), DestTy, DL);
  }

  // If this is a bitcast from constant vector -> vector, fold it.
  if (!isa<ConstantDataVector>(C) && !isa<ConstantVector>(C))
    return ConstantExpr::getBitCast(C, DestTy);

  // If the element types match, IR can fold it.
  unsigned NumDstElt = cast<FixedVectorType>(DestVTy)->getNumElements();
  unsigned NumSrcElt = cast<FixedVectorType>(C->getType())->getNumElements();
  if (NumDstElt == NumSrcElt)
    return ConstantExpr::getBitCast(C, DestTy);

  Type *SrcEltTy = cast<VectorType>(C->getType())->getElementType();
  Type *DstEltTy = DestVTy->getElementType();

  // Otherwise, we're changing the number of elements in a vector, which
  // requires endianness information to do the right thing.  For example,
  //    bitcast (<2 x i64> <i64 0, i64 1> to <4 x i32>)
  // folds to (little endian):
  //    <4 x i32> <i32 0, i32 0, i32 1, i32 0>
  // and to (big endian):
  //    <4 x i32> <i32 0, i32 0, i32 0, i32 1>

  // First thing is first.  We only want to think about integer here, so if
  // we have something in FP form, recast it as integer.
  if (DstEltTy->isFloatingPointTy()) {
    // Fold to an vector of integers with same size as our FP type.
    unsigned FPWidth = DstEltTy->getPrimitiveSizeInBits();
    auto *DestIVTy = FixedVectorType::get(
        IntegerType::get(C->getContext(), FPWidth), NumDstElt);
    // Recursively handle this integer conversion, if possible.
    C = FoldBitCast(C, DestIVTy, DL);

    // Finally, IR can handle this now that #elts line up.
    return ConstantExpr::getBitCast(C, DestTy);
  }

  // Okay, we know the destination is integer, if the input is FP, convert
  // it to integer first.
  if (SrcEltTy->isFloatingPointTy()) {
    unsigned FPWidth = SrcEltTy->getPrimitiveSizeInBits();
    auto *SrcIVTy = FixedVectorType::get(
        IntegerType::get(C->getContext(), FPWidth), NumSrcElt);
    // Ask IR to do the conversion now that #elts line up.
    C = ConstantExpr::getBitCast(C, SrcIVTy);
    // If IR wasn't able to fold it, bail out.
    if (!isa<ConstantVector>(C) &&  // FIXME: Remove ConstantVector.
        !isa<ConstantDataVector>(C))
      return C;
  }

  // Now we know that the input and output vectors are both integer vectors
  // of the same size, and that their #elements is not the same.  Do the
  // conversion here, which depends on whether the input or output has
  // more elements.
  bool isLittleEndian = DL.isLittleEndian();

  SmallVector<Constant*, 32> Result;
  if (NumDstElt < NumSrcElt) {
    // Handle: bitcast (<4 x i32> <i32 0, i32 1, i32 2, i32 3> to <2 x i64>)
    Constant *Zero = Constant::getNullValue(DstEltTy);
    unsigned Ratio = NumSrcElt/NumDstElt;
    unsigned SrcBitSize = SrcEltTy->getPrimitiveSizeInBits();
    unsigned SrcElt = 0;
    for (unsigned i = 0; i != NumDstElt; ++i) {
      // Build each element of the result.
      Constant *Elt = Zero;
      unsigned ShiftAmt = isLittleEndian ? 0 : SrcBitSize*(Ratio-1);
      for (unsigned j = 0; j != Ratio; ++j) {
        Constant *Src = C->getAggregateElement(SrcElt++);
        if (Src && isa<UndefValue>(Src))
          Src = Constant::getNullValue(
              cast<VectorType>(C->getType())->getElementType());
        else
          Src = dyn_cast_or_null<ConstantInt>(Src);
        if (!Src)  // Reject constantexpr elements.
          return ConstantExpr::getBitCast(C, DestTy);

        // Zero extend the element to the right size.
        Src = ConstantFoldCastOperand(Instruction::ZExt, Src, Elt->getType(),
                                      DL);
        assert(Src && "Constant folding cannot fail on plain integers");

        // Shift it to the right place, depending on endianness.
        Src = ConstantFoldBinaryOpOperands(
            Instruction::Shl, Src, ConstantInt::get(Src->getType(), ShiftAmt),
            DL);
        assert(Src && "Constant folding cannot fail on plain integers");

        ShiftAmt += isLittleEndian ? SrcBitSize : -SrcBitSize;

        // Mix it in.
        Elt = ConstantFoldBinaryOpOperands(Instruction::Or, Elt, Src, DL);
        assert(Elt && "Constant folding cannot fail on plain integers");
      }
      Result.push_back(Elt);
    }
    return ConstantVector::get(Result);
  }

  // Handle: bitcast (<2 x i64> <i64 0, i64 1> to <4 x i32>)
  unsigned Ratio = NumDstElt/NumSrcElt;
  unsigned DstBitSize = DL.getTypeSizeInBits(DstEltTy);

  // Loop over each source value, expanding into multiple results.
  for (unsigned i = 0; i != NumSrcElt; ++i) {
    auto *Element = C->getAggregateElement(i);

    if (!Element) // Reject constantexpr elements.
      return ConstantExpr::getBitCast(C, DestTy);

    if (isa<UndefValue>(Element)) {
      // Correctly Propagate undef values.
      Result.append(Ratio, UndefValue::get(DstEltTy));
      continue;
    }

    auto *Src = dyn_cast<ConstantInt>(Element);
    if (!Src)
      return ConstantExpr::getBitCast(C, DestTy);

    unsigned ShiftAmt = isLittleEndian ? 0 : DstBitSize*(Ratio-1);
    for (unsigned j = 0; j != Ratio; ++j) {
      // Shift the piece of the value into the right place, depending on
      // endianness.
      APInt Elt = Src->getValue().lshr(ShiftAmt);
      ShiftAmt += isLittleEndian ? DstBitSize : -DstBitSize;

      // Truncate and remember this piece.
      Result.push_back(ConstantInt::get(DstEltTy, Elt.trunc(DstBitSize)));
    }
  }

  return ConstantVector::get(Result);
}

} // end anonymous namespace

/// If this constant is a constant offset from a global, return the global and
/// the constant. Because of constantexprs, this function is recursive.
bool llvm::IsConstantOffsetFromGlobal(Constant *C, GlobalValue *&GV,
                                      APInt &Offset, const DataLayout &DL,
                                      DSOLocalEquivalent **DSOEquiv) {
  if (DSOEquiv)
    *DSOEquiv = nullptr;

  // Trivial case, constant is the global.
  if ((GV = dyn_cast<GlobalValue>(C))) {
    unsigned BitWidth = DL.getIndexTypeSizeInBits(GV->getType());
    Offset = APInt(BitWidth, 0);
    return true;
  }

  if (auto *FoundDSOEquiv = dyn_cast<DSOLocalEquivalent>(C)) {
    if (DSOEquiv)
      *DSOEquiv = FoundDSOEquiv;
    GV = FoundDSOEquiv->getGlobalValue();
    unsigned BitWidth = DL.getIndexTypeSizeInBits(GV->getType());
    Offset = APInt(BitWidth, 0);
    return true;
  }

  // Otherwise, if this isn't a constant expr, bail out.
  auto *CE = dyn_cast<ConstantExpr>(C);
  if (!CE) return false;

  // Look through ptr->int and ptr->ptr casts.
  if (CE->getOpcode() == Instruction::PtrToInt ||
      CE->getOpcode() == Instruction::BitCast)
    return IsConstantOffsetFromGlobal(CE->getOperand(0), GV, Offset, DL,
                                      DSOEquiv);

  // i32* getelementptr ([5 x i32]* @a, i32 0, i32 5)
  auto *GEP = dyn_cast<GEPOperator>(CE);
  if (!GEP)
    return false;

  unsigned BitWidth = DL.getIndexTypeSizeInBits(GEP->getType());
  APInt TmpOffset(BitWidth, 0);

  // If the base isn't a global+constant, we aren't either.
  if (!IsConstantOffsetFromGlobal(CE->getOperand(0), GV, TmpOffset, DL,
                                  DSOEquiv))
    return false;

  // Otherwise, add any offset that our operands provide.
  if (!GEP->accumulateConstantOffset(DL, TmpOffset))
    return false;

  Offset = TmpOffset;
  return true;
}

Constant *llvm::ConstantFoldLoadThroughBitcast(Constant *C, Type *DestTy,
                                               const DataLayout &DL) {
  do {
    Type *SrcTy = C->getType();
    if (SrcTy == DestTy)
      return C;

    TypeSize DestSize = DL.getTypeSizeInBits(DestTy);
    TypeSize SrcSize = DL.getTypeSizeInBits(SrcTy);
    if (!TypeSize::isKnownGE(SrcSize, DestSize))
      return nullptr;

    // Catch the obvious splat cases (since all-zeros can coerce non-integral
    // pointers legally).
    if (Constant *Res = ConstantFoldLoadFromUniformValue(C, DestTy, DL))
      return Res;

    // If the type sizes are the same and a cast is legal, just directly
    // cast the constant.
    // But be careful not to coerce non-integral pointers illegally.
    if (SrcSize == DestSize &&
        DL.isNonIntegralPointerType(SrcTy->getScalarType()) ==
            DL.isNonIntegralPointerType(DestTy->getScalarType())) {
      Instruction::CastOps Cast = Instruction::BitCast;
      // If we are going from a pointer to int or vice versa, we spell the cast
      // differently.
      if (SrcTy->isIntegerTy() && DestTy->isPointerTy())
        Cast = Instruction::IntToPtr;
      else if (SrcTy->isPointerTy() && DestTy->isIntegerTy())
        Cast = Instruction::PtrToInt;

      if (CastInst::castIsValid(Cast, C, DestTy))
        return ConstantFoldCastOperand(Cast, C, DestTy, DL);
    }

    // If this isn't an aggregate type, there is nothing we can do to drill down
    // and find a bitcastable constant.
    if (!SrcTy->isAggregateType() && !SrcTy->isVectorTy())
      return nullptr;

    // We're simulating a load through a pointer that was bitcast to point to
    // a different type, so we can try to walk down through the initial
    // elements of an aggregate to see if some part of the aggregate is
    // castable to implement the "load" semantic model.
    if (SrcTy->isStructTy()) {
      // Struct types might have leading zero-length elements like [0 x i32],
      // which are certainly not what we are looking for, so skip them.
      unsigned Elem = 0;
      Constant *ElemC;
      do {
        ElemC = C->getAggregateElement(Elem++);
      } while (ElemC && DL.getTypeSizeInBits(ElemC->getType()).isZero());
      C = ElemC;
    } else {
      // For non-byte-sized vector elements, the first element is not
      // necessarily located at the vector base address.
      if (auto *VT = dyn_cast<VectorType>(SrcTy))
        if (!DL.typeSizeEqualsStoreSize(VT->getElementType()))
          return nullptr;

      C = C->getAggregateElement(0u);
    }
  } while (C);

  return nullptr;
}

namespace {

/// Recursive helper to read bits out of global. C is the constant being copied
/// out of. ByteOffset is an offset into C. CurPtr is the pointer to copy
/// results into and BytesLeft is the number of bytes left in
/// the CurPtr buffer. DL is the DataLayout.
bool ReadDataFromGlobal(Constant *C, uint64_t ByteOffset, unsigned char *CurPtr,
                        unsigned BytesLeft, const DataLayout &DL) {
  assert(ByteOffset <= DL.getTypeAllocSize(C->getType()) &&
         "Out of range access");

  // If this element is zero or undefined, we can just return since *CurPtr is
  // zero initialized.
  if (isa<ConstantAggregateZero>(C) || isa<UndefValue>(C))
    return true;

  if (auto *CI = dyn_cast<ConstantInt>(C)) {
    if ((CI->getBitWidth() & 7) != 0)
      return false;
    const APInt &Val = CI->getValue();
    unsigned IntBytes = unsigned(CI->getBitWidth()/8);

    for (unsigned i = 0; i != BytesLeft && ByteOffset != IntBytes; ++i) {
      unsigned n = ByteOffset;
      if (!DL.isLittleEndian())
        n = IntBytes - n - 1;
      CurPtr[i] = Val.extractBits(8, n * 8).getZExtValue();
      ++ByteOffset;
    }
    return true;
  }

  if (auto *CFP = dyn_cast<ConstantFP>(C)) {
    if (CFP->getType()->isDoubleTy()) {
      C = FoldBitCast(C, Type::getInt64Ty(C->getContext()), DL);
      return ReadDataFromGlobal(C, ByteOffset, CurPtr, BytesLeft, DL);
    }
    if (CFP->getType()->isFloatTy()){
      C = FoldBitCast(C, Type::getInt32Ty(C->getContext()), DL);
      return ReadDataFromGlobal(C, ByteOffset, CurPtr, BytesLeft, DL);
    }
    if (CFP->getType()->isHalfTy()){
      C = FoldBitCast(C, Type::getInt16Ty(C->getContext()), DL);
      return ReadDataFromGlobal(C, ByteOffset, CurPtr, BytesLeft, DL);
    }
    return false;
  }

  if (auto *CS = dyn_cast<ConstantStruct>(C)) {
    const StructLayout *SL = DL.getStructLayout(CS->getType());
    unsigned Index = SL->getElementContainingOffset(ByteOffset);
    uint64_t CurEltOffset = SL->getElementOffset(Index);
    ByteOffset -= CurEltOffset;

    while (true) {
      // If the element access is to the element itself and not to tail padding,
      // read the bytes from the element.
      uint64_t EltSize = DL.getTypeAllocSize(CS->getOperand(Index)->getType());

      if (ByteOffset < EltSize &&
          !ReadDataFromGlobal(CS->getOperand(Index), ByteOffset, CurPtr,
                              BytesLeft, DL))
        return false;

      ++Index;

      // Check to see if we read from the last struct element, if so we're done.
      if (Index == CS->getType()->getNumElements())
        return true;

      // If we read all of the bytes we needed from this element we're done.
      uint64_t NextEltOffset = SL->getElementOffset(Index);

      if (BytesLeft <= NextEltOffset - CurEltOffset - ByteOffset)
        return true;

      // Move to the next element of the struct.
      CurPtr += NextEltOffset - CurEltOffset - ByteOffset;
      BytesLeft -= NextEltOffset - CurEltOffset - ByteOffset;
      ByteOffset = 0;
      CurEltOffset = NextEltOffset;
    }
    // not reached.
  }

  if (isa<ConstantArray>(C) || isa<ConstantVector>(C) ||
      isa<ConstantDataSequential>(C)) {
    uint64_t NumElts, EltSize;
    Type *EltTy;
    if (auto *AT = dyn_cast<ArrayType>(C->getType())) {
      NumElts = AT->getNumElements();
      EltTy = AT->getElementType();
      EltSize = DL.getTypeAllocSize(EltTy);
    } else {
      NumElts = cast<FixedVectorType>(C->getType())->getNumElements();
      EltTy = cast<FixedVectorType>(C->getType())->getElementType();
      // TODO: For non-byte-sized vectors, current implementation assumes there is
      // padding to the next byte boundary between elements.
      if (!DL.typeSizeEqualsStoreSize(EltTy))
        return false;

      EltSize = DL.getTypeStoreSize(EltTy);
    }
    uint64_t Index = ByteOffset / EltSize;
    uint64_t Offset = ByteOffset - Index * EltSize;

    for (; Index != NumElts; ++Index) {
      if (!ReadDataFromGlobal(C->getAggregateElement(Index), Offset, CurPtr,
                              BytesLeft, DL))
        return false;

      uint64_t BytesWritten = EltSize - Offset;
      assert(BytesWritten <= EltSize && "Not indexing into this element?");
      if (BytesWritten >= BytesLeft)
        return true;

      Offset = 0;
      BytesLeft -= BytesWritten;
      CurPtr += BytesWritten;
    }
    return true;
  }

  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    if (CE->getOpcode() == Instruction::IntToPtr &&
        CE->getOperand(0)->getType() == DL.getIntPtrType(CE->getType())) {
      return ReadDataFromGlobal(CE->getOperand(0), ByteOffset, CurPtr,
                                BytesLeft, DL);
    }
  }

  // Otherwise, unknown initializer type.
  return false;
}

Constant *FoldReinterpretLoadFromConst(Constant *C, Type *LoadTy,
                                       int64_t Offset, const DataLayout &DL) {
  // Bail out early. Not expect to load from scalable global variable.
  if (isa<ScalableVectorType>(LoadTy))
    return nullptr;

  auto *IntType = dyn_cast<IntegerType>(LoadTy);

  // If this isn't an integer load we can't fold it directly.
  if (!IntType) {
    // If this is a non-integer load, we can try folding it as an int load and
    // then bitcast the result.  This can be useful for union cases.  Note
    // that address spaces don't matter here since we're not going to result in
    // an actual new load.
    if (!LoadTy->isFloatingPointTy() && !LoadTy->isPointerTy() &&
        !LoadTy->isVectorTy())
      return nullptr;

    Type *MapTy = Type::getIntNTy(C->getContext(),
                                  DL.getTypeSizeInBits(LoadTy).getFixedValue());
    if (Constant *Res = FoldReinterpretLoadFromConst(C, MapTy, Offset, DL)) {
      if (Res->isNullValue() && !LoadTy->isX86_MMXTy() &&
          !LoadTy->isX86_AMXTy())
        // Materializing a zero can be done trivially without a bitcast
        return Constant::getNullValue(LoadTy);
      Type *CastTy = LoadTy->isPtrOrPtrVectorTy() ? DL.getIntPtrType(LoadTy) : LoadTy;
      Res = FoldBitCast(Res, CastTy, DL);
      if (LoadTy->isPtrOrPtrVectorTy()) {
        // For vector of pointer, we needed to first convert to a vector of integer, then do vector inttoptr
        if (Res->isNullValue() && !LoadTy->isX86_MMXTy() &&
            !LoadTy->isX86_AMXTy())
          return Constant::getNullValue(LoadTy);
        if (DL.isNonIntegralPointerType(LoadTy->getScalarType()))
          // Be careful not to replace a load of an addrspace value with an inttoptr here
          return nullptr;
        Res = ConstantExpr::getIntToPtr(Res, LoadTy);
      }
      return Res;
    }
    return nullptr;
  }

  unsigned BytesLoaded = (IntType->getBitWidth() + 7) / 8;
  if (BytesLoaded > 32 || BytesLoaded == 0)
    return nullptr;

  // If we're not accessing anything in this constant, the result is undefined.
  if (Offset <= -1 * static_cast<int64_t>(BytesLoaded))
    return PoisonValue::get(IntType);

  // TODO: We should be able to support scalable types.
  TypeSize InitializerSize = DL.getTypeAllocSize(C->getType());
  if (InitializerSize.isScalable())
    return nullptr;

  // If we're not accessing anything in this constant, the result is undefined.
  if (Offset >= (int64_t)InitializerSize.getFixedValue())
    return PoisonValue::get(IntType);

  unsigned char RawBytes[32] = {0};
  unsigned char *CurPtr = RawBytes;
  unsigned BytesLeft = BytesLoaded;

  // If we're loading off the beginning of the global, some bytes may be valid.
  if (Offset < 0) {
    CurPtr += -Offset;
    BytesLeft += Offset;
    Offset = 0;
  }

  if (!ReadDataFromGlobal(C, Offset, CurPtr, BytesLeft, DL))
    return nullptr;

  APInt ResultVal = APInt(IntType->getBitWidth(), 0);
  if (DL.isLittleEndian()) {
    ResultVal = RawBytes[BytesLoaded - 1];
    for (unsigned i = 1; i != BytesLoaded; ++i) {
      ResultVal <<= 8;
      ResultVal |= RawBytes[BytesLoaded - 1 - i];
    }
  } else {
    ResultVal = RawBytes[0];
    for (unsigned i = 1; i != BytesLoaded; ++i) {
      ResultVal <<= 8;
      ResultVal |= RawBytes[i];
    }
  }

  return ConstantInt::get(IntType->getContext(), ResultVal);
}

} // anonymous namespace

// If GV is a constant with an initializer read its representation starting
// at Offset and return it as a constant array of unsigned char.  Otherwise
// return null.
Constant *llvm::ReadByteArrayFromGlobal(const GlobalVariable *GV,
                                        uint64_t Offset) {
  if (!GV->isConstant() || !GV->hasDefinitiveInitializer())
    return nullptr;

  const DataLayout &DL = GV->getDataLayout();
  Constant *Init = const_cast<Constant *>(GV->getInitializer());
  TypeSize InitSize = DL.getTypeAllocSize(Init->getType());
  if (InitSize < Offset)
    return nullptr;

  uint64_t NBytes = InitSize - Offset;
  if (NBytes > UINT16_MAX)
    // Bail for large initializers in excess of 64K to avoid allocating
    // too much memory.
    // Offset is assumed to be less than or equal than InitSize (this
    // is enforced in ReadDataFromGlobal).
    return nullptr;

  SmallVector<unsigned char, 256> RawBytes(static_cast<size_t>(NBytes));
  unsigned char *CurPtr = RawBytes.data();

  if (!ReadDataFromGlobal(Init, Offset, CurPtr, NBytes, DL))
    return nullptr;

  return ConstantDataArray::get(GV->getContext(), RawBytes);
}

/// If this Offset points exactly to the start of an aggregate element, return
/// that element, otherwise return nullptr.
Constant *getConstantAtOffset(Constant *Base, APInt Offset,
                              const DataLayout &DL) {
  if (Offset.isZero())
    return Base;

  if (!isa<ConstantAggregate>(Base) && !isa<ConstantDataSequential>(Base))
    return nullptr;

  Type *ElemTy = Base->getType();
  SmallVector<APInt> Indices = DL.getGEPIndicesForOffset(ElemTy, Offset);
  if (!Offset.isZero() || !Indices[0].isZero())
    return nullptr;

  Constant *C = Base;
  for (const APInt &Index : drop_begin(Indices)) {
    if (Index.isNegative() || Index.getActiveBits() >= 32)
      return nullptr;

    C = C->getAggregateElement(Index.getZExtValue());
    if (!C)
      return nullptr;
  }

  return C;
}

Constant *llvm::ConstantFoldLoadFromConst(Constant *C, Type *Ty,
                                          const APInt &Offset,
                                          const DataLayout &DL) {
  if (Constant *AtOffset = getConstantAtOffset(C, Offset, DL))
    if (Constant *Result = ConstantFoldLoadThroughBitcast(AtOffset, Ty, DL))
      return Result;

  // Explicitly check for out-of-bounds access, so we return poison even if the
  // constant is a uniform value.
  TypeSize Size = DL.getTypeAllocSize(C->getType());
  if (!Size.isScalable() && Offset.sge(Size.getFixedValue()))
    return PoisonValue::get(Ty);

  // Try an offset-independent fold of a uniform value.
  if (Constant *Result = ConstantFoldLoadFromUniformValue(C, Ty, DL))
    return Result;

  // Try hard to fold loads from bitcasted strange and non-type-safe things.
  if (Offset.getSignificantBits() <= 64)
    if (Constant *Result =
            FoldReinterpretLoadFromConst(C, Ty, Offset.getSExtValue(), DL))
      return Result;

  return nullptr;
}

Constant *llvm::ConstantFoldLoadFromConst(Constant *C, Type *Ty,
                                          const DataLayout &DL) {
  return ConstantFoldLoadFromConst(C, Ty, APInt(64, 0), DL);
}

Constant *llvm::ConstantFoldLoadFromConstPtr(Constant *C, Type *Ty,
                                             APInt Offset,
                                             const DataLayout &DL) {
  // We can only fold loads from constant globals with a definitive initializer.
  // Check this upfront, to skip expensive offset calculations.
  auto *GV = dyn_cast<GlobalVariable>(getUnderlyingObject(C));
  if (!GV || !GV->isConstant() || !GV->hasDefinitiveInitializer())
    return nullptr;

  C = cast<Constant>(C->stripAndAccumulateConstantOffsets(
          DL, Offset, /* AllowNonInbounds */ true));

  if (C == GV)
    if (Constant *Result = ConstantFoldLoadFromConst(GV->getInitializer(), Ty,
                                                     Offset, DL))
      return Result;

  // If this load comes from anywhere in a uniform constant global, the value
  // is always the same, regardless of the loaded offset.
  return ConstantFoldLoadFromUniformValue(GV->getInitializer(), Ty, DL);
}

Constant *llvm::ConstantFoldLoadFromConstPtr(Constant *C, Type *Ty,
                                             const DataLayout &DL) {
  APInt Offset(DL.getIndexTypeSizeInBits(C->getType()), 0);
  return ConstantFoldLoadFromConstPtr(C, Ty, std::move(Offset), DL);
}

Constant *llvm::ConstantFoldLoadFromUniformValue(Constant *C, Type *Ty,
                                                 const DataLayout &DL) {
  if (isa<PoisonValue>(C))
    return PoisonValue::get(Ty);
  if (isa<UndefValue>(C))
    return UndefValue::get(Ty);
  // If padding is needed when storing C to memory, then it isn't considered as
  // uniform.
  if (!DL.typeSizeEqualsStoreSize(C->getType()))
    return nullptr;
  if (C->isNullValue() && !Ty->isX86_MMXTy() && !Ty->isX86_AMXTy())
    return Constant::getNullValue(Ty);
  if (C->isAllOnesValue() &&
      (Ty->isIntOrIntVectorTy() || Ty->isFPOrFPVectorTy()))
    return Constant::getAllOnesValue(Ty);
  return nullptr;
}

namespace {

/// One of Op0/Op1 is a constant expression.
/// Attempt to symbolically evaluate the result of a binary operator merging
/// these together.  If target data info is available, it is provided as DL,
/// otherwise DL is null.
Constant *SymbolicallyEvaluateBinop(unsigned Opc, Constant *Op0, Constant *Op1,
                                    const DataLayout &DL) {
  // SROA

  // Fold (and 0xffffffff00000000, (shl x, 32)) -> shl.
  // Fold (lshr (or X, Y), 32) -> (lshr [X/Y], 32) if one doesn't contribute
  // bits.

  if (Opc == Instruction::And) {
    KnownBits Known0 = computeKnownBits(Op0, DL);
    KnownBits Known1 = computeKnownBits(Op1, DL);
    if ((Known1.One | Known0.Zero).isAllOnes()) {
      // All the bits of Op0 that the 'and' could be masking are already zero.
      return Op0;
    }
    if ((Known0.One | Known1.Zero).isAllOnes()) {
      // All the bits of Op1 that the 'and' could be masking are already zero.
      return Op1;
    }

    Known0 &= Known1;
    if (Known0.isConstant())
      return ConstantInt::get(Op0->getType(), Known0.getConstant());
  }

  // If the constant expr is something like &A[123] - &A[4].f, fold this into a
  // constant.  This happens frequently when iterating over a global array.
  if (Opc == Instruction::Sub) {
    GlobalValue *GV1, *GV2;
    APInt Offs1, Offs2;

    if (IsConstantOffsetFromGlobal(Op0, GV1, Offs1, DL))
      if (IsConstantOffsetFromGlobal(Op1, GV2, Offs2, DL) && GV1 == GV2) {
        unsigned OpSize = DL.getTypeSizeInBits(Op0->getType());

        // (&GV+C1) - (&GV+C2) -> C1-C2, pointer arithmetic cannot overflow.
        // PtrToInt may change the bitwidth so we have convert to the right size
        // first.
        return ConstantInt::get(Op0->getType(), Offs1.zextOrTrunc(OpSize) -
                                                Offs2.zextOrTrunc(OpSize));
      }
  }

  return nullptr;
}

/// If array indices are not pointer-sized integers, explicitly cast them so
/// that they aren't implicitly casted by the getelementptr.
Constant *CastGEPIndices(Type *SrcElemTy, ArrayRef<Constant *> Ops,
                         Type *ResultTy, GEPNoWrapFlags NW,
                         std::optional<ConstantRange> InRange,
                         const DataLayout &DL, const TargetLibraryInfo *TLI) {
  Type *IntIdxTy = DL.getIndexType(ResultTy);
  Type *IntIdxScalarTy = IntIdxTy->getScalarType();

  bool Any = false;
  SmallVector<Constant*, 32> NewIdxs;
  for (unsigned i = 1, e = Ops.size(); i != e; ++i) {
    if ((i == 1 ||
         !isa<StructType>(GetElementPtrInst::getIndexedType(
             SrcElemTy, Ops.slice(1, i - 1)))) &&
        Ops[i]->getType()->getScalarType() != IntIdxScalarTy) {
      Any = true;
      Type *NewType =
          Ops[i]->getType()->isVectorTy() ? IntIdxTy : IntIdxScalarTy;
      Constant *NewIdx = ConstantFoldCastOperand(
          CastInst::getCastOpcode(Ops[i], true, NewType, true), Ops[i], NewType,
          DL);
      if (!NewIdx)
        return nullptr;
      NewIdxs.push_back(NewIdx);
    } else
      NewIdxs.push_back(Ops[i]);
  }

  if (!Any)
    return nullptr;

  Constant *C =
      ConstantExpr::getGetElementPtr(SrcElemTy, Ops[0], NewIdxs, NW, InRange);
  return ConstantFoldConstant(C, DL, TLI);
}

/// If we can symbolically evaluate the GEP constant expression, do so.
Constant *SymbolicallyEvaluateGEP(const GEPOperator *GEP,
                                  ArrayRef<Constant *> Ops,
                                  const DataLayout &DL,
                                  const TargetLibraryInfo *TLI) {
  Type *SrcElemTy = GEP->getSourceElementType();
  Type *ResTy = GEP->getType();
  if (!SrcElemTy->isSized() || isa<ScalableVectorType>(SrcElemTy))
    return nullptr;

  if (Constant *C = CastGEPIndices(SrcElemTy, Ops, ResTy, GEP->getNoWrapFlags(),
                                   GEP->getInRange(), DL, TLI))
    return C;

  Constant *Ptr = Ops[0];
  if (!Ptr->getType()->isPointerTy())
    return nullptr;

  Type *IntIdxTy = DL.getIndexType(Ptr->getType());

  for (unsigned i = 1, e = Ops.size(); i != e; ++i)
    if (!isa<ConstantInt>(Ops[i]))
      return nullptr;

  unsigned BitWidth = DL.getTypeSizeInBits(IntIdxTy);
  APInt Offset = APInt(
      BitWidth,
      DL.getIndexedOffsetInType(
          SrcElemTy, ArrayRef((Value *const *)Ops.data() + 1, Ops.size() - 1)));

  std::optional<ConstantRange> InRange = GEP->getInRange();
  if (InRange)
    InRange = InRange->sextOrTrunc(BitWidth);

  // If this is a GEP of a GEP, fold it all into a single GEP.
  GEPNoWrapFlags NW = GEP->getNoWrapFlags();
  bool Overflow = false;
  while (auto *GEP = dyn_cast<GEPOperator>(Ptr)) {
    NW &= GEP->getNoWrapFlags();

    SmallVector<Value *, 4> NestedOps(llvm::drop_begin(GEP->operands()));

    // Do not try the incorporate the sub-GEP if some index is not a number.
    bool AllConstantInt = true;
    for (Value *NestedOp : NestedOps)
      if (!isa<ConstantInt>(NestedOp)) {
        AllConstantInt = false;
        break;
      }
    if (!AllConstantInt)
      break;

    // TODO: Try to intersect two inrange attributes?
    if (!InRange) {
      InRange = GEP->getInRange();
      if (InRange)
        // Adjust inrange by offset until now.
        InRange = InRange->sextOrTrunc(BitWidth).subtract(Offset);
    }

    Ptr = cast<Constant>(GEP->getOperand(0));
    SrcElemTy = GEP->getSourceElementType();
    Offset = Offset.sadd_ov(
        APInt(BitWidth, DL.getIndexedOffsetInType(SrcElemTy, NestedOps)),
        Overflow);
  }

  // Preserving nusw (without inbounds) also requires that the offset
  // additions did not overflow.
  if (NW.hasNoUnsignedSignedWrap() && !NW.isInBounds() && Overflow)
    NW = NW.withoutNoUnsignedSignedWrap();

  // If the base value for this address is a literal integer value, fold the
  // getelementptr to the resulting integer value casted to the pointer type.
  APInt BasePtr(BitWidth, 0);
  if (auto *CE = dyn_cast<ConstantExpr>(Ptr)) {
    if (CE->getOpcode() == Instruction::IntToPtr) {
      if (auto *Base = dyn_cast<ConstantInt>(CE->getOperand(0)))
        BasePtr = Base->getValue().zextOrTrunc(BitWidth);
    }
  }

  auto *PTy = cast<PointerType>(Ptr->getType());
  if ((Ptr->isNullValue() || BasePtr != 0) &&
      !DL.isNonIntegralPointerType(PTy)) {
    Constant *C = ConstantInt::get(Ptr->getContext(), Offset + BasePtr);
    return ConstantExpr::getIntToPtr(C, ResTy);
  }

  // Try to infer inbounds for GEPs of globals.
  // TODO(gep_nowrap): Also infer nuw flag.
  if (!NW.isInBounds() && Offset.isNonNegative()) {
    bool CanBeNull, CanBeFreed;
    uint64_t DerefBytes =
        Ptr->getPointerDereferenceableBytes(DL, CanBeNull, CanBeFreed);
    if (DerefBytes != 0 && !CanBeNull && Offset.sle(DerefBytes))
      NW |= GEPNoWrapFlags::inBounds();
  }

  // Otherwise canonicalize this to a single ptradd.
  LLVMContext &Ctx = Ptr->getContext();
  return ConstantExpr::getGetElementPtr(Type::getInt8Ty(Ctx), Ptr,
                                        ConstantInt::get(Ctx, Offset), NW,
                                        InRange);
}

/// Attempt to constant fold an instruction with the
/// specified opcode and operands.  If successful, the constant result is
/// returned, if not, null is returned.  Note that this function can fail when
/// attempting to fold instructions like loads and stores, which have no
/// constant expression form.
Constant *ConstantFoldInstOperandsImpl(const Value *InstOrCE, unsigned Opcode,
                                       ArrayRef<Constant *> Ops,
                                       const DataLayout &DL,
                                       const TargetLibraryInfo *TLI,
                                       bool AllowNonDeterministic) {
  Type *DestTy = InstOrCE->getType();

  if (Instruction::isUnaryOp(Opcode))
    return ConstantFoldUnaryOpOperand(Opcode, Ops[0], DL);

  if (Instruction::isBinaryOp(Opcode)) {
    switch (Opcode) {
    default:
      break;
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
      // Handle floating point instructions separately to account for denormals
      // TODO: If a constant expression is being folded rather than an
      // instruction, denormals will not be flushed/treated as zero
      if (const auto *I = dyn_cast<Instruction>(InstOrCE)) {
        return ConstantFoldFPInstOperands(Opcode, Ops[0], Ops[1], DL, I,
                                          AllowNonDeterministic);
      }
    }
    return ConstantFoldBinaryOpOperands(Opcode, Ops[0], Ops[1], DL);
  }

  if (Instruction::isCast(Opcode))
    return ConstantFoldCastOperand(Opcode, Ops[0], DestTy, DL);

  if (auto *GEP = dyn_cast<GEPOperator>(InstOrCE)) {
    Type *SrcElemTy = GEP->getSourceElementType();
    if (!ConstantExpr::isSupportedGetElementPtr(SrcElemTy))
      return nullptr;

    if (Constant *C = SymbolicallyEvaluateGEP(GEP, Ops, DL, TLI))
      return C;

    return ConstantExpr::getGetElementPtr(SrcElemTy, Ops[0], Ops.slice(1),
                                          GEP->getNoWrapFlags(),
                                          GEP->getInRange());
  }

  if (auto *CE = dyn_cast<ConstantExpr>(InstOrCE))
    return CE->getWithOperands(Ops);

  switch (Opcode) {
  default: return nullptr;
  case Instruction::ICmp:
  case Instruction::FCmp: {
    auto *C = cast<CmpInst>(InstOrCE);
    return ConstantFoldCompareInstOperands(C->getPredicate(), Ops[0], Ops[1],
                                           DL, TLI, C);
  }
  case Instruction::Freeze:
    return isGuaranteedNotToBeUndefOrPoison(Ops[0]) ? Ops[0] : nullptr;
  case Instruction::Call:
    if (auto *F = dyn_cast<Function>(Ops.back())) {
      const auto *Call = cast<CallBase>(InstOrCE);
      if (canConstantFoldCallTo(Call, F))
        return ConstantFoldCall(Call, F, Ops.slice(0, Ops.size() - 1), TLI,
                                AllowNonDeterministic);
    }
    return nullptr;
  case Instruction::Select:
    return ConstantFoldSelectInstruction(Ops[0], Ops[1], Ops[2]);
  case Instruction::ExtractElement:
    return ConstantExpr::getExtractElement(Ops[0], Ops[1]);
  case Instruction::ExtractValue:
    return ConstantFoldExtractValueInstruction(
        Ops[0], cast<ExtractValueInst>(InstOrCE)->getIndices());
  case Instruction::InsertElement:
    return ConstantExpr::getInsertElement(Ops[0], Ops[1], Ops[2]);
  case Instruction::InsertValue:
    return ConstantFoldInsertValueInstruction(
        Ops[0], Ops[1], cast<InsertValueInst>(InstOrCE)->getIndices());
  case Instruction::ShuffleVector:
    return ConstantExpr::getShuffleVector(
        Ops[0], Ops[1], cast<ShuffleVectorInst>(InstOrCE)->getShuffleMask());
  case Instruction::Load: {
    const auto *LI = dyn_cast<LoadInst>(InstOrCE);
    if (LI->isVolatile())
      return nullptr;
    return ConstantFoldLoadFromConstPtr(Ops[0], LI->getType(), DL);
  }
  }
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Constant Folding public APIs
//===----------------------------------------------------------------------===//

namespace {

Constant *
ConstantFoldConstantImpl(const Constant *C, const DataLayout &DL,
                         const TargetLibraryInfo *TLI,
                         SmallDenseMap<Constant *, Constant *> &FoldedOps) {
  if (!isa<ConstantVector>(C) && !isa<ConstantExpr>(C))
    return const_cast<Constant *>(C);

  SmallVector<Constant *, 8> Ops;
  for (const Use &OldU : C->operands()) {
    Constant *OldC = cast<Constant>(&OldU);
    Constant *NewC = OldC;
    // Recursively fold the ConstantExpr's operands. If we have already folded
    // a ConstantExpr, we don't have to process it again.
    if (isa<ConstantVector>(OldC) || isa<ConstantExpr>(OldC)) {
      auto It = FoldedOps.find(OldC);
      if (It == FoldedOps.end()) {
        NewC = ConstantFoldConstantImpl(OldC, DL, TLI, FoldedOps);
        FoldedOps.insert({OldC, NewC});
      } else {
        NewC = It->second;
      }
    }
    Ops.push_back(NewC);
  }

  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    if (Constant *Res = ConstantFoldInstOperandsImpl(
            CE, CE->getOpcode(), Ops, DL, TLI, /*AllowNonDeterministic=*/true))
      return Res;
    return const_cast<Constant *>(C);
  }

  assert(isa<ConstantVector>(C));
  return ConstantVector::get(Ops);
}

} // end anonymous namespace

Constant *llvm::ConstantFoldInstruction(Instruction *I, const DataLayout &DL,
                                        const TargetLibraryInfo *TLI) {
  // Handle PHI nodes quickly here...
  if (auto *PN = dyn_cast<PHINode>(I)) {
    Constant *CommonValue = nullptr;

    SmallDenseMap<Constant *, Constant *> FoldedOps;
    for (Value *Incoming : PN->incoming_values()) {
      // If the incoming value is undef then skip it.  Note that while we could
      // skip the value if it is equal to the phi node itself we choose not to
      // because that would break the rule that constant folding only applies if
      // all operands are constants.
      if (isa<UndefValue>(Incoming))
        continue;
      // If the incoming value is not a constant, then give up.
      auto *C = dyn_cast<Constant>(Incoming);
      if (!C)
        return nullptr;
      // Fold the PHI's operands.
      C = ConstantFoldConstantImpl(C, DL, TLI, FoldedOps);
      // If the incoming value is a different constant to
      // the one we saw previously, then give up.
      if (CommonValue && C != CommonValue)
        return nullptr;
      CommonValue = C;
    }

    // If we reach here, all incoming values are the same constant or undef.
    return CommonValue ? CommonValue : UndefValue::get(PN->getType());
  }

  // Scan the operand list, checking to see if they are all constants, if so,
  // hand off to ConstantFoldInstOperandsImpl.
  if (!all_of(I->operands(), [](Use &U) { return isa<Constant>(U); }))
    return nullptr;

  SmallDenseMap<Constant *, Constant *> FoldedOps;
  SmallVector<Constant *, 8> Ops;
  for (const Use &OpU : I->operands()) {
    auto *Op = cast<Constant>(&OpU);
    // Fold the Instruction's operands.
    Op = ConstantFoldConstantImpl(Op, DL, TLI, FoldedOps);
    Ops.push_back(Op);
  }

  return ConstantFoldInstOperands(I, Ops, DL, TLI);
}

Constant *llvm::ConstantFoldConstant(const Constant *C, const DataLayout &DL,
                                     const TargetLibraryInfo *TLI) {
  SmallDenseMap<Constant *, Constant *> FoldedOps;
  return ConstantFoldConstantImpl(C, DL, TLI, FoldedOps);
}

Constant *llvm::ConstantFoldInstOperands(Instruction *I,
                                         ArrayRef<Constant *> Ops,
                                         const DataLayout &DL,
                                         const TargetLibraryInfo *TLI,
                                         bool AllowNonDeterministic) {
  return ConstantFoldInstOperandsImpl(I, I->getOpcode(), Ops, DL, TLI,
                                      AllowNonDeterministic);
}

Constant *llvm::ConstantFoldCompareInstOperands(
    unsigned IntPredicate, Constant *Ops0, Constant *Ops1, const DataLayout &DL,
    const TargetLibraryInfo *TLI, const Instruction *I) {
  CmpInst::Predicate Predicate = (CmpInst::Predicate)IntPredicate;
  // fold: icmp (inttoptr x), null         -> icmp x, 0
  // fold: icmp null, (inttoptr x)         -> icmp 0, x
  // fold: icmp (ptrtoint x), 0            -> icmp x, null
  // fold: icmp 0, (ptrtoint x)            -> icmp null, x
  // fold: icmp (inttoptr x), (inttoptr y) -> icmp trunc/zext x, trunc/zext y
  // fold: icmp (ptrtoint x), (ptrtoint y) -> icmp x, y
  //
  // FIXME: The following comment is out of data and the DataLayout is here now.
  // ConstantExpr::getCompare cannot do this, because it doesn't have DL
  // around to know if bit truncation is happening.
  if (auto *CE0 = dyn_cast<ConstantExpr>(Ops0)) {
    if (Ops1->isNullValue()) {
      if (CE0->getOpcode() == Instruction::IntToPtr) {
        Type *IntPtrTy = DL.getIntPtrType(CE0->getType());
        // Convert the integer value to the right size to ensure we get the
        // proper extension or truncation.
        if (Constant *C = ConstantFoldIntegerCast(CE0->getOperand(0), IntPtrTy,
                                                  /*IsSigned*/ false, DL)) {
          Constant *Null = Constant::getNullValue(C->getType());
          return ConstantFoldCompareInstOperands(Predicate, C, Null, DL, TLI);
        }
      }

      // Only do this transformation if the int is intptrty in size, otherwise
      // there is a truncation or extension that we aren't modeling.
      if (CE0->getOpcode() == Instruction::PtrToInt) {
        Type *IntPtrTy = DL.getIntPtrType(CE0->getOperand(0)->getType());
        if (CE0->getType() == IntPtrTy) {
          Constant *C = CE0->getOperand(0);
          Constant *Null = Constant::getNullValue(C->getType());
          return ConstantFoldCompareInstOperands(Predicate, C, Null, DL, TLI);
        }
      }
    }

    if (auto *CE1 = dyn_cast<ConstantExpr>(Ops1)) {
      if (CE0->getOpcode() == CE1->getOpcode()) {
        if (CE0->getOpcode() == Instruction::IntToPtr) {
          Type *IntPtrTy = DL.getIntPtrType(CE0->getType());

          // Convert the integer value to the right size to ensure we get the
          // proper extension or truncation.
          Constant *C0 = ConstantFoldIntegerCast(CE0->getOperand(0), IntPtrTy,
                                                 /*IsSigned*/ false, DL);
          Constant *C1 = ConstantFoldIntegerCast(CE1->getOperand(0), IntPtrTy,
                                                 /*IsSigned*/ false, DL);
          if (C0 && C1)
            return ConstantFoldCompareInstOperands(Predicate, C0, C1, DL, TLI);
        }

        // Only do this transformation if the int is intptrty in size, otherwise
        // there is a truncation or extension that we aren't modeling.
        if (CE0->getOpcode() == Instruction::PtrToInt) {
          Type *IntPtrTy = DL.getIntPtrType(CE0->getOperand(0)->getType());
          if (CE0->getType() == IntPtrTy &&
              CE0->getOperand(0)->getType() == CE1->getOperand(0)->getType()) {
            return ConstantFoldCompareInstOperands(
                Predicate, CE0->getOperand(0), CE1->getOperand(0), DL, TLI);
          }
        }
      }
    }

    // Convert pointer comparison (base+offset1) pred (base+offset2) into
    // offset1 pred offset2, for the case where the offset is inbounds. This
    // only works for equality and unsigned comparison, as inbounds permits
    // crossing the sign boundary. However, the offset comparison itself is
    // signed.
    if (Ops0->getType()->isPointerTy() && !ICmpInst::isSigned(Predicate)) {
      unsigned IndexWidth = DL.getIndexTypeSizeInBits(Ops0->getType());
      APInt Offset0(IndexWidth, 0);
      Value *Stripped0 =
          Ops0->stripAndAccumulateInBoundsConstantOffsets(DL, Offset0);
      APInt Offset1(IndexWidth, 0);
      Value *Stripped1 =
          Ops1->stripAndAccumulateInBoundsConstantOffsets(DL, Offset1);
      if (Stripped0 == Stripped1)
        return ConstantInt::getBool(
            Ops0->getContext(),
            ICmpInst::compare(Offset0, Offset1,
                              ICmpInst::getSignedPredicate(Predicate)));
    }
  } else if (isa<ConstantExpr>(Ops1)) {
    // If RHS is a constant expression, but the left side isn't, swap the
    // operands and try again.
    Predicate = ICmpInst::getSwappedPredicate(Predicate);
    return ConstantFoldCompareInstOperands(Predicate, Ops1, Ops0, DL, TLI);
  }

  // Flush any denormal constant float input according to denormal handling
  // mode.
  Ops0 = FlushFPConstant(Ops0, I, /* IsOutput */ false);
  if (!Ops0)
    return nullptr;
  Ops1 = FlushFPConstant(Ops1, I, /* IsOutput */ false);
  if (!Ops1)
    return nullptr;

  return ConstantFoldCompareInstruction(Predicate, Ops0, Ops1);
}

Constant *llvm::ConstantFoldUnaryOpOperand(unsigned Opcode, Constant *Op,
                                           const DataLayout &DL) {
  assert(Instruction::isUnaryOp(Opcode));

  return ConstantFoldUnaryInstruction(Opcode, Op);
}

Constant *llvm::ConstantFoldBinaryOpOperands(unsigned Opcode, Constant *LHS,
                                             Constant *RHS,
                                             const DataLayout &DL) {
  assert(Instruction::isBinaryOp(Opcode));
  if (isa<ConstantExpr>(LHS) || isa<ConstantExpr>(RHS))
    if (Constant *C = SymbolicallyEvaluateBinop(Opcode, LHS, RHS, DL))
      return C;

  if (ConstantExpr::isDesirableBinOp(Opcode))
    return ConstantExpr::get(Opcode, LHS, RHS);
  return ConstantFoldBinaryInstruction(Opcode, LHS, RHS);
}

Constant *llvm::FlushFPConstant(Constant *Operand, const Instruction *I,
                                bool IsOutput) {
  if (!I || !I->getParent() || !I->getFunction())
    return Operand;

  ConstantFP *CFP = dyn_cast<ConstantFP>(Operand);
  if (!CFP)
    return Operand;

  const APFloat &APF = CFP->getValueAPF();
  // TODO: Should this canonicalize nans?
  if (!APF.isDenormal())
    return Operand;

  Type *Ty = CFP->getType();
  DenormalMode DenormMode =
      I->getFunction()->getDenormalMode(Ty->getFltSemantics());
  DenormalMode::DenormalModeKind Mode =
      IsOutput ? DenormMode.Output : DenormMode.Input;
  switch (Mode) {
  default:
    llvm_unreachable("unknown denormal mode");
  case DenormalMode::Dynamic:
    return nullptr;
  case DenormalMode::IEEE:
    return Operand;
  case DenormalMode::PreserveSign:
    if (APF.isDenormal()) {
      return ConstantFP::get(
          Ty->getContext(),
          APFloat::getZero(Ty->getFltSemantics(), APF.isNegative()));
    }
    return Operand;
  case DenormalMode::PositiveZero:
    if (APF.isDenormal()) {
      return ConstantFP::get(Ty->getContext(),
                             APFloat::getZero(Ty->getFltSemantics(), false));
    }
    return Operand;
  }
  return Operand;
}

Constant *llvm::ConstantFoldFPInstOperands(unsigned Opcode, Constant *LHS,
                                           Constant *RHS, const DataLayout &DL,
                                           const Instruction *I,
                                           bool AllowNonDeterministic) {
  if (Instruction::isBinaryOp(Opcode)) {
    // Flush denormal inputs if needed.
    Constant *Op0 = FlushFPConstant(LHS, I, /* IsOutput */ false);
    if (!Op0)
      return nullptr;
    Constant *Op1 = FlushFPConstant(RHS, I, /* IsOutput */ false);
    if (!Op1)
      return nullptr;

    // If nsz or an algebraic FMF flag is set, the result of the FP operation
    // may change due to future optimization. Don't constant fold them if
    // non-deterministic results are not allowed.
    if (!AllowNonDeterministic)
      if (auto *FP = dyn_cast_or_null<FPMathOperator>(I))
        if (FP->hasNoSignedZeros() || FP->hasAllowReassoc() ||
            FP->hasAllowContract() || FP->hasAllowReciprocal())
          return nullptr;

    // Calculate constant result.
    Constant *C = ConstantFoldBinaryOpOperands(Opcode, Op0, Op1, DL);
    if (!C)
      return nullptr;

    // Flush denormal output if needed.
    C = FlushFPConstant(C, I, /* IsOutput */ true);
    if (!C)
      return nullptr;

    // The precise NaN value is non-deterministic.
    if (!AllowNonDeterministic && C->isNaN())
      return nullptr;

    return C;
  }
  // If instruction lacks a parent/function and the denormal mode cannot be
  // determined, use the default (IEEE).
  return ConstantFoldBinaryOpOperands(Opcode, LHS, RHS, DL);
}

Constant *llvm::ConstantFoldCastOperand(unsigned Opcode, Constant *C,
                                        Type *DestTy, const DataLayout &DL) {
  assert(Instruction::isCast(Opcode));
  switch (Opcode) {
  default:
    llvm_unreachable("Missing case");
  case Instruction::PtrToInt:
    if (auto *CE = dyn_cast<ConstantExpr>(C)) {
      Constant *FoldedValue = nullptr;
      // If the input is a inttoptr, eliminate the pair.  This requires knowing
      // the width of a pointer, so it can't be done in ConstantExpr::getCast.
      if (CE->getOpcode() == Instruction::IntToPtr) {
        // zext/trunc the inttoptr to pointer size.
        FoldedValue = ConstantFoldIntegerCast(CE->getOperand(0),
                                              DL.getIntPtrType(CE->getType()),
                                              /*IsSigned=*/false, DL);
      } else if (auto *GEP = dyn_cast<GEPOperator>(CE)) {
        // If we have GEP, we can perform the following folds:
        // (ptrtoint (gep null, x)) -> x
        // (ptrtoint (gep (gep null, x), y) -> x + y, etc.
        unsigned BitWidth = DL.getIndexTypeSizeInBits(GEP->getType());
        APInt BaseOffset(BitWidth, 0);
        auto *Base = cast<Constant>(GEP->stripAndAccumulateConstantOffsets(
            DL, BaseOffset, /*AllowNonInbounds=*/true));
        if (Base->isNullValue()) {
          FoldedValue = ConstantInt::get(CE->getContext(), BaseOffset);
        } else {
          // ptrtoint (gep i8, Ptr, (sub 0, V)) -> sub (ptrtoint Ptr), V
          if (GEP->getNumIndices() == 1 &&
              GEP->getSourceElementType()->isIntegerTy(8)) {
            auto *Ptr = cast<Constant>(GEP->getPointerOperand());
            auto *Sub = dyn_cast<ConstantExpr>(GEP->getOperand(1));
            Type *IntIdxTy = DL.getIndexType(Ptr->getType());
            if (Sub && Sub->getType() == IntIdxTy &&
                Sub->getOpcode() == Instruction::Sub &&
                Sub->getOperand(0)->isNullValue())
              FoldedValue = ConstantExpr::getSub(
                  ConstantExpr::getPtrToInt(Ptr, IntIdxTy), Sub->getOperand(1));
          }
        }
      }
      if (FoldedValue) {
        // Do a zext or trunc to get to the ptrtoint dest size.
        return ConstantFoldIntegerCast(FoldedValue, DestTy, /*IsSigned=*/false,
                                       DL);
      }
    }
    break;
  case Instruction::IntToPtr:
    // If the input is a ptrtoint, turn the pair into a ptr to ptr bitcast if
    // the int size is >= the ptr size and the address spaces are the same.
    // This requires knowing the width of a pointer, so it can't be done in
    // ConstantExpr::getCast.
    if (auto *CE = dyn_cast<ConstantExpr>(C)) {
      if (CE->getOpcode() == Instruction::PtrToInt) {
        Constant *SrcPtr = CE->getOperand(0);
        unsigned SrcPtrSize = DL.getPointerTypeSizeInBits(SrcPtr->getType());
        unsigned MidIntSize = CE->getType()->getScalarSizeInBits();

        if (MidIntSize >= SrcPtrSize) {
          unsigned SrcAS = SrcPtr->getType()->getPointerAddressSpace();
          if (SrcAS == DestTy->getPointerAddressSpace())
            return FoldBitCast(CE->getOperand(0), DestTy, DL);
        }
      }
    }
    break;
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::AddrSpaceCast:
    break;
  case Instruction::BitCast:
    return FoldBitCast(C, DestTy, DL);
  }

  if (ConstantExpr::isDesirableCastOp(Opcode))
    return ConstantExpr::getCast(Opcode, C, DestTy);
  return ConstantFoldCastInstruction(Opcode, C, DestTy);
}

Constant *llvm::ConstantFoldIntegerCast(Constant *C, Type *DestTy,
                                        bool IsSigned, const DataLayout &DL) {
  Type *SrcTy = C->getType();
  if (SrcTy == DestTy)
    return C;
  if (SrcTy->getScalarSizeInBits() > DestTy->getScalarSizeInBits())
    return ConstantFoldCastOperand(Instruction::Trunc, C, DestTy, DL);
  if (IsSigned)
    return ConstantFoldCastOperand(Instruction::SExt, C, DestTy, DL);
  return ConstantFoldCastOperand(Instruction::ZExt, C, DestTy, DL);
}

//===----------------------------------------------------------------------===//
//  Constant Folding for Calls
//

bool llvm::canConstantFoldCallTo(const CallBase *Call, const Function *F) {
  if (Call->isNoBuiltin())
    return false;
  if (Call->getFunctionType() != F->getFunctionType())
    return false;
  switch (F->getIntrinsicID()) {
  // Operations that do not operate floating-point numbers and do not depend on
  // FP environment can be folded even in strictfp functions.
  case Intrinsic::bswap:
  case Intrinsic::ctpop:
  case Intrinsic::ctlz:
  case Intrinsic::cttz:
  case Intrinsic::fshl:
  case Intrinsic::fshr:
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group:
  case Intrinsic::masked_load:
  case Intrinsic::get_active_lane_mask:
  case Intrinsic::abs:
  case Intrinsic::smax:
  case Intrinsic::smin:
  case Intrinsic::umax:
  case Intrinsic::umin:
  case Intrinsic::scmp:
  case Intrinsic::ucmp:
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
  case Intrinsic::smul_with_overflow:
  case Intrinsic::umul_with_overflow:
  case Intrinsic::sadd_sat:
  case Intrinsic::uadd_sat:
  case Intrinsic::ssub_sat:
  case Intrinsic::usub_sat:
  case Intrinsic::smul_fix:
  case Intrinsic::smul_fix_sat:
  case Intrinsic::bitreverse:
  case Intrinsic::is_constant:
  case Intrinsic::vector_reduce_add:
  case Intrinsic::vector_reduce_mul:
  case Intrinsic::vector_reduce_and:
  case Intrinsic::vector_reduce_or:
  case Intrinsic::vector_reduce_xor:
  case Intrinsic::vector_reduce_smin:
  case Intrinsic::vector_reduce_smax:
  case Intrinsic::vector_reduce_umin:
  case Intrinsic::vector_reduce_umax:
  // Target intrinsics
  case Intrinsic::amdgcn_perm:
  case Intrinsic::amdgcn_wave_reduce_umin:
  case Intrinsic::amdgcn_wave_reduce_umax:
  case Intrinsic::amdgcn_s_wqm:
  case Intrinsic::amdgcn_s_quadmask:
  case Intrinsic::amdgcn_s_bitreplicate:
  case Intrinsic::arm_mve_vctp8:
  case Intrinsic::arm_mve_vctp16:
  case Intrinsic::arm_mve_vctp32:
  case Intrinsic::arm_mve_vctp64:
  case Intrinsic::aarch64_sve_convert_from_svbool:
  // WebAssembly float semantics are always known
  case Intrinsic::wasm_trunc_signed:
  case Intrinsic::wasm_trunc_unsigned:
    return true;

  // Floating point operations cannot be folded in strictfp functions in
  // general case. They can be folded if FP environment is known to compiler.
  case Intrinsic::minnum:
  case Intrinsic::maxnum:
  case Intrinsic::minimum:
  case Intrinsic::maximum:
  case Intrinsic::log:
  case Intrinsic::log2:
  case Intrinsic::log10:
  case Intrinsic::exp:
  case Intrinsic::exp2:
  case Intrinsic::exp10:
  case Intrinsic::sqrt:
  case Intrinsic::sin:
  case Intrinsic::cos:
  case Intrinsic::pow:
  case Intrinsic::powi:
  case Intrinsic::ldexp:
  case Intrinsic::fma:
  case Intrinsic::fmuladd:
  case Intrinsic::frexp:
  case Intrinsic::fptoui_sat:
  case Intrinsic::fptosi_sat:
  case Intrinsic::convert_from_fp16:
  case Intrinsic::convert_to_fp16:
  case Intrinsic::amdgcn_cos:
  case Intrinsic::amdgcn_cubeid:
  case Intrinsic::amdgcn_cubema:
  case Intrinsic::amdgcn_cubesc:
  case Intrinsic::amdgcn_cubetc:
  case Intrinsic::amdgcn_fmul_legacy:
  case Intrinsic::amdgcn_fma_legacy:
  case Intrinsic::amdgcn_fract:
  case Intrinsic::amdgcn_sin:
  // The intrinsics below depend on rounding mode in MXCSR.
  case Intrinsic::x86_sse_cvtss2si:
  case Intrinsic::x86_sse_cvtss2si64:
  case Intrinsic::x86_sse_cvttss2si:
  case Intrinsic::x86_sse_cvttss2si64:
  case Intrinsic::x86_sse2_cvtsd2si:
  case Intrinsic::x86_sse2_cvtsd2si64:
  case Intrinsic::x86_sse2_cvttsd2si:
  case Intrinsic::x86_sse2_cvttsd2si64:
  case Intrinsic::x86_avx512_vcvtss2si32:
  case Intrinsic::x86_avx512_vcvtss2si64:
  case Intrinsic::x86_avx512_cvttss2si:
  case Intrinsic::x86_avx512_cvttss2si64:
  case Intrinsic::x86_avx512_vcvtsd2si32:
  case Intrinsic::x86_avx512_vcvtsd2si64:
  case Intrinsic::x86_avx512_cvttsd2si:
  case Intrinsic::x86_avx512_cvttsd2si64:
  case Intrinsic::x86_avx512_vcvtss2usi32:
  case Intrinsic::x86_avx512_vcvtss2usi64:
  case Intrinsic::x86_avx512_cvttss2usi:
  case Intrinsic::x86_avx512_cvttss2usi64:
  case Intrinsic::x86_avx512_vcvtsd2usi32:
  case Intrinsic::x86_avx512_vcvtsd2usi64:
  case Intrinsic::x86_avx512_cvttsd2usi:
  case Intrinsic::x86_avx512_cvttsd2usi64:
    return !Call->isStrictFP();

  // Sign operations are actually bitwise operations, they do not raise
  // exceptions even for SNANs.
  case Intrinsic::fabs:
  case Intrinsic::copysign:
  case Intrinsic::is_fpclass:
  // Non-constrained variants of rounding operations means default FP
  // environment, they can be folded in any case.
  case Intrinsic::ceil:
  case Intrinsic::floor:
  case Intrinsic::round:
  case Intrinsic::roundeven:
  case Intrinsic::trunc:
  case Intrinsic::nearbyint:
  case Intrinsic::rint:
  case Intrinsic::canonicalize:
  // Constrained intrinsics can be folded if FP environment is known
  // to compiler.
  case Intrinsic::experimental_constrained_fma:
  case Intrinsic::experimental_constrained_fmuladd:
  case Intrinsic::experimental_constrained_fadd:
  case Intrinsic::experimental_constrained_fsub:
  case Intrinsic::experimental_constrained_fmul:
  case Intrinsic::experimental_constrained_fdiv:
  case Intrinsic::experimental_constrained_frem:
  case Intrinsic::experimental_constrained_ceil:
  case Intrinsic::experimental_constrained_floor:
  case Intrinsic::experimental_constrained_round:
  case Intrinsic::experimental_constrained_roundeven:
  case Intrinsic::experimental_constrained_trunc:
  case Intrinsic::experimental_constrained_nearbyint:
  case Intrinsic::experimental_constrained_rint:
  case Intrinsic::experimental_constrained_fcmp:
  case Intrinsic::experimental_constrained_fcmps:
    return true;
  default:
    return false;
  case Intrinsic::not_intrinsic: break;
  }

  if (!F->hasName() || Call->isStrictFP())
    return false;

  // In these cases, the check of the length is required.  We don't want to
  // return true for a name like "cos\0blah" which strcmp would return equal to
  // "cos", but has length 8.
  StringRef Name = F->getName();
  switch (Name[0]) {
  default:
    return false;
  case 'a':
    return Name == "acos" || Name == "acosf" ||
           Name == "asin" || Name == "asinf" ||
           Name == "atan" || Name == "atanf" ||
           Name == "atan2" || Name == "atan2f";
  case 'c':
    return Name == "ceil" || Name == "ceilf" ||
           Name == "cos" || Name == "cosf" ||
           Name == "cosh" || Name == "coshf";
  case 'e':
    return Name == "exp" || Name == "expf" ||
           Name == "exp2" || Name == "exp2f";
  case 'f':
    return Name == "fabs" || Name == "fabsf" ||
           Name == "floor" || Name == "floorf" ||
           Name == "fmod" || Name == "fmodf";
  case 'l':
    return Name == "log" || Name == "logf" || Name == "log2" ||
           Name == "log2f" || Name == "log10" || Name == "log10f" ||
           Name == "logl";
  case 'n':
    return Name == "nearbyint" || Name == "nearbyintf";
  case 'p':
    return Name == "pow" || Name == "powf";
  case 'r':
    return Name == "remainder" || Name == "remainderf" ||
           Name == "rint" || Name == "rintf" ||
           Name == "round" || Name == "roundf";
  case 's':
    return Name == "sin" || Name == "sinf" ||
           Name == "sinh" || Name == "sinhf" ||
           Name == "sqrt" || Name == "sqrtf";
  case 't':
    return Name == "tan" || Name == "tanf" ||
           Name == "tanh" || Name == "tanhf" ||
           Name == "trunc" || Name == "truncf";
  case '_':
    // Check for various function names that get used for the math functions
    // when the header files are preprocessed with the macro
    // __FINITE_MATH_ONLY__ enabled.
    // The '12' here is the length of the shortest name that can match.
    // We need to check the size before looking at Name[1] and Name[2]
    // so we may as well check a limit that will eliminate mismatches.
    if (Name.size() < 12 || Name[1] != '_')
      return false;
    switch (Name[2]) {
    default:
      return false;
    case 'a':
      return Name == "__acos_finite" || Name == "__acosf_finite" ||
             Name == "__asin_finite" || Name == "__asinf_finite" ||
             Name == "__atan2_finite" || Name == "__atan2f_finite";
    case 'c':
      return Name == "__cosh_finite" || Name == "__coshf_finite";
    case 'e':
      return Name == "__exp_finite" || Name == "__expf_finite" ||
             Name == "__exp2_finite" || Name == "__exp2f_finite";
    case 'l':
      return Name == "__log_finite" || Name == "__logf_finite" ||
             Name == "__log10_finite" || Name == "__log10f_finite";
    case 'p':
      return Name == "__pow_finite" || Name == "__powf_finite";
    case 's':
      return Name == "__sinh_finite" || Name == "__sinhf_finite";
    }
  }
}

namespace {

Constant *GetConstantFoldFPValue(double V, Type *Ty) {
  if (Ty->isHalfTy() || Ty->isFloatTy()) {
    APFloat APF(V);
    bool unused;
    APF.convert(Ty->getFltSemantics(), APFloat::rmNearestTiesToEven, &unused);
    return ConstantFP::get(Ty->getContext(), APF);
  }
  if (Ty->isDoubleTy())
    return ConstantFP::get(Ty->getContext(), APFloat(V));
  llvm_unreachable("Can only constant fold half/float/double");
}

#if defined(HAS_IEE754_FLOAT128) && defined(HAS_LOGF128)
Constant *GetConstantFoldFPValue128(float128 V, Type *Ty) {
  if (Ty->isFP128Ty())
    return ConstantFP::get(Ty, V);
  llvm_unreachable("Can only constant fold fp128");
}
#endif

/// Clear the floating-point exception state.
inline void llvm_fenv_clearexcept() {
#if defined(HAVE_FENV_H) && HAVE_DECL_FE_ALL_EXCEPT
  feclearexcept(FE_ALL_EXCEPT);
#endif
  errno = 0;
}

/// Test if a floating-point exception was raised.
inline bool llvm_fenv_testexcept() {
  int errno_val = errno;
  if (errno_val == ERANGE || errno_val == EDOM)
    return true;
#if defined(HAVE_FENV_H) && HAVE_DECL_FE_ALL_EXCEPT && HAVE_DECL_FE_INEXACT
  if (fetestexcept(FE_ALL_EXCEPT & ~FE_INEXACT))
    return true;
#endif
  return false;
}

Constant *ConstantFoldFP(double (*NativeFP)(double), const APFloat &V,
                         Type *Ty) {
  llvm_fenv_clearexcept();
  double Result = NativeFP(V.convertToDouble());
  if (llvm_fenv_testexcept()) {
    llvm_fenv_clearexcept();
    return nullptr;
  }

  return GetConstantFoldFPValue(Result, Ty);
}

#if defined(HAS_IEE754_FLOAT128) && defined(HAS_LOGF128)
Constant *ConstantFoldFP128(float128 (*NativeFP)(float128), const APFloat &V,
                            Type *Ty) {
  llvm_fenv_clearexcept();
  float128 Result = NativeFP(V.convertToQuad());
  if (llvm_fenv_testexcept()) {
    llvm_fenv_clearexcept();
    return nullptr;
  }

  return GetConstantFoldFPValue128(Result, Ty);
}
#endif

Constant *ConstantFoldBinaryFP(double (*NativeFP)(double, double),
                               const APFloat &V, const APFloat &W, Type *Ty) {
  llvm_fenv_clearexcept();
  double Result = NativeFP(V.convertToDouble(), W.convertToDouble());
  if (llvm_fenv_testexcept()) {
    llvm_fenv_clearexcept();
    return nullptr;
  }

  return GetConstantFoldFPValue(Result, Ty);
}

Constant *constantFoldVectorReduce(Intrinsic::ID IID, Constant *Op) {
  FixedVectorType *VT = dyn_cast<FixedVectorType>(Op->getType());
  if (!VT)
    return nullptr;

  // This isn't strictly necessary, but handle the special/common case of zero:
  // all integer reductions of a zero input produce zero.
  if (isa<ConstantAggregateZero>(Op))
    return ConstantInt::get(VT->getElementType(), 0);

  // This is the same as the underlying binops - poison propagates.
  if (isa<PoisonValue>(Op) || Op->containsPoisonElement())
    return PoisonValue::get(VT->getElementType());

  // TODO: Handle undef.
  if (!isa<ConstantVector>(Op) && !isa<ConstantDataVector>(Op))
    return nullptr;

  auto *EltC = dyn_cast<ConstantInt>(Op->getAggregateElement(0U));
  if (!EltC)
    return nullptr;

  APInt Acc = EltC->getValue();
  for (unsigned I = 1, E = VT->getNumElements(); I != E; I++) {
    if (!(EltC = dyn_cast<ConstantInt>(Op->getAggregateElement(I))))
      return nullptr;
    const APInt &X = EltC->getValue();
    switch (IID) {
    case Intrinsic::vector_reduce_add:
      Acc = Acc + X;
      break;
    case Intrinsic::vector_reduce_mul:
      Acc = Acc * X;
      break;
    case Intrinsic::vector_reduce_and:
      Acc = Acc & X;
      break;
    case Intrinsic::vector_reduce_or:
      Acc = Acc | X;
      break;
    case Intrinsic::vector_reduce_xor:
      Acc = Acc ^ X;
      break;
    case Intrinsic::vector_reduce_smin:
      Acc = APIntOps::smin(Acc, X);
      break;
    case Intrinsic::vector_reduce_smax:
      Acc = APIntOps::smax(Acc, X);
      break;
    case Intrinsic::vector_reduce_umin:
      Acc = APIntOps::umin(Acc, X);
      break;
    case Intrinsic::vector_reduce_umax:
      Acc = APIntOps::umax(Acc, X);
      break;
    }
  }

  return ConstantInt::get(Op->getContext(), Acc);
}

/// Attempt to fold an SSE floating point to integer conversion of a constant
/// floating point. If roundTowardZero is false, the default IEEE rounding is
/// used (toward nearest, ties to even). This matches the behavior of the
/// non-truncating SSE instructions in the default rounding mode. The desired
/// integer type Ty is used to select how many bits are available for the
/// result. Returns null if the conversion cannot be performed, otherwise
/// returns the Constant value resulting from the conversion.
Constant *ConstantFoldSSEConvertToInt(const APFloat &Val, bool roundTowardZero,
                                      Type *Ty, bool IsSigned) {
  // All of these conversion intrinsics form an integer of at most 64bits.
  unsigned ResultWidth = Ty->getIntegerBitWidth();
  assert(ResultWidth <= 64 &&
         "Can only constant fold conversions to 64 and 32 bit ints");

  uint64_t UIntVal;
  bool isExact = false;
  APFloat::roundingMode mode = roundTowardZero? APFloat::rmTowardZero
                                              : APFloat::rmNearestTiesToEven;
  APFloat::opStatus status =
      Val.convertToInteger(MutableArrayRef(UIntVal), ResultWidth,
                           IsSigned, mode, &isExact);
  if (status != APFloat::opOK &&
      (!roundTowardZero || status != APFloat::opInexact))
    return nullptr;
  return ConstantInt::get(Ty, UIntVal, IsSigned);
}

double getValueAsDouble(ConstantFP *Op) {
  Type *Ty = Op->getType();

  if (Ty->isBFloatTy() || Ty->isHalfTy() || Ty->isFloatTy() || Ty->isDoubleTy())
    return Op->getValueAPF().convertToDouble();

  bool unused;
  APFloat APF = Op->getValueAPF();
  APF.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &unused);
  return APF.convertToDouble();
}

static bool getConstIntOrUndef(Value *Op, const APInt *&C) {
  if (auto *CI = dyn_cast<ConstantInt>(Op)) {
    C = &CI->getValue();
    return true;
  }
  if (isa<UndefValue>(Op)) {
    C = nullptr;
    return true;
  }
  return false;
}

/// Checks if the given intrinsic call, which evaluates to constant, is allowed
/// to be folded.
///
/// \param CI Constrained intrinsic call.
/// \param St Exception flags raised during constant evaluation.
static bool mayFoldConstrained(ConstrainedFPIntrinsic *CI,
                               APFloat::opStatus St) {
  std::optional<RoundingMode> ORM = CI->getRoundingMode();
  std::optional<fp::ExceptionBehavior> EB = CI->getExceptionBehavior();

  // If the operation does not change exception status flags, it is safe
  // to fold.
  if (St == APFloat::opStatus::opOK)
    return true;

  // If evaluation raised FP exception, the result can depend on rounding
  // mode. If the latter is unknown, folding is not possible.
  if (ORM && *ORM == RoundingMode::Dynamic)
    return false;

  // If FP exceptions are ignored, fold the call, even if such exception is
  // raised.
  if (EB && *EB != fp::ExceptionBehavior::ebStrict)
    return true;

  // Leave the calculation for runtime so that exception flags be correctly set
  // in hardware.
  return false;
}

/// Returns the rounding mode that should be used for constant evaluation.
static RoundingMode
getEvaluationRoundingMode(const ConstrainedFPIntrinsic *CI) {
  std::optional<RoundingMode> ORM = CI->getRoundingMode();
  if (!ORM || *ORM == RoundingMode::Dynamic)
    // Even if the rounding mode is unknown, try evaluating the operation.
    // If it does not raise inexact exception, rounding was not applied,
    // so the result is exact and does not depend on rounding mode. Whether
    // other FP exceptions are raised, it does not depend on rounding mode.
    return RoundingMode::NearestTiesToEven;
  return *ORM;
}

/// Try to constant fold llvm.canonicalize for the given caller and value.
static Constant *constantFoldCanonicalize(const Type *Ty, const CallBase *CI,
                                          const APFloat &Src) {
  // Zero, positive and negative, is always OK to fold.
  if (Src.isZero()) {
    // Get a fresh 0, since ppc_fp128 does have non-canonical zeros.
    return ConstantFP::get(
        CI->getContext(),
        APFloat::getZero(Src.getSemantics(), Src.isNegative()));
  }

  if (!Ty->isIEEELikeFPTy())
    return nullptr;

  // Zero is always canonical and the sign must be preserved.
  //
  // Denorms and nans may have special encodings, but it should be OK to fold a
  // totally average number.
  if (Src.isNormal() || Src.isInfinity())
    return ConstantFP::get(CI->getContext(), Src);

  if (Src.isDenormal() && CI->getParent() && CI->getFunction()) {
    DenormalMode DenormMode =
        CI->getFunction()->getDenormalMode(Src.getSemantics());

    if (DenormMode == DenormalMode::getIEEE())
      return ConstantFP::get(CI->getContext(), Src);

    if (DenormMode.Input == DenormalMode::Dynamic)
      return nullptr;

    // If we know if either input or output is flushed, we can fold.
    if ((DenormMode.Input == DenormalMode::Dynamic &&
         DenormMode.Output == DenormalMode::IEEE) ||
        (DenormMode.Input == DenormalMode::IEEE &&
         DenormMode.Output == DenormalMode::Dynamic))
      return nullptr;

    bool IsPositive =
        (!Src.isNegative() || DenormMode.Input == DenormalMode::PositiveZero ||
         (DenormMode.Output == DenormalMode::PositiveZero &&
          DenormMode.Input == DenormalMode::IEEE));

    return ConstantFP::get(CI->getContext(),
                           APFloat::getZero(Src.getSemantics(), !IsPositive));
  }

  return nullptr;
}

static Constant *ConstantFoldScalarCall1(StringRef Name,
                                         Intrinsic::ID IntrinsicID,
                                         Type *Ty,
                                         ArrayRef<Constant *> Operands,
                                         const TargetLibraryInfo *TLI,
                                         const CallBase *Call) {
  assert(Operands.size() == 1 && "Wrong number of operands.");

  if (IntrinsicID == Intrinsic::is_constant) {
    // We know we have a "Constant" argument. But we want to only
    // return true for manifest constants, not those that depend on
    // constants with unknowable values, e.g. GlobalValue or BlockAddress.
    if (Operands[0]->isManifestConstant())
      return ConstantInt::getTrue(Ty->getContext());
    return nullptr;
  }

  if (isa<PoisonValue>(Operands[0])) {
    // TODO: All of these operations should probably propagate poison.
    if (IntrinsicID == Intrinsic::canonicalize)
      return PoisonValue::get(Ty);
  }

  if (isa<UndefValue>(Operands[0])) {
    // cosine(arg) is between -1 and 1. cosine(invalid arg) is NaN.
    // ctpop() is between 0 and bitwidth, pick 0 for undef.
    // fptoui.sat and fptosi.sat can always fold to zero (for a zero input).
    if (IntrinsicID == Intrinsic::cos ||
        IntrinsicID == Intrinsic::ctpop ||
        IntrinsicID == Intrinsic::fptoui_sat ||
        IntrinsicID == Intrinsic::fptosi_sat ||
        IntrinsicID == Intrinsic::canonicalize)
      return Constant::getNullValue(Ty);
    if (IntrinsicID == Intrinsic::bswap ||
        IntrinsicID == Intrinsic::bitreverse ||
        IntrinsicID == Intrinsic::launder_invariant_group ||
        IntrinsicID == Intrinsic::strip_invariant_group)
      return Operands[0];
  }

  if (isa<ConstantPointerNull>(Operands[0])) {
    // launder(null) == null == strip(null) iff in addrspace 0
    if (IntrinsicID == Intrinsic::launder_invariant_group ||
        IntrinsicID == Intrinsic::strip_invariant_group) {
      // If instruction is not yet put in a basic block (e.g. when cloning
      // a function during inlining), Call's caller may not be available.
      // So check Call's BB first before querying Call->getCaller.
      const Function *Caller =
          Call->getParent() ? Call->getCaller() : nullptr;
      if (Caller &&
          !NullPointerIsDefined(
              Caller, Operands[0]->getType()->getPointerAddressSpace())) {
        return Operands[0];
      }
      return nullptr;
    }
  }

  if (auto *Op = dyn_cast<ConstantFP>(Operands[0])) {
    if (IntrinsicID == Intrinsic::convert_to_fp16) {
      APFloat Val(Op->getValueAPF());

      bool lost = false;
      Val.convert(APFloat::IEEEhalf(), APFloat::rmNearestTiesToEven, &lost);

      return ConstantInt::get(Ty->getContext(), Val.bitcastToAPInt());
    }

    APFloat U = Op->getValueAPF();

    if (IntrinsicID == Intrinsic::wasm_trunc_signed ||
        IntrinsicID == Intrinsic::wasm_trunc_unsigned) {
      bool Signed = IntrinsicID == Intrinsic::wasm_trunc_signed;

      if (U.isNaN())
        return nullptr;

      unsigned Width = Ty->getIntegerBitWidth();
      APSInt Int(Width, !Signed);
      bool IsExact = false;
      APFloat::opStatus Status =
          U.convertToInteger(Int, APFloat::rmTowardZero, &IsExact);

      if (Status == APFloat::opOK || Status == APFloat::opInexact)
        return ConstantInt::get(Ty, Int);

      return nullptr;
    }

    if (IntrinsicID == Intrinsic::fptoui_sat ||
        IntrinsicID == Intrinsic::fptosi_sat) {
      // convertToInteger() already has the desired saturation semantics.
      APSInt Int(Ty->getIntegerBitWidth(),
                 IntrinsicID == Intrinsic::fptoui_sat);
      bool IsExact;
      U.convertToInteger(Int, APFloat::rmTowardZero, &IsExact);
      return ConstantInt::get(Ty, Int);
    }

    if (IntrinsicID == Intrinsic::canonicalize)
      return constantFoldCanonicalize(Ty, Call, U);

#if defined(HAS_IEE754_FLOAT128) && defined(HAS_LOGF128)
    if (Ty->isFP128Ty()) {
      if (IntrinsicID == Intrinsic::log) {
        float128 Result = logf128(Op->getValueAPF().convertToQuad());
        return GetConstantFoldFPValue128(Result, Ty);
      }

      LibFunc Fp128Func = NotLibFunc;
      if (TLI->getLibFunc(Name, Fp128Func) && TLI->has(Fp128Func) &&
          Fp128Func == LibFunc_logl)
        return ConstantFoldFP128(logf128, Op->getValueAPF(), Ty);
    }
#endif

    if (!Ty->isHalfTy() && !Ty->isFloatTy() && !Ty->isDoubleTy())
      return nullptr;

    // Use internal versions of these intrinsics.

    if (IntrinsicID == Intrinsic::nearbyint || IntrinsicID == Intrinsic::rint) {
      U.roundToIntegral(APFloat::rmNearestTiesToEven);
      return ConstantFP::get(Ty->getContext(), U);
    }

    if (IntrinsicID == Intrinsic::round) {
      U.roundToIntegral(APFloat::rmNearestTiesToAway);
      return ConstantFP::get(Ty->getContext(), U);
    }

    if (IntrinsicID == Intrinsic::roundeven) {
      U.roundToIntegral(APFloat::rmNearestTiesToEven);
      return ConstantFP::get(Ty->getContext(), U);
    }

    if (IntrinsicID == Intrinsic::ceil) {
      U.roundToIntegral(APFloat::rmTowardPositive);
      return ConstantFP::get(Ty->getContext(), U);
    }

    if (IntrinsicID == Intrinsic::floor) {
      U.roundToIntegral(APFloat::rmTowardNegative);
      return ConstantFP::get(Ty->getContext(), U);
    }

    if (IntrinsicID == Intrinsic::trunc) {
      U.roundToIntegral(APFloat::rmTowardZero);
      return ConstantFP::get(Ty->getContext(), U);
    }

    if (IntrinsicID == Intrinsic::fabs) {
      U.clearSign();
      return ConstantFP::get(Ty->getContext(), U);
    }

    if (IntrinsicID == Intrinsic::amdgcn_fract) {
      // The v_fract instruction behaves like the OpenCL spec, which defines
      // fract(x) as fmin(x - floor(x), 0x1.fffffep-1f): "The min() operator is
      //   there to prevent fract(-small) from returning 1.0. It returns the
      //   largest positive floating-point number less than 1.0."
      APFloat FloorU(U);
      FloorU.roundToIntegral(APFloat::rmTowardNegative);
      APFloat FractU(U - FloorU);
      APFloat AlmostOne(U.getSemantics(), 1);
      AlmostOne.next(/*nextDown*/ true);
      return ConstantFP::get(Ty->getContext(), minimum(FractU, AlmostOne));
    }

    // Rounding operations (floor, trunc, ceil, round and nearbyint) do not
    // raise FP exceptions, unless the argument is signaling NaN.

    std::optional<APFloat::roundingMode> RM;
    switch (IntrinsicID) {
    default:
      break;
    case Intrinsic::experimental_constrained_nearbyint:
    case Intrinsic::experimental_constrained_rint: {
      auto CI = cast<ConstrainedFPIntrinsic>(Call);
      RM = CI->getRoundingMode();
      if (!RM || *RM == RoundingMode::Dynamic)
        return nullptr;
      break;
    }
    case Intrinsic::experimental_constrained_round:
      RM = APFloat::rmNearestTiesToAway;
      break;
    case Intrinsic::experimental_constrained_ceil:
      RM = APFloat::rmTowardPositive;
      break;
    case Intrinsic::experimental_constrained_floor:
      RM = APFloat::rmTowardNegative;
      break;
    case Intrinsic::experimental_constrained_trunc:
      RM = APFloat::rmTowardZero;
      break;
    }
    if (RM) {
      auto CI = cast<ConstrainedFPIntrinsic>(Call);
      if (U.isFinite()) {
        APFloat::opStatus St = U.roundToIntegral(*RM);
        if (IntrinsicID == Intrinsic::experimental_constrained_rint &&
            St == APFloat::opInexact) {
          std::optional<fp::ExceptionBehavior> EB = CI->getExceptionBehavior();
          if (EB && *EB == fp::ebStrict)
            return nullptr;
        }
      } else if (U.isSignaling()) {
        std::optional<fp::ExceptionBehavior> EB = CI->getExceptionBehavior();
        if (EB && *EB != fp::ebIgnore)
          return nullptr;
        U = APFloat::getQNaN(U.getSemantics());
      }
      return ConstantFP::get(Ty->getContext(), U);
    }

    /// We only fold functions with finite arguments. Folding NaN and inf is
    /// likely to be aborted with an exception anyway, and some host libms
    /// have known errors raising exceptions.
    if (!U.isFinite())
      return nullptr;

    /// Currently APFloat versions of these functions do not exist, so we use
    /// the host native double versions.  Float versions are not called
    /// directly but for all these it is true (float)(f((double)arg)) ==
    /// f(arg).  Long double not supported yet.
    const APFloat &APF = Op->getValueAPF();

    switch (IntrinsicID) {
      default: break;
      case Intrinsic::log:
        return ConstantFoldFP(log, APF, Ty);
      case Intrinsic::log2:
        // TODO: What about hosts that lack a C99 library?
        return ConstantFoldFP(log2, APF, Ty);
      case Intrinsic::log10:
        // TODO: What about hosts that lack a C99 library?
        return ConstantFoldFP(log10, APF, Ty);
      case Intrinsic::exp:
        return ConstantFoldFP(exp, APF, Ty);
      case Intrinsic::exp2:
        // Fold exp2(x) as pow(2, x), in case the host lacks a C99 library.
        return ConstantFoldBinaryFP(pow, APFloat(2.0), APF, Ty);
      case Intrinsic::exp10:
        // Fold exp10(x) as pow(10, x), in case the host lacks a C99 library.
        return ConstantFoldBinaryFP(pow, APFloat(10.0), APF, Ty);
      case Intrinsic::sin:
        return ConstantFoldFP(sin, APF, Ty);
      case Intrinsic::cos:
        return ConstantFoldFP(cos, APF, Ty);
      case Intrinsic::sqrt:
        return ConstantFoldFP(sqrt, APF, Ty);
      case Intrinsic::amdgcn_cos:
      case Intrinsic::amdgcn_sin: {
        double V = getValueAsDouble(Op);
        if (V < -256.0 || V > 256.0)
          // The gfx8 and gfx9 architectures handle arguments outside the range
          // [-256, 256] differently. This should be a rare case so bail out
          // rather than trying to handle the difference.
          return nullptr;
        bool IsCos = IntrinsicID == Intrinsic::amdgcn_cos;
        double V4 = V * 4.0;
        if (V4 == floor(V4)) {
          // Force exact results for quarter-integer inputs.
          const double SinVals[4] = { 0.0, 1.0, 0.0, -1.0 };
          V = SinVals[((int)V4 + (IsCos ? 1 : 0)) & 3];
        } else {
          if (IsCos)
            V = cos(V * 2.0 * numbers::pi);
          else
            V = sin(V * 2.0 * numbers::pi);
        }
        return GetConstantFoldFPValue(V, Ty);
      }
    }

    if (!TLI)
      return nullptr;

    LibFunc Func = NotLibFunc;
    if (!TLI->getLibFunc(Name, Func))
      return nullptr;

    switch (Func) {
    default:
      break;
    case LibFunc_acos:
    case LibFunc_acosf:
    case LibFunc_acos_finite:
    case LibFunc_acosf_finite:
      if (TLI->has(Func))
        return ConstantFoldFP(acos, APF, Ty);
      break;
    case LibFunc_asin:
    case LibFunc_asinf:
    case LibFunc_asin_finite:
    case LibFunc_asinf_finite:
      if (TLI->has(Func))
        return ConstantFoldFP(asin, APF, Ty);
      break;
    case LibFunc_atan:
    case LibFunc_atanf:
      if (TLI->has(Func))
        return ConstantFoldFP(atan, APF, Ty);
      break;
    case LibFunc_ceil:
    case LibFunc_ceilf:
      if (TLI->has(Func)) {
        U.roundToIntegral(APFloat::rmTowardPositive);
        return ConstantFP::get(Ty->getContext(), U);
      }
      break;
    case LibFunc_cos:
    case LibFunc_cosf:
      if (TLI->has(Func))
        return ConstantFoldFP(cos, APF, Ty);
      break;
    case LibFunc_cosh:
    case LibFunc_coshf:
    case LibFunc_cosh_finite:
    case LibFunc_coshf_finite:
      if (TLI->has(Func))
        return ConstantFoldFP(cosh, APF, Ty);
      break;
    case LibFunc_exp:
    case LibFunc_expf:
    case LibFunc_exp_finite:
    case LibFunc_expf_finite:
      if (TLI->has(Func))
        return ConstantFoldFP(exp, APF, Ty);
      break;
    case LibFunc_exp2:
    case LibFunc_exp2f:
    case LibFunc_exp2_finite:
    case LibFunc_exp2f_finite:
      if (TLI->has(Func))
        // Fold exp2(x) as pow(2, x), in case the host lacks a C99 library.
        return ConstantFoldBinaryFP(pow, APFloat(2.0), APF, Ty);
      break;
    case LibFunc_fabs:
    case LibFunc_fabsf:
      if (TLI->has(Func)) {
        U.clearSign();
        return ConstantFP::get(Ty->getContext(), U);
      }
      break;
    case LibFunc_floor:
    case LibFunc_floorf:
      if (TLI->has(Func)) {
        U.roundToIntegral(APFloat::rmTowardNegative);
        return ConstantFP::get(Ty->getContext(), U);
      }
      break;
    case LibFunc_log:
    case LibFunc_logf:
    case LibFunc_log_finite:
    case LibFunc_logf_finite:
      if (!APF.isNegative() && !APF.isZero() && TLI->has(Func))
        return ConstantFoldFP(log, APF, Ty);
      break;
    case LibFunc_log2:
    case LibFunc_log2f:
    case LibFunc_log2_finite:
    case LibFunc_log2f_finite:
      if (!APF.isNegative() && !APF.isZero() && TLI->has(Func))
        // TODO: What about hosts that lack a C99 library?
        return ConstantFoldFP(log2, APF, Ty);
      break;
    case LibFunc_log10:
    case LibFunc_log10f:
    case LibFunc_log10_finite:
    case LibFunc_log10f_finite:
      if (!APF.isNegative() && !APF.isZero() && TLI->has(Func))
        // TODO: What about hosts that lack a C99 library?
        return ConstantFoldFP(log10, APF, Ty);
      break;
    case LibFunc_logl:
      return nullptr;
    case LibFunc_nearbyint:
    case LibFunc_nearbyintf:
    case LibFunc_rint:
    case LibFunc_rintf:
      if (TLI->has(Func)) {
        U.roundToIntegral(APFloat::rmNearestTiesToEven);
        return ConstantFP::get(Ty->getContext(), U);
      }
      break;
    case LibFunc_round:
    case LibFunc_roundf:
      if (TLI->has(Func)) {
        U.roundToIntegral(APFloat::rmNearestTiesToAway);
        return ConstantFP::get(Ty->getContext(), U);
      }
      break;
    case LibFunc_sin:
    case LibFunc_sinf:
      if (TLI->has(Func))
        return ConstantFoldFP(sin, APF, Ty);
      break;
    case LibFunc_sinh:
    case LibFunc_sinhf:
    case LibFunc_sinh_finite:
    case LibFunc_sinhf_finite:
      if (TLI->has(Func))
        return ConstantFoldFP(sinh, APF, Ty);
      break;
    case LibFunc_sqrt:
    case LibFunc_sqrtf:
      if (!APF.isNegative() && TLI->has(Func))
        return ConstantFoldFP(sqrt, APF, Ty);
      break;
    case LibFunc_tan:
    case LibFunc_tanf:
      if (TLI->has(Func))
        return ConstantFoldFP(tan, APF, Ty);
      break;
    case LibFunc_tanh:
    case LibFunc_tanhf:
      if (TLI->has(Func))
        return ConstantFoldFP(tanh, APF, Ty);
      break;
    case LibFunc_trunc:
    case LibFunc_truncf:
      if (TLI->has(Func)) {
        U.roundToIntegral(APFloat::rmTowardZero);
        return ConstantFP::get(Ty->getContext(), U);
      }
      break;
    }
    return nullptr;
  }

  if (auto *Op = dyn_cast<ConstantInt>(Operands[0])) {
    switch (IntrinsicID) {
    case Intrinsic::bswap:
      return ConstantInt::get(Ty->getContext(), Op->getValue().byteSwap());
    case Intrinsic::ctpop:
      return ConstantInt::get(Ty, Op->getValue().popcount());
    case Intrinsic::bitreverse:
      return ConstantInt::get(Ty->getContext(), Op->getValue().reverseBits());
    case Intrinsic::convert_from_fp16: {
      APFloat Val(APFloat::IEEEhalf(), Op->getValue());

      bool lost = false;
      APFloat::opStatus status = Val.convert(
          Ty->getFltSemantics(), APFloat::rmNearestTiesToEven, &lost);

      // Conversion is always precise.
      (void)status;
      assert(status != APFloat::opInexact && !lost &&
             "Precision lost during fp16 constfolding");

      return ConstantFP::get(Ty->getContext(), Val);
    }

    case Intrinsic::amdgcn_s_wqm: {
      uint64_t Val = Op->getZExtValue();
      Val |= (Val & 0x5555555555555555ULL) << 1 |
             ((Val >> 1) & 0x5555555555555555ULL);
      Val |= (Val & 0x3333333333333333ULL) << 2 |
             ((Val >> 2) & 0x3333333333333333ULL);
      return ConstantInt::get(Ty, Val);
    }

    case Intrinsic::amdgcn_s_quadmask: {
      uint64_t Val = Op->getZExtValue();
      uint64_t QuadMask = 0;
      for (unsigned I = 0; I < Op->getBitWidth() / 4; ++I, Val >>= 4) {
        if (!(Val & 0xF))
          continue;

        QuadMask |= (1ULL << I);
      }
      return ConstantInt::get(Ty, QuadMask);
    }

    case Intrinsic::amdgcn_s_bitreplicate: {
      uint64_t Val = Op->getZExtValue();
      Val = (Val & 0x000000000000FFFFULL) | (Val & 0x00000000FFFF0000ULL) << 16;
      Val = (Val & 0x000000FF000000FFULL) | (Val & 0x0000FF000000FF00ULL) << 8;
      Val = (Val & 0x000F000F000F000FULL) | (Val & 0x00F000F000F000F0ULL) << 4;
      Val = (Val & 0x0303030303030303ULL) | (Val & 0x0C0C0C0C0C0C0C0CULL) << 2;
      Val = (Val & 0x1111111111111111ULL) | (Val & 0x2222222222222222ULL) << 1;
      Val = Val | Val << 1;
      return ConstantInt::get(Ty, Val);
    }

    default:
      return nullptr;
    }
  }

  switch (IntrinsicID) {
  default: break;
  case Intrinsic::vector_reduce_add:
  case Intrinsic::vector_reduce_mul:
  case Intrinsic::vector_reduce_and:
  case Intrinsic::vector_reduce_or:
  case Intrinsic::vector_reduce_xor:
  case Intrinsic::vector_reduce_smin:
  case Intrinsic::vector_reduce_smax:
  case Intrinsic::vector_reduce_umin:
  case Intrinsic::vector_reduce_umax:
    if (Constant *C = constantFoldVectorReduce(IntrinsicID, Operands[0]))
      return C;
    break;
  }

  // Support ConstantVector in case we have an Undef in the top.
  if (isa<ConstantVector>(Operands[0]) ||
      isa<ConstantDataVector>(Operands[0])) {
    auto *Op = cast<Constant>(Operands[0]);
    switch (IntrinsicID) {
    default: break;
    case Intrinsic::x86_sse_cvtss2si:
    case Intrinsic::x86_sse_cvtss2si64:
    case Intrinsic::x86_sse2_cvtsd2si:
    case Intrinsic::x86_sse2_cvtsd2si64:
      if (ConstantFP *FPOp =
              dyn_cast_or_null<ConstantFP>(Op->getAggregateElement(0U)))
        return ConstantFoldSSEConvertToInt(FPOp->getValueAPF(),
                                           /*roundTowardZero=*/false, Ty,
                                           /*IsSigned*/true);
      break;
    case Intrinsic::x86_sse_cvttss2si:
    case Intrinsic::x86_sse_cvttss2si64:
    case Intrinsic::x86_sse2_cvttsd2si:
    case Intrinsic::x86_sse2_cvttsd2si64:
      if (ConstantFP *FPOp =
              dyn_cast_or_null<ConstantFP>(Op->getAggregateElement(0U)))
        return ConstantFoldSSEConvertToInt(FPOp->getValueAPF(),
                                           /*roundTowardZero=*/true, Ty,
                                           /*IsSigned*/true);
      break;
    }
  }

  return nullptr;
}

static Constant *evaluateCompare(const APFloat &Op1, const APFloat &Op2,
                                 const ConstrainedFPIntrinsic *Call) {
  APFloat::opStatus St = APFloat::opOK;
  auto *FCmp = cast<ConstrainedFPCmpIntrinsic>(Call);
  FCmpInst::Predicate Cond = FCmp->getPredicate();
  if (FCmp->isSignaling()) {
    if (Op1.isNaN() || Op2.isNaN())
      St = APFloat::opInvalidOp;
  } else {
    if (Op1.isSignaling() || Op2.isSignaling())
      St = APFloat::opInvalidOp;
  }
  bool Result = FCmpInst::compare(Op1, Op2, Cond);
  if (mayFoldConstrained(const_cast<ConstrainedFPCmpIntrinsic *>(FCmp), St))
    return ConstantInt::get(Call->getType()->getScalarType(), Result);
  return nullptr;
}

static Constant *ConstantFoldLibCall2(StringRef Name, Type *Ty,
                                      ArrayRef<Constant *> Operands,
                                      const TargetLibraryInfo *TLI) {
  if (!TLI)
    return nullptr;

  LibFunc Func = NotLibFunc;
  if (!TLI->getLibFunc(Name, Func))
    return nullptr;

  const auto *Op1 = dyn_cast<ConstantFP>(Operands[0]);
  if (!Op1)
    return nullptr;

  const auto *Op2 = dyn_cast<ConstantFP>(Operands[1]);
  if (!Op2)
    return nullptr;

  const APFloat &Op1V = Op1->getValueAPF();
  const APFloat &Op2V = Op2->getValueAPF();

  switch (Func) {
  default:
    break;
  case LibFunc_pow:
  case LibFunc_powf:
  case LibFunc_pow_finite:
  case LibFunc_powf_finite:
    if (TLI->has(Func))
      return ConstantFoldBinaryFP(pow, Op1V, Op2V, Ty);
    break;
  case LibFunc_fmod:
  case LibFunc_fmodf:
    if (TLI->has(Func)) {
      APFloat V = Op1->getValueAPF();
      if (APFloat::opStatus::opOK == V.mod(Op2->getValueAPF()))
        return ConstantFP::get(Ty->getContext(), V);
    }
    break;
  case LibFunc_remainder:
  case LibFunc_remainderf:
    if (TLI->has(Func)) {
      APFloat V = Op1->getValueAPF();
      if (APFloat::opStatus::opOK == V.remainder(Op2->getValueAPF()))
        return ConstantFP::get(Ty->getContext(), V);
    }
    break;
  case LibFunc_atan2:
  case LibFunc_atan2f:
    // atan2(+/-0.0, +/-0.0) is known to raise an exception on some libm
    // (Solaris), so we do not assume a known result for that.
    if (Op1V.isZero() && Op2V.isZero())
      return nullptr;
    [[fallthrough]];
  case LibFunc_atan2_finite:
  case LibFunc_atan2f_finite:
    if (TLI->has(Func))
      return ConstantFoldBinaryFP(atan2, Op1V, Op2V, Ty);
    break;
  }

  return nullptr;
}

static Constant *ConstantFoldIntrinsicCall2(Intrinsic::ID IntrinsicID, Type *Ty,
                                            ArrayRef<Constant *> Operands,
                                            const CallBase *Call) {
  assert(Operands.size() == 2 && "Wrong number of operands.");

  if (Ty->isFloatingPointTy()) {
    // TODO: We should have undef handling for all of the FP intrinsics that
    //       are attempted to be folded in this function.
    bool IsOp0Undef = isa<UndefValue>(Operands[0]);
    bool IsOp1Undef = isa<UndefValue>(Operands[1]);
    switch (IntrinsicID) {
    case Intrinsic::maxnum:
    case Intrinsic::minnum:
    case Intrinsic::maximum:
    case Intrinsic::minimum:
      // If one argument is undef, return the other argument.
      if (IsOp0Undef)
        return Operands[1];
      if (IsOp1Undef)
        return Operands[0];
      break;
    }
  }

  if (const auto *Op1 = dyn_cast<ConstantFP>(Operands[0])) {
    const APFloat &Op1V = Op1->getValueAPF();

    if (const auto *Op2 = dyn_cast<ConstantFP>(Operands[1])) {
      if (Op2->getType() != Op1->getType())
        return nullptr;
      const APFloat &Op2V = Op2->getValueAPF();

      if (const auto *ConstrIntr =
              dyn_cast_if_present<ConstrainedFPIntrinsic>(Call)) {
        RoundingMode RM = getEvaluationRoundingMode(ConstrIntr);
        APFloat Res = Op1V;
        APFloat::opStatus St;
        switch (IntrinsicID) {
        default:
          return nullptr;
        case Intrinsic::experimental_constrained_fadd:
          St = Res.add(Op2V, RM);
          break;
        case Intrinsic::experimental_constrained_fsub:
          St = Res.subtract(Op2V, RM);
          break;
        case Intrinsic::experimental_constrained_fmul:
          St = Res.multiply(Op2V, RM);
          break;
        case Intrinsic::experimental_constrained_fdiv:
          St = Res.divide(Op2V, RM);
          break;
        case Intrinsic::experimental_constrained_frem:
          St = Res.mod(Op2V);
          break;
        case Intrinsic::experimental_constrained_fcmp:
        case Intrinsic::experimental_constrained_fcmps:
          return evaluateCompare(Op1V, Op2V, ConstrIntr);
        }
        if (mayFoldConstrained(const_cast<ConstrainedFPIntrinsic *>(ConstrIntr),
                               St))
          return ConstantFP::get(Ty->getContext(), Res);
        return nullptr;
      }

      switch (IntrinsicID) {
      default:
        break;
      case Intrinsic::copysign:
        return ConstantFP::get(Ty->getContext(), APFloat::copySign(Op1V, Op2V));
      case Intrinsic::minnum:
        return ConstantFP::get(Ty->getContext(), minnum(Op1V, Op2V));
      case Intrinsic::maxnum:
        return ConstantFP::get(Ty->getContext(), maxnum(Op1V, Op2V));
      case Intrinsic::minimum:
        return ConstantFP::get(Ty->getContext(), minimum(Op1V, Op2V));
      case Intrinsic::maximum:
        return ConstantFP::get(Ty->getContext(), maximum(Op1V, Op2V));
      }

      if (!Ty->isHalfTy() && !Ty->isFloatTy() && !Ty->isDoubleTy())
        return nullptr;

      switch (IntrinsicID) {
      default:
        break;
      case Intrinsic::pow:
        return ConstantFoldBinaryFP(pow, Op1V, Op2V, Ty);
      case Intrinsic::amdgcn_fmul_legacy:
        // The legacy behaviour is that multiplying +/- 0.0 by anything, even
        // NaN or infinity, gives +0.0.
        if (Op1V.isZero() || Op2V.isZero())
          return ConstantFP::getZero(Ty);
        return ConstantFP::get(Ty->getContext(), Op1V * Op2V);
      }

    } else if (auto *Op2C = dyn_cast<ConstantInt>(Operands[1])) {
      switch (IntrinsicID) {
      case Intrinsic::ldexp: {
        return ConstantFP::get(
            Ty->getContext(),
            scalbn(Op1V, Op2C->getSExtValue(), APFloat::rmNearestTiesToEven));
      }
      case Intrinsic::is_fpclass: {
        FPClassTest Mask = static_cast<FPClassTest>(Op2C->getZExtValue());
        bool Result =
          ((Mask & fcSNan) && Op1V.isNaN() && Op1V.isSignaling()) ||
          ((Mask & fcQNan) && Op1V.isNaN() && !Op1V.isSignaling()) ||
          ((Mask & fcNegInf) && Op1V.isNegInfinity()) ||
          ((Mask & fcNegNormal) && Op1V.isNormal() && Op1V.isNegative()) ||
          ((Mask & fcNegSubnormal) && Op1V.isDenormal() && Op1V.isNegative()) ||
          ((Mask & fcNegZero) && Op1V.isZero() && Op1V.isNegative()) ||
          ((Mask & fcPosZero) && Op1V.isZero() && !Op1V.isNegative()) ||
          ((Mask & fcPosSubnormal) && Op1V.isDenormal() && !Op1V.isNegative()) ||
          ((Mask & fcPosNormal) && Op1V.isNormal() && !Op1V.isNegative()) ||
          ((Mask & fcPosInf) && Op1V.isPosInfinity());
        return ConstantInt::get(Ty, Result);
      }
      case Intrinsic::powi: {
        int Exp = static_cast<int>(Op2C->getSExtValue());
        switch (Ty->getTypeID()) {
        case Type::HalfTyID:
        case Type::FloatTyID: {
          APFloat Res(static_cast<float>(std::pow(Op1V.convertToFloat(), Exp)));
          if (Ty->isHalfTy()) {
            bool Unused;
            Res.convert(APFloat::IEEEhalf(), APFloat::rmNearestTiesToEven,
                        &Unused);
          }
          return ConstantFP::get(Ty->getContext(), Res);
        }
        case Type::DoubleTyID:
          return ConstantFP::get(Ty, std::pow(Op1V.convertToDouble(), Exp));
        default:
          return nullptr;
        }
      }
      default:
        break;
      }
    }
    return nullptr;
  }

  if (Operands[0]->getType()->isIntegerTy() &&
      Operands[1]->getType()->isIntegerTy()) {
    const APInt *C0, *C1;
    if (!getConstIntOrUndef(Operands[0], C0) ||
        !getConstIntOrUndef(Operands[1], C1))
      return nullptr;

    switch (IntrinsicID) {
    default: break;
    case Intrinsic::smax:
    case Intrinsic::smin:
    case Intrinsic::umax:
    case Intrinsic::umin:
      // This is the same as for binary ops - poison propagates.
      // TODO: Poison handling should be consolidated.
      if (isa<PoisonValue>(Operands[0]) || isa<PoisonValue>(Operands[1]))
        return PoisonValue::get(Ty);

      if (!C0 && !C1)
        return UndefValue::get(Ty);
      if (!C0 || !C1)
        return MinMaxIntrinsic::getSaturationPoint(IntrinsicID, Ty);
      return ConstantInt::get(
          Ty, ICmpInst::compare(*C0, *C1,
                                MinMaxIntrinsic::getPredicate(IntrinsicID))
                  ? *C0
                  : *C1);

    case Intrinsic::scmp:
    case Intrinsic::ucmp:
      if (isa<PoisonValue>(Operands[0]) || isa<PoisonValue>(Operands[1]))
        return PoisonValue::get(Ty);

      if (!C0 || !C1)
        return ConstantInt::get(Ty, 0);

      int Res;
      if (IntrinsicID == Intrinsic::scmp)
        Res = C0->sgt(*C1) ? 1 : C0->slt(*C1) ? -1 : 0;
      else
        Res = C0->ugt(*C1) ? 1 : C0->ult(*C1) ? -1 : 0;
      return ConstantInt::get(Ty, Res, /*IsSigned=*/true);

    case Intrinsic::usub_with_overflow:
    case Intrinsic::ssub_with_overflow:
      // X - undef -> { 0, false }
      // undef - X -> { 0, false }
      if (!C0 || !C1)
        return Constant::getNullValue(Ty);
      [[fallthrough]];
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::sadd_with_overflow:
      // X + undef -> { -1, false }
      // undef + x -> { -1, false }
      if (!C0 || !C1) {
        return ConstantStruct::get(
            cast<StructType>(Ty),
            {Constant::getAllOnesValue(Ty->getStructElementType(0)),
             Constant::getNullValue(Ty->getStructElementType(1))});
      }
      [[fallthrough]];
    case Intrinsic::smul_with_overflow:
    case Intrinsic::umul_with_overflow: {
      // undef * X -> { 0, false }
      // X * undef -> { 0, false }
      if (!C0 || !C1)
        return Constant::getNullValue(Ty);

      APInt Res;
      bool Overflow;
      switch (IntrinsicID) {
      default: llvm_unreachable("Invalid case");
      case Intrinsic::sadd_with_overflow:
        Res = C0->sadd_ov(*C1, Overflow);
        break;
      case Intrinsic::uadd_with_overflow:
        Res = C0->uadd_ov(*C1, Overflow);
        break;
      case Intrinsic::ssub_with_overflow:
        Res = C0->ssub_ov(*C1, Overflow);
        break;
      case Intrinsic::usub_with_overflow:
        Res = C0->usub_ov(*C1, Overflow);
        break;
      case Intrinsic::smul_with_overflow:
        Res = C0->smul_ov(*C1, Overflow);
        break;
      case Intrinsic::umul_with_overflow:
        Res = C0->umul_ov(*C1, Overflow);
        break;
      }
      Constant *Ops[] = {
        ConstantInt::get(Ty->getContext(), Res),
        ConstantInt::get(Type::getInt1Ty(Ty->getContext()), Overflow)
      };
      return ConstantStruct::get(cast<StructType>(Ty), Ops);
    }
    case Intrinsic::uadd_sat:
    case Intrinsic::sadd_sat:
      // This is the same as for binary ops - poison propagates.
      // TODO: Poison handling should be consolidated.
      if (isa<PoisonValue>(Operands[0]) || isa<PoisonValue>(Operands[1]))
        return PoisonValue::get(Ty);

      if (!C0 && !C1)
        return UndefValue::get(Ty);
      if (!C0 || !C1)
        return Constant::getAllOnesValue(Ty);
      if (IntrinsicID == Intrinsic::uadd_sat)
        return ConstantInt::get(Ty, C0->uadd_sat(*C1));
      else
        return ConstantInt::get(Ty, C0->sadd_sat(*C1));
    case Intrinsic::usub_sat:
    case Intrinsic::ssub_sat:
      // This is the same as for binary ops - poison propagates.
      // TODO: Poison handling should be consolidated.
      if (isa<PoisonValue>(Operands[0]) || isa<PoisonValue>(Operands[1]))
        return PoisonValue::get(Ty);

      if (!C0 && !C1)
        return UndefValue::get(Ty);
      if (!C0 || !C1)
        return Constant::getNullValue(Ty);
      if (IntrinsicID == Intrinsic::usub_sat)
        return ConstantInt::get(Ty, C0->usub_sat(*C1));
      else
        return ConstantInt::get(Ty, C0->ssub_sat(*C1));
    case Intrinsic::cttz:
    case Intrinsic::ctlz:
      assert(C1 && "Must be constant int");

      // cttz(0, 1) and ctlz(0, 1) are poison.
      if (C1->isOne() && (!C0 || C0->isZero()))
        return PoisonValue::get(Ty);
      if (!C0)
        return Constant::getNullValue(Ty);
      if (IntrinsicID == Intrinsic::cttz)
        return ConstantInt::get(Ty, C0->countr_zero());
      else
        return ConstantInt::get(Ty, C0->countl_zero());

    case Intrinsic::abs:
      assert(C1 && "Must be constant int");
      assert((C1->isOne() || C1->isZero()) && "Must be 0 or 1");

      // Undef or minimum val operand with poison min --> undef
      if (C1->isOne() && (!C0 || C0->isMinSignedValue()))
        return UndefValue::get(Ty);

      // Undef operand with no poison min --> 0 (sign bit must be clear)
      if (!C0)
        return Constant::getNullValue(Ty);

      return ConstantInt::get(Ty, C0->abs());
    case Intrinsic::amdgcn_wave_reduce_umin:
    case Intrinsic::amdgcn_wave_reduce_umax:
      return dyn_cast<Constant>(Operands[0]);
    }

    return nullptr;
  }

  // Support ConstantVector in case we have an Undef in the top.
  if ((isa<ConstantVector>(Operands[0]) ||
       isa<ConstantDataVector>(Operands[0])) &&
      // Check for default rounding mode.
      // FIXME: Support other rounding modes?
      isa<ConstantInt>(Operands[1]) &&
      cast<ConstantInt>(Operands[1])->getValue() == 4) {
    auto *Op = cast<Constant>(Operands[0]);
    switch (IntrinsicID) {
    default: break;
    case Intrinsic::x86_avx512_vcvtss2si32:
    case Intrinsic::x86_avx512_vcvtss2si64:
    case Intrinsic::x86_avx512_vcvtsd2si32:
    case Intrinsic::x86_avx512_vcvtsd2si64:
      if (ConstantFP *FPOp =
              dyn_cast_or_null<ConstantFP>(Op->getAggregateElement(0U)))
        return ConstantFoldSSEConvertToInt(FPOp->getValueAPF(),
                                           /*roundTowardZero=*/false, Ty,
                                           /*IsSigned*/true);
      break;
    case Intrinsic::x86_avx512_vcvtss2usi32:
    case Intrinsic::x86_avx512_vcvtss2usi64:
    case Intrinsic::x86_avx512_vcvtsd2usi32:
    case Intrinsic::x86_avx512_vcvtsd2usi64:
      if (ConstantFP *FPOp =
              dyn_cast_or_null<ConstantFP>(Op->getAggregateElement(0U)))
        return ConstantFoldSSEConvertToInt(FPOp->getValueAPF(),
                                           /*roundTowardZero=*/false, Ty,
                                           /*IsSigned*/false);
      break;
    case Intrinsic::x86_avx512_cvttss2si:
    case Intrinsic::x86_avx512_cvttss2si64:
    case Intrinsic::x86_avx512_cvttsd2si:
    case Intrinsic::x86_avx512_cvttsd2si64:
      if (ConstantFP *FPOp =
              dyn_cast_or_null<ConstantFP>(Op->getAggregateElement(0U)))
        return ConstantFoldSSEConvertToInt(FPOp->getValueAPF(),
                                           /*roundTowardZero=*/true, Ty,
                                           /*IsSigned*/true);
      break;
    case Intrinsic::x86_avx512_cvttss2usi:
    case Intrinsic::x86_avx512_cvttss2usi64:
    case Intrinsic::x86_avx512_cvttsd2usi:
    case Intrinsic::x86_avx512_cvttsd2usi64:
      if (ConstantFP *FPOp =
              dyn_cast_or_null<ConstantFP>(Op->getAggregateElement(0U)))
        return ConstantFoldSSEConvertToInt(FPOp->getValueAPF(),
                                           /*roundTowardZero=*/true, Ty,
                                           /*IsSigned*/false);
      break;
    }
  }
  return nullptr;
}

static APFloat ConstantFoldAMDGCNCubeIntrinsic(Intrinsic::ID IntrinsicID,
                                               const APFloat &S0,
                                               const APFloat &S1,
                                               const APFloat &S2) {
  unsigned ID;
  const fltSemantics &Sem = S0.getSemantics();
  APFloat MA(Sem), SC(Sem), TC(Sem);
  if (abs(S2) >= abs(S0) && abs(S2) >= abs(S1)) {
    if (S2.isNegative() && S2.isNonZero() && !S2.isNaN()) {
      // S2 < 0
      ID = 5;
      SC = -S0;
    } else {
      ID = 4;
      SC = S0;
    }
    MA = S2;
    TC = -S1;
  } else if (abs(S1) >= abs(S0)) {
    if (S1.isNegative() && S1.isNonZero() && !S1.isNaN()) {
      // S1 < 0
      ID = 3;
      TC = -S2;
    } else {
      ID = 2;
      TC = S2;
    }
    MA = S1;
    SC = S0;
  } else {
    if (S0.isNegative() && S0.isNonZero() && !S0.isNaN()) {
      // S0 < 0
      ID = 1;
      SC = S2;
    } else {
      ID = 0;
      SC = -S2;
    }
    MA = S0;
    TC = -S1;
  }
  switch (IntrinsicID) {
  default:
    llvm_unreachable("unhandled amdgcn cube intrinsic");
  case Intrinsic::amdgcn_cubeid:
    return APFloat(Sem, ID);
  case Intrinsic::amdgcn_cubema:
    return MA + MA;
  case Intrinsic::amdgcn_cubesc:
    return SC;
  case Intrinsic::amdgcn_cubetc:
    return TC;
  }
}

static Constant *ConstantFoldAMDGCNPermIntrinsic(ArrayRef<Constant *> Operands,
                                                 Type *Ty) {
  const APInt *C0, *C1, *C2;
  if (!getConstIntOrUndef(Operands[0], C0) ||
      !getConstIntOrUndef(Operands[1], C1) ||
      !getConstIntOrUndef(Operands[2], C2))
    return nullptr;

  if (!C2)
    return UndefValue::get(Ty);

  APInt Val(32, 0);
  unsigned NumUndefBytes = 0;
  for (unsigned I = 0; I < 32; I += 8) {
    unsigned Sel = C2->extractBitsAsZExtValue(8, I);
    unsigned B = 0;

    if (Sel >= 13)
      B = 0xff;
    else if (Sel == 12)
      B = 0x00;
    else {
      const APInt *Src = ((Sel & 10) == 10 || (Sel & 12) == 4) ? C0 : C1;
      if (!Src)
        ++NumUndefBytes;
      else if (Sel < 8)
        B = Src->extractBitsAsZExtValue(8, (Sel & 3) * 8);
      else
        B = Src->extractBitsAsZExtValue(1, (Sel & 1) ? 31 : 15) * 0xff;
    }

    Val.insertBits(B, I, 8);
  }

  if (NumUndefBytes == 4)
    return UndefValue::get(Ty);

  return ConstantInt::get(Ty, Val);
}

static Constant *ConstantFoldScalarCall3(StringRef Name,
                                         Intrinsic::ID IntrinsicID,
                                         Type *Ty,
                                         ArrayRef<Constant *> Operands,
                                         const TargetLibraryInfo *TLI,
                                         const CallBase *Call) {
  assert(Operands.size() == 3 && "Wrong number of operands.");

  if (const auto *Op1 = dyn_cast<ConstantFP>(Operands[0])) {
    if (const auto *Op2 = dyn_cast<ConstantFP>(Operands[1])) {
      if (const auto *Op3 = dyn_cast<ConstantFP>(Operands[2])) {
        const APFloat &C1 = Op1->getValueAPF();
        const APFloat &C2 = Op2->getValueAPF();
        const APFloat &C3 = Op3->getValueAPF();

        if (const auto *ConstrIntr = dyn_cast<ConstrainedFPIntrinsic>(Call)) {
          RoundingMode RM = getEvaluationRoundingMode(ConstrIntr);
          APFloat Res = C1;
          APFloat::opStatus St;
          switch (IntrinsicID) {
          default:
            return nullptr;
          case Intrinsic::experimental_constrained_fma:
          case Intrinsic::experimental_constrained_fmuladd:
            St = Res.fusedMultiplyAdd(C2, C3, RM);
            break;
          }
          if (mayFoldConstrained(
                  const_cast<ConstrainedFPIntrinsic *>(ConstrIntr), St))
            return ConstantFP::get(Ty->getContext(), Res);
          return nullptr;
        }

        switch (IntrinsicID) {
        default: break;
        case Intrinsic::amdgcn_fma_legacy: {
          // The legacy behaviour is that multiplying +/- 0.0 by anything, even
          // NaN or infinity, gives +0.0.
          if (C1.isZero() || C2.isZero()) {
            // It's tempting to just return C3 here, but that would give the
            // wrong result if C3 was -0.0.
            return ConstantFP::get(Ty->getContext(), APFloat(0.0f) + C3);
          }
          [[fallthrough]];
        }
        case Intrinsic::fma:
        case Intrinsic::fmuladd: {
          APFloat V = C1;
          V.fusedMultiplyAdd(C2, C3, APFloat::rmNearestTiesToEven);
          return ConstantFP::get(Ty->getContext(), V);
        }
        case Intrinsic::amdgcn_cubeid:
        case Intrinsic::amdgcn_cubema:
        case Intrinsic::amdgcn_cubesc:
        case Intrinsic::amdgcn_cubetc: {
          APFloat V = ConstantFoldAMDGCNCubeIntrinsic(IntrinsicID, C1, C2, C3);
          return ConstantFP::get(Ty->getContext(), V);
        }
        }
      }
    }
  }

  if (IntrinsicID == Intrinsic::smul_fix ||
      IntrinsicID == Intrinsic::smul_fix_sat) {
    // poison * C -> poison
    // C * poison -> poison
    if (isa<PoisonValue>(Operands[0]) || isa<PoisonValue>(Operands[1]))
      return PoisonValue::get(Ty);

    const APInt *C0, *C1;
    if (!getConstIntOrUndef(Operands[0], C0) ||
        !getConstIntOrUndef(Operands[1], C1))
      return nullptr;

    // undef * C -> 0
    // C * undef -> 0
    if (!C0 || !C1)
      return Constant::getNullValue(Ty);

    // This code performs rounding towards negative infinity in case the result
    // cannot be represented exactly for the given scale. Targets that do care
    // about rounding should use a target hook for specifying how rounding
    // should be done, and provide their own folding to be consistent with
    // rounding. This is the same approach as used by
    // DAGTypeLegalizer::ExpandIntRes_MULFIX.
    unsigned Scale = cast<ConstantInt>(Operands[2])->getZExtValue();
    unsigned Width = C0->getBitWidth();
    assert(Scale < Width && "Illegal scale.");
    unsigned ExtendedWidth = Width * 2;
    APInt Product =
        (C0->sext(ExtendedWidth) * C1->sext(ExtendedWidth)).ashr(Scale);
    if (IntrinsicID == Intrinsic::smul_fix_sat) {
      APInt Max = APInt::getSignedMaxValue(Width).sext(ExtendedWidth);
      APInt Min = APInt::getSignedMinValue(Width).sext(ExtendedWidth);
      Product = APIntOps::smin(Product, Max);
      Product = APIntOps::smax(Product, Min);
    }
    return ConstantInt::get(Ty->getContext(), Product.sextOrTrunc(Width));
  }

  if (IntrinsicID == Intrinsic::fshl || IntrinsicID == Intrinsic::fshr) {
    const APInt *C0, *C1, *C2;
    if (!getConstIntOrUndef(Operands[0], C0) ||
        !getConstIntOrUndef(Operands[1], C1) ||
        !getConstIntOrUndef(Operands[2], C2))
      return nullptr;

    bool IsRight = IntrinsicID == Intrinsic::fshr;
    if (!C2)
      return Operands[IsRight ? 1 : 0];
    if (!C0 && !C1)
      return UndefValue::get(Ty);

    // The shift amount is interpreted as modulo the bitwidth. If the shift
    // amount is effectively 0, avoid UB due to oversized inverse shift below.
    unsigned BitWidth = C2->getBitWidth();
    unsigned ShAmt = C2->urem(BitWidth);
    if (!ShAmt)
      return Operands[IsRight ? 1 : 0];

    // (C0 << ShlAmt) | (C1 >> LshrAmt)
    unsigned LshrAmt = IsRight ? ShAmt : BitWidth - ShAmt;
    unsigned ShlAmt = !IsRight ? ShAmt : BitWidth - ShAmt;
    if (!C0)
      return ConstantInt::get(Ty, C1->lshr(LshrAmt));
    if (!C1)
      return ConstantInt::get(Ty, C0->shl(ShlAmt));
    return ConstantInt::get(Ty, C0->shl(ShlAmt) | C1->lshr(LshrAmt));
  }

  if (IntrinsicID == Intrinsic::amdgcn_perm)
    return ConstantFoldAMDGCNPermIntrinsic(Operands, Ty);

  return nullptr;
}

static Constant *ConstantFoldScalarCall(StringRef Name,
                                        Intrinsic::ID IntrinsicID,
                                        Type *Ty,
                                        ArrayRef<Constant *> Operands,
                                        const TargetLibraryInfo *TLI,
                                        const CallBase *Call) {
  if (Operands.size() == 1)
    return ConstantFoldScalarCall1(Name, IntrinsicID, Ty, Operands, TLI, Call);

  if (Operands.size() == 2) {
    if (Constant *FoldedLibCall =
            ConstantFoldLibCall2(Name, Ty, Operands, TLI)) {
      return FoldedLibCall;
    }
    return ConstantFoldIntrinsicCall2(IntrinsicID, Ty, Operands, Call);
  }

  if (Operands.size() == 3)
    return ConstantFoldScalarCall3(Name, IntrinsicID, Ty, Operands, TLI, Call);

  return nullptr;
}

static Constant *ConstantFoldFixedVectorCall(
    StringRef Name, Intrinsic::ID IntrinsicID, FixedVectorType *FVTy,
    ArrayRef<Constant *> Operands, const DataLayout &DL,
    const TargetLibraryInfo *TLI, const CallBase *Call) {
  SmallVector<Constant *, 4> Result(FVTy->getNumElements());
  SmallVector<Constant *, 4> Lane(Operands.size());
  Type *Ty = FVTy->getElementType();

  switch (IntrinsicID) {
  case Intrinsic::masked_load: {
    auto *SrcPtr = Operands[0];
    auto *Mask = Operands[2];
    auto *Passthru = Operands[3];

    Constant *VecData = ConstantFoldLoadFromConstPtr(SrcPtr, FVTy, DL);

    SmallVector<Constant *, 32> NewElements;
    for (unsigned I = 0, E = FVTy->getNumElements(); I != E; ++I) {
      auto *MaskElt = Mask->getAggregateElement(I);
      if (!MaskElt)
        break;
      auto *PassthruElt = Passthru->getAggregateElement(I);
      auto *VecElt = VecData ? VecData->getAggregateElement(I) : nullptr;
      if (isa<UndefValue>(MaskElt)) {
        if (PassthruElt)
          NewElements.push_back(PassthruElt);
        else if (VecElt)
          NewElements.push_back(VecElt);
        else
          return nullptr;
      }
      if (MaskElt->isNullValue()) {
        if (!PassthruElt)
          return nullptr;
        NewElements.push_back(PassthruElt);
      } else if (MaskElt->isOneValue()) {
        if (!VecElt)
          return nullptr;
        NewElements.push_back(VecElt);
      } else {
        return nullptr;
      }
    }
    if (NewElements.size() != FVTy->getNumElements())
      return nullptr;
    return ConstantVector::get(NewElements);
  }
  case Intrinsic::arm_mve_vctp8:
  case Intrinsic::arm_mve_vctp16:
  case Intrinsic::arm_mve_vctp32:
  case Intrinsic::arm_mve_vctp64: {
    if (auto *Op = dyn_cast<ConstantInt>(Operands[0])) {
      unsigned Lanes = FVTy->getNumElements();
      uint64_t Limit = Op->getZExtValue();

      SmallVector<Constant *, 16> NCs;
      for (unsigned i = 0; i < Lanes; i++) {
        if (i < Limit)
          NCs.push_back(ConstantInt::getTrue(Ty));
        else
          NCs.push_back(ConstantInt::getFalse(Ty));
      }
      return ConstantVector::get(NCs);
    }
    return nullptr;
  }
  case Intrinsic::get_active_lane_mask: {
    auto *Op0 = dyn_cast<ConstantInt>(Operands[0]);
    auto *Op1 = dyn_cast<ConstantInt>(Operands[1]);
    if (Op0 && Op1) {
      unsigned Lanes = FVTy->getNumElements();
      uint64_t Base = Op0->getZExtValue();
      uint64_t Limit = Op1->getZExtValue();

      SmallVector<Constant *, 16> NCs;
      for (unsigned i = 0; i < Lanes; i++) {
        if (Base + i < Limit)
          NCs.push_back(ConstantInt::getTrue(Ty));
        else
          NCs.push_back(ConstantInt::getFalse(Ty));
      }
      return ConstantVector::get(NCs);
    }
    return nullptr;
  }
  default:
    break;
  }

  for (unsigned I = 0, E = FVTy->getNumElements(); I != E; ++I) {
    // Gather a column of constants.
    for (unsigned J = 0, JE = Operands.size(); J != JE; ++J) {
      // Some intrinsics use a scalar type for certain arguments.
      if (isVectorIntrinsicWithScalarOpAtArg(IntrinsicID, J)) {
        Lane[J] = Operands[J];
        continue;
      }

      Constant *Agg = Operands[J]->getAggregateElement(I);
      if (!Agg)
        return nullptr;

      Lane[J] = Agg;
    }

    // Use the regular scalar folding to simplify this column.
    Constant *Folded =
        ConstantFoldScalarCall(Name, IntrinsicID, Ty, Lane, TLI, Call);
    if (!Folded)
      return nullptr;
    Result[I] = Folded;
  }

  return ConstantVector::get(Result);
}

static Constant *ConstantFoldScalableVectorCall(
    StringRef Name, Intrinsic::ID IntrinsicID, ScalableVectorType *SVTy,
    ArrayRef<Constant *> Operands, const DataLayout &DL,
    const TargetLibraryInfo *TLI, const CallBase *Call) {
  switch (IntrinsicID) {
  case Intrinsic::aarch64_sve_convert_from_svbool: {
    auto *Src = dyn_cast<Constant>(Operands[0]);
    if (!Src || !Src->isNullValue())
      break;

    return ConstantInt::getFalse(SVTy);
  }
  default:
    break;
  }
  return nullptr;
}

static std::pair<Constant *, Constant *>
ConstantFoldScalarFrexpCall(Constant *Op, Type *IntTy) {
  if (isa<PoisonValue>(Op))
    return {Op, PoisonValue::get(IntTy)};

  auto *ConstFP = dyn_cast<ConstantFP>(Op);
  if (!ConstFP)
    return {};

  const APFloat &U = ConstFP->getValueAPF();
  int FrexpExp;
  APFloat FrexpMant = frexp(U, FrexpExp, APFloat::rmNearestTiesToEven);
  Constant *Result0 = ConstantFP::get(ConstFP->getType(), FrexpMant);

  // The exponent is an "unspecified value" for inf/nan. We use zero to avoid
  // using undef.
  Constant *Result1 = FrexpMant.isFinite() ? ConstantInt::get(IntTy, FrexpExp)
                                           : ConstantInt::getNullValue(IntTy);
  return {Result0, Result1};
}

/// Handle intrinsics that return tuples, which may be tuples of vectors.
static Constant *
ConstantFoldStructCall(StringRef Name, Intrinsic::ID IntrinsicID,
                       StructType *StTy, ArrayRef<Constant *> Operands,
                       const DataLayout &DL, const TargetLibraryInfo *TLI,
                       const CallBase *Call) {

  switch (IntrinsicID) {
  case Intrinsic::frexp: {
    Type *Ty0 = StTy->getContainedType(0);
    Type *Ty1 = StTy->getContainedType(1)->getScalarType();

    if (auto *FVTy0 = dyn_cast<FixedVectorType>(Ty0)) {
      SmallVector<Constant *, 4> Results0(FVTy0->getNumElements());
      SmallVector<Constant *, 4> Results1(FVTy0->getNumElements());

      for (unsigned I = 0, E = FVTy0->getNumElements(); I != E; ++I) {
        Constant *Lane = Operands[0]->getAggregateElement(I);
        std::tie(Results0[I], Results1[I]) =
            ConstantFoldScalarFrexpCall(Lane, Ty1);
        if (!Results0[I])
          return nullptr;
      }

      return ConstantStruct::get(StTy, ConstantVector::get(Results0),
                                 ConstantVector::get(Results1));
    }

    auto [Result0, Result1] = ConstantFoldScalarFrexpCall(Operands[0], Ty1);
    if (!Result0)
      return nullptr;
    return ConstantStruct::get(StTy, Result0, Result1);
  }
  default:
    // TODO: Constant folding of vector intrinsics that fall through here does
    // not work (e.g. overflow intrinsics)
    return ConstantFoldScalarCall(Name, IntrinsicID, StTy, Operands, TLI, Call);
  }

  return nullptr;
}

} // end anonymous namespace

Constant *llvm::ConstantFoldBinaryIntrinsic(Intrinsic::ID ID, Constant *LHS,
                                            Constant *RHS, Type *Ty,
                                            Instruction *FMFSource) {
  return ConstantFoldIntrinsicCall2(ID, Ty, {LHS, RHS},
                                    dyn_cast_if_present<CallBase>(FMFSource));
}

Constant *llvm::ConstantFoldCall(const CallBase *Call, Function *F,
                                 ArrayRef<Constant *> Operands,
                                 const TargetLibraryInfo *TLI,
                                 bool AllowNonDeterministic) {
  if (Call->isNoBuiltin())
    return nullptr;
  if (!F->hasName())
    return nullptr;

  // If this is not an intrinsic and not recognized as a library call, bail out.
  Intrinsic::ID IID = F->getIntrinsicID();
  if (IID == Intrinsic::not_intrinsic) {
    if (!TLI)
      return nullptr;
    LibFunc LibF;
    if (!TLI->getLibFunc(*F, LibF))
      return nullptr;
  }

  // Conservatively assume that floating-point libcalls may be
  // non-deterministic.
  Type *Ty = F->getReturnType();
  if (!AllowNonDeterministic && Ty->isFPOrFPVectorTy())
    return nullptr;

  StringRef Name = F->getName();
  if (auto *FVTy = dyn_cast<FixedVectorType>(Ty))
    return ConstantFoldFixedVectorCall(
        Name, IID, FVTy, Operands, F->getDataLayout(), TLI, Call);

  if (auto *SVTy = dyn_cast<ScalableVectorType>(Ty))
    return ConstantFoldScalableVectorCall(
        Name, IID, SVTy, Operands, F->getDataLayout(), TLI, Call);

  if (auto *StTy = dyn_cast<StructType>(Ty))
    return ConstantFoldStructCall(Name, IID, StTy, Operands,
                                  F->getDataLayout(), TLI, Call);

  // TODO: If this is a library function, we already discovered that above,
  //       so we should pass the LibFunc, not the name (and it might be better
  //       still to separate intrinsic handling from libcalls).
  return ConstantFoldScalarCall(Name, IID, Ty, Operands, TLI, Call);
}

bool llvm::isMathLibCallNoop(const CallBase *Call,
                             const TargetLibraryInfo *TLI) {
  // FIXME: Refactor this code; this duplicates logic in LibCallsShrinkWrap
  // (and to some extent ConstantFoldScalarCall).
  if (Call->isNoBuiltin() || Call->isStrictFP())
    return false;
  Function *F = Call->getCalledFunction();
  if (!F)
    return false;

  LibFunc Func;
  if (!TLI || !TLI->getLibFunc(*F, Func))
    return false;

  if (Call->arg_size() == 1) {
    if (ConstantFP *OpC = dyn_cast<ConstantFP>(Call->getArgOperand(0))) {
      const APFloat &Op = OpC->getValueAPF();
      switch (Func) {
      case LibFunc_logl:
      case LibFunc_log:
      case LibFunc_logf:
      case LibFunc_log2l:
      case LibFunc_log2:
      case LibFunc_log2f:
      case LibFunc_log10l:
      case LibFunc_log10:
      case LibFunc_log10f:
        return Op.isNaN() || (!Op.isZero() && !Op.isNegative());

      case LibFunc_expl:
      case LibFunc_exp:
      case LibFunc_expf:
        // FIXME: These boundaries are slightly conservative.
        if (OpC->getType()->isDoubleTy())
          return !(Op < APFloat(-745.0) || Op > APFloat(709.0));
        if (OpC->getType()->isFloatTy())
          return !(Op < APFloat(-103.0f) || Op > APFloat(88.0f));
        break;

      case LibFunc_exp2l:
      case LibFunc_exp2:
      case LibFunc_exp2f:
        // FIXME: These boundaries are slightly conservative.
        if (OpC->getType()->isDoubleTy())
          return !(Op < APFloat(-1074.0) || Op > APFloat(1023.0));
        if (OpC->getType()->isFloatTy())
          return !(Op < APFloat(-149.0f) || Op > APFloat(127.0f));
        break;

      case LibFunc_sinl:
      case LibFunc_sin:
      case LibFunc_sinf:
      case LibFunc_cosl:
      case LibFunc_cos:
      case LibFunc_cosf:
        return !Op.isInfinity();

      case LibFunc_tanl:
      case LibFunc_tan:
      case LibFunc_tanf: {
        // FIXME: Stop using the host math library.
        // FIXME: The computation isn't done in the right precision.
        Type *Ty = OpC->getType();
        if (Ty->isDoubleTy() || Ty->isFloatTy() || Ty->isHalfTy())
          return ConstantFoldFP(tan, OpC->getValueAPF(), Ty) != nullptr;
        break;
      }

      case LibFunc_atan:
      case LibFunc_atanf:
      case LibFunc_atanl:
        // Per POSIX, this MAY fail if Op is denormal. We choose not failing.
        return true;


      case LibFunc_asinl:
      case LibFunc_asin:
      case LibFunc_asinf:
      case LibFunc_acosl:
      case LibFunc_acos:
      case LibFunc_acosf:
        return !(Op < APFloat(Op.getSemantics(), "-1") ||
                 Op > APFloat(Op.getSemantics(), "1"));

      case LibFunc_sinh:
      case LibFunc_cosh:
      case LibFunc_sinhf:
      case LibFunc_coshf:
      case LibFunc_sinhl:
      case LibFunc_coshl:
        // FIXME: These boundaries are slightly conservative.
        if (OpC->getType()->isDoubleTy())
          return !(Op < APFloat(-710.0) || Op > APFloat(710.0));
        if (OpC->getType()->isFloatTy())
          return !(Op < APFloat(-89.0f) || Op > APFloat(89.0f));
        break;

      case LibFunc_sqrtl:
      case LibFunc_sqrt:
      case LibFunc_sqrtf:
        return Op.isNaN() || Op.isZero() || !Op.isNegative();

      // FIXME: Add more functions: sqrt_finite, atanh, expm1, log1p,
      // maybe others?
      default:
        break;
      }
    }
  }

  if (Call->arg_size() == 2) {
    ConstantFP *Op0C = dyn_cast<ConstantFP>(Call->getArgOperand(0));
    ConstantFP *Op1C = dyn_cast<ConstantFP>(Call->getArgOperand(1));
    if (Op0C && Op1C) {
      const APFloat &Op0 = Op0C->getValueAPF();
      const APFloat &Op1 = Op1C->getValueAPF();

      switch (Func) {
      case LibFunc_powl:
      case LibFunc_pow:
      case LibFunc_powf: {
        // FIXME: Stop using the host math library.
        // FIXME: The computation isn't done in the right precision.
        Type *Ty = Op0C->getType();
        if (Ty->isDoubleTy() || Ty->isFloatTy() || Ty->isHalfTy()) {
          if (Ty == Op1C->getType())
            return ConstantFoldBinaryFP(pow, Op0, Op1, Ty) != nullptr;
        }
        break;
      }

      case LibFunc_fmodl:
      case LibFunc_fmod:
      case LibFunc_fmodf:
      case LibFunc_remainderl:
      case LibFunc_remainder:
      case LibFunc_remainderf:
        return Op0.isNaN() || Op1.isNaN() ||
               (!Op0.isInfinity() && !Op1.isZero());

      case LibFunc_atan2:
      case LibFunc_atan2f:
      case LibFunc_atan2l:
        // Although IEEE-754 says atan2(+/-0.0, +/-0.0) are well-defined, and
        // GLIBC and MSVC do not appear to raise an error on those, we
        // cannot rely on that behavior. POSIX and C11 say that a domain error
        // may occur, so allow for that possibility.
        return !Op0.isZero() || !Op1.isZero();

      default:
        break;
      }
    }
  }

  return false;
}

void TargetFolder::anchor() {}
