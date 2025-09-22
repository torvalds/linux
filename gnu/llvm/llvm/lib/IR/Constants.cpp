//===-- Constants.cpp - Implement Constant nodes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Constant* classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Constants.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/ConstantFold.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;
using namespace PatternMatch;

// As set of temporary options to help migrate how splats are represented.
static cl::opt<bool> UseConstantIntForFixedLengthSplat(
    "use-constant-int-for-fixed-length-splat", cl::init(false), cl::Hidden,
    cl::desc("Use ConstantInt's native fixed-length vector splat support."));
static cl::opt<bool> UseConstantFPForFixedLengthSplat(
    "use-constant-fp-for-fixed-length-splat", cl::init(false), cl::Hidden,
    cl::desc("Use ConstantFP's native fixed-length vector splat support."));
static cl::opt<bool> UseConstantIntForScalableSplat(
    "use-constant-int-for-scalable-splat", cl::init(false), cl::Hidden,
    cl::desc("Use ConstantInt's native scalable vector splat support."));
static cl::opt<bool> UseConstantFPForScalableSplat(
    "use-constant-fp-for-scalable-splat", cl::init(false), cl::Hidden,
    cl::desc("Use ConstantFP's native scalable vector splat support."));

//===----------------------------------------------------------------------===//
//                              Constant Class
//===----------------------------------------------------------------------===//

bool Constant::isNegativeZeroValue() const {
  // Floating point values have an explicit -0.0 value.
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->isZero() && CFP->isNegative();

  // Equivalent for a vector of -0.0's.
  if (getType()->isVectorTy())
    if (const auto *SplatCFP = dyn_cast_or_null<ConstantFP>(getSplatValue()))
      return SplatCFP->isNegativeZeroValue();

  // We've already handled true FP case; any other FP vectors can't represent -0.0.
  if (getType()->isFPOrFPVectorTy())
    return false;

  // Otherwise, just use +0.0.
  return isNullValue();
}

// Return true iff this constant is positive zero (floating point), negative
// zero (floating point), or a null value.
bool Constant::isZeroValue() const {
  // Floating point values have an explicit -0.0 value.
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->isZero();

  // Check for constant splat vectors of 1 values.
  if (getType()->isVectorTy())
    if (const auto *SplatCFP = dyn_cast_or_null<ConstantFP>(getSplatValue()))
      return SplatCFP->isZero();

  // Otherwise, just use +0.0.
  return isNullValue();
}

bool Constant::isNullValue() const {
  // 0 is null.
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->isZero();

  // +0.0 is null.
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    // ppc_fp128 determine isZero using high order double only
    // Should check the bitwise value to make sure all bits are zero.
    return CFP->isExactlyValue(+0.0);

  // constant zero is zero for aggregates, cpnull is null for pointers, none for
  // tokens.
  return isa<ConstantAggregateZero>(this) || isa<ConstantPointerNull>(this) ||
         isa<ConstantTokenNone>(this) || isa<ConstantTargetNone>(this);
}

bool Constant::isAllOnesValue() const {
  // Check for -1 integers
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->isMinusOne();

  // Check for FP which are bitcasted from -1 integers
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().bitcastToAPInt().isAllOnes();

  // Check for constant splat vectors of 1 values.
  if (getType()->isVectorTy())
    if (const auto *SplatVal = getSplatValue())
      return SplatVal->isAllOnesValue();

  return false;
}

bool Constant::isOneValue() const {
  // Check for 1 integers
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->isOne();

  // Check for FP which are bitcasted from 1 integers
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().bitcastToAPInt().isOne();

  // Check for constant splat vectors of 1 values.
  if (getType()->isVectorTy())
    if (const auto *SplatVal = getSplatValue())
      return SplatVal->isOneValue();

  return false;
}

bool Constant::isNotOneValue() const {
  // Check for 1 integers
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return !CI->isOneValue();

  // Check for FP which are bitcasted from 1 integers
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return !CFP->getValueAPF().bitcastToAPInt().isOne();

  // Check that vectors don't contain 1
  if (auto *VTy = dyn_cast<FixedVectorType>(getType())) {
    for (unsigned I = 0, E = VTy->getNumElements(); I != E; ++I) {
      Constant *Elt = getAggregateElement(I);
      if (!Elt || !Elt->isNotOneValue())
        return false;
    }
    return true;
  }

  // Check for splats that don't contain 1
  if (getType()->isVectorTy())
    if (const auto *SplatVal = getSplatValue())
      return SplatVal->isNotOneValue();

  // It *may* contain 1, we can't tell.
  return false;
}

bool Constant::isMinSignedValue() const {
  // Check for INT_MIN integers
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->isMinValue(/*isSigned=*/true);

  // Check for FP which are bitcasted from INT_MIN integers
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().bitcastToAPInt().isMinSignedValue();

  // Check for splats of INT_MIN values.
  if (getType()->isVectorTy())
    if (const auto *SplatVal = getSplatValue())
      return SplatVal->isMinSignedValue();

  return false;
}

bool Constant::isNotMinSignedValue() const {
  // Check for INT_MIN integers
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return !CI->isMinValue(/*isSigned=*/true);

  // Check for FP which are bitcasted from INT_MIN integers
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return !CFP->getValueAPF().bitcastToAPInt().isMinSignedValue();

  // Check that vectors don't contain INT_MIN
  if (auto *VTy = dyn_cast<FixedVectorType>(getType())) {
    for (unsigned I = 0, E = VTy->getNumElements(); I != E; ++I) {
      Constant *Elt = getAggregateElement(I);
      if (!Elt || !Elt->isNotMinSignedValue())
        return false;
    }
    return true;
  }

  // Check for splats that aren't INT_MIN
  if (getType()->isVectorTy())
    if (const auto *SplatVal = getSplatValue())
      return SplatVal->isNotMinSignedValue();

  // It *may* contain INT_MIN, we can't tell.
  return false;
}

bool Constant::isFiniteNonZeroFP() const {
  if (auto *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().isFiniteNonZero();

  if (auto *VTy = dyn_cast<FixedVectorType>(getType())) {
    for (unsigned I = 0, E = VTy->getNumElements(); I != E; ++I) {
      auto *CFP = dyn_cast_or_null<ConstantFP>(getAggregateElement(I));
      if (!CFP || !CFP->getValueAPF().isFiniteNonZero())
        return false;
    }
    return true;
  }

  if (getType()->isVectorTy())
    if (const auto *SplatCFP = dyn_cast_or_null<ConstantFP>(getSplatValue()))
      return SplatCFP->isFiniteNonZeroFP();

  // It *may* contain finite non-zero, we can't tell.
  return false;
}

bool Constant::isNormalFP() const {
  if (auto *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().isNormal();

  if (auto *VTy = dyn_cast<FixedVectorType>(getType())) {
    for (unsigned I = 0, E = VTy->getNumElements(); I != E; ++I) {
      auto *CFP = dyn_cast_or_null<ConstantFP>(getAggregateElement(I));
      if (!CFP || !CFP->getValueAPF().isNormal())
        return false;
    }
    return true;
  }

  if (getType()->isVectorTy())
    if (const auto *SplatCFP = dyn_cast_or_null<ConstantFP>(getSplatValue()))
      return SplatCFP->isNormalFP();

  // It *may* contain a normal fp value, we can't tell.
  return false;
}

bool Constant::hasExactInverseFP() const {
  if (auto *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().getExactInverse(nullptr);

  if (auto *VTy = dyn_cast<FixedVectorType>(getType())) {
    for (unsigned I = 0, E = VTy->getNumElements(); I != E; ++I) {
      auto *CFP = dyn_cast_or_null<ConstantFP>(getAggregateElement(I));
      if (!CFP || !CFP->getValueAPF().getExactInverse(nullptr))
        return false;
    }
    return true;
  }

  if (getType()->isVectorTy())
    if (const auto *SplatCFP = dyn_cast_or_null<ConstantFP>(getSplatValue()))
      return SplatCFP->hasExactInverseFP();

  // It *may* have an exact inverse fp value, we can't tell.
  return false;
}

bool Constant::isNaN() const {
  if (auto *CFP = dyn_cast<ConstantFP>(this))
    return CFP->isNaN();

  if (auto *VTy = dyn_cast<FixedVectorType>(getType())) {
    for (unsigned I = 0, E = VTy->getNumElements(); I != E; ++I) {
      auto *CFP = dyn_cast_or_null<ConstantFP>(getAggregateElement(I));
      if (!CFP || !CFP->isNaN())
        return false;
    }
    return true;
  }

  if (getType()->isVectorTy())
    if (const auto *SplatCFP = dyn_cast_or_null<ConstantFP>(getSplatValue()))
      return SplatCFP->isNaN();

  // It *may* be NaN, we can't tell.
  return false;
}

bool Constant::isElementWiseEqual(Value *Y) const {
  // Are they fully identical?
  if (this == Y)
    return true;

  // The input value must be a vector constant with the same type.
  auto *VTy = dyn_cast<VectorType>(getType());
  if (!isa<Constant>(Y) || !VTy || VTy != Y->getType())
    return false;

  // TODO: Compare pointer constants?
  if (!(VTy->getElementType()->isIntegerTy() ||
        VTy->getElementType()->isFloatingPointTy()))
    return false;

  // They may still be identical element-wise (if they have `undef`s).
  // Bitcast to integer to allow exact bitwise comparison for all types.
  Type *IntTy = VectorType::getInteger(VTy);
  Constant *C0 = ConstantExpr::getBitCast(const_cast<Constant *>(this), IntTy);
  Constant *C1 = ConstantExpr::getBitCast(cast<Constant>(Y), IntTy);
  Constant *CmpEq = ConstantFoldCompareInstruction(ICmpInst::ICMP_EQ, C0, C1);
  return CmpEq && (isa<PoisonValue>(CmpEq) || match(CmpEq, m_One()));
}

static bool
containsUndefinedElement(const Constant *C,
                         function_ref<bool(const Constant *)> HasFn) {
  if (auto *VTy = dyn_cast<VectorType>(C->getType())) {
    if (HasFn(C))
      return true;
    if (isa<ConstantAggregateZero>(C))
      return false;
    if (isa<ScalableVectorType>(C->getType()))
      return false;

    for (unsigned i = 0, e = cast<FixedVectorType>(VTy)->getNumElements();
         i != e; ++i) {
      if (Constant *Elem = C->getAggregateElement(i))
        if (HasFn(Elem))
          return true;
    }
  }

  return false;
}

bool Constant::containsUndefOrPoisonElement() const {
  return containsUndefinedElement(
      this, [&](const auto *C) { return isa<UndefValue>(C); });
}

bool Constant::containsPoisonElement() const {
  return containsUndefinedElement(
      this, [&](const auto *C) { return isa<PoisonValue>(C); });
}

bool Constant::containsUndefElement() const {
  return containsUndefinedElement(this, [&](const auto *C) {
    return isa<UndefValue>(C) && !isa<PoisonValue>(C);
  });
}

bool Constant::containsConstantExpression() const {
  if (auto *VTy = dyn_cast<FixedVectorType>(getType())) {
    for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
      if (isa<ConstantExpr>(getAggregateElement(i)))
        return true;
  }
  return false;
}

/// Constructor to create a '0' constant of arbitrary type.
Constant *Constant::getNullValue(Type *Ty) {
  switch (Ty->getTypeID()) {
  case Type::IntegerTyID:
    return ConstantInt::get(Ty, 0);
  case Type::HalfTyID:
  case Type::BFloatTyID:
  case Type::FloatTyID:
  case Type::DoubleTyID:
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
    return ConstantFP::get(Ty->getContext(),
                           APFloat::getZero(Ty->getFltSemantics()));
  case Type::PointerTyID:
    return ConstantPointerNull::get(cast<PointerType>(Ty));
  case Type::StructTyID:
  case Type::ArrayTyID:
  case Type::FixedVectorTyID:
  case Type::ScalableVectorTyID:
    return ConstantAggregateZero::get(Ty);
  case Type::TokenTyID:
    return ConstantTokenNone::get(Ty->getContext());
  case Type::TargetExtTyID:
    return ConstantTargetNone::get(cast<TargetExtType>(Ty));
  default:
    // Function, Label, or Opaque type?
    llvm_unreachable("Cannot create a null constant of that type!");
  }
}

Constant *Constant::getIntegerValue(Type *Ty, const APInt &V) {
  Type *ScalarTy = Ty->getScalarType();

  // Create the base integer constant.
  Constant *C = ConstantInt::get(Ty->getContext(), V);

  // Convert an integer to a pointer, if necessary.
  if (PointerType *PTy = dyn_cast<PointerType>(ScalarTy))
    C = ConstantExpr::getIntToPtr(C, PTy);

  // Broadcast a scalar to a vector, if necessary.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    C = ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

Constant *Constant::getAllOnesValue(Type *Ty) {
  if (IntegerType *ITy = dyn_cast<IntegerType>(Ty))
    return ConstantInt::get(Ty->getContext(),
                            APInt::getAllOnes(ITy->getBitWidth()));

  if (Ty->isFloatingPointTy()) {
    APFloat FL = APFloat::getAllOnesValue(Ty->getFltSemantics());
    return ConstantFP::get(Ty->getContext(), FL);
  }

  VectorType *VTy = cast<VectorType>(Ty);
  return ConstantVector::getSplat(VTy->getElementCount(),
                                  getAllOnesValue(VTy->getElementType()));
}

Constant *Constant::getAggregateElement(unsigned Elt) const {
  assert((getType()->isAggregateType() || getType()->isVectorTy()) &&
         "Must be an aggregate/vector constant");

  if (const auto *CC = dyn_cast<ConstantAggregate>(this))
    return Elt < CC->getNumOperands() ? CC->getOperand(Elt) : nullptr;

  if (const auto *CAZ = dyn_cast<ConstantAggregateZero>(this))
    return Elt < CAZ->getElementCount().getKnownMinValue()
               ? CAZ->getElementValue(Elt)
               : nullptr;

  // FIXME: getNumElements() will fail for non-fixed vector types.
  if (isa<ScalableVectorType>(getType()))
    return nullptr;

  if (const auto *PV = dyn_cast<PoisonValue>(this))
    return Elt < PV->getNumElements() ? PV->getElementValue(Elt) : nullptr;

  if (const auto *UV = dyn_cast<UndefValue>(this))
    return Elt < UV->getNumElements() ? UV->getElementValue(Elt) : nullptr;

  if (const auto *CDS = dyn_cast<ConstantDataSequential>(this))
    return Elt < CDS->getNumElements() ? CDS->getElementAsConstant(Elt)
                                       : nullptr;

  return nullptr;
}

Constant *Constant::getAggregateElement(Constant *Elt) const {
  assert(isa<IntegerType>(Elt->getType()) && "Index must be an integer");
  if (ConstantInt *CI = dyn_cast<ConstantInt>(Elt)) {
    // Check if the constant fits into an uint64_t.
    if (CI->getValue().getActiveBits() > 64)
      return nullptr;
    return getAggregateElement(CI->getZExtValue());
  }
  return nullptr;
}

void Constant::destroyConstant() {
  /// First call destroyConstantImpl on the subclass.  This gives the subclass
  /// a chance to remove the constant from any maps/pools it's contained in.
  switch (getValueID()) {
  default:
    llvm_unreachable("Not a constant!");
#define HANDLE_CONSTANT(Name)                                                  \
  case Value::Name##Val:                                                       \
    cast<Name>(this)->destroyConstantImpl();                                   \
    break;
#include "llvm/IR/Value.def"
  }

  // When a Constant is destroyed, there may be lingering
  // references to the constant by other constants in the constant pool.  These
  // constants are implicitly dependent on the module that is being deleted,
  // but they don't know that.  Because we only find out when the CPV is
  // deleted, we must now notify all of our users (that should only be
  // Constants) that they are, in fact, invalid now and should be deleted.
  //
  while (!use_empty()) {
    Value *V = user_back();
#ifndef NDEBUG // Only in -g mode...
    if (!isa<Constant>(V)) {
      dbgs() << "While deleting: " << *this
             << "\n\nUse still stuck around after Def is destroyed: " << *V
             << "\n\n";
    }
#endif
    assert(isa<Constant>(V) && "References remain to Constant being destroyed");
    cast<Constant>(V)->destroyConstant();

    // The constant should remove itself from our use list...
    assert((use_empty() || user_back() != V) && "Constant not removed!");
  }

  // Value has no outstanding references it is safe to delete it now...
  deleteConstant(this);
}

void llvm::deleteConstant(Constant *C) {
  switch (C->getValueID()) {
  case Constant::ConstantIntVal:
    delete static_cast<ConstantInt *>(C);
    break;
  case Constant::ConstantFPVal:
    delete static_cast<ConstantFP *>(C);
    break;
  case Constant::ConstantAggregateZeroVal:
    delete static_cast<ConstantAggregateZero *>(C);
    break;
  case Constant::ConstantArrayVal:
    delete static_cast<ConstantArray *>(C);
    break;
  case Constant::ConstantStructVal:
    delete static_cast<ConstantStruct *>(C);
    break;
  case Constant::ConstantVectorVal:
    delete static_cast<ConstantVector *>(C);
    break;
  case Constant::ConstantPointerNullVal:
    delete static_cast<ConstantPointerNull *>(C);
    break;
  case Constant::ConstantDataArrayVal:
    delete static_cast<ConstantDataArray *>(C);
    break;
  case Constant::ConstantDataVectorVal:
    delete static_cast<ConstantDataVector *>(C);
    break;
  case Constant::ConstantTokenNoneVal:
    delete static_cast<ConstantTokenNone *>(C);
    break;
  case Constant::BlockAddressVal:
    delete static_cast<BlockAddress *>(C);
    break;
  case Constant::DSOLocalEquivalentVal:
    delete static_cast<DSOLocalEquivalent *>(C);
    break;
  case Constant::NoCFIValueVal:
    delete static_cast<NoCFIValue *>(C);
    break;
  case Constant::ConstantPtrAuthVal:
    delete static_cast<ConstantPtrAuth *>(C);
    break;
  case Constant::UndefValueVal:
    delete static_cast<UndefValue *>(C);
    break;
  case Constant::PoisonValueVal:
    delete static_cast<PoisonValue *>(C);
    break;
  case Constant::ConstantExprVal:
    if (isa<CastConstantExpr>(C))
      delete static_cast<CastConstantExpr *>(C);
    else if (isa<BinaryConstantExpr>(C))
      delete static_cast<BinaryConstantExpr *>(C);
    else if (isa<ExtractElementConstantExpr>(C))
      delete static_cast<ExtractElementConstantExpr *>(C);
    else if (isa<InsertElementConstantExpr>(C))
      delete static_cast<InsertElementConstantExpr *>(C);
    else if (isa<ShuffleVectorConstantExpr>(C))
      delete static_cast<ShuffleVectorConstantExpr *>(C);
    else if (isa<GetElementPtrConstantExpr>(C))
      delete static_cast<GetElementPtrConstantExpr *>(C);
    else
      llvm_unreachable("Unexpected constant expr");
    break;
  default:
    llvm_unreachable("Unexpected constant");
  }
}

/// Check if C contains a GlobalValue for which Predicate is true.
static bool
ConstHasGlobalValuePredicate(const Constant *C,
                             bool (*Predicate)(const GlobalValue *)) {
  SmallPtrSet<const Constant *, 8> Visited;
  SmallVector<const Constant *, 8> WorkList;
  WorkList.push_back(C);
  Visited.insert(C);

  while (!WorkList.empty()) {
    const Constant *WorkItem = WorkList.pop_back_val();
    if (const auto *GV = dyn_cast<GlobalValue>(WorkItem))
      if (Predicate(GV))
        return true;
    for (const Value *Op : WorkItem->operands()) {
      const Constant *ConstOp = dyn_cast<Constant>(Op);
      if (!ConstOp)
        continue;
      if (Visited.insert(ConstOp).second)
        WorkList.push_back(ConstOp);
    }
  }
  return false;
}

bool Constant::isThreadDependent() const {
  auto DLLImportPredicate = [](const GlobalValue *GV) {
    return GV->isThreadLocal();
  };
  return ConstHasGlobalValuePredicate(this, DLLImportPredicate);
}

bool Constant::isDLLImportDependent() const {
  auto DLLImportPredicate = [](const GlobalValue *GV) {
    return GV->hasDLLImportStorageClass();
  };
  return ConstHasGlobalValuePredicate(this, DLLImportPredicate);
}

bool Constant::isConstantUsed() const {
  for (const User *U : users()) {
    const Constant *UC = dyn_cast<Constant>(U);
    if (!UC || isa<GlobalValue>(UC))
      return true;

    if (UC->isConstantUsed())
      return true;
  }
  return false;
}

bool Constant::needsDynamicRelocation() const {
  return getRelocationInfo() == GlobalRelocation;
}

bool Constant::needsRelocation() const {
  return getRelocationInfo() != NoRelocation;
}

Constant::PossibleRelocationsTy Constant::getRelocationInfo() const {
  if (isa<GlobalValue>(this))
    return GlobalRelocation; // Global reference.

  if (const BlockAddress *BA = dyn_cast<BlockAddress>(this))
    return BA->getFunction()->getRelocationInfo();

  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(this)) {
    if (CE->getOpcode() == Instruction::Sub) {
      ConstantExpr *LHS = dyn_cast<ConstantExpr>(CE->getOperand(0));
      ConstantExpr *RHS = dyn_cast<ConstantExpr>(CE->getOperand(1));
      if (LHS && RHS && LHS->getOpcode() == Instruction::PtrToInt &&
          RHS->getOpcode() == Instruction::PtrToInt) {
        Constant *LHSOp0 = LHS->getOperand(0);
        Constant *RHSOp0 = RHS->getOperand(0);

        // While raw uses of blockaddress need to be relocated, differences
        // between two of them don't when they are for labels in the same
        // function.  This is a common idiom when creating a table for the
        // indirect goto extension, so we handle it efficiently here.
        if (isa<BlockAddress>(LHSOp0) && isa<BlockAddress>(RHSOp0) &&
            cast<BlockAddress>(LHSOp0)->getFunction() ==
                cast<BlockAddress>(RHSOp0)->getFunction())
          return NoRelocation;

        // Relative pointers do not need to be dynamically relocated.
        if (auto *RHSGV =
                dyn_cast<GlobalValue>(RHSOp0->stripInBoundsConstantOffsets())) {
          auto *LHS = LHSOp0->stripInBoundsConstantOffsets();
          if (auto *LHSGV = dyn_cast<GlobalValue>(LHS)) {
            if (LHSGV->isDSOLocal() && RHSGV->isDSOLocal())
              return LocalRelocation;
          } else if (isa<DSOLocalEquivalent>(LHS)) {
            if (RHSGV->isDSOLocal())
              return LocalRelocation;
          }
        }
      }
    }
  }

  PossibleRelocationsTy Result = NoRelocation;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
    Result =
        std::max(cast<Constant>(getOperand(i))->getRelocationInfo(), Result);

  return Result;
}

/// Return true if the specified constantexpr is dead. This involves
/// recursively traversing users of the constantexpr.
/// If RemoveDeadUsers is true, also remove dead users at the same time.
static bool constantIsDead(const Constant *C, bool RemoveDeadUsers) {
  if (isa<GlobalValue>(C)) return false; // Cannot remove this

  Value::const_user_iterator I = C->user_begin(), E = C->user_end();
  while (I != E) {
    const Constant *User = dyn_cast<Constant>(*I);
    if (!User) return false; // Non-constant usage;
    if (!constantIsDead(User, RemoveDeadUsers))
      return false; // Constant wasn't dead

    // Just removed User, so the iterator was invalidated.
    // Since we return immediately upon finding a live user, we can always
    // restart from user_begin().
    if (RemoveDeadUsers)
      I = C->user_begin();
    else
      ++I;
  }

  if (RemoveDeadUsers) {
    // If C is only used by metadata, it should not be preserved but should
    // have its uses replaced.
    ReplaceableMetadataImpl::SalvageDebugInfo(*C);
    const_cast<Constant *>(C)->destroyConstant();
  }
  
  return true;
}

void Constant::removeDeadConstantUsers() const {
  Value::const_user_iterator I = user_begin(), E = user_end();
  Value::const_user_iterator LastNonDeadUser = E;
  while (I != E) {
    const Constant *User = dyn_cast<Constant>(*I);
    if (!User) {
      LastNonDeadUser = I;
      ++I;
      continue;
    }

    if (!constantIsDead(User, /* RemoveDeadUsers= */ true)) {
      // If the constant wasn't dead, remember that this was the last live use
      // and move on to the next constant.
      LastNonDeadUser = I;
      ++I;
      continue;
    }

    // If the constant was dead, then the iterator is invalidated.
    if (LastNonDeadUser == E)
      I = user_begin();
    else
      I = std::next(LastNonDeadUser);
  }
}

bool Constant::hasOneLiveUse() const { return hasNLiveUses(1); }

bool Constant::hasZeroLiveUses() const { return hasNLiveUses(0); }

bool Constant::hasNLiveUses(unsigned N) const {
  unsigned NumUses = 0;
  for (const Use &U : uses()) {
    const Constant *User = dyn_cast<Constant>(U.getUser());
    if (!User || !constantIsDead(User, /* RemoveDeadUsers= */ false)) {
      ++NumUses;

      if (NumUses > N)
        return false;
    }
  }
  return NumUses == N;
}

Constant *Constant::replaceUndefsWith(Constant *C, Constant *Replacement) {
  assert(C && Replacement && "Expected non-nullptr constant arguments");
  Type *Ty = C->getType();
  if (match(C, m_Undef())) {
    assert(Ty == Replacement->getType() && "Expected matching types");
    return Replacement;
  }

  // Don't know how to deal with this constant.
  auto *VTy = dyn_cast<FixedVectorType>(Ty);
  if (!VTy)
    return C;

  unsigned NumElts = VTy->getNumElements();
  SmallVector<Constant *, 32> NewC(NumElts);
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *EltC = C->getAggregateElement(i);
    assert((!EltC || EltC->getType() == Replacement->getType()) &&
           "Expected matching types");
    NewC[i] = EltC && match(EltC, m_Undef()) ? Replacement : EltC;
  }
  return ConstantVector::get(NewC);
}

Constant *Constant::mergeUndefsWith(Constant *C, Constant *Other) {
  assert(C && Other && "Expected non-nullptr constant arguments");
  if (match(C, m_Undef()))
    return C;

  Type *Ty = C->getType();
  if (match(Other, m_Undef()))
    return UndefValue::get(Ty);

  auto *VTy = dyn_cast<FixedVectorType>(Ty);
  if (!VTy)
    return C;

  Type *EltTy = VTy->getElementType();
  unsigned NumElts = VTy->getNumElements();
  assert(isa<FixedVectorType>(Other->getType()) &&
         cast<FixedVectorType>(Other->getType())->getNumElements() == NumElts &&
         "Type mismatch");

  bool FoundExtraUndef = false;
  SmallVector<Constant *, 32> NewC(NumElts);
  for (unsigned I = 0; I != NumElts; ++I) {
    NewC[I] = C->getAggregateElement(I);
    Constant *OtherEltC = Other->getAggregateElement(I);
    assert(NewC[I] && OtherEltC && "Unknown vector element");
    if (!match(NewC[I], m_Undef()) && match(OtherEltC, m_Undef())) {
      NewC[I] = UndefValue::get(EltTy);
      FoundExtraUndef = true;
    }
  }
  if (FoundExtraUndef)
    return ConstantVector::get(NewC);
  return C;
}

bool Constant::isManifestConstant() const {
  if (isa<ConstantData>(this))
    return true;
  if (isa<ConstantAggregate>(this) || isa<ConstantExpr>(this)) {
    for (const Value *Op : operand_values())
      if (!cast<Constant>(Op)->isManifestConstant())
        return false;
    return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
//                                ConstantInt
//===----------------------------------------------------------------------===//

ConstantInt::ConstantInt(Type *Ty, const APInt &V)
    : ConstantData(Ty, ConstantIntVal), Val(V) {
  assert(V.getBitWidth() ==
             cast<IntegerType>(Ty->getScalarType())->getBitWidth() &&
         "Invalid constant for type");
}

ConstantInt *ConstantInt::getTrue(LLVMContext &Context) {
  LLVMContextImpl *pImpl = Context.pImpl;
  if (!pImpl->TheTrueVal)
    pImpl->TheTrueVal = ConstantInt::get(Type::getInt1Ty(Context), 1);
  return pImpl->TheTrueVal;
}

ConstantInt *ConstantInt::getFalse(LLVMContext &Context) {
  LLVMContextImpl *pImpl = Context.pImpl;
  if (!pImpl->TheFalseVal)
    pImpl->TheFalseVal = ConstantInt::get(Type::getInt1Ty(Context), 0);
  return pImpl->TheFalseVal;
}

ConstantInt *ConstantInt::getBool(LLVMContext &Context, bool V) {
  return V ? getTrue(Context) : getFalse(Context);
}

Constant *ConstantInt::getTrue(Type *Ty) {
  assert(Ty->isIntOrIntVectorTy(1) && "Type not i1 or vector of i1.");
  ConstantInt *TrueC = ConstantInt::getTrue(Ty->getContext());
  if (auto *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), TrueC);
  return TrueC;
}

Constant *ConstantInt::getFalse(Type *Ty) {
  assert(Ty->isIntOrIntVectorTy(1) && "Type not i1 or vector of i1.");
  ConstantInt *FalseC = ConstantInt::getFalse(Ty->getContext());
  if (auto *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), FalseC);
  return FalseC;
}

Constant *ConstantInt::getBool(Type *Ty, bool V) {
  return V ? getTrue(Ty) : getFalse(Ty);
}

// Get a ConstantInt from an APInt.
ConstantInt *ConstantInt::get(LLVMContext &Context, const APInt &V) {
  // get an existing value or the insertion position
  LLVMContextImpl *pImpl = Context.pImpl;
  std::unique_ptr<ConstantInt> &Slot =
      V.isZero()  ? pImpl->IntZeroConstants[V.getBitWidth()]
      : V.isOne() ? pImpl->IntOneConstants[V.getBitWidth()]
                  : pImpl->IntConstants[V];
  if (!Slot) {
    // Get the corresponding integer type for the bit width of the value.
    IntegerType *ITy = IntegerType::get(Context, V.getBitWidth());
    Slot.reset(new ConstantInt(ITy, V));
  }
  assert(Slot->getType() == IntegerType::get(Context, V.getBitWidth()));
  return Slot.get();
}

// Get a ConstantInt vector with each lane set to the same APInt.
ConstantInt *ConstantInt::get(LLVMContext &Context, ElementCount EC,
                              const APInt &V) {
  // Get an existing value or the insertion position.
  std::unique_ptr<ConstantInt> &Slot =
      Context.pImpl->IntSplatConstants[std::make_pair(EC, V)];
  if (!Slot) {
    IntegerType *ITy = IntegerType::get(Context, V.getBitWidth());
    VectorType *VTy = VectorType::get(ITy, EC);
    Slot.reset(new ConstantInt(VTy, V));
  }

#ifndef NDEBUG
  IntegerType *ITy = IntegerType::get(Context, V.getBitWidth());
  VectorType *VTy = VectorType::get(ITy, EC);
  assert(Slot->getType() == VTy);
#endif
  return Slot.get();
}

Constant *ConstantInt::get(Type *Ty, uint64_t V, bool isSigned) {
  Constant *C = get(cast<IntegerType>(Ty->getScalarType()), V, isSigned);

  // For vectors, broadcast the value.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

ConstantInt *ConstantInt::get(IntegerType *Ty, uint64_t V, bool isSigned) {
  return get(Ty->getContext(), APInt(Ty->getBitWidth(), V, isSigned));
}

Constant *ConstantInt::get(Type *Ty, const APInt& V) {
  ConstantInt *C = get(Ty->getContext(), V);
  assert(C->getType() == Ty->getScalarType() &&
         "ConstantInt type doesn't match the type implied by its value!");

  // For vectors, broadcast the value.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

ConstantInt *ConstantInt::get(IntegerType* Ty, StringRef Str, uint8_t radix) {
  return get(Ty->getContext(), APInt(Ty->getBitWidth(), Str, radix));
}

/// Remove the constant from the constant table.
void ConstantInt::destroyConstantImpl() {
  llvm_unreachable("You can't ConstantInt->destroyConstantImpl()!");
}

//===----------------------------------------------------------------------===//
//                                ConstantFP
//===----------------------------------------------------------------------===//

Constant *ConstantFP::get(Type *Ty, double V) {
  LLVMContext &Context = Ty->getContext();

  APFloat FV(V);
  bool ignored;
  FV.convert(Ty->getScalarType()->getFltSemantics(),
             APFloat::rmNearestTiesToEven, &ignored);
  Constant *C = get(Context, FV);

  // For vectors, broadcast the value.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

Constant *ConstantFP::get(Type *Ty, const APFloat &V) {
  ConstantFP *C = get(Ty->getContext(), V);
  assert(C->getType() == Ty->getScalarType() &&
         "ConstantFP type doesn't match the type implied by its value!");

  // For vectors, broadcast the value.
  if (auto *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

Constant *ConstantFP::get(Type *Ty, StringRef Str) {
  LLVMContext &Context = Ty->getContext();

  APFloat FV(Ty->getScalarType()->getFltSemantics(), Str);
  Constant *C = get(Context, FV);

  // For vectors, broadcast the value.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

Constant *ConstantFP::getNaN(Type *Ty, bool Negative, uint64_t Payload) {
  const fltSemantics &Semantics = Ty->getScalarType()->getFltSemantics();
  APFloat NaN = APFloat::getNaN(Semantics, Negative, Payload);
  Constant *C = get(Ty->getContext(), NaN);

  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

Constant *ConstantFP::getQNaN(Type *Ty, bool Negative, APInt *Payload) {
  const fltSemantics &Semantics = Ty->getScalarType()->getFltSemantics();
  APFloat NaN = APFloat::getQNaN(Semantics, Negative, Payload);
  Constant *C = get(Ty->getContext(), NaN);

  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

Constant *ConstantFP::getSNaN(Type *Ty, bool Negative, APInt *Payload) {
  const fltSemantics &Semantics = Ty->getScalarType()->getFltSemantics();
  APFloat NaN = APFloat::getSNaN(Semantics, Negative, Payload);
  Constant *C = get(Ty->getContext(), NaN);

  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

Constant *ConstantFP::getZero(Type *Ty, bool Negative) {
  const fltSemantics &Semantics = Ty->getScalarType()->getFltSemantics();
  APFloat NegZero = APFloat::getZero(Semantics, Negative);
  Constant *C = get(Ty->getContext(), NegZero);

  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}


// ConstantFP accessors.
ConstantFP* ConstantFP::get(LLVMContext &Context, const APFloat& V) {
  LLVMContextImpl* pImpl = Context.pImpl;

  std::unique_ptr<ConstantFP> &Slot = pImpl->FPConstants[V];

  if (!Slot) {
    Type *Ty = Type::getFloatingPointTy(Context, V.getSemantics());
    Slot.reset(new ConstantFP(Ty, V));
  }

  return Slot.get();
}

// Get a ConstantFP vector with each lane set to the same APFloat.
ConstantFP *ConstantFP::get(LLVMContext &Context, ElementCount EC,
                            const APFloat &V) {
  // Get an existing value or the insertion position.
  std::unique_ptr<ConstantFP> &Slot =
      Context.pImpl->FPSplatConstants[std::make_pair(EC, V)];
  if (!Slot) {
    Type *EltTy = Type::getFloatingPointTy(Context, V.getSemantics());
    VectorType *VTy = VectorType::get(EltTy, EC);
    Slot.reset(new ConstantFP(VTy, V));
  }

#ifndef NDEBUG
  Type *EltTy = Type::getFloatingPointTy(Context, V.getSemantics());
  VectorType *VTy = VectorType::get(EltTy, EC);
  assert(Slot->getType() == VTy);
#endif
  return Slot.get();
}

Constant *ConstantFP::getInfinity(Type *Ty, bool Negative) {
  const fltSemantics &Semantics = Ty->getScalarType()->getFltSemantics();
  Constant *C = get(Ty->getContext(), APFloat::getInf(Semantics, Negative));

  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getElementCount(), C);

  return C;
}

ConstantFP::ConstantFP(Type *Ty, const APFloat &V)
    : ConstantData(Ty, ConstantFPVal), Val(V) {
  assert(&V.getSemantics() == &Ty->getScalarType()->getFltSemantics() &&
         "FP type Mismatch");
}

bool ConstantFP::isExactlyValue(const APFloat &V) const {
  return Val.bitwiseIsEqual(V);
}

/// Remove the constant from the constant table.
void ConstantFP::destroyConstantImpl() {
  llvm_unreachable("You can't ConstantFP->destroyConstantImpl()!");
}

//===----------------------------------------------------------------------===//
//                   ConstantAggregateZero Implementation
//===----------------------------------------------------------------------===//

Constant *ConstantAggregateZero::getSequentialElement() const {
  if (auto *AT = dyn_cast<ArrayType>(getType()))
    return Constant::getNullValue(AT->getElementType());
  return Constant::getNullValue(cast<VectorType>(getType())->getElementType());
}

Constant *ConstantAggregateZero::getStructElement(unsigned Elt) const {
  return Constant::getNullValue(getType()->getStructElementType(Elt));
}

Constant *ConstantAggregateZero::getElementValue(Constant *C) const {
  if (isa<ArrayType>(getType()) || isa<VectorType>(getType()))
    return getSequentialElement();
  return getStructElement(cast<ConstantInt>(C)->getZExtValue());
}

Constant *ConstantAggregateZero::getElementValue(unsigned Idx) const {
  if (isa<ArrayType>(getType()) || isa<VectorType>(getType()))
    return getSequentialElement();
  return getStructElement(Idx);
}

ElementCount ConstantAggregateZero::getElementCount() const {
  Type *Ty = getType();
  if (auto *AT = dyn_cast<ArrayType>(Ty))
    return ElementCount::getFixed(AT->getNumElements());
  if (auto *VT = dyn_cast<VectorType>(Ty))
    return VT->getElementCount();
  return ElementCount::getFixed(Ty->getStructNumElements());
}

//===----------------------------------------------------------------------===//
//                         UndefValue Implementation
//===----------------------------------------------------------------------===//

UndefValue *UndefValue::getSequentialElement() const {
  if (ArrayType *ATy = dyn_cast<ArrayType>(getType()))
    return UndefValue::get(ATy->getElementType());
  return UndefValue::get(cast<VectorType>(getType())->getElementType());
}

UndefValue *UndefValue::getStructElement(unsigned Elt) const {
  return UndefValue::get(getType()->getStructElementType(Elt));
}

UndefValue *UndefValue::getElementValue(Constant *C) const {
  if (isa<ArrayType>(getType()) || isa<VectorType>(getType()))
    return getSequentialElement();
  return getStructElement(cast<ConstantInt>(C)->getZExtValue());
}

UndefValue *UndefValue::getElementValue(unsigned Idx) const {
  if (isa<ArrayType>(getType()) || isa<VectorType>(getType()))
    return getSequentialElement();
  return getStructElement(Idx);
}

unsigned UndefValue::getNumElements() const {
  Type *Ty = getType();
  if (auto *AT = dyn_cast<ArrayType>(Ty))
    return AT->getNumElements();
  if (auto *VT = dyn_cast<VectorType>(Ty))
    return cast<FixedVectorType>(VT)->getNumElements();
  return Ty->getStructNumElements();
}

//===----------------------------------------------------------------------===//
//                         PoisonValue Implementation
//===----------------------------------------------------------------------===//

PoisonValue *PoisonValue::getSequentialElement() const {
  if (ArrayType *ATy = dyn_cast<ArrayType>(getType()))
    return PoisonValue::get(ATy->getElementType());
  return PoisonValue::get(cast<VectorType>(getType())->getElementType());
}

PoisonValue *PoisonValue::getStructElement(unsigned Elt) const {
  return PoisonValue::get(getType()->getStructElementType(Elt));
}

PoisonValue *PoisonValue::getElementValue(Constant *C) const {
  if (isa<ArrayType>(getType()) || isa<VectorType>(getType()))
    return getSequentialElement();
  return getStructElement(cast<ConstantInt>(C)->getZExtValue());
}

PoisonValue *PoisonValue::getElementValue(unsigned Idx) const {
  if (isa<ArrayType>(getType()) || isa<VectorType>(getType()))
    return getSequentialElement();
  return getStructElement(Idx);
}

//===----------------------------------------------------------------------===//
//                            ConstantXXX Classes
//===----------------------------------------------------------------------===//

template <typename ItTy, typename EltTy>
static bool rangeOnlyContains(ItTy Start, ItTy End, EltTy Elt) {
  for (; Start != End; ++Start)
    if (*Start != Elt)
      return false;
  return true;
}

template <typename SequentialTy, typename ElementTy>
static Constant *getIntSequenceIfElementsMatch(ArrayRef<Constant *> V) {
  assert(!V.empty() && "Cannot get empty int sequence.");

  SmallVector<ElementTy, 16> Elts;
  for (Constant *C : V)
    if (auto *CI = dyn_cast<ConstantInt>(C))
      Elts.push_back(CI->getZExtValue());
    else
      return nullptr;
  return SequentialTy::get(V[0]->getContext(), Elts);
}

template <typename SequentialTy, typename ElementTy>
static Constant *getFPSequenceIfElementsMatch(ArrayRef<Constant *> V) {
  assert(!V.empty() && "Cannot get empty FP sequence.");

  SmallVector<ElementTy, 16> Elts;
  for (Constant *C : V)
    if (auto *CFP = dyn_cast<ConstantFP>(C))
      Elts.push_back(CFP->getValueAPF().bitcastToAPInt().getLimitedValue());
    else
      return nullptr;
  return SequentialTy::getFP(V[0]->getType(), Elts);
}

template <typename SequenceTy>
static Constant *getSequenceIfElementsMatch(Constant *C,
                                            ArrayRef<Constant *> V) {
  // We speculatively build the elements here even if it turns out that there is
  // a constantexpr or something else weird, since it is so uncommon for that to
  // happen.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(C)) {
    if (CI->getType()->isIntegerTy(8))
      return getIntSequenceIfElementsMatch<SequenceTy, uint8_t>(V);
    else if (CI->getType()->isIntegerTy(16))
      return getIntSequenceIfElementsMatch<SequenceTy, uint16_t>(V);
    else if (CI->getType()->isIntegerTy(32))
      return getIntSequenceIfElementsMatch<SequenceTy, uint32_t>(V);
    else if (CI->getType()->isIntegerTy(64))
      return getIntSequenceIfElementsMatch<SequenceTy, uint64_t>(V);
  } else if (ConstantFP *CFP = dyn_cast<ConstantFP>(C)) {
    if (CFP->getType()->isHalfTy() || CFP->getType()->isBFloatTy())
      return getFPSequenceIfElementsMatch<SequenceTy, uint16_t>(V);
    else if (CFP->getType()->isFloatTy())
      return getFPSequenceIfElementsMatch<SequenceTy, uint32_t>(V);
    else if (CFP->getType()->isDoubleTy())
      return getFPSequenceIfElementsMatch<SequenceTy, uint64_t>(V);
  }

  return nullptr;
}

ConstantAggregate::ConstantAggregate(Type *T, ValueTy VT,
                                     ArrayRef<Constant *> V)
    : Constant(T, VT, OperandTraits<ConstantAggregate>::op_end(this) - V.size(),
               V.size()) {
  llvm::copy(V, op_begin());

  // Check that types match, unless this is an opaque struct.
  if (auto *ST = dyn_cast<StructType>(T)) {
    if (ST->isOpaque())
      return;
    for (unsigned I = 0, E = V.size(); I != E; ++I)
      assert(V[I]->getType() == ST->getTypeAtIndex(I) &&
             "Initializer for struct element doesn't match!");
  }
}

ConstantArray::ConstantArray(ArrayType *T, ArrayRef<Constant *> V)
    : ConstantAggregate(T, ConstantArrayVal, V) {
  assert(V.size() == T->getNumElements() &&
         "Invalid initializer for constant array");
}

Constant *ConstantArray::get(ArrayType *Ty, ArrayRef<Constant*> V) {
  if (Constant *C = getImpl(Ty, V))
    return C;
  return Ty->getContext().pImpl->ArrayConstants.getOrCreate(Ty, V);
}

Constant *ConstantArray::getImpl(ArrayType *Ty, ArrayRef<Constant*> V) {
  // Empty arrays are canonicalized to ConstantAggregateZero.
  if (V.empty())
    return ConstantAggregateZero::get(Ty);

  for (Constant *C : V) {
    assert(C->getType() == Ty->getElementType() &&
           "Wrong type in array element initializer");
    (void)C;
  }

  // If this is an all-zero array, return a ConstantAggregateZero object.  If
  // all undef, return an UndefValue, if "all simple", then return a
  // ConstantDataArray.
  Constant *C = V[0];
  if (isa<PoisonValue>(C) && rangeOnlyContains(V.begin(), V.end(), C))
    return PoisonValue::get(Ty);

  if (isa<UndefValue>(C) && rangeOnlyContains(V.begin(), V.end(), C))
    return UndefValue::get(Ty);

  if (C->isNullValue() && rangeOnlyContains(V.begin(), V.end(), C))
    return ConstantAggregateZero::get(Ty);

  // Check to see if all of the elements are ConstantFP or ConstantInt and if
  // the element type is compatible with ConstantDataVector.  If so, use it.
  if (ConstantDataSequential::isElementTypeCompatible(C->getType()))
    return getSequenceIfElementsMatch<ConstantDataArray>(C, V);

  // Otherwise, we really do want to create a ConstantArray.
  return nullptr;
}

StructType *ConstantStruct::getTypeForElements(LLVMContext &Context,
                                               ArrayRef<Constant*> V,
                                               bool Packed) {
  unsigned VecSize = V.size();
  SmallVector<Type*, 16> EltTypes(VecSize);
  for (unsigned i = 0; i != VecSize; ++i)
    EltTypes[i] = V[i]->getType();

  return StructType::get(Context, EltTypes, Packed);
}


StructType *ConstantStruct::getTypeForElements(ArrayRef<Constant*> V,
                                               bool Packed) {
  assert(!V.empty() &&
         "ConstantStruct::getTypeForElements cannot be called on empty list");
  return getTypeForElements(V[0]->getContext(), V, Packed);
}

ConstantStruct::ConstantStruct(StructType *T, ArrayRef<Constant *> V)
    : ConstantAggregate(T, ConstantStructVal, V) {
  assert((T->isOpaque() || V.size() == T->getNumElements()) &&
         "Invalid initializer for constant struct");
}

// ConstantStruct accessors.
Constant *ConstantStruct::get(StructType *ST, ArrayRef<Constant*> V) {
  assert((ST->isOpaque() || ST->getNumElements() == V.size()) &&
         "Incorrect # elements specified to ConstantStruct::get");

  // Create a ConstantAggregateZero value if all elements are zeros.
  bool isZero = true;
  bool isUndef = false;
  bool isPoison = false;

  if (!V.empty()) {
    isUndef = isa<UndefValue>(V[0]);
    isPoison = isa<PoisonValue>(V[0]);
    isZero = V[0]->isNullValue();
    // PoisonValue inherits UndefValue, so its check is not necessary.
    if (isUndef || isZero) {
      for (Constant *C : V) {
        if (!C->isNullValue())
          isZero = false;
        if (!isa<PoisonValue>(C))
          isPoison = false;
        if (isa<PoisonValue>(C) || !isa<UndefValue>(C))
          isUndef = false;
      }
    }
  }
  if (isZero)
    return ConstantAggregateZero::get(ST);
  if (isPoison)
    return PoisonValue::get(ST);
  if (isUndef)
    return UndefValue::get(ST);

  return ST->getContext().pImpl->StructConstants.getOrCreate(ST, V);
}

ConstantVector::ConstantVector(VectorType *T, ArrayRef<Constant *> V)
    : ConstantAggregate(T, ConstantVectorVal, V) {
  assert(V.size() == cast<FixedVectorType>(T)->getNumElements() &&
         "Invalid initializer for constant vector");
}

// ConstantVector accessors.
Constant *ConstantVector::get(ArrayRef<Constant*> V) {
  if (Constant *C = getImpl(V))
    return C;
  auto *Ty = FixedVectorType::get(V.front()->getType(), V.size());
  return Ty->getContext().pImpl->VectorConstants.getOrCreate(Ty, V);
}

Constant *ConstantVector::getImpl(ArrayRef<Constant*> V) {
  assert(!V.empty() && "Vectors can't be empty");
  auto *T = FixedVectorType::get(V.front()->getType(), V.size());

  // If this is an all-undef or all-zero vector, return a
  // ConstantAggregateZero or UndefValue.
  Constant *C = V[0];
  bool isZero = C->isNullValue();
  bool isUndef = isa<UndefValue>(C);
  bool isPoison = isa<PoisonValue>(C);
  bool isSplatFP = UseConstantFPForFixedLengthSplat && isa<ConstantFP>(C);
  bool isSplatInt = UseConstantIntForFixedLengthSplat && isa<ConstantInt>(C);

  if (isZero || isUndef || isSplatFP || isSplatInt) {
    for (unsigned i = 1, e = V.size(); i != e; ++i)
      if (V[i] != C) {
        isZero = isUndef = isPoison = isSplatFP = isSplatInt = false;
        break;
      }
  }

  if (isZero)
    return ConstantAggregateZero::get(T);
  if (isPoison)
    return PoisonValue::get(T);
  if (isUndef)
    return UndefValue::get(T);
  if (isSplatFP)
    return ConstantFP::get(C->getContext(), T->getElementCount(),
                           cast<ConstantFP>(C)->getValue());
  if (isSplatInt)
    return ConstantInt::get(C->getContext(), T->getElementCount(),
                            cast<ConstantInt>(C)->getValue());

  // Check to see if all of the elements are ConstantFP or ConstantInt and if
  // the element type is compatible with ConstantDataVector.  If so, use it.
  if (ConstantDataSequential::isElementTypeCompatible(C->getType()))
    return getSequenceIfElementsMatch<ConstantDataVector>(C, V);

  // Otherwise, the element type isn't compatible with ConstantDataVector, or
  // the operand list contains a ConstantExpr or something else strange.
  return nullptr;
}

Constant *ConstantVector::getSplat(ElementCount EC, Constant *V) {
  if (!EC.isScalable()) {
    // Maintain special handling of zero.
    if (!V->isNullValue()) {
      if (UseConstantIntForFixedLengthSplat && isa<ConstantInt>(V))
        return ConstantInt::get(V->getContext(), EC,
                                cast<ConstantInt>(V)->getValue());
      if (UseConstantFPForFixedLengthSplat && isa<ConstantFP>(V))
        return ConstantFP::get(V->getContext(), EC,
                               cast<ConstantFP>(V)->getValue());
    }

    // If this splat is compatible with ConstantDataVector, use it instead of
    // ConstantVector.
    if ((isa<ConstantFP>(V) || isa<ConstantInt>(V)) &&
        ConstantDataSequential::isElementTypeCompatible(V->getType()))
      return ConstantDataVector::getSplat(EC.getKnownMinValue(), V);

    SmallVector<Constant *, 32> Elts(EC.getKnownMinValue(), V);
    return get(Elts);
  }

  // Maintain special handling of zero.
  if (!V->isNullValue()) {
    if (UseConstantIntForScalableSplat && isa<ConstantInt>(V))
      return ConstantInt::get(V->getContext(), EC,
                              cast<ConstantInt>(V)->getValue());
    if (UseConstantFPForScalableSplat && isa<ConstantFP>(V))
      return ConstantFP::get(V->getContext(), EC,
                             cast<ConstantFP>(V)->getValue());
  }

  Type *VTy = VectorType::get(V->getType(), EC);

  if (V->isNullValue())
    return ConstantAggregateZero::get(VTy);
  else if (isa<UndefValue>(V))
    return UndefValue::get(VTy);

  Type *IdxTy = Type::getInt64Ty(VTy->getContext());

  // Move scalar into vector.
  Constant *PoisonV = PoisonValue::get(VTy);
  V = ConstantExpr::getInsertElement(PoisonV, V, ConstantInt::get(IdxTy, 0));
  // Build shuffle mask to perform the splat.
  SmallVector<int, 8> Zeros(EC.getKnownMinValue(), 0);
  // Splat.
  return ConstantExpr::getShuffleVector(V, PoisonV, Zeros);
}

ConstantTokenNone *ConstantTokenNone::get(LLVMContext &Context) {
  LLVMContextImpl *pImpl = Context.pImpl;
  if (!pImpl->TheNoneToken)
    pImpl->TheNoneToken.reset(new ConstantTokenNone(Context));
  return pImpl->TheNoneToken.get();
}

/// Remove the constant from the constant table.
void ConstantTokenNone::destroyConstantImpl() {
  llvm_unreachable("You can't ConstantTokenNone->destroyConstantImpl()!");
}

// Utility function for determining if a ConstantExpr is a CastOp or not. This
// can't be inline because we don't want to #include Instruction.h into
// Constant.h
bool ConstantExpr::isCast() const { return Instruction::isCast(getOpcode()); }

ArrayRef<int> ConstantExpr::getShuffleMask() const {
  return cast<ShuffleVectorConstantExpr>(this)->ShuffleMask;
}

Constant *ConstantExpr::getShuffleMaskForBitcode() const {
  return cast<ShuffleVectorConstantExpr>(this)->ShuffleMaskForBitcode;
}

Constant *ConstantExpr::getWithOperands(ArrayRef<Constant *> Ops, Type *Ty,
                                        bool OnlyIfReduced, Type *SrcTy) const {
  assert(Ops.size() == getNumOperands() && "Operand count mismatch!");

  // If no operands changed return self.
  if (Ty == getType() && std::equal(Ops.begin(), Ops.end(), op_begin()))
    return const_cast<ConstantExpr*>(this);

  Type *OnlyIfReducedTy = OnlyIfReduced ? Ty : nullptr;
  switch (getOpcode()) {
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
    return ConstantExpr::getCast(getOpcode(), Ops[0], Ty, OnlyIfReduced);
  case Instruction::InsertElement:
    return ConstantExpr::getInsertElement(Ops[0], Ops[1], Ops[2],
                                          OnlyIfReducedTy);
  case Instruction::ExtractElement:
    return ConstantExpr::getExtractElement(Ops[0], Ops[1], OnlyIfReducedTy);
  case Instruction::ShuffleVector:
    return ConstantExpr::getShuffleVector(Ops[0], Ops[1], getShuffleMask(),
                                          OnlyIfReducedTy);
  case Instruction::GetElementPtr: {
    auto *GEPO = cast<GEPOperator>(this);
    assert(SrcTy || (Ops[0]->getType() == getOperand(0)->getType()));
    return ConstantExpr::getGetElementPtr(
        SrcTy ? SrcTy : GEPO->getSourceElementType(), Ops[0], Ops.slice(1),
        GEPO->getNoWrapFlags(), GEPO->getInRange(), OnlyIfReducedTy);
  }
  default:
    assert(getNumOperands() == 2 && "Must be binary operator?");
    return ConstantExpr::get(getOpcode(), Ops[0], Ops[1], SubclassOptionalData,
                             OnlyIfReducedTy);
  }
}


//===----------------------------------------------------------------------===//
//                      isValueValidForType implementations

bool ConstantInt::isValueValidForType(Type *Ty, uint64_t Val) {
  unsigned NumBits = Ty->getIntegerBitWidth(); // assert okay
  if (Ty->isIntegerTy(1))
    return Val == 0 || Val == 1;
  return isUIntN(NumBits, Val);
}

bool ConstantInt::isValueValidForType(Type *Ty, int64_t Val) {
  unsigned NumBits = Ty->getIntegerBitWidth();
  if (Ty->isIntegerTy(1))
    return Val == 0 || Val == 1 || Val == -1;
  return isIntN(NumBits, Val);
}

bool ConstantFP::isValueValidForType(Type *Ty, const APFloat& Val) {
  // convert modifies in place, so make a copy.
  APFloat Val2 = APFloat(Val);
  bool losesInfo;
  switch (Ty->getTypeID()) {
  default:
    return false;         // These can't be represented as floating point!

  // FIXME rounding mode needs to be more flexible
  case Type::HalfTyID: {
    if (&Val2.getSemantics() == &APFloat::IEEEhalf())
      return true;
    Val2.convert(APFloat::IEEEhalf(), APFloat::rmNearestTiesToEven, &losesInfo);
    return !losesInfo;
  }
  case Type::BFloatTyID: {
    if (&Val2.getSemantics() == &APFloat::BFloat())
      return true;
    Val2.convert(APFloat::BFloat(), APFloat::rmNearestTiesToEven, &losesInfo);
    return !losesInfo;
  }
  case Type::FloatTyID: {
    if (&Val2.getSemantics() == &APFloat::IEEEsingle())
      return true;
    Val2.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven, &losesInfo);
    return !losesInfo;
  }
  case Type::DoubleTyID: {
    if (&Val2.getSemantics() == &APFloat::IEEEhalf() ||
        &Val2.getSemantics() == &APFloat::BFloat() ||
        &Val2.getSemantics() == &APFloat::IEEEsingle() ||
        &Val2.getSemantics() == &APFloat::IEEEdouble())
      return true;
    Val2.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &losesInfo);
    return !losesInfo;
  }
  case Type::X86_FP80TyID:
    return &Val2.getSemantics() == &APFloat::IEEEhalf() ||
           &Val2.getSemantics() == &APFloat::BFloat() ||
           &Val2.getSemantics() == &APFloat::IEEEsingle() ||
           &Val2.getSemantics() == &APFloat::IEEEdouble() ||
           &Val2.getSemantics() == &APFloat::x87DoubleExtended();
  case Type::FP128TyID:
    return &Val2.getSemantics() == &APFloat::IEEEhalf() ||
           &Val2.getSemantics() == &APFloat::BFloat() ||
           &Val2.getSemantics() == &APFloat::IEEEsingle() ||
           &Val2.getSemantics() == &APFloat::IEEEdouble() ||
           &Val2.getSemantics() == &APFloat::IEEEquad();
  case Type::PPC_FP128TyID:
    return &Val2.getSemantics() == &APFloat::IEEEhalf() ||
           &Val2.getSemantics() == &APFloat::BFloat() ||
           &Val2.getSemantics() == &APFloat::IEEEsingle() ||
           &Val2.getSemantics() == &APFloat::IEEEdouble() ||
           &Val2.getSemantics() == &APFloat::PPCDoubleDouble();
  }
}


//===----------------------------------------------------------------------===//
//                      Factory Function Implementation

ConstantAggregateZero *ConstantAggregateZero::get(Type *Ty) {
  assert((Ty->isStructTy() || Ty->isArrayTy() || Ty->isVectorTy()) &&
         "Cannot create an aggregate zero of non-aggregate type!");

  std::unique_ptr<ConstantAggregateZero> &Entry =
      Ty->getContext().pImpl->CAZConstants[Ty];
  if (!Entry)
    Entry.reset(new ConstantAggregateZero(Ty));

  return Entry.get();
}

/// Remove the constant from the constant table.
void ConstantAggregateZero::destroyConstantImpl() {
  getContext().pImpl->CAZConstants.erase(getType());
}

/// Remove the constant from the constant table.
void ConstantArray::destroyConstantImpl() {
  getType()->getContext().pImpl->ArrayConstants.remove(this);
}


//---- ConstantStruct::get() implementation...
//

/// Remove the constant from the constant table.
void ConstantStruct::destroyConstantImpl() {
  getType()->getContext().pImpl->StructConstants.remove(this);
}

/// Remove the constant from the constant table.
void ConstantVector::destroyConstantImpl() {
  getType()->getContext().pImpl->VectorConstants.remove(this);
}

Constant *Constant::getSplatValue(bool AllowPoison) const {
  assert(this->getType()->isVectorTy() && "Only valid for vectors!");
  if (isa<ConstantAggregateZero>(this))
    return getNullValue(cast<VectorType>(getType())->getElementType());
  if (const ConstantDataVector *CV = dyn_cast<ConstantDataVector>(this))
    return CV->getSplatValue();
  if (const ConstantVector *CV = dyn_cast<ConstantVector>(this))
    return CV->getSplatValue(AllowPoison);

  // Check if this is a constant expression splat of the form returned by
  // ConstantVector::getSplat()
  const auto *Shuf = dyn_cast<ConstantExpr>(this);
  if (Shuf && Shuf->getOpcode() == Instruction::ShuffleVector &&
      isa<UndefValue>(Shuf->getOperand(1))) {

    const auto *IElt = dyn_cast<ConstantExpr>(Shuf->getOperand(0));
    if (IElt && IElt->getOpcode() == Instruction::InsertElement &&
        isa<UndefValue>(IElt->getOperand(0))) {

      ArrayRef<int> Mask = Shuf->getShuffleMask();
      Constant *SplatVal = IElt->getOperand(1);
      ConstantInt *Index = dyn_cast<ConstantInt>(IElt->getOperand(2));

      if (Index && Index->getValue() == 0 &&
          llvm::all_of(Mask, [](int I) { return I == 0; }))
        return SplatVal;
    }
  }

  return nullptr;
}

Constant *ConstantVector::getSplatValue(bool AllowPoison) const {
  // Check out first element.
  Constant *Elt = getOperand(0);
  // Then make sure all remaining elements point to the same value.
  for (unsigned I = 1, E = getNumOperands(); I < E; ++I) {
    Constant *OpC = getOperand(I);
    if (OpC == Elt)
      continue;

    // Strict mode: any mismatch is not a splat.
    if (!AllowPoison)
      return nullptr;

    // Allow poison mode: ignore poison elements.
    if (isa<PoisonValue>(OpC))
      continue;

    // If we do not have a defined element yet, use the current operand.
    if (isa<PoisonValue>(Elt))
      Elt = OpC;

    if (OpC != Elt)
      return nullptr;
  }
  return Elt;
}

const APInt &Constant::getUniqueInteger() const {
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->getValue();
  // Scalable vectors can use a ConstantExpr to build a splat.
  if (isa<ConstantExpr>(this))
    return cast<ConstantInt>(this->getSplatValue())->getValue();
  // For non-ConstantExpr we use getAggregateElement as a fast path to avoid
  // calling getSplatValue in release builds.
  assert(this->getSplatValue() && "Doesn't contain a unique integer!");
  const Constant *C = this->getAggregateElement(0U);
  assert(C && isa<ConstantInt>(C) && "Not a vector of numbers!");
  return cast<ConstantInt>(C)->getValue();
}

ConstantRange Constant::toConstantRange() const {
  if (auto *CI = dyn_cast<ConstantInt>(this))
    return ConstantRange(CI->getValue());

  unsigned BitWidth = getType()->getScalarSizeInBits();
  if (!getType()->isVectorTy())
    return ConstantRange::getFull(BitWidth);

  if (auto *CI = dyn_cast_or_null<ConstantInt>(
          getSplatValue(/*AllowPoison=*/true)))
    return ConstantRange(CI->getValue());

  if (auto *CDV = dyn_cast<ConstantDataVector>(this)) {
    ConstantRange CR = ConstantRange::getEmpty(BitWidth);
    for (unsigned I = 0, E = CDV->getNumElements(); I < E; ++I)
      CR = CR.unionWith(CDV->getElementAsAPInt(I));
    return CR;
  }

  if (auto *CV = dyn_cast<ConstantVector>(this)) {
    ConstantRange CR = ConstantRange::getEmpty(BitWidth);
    for (unsigned I = 0, E = CV->getNumOperands(); I < E; ++I) {
      Constant *Elem = CV->getOperand(I);
      if (!Elem)
        return ConstantRange::getFull(BitWidth);
      if (isa<PoisonValue>(Elem))
        continue;
      auto *CI = dyn_cast<ConstantInt>(Elem);
      if (!CI)
        return ConstantRange::getFull(BitWidth);
      CR = CR.unionWith(CI->getValue());
    }
    return CR;
  }

  return ConstantRange::getFull(BitWidth);
}

//---- ConstantPointerNull::get() implementation.
//

ConstantPointerNull *ConstantPointerNull::get(PointerType *Ty) {
  std::unique_ptr<ConstantPointerNull> &Entry =
      Ty->getContext().pImpl->CPNConstants[Ty];
  if (!Entry)
    Entry.reset(new ConstantPointerNull(Ty));

  return Entry.get();
}

/// Remove the constant from the constant table.
void ConstantPointerNull::destroyConstantImpl() {
  getContext().pImpl->CPNConstants.erase(getType());
}

//---- ConstantTargetNone::get() implementation.
//

ConstantTargetNone *ConstantTargetNone::get(TargetExtType *Ty) {
  assert(Ty->hasProperty(TargetExtType::HasZeroInit) &&
         "Target extension type not allowed to have a zeroinitializer");
  std::unique_ptr<ConstantTargetNone> &Entry =
      Ty->getContext().pImpl->CTNConstants[Ty];
  if (!Entry)
    Entry.reset(new ConstantTargetNone(Ty));

  return Entry.get();
}

/// Remove the constant from the constant table.
void ConstantTargetNone::destroyConstantImpl() {
  getContext().pImpl->CTNConstants.erase(getType());
}

UndefValue *UndefValue::get(Type *Ty) {
  std::unique_ptr<UndefValue> &Entry = Ty->getContext().pImpl->UVConstants[Ty];
  if (!Entry)
    Entry.reset(new UndefValue(Ty));

  return Entry.get();
}

/// Remove the constant from the constant table.
void UndefValue::destroyConstantImpl() {
  // Free the constant and any dangling references to it.
  if (getValueID() == UndefValueVal) {
    getContext().pImpl->UVConstants.erase(getType());
  } else if (getValueID() == PoisonValueVal) {
    getContext().pImpl->PVConstants.erase(getType());
  }
  llvm_unreachable("Not a undef or a poison!");
}

PoisonValue *PoisonValue::get(Type *Ty) {
  std::unique_ptr<PoisonValue> &Entry = Ty->getContext().pImpl->PVConstants[Ty];
  if (!Entry)
    Entry.reset(new PoisonValue(Ty));

  return Entry.get();
}

/// Remove the constant from the constant table.
void PoisonValue::destroyConstantImpl() {
  // Free the constant and any dangling references to it.
  getContext().pImpl->PVConstants.erase(getType());
}

BlockAddress *BlockAddress::get(BasicBlock *BB) {
  assert(BB->getParent() && "Block must have a parent");
  return get(BB->getParent(), BB);
}

BlockAddress *BlockAddress::get(Function *F, BasicBlock *BB) {
  BlockAddress *&BA =
    F->getContext().pImpl->BlockAddresses[std::make_pair(F, BB)];
  if (!BA)
    BA = new BlockAddress(F, BB);

  assert(BA->getFunction() == F && "Basic block moved between functions");
  return BA;
}

BlockAddress::BlockAddress(Function *F, BasicBlock *BB)
    : Constant(PointerType::get(F->getContext(), F->getAddressSpace()),
               Value::BlockAddressVal, &Op<0>(), 2) {
  setOperand(0, F);
  setOperand(1, BB);
  BB->AdjustBlockAddressRefCount(1);
}

BlockAddress *BlockAddress::lookup(const BasicBlock *BB) {
  if (!BB->hasAddressTaken())
    return nullptr;

  const Function *F = BB->getParent();
  assert(F && "Block must have a parent");
  BlockAddress *BA =
      F->getContext().pImpl->BlockAddresses.lookup(std::make_pair(F, BB));
  assert(BA && "Refcount and block address map disagree!");
  return BA;
}

/// Remove the constant from the constant table.
void BlockAddress::destroyConstantImpl() {
  getFunction()->getType()->getContext().pImpl
    ->BlockAddresses.erase(std::make_pair(getFunction(), getBasicBlock()));
  getBasicBlock()->AdjustBlockAddressRefCount(-1);
}

Value *BlockAddress::handleOperandChangeImpl(Value *From, Value *To) {
  // This could be replacing either the Basic Block or the Function.  In either
  // case, we have to remove the map entry.
  Function *NewF = getFunction();
  BasicBlock *NewBB = getBasicBlock();

  if (From == NewF)
    NewF = cast<Function>(To->stripPointerCasts());
  else {
    assert(From == NewBB && "From does not match any operand");
    NewBB = cast<BasicBlock>(To);
  }

  // See if the 'new' entry already exists, if not, just update this in place
  // and return early.
  BlockAddress *&NewBA =
    getContext().pImpl->BlockAddresses[std::make_pair(NewF, NewBB)];
  if (NewBA)
    return NewBA;

  getBasicBlock()->AdjustBlockAddressRefCount(-1);

  // Remove the old entry, this can't cause the map to rehash (just a
  // tombstone will get added).
  getContext().pImpl->BlockAddresses.erase(std::make_pair(getFunction(),
                                                          getBasicBlock()));
  NewBA = this;
  setOperand(0, NewF);
  setOperand(1, NewBB);
  getBasicBlock()->AdjustBlockAddressRefCount(1);

  // If we just want to keep the existing value, then return null.
  // Callers know that this means we shouldn't delete this value.
  return nullptr;
}

DSOLocalEquivalent *DSOLocalEquivalent::get(GlobalValue *GV) {
  DSOLocalEquivalent *&Equiv = GV->getContext().pImpl->DSOLocalEquivalents[GV];
  if (!Equiv)
    Equiv = new DSOLocalEquivalent(GV);

  assert(Equiv->getGlobalValue() == GV &&
         "DSOLocalFunction does not match the expected global value");
  return Equiv;
}

DSOLocalEquivalent::DSOLocalEquivalent(GlobalValue *GV)
    : Constant(GV->getType(), Value::DSOLocalEquivalentVal, &Op<0>(), 1) {
  setOperand(0, GV);
}

/// Remove the constant from the constant table.
void DSOLocalEquivalent::destroyConstantImpl() {
  const GlobalValue *GV = getGlobalValue();
  GV->getContext().pImpl->DSOLocalEquivalents.erase(GV);
}

Value *DSOLocalEquivalent::handleOperandChangeImpl(Value *From, Value *To) {
  assert(From == getGlobalValue() && "Changing value does not match operand.");
  assert(isa<Constant>(To) && "Can only replace the operands with a constant");

  // The replacement is with another global value.
  if (const auto *ToObj = dyn_cast<GlobalValue>(To)) {
    DSOLocalEquivalent *&NewEquiv =
        getContext().pImpl->DSOLocalEquivalents[ToObj];
    if (NewEquiv)
      return llvm::ConstantExpr::getBitCast(NewEquiv, getType());
  }

  // If the argument is replaced with a null value, just replace this constant
  // with a null value.
  if (cast<Constant>(To)->isNullValue())
    return To;

  // The replacement could be a bitcast or an alias to another function. We can
  // replace it with a bitcast to the dso_local_equivalent of that function.
  auto *Func = cast<Function>(To->stripPointerCastsAndAliases());
  DSOLocalEquivalent *&NewEquiv = getContext().pImpl->DSOLocalEquivalents[Func];
  if (NewEquiv)
    return llvm::ConstantExpr::getBitCast(NewEquiv, getType());

  // Replace this with the new one.
  getContext().pImpl->DSOLocalEquivalents.erase(getGlobalValue());
  NewEquiv = this;
  setOperand(0, Func);

  if (Func->getType() != getType()) {
    // It is ok to mutate the type here because this constant should always
    // reflect the type of the function it's holding.
    mutateType(Func->getType());
  }
  return nullptr;
}

NoCFIValue *NoCFIValue::get(GlobalValue *GV) {
  NoCFIValue *&NC = GV->getContext().pImpl->NoCFIValues[GV];
  if (!NC)
    NC = new NoCFIValue(GV);

  assert(NC->getGlobalValue() == GV &&
         "NoCFIValue does not match the expected global value");
  return NC;
}

NoCFIValue::NoCFIValue(GlobalValue *GV)
    : Constant(GV->getType(), Value::NoCFIValueVal, &Op<0>(), 1) {
  setOperand(0, GV);
}

/// Remove the constant from the constant table.
void NoCFIValue::destroyConstantImpl() {
  const GlobalValue *GV = getGlobalValue();
  GV->getContext().pImpl->NoCFIValues.erase(GV);
}

Value *NoCFIValue::handleOperandChangeImpl(Value *From, Value *To) {
  assert(From == getGlobalValue() && "Changing value does not match operand.");

  GlobalValue *GV = dyn_cast<GlobalValue>(To->stripPointerCasts());
  assert(GV && "Can only replace the operands with a global value");

  NoCFIValue *&NewNC = getContext().pImpl->NoCFIValues[GV];
  if (NewNC)
    return llvm::ConstantExpr::getBitCast(NewNC, getType());

  getContext().pImpl->NoCFIValues.erase(getGlobalValue());
  NewNC = this;
  setOperand(0, GV);

  if (GV->getType() != getType())
    mutateType(GV->getType());

  return nullptr;
}

//---- ConstantPtrAuth::get() implementations.
//

ConstantPtrAuth *ConstantPtrAuth::get(Constant *Ptr, ConstantInt *Key,
                                      ConstantInt *Disc, Constant *AddrDisc) {
  Constant *ArgVec[] = {Ptr, Key, Disc, AddrDisc};
  ConstantPtrAuthKeyType MapKey(ArgVec);
  LLVMContextImpl *pImpl = Ptr->getContext().pImpl;
  return pImpl->ConstantPtrAuths.getOrCreate(Ptr->getType(), MapKey);
}

ConstantPtrAuth *ConstantPtrAuth::getWithSameSchema(Constant *Pointer) const {
  return get(Pointer, getKey(), getDiscriminator(), getAddrDiscriminator());
}

ConstantPtrAuth::ConstantPtrAuth(Constant *Ptr, ConstantInt *Key,
                                 ConstantInt *Disc, Constant *AddrDisc)
    : Constant(Ptr->getType(), Value::ConstantPtrAuthVal, &Op<0>(), 4) {
  assert(Ptr->getType()->isPointerTy());
  assert(Key->getBitWidth() == 32);
  assert(Disc->getBitWidth() == 64);
  assert(AddrDisc->getType()->isPointerTy());
  setOperand(0, Ptr);
  setOperand(1, Key);
  setOperand(2, Disc);
  setOperand(3, AddrDisc);
}

/// Remove the constant from the constant table.
void ConstantPtrAuth::destroyConstantImpl() {
  getType()->getContext().pImpl->ConstantPtrAuths.remove(this);
}

Value *ConstantPtrAuth::handleOperandChangeImpl(Value *From, Value *ToV) {
  assert(isa<Constant>(ToV) && "Cannot make Constant refer to non-constant!");
  Constant *To = cast<Constant>(ToV);

  SmallVector<Constant *, 4> Values;
  Values.reserve(getNumOperands());

  unsigned NumUpdated = 0;

  Use *OperandList = getOperandList();
  unsigned OperandNo = 0;
  for (Use *O = OperandList, *E = OperandList + getNumOperands(); O != E; ++O) {
    Constant *Val = cast<Constant>(O->get());
    if (Val == From) {
      OperandNo = (O - OperandList);
      Val = To;
      ++NumUpdated;
    }
    Values.push_back(Val);
  }

  return getContext().pImpl->ConstantPtrAuths.replaceOperandsInPlace(
      Values, this, From, To, NumUpdated, OperandNo);
}

bool ConstantPtrAuth::isKnownCompatibleWith(const Value *Key,
                                            const Value *Discriminator,
                                            const DataLayout &DL) const {
  // If the keys are different, there's no chance for this to be compatible.
  if (getKey() != Key)
    return false;

  // We can have 3 kinds of discriminators:
  // - simple, integer-only:    `i64 x, ptr null` vs. `i64 x`
  // - address-only:            `i64 0, ptr p` vs. `ptr p`
  // - blended address/integer: `i64 x, ptr p` vs. `@llvm.ptrauth.blend(p, x)`

  // If this constant has a simple discriminator (integer, no address), easy:
  // it's compatible iff the provided full discriminator is also a simple
  // discriminator, identical to our integer discriminator.
  if (!hasAddressDiscriminator())
    return getDiscriminator() == Discriminator;

  // Otherwise, we can isolate address and integer discriminator components.
  const Value *AddrDiscriminator = nullptr;

  // This constant may or may not have an integer discriminator (instead of 0).
  if (!getDiscriminator()->isNullValue()) {
    // If it does, there's an implicit blend.  We need to have a matching blend
    // intrinsic in the provided full discriminator.
    if (!match(Discriminator,
               m_Intrinsic<Intrinsic::ptrauth_blend>(
                   m_Value(AddrDiscriminator), m_Specific(getDiscriminator()))))
      return false;
  } else {
    // Otherwise, interpret the provided full discriminator as address-only.
    AddrDiscriminator = Discriminator;
  }

  // Either way, we can now focus on comparing the address discriminators.

  // Discriminators are i64, so the provided addr disc may be a ptrtoint.
  if (auto *Cast = dyn_cast<PtrToIntOperator>(AddrDiscriminator))
    AddrDiscriminator = Cast->getPointerOperand();

  // Beyond that, we're only interested in compatible pointers.
  if (getAddrDiscriminator()->getType() != AddrDiscriminator->getType())
    return false;

  // These are often the same constant GEP, making them trivially equivalent.
  if (getAddrDiscriminator() == AddrDiscriminator)
    return true;

  // Finally, they may be equivalent base+offset expressions.
  APInt Off1(DL.getIndexTypeSizeInBits(getAddrDiscriminator()->getType()), 0);
  auto *Base1 = getAddrDiscriminator()->stripAndAccumulateConstantOffsets(
      DL, Off1, /*AllowNonInbounds=*/true);

  APInt Off2(DL.getIndexTypeSizeInBits(AddrDiscriminator->getType()), 0);
  auto *Base2 = AddrDiscriminator->stripAndAccumulateConstantOffsets(
      DL, Off2, /*AllowNonInbounds=*/true);

  return Base1 == Base2 && Off1 == Off2;
}

//---- ConstantExpr::get() implementations.
//

/// This is a utility function to handle folding of casts and lookup of the
/// cast in the ExprConstants map. It is used by the various get* methods below.
static Constant *getFoldedCast(Instruction::CastOps opc, Constant *C, Type *Ty,
                               bool OnlyIfReduced = false) {
  assert(Ty->isFirstClassType() && "Cannot cast to an aggregate type!");
  // Fold a few common cases
  if (Constant *FC = ConstantFoldCastInstruction(opc, C, Ty))
    return FC;

  if (OnlyIfReduced)
    return nullptr;

  LLVMContextImpl *pImpl = Ty->getContext().pImpl;

  // Look up the constant in the table first to ensure uniqueness.
  ConstantExprKeyType Key(opc, C);

  return pImpl->ExprConstants.getOrCreate(Ty, Key);
}

Constant *ConstantExpr::getCast(unsigned oc, Constant *C, Type *Ty,
                                bool OnlyIfReduced) {
  Instruction::CastOps opc = Instruction::CastOps(oc);
  assert(Instruction::isCast(opc) && "opcode out of range");
  assert(isSupportedCastOp(opc) &&
         "Cast opcode not supported as constant expression");
  assert(C && Ty && "Null arguments to getCast");
  assert(CastInst::castIsValid(opc, C, Ty) && "Invalid constantexpr cast!");

  switch (opc) {
  default:
    llvm_unreachable("Invalid cast opcode");
  case Instruction::Trunc:
    return getTrunc(C, Ty, OnlyIfReduced);
  case Instruction::PtrToInt:
    return getPtrToInt(C, Ty, OnlyIfReduced);
  case Instruction::IntToPtr:
    return getIntToPtr(C, Ty, OnlyIfReduced);
  case Instruction::BitCast:
    return getBitCast(C, Ty, OnlyIfReduced);
  case Instruction::AddrSpaceCast:
    return getAddrSpaceCast(C, Ty, OnlyIfReduced);
  }
}

Constant *ConstantExpr::getTruncOrBitCast(Constant *C, Type *Ty) {
  if (C->getType()->getScalarSizeInBits() == Ty->getScalarSizeInBits())
    return getBitCast(C, Ty);
  return getTrunc(C, Ty);
}

Constant *ConstantExpr::getPointerCast(Constant *S, Type *Ty) {
  assert(S->getType()->isPtrOrPtrVectorTy() && "Invalid cast");
  assert((Ty->isIntOrIntVectorTy() || Ty->isPtrOrPtrVectorTy()) &&
          "Invalid cast");

  if (Ty->isIntOrIntVectorTy())
    return getPtrToInt(S, Ty);

  unsigned SrcAS = S->getType()->getPointerAddressSpace();
  if (Ty->isPtrOrPtrVectorTy() && SrcAS != Ty->getPointerAddressSpace())
    return getAddrSpaceCast(S, Ty);

  return getBitCast(S, Ty);
}

Constant *ConstantExpr::getPointerBitCastOrAddrSpaceCast(Constant *S,
                                                         Type *Ty) {
  assert(S->getType()->isPtrOrPtrVectorTy() && "Invalid cast");
  assert(Ty->isPtrOrPtrVectorTy() && "Invalid cast");

  if (S->getType()->getPointerAddressSpace() != Ty->getPointerAddressSpace())
    return getAddrSpaceCast(S, Ty);

  return getBitCast(S, Ty);
}

Constant *ConstantExpr::getTrunc(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = isa<VectorType>(C->getType());
  bool toVec = isa<VectorType>(Ty);
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isIntOrIntVectorTy() && "Trunc operand must be integer");
  assert(Ty->isIntOrIntVectorTy() && "Trunc produces only integral");
  assert(C->getType()->getScalarSizeInBits() > Ty->getScalarSizeInBits()&&
         "SrcTy must be larger than DestTy for Trunc!");

  return getFoldedCast(Instruction::Trunc, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getPtrToInt(Constant *C, Type *DstTy,
                                    bool OnlyIfReduced) {
  assert(C->getType()->isPtrOrPtrVectorTy() &&
         "PtrToInt source must be pointer or pointer vector");
  assert(DstTy->isIntOrIntVectorTy() &&
         "PtrToInt destination must be integer or integer vector");
  assert(isa<VectorType>(C->getType()) == isa<VectorType>(DstTy));
  if (isa<VectorType>(C->getType()))
    assert(cast<VectorType>(C->getType())->getElementCount() ==
               cast<VectorType>(DstTy)->getElementCount() &&
           "Invalid cast between a different number of vector elements");
  return getFoldedCast(Instruction::PtrToInt, C, DstTy, OnlyIfReduced);
}

Constant *ConstantExpr::getIntToPtr(Constant *C, Type *DstTy,
                                    bool OnlyIfReduced) {
  assert(C->getType()->isIntOrIntVectorTy() &&
         "IntToPtr source must be integer or integer vector");
  assert(DstTy->isPtrOrPtrVectorTy() &&
         "IntToPtr destination must be a pointer or pointer vector");
  assert(isa<VectorType>(C->getType()) == isa<VectorType>(DstTy));
  if (isa<VectorType>(C->getType()))
    assert(cast<VectorType>(C->getType())->getElementCount() ==
               cast<VectorType>(DstTy)->getElementCount() &&
           "Invalid cast between a different number of vector elements");
  return getFoldedCast(Instruction::IntToPtr, C, DstTy, OnlyIfReduced);
}

Constant *ConstantExpr::getBitCast(Constant *C, Type *DstTy,
                                   bool OnlyIfReduced) {
  assert(CastInst::castIsValid(Instruction::BitCast, C, DstTy) &&
         "Invalid constantexpr bitcast!");

  // It is common to ask for a bitcast of a value to its own type, handle this
  // speedily.
  if (C->getType() == DstTy) return C;

  return getFoldedCast(Instruction::BitCast, C, DstTy, OnlyIfReduced);
}

Constant *ConstantExpr::getAddrSpaceCast(Constant *C, Type *DstTy,
                                         bool OnlyIfReduced) {
  assert(CastInst::castIsValid(Instruction::AddrSpaceCast, C, DstTy) &&
         "Invalid constantexpr addrspacecast!");
  return getFoldedCast(Instruction::AddrSpaceCast, C, DstTy, OnlyIfReduced);
}

Constant *ConstantExpr::get(unsigned Opcode, Constant *C1, Constant *C2,
                            unsigned Flags, Type *OnlyIfReducedTy) {
  // Check the operands for consistency first.
  assert(Instruction::isBinaryOp(Opcode) &&
         "Invalid opcode in binary constant expression");
  assert(isSupportedBinOp(Opcode) &&
         "Binop not supported as constant expression");
  assert(C1->getType() == C2->getType() &&
         "Operand types in binary constant expression should match");

#ifndef NDEBUG
  switch (Opcode) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
    assert(C1->getType()->isIntOrIntVectorTy() &&
           "Tried to create an integer operation on a non-integer type!");
    break;
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    assert(C1->getType()->isIntOrIntVectorTy() &&
           "Tried to create a logical operation on a non-integral type!");
    break;
  default:
    break;
  }
#endif

  if (Constant *FC = ConstantFoldBinaryInstruction(Opcode, C1, C2))
    return FC;

  if (OnlyIfReducedTy == C1->getType())
    return nullptr;

  Constant *ArgVec[] = {C1, C2};
  ConstantExprKeyType Key(Opcode, ArgVec, Flags);

  LLVMContextImpl *pImpl = C1->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(C1->getType(), Key);
}

bool ConstantExpr::isDesirableBinOp(unsigned Opcode) {
  switch (Opcode) {
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::Shl:
    return false;
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::Xor:
    return true;
  default:
    llvm_unreachable("Argument must be binop opcode");
  }
}

bool ConstantExpr::isSupportedBinOp(unsigned Opcode) {
  switch (Opcode) {
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::Shl:
    return false;
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::Xor:
    return true;
  default:
    llvm_unreachable("Argument must be binop opcode");
  }
}

bool ConstantExpr::isDesirableCastOp(unsigned Opcode) {
  switch (Opcode) {
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
    return false;
  case Instruction::Trunc:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
    return true;
  default:
    llvm_unreachable("Argument must be cast opcode");
  }
}

bool ConstantExpr::isSupportedCastOp(unsigned Opcode) {
  switch (Opcode) {
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
    return false;
  case Instruction::Trunc:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
    return true;
  default:
    llvm_unreachable("Argument must be cast opcode");
  }
}

Constant *ConstantExpr::getSizeOf(Type* Ty) {
  // sizeof is implemented as: (i64) gep (Ty*)null, 1
  // Note that a non-inbounds gep is used, as null isn't within any object.
  Constant *GEPIdx = ConstantInt::get(Type::getInt32Ty(Ty->getContext()), 1);
  Constant *GEP = getGetElementPtr(
      Ty, Constant::getNullValue(PointerType::getUnqual(Ty)), GEPIdx);
  return getPtrToInt(GEP,
                     Type::getInt64Ty(Ty->getContext()));
}

Constant *ConstantExpr::getAlignOf(Type* Ty) {
  // alignof is implemented as: (i64) gep ({i1,Ty}*)null, 0, 1
  // Note that a non-inbounds gep is used, as null isn't within any object.
  Type *AligningTy = StructType::get(Type::getInt1Ty(Ty->getContext()), Ty);
  Constant *NullPtr =
      Constant::getNullValue(PointerType::getUnqual(AligningTy->getContext()));
  Constant *Zero = ConstantInt::get(Type::getInt64Ty(Ty->getContext()), 0);
  Constant *One = ConstantInt::get(Type::getInt32Ty(Ty->getContext()), 1);
  Constant *Indices[2] = {Zero, One};
  Constant *GEP = getGetElementPtr(AligningTy, NullPtr, Indices);
  return getPtrToInt(GEP, Type::getInt64Ty(Ty->getContext()));
}

Constant *ConstantExpr::getGetElementPtr(Type *Ty, Constant *C,
                                         ArrayRef<Value *> Idxs,
                                         GEPNoWrapFlags NW,
                                         std::optional<ConstantRange> InRange,
                                         Type *OnlyIfReducedTy) {
  assert(Ty && "Must specify element type");
  assert(isSupportedGetElementPtr(Ty) && "Element type is unsupported!");

  if (Constant *FC = ConstantFoldGetElementPtr(Ty, C, InRange, Idxs))
    return FC; // Fold a few common cases.

  assert(GetElementPtrInst::getIndexedType(Ty, Idxs) && "GEP indices invalid!");
  ;

  // Get the result type of the getelementptr!
  Type *ReqTy = GetElementPtrInst::getGEPReturnType(C, Idxs);
  if (OnlyIfReducedTy == ReqTy)
    return nullptr;

  auto EltCount = ElementCount::getFixed(0);
  if (VectorType *VecTy = dyn_cast<VectorType>(ReqTy))
    EltCount = VecTy->getElementCount();

  // Look up the constant in the table first to ensure uniqueness
  std::vector<Constant*> ArgVec;
  ArgVec.reserve(1 + Idxs.size());
  ArgVec.push_back(C);
  auto GTI = gep_type_begin(Ty, Idxs), GTE = gep_type_end(Ty, Idxs);
  for (; GTI != GTE; ++GTI) {
    auto *Idx = cast<Constant>(GTI.getOperand());
    assert(
        (!isa<VectorType>(Idx->getType()) ||
         cast<VectorType>(Idx->getType())->getElementCount() == EltCount) &&
        "getelementptr index type missmatch");

    if (GTI.isStruct() && Idx->getType()->isVectorTy()) {
      Idx = Idx->getSplatValue();
    } else if (GTI.isSequential() && EltCount.isNonZero() &&
               !Idx->getType()->isVectorTy()) {
      Idx = ConstantVector::getSplat(EltCount, Idx);
    }
    ArgVec.push_back(Idx);
  }

  const ConstantExprKeyType Key(Instruction::GetElementPtr, ArgVec, NW.getRaw(),
                                std::nullopt, Ty, InRange);

  LLVMContextImpl *pImpl = C->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ReqTy, Key);
}

Constant *ConstantExpr::getExtractElement(Constant *Val, Constant *Idx,
                                          Type *OnlyIfReducedTy) {
  assert(Val->getType()->isVectorTy() &&
         "Tried to create extractelement operation on non-vector type!");
  assert(Idx->getType()->isIntegerTy() &&
         "Extractelement index must be an integer type!");

  if (Constant *FC = ConstantFoldExtractElementInstruction(Val, Idx))
    return FC;          // Fold a few common cases.

  Type *ReqTy = cast<VectorType>(Val->getType())->getElementType();
  if (OnlyIfReducedTy == ReqTy)
    return nullptr;

  // Look up the constant in the table first to ensure uniqueness
  Constant *ArgVec[] = { Val, Idx };
  const ConstantExprKeyType Key(Instruction::ExtractElement, ArgVec);

  LLVMContextImpl *pImpl = Val->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ReqTy, Key);
}

Constant *ConstantExpr::getInsertElement(Constant *Val, Constant *Elt,
                                         Constant *Idx, Type *OnlyIfReducedTy) {
  assert(Val->getType()->isVectorTy() &&
         "Tried to create insertelement operation on non-vector type!");
  assert(Elt->getType() == cast<VectorType>(Val->getType())->getElementType() &&
         "Insertelement types must match!");
  assert(Idx->getType()->isIntegerTy() &&
         "Insertelement index must be i32 type!");

  if (Constant *FC = ConstantFoldInsertElementInstruction(Val, Elt, Idx))
    return FC;          // Fold a few common cases.

  if (OnlyIfReducedTy == Val->getType())
    return nullptr;

  // Look up the constant in the table first to ensure uniqueness
  Constant *ArgVec[] = { Val, Elt, Idx };
  const ConstantExprKeyType Key(Instruction::InsertElement, ArgVec);

  LLVMContextImpl *pImpl = Val->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(Val->getType(), Key);
}

Constant *ConstantExpr::getShuffleVector(Constant *V1, Constant *V2,
                                         ArrayRef<int> Mask,
                                         Type *OnlyIfReducedTy) {
  assert(ShuffleVectorInst::isValidOperands(V1, V2, Mask) &&
         "Invalid shuffle vector constant expr operands!");

  if (Constant *FC = ConstantFoldShuffleVectorInstruction(V1, V2, Mask))
    return FC;          // Fold a few common cases.

  unsigned NElts = Mask.size();
  auto V1VTy = cast<VectorType>(V1->getType());
  Type *EltTy = V1VTy->getElementType();
  bool TypeIsScalable = isa<ScalableVectorType>(V1VTy);
  Type *ShufTy = VectorType::get(EltTy, NElts, TypeIsScalable);

  if (OnlyIfReducedTy == ShufTy)
    return nullptr;

  // Look up the constant in the table first to ensure uniqueness
  Constant *ArgVec[] = {V1, V2};
  ConstantExprKeyType Key(Instruction::ShuffleVector, ArgVec, 0, Mask);

  LLVMContextImpl *pImpl = ShufTy->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ShufTy, Key);
}

Constant *ConstantExpr::getNeg(Constant *C, bool HasNSW) {
  assert(C->getType()->isIntOrIntVectorTy() &&
         "Cannot NEG a nonintegral value!");
  return getSub(ConstantInt::get(C->getType(), 0), C, /*HasNUW=*/false, HasNSW);
}

Constant *ConstantExpr::getNot(Constant *C) {
  assert(C->getType()->isIntOrIntVectorTy() &&
         "Cannot NOT a nonintegral value!");
  return get(Instruction::Xor, C, Constant::getAllOnesValue(C->getType()));
}

Constant *ConstantExpr::getAdd(Constant *C1, Constant *C2,
                               bool HasNUW, bool HasNSW) {
  unsigned Flags = (HasNUW ? OverflowingBinaryOperator::NoUnsignedWrap : 0) |
                   (HasNSW ? OverflowingBinaryOperator::NoSignedWrap   : 0);
  return get(Instruction::Add, C1, C2, Flags);
}

Constant *ConstantExpr::getSub(Constant *C1, Constant *C2,
                               bool HasNUW, bool HasNSW) {
  unsigned Flags = (HasNUW ? OverflowingBinaryOperator::NoUnsignedWrap : 0) |
                   (HasNSW ? OverflowingBinaryOperator::NoSignedWrap   : 0);
  return get(Instruction::Sub, C1, C2, Flags);
}

Constant *ConstantExpr::getMul(Constant *C1, Constant *C2,
                               bool HasNUW, bool HasNSW) {
  unsigned Flags = (HasNUW ? OverflowingBinaryOperator::NoUnsignedWrap : 0) |
                   (HasNSW ? OverflowingBinaryOperator::NoSignedWrap   : 0);
  return get(Instruction::Mul, C1, C2, Flags);
}

Constant *ConstantExpr::getXor(Constant *C1, Constant *C2) {
  return get(Instruction::Xor, C1, C2);
}

Constant *ConstantExpr::getExactLogBase2(Constant *C) {
  Type *Ty = C->getType();
  const APInt *IVal;
  if (match(C, m_APInt(IVal)) && IVal->isPowerOf2())
    return ConstantInt::get(Ty, IVal->logBase2());

  // FIXME: We can extract pow of 2 of splat constant for scalable vectors.
  auto *VecTy = dyn_cast<FixedVectorType>(Ty);
  if (!VecTy)
    return nullptr;

  SmallVector<Constant *, 4> Elts;
  for (unsigned I = 0, E = VecTy->getNumElements(); I != E; ++I) {
    Constant *Elt = C->getAggregateElement(I);
    if (!Elt)
      return nullptr;
    // Note that log2(iN undef) is *NOT* iN undef, because log2(iN undef) u< N.
    if (isa<UndefValue>(Elt)) {
      Elts.push_back(Constant::getNullValue(Ty->getScalarType()));
      continue;
    }
    if (!match(Elt, m_APInt(IVal)) || !IVal->isPowerOf2())
      return nullptr;
    Elts.push_back(ConstantInt::get(Ty->getScalarType(), IVal->logBase2()));
  }

  return ConstantVector::get(Elts);
}

Constant *ConstantExpr::getBinOpIdentity(unsigned Opcode, Type *Ty,
                                         bool AllowRHSConstant, bool NSZ) {
  assert(Instruction::isBinaryOp(Opcode) && "Only binops allowed");

  // Commutative opcodes: it does not matter if AllowRHSConstant is set.
  if (Instruction::isCommutative(Opcode)) {
    switch (Opcode) {
      case Instruction::Add: // X + 0 = X
      case Instruction::Or:  // X | 0 = X
      case Instruction::Xor: // X ^ 0 = X
        return Constant::getNullValue(Ty);
      case Instruction::Mul: // X * 1 = X
        return ConstantInt::get(Ty, 1);
      case Instruction::And: // X & -1 = X
        return Constant::getAllOnesValue(Ty);
      case Instruction::FAdd: // X + -0.0 = X
        return ConstantFP::getZero(Ty, !NSZ);
      case Instruction::FMul: // X * 1.0 = X
        return ConstantFP::get(Ty, 1.0);
      default:
        llvm_unreachable("Every commutative binop has an identity constant");
    }
  }

  // Non-commutative opcodes: AllowRHSConstant must be set.
  if (!AllowRHSConstant)
    return nullptr;

  switch (Opcode) {
    case Instruction::Sub:  // X - 0 = X
    case Instruction::Shl:  // X << 0 = X
    case Instruction::LShr: // X >>u 0 = X
    case Instruction::AShr: // X >> 0 = X
    case Instruction::FSub: // X - 0.0 = X
      return Constant::getNullValue(Ty);
    case Instruction::SDiv: // X / 1 = X
    case Instruction::UDiv: // X /u 1 = X
      return ConstantInt::get(Ty, 1);
    case Instruction::FDiv: // X / 1.0 = X
      return ConstantFP::get(Ty, 1.0);
    default:
      return nullptr;
  }
}

Constant *ConstantExpr::getIntrinsicIdentity(Intrinsic::ID ID, Type *Ty) {
  switch (ID) {
  case Intrinsic::umax:
    return Constant::getNullValue(Ty);
  case Intrinsic::umin:
    return Constant::getAllOnesValue(Ty);
  case Intrinsic::smax:
    return Constant::getIntegerValue(
        Ty, APInt::getSignedMinValue(Ty->getIntegerBitWidth()));
  case Intrinsic::smin:
    return Constant::getIntegerValue(
        Ty, APInt::getSignedMaxValue(Ty->getIntegerBitWidth()));
  default:
    return nullptr;
  }
}

Constant *ConstantExpr::getIdentity(Instruction *I, Type *Ty,
                                    bool AllowRHSConstant, bool NSZ) {
  if (I->isBinaryOp())
    return getBinOpIdentity(I->getOpcode(), Ty, AllowRHSConstant, NSZ);
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I))
    return getIntrinsicIdentity(II->getIntrinsicID(), Ty);
  return nullptr;
}

Constant *ConstantExpr::getBinOpAbsorber(unsigned Opcode, Type *Ty) {
  switch (Opcode) {
  default:
    // Doesn't have an absorber.
    return nullptr;

  case Instruction::Or:
    return Constant::getAllOnesValue(Ty);

  case Instruction::And:
  case Instruction::Mul:
    return Constant::getNullValue(Ty);
  }
}

/// Remove the constant from the constant table.
void ConstantExpr::destroyConstantImpl() {
  getType()->getContext().pImpl->ExprConstants.remove(this);
}

const char *ConstantExpr::getOpcodeName() const {
  return Instruction::getOpcodeName(getOpcode());
}

GetElementPtrConstantExpr::GetElementPtrConstantExpr(
    Type *SrcElementTy, Constant *C, ArrayRef<Constant *> IdxList, Type *DestTy,
    std::optional<ConstantRange> InRange)
    : ConstantExpr(DestTy, Instruction::GetElementPtr,
                   OperandTraits<GetElementPtrConstantExpr>::op_end(this) -
                       (IdxList.size() + 1),
                   IdxList.size() + 1),
      SrcElementTy(SrcElementTy),
      ResElementTy(GetElementPtrInst::getIndexedType(SrcElementTy, IdxList)),
      InRange(std::move(InRange)) {
  Op<0>() = C;
  Use *OperandList = getOperandList();
  for (unsigned i = 0, E = IdxList.size(); i != E; ++i)
    OperandList[i+1] = IdxList[i];
}

Type *GetElementPtrConstantExpr::getSourceElementType() const {
  return SrcElementTy;
}

Type *GetElementPtrConstantExpr::getResultElementType() const {
  return ResElementTy;
}

std::optional<ConstantRange> GetElementPtrConstantExpr::getInRange() const {
  return InRange;
}

//===----------------------------------------------------------------------===//
//                       ConstantData* implementations

Type *ConstantDataSequential::getElementType() const {
  if (ArrayType *ATy = dyn_cast<ArrayType>(getType()))
    return ATy->getElementType();
  return cast<VectorType>(getType())->getElementType();
}

StringRef ConstantDataSequential::getRawDataValues() const {
  return StringRef(DataElements, getNumElements()*getElementByteSize());
}

bool ConstantDataSequential::isElementTypeCompatible(Type *Ty) {
  if (Ty->isHalfTy() || Ty->isBFloatTy() || Ty->isFloatTy() || Ty->isDoubleTy())
    return true;
  if (auto *IT = dyn_cast<IntegerType>(Ty)) {
    switch (IT->getBitWidth()) {
    case 8:
    case 16:
    case 32:
    case 64:
      return true;
    default: break;
    }
  }
  return false;
}

unsigned ConstantDataSequential::getNumElements() const {
  if (ArrayType *AT = dyn_cast<ArrayType>(getType()))
    return AT->getNumElements();
  return cast<FixedVectorType>(getType())->getNumElements();
}


uint64_t ConstantDataSequential::getElementByteSize() const {
  return getElementType()->getPrimitiveSizeInBits()/8;
}

/// Return the start of the specified element.
const char *ConstantDataSequential::getElementPointer(unsigned Elt) const {
  assert(Elt < getNumElements() && "Invalid Elt");
  return DataElements+Elt*getElementByteSize();
}


/// Return true if the array is empty or all zeros.
static bool isAllZeros(StringRef Arr) {
  for (char I : Arr)
    if (I != 0)
      return false;
  return true;
}

/// This is the underlying implementation of all of the
/// ConstantDataSequential::get methods.  They all thunk down to here, providing
/// the correct element type.  We take the bytes in as a StringRef because
/// we *want* an underlying "char*" to avoid TBAA type punning violations.
Constant *ConstantDataSequential::getImpl(StringRef Elements, Type *Ty) {
#ifndef NDEBUG
  if (ArrayType *ATy = dyn_cast<ArrayType>(Ty))
    assert(isElementTypeCompatible(ATy->getElementType()));
  else
    assert(isElementTypeCompatible(cast<VectorType>(Ty)->getElementType()));
#endif
  // If the elements are all zero or there are no elements, return a CAZ, which
  // is more dense and canonical.
  if (isAllZeros(Elements))
    return ConstantAggregateZero::get(Ty);

  // Do a lookup to see if we have already formed one of these.
  auto &Slot =
      *Ty->getContext()
           .pImpl->CDSConstants.insert(std::make_pair(Elements, nullptr))
           .first;

  // The bucket can point to a linked list of different CDS's that have the same
  // body but different types.  For example, 0,0,0,1 could be a 4 element array
  // of i8, or a 1-element array of i32.  They'll both end up in the same
  /// StringMap bucket, linked up by their Next pointers.  Walk the list.
  std::unique_ptr<ConstantDataSequential> *Entry = &Slot.second;
  for (; *Entry; Entry = &(*Entry)->Next)
    if ((*Entry)->getType() == Ty)
      return Entry->get();

  // Okay, we didn't get a hit.  Create a node of the right class, link it in,
  // and return it.
  if (isa<ArrayType>(Ty)) {
    // Use reset because std::make_unique can't access the constructor.
    Entry->reset(new ConstantDataArray(Ty, Slot.first().data()));
    return Entry->get();
  }

  assert(isa<VectorType>(Ty));
  // Use reset because std::make_unique can't access the constructor.
  Entry->reset(new ConstantDataVector(Ty, Slot.first().data()));
  return Entry->get();
}

void ConstantDataSequential::destroyConstantImpl() {
  // Remove the constant from the StringMap.
  StringMap<std::unique_ptr<ConstantDataSequential>> &CDSConstants =
      getType()->getContext().pImpl->CDSConstants;

  auto Slot = CDSConstants.find(getRawDataValues());

  assert(Slot != CDSConstants.end() && "CDS not found in uniquing table");

  std::unique_ptr<ConstantDataSequential> *Entry = &Slot->getValue();

  // Remove the entry from the hash table.
  if (!(*Entry)->Next) {
    // If there is only one value in the bucket (common case) it must be this
    // entry, and removing the entry should remove the bucket completely.
    assert(Entry->get() == this && "Hash mismatch in ConstantDataSequential");
    getContext().pImpl->CDSConstants.erase(Slot);
    return;
  }

  // Otherwise, there are multiple entries linked off the bucket, unlink the
  // node we care about but keep the bucket around.
  while (true) {
    std::unique_ptr<ConstantDataSequential> &Node = *Entry;
    assert(Node && "Didn't find entry in its uniquing hash table!");
    // If we found our entry, unlink it from the list and we're done.
    if (Node.get() == this) {
      Node = std::move(Node->Next);
      return;
    }

    Entry = &Node->Next;
  }
}

/// getFP() constructors - Return a constant of array type with a float
/// element type taken from argument `ElementType', and count taken from
/// argument `Elts'.  The amount of bits of the contained type must match the
/// number of bits of the type contained in the passed in ArrayRef.
/// (i.e. half or bfloat for 16bits, float for 32bits, double for 64bits) Note
/// that this can return a ConstantAggregateZero object.
Constant *ConstantDataArray::getFP(Type *ElementType, ArrayRef<uint16_t> Elts) {
  assert((ElementType->isHalfTy() || ElementType->isBFloatTy()) &&
         "Element type is not a 16-bit float type");
  Type *Ty = ArrayType::get(ElementType, Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 2), Ty);
}
Constant *ConstantDataArray::getFP(Type *ElementType, ArrayRef<uint32_t> Elts) {
  assert(ElementType->isFloatTy() && "Element type is not a 32-bit float type");
  Type *Ty = ArrayType::get(ElementType, Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 4), Ty);
}
Constant *ConstantDataArray::getFP(Type *ElementType, ArrayRef<uint64_t> Elts) {
  assert(ElementType->isDoubleTy() &&
         "Element type is not a 64-bit float type");
  Type *Ty = ArrayType::get(ElementType, Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 8), Ty);
}

Constant *ConstantDataArray::getString(LLVMContext &Context,
                                       StringRef Str, bool AddNull) {
  if (!AddNull) {
    const uint8_t *Data = Str.bytes_begin();
    return get(Context, ArrayRef(Data, Str.size()));
  }

  SmallVector<uint8_t, 64> ElementVals;
  ElementVals.append(Str.begin(), Str.end());
  ElementVals.push_back(0);
  return get(Context, ElementVals);
}

/// get() constructors - Return a constant with vector type with an element
/// count and element type matching the ArrayRef passed in.  Note that this
/// can return a ConstantAggregateZero object.
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<uint8_t> Elts){
  auto *Ty = FixedVectorType::get(Type::getInt8Ty(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 1), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<uint16_t> Elts){
  auto *Ty = FixedVectorType::get(Type::getInt16Ty(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 2), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<uint32_t> Elts){
  auto *Ty = FixedVectorType::get(Type::getInt32Ty(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 4), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<uint64_t> Elts){
  auto *Ty = FixedVectorType::get(Type::getInt64Ty(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 8), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<float> Elts) {
  auto *Ty = FixedVectorType::get(Type::getFloatTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 4), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<double> Elts) {
  auto *Ty = FixedVectorType::get(Type::getDoubleTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 8), Ty);
}

/// getFP() constructors - Return a constant of vector type with a float
/// element type taken from argument `ElementType', and count taken from
/// argument `Elts'.  The amount of bits of the contained type must match the
/// number of bits of the type contained in the passed in ArrayRef.
/// (i.e. half or bfloat for 16bits, float for 32bits, double for 64bits) Note
/// that this can return a ConstantAggregateZero object.
Constant *ConstantDataVector::getFP(Type *ElementType,
                                    ArrayRef<uint16_t> Elts) {
  assert((ElementType->isHalfTy() || ElementType->isBFloatTy()) &&
         "Element type is not a 16-bit float type");
  auto *Ty = FixedVectorType::get(ElementType, Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 2), Ty);
}
Constant *ConstantDataVector::getFP(Type *ElementType,
                                    ArrayRef<uint32_t> Elts) {
  assert(ElementType->isFloatTy() && "Element type is not a 32-bit float type");
  auto *Ty = FixedVectorType::get(ElementType, Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 4), Ty);
}
Constant *ConstantDataVector::getFP(Type *ElementType,
                                    ArrayRef<uint64_t> Elts) {
  assert(ElementType->isDoubleTy() &&
         "Element type is not a 64-bit float type");
  auto *Ty = FixedVectorType::get(ElementType, Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 8), Ty);
}

Constant *ConstantDataVector::getSplat(unsigned NumElts, Constant *V) {
  assert(isElementTypeCompatible(V->getType()) &&
         "Element type not compatible with ConstantData");
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getType()->isIntegerTy(8)) {
      SmallVector<uint8_t, 16> Elts(NumElts, CI->getZExtValue());
      return get(V->getContext(), Elts);
    }
    if (CI->getType()->isIntegerTy(16)) {
      SmallVector<uint16_t, 16> Elts(NumElts, CI->getZExtValue());
      return get(V->getContext(), Elts);
    }
    if (CI->getType()->isIntegerTy(32)) {
      SmallVector<uint32_t, 16> Elts(NumElts, CI->getZExtValue());
      return get(V->getContext(), Elts);
    }
    assert(CI->getType()->isIntegerTy(64) && "Unsupported ConstantData type");
    SmallVector<uint64_t, 16> Elts(NumElts, CI->getZExtValue());
    return get(V->getContext(), Elts);
  }

  if (ConstantFP *CFP = dyn_cast<ConstantFP>(V)) {
    if (CFP->getType()->isHalfTy()) {
      SmallVector<uint16_t, 16> Elts(
          NumElts, CFP->getValueAPF().bitcastToAPInt().getLimitedValue());
      return getFP(V->getType(), Elts);
    }
    if (CFP->getType()->isBFloatTy()) {
      SmallVector<uint16_t, 16> Elts(
          NumElts, CFP->getValueAPF().bitcastToAPInt().getLimitedValue());
      return getFP(V->getType(), Elts);
    }
    if (CFP->getType()->isFloatTy()) {
      SmallVector<uint32_t, 16> Elts(
          NumElts, CFP->getValueAPF().bitcastToAPInt().getLimitedValue());
      return getFP(V->getType(), Elts);
    }
    if (CFP->getType()->isDoubleTy()) {
      SmallVector<uint64_t, 16> Elts(
          NumElts, CFP->getValueAPF().bitcastToAPInt().getLimitedValue());
      return getFP(V->getType(), Elts);
    }
  }
  return ConstantVector::getSplat(ElementCount::getFixed(NumElts), V);
}


uint64_t ConstantDataSequential::getElementAsInteger(unsigned Elt) const {
  assert(isa<IntegerType>(getElementType()) &&
         "Accessor can only be used when element is an integer");
  const char *EltPtr = getElementPointer(Elt);

  // The data is stored in host byte order, make sure to cast back to the right
  // type to load with the right endianness.
  switch (getElementType()->getIntegerBitWidth()) {
  default: llvm_unreachable("Invalid bitwidth for CDS");
  case 8:
    return *reinterpret_cast<const uint8_t *>(EltPtr);
  case 16:
    return *reinterpret_cast<const uint16_t *>(EltPtr);
  case 32:
    return *reinterpret_cast<const uint32_t *>(EltPtr);
  case 64:
    return *reinterpret_cast<const uint64_t *>(EltPtr);
  }
}

APInt ConstantDataSequential::getElementAsAPInt(unsigned Elt) const {
  assert(isa<IntegerType>(getElementType()) &&
         "Accessor can only be used when element is an integer");
  const char *EltPtr = getElementPointer(Elt);

  // The data is stored in host byte order, make sure to cast back to the right
  // type to load with the right endianness.
  switch (getElementType()->getIntegerBitWidth()) {
  default: llvm_unreachable("Invalid bitwidth for CDS");
  case 8: {
    auto EltVal = *reinterpret_cast<const uint8_t *>(EltPtr);
    return APInt(8, EltVal);
  }
  case 16: {
    auto EltVal = *reinterpret_cast<const uint16_t *>(EltPtr);
    return APInt(16, EltVal);
  }
  case 32: {
    auto EltVal = *reinterpret_cast<const uint32_t *>(EltPtr);
    return APInt(32, EltVal);
  }
  case 64: {
    auto EltVal = *reinterpret_cast<const uint64_t *>(EltPtr);
    return APInt(64, EltVal);
  }
  }
}

APFloat ConstantDataSequential::getElementAsAPFloat(unsigned Elt) const {
  const char *EltPtr = getElementPointer(Elt);

  switch (getElementType()->getTypeID()) {
  default:
    llvm_unreachable("Accessor can only be used when element is float/double!");
  case Type::HalfTyID: {
    auto EltVal = *reinterpret_cast<const uint16_t *>(EltPtr);
    return APFloat(APFloat::IEEEhalf(), APInt(16, EltVal));
  }
  case Type::BFloatTyID: {
    auto EltVal = *reinterpret_cast<const uint16_t *>(EltPtr);
    return APFloat(APFloat::BFloat(), APInt(16, EltVal));
  }
  case Type::FloatTyID: {
    auto EltVal = *reinterpret_cast<const uint32_t *>(EltPtr);
    return APFloat(APFloat::IEEEsingle(), APInt(32, EltVal));
  }
  case Type::DoubleTyID: {
    auto EltVal = *reinterpret_cast<const uint64_t *>(EltPtr);
    return APFloat(APFloat::IEEEdouble(), APInt(64, EltVal));
  }
  }
}

float ConstantDataSequential::getElementAsFloat(unsigned Elt) const {
  assert(getElementType()->isFloatTy() &&
         "Accessor can only be used when element is a 'float'");
  return *reinterpret_cast<const float *>(getElementPointer(Elt));
}

double ConstantDataSequential::getElementAsDouble(unsigned Elt) const {
  assert(getElementType()->isDoubleTy() &&
         "Accessor can only be used when element is a 'float'");
  return *reinterpret_cast<const double *>(getElementPointer(Elt));
}

Constant *ConstantDataSequential::getElementAsConstant(unsigned Elt) const {
  if (getElementType()->isHalfTy() || getElementType()->isBFloatTy() ||
      getElementType()->isFloatTy() || getElementType()->isDoubleTy())
    return ConstantFP::get(getContext(), getElementAsAPFloat(Elt));

  return ConstantInt::get(getElementType(), getElementAsInteger(Elt));
}

bool ConstantDataSequential::isString(unsigned CharSize) const {
  return isa<ArrayType>(getType()) && getElementType()->isIntegerTy(CharSize);
}

bool ConstantDataSequential::isCString() const {
  if (!isString())
    return false;

  StringRef Str = getAsString();

  // The last value must be nul.
  if (Str.back() != 0) return false;

  // Other elements must be non-nul.
  return !Str.drop_back().contains(0);
}

bool ConstantDataVector::isSplatData() const {
  const char *Base = getRawDataValues().data();

  // Compare elements 1+ to the 0'th element.
  unsigned EltSize = getElementByteSize();
  for (unsigned i = 1, e = getNumElements(); i != e; ++i)
    if (memcmp(Base, Base+i*EltSize, EltSize))
      return false;

  return true;
}

bool ConstantDataVector::isSplat() const {
  if (!IsSplatSet) {
    IsSplatSet = true;
    IsSplat = isSplatData();
  }
  return IsSplat;
}

Constant *ConstantDataVector::getSplatValue() const {
  // If they're all the same, return the 0th one as a representative.
  return isSplat() ? getElementAsConstant(0) : nullptr;
}

//===----------------------------------------------------------------------===//
//                handleOperandChange implementations

/// Update this constant array to change uses of
/// 'From' to be uses of 'To'.  This must update the uniquing data structures
/// etc.
///
/// Note that we intentionally replace all uses of From with To here.  Consider
/// a large array that uses 'From' 1000 times.  By handling this case all here,
/// ConstantArray::handleOperandChange is only invoked once, and that
/// single invocation handles all 1000 uses.  Handling them one at a time would
/// work, but would be really slow because it would have to unique each updated
/// array instance.
///
void Constant::handleOperandChange(Value *From, Value *To) {
  Value *Replacement = nullptr;
  switch (getValueID()) {
  default:
    llvm_unreachable("Not a constant!");
#define HANDLE_CONSTANT(Name)                                                  \
  case Value::Name##Val:                                                       \
    Replacement = cast<Name>(this)->handleOperandChangeImpl(From, To);         \
    break;
#include "llvm/IR/Value.def"
  }

  // If handleOperandChangeImpl returned nullptr, then it handled
  // replacing itself and we don't want to delete or replace anything else here.
  if (!Replacement)
    return;

  // I do need to replace this with an existing value.
  assert(Replacement != this && "I didn't contain From!");

  // Everyone using this now uses the replacement.
  replaceAllUsesWith(Replacement);

  // Delete the old constant!
  destroyConstant();
}

Value *ConstantArray::handleOperandChangeImpl(Value *From, Value *To) {
  assert(isa<Constant>(To) && "Cannot make Constant refer to non-constant!");
  Constant *ToC = cast<Constant>(To);

  SmallVector<Constant*, 8> Values;
  Values.reserve(getNumOperands());  // Build replacement array.

  // Fill values with the modified operands of the constant array.  Also,
  // compute whether this turns into an all-zeros array.
  unsigned NumUpdated = 0;

  // Keep track of whether all the values in the array are "ToC".
  bool AllSame = true;
  Use *OperandList = getOperandList();
  unsigned OperandNo = 0;
  for (Use *O = OperandList, *E = OperandList+getNumOperands(); O != E; ++O) {
    Constant *Val = cast<Constant>(O->get());
    if (Val == From) {
      OperandNo = (O - OperandList);
      Val = ToC;
      ++NumUpdated;
    }
    Values.push_back(Val);
    AllSame &= Val == ToC;
  }

  if (AllSame && ToC->isNullValue())
    return ConstantAggregateZero::get(getType());

  if (AllSame && isa<UndefValue>(ToC))
    return UndefValue::get(getType());

  // Check for any other type of constant-folding.
  if (Constant *C = getImpl(getType(), Values))
    return C;

  // Update to the new value.
  return getContext().pImpl->ArrayConstants.replaceOperandsInPlace(
      Values, this, From, ToC, NumUpdated, OperandNo);
}

Value *ConstantStruct::handleOperandChangeImpl(Value *From, Value *To) {
  assert(isa<Constant>(To) && "Cannot make Constant refer to non-constant!");
  Constant *ToC = cast<Constant>(To);

  Use *OperandList = getOperandList();

  SmallVector<Constant*, 8> Values;
  Values.reserve(getNumOperands());  // Build replacement struct.

  // Fill values with the modified operands of the constant struct.  Also,
  // compute whether this turns into an all-zeros struct.
  unsigned NumUpdated = 0;
  bool AllSame = true;
  unsigned OperandNo = 0;
  for (Use *O = OperandList, *E = OperandList + getNumOperands(); O != E; ++O) {
    Constant *Val = cast<Constant>(O->get());
    if (Val == From) {
      OperandNo = (O - OperandList);
      Val = ToC;
      ++NumUpdated;
    }
    Values.push_back(Val);
    AllSame &= Val == ToC;
  }

  if (AllSame && ToC->isNullValue())
    return ConstantAggregateZero::get(getType());

  if (AllSame && isa<UndefValue>(ToC))
    return UndefValue::get(getType());

  // Update to the new value.
  return getContext().pImpl->StructConstants.replaceOperandsInPlace(
      Values, this, From, ToC, NumUpdated, OperandNo);
}

Value *ConstantVector::handleOperandChangeImpl(Value *From, Value *To) {
  assert(isa<Constant>(To) && "Cannot make Constant refer to non-constant!");
  Constant *ToC = cast<Constant>(To);

  SmallVector<Constant*, 8> Values;
  Values.reserve(getNumOperands());  // Build replacement array...
  unsigned NumUpdated = 0;
  unsigned OperandNo = 0;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    Constant *Val = getOperand(i);
    if (Val == From) {
      OperandNo = i;
      ++NumUpdated;
      Val = ToC;
    }
    Values.push_back(Val);
  }

  if (Constant *C = getImpl(Values))
    return C;

  // Update to the new value.
  return getContext().pImpl->VectorConstants.replaceOperandsInPlace(
      Values, this, From, ToC, NumUpdated, OperandNo);
}

Value *ConstantExpr::handleOperandChangeImpl(Value *From, Value *ToV) {
  assert(isa<Constant>(ToV) && "Cannot make Constant refer to non-constant!");
  Constant *To = cast<Constant>(ToV);

  SmallVector<Constant*, 8> NewOps;
  unsigned NumUpdated = 0;
  unsigned OperandNo = 0;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    Constant *Op = getOperand(i);
    if (Op == From) {
      OperandNo = i;
      ++NumUpdated;
      Op = To;
    }
    NewOps.push_back(Op);
  }
  assert(NumUpdated && "I didn't contain From!");

  if (Constant *C = getWithOperands(NewOps, getType(), true))
    return C;

  // Update to the new value.
  return getContext().pImpl->ExprConstants.replaceOperandsInPlace(
      NewOps, this, From, To, NumUpdated, OperandNo);
}

Instruction *ConstantExpr::getAsInstruction() const {
  SmallVector<Value *, 4> ValueOperands(operands());
  ArrayRef<Value*> Ops(ValueOperands);

  switch (getOpcode()) {
  case Instruction::Trunc:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
    return CastInst::Create((Instruction::CastOps)getOpcode(), Ops[0],
                            getType(), "");
  case Instruction::InsertElement:
    return InsertElementInst::Create(Ops[0], Ops[1], Ops[2], "");
  case Instruction::ExtractElement:
    return ExtractElementInst::Create(Ops[0], Ops[1], "");
  case Instruction::ShuffleVector:
    return new ShuffleVectorInst(Ops[0], Ops[1], getShuffleMask(), "");

  case Instruction::GetElementPtr: {
    const auto *GO = cast<GEPOperator>(this);
    return GetElementPtrInst::Create(GO->getSourceElementType(), Ops[0],
                                     Ops.slice(1), GO->getNoWrapFlags(), "");
  }
  default:
    assert(getNumOperands() == 2 && "Must be binary operator?");
    BinaryOperator *BO = BinaryOperator::Create(
        (Instruction::BinaryOps)getOpcode(), Ops[0], Ops[1], "");
    if (isa<OverflowingBinaryOperator>(BO)) {
      BO->setHasNoUnsignedWrap(SubclassOptionalData &
                               OverflowingBinaryOperator::NoUnsignedWrap);
      BO->setHasNoSignedWrap(SubclassOptionalData &
                             OverflowingBinaryOperator::NoSignedWrap);
    }
    if (isa<PossiblyExactOperator>(BO))
      BO->setIsExact(SubclassOptionalData & PossiblyExactOperator::IsExact);
    return BO;
  }
}
