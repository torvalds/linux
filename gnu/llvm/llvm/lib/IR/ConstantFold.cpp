//===- ConstantFold.cpp - LLVM constant folder ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements folding of constants for LLVM.  This implements the
// (internal) ConstantFold.h interface, which is used by the
// ConstantExpr::get* methods to automatically fold constants when possible.
//
// The current constant folding implementation is implemented in two pieces: the
// pieces that don't need DataLayout, and the pieces that do. This is to avoid
// a dependence in IR on Target.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/ConstantFold.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;
using namespace llvm::PatternMatch;

//===----------------------------------------------------------------------===//
//                ConstantFold*Instruction Implementations
//===----------------------------------------------------------------------===//

/// This function determines which opcode to use to fold two constant cast
/// expressions together. It uses CastInst::isEliminableCastPair to determine
/// the opcode. Consequently its just a wrapper around that function.
/// Determine if it is valid to fold a cast of a cast
static unsigned
foldConstantCastPair(
  unsigned opc,          ///< opcode of the second cast constant expression
  ConstantExpr *Op,      ///< the first cast constant expression
  Type *DstTy            ///< destination type of the first cast
) {
  assert(Op && Op->isCast() && "Can't fold cast of cast without a cast!");
  assert(DstTy && DstTy->isFirstClassType() && "Invalid cast destination type");
  assert(CastInst::isCast(opc) && "Invalid cast opcode");

  // The types and opcodes for the two Cast constant expressions
  Type *SrcTy = Op->getOperand(0)->getType();
  Type *MidTy = Op->getType();
  Instruction::CastOps firstOp = Instruction::CastOps(Op->getOpcode());
  Instruction::CastOps secondOp = Instruction::CastOps(opc);

  // Assume that pointers are never more than 64 bits wide, and only use this
  // for the middle type. Otherwise we could end up folding away illegal
  // bitcasts between address spaces with different sizes.
  IntegerType *FakeIntPtrTy = Type::getInt64Ty(DstTy->getContext());

  // Let CastInst::isEliminableCastPair do the heavy lifting.
  return CastInst::isEliminableCastPair(firstOp, secondOp, SrcTy, MidTy, DstTy,
                                        nullptr, FakeIntPtrTy, nullptr);
}

static Constant *FoldBitCast(Constant *V, Type *DestTy) {
  Type *SrcTy = V->getType();
  if (SrcTy == DestTy)
    return V; // no-op cast

  // Handle casts from one vector constant to another.  We know that the src
  // and dest type have the same size (otherwise its an illegal cast).
  if (VectorType *DestPTy = dyn_cast<VectorType>(DestTy)) {
    if (V->isAllOnesValue())
      return Constant::getAllOnesValue(DestTy);

    // Canonicalize scalar-to-vector bitcasts into vector-to-vector bitcasts
    // This allows for other simplifications (although some of them
    // can only be handled by Analysis/ConstantFolding.cpp).
    if (isa<ConstantInt>(V) || isa<ConstantFP>(V))
      return ConstantExpr::getBitCast(ConstantVector::get(V), DestPTy);
    return nullptr;
  }

  // Handle integral constant input.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    // See note below regarding the PPC_FP128 restriction.
    if (DestTy->isFloatingPointTy() && !DestTy->isPPC_FP128Ty())
      return ConstantFP::get(DestTy->getContext(),
                             APFloat(DestTy->getFltSemantics(),
                                     CI->getValue()));

    // Otherwise, can't fold this (vector?)
    return nullptr;
  }

  // Handle ConstantFP input: FP -> Integral.
  if (ConstantFP *FP = dyn_cast<ConstantFP>(V)) {
    // PPC_FP128 is really the sum of two consecutive doubles, where the first
    // double is always stored first in memory, regardless of the target
    // endianness. The memory layout of i128, however, depends on the target
    // endianness, and so we can't fold this without target endianness
    // information. This should instead be handled by
    // Analysis/ConstantFolding.cpp
    if (FP->getType()->isPPC_FP128Ty())
      return nullptr;

    // Make sure dest type is compatible with the folded integer constant.
    if (!DestTy->isIntegerTy())
      return nullptr;

    return ConstantInt::get(FP->getContext(),
                            FP->getValueAPF().bitcastToAPInt());
  }

  return nullptr;
}

static Constant *foldMaybeUndesirableCast(unsigned opc, Constant *V,
                                          Type *DestTy) {
  return ConstantExpr::isDesirableCastOp(opc)
             ? ConstantExpr::getCast(opc, V, DestTy)
             : ConstantFoldCastInstruction(opc, V, DestTy);
}

Constant *llvm::ConstantFoldCastInstruction(unsigned opc, Constant *V,
                                            Type *DestTy) {
  if (isa<PoisonValue>(V))
    return PoisonValue::get(DestTy);

  if (isa<UndefValue>(V)) {
    // zext(undef) = 0, because the top bits will be zero.
    // sext(undef) = 0, because the top bits will all be the same.
    // [us]itofp(undef) = 0, because the result value is bounded.
    if (opc == Instruction::ZExt || opc == Instruction::SExt ||
        opc == Instruction::UIToFP || opc == Instruction::SIToFP)
      return Constant::getNullValue(DestTy);
    return UndefValue::get(DestTy);
  }

  if (V->isNullValue() && !DestTy->isX86_MMXTy() && !DestTy->isX86_AMXTy() &&
      opc != Instruction::AddrSpaceCast)
    return Constant::getNullValue(DestTy);

  // If the cast operand is a constant expression, there's a few things we can
  // do to try to simplify it.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
    if (CE->isCast()) {
      // Try hard to fold cast of cast because they are often eliminable.
      if (unsigned newOpc = foldConstantCastPair(opc, CE, DestTy))
        return foldMaybeUndesirableCast(newOpc, CE->getOperand(0), DestTy);
    }
  }

  // If the cast operand is a constant vector, perform the cast by
  // operating on each element. In the cast of bitcasts, the element
  // count may be mismatched; don't attempt to handle that here.
  if ((isa<ConstantVector>(V) || isa<ConstantDataVector>(V)) &&
      DestTy->isVectorTy() &&
      cast<FixedVectorType>(DestTy)->getNumElements() ==
          cast<FixedVectorType>(V->getType())->getNumElements()) {
    VectorType *DestVecTy = cast<VectorType>(DestTy);
    Type *DstEltTy = DestVecTy->getElementType();
    // Fast path for splatted constants.
    if (Constant *Splat = V->getSplatValue()) {
      Constant *Res = foldMaybeUndesirableCast(opc, Splat, DstEltTy);
      if (!Res)
        return nullptr;
      return ConstantVector::getSplat(
          cast<VectorType>(DestTy)->getElementCount(), Res);
    }
    SmallVector<Constant *, 16> res;
    Type *Ty = IntegerType::get(V->getContext(), 32);
    for (unsigned i = 0,
                  e = cast<FixedVectorType>(V->getType())->getNumElements();
         i != e; ++i) {
      Constant *C = ConstantExpr::getExtractElement(V, ConstantInt::get(Ty, i));
      Constant *Casted = foldMaybeUndesirableCast(opc, C, DstEltTy);
      if (!Casted)
        return nullptr;
      res.push_back(Casted);
    }
    return ConstantVector::get(res);
  }

  // We actually have to do a cast now. Perform the cast according to the
  // opcode specified.
  switch (opc) {
  default:
    llvm_unreachable("Failed to cast constant expression");
  case Instruction::FPTrunc:
  case Instruction::FPExt:
    if (ConstantFP *FPC = dyn_cast<ConstantFP>(V)) {
      bool ignored;
      APFloat Val = FPC->getValueAPF();
      Val.convert(DestTy->getFltSemantics(), APFloat::rmNearestTiesToEven,
                  &ignored);
      return ConstantFP::get(V->getContext(), Val);
    }
    return nullptr; // Can't fold.
  case Instruction::FPToUI:
  case Instruction::FPToSI:
    if (ConstantFP *FPC = dyn_cast<ConstantFP>(V)) {
      const APFloat &V = FPC->getValueAPF();
      bool ignored;
      uint32_t DestBitWidth = cast<IntegerType>(DestTy)->getBitWidth();
      APSInt IntVal(DestBitWidth, opc == Instruction::FPToUI);
      if (APFloat::opInvalidOp ==
          V.convertToInteger(IntVal, APFloat::rmTowardZero, &ignored)) {
        // Undefined behavior invoked - the destination type can't represent
        // the input constant.
        return PoisonValue::get(DestTy);
      }
      return ConstantInt::get(FPC->getContext(), IntVal);
    }
    return nullptr; // Can't fold.
  case Instruction::UIToFP:
  case Instruction::SIToFP:
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      const APInt &api = CI->getValue();
      APFloat apf(DestTy->getFltSemantics(),
                  APInt::getZero(DestTy->getPrimitiveSizeInBits()));
      apf.convertFromAPInt(api, opc==Instruction::SIToFP,
                           APFloat::rmNearestTiesToEven);
      return ConstantFP::get(V->getContext(), apf);
    }
    return nullptr;
  case Instruction::ZExt:
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      uint32_t BitWidth = cast<IntegerType>(DestTy)->getBitWidth();
      return ConstantInt::get(V->getContext(),
                              CI->getValue().zext(BitWidth));
    }
    return nullptr;
  case Instruction::SExt:
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      uint32_t BitWidth = cast<IntegerType>(DestTy)->getBitWidth();
      return ConstantInt::get(V->getContext(),
                              CI->getValue().sext(BitWidth));
    }
    return nullptr;
  case Instruction::Trunc: {
    if (V->getType()->isVectorTy())
      return nullptr;

    uint32_t DestBitWidth = cast<IntegerType>(DestTy)->getBitWidth();
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      return ConstantInt::get(V->getContext(),
                              CI->getValue().trunc(DestBitWidth));
    }

    return nullptr;
  }
  case Instruction::BitCast:
    return FoldBitCast(V, DestTy);
  case Instruction::AddrSpaceCast:
  case Instruction::IntToPtr:
  case Instruction::PtrToInt:
    return nullptr;
  }
}

Constant *llvm::ConstantFoldSelectInstruction(Constant *Cond,
                                              Constant *V1, Constant *V2) {
  // Check for i1 and vector true/false conditions.
  if (Cond->isNullValue()) return V2;
  if (Cond->isAllOnesValue()) return V1;

  // If the condition is a vector constant, fold the result elementwise.
  if (ConstantVector *CondV = dyn_cast<ConstantVector>(Cond)) {
    auto *V1VTy = CondV->getType();
    SmallVector<Constant*, 16> Result;
    Type *Ty = IntegerType::get(CondV->getContext(), 32);
    for (unsigned i = 0, e = V1VTy->getNumElements(); i != e; ++i) {
      Constant *V;
      Constant *V1Element = ConstantExpr::getExtractElement(V1,
                                                    ConstantInt::get(Ty, i));
      Constant *V2Element = ConstantExpr::getExtractElement(V2,
                                                    ConstantInt::get(Ty, i));
      auto *Cond = cast<Constant>(CondV->getOperand(i));
      if (isa<PoisonValue>(Cond)) {
        V = PoisonValue::get(V1Element->getType());
      } else if (V1Element == V2Element) {
        V = V1Element;
      } else if (isa<UndefValue>(Cond)) {
        V = isa<UndefValue>(V1Element) ? V1Element : V2Element;
      } else {
        if (!isa<ConstantInt>(Cond)) break;
        V = Cond->isNullValue() ? V2Element : V1Element;
      }
      Result.push_back(V);
    }

    // If we were able to build the vector, return it.
    if (Result.size() == V1VTy->getNumElements())
      return ConstantVector::get(Result);
  }

  if (isa<PoisonValue>(Cond))
    return PoisonValue::get(V1->getType());

  if (isa<UndefValue>(Cond)) {
    if (isa<UndefValue>(V1)) return V1;
    return V2;
  }

  if (V1 == V2) return V1;

  if (isa<PoisonValue>(V1))
    return V2;
  if (isa<PoisonValue>(V2))
    return V1;

  // If the true or false value is undef, we can fold to the other value as
  // long as the other value isn't poison.
  auto NotPoison = [](Constant *C) {
    if (isa<PoisonValue>(C))
      return false;

    // TODO: We can analyze ConstExpr by opcode to determine if there is any
    //       possibility of poison.
    if (isa<ConstantExpr>(C))
      return false;

    if (isa<ConstantInt>(C) || isa<GlobalVariable>(C) || isa<ConstantFP>(C) ||
        isa<ConstantPointerNull>(C) || isa<Function>(C))
      return true;

    if (C->getType()->isVectorTy())
      return !C->containsPoisonElement() && !C->containsConstantExpression();

    // TODO: Recursively analyze aggregates or other constants.
    return false;
  };
  if (isa<UndefValue>(V1) && NotPoison(V2)) return V2;
  if (isa<UndefValue>(V2) && NotPoison(V1)) return V1;

  return nullptr;
}

Constant *llvm::ConstantFoldExtractElementInstruction(Constant *Val,
                                                      Constant *Idx) {
  auto *ValVTy = cast<VectorType>(Val->getType());

  // extractelt poison, C -> poison
  // extractelt C, undef -> poison
  if (isa<PoisonValue>(Val) || isa<UndefValue>(Idx))
    return PoisonValue::get(ValVTy->getElementType());

  // extractelt undef, C -> undef
  if (isa<UndefValue>(Val))
    return UndefValue::get(ValVTy->getElementType());

  auto *CIdx = dyn_cast<ConstantInt>(Idx);
  if (!CIdx)
    return nullptr;

  if (auto *ValFVTy = dyn_cast<FixedVectorType>(Val->getType())) {
    // ee({w,x,y,z}, wrong_value) -> poison
    if (CIdx->uge(ValFVTy->getNumElements()))
      return PoisonValue::get(ValFVTy->getElementType());
  }

  // ee (gep (ptr, idx0, ...), idx) -> gep (ee (ptr, idx), ee (idx0, idx), ...)
  if (auto *CE = dyn_cast<ConstantExpr>(Val)) {
    if (auto *GEP = dyn_cast<GEPOperator>(CE)) {
      SmallVector<Constant *, 8> Ops;
      Ops.reserve(CE->getNumOperands());
      for (unsigned i = 0, e = CE->getNumOperands(); i != e; ++i) {
        Constant *Op = CE->getOperand(i);
        if (Op->getType()->isVectorTy()) {
          Constant *ScalarOp = ConstantExpr::getExtractElement(Op, Idx);
          if (!ScalarOp)
            return nullptr;
          Ops.push_back(ScalarOp);
        } else
          Ops.push_back(Op);
      }
      return CE->getWithOperands(Ops, ValVTy->getElementType(), false,
                                 GEP->getSourceElementType());
    } else if (CE->getOpcode() == Instruction::InsertElement) {
      if (const auto *IEIdx = dyn_cast<ConstantInt>(CE->getOperand(2))) {
        if (APSInt::isSameValue(APSInt(IEIdx->getValue()),
                                APSInt(CIdx->getValue()))) {
          return CE->getOperand(1);
        } else {
          return ConstantExpr::getExtractElement(CE->getOperand(0), CIdx);
        }
      }
    }
  }

  if (Constant *C = Val->getAggregateElement(CIdx))
    return C;

  // Lane < Splat minimum vector width => extractelt Splat(x), Lane -> x
  if (CIdx->getValue().ult(ValVTy->getElementCount().getKnownMinValue())) {
    if (Constant *SplatVal = Val->getSplatValue())
      return SplatVal;
  }

  return nullptr;
}

Constant *llvm::ConstantFoldInsertElementInstruction(Constant *Val,
                                                     Constant *Elt,
                                                     Constant *Idx) {
  if (isa<UndefValue>(Idx))
    return PoisonValue::get(Val->getType());

  // Inserting null into all zeros is still all zeros.
  // TODO: This is true for undef and poison splats too.
  if (isa<ConstantAggregateZero>(Val) && Elt->isNullValue())
    return Val;

  ConstantInt *CIdx = dyn_cast<ConstantInt>(Idx);
  if (!CIdx) return nullptr;

  // Do not iterate on scalable vector. The num of elements is unknown at
  // compile-time.
  if (isa<ScalableVectorType>(Val->getType()))
    return nullptr;

  auto *ValTy = cast<FixedVectorType>(Val->getType());

  unsigned NumElts = ValTy->getNumElements();
  if (CIdx->uge(NumElts))
    return PoisonValue::get(Val->getType());

  SmallVector<Constant*, 16> Result;
  Result.reserve(NumElts);
  auto *Ty = Type::getInt32Ty(Val->getContext());
  uint64_t IdxVal = CIdx->getZExtValue();
  for (unsigned i = 0; i != NumElts; ++i) {
    if (i == IdxVal) {
      Result.push_back(Elt);
      continue;
    }

    Constant *C = ConstantExpr::getExtractElement(Val, ConstantInt::get(Ty, i));
    Result.push_back(C);
  }

  return ConstantVector::get(Result);
}

Constant *llvm::ConstantFoldShuffleVectorInstruction(Constant *V1, Constant *V2,
                                                     ArrayRef<int> Mask) {
  auto *V1VTy = cast<VectorType>(V1->getType());
  unsigned MaskNumElts = Mask.size();
  auto MaskEltCount =
      ElementCount::get(MaskNumElts, isa<ScalableVectorType>(V1VTy));
  Type *EltTy = V1VTy->getElementType();

  // Poison shuffle mask -> poison value.
  if (all_of(Mask, [](int Elt) { return Elt == PoisonMaskElem; })) {
    return PoisonValue::get(VectorType::get(EltTy, MaskEltCount));
  }

  // If the mask is all zeros this is a splat, no need to go through all
  // elements.
  if (all_of(Mask, [](int Elt) { return Elt == 0; })) {
    Type *Ty = IntegerType::get(V1->getContext(), 32);
    Constant *Elt =
        ConstantExpr::getExtractElement(V1, ConstantInt::get(Ty, 0));

    if (Elt->isNullValue()) {
      auto *VTy = VectorType::get(EltTy, MaskEltCount);
      return ConstantAggregateZero::get(VTy);
    } else if (!MaskEltCount.isScalable())
      return ConstantVector::getSplat(MaskEltCount, Elt);
  }

  // Do not iterate on scalable vector. The num of elements is unknown at
  // compile-time.
  if (isa<ScalableVectorType>(V1VTy))
    return nullptr;

  unsigned SrcNumElts = V1VTy->getElementCount().getKnownMinValue();

  // Loop over the shuffle mask, evaluating each element.
  SmallVector<Constant*, 32> Result;
  for (unsigned i = 0; i != MaskNumElts; ++i) {
    int Elt = Mask[i];
    if (Elt == -1) {
      Result.push_back(UndefValue::get(EltTy));
      continue;
    }
    Constant *InElt;
    if (unsigned(Elt) >= SrcNumElts*2)
      InElt = UndefValue::get(EltTy);
    else if (unsigned(Elt) >= SrcNumElts) {
      Type *Ty = IntegerType::get(V2->getContext(), 32);
      InElt =
        ConstantExpr::getExtractElement(V2,
                                        ConstantInt::get(Ty, Elt - SrcNumElts));
    } else {
      Type *Ty = IntegerType::get(V1->getContext(), 32);
      InElt = ConstantExpr::getExtractElement(V1, ConstantInt::get(Ty, Elt));
    }
    Result.push_back(InElt);
  }

  return ConstantVector::get(Result);
}

Constant *llvm::ConstantFoldExtractValueInstruction(Constant *Agg,
                                                    ArrayRef<unsigned> Idxs) {
  // Base case: no indices, so return the entire value.
  if (Idxs.empty())
    return Agg;

  if (Constant *C = Agg->getAggregateElement(Idxs[0]))
    return ConstantFoldExtractValueInstruction(C, Idxs.slice(1));

  return nullptr;
}

Constant *llvm::ConstantFoldInsertValueInstruction(Constant *Agg,
                                                   Constant *Val,
                                                   ArrayRef<unsigned> Idxs) {
  // Base case: no indices, so replace the entire value.
  if (Idxs.empty())
    return Val;

  unsigned NumElts;
  if (StructType *ST = dyn_cast<StructType>(Agg->getType()))
    NumElts = ST->getNumElements();
  else
    NumElts = cast<ArrayType>(Agg->getType())->getNumElements();

  SmallVector<Constant*, 32> Result;
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *C = Agg->getAggregateElement(i);
    if (!C) return nullptr;

    if (Idxs[0] == i)
      C = ConstantFoldInsertValueInstruction(C, Val, Idxs.slice(1));

    Result.push_back(C);
  }

  if (StructType *ST = dyn_cast<StructType>(Agg->getType()))
    return ConstantStruct::get(ST, Result);
  return ConstantArray::get(cast<ArrayType>(Agg->getType()), Result);
}

Constant *llvm::ConstantFoldUnaryInstruction(unsigned Opcode, Constant *C) {
  assert(Instruction::isUnaryOp(Opcode) && "Non-unary instruction detected");

  // Handle scalar UndefValue and scalable vector UndefValue. Fixed-length
  // vectors are always evaluated per element.
  bool IsScalableVector = isa<ScalableVectorType>(C->getType());
  bool HasScalarUndefOrScalableVectorUndef =
      (!C->getType()->isVectorTy() || IsScalableVector) && isa<UndefValue>(C);

  if (HasScalarUndefOrScalableVectorUndef) {
    switch (static_cast<Instruction::UnaryOps>(Opcode)) {
    case Instruction::FNeg:
      return C; // -undef -> undef
    case Instruction::UnaryOpsEnd:
      llvm_unreachable("Invalid UnaryOp");
    }
  }

  // Constant should not be UndefValue, unless these are vector constants.
  assert(!HasScalarUndefOrScalableVectorUndef && "Unexpected UndefValue");
  // We only have FP UnaryOps right now.
  assert(!isa<ConstantInt>(C) && "Unexpected Integer UnaryOp");

  if (ConstantFP *CFP = dyn_cast<ConstantFP>(C)) {
    const APFloat &CV = CFP->getValueAPF();
    switch (Opcode) {
    default:
      break;
    case Instruction::FNeg:
      return ConstantFP::get(C->getContext(), neg(CV));
    }
  } else if (auto *VTy = dyn_cast<FixedVectorType>(C->getType())) {

    Type *Ty = IntegerType::get(VTy->getContext(), 32);
    // Fast path for splatted constants.
    if (Constant *Splat = C->getSplatValue())
      if (Constant *Elt = ConstantFoldUnaryInstruction(Opcode, Splat))
        return ConstantVector::getSplat(VTy->getElementCount(), Elt);

    // Fold each element and create a vector constant from those constants.
    SmallVector<Constant *, 16> Result;
    for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i) {
      Constant *ExtractIdx = ConstantInt::get(Ty, i);
      Constant *Elt = ConstantExpr::getExtractElement(C, ExtractIdx);
      Constant *Res = ConstantFoldUnaryInstruction(Opcode, Elt);
      if (!Res)
        return nullptr;
      Result.push_back(Res);
    }

    return ConstantVector::get(Result);
  }

  // We don't know how to fold this.
  return nullptr;
}

Constant *llvm::ConstantFoldBinaryInstruction(unsigned Opcode, Constant *C1,
                                              Constant *C2) {
  assert(Instruction::isBinaryOp(Opcode) && "Non-binary instruction detected");

  // Simplify BinOps with their identity values first. They are no-ops and we
  // can always return the other value, including undef or poison values.
  if (Constant *Identity = ConstantExpr::getBinOpIdentity(
          Opcode, C1->getType(), /*AllowRHSIdentity*/ false)) {
    if (C1 == Identity)
      return C2;
    if (C2 == Identity)
      return C1;
  } else if (Constant *Identity = ConstantExpr::getBinOpIdentity(
                 Opcode, C1->getType(), /*AllowRHSIdentity*/ true)) {
    if (C2 == Identity)
      return C1;
  }

  // Binary operations propagate poison.
  if (isa<PoisonValue>(C1) || isa<PoisonValue>(C2))
    return PoisonValue::get(C1->getType());

  // Handle scalar UndefValue and scalable vector UndefValue. Fixed-length
  // vectors are always evaluated per element.
  bool IsScalableVector = isa<ScalableVectorType>(C1->getType());
  bool HasScalarUndefOrScalableVectorUndef =
      (!C1->getType()->isVectorTy() || IsScalableVector) &&
      (isa<UndefValue>(C1) || isa<UndefValue>(C2));
  if (HasScalarUndefOrScalableVectorUndef) {
    switch (static_cast<Instruction::BinaryOps>(Opcode)) {
    case Instruction::Xor:
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2))
        // Handle undef ^ undef -> 0 special case. This is a common
        // idiom (misuse).
        return Constant::getNullValue(C1->getType());
      [[fallthrough]];
    case Instruction::Add:
    case Instruction::Sub:
      return UndefValue::get(C1->getType());
    case Instruction::And:
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2)) // undef & undef -> undef
        return C1;
      return Constant::getNullValue(C1->getType());   // undef & X -> 0
    case Instruction::Mul: {
      // undef * undef -> undef
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2))
        return C1;
      const APInt *CV;
      // X * undef -> undef   if X is odd
      if (match(C1, m_APInt(CV)) || match(C2, m_APInt(CV)))
        if ((*CV)[0])
          return UndefValue::get(C1->getType());

      // X * undef -> 0       otherwise
      return Constant::getNullValue(C1->getType());
    }
    case Instruction::SDiv:
    case Instruction::UDiv:
      // X / undef -> poison
      // X / 0 -> poison
      if (match(C2, m_CombineOr(m_Undef(), m_Zero())))
        return PoisonValue::get(C2->getType());
      // undef / X -> 0       otherwise
      return Constant::getNullValue(C1->getType());
    case Instruction::URem:
    case Instruction::SRem:
      // X % undef -> poison
      // X % 0 -> poison
      if (match(C2, m_CombineOr(m_Undef(), m_Zero())))
        return PoisonValue::get(C2->getType());
      // undef % X -> 0       otherwise
      return Constant::getNullValue(C1->getType());
    case Instruction::Or:                          // X | undef -> -1
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2)) // undef | undef -> undef
        return C1;
      return Constant::getAllOnesValue(C1->getType()); // undef | X -> ~0
    case Instruction::LShr:
      // X >>l undef -> poison
      if (isa<UndefValue>(C2))
        return PoisonValue::get(C2->getType());
      // undef >>l X -> 0
      return Constant::getNullValue(C1->getType());
    case Instruction::AShr:
      // X >>a undef -> poison
      if (isa<UndefValue>(C2))
        return PoisonValue::get(C2->getType());
      // TODO: undef >>a X -> poison if the shift is exact
      // undef >>a X -> 0
      return Constant::getNullValue(C1->getType());
    case Instruction::Shl:
      // X << undef -> undef
      if (isa<UndefValue>(C2))
        return PoisonValue::get(C2->getType());
      // undef << X -> 0
      return Constant::getNullValue(C1->getType());
    case Instruction::FSub:
      // -0.0 - undef --> undef (consistent with "fneg undef")
      if (match(C1, m_NegZeroFP()) && isa<UndefValue>(C2))
        return C2;
      [[fallthrough]];
    case Instruction::FAdd:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
      // [any flop] undef, undef -> undef
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2))
        return C1;
      // [any flop] C, undef -> NaN
      // [any flop] undef, C -> NaN
      // We could potentially specialize NaN/Inf constants vs. 'normal'
      // constants (possibly differently depending on opcode and operand). This
      // would allow returning undef sometimes. But it is always safe to fold to
      // NaN because we can choose the undef operand as NaN, and any FP opcode
      // with a NaN operand will propagate NaN.
      return ConstantFP::getNaN(C1->getType());
    case Instruction::BinaryOpsEnd:
      llvm_unreachable("Invalid BinaryOp");
    }
  }

  // Neither constant should be UndefValue, unless these are vector constants.
  assert((!HasScalarUndefOrScalableVectorUndef) && "Unexpected UndefValue");

  // Handle simplifications when the RHS is a constant int.
  if (ConstantInt *CI2 = dyn_cast<ConstantInt>(C2)) {
    switch (Opcode) {
    case Instruction::Mul:
      if (CI2->isZero())
        return C2; // X * 0 == 0
      break;
    case Instruction::UDiv:
    case Instruction::SDiv:
      if (CI2->isZero())
        return PoisonValue::get(CI2->getType());              // X / 0 == poison
      break;
    case Instruction::URem:
    case Instruction::SRem:
      if (CI2->isOne())
        return Constant::getNullValue(CI2->getType());        // X % 1 == 0
      if (CI2->isZero())
        return PoisonValue::get(CI2->getType());              // X % 0 == poison
      break;
    case Instruction::And:
      if (CI2->isZero())
        return C2; // X & 0 == 0

      if (ConstantExpr *CE1 = dyn_cast<ConstantExpr>(C1)) {
        // If and'ing the address of a global with a constant, fold it.
        if (CE1->getOpcode() == Instruction::PtrToInt &&
            isa<GlobalValue>(CE1->getOperand(0))) {
          GlobalValue *GV = cast<GlobalValue>(CE1->getOperand(0));

          Align GVAlign; // defaults to 1

          if (Module *TheModule = GV->getParent()) {
            const DataLayout &DL = TheModule->getDataLayout();
            GVAlign = GV->getPointerAlignment(DL);

            // If the function alignment is not specified then assume that it
            // is 4.
            // This is dangerous; on x86, the alignment of the pointer
            // corresponds to the alignment of the function, but might be less
            // than 4 if it isn't explicitly specified.
            // However, a fix for this behaviour was reverted because it
            // increased code size (see https://reviews.llvm.org/D55115)
            // FIXME: This code should be deleted once existing targets have
            // appropriate defaults
            if (isa<Function>(GV) && !DL.getFunctionPtrAlign())
              GVAlign = Align(4);
          } else if (isa<GlobalVariable>(GV)) {
            GVAlign = cast<GlobalVariable>(GV)->getAlign().valueOrOne();
          }

          if (GVAlign > 1) {
            unsigned DstWidth = CI2->getBitWidth();
            unsigned SrcWidth = std::min(DstWidth, Log2(GVAlign));
            APInt BitsNotSet(APInt::getLowBitsSet(DstWidth, SrcWidth));

            // If checking bits we know are clear, return zero.
            if ((CI2->getValue() & BitsNotSet) == CI2->getValue())
              return Constant::getNullValue(CI2->getType());
          }
        }
      }
      break;
    case Instruction::Or:
      if (CI2->isMinusOne())
        return C2; // X | -1 == -1
      break;
    }
  } else if (isa<ConstantInt>(C1)) {
    // If C1 is a ConstantInt and C2 is not, swap the operands.
    if (Instruction::isCommutative(Opcode))
      return ConstantExpr::isDesirableBinOp(Opcode)
                 ? ConstantExpr::get(Opcode, C2, C1)
                 : ConstantFoldBinaryInstruction(Opcode, C2, C1);
  }

  if (ConstantInt *CI1 = dyn_cast<ConstantInt>(C1)) {
    if (ConstantInt *CI2 = dyn_cast<ConstantInt>(C2)) {
      const APInt &C1V = CI1->getValue();
      const APInt &C2V = CI2->getValue();
      switch (Opcode) {
      default:
        break;
      case Instruction::Add:
        return ConstantInt::get(CI1->getContext(), C1V + C2V);
      case Instruction::Sub:
        return ConstantInt::get(CI1->getContext(), C1V - C2V);
      case Instruction::Mul:
        return ConstantInt::get(CI1->getContext(), C1V * C2V);
      case Instruction::UDiv:
        assert(!CI2->isZero() && "Div by zero handled above");
        return ConstantInt::get(CI1->getContext(), C1V.udiv(C2V));
      case Instruction::SDiv:
        assert(!CI2->isZero() && "Div by zero handled above");
        if (C2V.isAllOnes() && C1V.isMinSignedValue())
          return PoisonValue::get(CI1->getType());   // MIN_INT / -1 -> poison
        return ConstantInt::get(CI1->getContext(), C1V.sdiv(C2V));
      case Instruction::URem:
        assert(!CI2->isZero() && "Div by zero handled above");
        return ConstantInt::get(CI1->getContext(), C1V.urem(C2V));
      case Instruction::SRem:
        assert(!CI2->isZero() && "Div by zero handled above");
        if (C2V.isAllOnes() && C1V.isMinSignedValue())
          return PoisonValue::get(CI1->getType());   // MIN_INT % -1 -> poison
        return ConstantInt::get(CI1->getContext(), C1V.srem(C2V));
      case Instruction::And:
        return ConstantInt::get(CI1->getContext(), C1V & C2V);
      case Instruction::Or:
        return ConstantInt::get(CI1->getContext(), C1V | C2V);
      case Instruction::Xor:
        return ConstantInt::get(CI1->getContext(), C1V ^ C2V);
      case Instruction::Shl:
        if (C2V.ult(C1V.getBitWidth()))
          return ConstantInt::get(CI1->getContext(), C1V.shl(C2V));
        return PoisonValue::get(C1->getType()); // too big shift is poison
      case Instruction::LShr:
        if (C2V.ult(C1V.getBitWidth()))
          return ConstantInt::get(CI1->getContext(), C1V.lshr(C2V));
        return PoisonValue::get(C1->getType()); // too big shift is poison
      case Instruction::AShr:
        if (C2V.ult(C1V.getBitWidth()))
          return ConstantInt::get(CI1->getContext(), C1V.ashr(C2V));
        return PoisonValue::get(C1->getType()); // too big shift is poison
      }
    }

    switch (Opcode) {
    case Instruction::SDiv:
    case Instruction::UDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::Shl:
      if (CI1->isZero()) return C1;
      break;
    default:
      break;
    }
  } else if (ConstantFP *CFP1 = dyn_cast<ConstantFP>(C1)) {
    if (ConstantFP *CFP2 = dyn_cast<ConstantFP>(C2)) {
      const APFloat &C1V = CFP1->getValueAPF();
      const APFloat &C2V = CFP2->getValueAPF();
      APFloat C3V = C1V;  // copy for modification
      switch (Opcode) {
      default:
        break;
      case Instruction::FAdd:
        (void)C3V.add(C2V, APFloat::rmNearestTiesToEven);
        return ConstantFP::get(C1->getContext(), C3V);
      case Instruction::FSub:
        (void)C3V.subtract(C2V, APFloat::rmNearestTiesToEven);
        return ConstantFP::get(C1->getContext(), C3V);
      case Instruction::FMul:
        (void)C3V.multiply(C2V, APFloat::rmNearestTiesToEven);
        return ConstantFP::get(C1->getContext(), C3V);
      case Instruction::FDiv:
        (void)C3V.divide(C2V, APFloat::rmNearestTiesToEven);
        return ConstantFP::get(C1->getContext(), C3V);
      case Instruction::FRem:
        (void)C3V.mod(C2V);
        return ConstantFP::get(C1->getContext(), C3V);
      }
    }
  } else if (auto *VTy = dyn_cast<VectorType>(C1->getType())) {
    // Fast path for splatted constants.
    if (Constant *C2Splat = C2->getSplatValue()) {
      if (Instruction::isIntDivRem(Opcode) && C2Splat->isNullValue())
        return PoisonValue::get(VTy);
      if (Constant *C1Splat = C1->getSplatValue()) {
        Constant *Res =
            ConstantExpr::isDesirableBinOp(Opcode)
                ? ConstantExpr::get(Opcode, C1Splat, C2Splat)
                : ConstantFoldBinaryInstruction(Opcode, C1Splat, C2Splat);
        if (!Res)
          return nullptr;
        return ConstantVector::getSplat(VTy->getElementCount(), Res);
      }
    }

    if (auto *FVTy = dyn_cast<FixedVectorType>(VTy)) {
      // Fold each element and create a vector constant from those constants.
      SmallVector<Constant*, 16> Result;
      Type *Ty = IntegerType::get(FVTy->getContext(), 32);
      for (unsigned i = 0, e = FVTy->getNumElements(); i != e; ++i) {
        Constant *ExtractIdx = ConstantInt::get(Ty, i);
        Constant *LHS = ConstantExpr::getExtractElement(C1, ExtractIdx);
        Constant *RHS = ConstantExpr::getExtractElement(C2, ExtractIdx);

        // If any element of a divisor vector is zero, the whole op is poison.
        if (Instruction::isIntDivRem(Opcode) && RHS->isNullValue())
          return PoisonValue::get(VTy);

        Constant *Res = ConstantExpr::isDesirableBinOp(Opcode)
                            ? ConstantExpr::get(Opcode, LHS, RHS)
                            : ConstantFoldBinaryInstruction(Opcode, LHS, RHS);
        if (!Res)
          return nullptr;
        Result.push_back(Res);
      }

      return ConstantVector::get(Result);
    }
  }

  if (ConstantExpr *CE1 = dyn_cast<ConstantExpr>(C1)) {
    // There are many possible foldings we could do here.  We should probably
    // at least fold add of a pointer with an integer into the appropriate
    // getelementptr.  This will improve alias analysis a bit.

    // Given ((a + b) + c), if (b + c) folds to something interesting, return
    // (a + (b + c)).
    if (Instruction::isAssociative(Opcode) && CE1->getOpcode() == Opcode) {
      Constant *T = ConstantExpr::get(Opcode, CE1->getOperand(1), C2);
      if (!isa<ConstantExpr>(T) || cast<ConstantExpr>(T)->getOpcode() != Opcode)
        return ConstantExpr::get(Opcode, CE1->getOperand(0), T);
    }
  } else if (isa<ConstantExpr>(C2)) {
    // If C2 is a constant expr and C1 isn't, flop them around and fold the
    // other way if possible.
    if (Instruction::isCommutative(Opcode))
      return ConstantFoldBinaryInstruction(Opcode, C2, C1);
  }

  // i1 can be simplified in many cases.
  if (C1->getType()->isIntegerTy(1)) {
    switch (Opcode) {
    case Instruction::Add:
    case Instruction::Sub:
      return ConstantExpr::getXor(C1, C2);
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
      // We can assume that C2 == 0.  If it were one the result would be
      // undefined because the shift value is as large as the bitwidth.
      return C1;
    case Instruction::SDiv:
    case Instruction::UDiv:
      // We can assume that C2 == 1.  If it were zero the result would be
      // undefined through division by zero.
      return C1;
    case Instruction::URem:
    case Instruction::SRem:
      // We can assume that C2 == 1.  If it were zero the result would be
      // undefined through division by zero.
      return ConstantInt::getFalse(C1->getContext());
    default:
      break;
    }
  }

  // We don't know how to fold this.
  return nullptr;
}

static ICmpInst::Predicate areGlobalsPotentiallyEqual(const GlobalValue *GV1,
                                                      const GlobalValue *GV2) {
  auto isGlobalUnsafeForEquality = [](const GlobalValue *GV) {
    if (GV->isInterposable() || GV->hasGlobalUnnamedAddr())
      return true;
    if (const auto *GVar = dyn_cast<GlobalVariable>(GV)) {
      Type *Ty = GVar->getValueType();
      // A global with opaque type might end up being zero sized.
      if (!Ty->isSized())
        return true;
      // A global with an empty type might lie at the address of any other
      // global.
      if (Ty->isEmptyTy())
        return true;
    }
    return false;
  };
  // Don't try to decide equality of aliases.
  if (!isa<GlobalAlias>(GV1) && !isa<GlobalAlias>(GV2))
    if (!isGlobalUnsafeForEquality(GV1) && !isGlobalUnsafeForEquality(GV2))
      return ICmpInst::ICMP_NE;
  return ICmpInst::BAD_ICMP_PREDICATE;
}

/// This function determines if there is anything we can decide about the two
/// constants provided. This doesn't need to handle simple things like integer
/// comparisons, but should instead handle ConstantExprs and GlobalValues.
/// If we can determine that the two constants have a particular relation to
/// each other, we should return the corresponding ICmp predicate, otherwise
/// return ICmpInst::BAD_ICMP_PREDICATE.
static ICmpInst::Predicate evaluateICmpRelation(Constant *V1, Constant *V2) {
  assert(V1->getType() == V2->getType() &&
         "Cannot compare different types of values!");
  if (V1 == V2) return ICmpInst::ICMP_EQ;

  // The following folds only apply to pointers.
  if (!V1->getType()->isPointerTy())
    return ICmpInst::BAD_ICMP_PREDICATE;

  // To simplify this code we canonicalize the relation so that the first
  // operand is always the most "complex" of the two.  We consider simple
  // constants (like ConstantPointerNull) to be the simplest, followed by
  // BlockAddress, GlobalValues, and ConstantExpr's (the most complex).
  auto GetComplexity = [](Constant *V) {
    if (isa<ConstantExpr>(V))
      return 3;
    if (isa<GlobalValue>(V))
      return 2;
    if (isa<BlockAddress>(V))
      return 1;
    return 0;
  };
  if (GetComplexity(V1) < GetComplexity(V2)) {
    ICmpInst::Predicate SwappedRelation = evaluateICmpRelation(V2, V1);
    if (SwappedRelation != ICmpInst::BAD_ICMP_PREDICATE)
      return ICmpInst::getSwappedPredicate(SwappedRelation);
    return ICmpInst::BAD_ICMP_PREDICATE;
  }

  if (const BlockAddress *BA = dyn_cast<BlockAddress>(V1)) {
    // Now we know that the RHS is a BlockAddress or simple constant.
    if (const BlockAddress *BA2 = dyn_cast<BlockAddress>(V2)) {
      // Block address in another function can't equal this one, but block
      // addresses in the current function might be the same if blocks are
      // empty.
      if (BA2->getFunction() != BA->getFunction())
        return ICmpInst::ICMP_NE;
    } else if (isa<ConstantPointerNull>(V2)) {
      return ICmpInst::ICMP_NE;
    }
  } else if (const GlobalValue *GV = dyn_cast<GlobalValue>(V1)) {
    // Now we know that the RHS is a GlobalValue, BlockAddress or simple
    // constant.
    if (const GlobalValue *GV2 = dyn_cast<GlobalValue>(V2)) {
      return areGlobalsPotentiallyEqual(GV, GV2);
    } else if (isa<BlockAddress>(V2)) {
      return ICmpInst::ICMP_NE; // Globals never equal labels.
    } else if (isa<ConstantPointerNull>(V2)) {
      // GlobalVals can never be null unless they have external weak linkage.
      // We don't try to evaluate aliases here.
      // NOTE: We should not be doing this constant folding if null pointer
      // is considered valid for the function. But currently there is no way to
      // query it from the Constant type.
      if (!GV->hasExternalWeakLinkage() && !isa<GlobalAlias>(GV) &&
          !NullPointerIsDefined(nullptr /* F */,
                                GV->getType()->getAddressSpace()))
        return ICmpInst::ICMP_UGT;
    }
  } else if (auto *CE1 = dyn_cast<ConstantExpr>(V1)) {
    // Ok, the LHS is known to be a constantexpr.  The RHS can be any of a
    // constantexpr, a global, block address, or a simple constant.
    Constant *CE1Op0 = CE1->getOperand(0);

    switch (CE1->getOpcode()) {
    case Instruction::GetElementPtr: {
      GEPOperator *CE1GEP = cast<GEPOperator>(CE1);
      // Ok, since this is a getelementptr, we know that the constant has a
      // pointer type.  Check the various cases.
      if (isa<ConstantPointerNull>(V2)) {
        // If we are comparing a GEP to a null pointer, check to see if the base
        // of the GEP equals the null pointer.
        if (const GlobalValue *GV = dyn_cast<GlobalValue>(CE1Op0)) {
          // If its not weak linkage, the GVal must have a non-zero address
          // so the result is greater-than
          if (!GV->hasExternalWeakLinkage() && CE1GEP->isInBounds())
            return ICmpInst::ICMP_UGT;
        }
      } else if (const GlobalValue *GV2 = dyn_cast<GlobalValue>(V2)) {
        if (const GlobalValue *GV = dyn_cast<GlobalValue>(CE1Op0)) {
          if (GV != GV2) {
            if (CE1GEP->hasAllZeroIndices())
              return areGlobalsPotentiallyEqual(GV, GV2);
            return ICmpInst::BAD_ICMP_PREDICATE;
          }
        }
      } else if (const auto *CE2GEP = dyn_cast<GEPOperator>(V2)) {
        // By far the most common case to handle is when the base pointers are
        // obviously to the same global.
        const Constant *CE2Op0 = cast<Constant>(CE2GEP->getPointerOperand());
        if (isa<GlobalValue>(CE1Op0) && isa<GlobalValue>(CE2Op0)) {
          // Don't know relative ordering, but check for inequality.
          if (CE1Op0 != CE2Op0) {
            if (CE1GEP->hasAllZeroIndices() && CE2GEP->hasAllZeroIndices())
              return areGlobalsPotentiallyEqual(cast<GlobalValue>(CE1Op0),
                                                cast<GlobalValue>(CE2Op0));
            return ICmpInst::BAD_ICMP_PREDICATE;
          }
        }
      }
      break;
    }
    default:
      break;
    }
  }

  return ICmpInst::BAD_ICMP_PREDICATE;
}

Constant *llvm::ConstantFoldCompareInstruction(CmpInst::Predicate Predicate,
                                               Constant *C1, Constant *C2) {
  Type *ResultTy;
  if (VectorType *VT = dyn_cast<VectorType>(C1->getType()))
    ResultTy = VectorType::get(Type::getInt1Ty(C1->getContext()),
                               VT->getElementCount());
  else
    ResultTy = Type::getInt1Ty(C1->getContext());

  // Fold FCMP_FALSE/FCMP_TRUE unconditionally.
  if (Predicate == FCmpInst::FCMP_FALSE)
    return Constant::getNullValue(ResultTy);

  if (Predicate == FCmpInst::FCMP_TRUE)
    return Constant::getAllOnesValue(ResultTy);

  // Handle some degenerate cases first
  if (isa<PoisonValue>(C1) || isa<PoisonValue>(C2))
    return PoisonValue::get(ResultTy);

  if (isa<UndefValue>(C1) || isa<UndefValue>(C2)) {
    bool isIntegerPredicate = ICmpInst::isIntPredicate(Predicate);
    // For EQ and NE, we can always pick a value for the undef to make the
    // predicate pass or fail, so we can return undef.
    // Also, if both operands are undef, we can return undef for int comparison.
    if (ICmpInst::isEquality(Predicate) || (isIntegerPredicate && C1 == C2))
      return UndefValue::get(ResultTy);

    // Otherwise, for integer compare, pick the same value as the non-undef
    // operand, and fold it to true or false.
    if (isIntegerPredicate)
      return ConstantInt::get(ResultTy, CmpInst::isTrueWhenEqual(Predicate));

    // Choosing NaN for the undef will always make unordered comparison succeed
    // and ordered comparison fails.
    return ConstantInt::get(ResultTy, CmpInst::isUnordered(Predicate));
  }

  if (C2->isNullValue()) {
    // The caller is expected to commute the operands if the constant expression
    // is C2.
    // C1 >= 0 --> true
    if (Predicate == ICmpInst::ICMP_UGE)
      return Constant::getAllOnesValue(ResultTy);
    // C1 < 0 --> false
    if (Predicate == ICmpInst::ICMP_ULT)
      return Constant::getNullValue(ResultTy);
  }

  // If the comparison is a comparison between two i1's, simplify it.
  if (C1->getType()->isIntegerTy(1)) {
    switch (Predicate) {
    case ICmpInst::ICMP_EQ:
      if (isa<ConstantInt>(C2))
        return ConstantExpr::getXor(C1, ConstantExpr::getNot(C2));
      return ConstantExpr::getXor(ConstantExpr::getNot(C1), C2);
    case ICmpInst::ICMP_NE:
      return ConstantExpr::getXor(C1, C2);
    default:
      break;
    }
  }

  if (isa<ConstantInt>(C1) && isa<ConstantInt>(C2)) {
    const APInt &V1 = cast<ConstantInt>(C1)->getValue();
    const APInt &V2 = cast<ConstantInt>(C2)->getValue();
    return ConstantInt::get(ResultTy, ICmpInst::compare(V1, V2, Predicate));
  } else if (isa<ConstantFP>(C1) && isa<ConstantFP>(C2)) {
    const APFloat &C1V = cast<ConstantFP>(C1)->getValueAPF();
    const APFloat &C2V = cast<ConstantFP>(C2)->getValueAPF();
    return ConstantInt::get(ResultTy, FCmpInst::compare(C1V, C2V, Predicate));
  } else if (auto *C1VTy = dyn_cast<VectorType>(C1->getType())) {

    // Fast path for splatted constants.
    if (Constant *C1Splat = C1->getSplatValue())
      if (Constant *C2Splat = C2->getSplatValue())
        if (Constant *Elt =
                ConstantFoldCompareInstruction(Predicate, C1Splat, C2Splat))
          return ConstantVector::getSplat(C1VTy->getElementCount(), Elt);

    // Do not iterate on scalable vector. The number of elements is unknown at
    // compile-time.
    if (isa<ScalableVectorType>(C1VTy))
      return nullptr;

    // If we can constant fold the comparison of each element, constant fold
    // the whole vector comparison.
    SmallVector<Constant*, 4> ResElts;
    Type *Ty = IntegerType::get(C1->getContext(), 32);
    // Compare the elements, producing an i1 result or constant expr.
    for (unsigned I = 0, E = C1VTy->getElementCount().getKnownMinValue();
         I != E; ++I) {
      Constant *C1E =
          ConstantExpr::getExtractElement(C1, ConstantInt::get(Ty, I));
      Constant *C2E =
          ConstantExpr::getExtractElement(C2, ConstantInt::get(Ty, I));
      Constant *Elt = ConstantFoldCompareInstruction(Predicate, C1E, C2E);
      if (!Elt)
        return nullptr;

      ResElts.push_back(Elt);
    }

    return ConstantVector::get(ResElts);
  }

  if (C1->getType()->isFPOrFPVectorTy()) {
    if (C1 == C2) {
      // We know that C1 == C2 || isUnordered(C1, C2).
      if (Predicate == FCmpInst::FCMP_ONE)
        return ConstantInt::getFalse(ResultTy);
      else if (Predicate == FCmpInst::FCMP_UEQ)
        return ConstantInt::getTrue(ResultTy);
    }
  } else {
    // Evaluate the relation between the two constants, per the predicate.
    int Result = -1;  // -1 = unknown, 0 = known false, 1 = known true.
    switch (evaluateICmpRelation(C1, C2)) {
    default: llvm_unreachable("Unknown relational!");
    case ICmpInst::BAD_ICMP_PREDICATE:
      break;  // Couldn't determine anything about these constants.
    case ICmpInst::ICMP_EQ:   // We know the constants are equal!
      // If we know the constants are equal, we can decide the result of this
      // computation precisely.
      Result = ICmpInst::isTrueWhenEqual(Predicate);
      break;
    case ICmpInst::ICMP_ULT:
      switch (Predicate) {
      case ICmpInst::ICMP_ULT: case ICmpInst::ICMP_NE: case ICmpInst::ICMP_ULE:
        Result = 1; break;
      case ICmpInst::ICMP_UGT: case ICmpInst::ICMP_EQ: case ICmpInst::ICMP_UGE:
        Result = 0; break;
      default:
        break;
      }
      break;
    case ICmpInst::ICMP_SLT:
      switch (Predicate) {
      case ICmpInst::ICMP_SLT: case ICmpInst::ICMP_NE: case ICmpInst::ICMP_SLE:
        Result = 1; break;
      case ICmpInst::ICMP_SGT: case ICmpInst::ICMP_EQ: case ICmpInst::ICMP_SGE:
        Result = 0; break;
      default:
        break;
      }
      break;
    case ICmpInst::ICMP_UGT:
      switch (Predicate) {
      case ICmpInst::ICMP_UGT: case ICmpInst::ICMP_NE: case ICmpInst::ICMP_UGE:
        Result = 1; break;
      case ICmpInst::ICMP_ULT: case ICmpInst::ICMP_EQ: case ICmpInst::ICMP_ULE:
        Result = 0; break;
      default:
        break;
      }
      break;
    case ICmpInst::ICMP_SGT:
      switch (Predicate) {
      case ICmpInst::ICMP_SGT: case ICmpInst::ICMP_NE: case ICmpInst::ICMP_SGE:
        Result = 1; break;
      case ICmpInst::ICMP_SLT: case ICmpInst::ICMP_EQ: case ICmpInst::ICMP_SLE:
        Result = 0; break;
      default:
        break;
      }
      break;
    case ICmpInst::ICMP_ULE:
      if (Predicate == ICmpInst::ICMP_UGT)
        Result = 0;
      if (Predicate == ICmpInst::ICMP_ULT || Predicate == ICmpInst::ICMP_ULE)
        Result = 1;
      break;
    case ICmpInst::ICMP_SLE:
      if (Predicate == ICmpInst::ICMP_SGT)
        Result = 0;
      if (Predicate == ICmpInst::ICMP_SLT || Predicate == ICmpInst::ICMP_SLE)
        Result = 1;
      break;
    case ICmpInst::ICMP_UGE:
      if (Predicate == ICmpInst::ICMP_ULT)
        Result = 0;
      if (Predicate == ICmpInst::ICMP_UGT || Predicate == ICmpInst::ICMP_UGE)
        Result = 1;
      break;
    case ICmpInst::ICMP_SGE:
      if (Predicate == ICmpInst::ICMP_SLT)
        Result = 0;
      if (Predicate == ICmpInst::ICMP_SGT || Predicate == ICmpInst::ICMP_SGE)
        Result = 1;
      break;
    case ICmpInst::ICMP_NE:
      if (Predicate == ICmpInst::ICMP_EQ)
        Result = 0;
      if (Predicate == ICmpInst::ICMP_NE)
        Result = 1;
      break;
    }

    // If we evaluated the result, return it now.
    if (Result != -1)
      return ConstantInt::get(ResultTy, Result);

    if ((!isa<ConstantExpr>(C1) && isa<ConstantExpr>(C2)) ||
        (C1->isNullValue() && !C2->isNullValue())) {
      // If C2 is a constant expr and C1 isn't, flip them around and fold the
      // other way if possible.
      // Also, if C1 is null and C2 isn't, flip them around.
      Predicate = ICmpInst::getSwappedPredicate(Predicate);
      return ConstantFoldCompareInstruction(Predicate, C2, C1);
    }
  }
  return nullptr;
}

Constant *llvm::ConstantFoldGetElementPtr(Type *PointeeTy, Constant *C,
                                          std::optional<ConstantRange> InRange,
                                          ArrayRef<Value *> Idxs) {
  if (Idxs.empty()) return C;

  Type *GEPTy = GetElementPtrInst::getGEPReturnType(
      C, ArrayRef((Value *const *)Idxs.data(), Idxs.size()));

  if (isa<PoisonValue>(C))
    return PoisonValue::get(GEPTy);

  if (isa<UndefValue>(C))
    return UndefValue::get(GEPTy);

  auto IsNoOp = [&]() {
    // Avoid losing inrange information.
    if (InRange)
      return false;

    return all_of(Idxs, [](Value *Idx) {
      Constant *IdxC = cast<Constant>(Idx);
      return IdxC->isNullValue() || isa<UndefValue>(IdxC);
    });
  };
  if (IsNoOp())
    return GEPTy->isVectorTy() && !C->getType()->isVectorTy()
               ? ConstantVector::getSplat(
                     cast<VectorType>(GEPTy)->getElementCount(), C)
               : C;

  return nullptr;
}
