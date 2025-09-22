//===-- llvm/Operator.h - Operator utility subclass -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines various classes for working with Instructions and
// ConstantExprs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_OPERATOR_H
#define LLVM_IR_OPERATOR_H

#include "llvm/ADT/MapVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/GEPNoWrapFlags.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <cstddef>
#include <optional>

namespace llvm {

/// This is a utility class that provides an abstraction for the common
/// functionality between Instructions and ConstantExprs.
class Operator : public User {
public:
  // The Operator class is intended to be used as a utility, and is never itself
  // instantiated.
  Operator() = delete;
  ~Operator() = delete;

  void *operator new(size_t s) = delete;

  /// Return the opcode for this Instruction or ConstantExpr.
  unsigned getOpcode() const {
    if (const Instruction *I = dyn_cast<Instruction>(this))
      return I->getOpcode();
    return cast<ConstantExpr>(this)->getOpcode();
  }

  /// If V is an Instruction or ConstantExpr, return its opcode.
  /// Otherwise return UserOp1.
  static unsigned getOpcode(const Value *V) {
    if (const Instruction *I = dyn_cast<Instruction>(V))
      return I->getOpcode();
    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(V))
      return CE->getOpcode();
    return Instruction::UserOp1;
  }

  static bool classof(const Instruction *) { return true; }
  static bool classof(const ConstantExpr *) { return true; }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) || isa<ConstantExpr>(V);
  }

  /// Return true if this operator has flags which may cause this operator
  /// to evaluate to poison despite having non-poison inputs.
  bool hasPoisonGeneratingFlags() const;

  /// Return true if this operator has poison-generating flags,
  /// return attributes or metadata. The latter two is only possible for
  /// instructions.
  bool hasPoisonGeneratingAnnotations() const;
};

/// Utility class for integer operators which may exhibit overflow - Add, Sub,
/// Mul, and Shl. It does not include SDiv, despite that operator having the
/// potential for overflow.
class OverflowingBinaryOperator : public Operator {
public:
  enum {
    AnyWrap        = 0,
    NoUnsignedWrap = (1 << 0),
    NoSignedWrap   = (1 << 1)
  };

private:
  friend class Instruction;
  friend class ConstantExpr;

  void setHasNoUnsignedWrap(bool B) {
    SubclassOptionalData =
      (SubclassOptionalData & ~NoUnsignedWrap) | (B * NoUnsignedWrap);
  }
  void setHasNoSignedWrap(bool B) {
    SubclassOptionalData =
      (SubclassOptionalData & ~NoSignedWrap) | (B * NoSignedWrap);
  }

public:
  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Test whether this operation is known to never
  /// undergo unsigned overflow, aka the nuw property.
  bool hasNoUnsignedWrap() const {
    return SubclassOptionalData & NoUnsignedWrap;
  }

  /// Test whether this operation is known to never
  /// undergo signed overflow, aka the nsw property.
  bool hasNoSignedWrap() const {
    return (SubclassOptionalData & NoSignedWrap) != 0;
  }

  /// Returns the no-wrap kind of the operation.
  unsigned getNoWrapKind() const {
    unsigned NoWrapKind = 0;
    if (hasNoUnsignedWrap())
      NoWrapKind |= NoUnsignedWrap;

    if (hasNoSignedWrap())
      NoWrapKind |= NoSignedWrap;

    return NoWrapKind;
  }

  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Add ||
           I->getOpcode() == Instruction::Sub ||
           I->getOpcode() == Instruction::Mul ||
           I->getOpcode() == Instruction::Shl;
  }
  static bool classof(const ConstantExpr *CE) {
    return CE->getOpcode() == Instruction::Add ||
           CE->getOpcode() == Instruction::Sub ||
           CE->getOpcode() == Instruction::Mul ||
           CE->getOpcode() == Instruction::Shl;
  }
  static bool classof(const Value *V) {
    return (isa<Instruction>(V) && classof(cast<Instruction>(V))) ||
           (isa<ConstantExpr>(V) && classof(cast<ConstantExpr>(V)));
  }
};

template <>
struct OperandTraits<OverflowingBinaryOperator>
    : public FixedNumOperandTraits<OverflowingBinaryOperator, 2> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(OverflowingBinaryOperator, Value)

/// A udiv or sdiv instruction, which can be marked as "exact",
/// indicating that no bits are destroyed.
class PossiblyExactOperator : public Operator {
public:
  enum {
    IsExact = (1 << 0)
  };

private:
  friend class Instruction;
  friend class ConstantExpr;

  void setIsExact(bool B) {
    SubclassOptionalData = (SubclassOptionalData & ~IsExact) | (B * IsExact);
  }

public:
  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Test whether this division is known to be exact, with zero remainder.
  bool isExact() const {
    return SubclassOptionalData & IsExact;
  }

  static bool isPossiblyExactOpcode(unsigned OpC) {
    return OpC == Instruction::SDiv ||
           OpC == Instruction::UDiv ||
           OpC == Instruction::AShr ||
           OpC == Instruction::LShr;
  }

  static bool classof(const ConstantExpr *CE) {
    return isPossiblyExactOpcode(CE->getOpcode());
  }
  static bool classof(const Instruction *I) {
    return isPossiblyExactOpcode(I->getOpcode());
  }
  static bool classof(const Value *V) {
    return (isa<Instruction>(V) && classof(cast<Instruction>(V))) ||
           (isa<ConstantExpr>(V) && classof(cast<ConstantExpr>(V)));
  }
};

template <>
struct OperandTraits<PossiblyExactOperator>
    : public FixedNumOperandTraits<PossiblyExactOperator, 2> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(PossiblyExactOperator, Value)

/// Utility class for floating point operations which can have
/// information about relaxed accuracy requirements attached to them.
class FPMathOperator : public Operator {
private:
  friend class Instruction;

  /// 'Fast' means all bits are set.
  void setFast(bool B) {
    setHasAllowReassoc(B);
    setHasNoNaNs(B);
    setHasNoInfs(B);
    setHasNoSignedZeros(B);
    setHasAllowReciprocal(B);
    setHasAllowContract(B);
    setHasApproxFunc(B);
  }

  void setHasAllowReassoc(bool B) {
    SubclassOptionalData =
    (SubclassOptionalData & ~FastMathFlags::AllowReassoc) |
    (B * FastMathFlags::AllowReassoc);
  }

  void setHasNoNaNs(bool B) {
    SubclassOptionalData =
      (SubclassOptionalData & ~FastMathFlags::NoNaNs) |
      (B * FastMathFlags::NoNaNs);
  }

  void setHasNoInfs(bool B) {
    SubclassOptionalData =
      (SubclassOptionalData & ~FastMathFlags::NoInfs) |
      (B * FastMathFlags::NoInfs);
  }

  void setHasNoSignedZeros(bool B) {
    SubclassOptionalData =
      (SubclassOptionalData & ~FastMathFlags::NoSignedZeros) |
      (B * FastMathFlags::NoSignedZeros);
  }

  void setHasAllowReciprocal(bool B) {
    SubclassOptionalData =
      (SubclassOptionalData & ~FastMathFlags::AllowReciprocal) |
      (B * FastMathFlags::AllowReciprocal);
  }

  void setHasAllowContract(bool B) {
    SubclassOptionalData =
        (SubclassOptionalData & ~FastMathFlags::AllowContract) |
        (B * FastMathFlags::AllowContract);
  }

  void setHasApproxFunc(bool B) {
    SubclassOptionalData =
        (SubclassOptionalData & ~FastMathFlags::ApproxFunc) |
        (B * FastMathFlags::ApproxFunc);
  }

  /// Convenience function for setting multiple fast-math flags.
  /// FMF is a mask of the bits to set.
  void setFastMathFlags(FastMathFlags FMF) {
    SubclassOptionalData |= FMF.Flags;
  }

  /// Convenience function for copying all fast-math flags.
  /// All values in FMF are transferred to this operator.
  void copyFastMathFlags(FastMathFlags FMF) {
    SubclassOptionalData = FMF.Flags;
  }

public:
  /// Test if this operation allows all non-strict floating-point transforms.
  bool isFast() const {
    return ((SubclassOptionalData & FastMathFlags::AllowReassoc) != 0 &&
            (SubclassOptionalData & FastMathFlags::NoNaNs) != 0 &&
            (SubclassOptionalData & FastMathFlags::NoInfs) != 0 &&
            (SubclassOptionalData & FastMathFlags::NoSignedZeros) != 0 &&
            (SubclassOptionalData & FastMathFlags::AllowReciprocal) != 0 &&
            (SubclassOptionalData & FastMathFlags::AllowContract) != 0 &&
            (SubclassOptionalData & FastMathFlags::ApproxFunc) != 0);
  }

  /// Test if this operation may be simplified with reassociative transforms.
  bool hasAllowReassoc() const {
    return (SubclassOptionalData & FastMathFlags::AllowReassoc) != 0;
  }

  /// Test if this operation's arguments and results are assumed not-NaN.
  bool hasNoNaNs() const {
    return (SubclassOptionalData & FastMathFlags::NoNaNs) != 0;
  }

  /// Test if this operation's arguments and results are assumed not-infinite.
  bool hasNoInfs() const {
    return (SubclassOptionalData & FastMathFlags::NoInfs) != 0;
  }

  /// Test if this operation can ignore the sign of zero.
  bool hasNoSignedZeros() const {
    return (SubclassOptionalData & FastMathFlags::NoSignedZeros) != 0;
  }

  /// Test if this operation can use reciprocal multiply instead of division.
  bool hasAllowReciprocal() const {
    return (SubclassOptionalData & FastMathFlags::AllowReciprocal) != 0;
  }

  /// Test if this operation can be floating-point contracted (FMA).
  bool hasAllowContract() const {
    return (SubclassOptionalData & FastMathFlags::AllowContract) != 0;
  }

  /// Test if this operation allows approximations of math library functions or
  /// intrinsics.
  bool hasApproxFunc() const {
    return (SubclassOptionalData & FastMathFlags::ApproxFunc) != 0;
  }

  /// Convenience function for getting all the fast-math flags
  FastMathFlags getFastMathFlags() const {
    return FastMathFlags(SubclassOptionalData);
  }

  /// Get the maximum error permitted by this operation in ULPs. An accuracy of
  /// 0.0 means that the operation should be performed with the default
  /// precision.
  float getFPAccuracy() const;

  static bool classof(const Value *V) {
    unsigned Opcode;
    if (auto *I = dyn_cast<Instruction>(V))
      Opcode = I->getOpcode();
    else
      return false;

    switch (Opcode) {
    case Instruction::FNeg:
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
    // FIXME: To clean up and correct the semantics of fast-math-flags, FCmp
    //        should not be treated as a math op, but the other opcodes should.
    //        This would make things consistent with Select/PHI (FP value type
    //        determines whether they are math ops and, therefore, capable of
    //        having fast-math-flags).
    case Instruction::FCmp:
      return true;
    case Instruction::PHI:
    case Instruction::Select:
    case Instruction::Call: {
      Type *Ty = V->getType();
      while (ArrayType *ArrTy = dyn_cast<ArrayType>(Ty))
        Ty = ArrTy->getElementType();
      return Ty->isFPOrFPVectorTy();
    }
    default:
      return false;
    }
  }
};

/// A helper template for defining operators for individual opcodes.
template<typename SuperClass, unsigned Opc>
class ConcreteOperator : public SuperClass {
public:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Opc;
  }
  static bool classof(const ConstantExpr *CE) {
    return CE->getOpcode() == Opc;
  }
  static bool classof(const Value *V) {
    return (isa<Instruction>(V) && classof(cast<Instruction>(V))) ||
           (isa<ConstantExpr>(V) && classof(cast<ConstantExpr>(V)));
  }
};

class AddOperator
  : public ConcreteOperator<OverflowingBinaryOperator, Instruction::Add> {
};
class SubOperator
  : public ConcreteOperator<OverflowingBinaryOperator, Instruction::Sub> {
};
class MulOperator
  : public ConcreteOperator<OverflowingBinaryOperator, Instruction::Mul> {
};
class ShlOperator
  : public ConcreteOperator<OverflowingBinaryOperator, Instruction::Shl> {
};

class AShrOperator
  : public ConcreteOperator<PossiblyExactOperator, Instruction::AShr> {
};
class LShrOperator
  : public ConcreteOperator<PossiblyExactOperator, Instruction::LShr> {
};

class GEPOperator
    : public ConcreteOperator<Operator, Instruction::GetElementPtr> {
public:
  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  GEPNoWrapFlags getNoWrapFlags() const {
    return GEPNoWrapFlags::fromRaw(SubclassOptionalData);
  }

  /// Test whether this is an inbounds GEP, as defined by LangRef.html.
  bool isInBounds() const { return getNoWrapFlags().isInBounds(); }

  bool hasNoUnsignedSignedWrap() const {
    return getNoWrapFlags().hasNoUnsignedSignedWrap();
  }

  bool hasNoUnsignedWrap() const {
    return getNoWrapFlags().hasNoUnsignedWrap();
  }

  /// Returns the offset of the index with an inrange attachment, or
  /// std::nullopt if none.
  std::optional<ConstantRange> getInRange() const;

  inline op_iterator       idx_begin()       { return op_begin()+1; }
  inline const_op_iterator idx_begin() const { return op_begin()+1; }
  inline op_iterator       idx_end()         { return op_end(); }
  inline const_op_iterator idx_end()   const { return op_end(); }

  inline iterator_range<op_iterator> indices() {
    return make_range(idx_begin(), idx_end());
  }

  inline iterator_range<const_op_iterator> indices() const {
    return make_range(idx_begin(), idx_end());
  }

  Value *getPointerOperand() {
    return getOperand(0);
  }
  const Value *getPointerOperand() const {
    return getOperand(0);
  }
  static unsigned getPointerOperandIndex() {
    return 0U;                      // get index for modifying correct operand
  }

  /// Method to return the pointer operand as a PointerType.
  Type *getPointerOperandType() const {
    return getPointerOperand()->getType();
  }

  Type *getSourceElementType() const;
  Type *getResultElementType() const;

  /// Method to return the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }

  unsigned getNumIndices() const {  // Note: always non-negative
    return getNumOperands() - 1;
  }

  bool hasIndices() const {
    return getNumOperands() > 1;
  }

  /// Return true if all of the indices of this GEP are zeros.
  /// If so, the result pointer and the first operand have the same
  /// value, just potentially different types.
  bool hasAllZeroIndices() const {
    for (const_op_iterator I = idx_begin(), E = idx_end(); I != E; ++I) {
      if (ConstantInt *C = dyn_cast<ConstantInt>(I))
        if (C->isZero())
          continue;
      return false;
    }
    return true;
  }

  /// Return true if all of the indices of this GEP are constant integers.
  /// If so, the result pointer and the first operand have
  /// a constant offset between them.
  bool hasAllConstantIndices() const {
    for (const_op_iterator I = idx_begin(), E = idx_end(); I != E; ++I) {
      if (!isa<ConstantInt>(I))
        return false;
    }
    return true;
  }

  unsigned countNonConstantIndices() const {
    return count_if(indices(), [](const Use& use) {
        return !isa<ConstantInt>(*use);
      });
  }

  /// Compute the maximum alignment that this GEP is garranteed to preserve.
  Align getMaxPreservedAlignment(const DataLayout &DL) const;

  /// Accumulate the constant address offset of this GEP if possible.
  ///
  /// This routine accepts an APInt into which it will try to accumulate the
  /// constant offset of this GEP.
  ///
  /// If \p ExternalAnalysis is provided it will be used to calculate a offset
  /// when a operand of GEP is not constant.
  /// For example, for a value \p ExternalAnalysis might try to calculate a
  /// lower bound. If \p ExternalAnalysis is successful, it should return true.
  ///
  /// If the \p ExternalAnalysis returns false or the value returned by \p
  /// ExternalAnalysis results in a overflow/underflow, this routine returns
  /// false and the value of the offset APInt is undefined (it is *not*
  /// preserved!).
  ///
  /// The APInt passed into this routine must be at exactly as wide as the
  /// IntPtr type for the address space of the base GEP pointer.
  bool accumulateConstantOffset(
      const DataLayout &DL, APInt &Offset,
      function_ref<bool(Value &, APInt &)> ExternalAnalysis = nullptr) const;

  static bool accumulateConstantOffset(
      Type *SourceType, ArrayRef<const Value *> Index, const DataLayout &DL,
      APInt &Offset,
      function_ref<bool(Value &, APInt &)> ExternalAnalysis = nullptr);

  /// Collect the offset of this GEP as a map of Values to their associated
  /// APInt multipliers, as well as a total Constant Offset.
  bool collectOffset(const DataLayout &DL, unsigned BitWidth,
                     MapVector<Value *, APInt> &VariableOffsets,
                     APInt &ConstantOffset) const;
};

template <>
struct OperandTraits<GEPOperator>
    : public VariadicOperandTraits<GEPOperator, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GEPOperator, Value)

class PtrToIntOperator
    : public ConcreteOperator<Operator, Instruction::PtrToInt> {
  friend class PtrToInt;
  friend class ConstantExpr;

public:
  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  Value *getPointerOperand() {
    return getOperand(0);
  }
  const Value *getPointerOperand() const {
    return getOperand(0);
  }

  static unsigned getPointerOperandIndex() {
    return 0U;                      // get index for modifying correct operand
  }

  /// Method to return the pointer operand as a PointerType.
  Type *getPointerOperandType() const {
    return getPointerOperand()->getType();
  }

  /// Method to return the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return cast<PointerType>(getPointerOperandType())->getAddressSpace();
  }
};

template <>
struct OperandTraits<PtrToIntOperator>
    : public FixedNumOperandTraits<PtrToIntOperator, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(PtrToIntOperator, Value)

class BitCastOperator
    : public ConcreteOperator<Operator, Instruction::BitCast> {
  friend class BitCastInst;
  friend class ConstantExpr;

public:
  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  Type *getSrcTy() const {
    return getOperand(0)->getType();
  }

  Type *getDestTy() const {
    return getType();
  }
};

template <>
struct OperandTraits<BitCastOperator>
    : public FixedNumOperandTraits<BitCastOperator, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(BitCastOperator, Value)

class AddrSpaceCastOperator
    : public ConcreteOperator<Operator, Instruction::AddrSpaceCast> {
  friend class AddrSpaceCastInst;
  friend class ConstantExpr;

public:
  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  Value *getPointerOperand() { return getOperand(0); }

  const Value *getPointerOperand() const { return getOperand(0); }

  unsigned getSrcAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  unsigned getDestAddressSpace() const {
    return getType()->getPointerAddressSpace();
  }
};

template <>
struct OperandTraits<AddrSpaceCastOperator>
    : public FixedNumOperandTraits<AddrSpaceCastOperator, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(AddrSpaceCastOperator, Value)

} // end namespace llvm

#endif // LLVM_IR_OPERATOR_H
