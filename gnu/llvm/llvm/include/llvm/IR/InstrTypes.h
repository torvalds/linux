//===- llvm/InstrTypes.h - Important Instruction subclasses -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines various meta classes of instructions that exist in the VM
// representation.  Specific concrete subclasses of these may be found in the
// i*.h files...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INSTRTYPES_H
#define LLVM_IR_INSTRTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/User.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class StringRef;
class Type;
class Value;
class ConstantRange;

namespace Intrinsic {
typedef unsigned ID;
}

//===----------------------------------------------------------------------===//
//                          UnaryInstruction Class
//===----------------------------------------------------------------------===//

class UnaryInstruction : public Instruction {
protected:
  UnaryInstruction(Type *Ty, unsigned iType, Value *V, BasicBlock::iterator IB)
      : Instruction(Ty, iType, &Op<0>(), 1, IB) {
    Op<0>() = V;
  }
  UnaryInstruction(Type *Ty, unsigned iType, Value *V,
                   Instruction *IB = nullptr)
    : Instruction(Ty, iType, &Op<0>(), 1, IB) {
    Op<0>() = V;
  }
  UnaryInstruction(Type *Ty, unsigned iType, Value *V, BasicBlock *IAE)
    : Instruction(Ty, iType, &Op<0>(), 1, IAE) {
    Op<0>() = V;
  }

public:
  // allocate space for exactly one operand
  void *operator new(size_t S) { return User::operator new(S, 1); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->isUnaryOp() ||
           I->getOpcode() == Instruction::Alloca ||
           I->getOpcode() == Instruction::Load ||
           I->getOpcode() == Instruction::VAArg ||
           I->getOpcode() == Instruction::ExtractValue ||
           (I->getOpcode() >= CastOpsBegin && I->getOpcode() < CastOpsEnd);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<UnaryInstruction> :
  public FixedNumOperandTraits<UnaryInstruction, 1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(UnaryInstruction, Value)

//===----------------------------------------------------------------------===//
//                                UnaryOperator Class
//===----------------------------------------------------------------------===//

class UnaryOperator : public UnaryInstruction {
  void AssertOK();

protected:
  UnaryOperator(UnaryOps iType, Value *S, Type *Ty, const Twine &Name,
                InsertPosition InsertBefore);

  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  UnaryOperator *cloneImpl() const;

public:
  /// Construct a unary instruction, given the opcode and an operand.
  /// Optionally (if InstBefore is specified) insert the instruction
  /// into a BasicBlock right before the specified instruction.  The specified
  /// Instruction is allowed to be a dereferenced end iterator.
  ///
  static UnaryOperator *Create(UnaryOps Op, Value *S,
                               const Twine &Name = Twine(),
                               InsertPosition InsertBefore = nullptr);

  /// These methods just forward to Create, and are useful when you
  /// statically know what type of instruction you're going to create.  These
  /// helpers just save some typing.
#define HANDLE_UNARY_INST(N, OPC, CLASS) \
  static UnaryOperator *Create##OPC(Value *V, const Twine &Name = "") {\
    return Create(Instruction::OPC, V, Name);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_UNARY_INST(N, OPC, CLASS) \
  static UnaryOperator *Create##OPC(Value *V, const Twine &Name, \
                                    BasicBlock *BB) {\
    return Create(Instruction::OPC, V, Name, BB);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_UNARY_INST(N, OPC, CLASS) \
  static UnaryOperator *Create##OPC(Value *V, const Twine &Name, \
                                    Instruction *I) {\
    return Create(Instruction::OPC, V, Name, I);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_UNARY_INST(N, OPC, CLASS) \
  static UnaryOperator *Create##OPC(Value *V, const Twine &Name, \
                                    BasicBlock::iterator It) {\
    return Create(Instruction::OPC, V, Name, It);\
  }
#include "llvm/IR/Instruction.def"

  static UnaryOperator *
  CreateWithCopiedFlags(UnaryOps Opc, Value *V, Instruction *CopyO,
                        const Twine &Name = "",
                        InsertPosition InsertBefore = nullptr) {
    UnaryOperator *UO = Create(Opc, V, Name, InsertBefore);
    UO->copyIRFlags(CopyO);
    return UO;
  }

  static UnaryOperator *CreateFNegFMF(Value *Op, Instruction *FMFSource,
                                      const Twine &Name = "",
                                      InsertPosition InsertBefore = nullptr) {
    return CreateWithCopiedFlags(Instruction::FNeg, Op, FMFSource, Name,
                                 InsertBefore);
  }

  UnaryOps getOpcode() const {
    return static_cast<UnaryOps>(Instruction::getOpcode());
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->isUnaryOp();
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                           BinaryOperator Class
//===----------------------------------------------------------------------===//

class BinaryOperator : public Instruction {
  void AssertOK();

protected:
  BinaryOperator(BinaryOps iType, Value *S1, Value *S2, Type *Ty,
                 const Twine &Name, InsertPosition InsertBefore);

  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  BinaryOperator *cloneImpl() const;

public:
  // allocate space for exactly two operands
  void *operator new(size_t S) { return User::operator new(S, 2); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Construct a binary instruction, given the opcode and the two
  /// operands.  Optionally (if InstBefore is specified) insert the instruction
  /// into a BasicBlock right before the specified instruction.  The specified
  /// Instruction is allowed to be a dereferenced end iterator.
  ///
  static BinaryOperator *Create(BinaryOps Op, Value *S1, Value *S2,
                                const Twine &Name = Twine(),
                                InsertPosition InsertBefore = nullptr);

  /// These methods just forward to Create, and are useful when you
  /// statically know what type of instruction you're going to create.  These
  /// helpers just save some typing.
#define HANDLE_BINARY_INST(N, OPC, CLASS) \
  static BinaryOperator *Create##OPC(Value *V1, Value *V2, \
                                     const Twine &Name = "") {\
    return Create(Instruction::OPC, V1, V2, Name);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_BINARY_INST(N, OPC, CLASS) \
  static BinaryOperator *Create##OPC(Value *V1, Value *V2, \
                                     const Twine &Name, BasicBlock *BB) {\
    return Create(Instruction::OPC, V1, V2, Name, BB);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_BINARY_INST(N, OPC, CLASS) \
  static BinaryOperator *Create##OPC(Value *V1, Value *V2, \
                                     const Twine &Name, Instruction *I) {\
    return Create(Instruction::OPC, V1, V2, Name, I);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_BINARY_INST(N, OPC, CLASS) \
  static BinaryOperator *Create##OPC(Value *V1, Value *V2, \
                                     const Twine &Name, BasicBlock::iterator It) {\
    return Create(Instruction::OPC, V1, V2, Name, It);\
  }
#include "llvm/IR/Instruction.def"

  static BinaryOperator *
  CreateWithCopiedFlags(BinaryOps Opc, Value *V1, Value *V2, Value *CopyO,
                        const Twine &Name = "",
                        InsertPosition InsertBefore = nullptr) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, InsertBefore);
    BO->copyIRFlags(CopyO);
    return BO;
  }

  static BinaryOperator *CreateWithFMF(BinaryOps Opc, Value *V1, Value *V2,
                                       FastMathFlags FMF,
                                       const Twine &Name = "",
                                       InsertPosition InsertBefore = nullptr) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, InsertBefore);
    BO->setFastMathFlags(FMF);
    return BO;
  }

  static BinaryOperator *CreateFAddFMF(Value *V1, Value *V2, FastMathFlags FMF,
                                       const Twine &Name = "") {
    return CreateWithFMF(Instruction::FAdd, V1, V2, FMF, Name);
  }
  static BinaryOperator *CreateFSubFMF(Value *V1, Value *V2, FastMathFlags FMF,
                                       const Twine &Name = "") {
    return CreateWithFMF(Instruction::FSub, V1, V2, FMF, Name);
  }
  static BinaryOperator *CreateFMulFMF(Value *V1, Value *V2, FastMathFlags FMF,
                                       const Twine &Name = "") {
    return CreateWithFMF(Instruction::FMul, V1, V2, FMF, Name);
  }
  static BinaryOperator *CreateFDivFMF(Value *V1, Value *V2, FastMathFlags FMF,
                                       const Twine &Name = "") {
    return CreateWithFMF(Instruction::FDiv, V1, V2, FMF, Name);
  }

  static BinaryOperator *CreateFAddFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FAdd, V1, V2, FMFSource, Name);
  }
  static BinaryOperator *CreateFSubFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FSub, V1, V2, FMFSource, Name);
  }
  static BinaryOperator *CreateFMulFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FMul, V1, V2, FMFSource, Name);
  }
  static BinaryOperator *CreateFDivFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FDiv, V1, V2, FMFSource, Name);
  }
  static BinaryOperator *CreateFRemFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FRem, V1, V2, FMFSource, Name);
  }

  static BinaryOperator *CreateNSW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->setHasNoSignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNSW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, BasicBlock *BB) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, BB);
    BO->setHasNoSignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNSW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, Instruction *I) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, I);
    BO->setHasNoSignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNSW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, BasicBlock::iterator It) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, It);
    BO->setHasNoSignedWrap(true);
    return BO;
  }

  static BinaryOperator *CreateNUW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->setHasNoUnsignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNUW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, BasicBlock *BB) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, BB);
    BO->setHasNoUnsignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNUW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, Instruction *I) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, I);
    BO->setHasNoUnsignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNUW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, BasicBlock::iterator It) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, It);
    BO->setHasNoUnsignedWrap(true);
    return BO;
  }

  static BinaryOperator *CreateExact(BinaryOps Opc, Value *V1, Value *V2,
                                     const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->setIsExact(true);
    return BO;
  }
  static BinaryOperator *CreateExact(BinaryOps Opc, Value *V1, Value *V2,
                                     const Twine &Name, BasicBlock *BB) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, BB);
    BO->setIsExact(true);
    return BO;
  }
  static BinaryOperator *CreateExact(BinaryOps Opc, Value *V1, Value *V2,
                                     const Twine &Name, Instruction *I) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, I);
    BO->setIsExact(true);
    return BO;
  }
  static BinaryOperator *CreateExact(BinaryOps Opc, Value *V1, Value *V2,
                                     const Twine &Name,
                                     BasicBlock::iterator It) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, It);
    BO->setIsExact(true);
    return BO;
  }

  static inline BinaryOperator *
  CreateDisjoint(BinaryOps Opc, Value *V1, Value *V2, const Twine &Name = "");
  static inline BinaryOperator *CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name,
                                               BasicBlock *BB);
  static inline BinaryOperator *CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name,
                                               Instruction *I);
  static inline BinaryOperator *CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name,
                                               BasicBlock::iterator It);

#define DEFINE_HELPERS(OPC, NUWNSWEXACT)                                       \
  static BinaryOperator *Create##NUWNSWEXACT##OPC(Value *V1, Value *V2,        \
                                                  const Twine &Name = "") {    \
    return Create##NUWNSWEXACT(Instruction::OPC, V1, V2, Name);                \
  }                                                                            \
  static BinaryOperator *Create##NUWNSWEXACT##OPC(                             \
      Value *V1, Value *V2, const Twine &Name, BasicBlock *BB) {               \
    return Create##NUWNSWEXACT(Instruction::OPC, V1, V2, Name, BB);            \
  }                                                                            \
  static BinaryOperator *Create##NUWNSWEXACT##OPC(                             \
      Value *V1, Value *V2, const Twine &Name, Instruction *I) {               \
    return Create##NUWNSWEXACT(Instruction::OPC, V1, V2, Name, I);             \
  }                                                                            \
  static BinaryOperator *Create##NUWNSWEXACT##OPC(                             \
      Value *V1, Value *V2, const Twine &Name, BasicBlock::iterator It) {      \
    return Create##NUWNSWEXACT(Instruction::OPC, V1, V2, Name, It);            \
  }

  DEFINE_HELPERS(Add, NSW) // CreateNSWAdd
  DEFINE_HELPERS(Add, NUW) // CreateNUWAdd
  DEFINE_HELPERS(Sub, NSW) // CreateNSWSub
  DEFINE_HELPERS(Sub, NUW) // CreateNUWSub
  DEFINE_HELPERS(Mul, NSW) // CreateNSWMul
  DEFINE_HELPERS(Mul, NUW) // CreateNUWMul
  DEFINE_HELPERS(Shl, NSW) // CreateNSWShl
  DEFINE_HELPERS(Shl, NUW) // CreateNUWShl

  DEFINE_HELPERS(SDiv, Exact)  // CreateExactSDiv
  DEFINE_HELPERS(UDiv, Exact)  // CreateExactUDiv
  DEFINE_HELPERS(AShr, Exact)  // CreateExactAShr
  DEFINE_HELPERS(LShr, Exact)  // CreateExactLShr

  DEFINE_HELPERS(Or, Disjoint) // CreateDisjointOr

#undef DEFINE_HELPERS

  /// Helper functions to construct and inspect unary operations (NEG and NOT)
  /// via binary operators SUB and XOR:
  ///
  /// Create the NEG and NOT instructions out of SUB and XOR instructions.
  ///
  static BinaryOperator *CreateNeg(Value *Op, const Twine &Name = "",
                                   InsertPosition InsertBefore = nullptr);
  static BinaryOperator *CreateNSWNeg(Value *Op, const Twine &Name = "",
                                      InsertPosition InsertBefore = nullptr);
  static BinaryOperator *CreateNot(Value *Op, const Twine &Name = "",
                                   InsertPosition InsertBefore = nullptr);

  BinaryOps getOpcode() const {
    return static_cast<BinaryOps>(Instruction::getOpcode());
  }

  /// Exchange the two operands to this instruction.
  /// This instruction is safe to use on any binary instruction and
  /// does not modify the semantics of the instruction.  If the instruction
  /// cannot be reversed (ie, it's a Div), then return true.
  ///
  bool swapOperands();

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->isBinaryOp();
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<BinaryOperator> :
  public FixedNumOperandTraits<BinaryOperator, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(BinaryOperator, Value)

/// An or instruction, which can be marked as "disjoint", indicating that the
/// inputs don't have a 1 in the same bit position. Meaning this instruction
/// can also be treated as an add.
class PossiblyDisjointInst : public BinaryOperator {
public:
  enum { IsDisjoint = (1 << 0) };

  void setIsDisjoint(bool B) {
    SubclassOptionalData =
        (SubclassOptionalData & ~IsDisjoint) | (B * IsDisjoint);
  }

  bool isDisjoint() const { return SubclassOptionalData & IsDisjoint; }

  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Or;
  }

  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

BinaryOperator *BinaryOperator::CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name) {
  BinaryOperator *BO = Create(Opc, V1, V2, Name);
  cast<PossiblyDisjointInst>(BO)->setIsDisjoint(true);
  return BO;
}
BinaryOperator *BinaryOperator::CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name,
                                               BasicBlock *BB) {
  BinaryOperator *BO = Create(Opc, V1, V2, Name, BB);
  cast<PossiblyDisjointInst>(BO)->setIsDisjoint(true);
  return BO;
}
BinaryOperator *BinaryOperator::CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name,
                                               Instruction *I) {
  BinaryOperator *BO = Create(Opc, V1, V2, Name, I);
  cast<PossiblyDisjointInst>(BO)->setIsDisjoint(true);
  return BO;
}
BinaryOperator *BinaryOperator::CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name,
                                               BasicBlock::iterator It) {
  BinaryOperator *BO = Create(Opc, V1, V2, Name, It);
  cast<PossiblyDisjointInst>(BO)->setIsDisjoint(true);
  return BO;
}

//===----------------------------------------------------------------------===//
//                               CastInst Class
//===----------------------------------------------------------------------===//

/// This is the base class for all instructions that perform data
/// casts. It is simply provided so that instruction category testing
/// can be performed with code like:
///
/// if (isa<CastInst>(Instr)) { ... }
/// Base class of casting instructions.
class CastInst : public UnaryInstruction {
protected:
  /// Constructor with insert-before-instruction semantics for subclasses
  CastInst(Type *Ty, unsigned iType, Value *S, const Twine &NameStr = "",
           InsertPosition InsertBefore = nullptr)
      : UnaryInstruction(Ty, iType, S, InsertBefore) {
    setName(NameStr);
  }

public:
  /// Provides a way to construct any of the CastInst subclasses using an
  /// opcode instead of the subclass's constructor. The opcode must be in the
  /// CastOps category (Instruction::isCast(opcode) returns true). This
  /// constructor has insert-before-instruction semantics to automatically
  /// insert the new CastInst before InsertBefore (if it is non-null).
  /// Construct any of the CastInst subclasses
  static CastInst *Create(
      Instruction::CastOps,   ///< The opcode of the cast instruction
      Value *S,               ///< The value to be casted (operand 0)
      Type *Ty,               ///< The type to which cast should be made
      const Twine &Name = "", ///< Name for the instruction
      InsertPosition InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a ZExt or BitCast cast instruction
  static CastInst *CreateZExtOrBitCast(
      Value *S,               ///< The value to be casted (operand 0)
      Type *Ty,               ///< The type to which cast should be made
      const Twine &Name = "", ///< Name for the instruction
      InsertPosition InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a SExt or BitCast cast instruction
  static CastInst *CreateSExtOrBitCast(
      Value *S,               ///< The value to be casted (operand 0)
      Type *Ty,               ///< The type to which cast should be made
      const Twine &Name = "", ///< Name for the instruction
      InsertPosition InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a BitCast, AddrSpaceCast or a PtrToInt cast instruction.
  static CastInst *CreatePointerCast(
      Value *S,               ///< The pointer value to be casted (operand 0)
      Type *Ty,               ///< The type to which cast should be made
      const Twine &Name = "", ///< Name for the instruction
      InsertPosition InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a BitCast or an AddrSpaceCast cast instruction.
  static CastInst *CreatePointerBitCastOrAddrSpaceCast(
      Value *S,               ///< The pointer value to be casted (operand 0)
      Type *Ty,               ///< The type to which cast should be made
      const Twine &Name = "", ///< Name for the instruction
      InsertPosition InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a BitCast, a PtrToInt, or an IntToPTr cast instruction.
  ///
  /// If the value is a pointer type and the destination an integer type,
  /// creates a PtrToInt cast. If the value is an integer type and the
  /// destination a pointer type, creates an IntToPtr cast. Otherwise, creates
  /// a bitcast.
  static CastInst *CreateBitOrPointerCast(
      Value *S,               ///< The pointer value to be casted (operand 0)
      Type *Ty,               ///< The type to which cast should be made
      const Twine &Name = "", ///< Name for the instruction
      InsertPosition InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a ZExt, BitCast, or Trunc for int -> int casts.
  static CastInst *CreateIntegerCast(
      Value *S,               ///< The pointer value to be casted (operand 0)
      Type *Ty,               ///< The type to which cast should be made
      bool isSigned,          ///< Whether to regard S as signed or not
      const Twine &Name = "", ///< Name for the instruction
      InsertPosition InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create an FPExt, BitCast, or FPTrunc for fp -> fp casts
  static CastInst *CreateFPCast(
      Value *S,               ///< The floating point value to be casted
      Type *Ty,               ///< The floating point type to cast to
      const Twine &Name = "", ///< Name for the instruction
      InsertPosition InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a Trunc or BitCast cast instruction
  static CastInst *CreateTruncOrBitCast(
      Value *S,               ///< The value to be casted (operand 0)
      Type *Ty,               ///< The type to which cast should be made
      const Twine &Name = "", ///< Name for the instruction
      InsertPosition InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Check whether a bitcast between these types is valid
  static bool isBitCastable(
    Type *SrcTy, ///< The Type from which the value should be cast.
    Type *DestTy ///< The Type to which the value should be cast.
  );

  /// Check whether a bitcast, inttoptr, or ptrtoint cast between these
  /// types is valid and a no-op.
  ///
  /// This ensures that any pointer<->integer cast has enough bits in the
  /// integer and any other cast is a bitcast.
  static bool isBitOrNoopPointerCastable(
      Type *SrcTy,  ///< The Type from which the value should be cast.
      Type *DestTy, ///< The Type to which the value should be cast.
      const DataLayout &DL);

  /// Returns the opcode necessary to cast Val into Ty using usual casting
  /// rules.
  /// Infer the opcode for cast operand and type
  static Instruction::CastOps getCastOpcode(
    const Value *Val, ///< The value to cast
    bool SrcIsSigned, ///< Whether to treat the source as signed
    Type *Ty,   ///< The Type to which the value should be casted
    bool DstIsSigned  ///< Whether to treate the dest. as signed
  );

  /// There are several places where we need to know if a cast instruction
  /// only deals with integer source and destination types. To simplify that
  /// logic, this method is provided.
  /// @returns true iff the cast has only integral typed operand and dest type.
  /// Determine if this is an integer-only cast.
  bool isIntegerCast() const;

  /// A no-op cast is one that can be effected without changing any bits.
  /// It implies that the source and destination types are the same size. The
  /// DataLayout argument is to determine the pointer size when examining casts
  /// involving Integer and Pointer types. They are no-op casts if the integer
  /// is the same size as the pointer. However, pointer size varies with
  /// platform.  Note that a precondition of this method is that the cast is
  /// legal - i.e. the instruction formed with these operands would verify.
  static bool isNoopCast(
    Instruction::CastOps Opcode, ///< Opcode of cast
    Type *SrcTy,         ///< SrcTy of cast
    Type *DstTy,         ///< DstTy of cast
    const DataLayout &DL ///< DataLayout to get the Int Ptr type from.
  );

  /// Determine if this cast is a no-op cast.
  ///
  /// \param DL is the DataLayout to determine pointer size.
  bool isNoopCast(const DataLayout &DL) const;

  /// Determine how a pair of casts can be eliminated, if they can be at all.
  /// This is a helper function for both CastInst and ConstantExpr.
  /// @returns 0 if the CastInst pair can't be eliminated, otherwise
  /// returns Instruction::CastOps value for a cast that can replace
  /// the pair, casting SrcTy to DstTy.
  /// Determine if a cast pair is eliminable
  static unsigned isEliminableCastPair(
    Instruction::CastOps firstOpcode,  ///< Opcode of first cast
    Instruction::CastOps secondOpcode, ///< Opcode of second cast
    Type *SrcTy, ///< SrcTy of 1st cast
    Type *MidTy, ///< DstTy of 1st cast & SrcTy of 2nd cast
    Type *DstTy, ///< DstTy of 2nd cast
    Type *SrcIntPtrTy, ///< Integer type corresponding to Ptr SrcTy, or null
    Type *MidIntPtrTy, ///< Integer type corresponding to Ptr MidTy, or null
    Type *DstIntPtrTy  ///< Integer type corresponding to Ptr DstTy, or null
  );

  /// Return the opcode of this CastInst
  Instruction::CastOps getOpcode() const {
    return Instruction::CastOps(Instruction::getOpcode());
  }

  /// Return the source type, as a convenience
  Type* getSrcTy() const { return getOperand(0)->getType(); }
  /// Return the destination type, as a convenience
  Type* getDestTy() const { return getType(); }

  /// This method can be used to determine if a cast from SrcTy to DstTy using
  /// Opcode op is valid or not.
  /// @returns true iff the proposed cast is valid.
  /// Determine if a cast is valid without creating one.
  static bool castIsValid(Instruction::CastOps op, Type *SrcTy, Type *DstTy);
  static bool castIsValid(Instruction::CastOps op, Value *S, Type *DstTy) {
    return castIsValid(op, S->getType(), DstTy);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->isCast();
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

/// Instruction that can have a nneg flag (zext/uitofp).
class PossiblyNonNegInst : public CastInst {
public:
  enum { NonNeg = (1 << 0) };

  static bool classof(const Instruction *I) {
    switch (I->getOpcode()) {
    case Instruction::ZExt:
    case Instruction::UIToFP:
      return true;
    default:
      return false;
    }
  }

  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               CmpInst Class
//===----------------------------------------------------------------------===//

/// This class is the base class for the comparison instructions.
/// Abstract base class of comparison instructions.
class CmpInst : public Instruction {
public:
  /// This enumeration lists the possible predicates for CmpInst subclasses.
  /// Values in the range 0-31 are reserved for FCmpInst, while values in the
  /// range 32-64 are reserved for ICmpInst. This is necessary to ensure the
  /// predicate values are not overlapping between the classes.
  ///
  /// Some passes (e.g. InstCombine) depend on the bit-wise characteristics of
  /// FCMP_* values. Changing the bit patterns requires a potential change to
  /// those passes.
  enum Predicate : unsigned {
    // Opcode            U L G E    Intuitive operation
    FCMP_FALSE = 0, ///< 0 0 0 0    Always false (always folded)
    FCMP_OEQ = 1,   ///< 0 0 0 1    True if ordered and equal
    FCMP_OGT = 2,   ///< 0 0 1 0    True if ordered and greater than
    FCMP_OGE = 3,   ///< 0 0 1 1    True if ordered and greater than or equal
    FCMP_OLT = 4,   ///< 0 1 0 0    True if ordered and less than
    FCMP_OLE = 5,   ///< 0 1 0 1    True if ordered and less than or equal
    FCMP_ONE = 6,   ///< 0 1 1 0    True if ordered and operands are unequal
    FCMP_ORD = 7,   ///< 0 1 1 1    True if ordered (no nans)
    FCMP_UNO = 8,   ///< 1 0 0 0    True if unordered: isnan(X) | isnan(Y)
    FCMP_UEQ = 9,   ///< 1 0 0 1    True if unordered or equal
    FCMP_UGT = 10,  ///< 1 0 1 0    True if unordered or greater than
    FCMP_UGE = 11,  ///< 1 0 1 1    True if unordered, greater than, or equal
    FCMP_ULT = 12,  ///< 1 1 0 0    True if unordered or less than
    FCMP_ULE = 13,  ///< 1 1 0 1    True if unordered, less than, or equal
    FCMP_UNE = 14,  ///< 1 1 1 0    True if unordered or not equal
    FCMP_TRUE = 15, ///< 1 1 1 1    Always true (always folded)
    FIRST_FCMP_PREDICATE = FCMP_FALSE,
    LAST_FCMP_PREDICATE = FCMP_TRUE,
    BAD_FCMP_PREDICATE = FCMP_TRUE + 1,
    ICMP_EQ = 32,  ///< equal
    ICMP_NE = 33,  ///< not equal
    ICMP_UGT = 34, ///< unsigned greater than
    ICMP_UGE = 35, ///< unsigned greater or equal
    ICMP_ULT = 36, ///< unsigned less than
    ICMP_ULE = 37, ///< unsigned less or equal
    ICMP_SGT = 38, ///< signed greater than
    ICMP_SGE = 39, ///< signed greater or equal
    ICMP_SLT = 40, ///< signed less than
    ICMP_SLE = 41, ///< signed less or equal
    FIRST_ICMP_PREDICATE = ICMP_EQ,
    LAST_ICMP_PREDICATE = ICMP_SLE,
    BAD_ICMP_PREDICATE = ICMP_SLE + 1
  };
  using PredicateField =
      Bitfield::Element<Predicate, 0, 6, LAST_ICMP_PREDICATE>;

  /// Returns the sequence of all FCmp predicates.
  static auto FCmpPredicates() {
    return enum_seq_inclusive(Predicate::FIRST_FCMP_PREDICATE,
                              Predicate::LAST_FCMP_PREDICATE,
                              force_iteration_on_noniterable_enum);
  }

  /// Returns the sequence of all ICmp predicates.
  static auto ICmpPredicates() {
    return enum_seq_inclusive(Predicate::FIRST_ICMP_PREDICATE,
                              Predicate::LAST_ICMP_PREDICATE,
                              force_iteration_on_noniterable_enum);
  }

protected:
  CmpInst(Type *ty, Instruction::OtherOps op, Predicate pred, Value *LHS,
          Value *RHS, const Twine &Name = "",
          InsertPosition InsertBefore = nullptr,
          Instruction *FlagsSource = nullptr);

public:
  // allocate space for exactly two operands
  void *operator new(size_t S) { return User::operator new(S, 2); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Construct a compare instruction, given the opcode, the predicate and
  /// the two operands.  Optionally (if InstBefore is specified) insert the
  /// instruction into a BasicBlock right before the specified instruction.
  /// The specified Instruction is allowed to be a dereferenced end iterator.
  /// Create a CmpInst
  static CmpInst *Create(OtherOps Op, Predicate Pred, Value *S1, Value *S2,
                         const Twine &Name = "",
                         InsertPosition InsertBefore = nullptr);

  /// Construct a compare instruction, given the opcode, the predicate,
  /// the two operands and the instruction to copy the flags from. Optionally
  /// (if InstBefore is specified) insert the instruction into a BasicBlock
  /// right before the specified instruction. The specified Instruction is
  /// allowed to be a dereferenced end iterator.
  /// Create a CmpInst
  static CmpInst *CreateWithCopiedFlags(OtherOps Op, Predicate Pred, Value *S1,
                                        Value *S2,
                                        const Instruction *FlagsSource,
                                        const Twine &Name = "",
                                        InsertPosition InsertBefore = nullptr);

  /// Get the opcode casted to the right type
  OtherOps getOpcode() const {
    return static_cast<OtherOps>(Instruction::getOpcode());
  }

  /// Return the predicate for this instruction.
  Predicate getPredicate() const { return getSubclassData<PredicateField>(); }

  /// Set the predicate for this instruction to the specified value.
  void setPredicate(Predicate P) { setSubclassData<PredicateField>(P); }

  static bool isFPPredicate(Predicate P) {
    static_assert(FIRST_FCMP_PREDICATE == 0,
                  "FIRST_FCMP_PREDICATE is required to be 0");
    return P <= LAST_FCMP_PREDICATE;
  }

  static bool isIntPredicate(Predicate P) {
    return P >= FIRST_ICMP_PREDICATE && P <= LAST_ICMP_PREDICATE;
  }

  static StringRef getPredicateName(Predicate P);

  bool isFPPredicate() const { return isFPPredicate(getPredicate()); }
  bool isIntPredicate() const { return isIntPredicate(getPredicate()); }

  /// For example, EQ -> NE, UGT -> ULE, SLT -> SGE,
  ///              OEQ -> UNE, UGT -> OLE, OLT -> UGE, etc.
  /// @returns the inverse predicate for the instruction's current predicate.
  /// Return the inverse of the instruction's predicate.
  Predicate getInversePredicate() const {
    return getInversePredicate(getPredicate());
  }

  /// Returns the ordered variant of a floating point compare.
  ///
  /// For example, UEQ -> OEQ, ULT -> OLT, OEQ -> OEQ
  static Predicate getOrderedPredicate(Predicate Pred) {
    return static_cast<Predicate>(Pred & FCMP_ORD);
  }

  Predicate getOrderedPredicate() const {
    return getOrderedPredicate(getPredicate());
  }

  /// Returns the unordered variant of a floating point compare.
  ///
  /// For example, OEQ -> UEQ, OLT -> ULT, OEQ -> UEQ
  static Predicate getUnorderedPredicate(Predicate Pred) {
    return static_cast<Predicate>(Pred | FCMP_UNO);
  }

  Predicate getUnorderedPredicate() const {
    return getUnorderedPredicate(getPredicate());
  }

  /// For example, EQ -> NE, UGT -> ULE, SLT -> SGE,
  ///              OEQ -> UNE, UGT -> OLE, OLT -> UGE, etc.
  /// @returns the inverse predicate for predicate provided in \p pred.
  /// Return the inverse of a given predicate
  static Predicate getInversePredicate(Predicate pred);

  /// For example, EQ->EQ, SLE->SGE, ULT->UGT,
  ///              OEQ->OEQ, ULE->UGE, OLT->OGT, etc.
  /// @returns the predicate that would be the result of exchanging the two
  /// operands of the CmpInst instruction without changing the result
  /// produced.
  /// Return the predicate as if the operands were swapped
  Predicate getSwappedPredicate() const {
    return getSwappedPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction
  /// available.
  /// Return the predicate as if the operands were swapped.
  static Predicate getSwappedPredicate(Predicate pred);

  /// This is a static version that you can use without an instruction
  /// available.
  /// @returns true if the comparison predicate is strict, false otherwise.
  static bool isStrictPredicate(Predicate predicate);

  /// @returns true if the comparison predicate is strict, false otherwise.
  /// Determine if this instruction is using an strict comparison predicate.
  bool isStrictPredicate() const { return isStrictPredicate(getPredicate()); }

  /// This is a static version that you can use without an instruction
  /// available.
  /// @returns true if the comparison predicate is non-strict, false otherwise.
  static bool isNonStrictPredicate(Predicate predicate);

  /// @returns true if the comparison predicate is non-strict, false otherwise.
  /// Determine if this instruction is using an non-strict comparison predicate.
  bool isNonStrictPredicate() const {
    return isNonStrictPredicate(getPredicate());
  }

  /// For example, SGE -> SGT, SLE -> SLT, ULE -> ULT, UGE -> UGT.
  /// Returns the strict version of non-strict comparisons.
  Predicate getStrictPredicate() const {
    return getStrictPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction
  /// available.
  /// @returns the strict version of comparison provided in \p pred.
  /// If \p pred is not a strict comparison predicate, returns \p pred.
  /// Returns the strict version of non-strict comparisons.
  static Predicate getStrictPredicate(Predicate pred);

  /// For example, SGT -> SGE, SLT -> SLE, ULT -> ULE, UGT -> UGE.
  /// Returns the non-strict version of strict comparisons.
  Predicate getNonStrictPredicate() const {
    return getNonStrictPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction
  /// available.
  /// @returns the non-strict version of comparison provided in \p pred.
  /// If \p pred is not a strict comparison predicate, returns \p pred.
  /// Returns the non-strict version of strict comparisons.
  static Predicate getNonStrictPredicate(Predicate pred);

  /// This is a static version that you can use without an instruction
  /// available.
  /// Return the flipped strictness of predicate
  static Predicate getFlippedStrictnessPredicate(Predicate pred);

  /// For predicate of kind "is X or equal to 0" returns the predicate "is X".
  /// For predicate of kind "is X" returns the predicate "is X or equal to 0".
  /// does not support other kind of predicates.
  /// @returns the predicate that does not contains is equal to zero if
  /// it had and vice versa.
  /// Return the flipped strictness of predicate
  Predicate getFlippedStrictnessPredicate() const {
    return getFlippedStrictnessPredicate(getPredicate());
  }

  /// Provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// This is just a convenience that dispatches to the subclasses.
  /// Swap the operands and adjust predicate accordingly to retain
  /// the same comparison.
  void swapOperands();

  /// This is just a convenience that dispatches to the subclasses.
  /// Determine if this CmpInst is commutative.
  bool isCommutative() const;

  /// Determine if this is an equals/not equals predicate.
  /// This is a static version that you can use without an instruction
  /// available.
  static bool isEquality(Predicate pred);

  /// Determine if this is an equals/not equals predicate.
  bool isEquality() const { return isEquality(getPredicate()); }

  /// Return true if the predicate is relational (not EQ or NE).
  static bool isRelational(Predicate P) { return !isEquality(P); }

  /// Return true if the predicate is relational (not EQ or NE).
  bool isRelational() const { return !isEquality(); }

  /// @returns true if the comparison is signed, false otherwise.
  /// Determine if this instruction is using a signed comparison.
  bool isSigned() const {
    return isSigned(getPredicate());
  }

  /// @returns true if the comparison is unsigned, false otherwise.
  /// Determine if this instruction is using an unsigned comparison.
  bool isUnsigned() const {
    return isUnsigned(getPredicate());
  }

  /// For example, ULT->SLT, ULE->SLE, UGT->SGT, UGE->SGE, SLT->Failed assert
  /// @returns the signed version of the unsigned predicate pred.
  /// return the signed version of a predicate
  static Predicate getSignedPredicate(Predicate pred);

  /// For example, ULT->SLT, ULE->SLE, UGT->SGT, UGE->SGE, SLT->Failed assert
  /// @returns the signed version of the predicate for this instruction (which
  /// has to be an unsigned predicate).
  /// return the signed version of a predicate
  Predicate getSignedPredicate() {
    return getSignedPredicate(getPredicate());
  }

  /// For example, SLT->ULT, SLE->ULE, SGT->UGT, SGE->UGE, ULT->Failed assert
  /// @returns the unsigned version of the signed predicate pred.
  static Predicate getUnsignedPredicate(Predicate pred);

  /// For example, SLT->ULT, SLE->ULE, SGT->UGT, SGE->UGE, ULT->Failed assert
  /// @returns the unsigned version of the predicate for this instruction (which
  /// has to be an signed predicate).
  /// return the unsigned version of a predicate
  Predicate getUnsignedPredicate() {
    return getUnsignedPredicate(getPredicate());
  }

  /// For example, SLT->ULT, ULT->SLT, SLE->ULE, ULE->SLE, EQ->Failed assert
  /// @returns the unsigned version of the signed predicate pred or
  ///          the signed version of the signed predicate pred.
  static Predicate getFlippedSignednessPredicate(Predicate pred);

  /// For example, SLT->ULT, ULT->SLT, SLE->ULE, ULE->SLE, EQ->Failed assert
  /// @returns the unsigned version of the signed predicate pred or
  ///          the signed version of the signed predicate pred.
  Predicate getFlippedSignednessPredicate() {
    return getFlippedSignednessPredicate(getPredicate());
  }

  /// This is just a convenience.
  /// Determine if this is true when both operands are the same.
  bool isTrueWhenEqual() const {
    return isTrueWhenEqual(getPredicate());
  }

  /// This is just a convenience.
  /// Determine if this is false when both operands are the same.
  bool isFalseWhenEqual() const {
    return isFalseWhenEqual(getPredicate());
  }

  /// @returns true if the predicate is unsigned, false otherwise.
  /// Determine if the predicate is an unsigned operation.
  static bool isUnsigned(Predicate predicate);

  /// @returns true if the predicate is signed, false otherwise.
  /// Determine if the predicate is an signed operation.
  static bool isSigned(Predicate predicate);

  /// Determine if the predicate is an ordered operation.
  static bool isOrdered(Predicate predicate);

  /// Determine if the predicate is an unordered operation.
  static bool isUnordered(Predicate predicate);

  /// Determine if the predicate is true when comparing a value with itself.
  static bool isTrueWhenEqual(Predicate predicate);

  /// Determine if the predicate is false when comparing a value with itself.
  static bool isFalseWhenEqual(Predicate predicate);

  /// Determine if Pred1 implies Pred2 is true when two compares have matching
  /// operands.
  static bool isImpliedTrueByMatchingCmp(Predicate Pred1, Predicate Pred2);

  /// Determine if Pred1 implies Pred2 is false when two compares have matching
  /// operands.
  static bool isImpliedFalseByMatchingCmp(Predicate Pred1, Predicate Pred2);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ICmp ||
           I->getOpcode() == Instruction::FCmp;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  /// Create a result type for fcmp/icmp
  static Type* makeCmpResultType(Type* opnd_type) {
    if (VectorType* vt = dyn_cast<VectorType>(opnd_type)) {
      return VectorType::get(Type::getInt1Ty(opnd_type->getContext()),
                             vt->getElementCount());
    }
    return Type::getInt1Ty(opnd_type->getContext());
  }

private:
  // Shadow Value::setValueSubclassData with a private forwarding method so that
  // subclasses cannot accidentally use it.
  void setValueSubclassData(unsigned short D) {
    Value::setValueSubclassData(D);
  }
};

// FIXME: these are redundant if CmpInst < BinaryOperator
template <>
struct OperandTraits<CmpInst> : public FixedNumOperandTraits<CmpInst, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CmpInst, Value)

raw_ostream &operator<<(raw_ostream &OS, CmpInst::Predicate Pred);

/// A lightweight accessor for an operand bundle meant to be passed
/// around by value.
struct OperandBundleUse {
  ArrayRef<Use> Inputs;

  OperandBundleUse() = default;
  explicit OperandBundleUse(StringMapEntry<uint32_t> *Tag, ArrayRef<Use> Inputs)
      : Inputs(Inputs), Tag(Tag) {}

  /// Return true if the operand at index \p Idx in this operand bundle
  /// has the attribute A.
  bool operandHasAttr(unsigned Idx, Attribute::AttrKind A) const {
    if (isDeoptOperandBundle())
      if (A == Attribute::ReadOnly || A == Attribute::NoCapture)
        return Inputs[Idx]->getType()->isPointerTy();

    // Conservative answer:  no operands have any attributes.
    return false;
  }

  /// Return the tag of this operand bundle as a string.
  StringRef getTagName() const {
    return Tag->getKey();
  }

  /// Return the tag of this operand bundle as an integer.
  ///
  /// Operand bundle tags are interned by LLVMContextImpl::getOrInsertBundleTag,
  /// and this function returns the unique integer getOrInsertBundleTag
  /// associated the tag of this operand bundle to.
  uint32_t getTagID() const {
    return Tag->getValue();
  }

  /// Return true if this is a "deopt" operand bundle.
  bool isDeoptOperandBundle() const {
    return getTagID() == LLVMContext::OB_deopt;
  }

  /// Return true if this is a "funclet" operand bundle.
  bool isFuncletOperandBundle() const {
    return getTagID() == LLVMContext::OB_funclet;
  }

  /// Return true if this is a "cfguardtarget" operand bundle.
  bool isCFGuardTargetOperandBundle() const {
    return getTagID() == LLVMContext::OB_cfguardtarget;
  }

private:
  /// Pointer to an entry in LLVMContextImpl::getOrInsertBundleTag.
  StringMapEntry<uint32_t> *Tag;
};

/// A container for an operand bundle being viewed as a set of values
/// rather than a set of uses.
///
/// Unlike OperandBundleUse, OperandBundleDefT owns the memory it carries, and
/// so it is possible to create and pass around "self-contained" instances of
/// OperandBundleDef and ConstOperandBundleDef.
template <typename InputTy> class OperandBundleDefT {
  std::string Tag;
  std::vector<InputTy> Inputs;

public:
  explicit OperandBundleDefT(std::string Tag, std::vector<InputTy> Inputs)
      : Tag(std::move(Tag)), Inputs(std::move(Inputs)) {}
  explicit OperandBundleDefT(std::string Tag, ArrayRef<InputTy> Inputs)
      : Tag(std::move(Tag)), Inputs(Inputs) {}

  explicit OperandBundleDefT(const OperandBundleUse &OBU) {
    Tag = std::string(OBU.getTagName());
    llvm::append_range(Inputs, OBU.Inputs);
  }

  ArrayRef<InputTy> inputs() const { return Inputs; }

  using input_iterator = typename std::vector<InputTy>::const_iterator;

  size_t input_size() const { return Inputs.size(); }
  input_iterator input_begin() const { return Inputs.begin(); }
  input_iterator input_end() const { return Inputs.end(); }

  StringRef getTag() const { return Tag; }
};

using OperandBundleDef = OperandBundleDefT<Value *>;
using ConstOperandBundleDef = OperandBundleDefT<const Value *>;

//===----------------------------------------------------------------------===//
//                               CallBase Class
//===----------------------------------------------------------------------===//

/// Base class for all callable instructions (InvokeInst and CallInst)
/// Holds everything related to calling a function.
///
/// All call-like instructions are required to use a common operand layout:
/// - Zero or more arguments to the call,
/// - Zero or more operand bundles with zero or more operand inputs each
///   bundle,
/// - Zero or more subclass controlled operands
/// - The called function.
///
/// This allows this base class to easily access the called function and the
/// start of the arguments without knowing how many other operands a particular
/// subclass requires. Note that accessing the end of the argument list isn't
/// as cheap as most other operations on the base class.
class CallBase : public Instruction {
protected:
  // The first two bits are reserved by CallInst for fast retrieval,
  using CallInstReservedField = Bitfield::Element<unsigned, 0, 2>;
  using CallingConvField =
      Bitfield::Element<CallingConv::ID, CallInstReservedField::NextBit, 10,
                        CallingConv::MaxID>;
  static_assert(
      Bitfield::areContiguous<CallInstReservedField, CallingConvField>(),
      "Bitfields must be contiguous");

  /// The last operand is the called operand.
  static constexpr int CalledOperandOpEndIdx = -1;

  AttributeList Attrs; ///< parameter attributes for callable
  FunctionType *FTy;

  template <class... ArgsTy>
  CallBase(AttributeList const &A, FunctionType *FT, ArgsTy &&... Args)
      : Instruction(std::forward<ArgsTy>(Args)...), Attrs(A), FTy(FT) {}

  using Instruction::Instruction;

  bool hasDescriptor() const { return Value::HasDescriptor; }

  unsigned getNumSubclassExtraOperands() const {
    switch (getOpcode()) {
    case Instruction::Call:
      return 0;
    case Instruction::Invoke:
      return 2;
    case Instruction::CallBr:
      return getNumSubclassExtraOperandsDynamic();
    }
    llvm_unreachable("Invalid opcode!");
  }

  /// Get the number of extra operands for instructions that don't have a fixed
  /// number of extra operands.
  unsigned getNumSubclassExtraOperandsDynamic() const;

public:
  using Instruction::getContext;

  /// Create a clone of \p CB with a different set of operand bundles and
  /// insert it before \p InsertPt.
  ///
  /// The returned call instruction is identical \p CB in every way except that
  /// the operand bundles for the new instruction are set to the operand bundles
  /// in \p Bundles.
  static CallBase *Create(CallBase *CB, ArrayRef<OperandBundleDef> Bundles,
                          InsertPosition InsertPt = nullptr);

  /// Create a clone of \p CB with the operand bundle with the tag matching
  /// \p Bundle's tag replaced with Bundle, and insert it before \p InsertPt.
  ///
  /// The returned call instruction is identical \p CI in every way except that
  /// the specified operand bundle has been replaced.
  static CallBase *Create(CallBase *CB, OperandBundleDef Bundle,
                          InsertPosition InsertPt = nullptr);

  /// Create a clone of \p CB with operand bundle \p OB added.
  static CallBase *addOperandBundle(CallBase *CB, uint32_t ID,
                                    OperandBundleDef OB,
                                    InsertPosition InsertPt = nullptr);

  /// Create a clone of \p CB with operand bundle \p ID removed.
  static CallBase *removeOperandBundle(CallBase *CB, uint32_t ID,
                                       InsertPosition InsertPt = nullptr);

  /// Return the convergence control token for this call, if it exists.
  Value *getConvergenceControlToken() const {
    if (auto Bundle = getOperandBundle(llvm::LLVMContext::OB_convergencectrl)) {
      return Bundle->Inputs[0].get();
    }
    return nullptr;
  }

  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Call ||
           I->getOpcode() == Instruction::Invoke ||
           I->getOpcode() == Instruction::CallBr;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  FunctionType *getFunctionType() const { return FTy; }

  void mutateFunctionType(FunctionType *FTy) {
    Value::mutateType(FTy->getReturnType());
    this->FTy = FTy;
  }

  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// data_operands_begin/data_operands_end - Return iterators iterating over
  /// the call / invoke argument list and bundle operands.  For invokes, this is
  /// the set of instruction operands except the invoke target and the two
  /// successor blocks; and for calls this is the set of instruction operands
  /// except the call target.
  User::op_iterator data_operands_begin() { return op_begin(); }
  User::const_op_iterator data_operands_begin() const {
    return const_cast<CallBase *>(this)->data_operands_begin();
  }
  User::op_iterator data_operands_end() {
    // Walk from the end of the operands over the called operand and any
    // subclass operands.
    return op_end() - getNumSubclassExtraOperands() - 1;
  }
  User::const_op_iterator data_operands_end() const {
    return const_cast<CallBase *>(this)->data_operands_end();
  }
  iterator_range<User::op_iterator> data_ops() {
    return make_range(data_operands_begin(), data_operands_end());
  }
  iterator_range<User::const_op_iterator> data_ops() const {
    return make_range(data_operands_begin(), data_operands_end());
  }
  bool data_operands_empty() const {
    return data_operands_end() == data_operands_begin();
  }
  unsigned data_operands_size() const {
    return std::distance(data_operands_begin(), data_operands_end());
  }

  bool isDataOperand(const Use *U) const {
    assert(this == U->getUser() &&
           "Only valid to query with a use of this instruction!");
    return data_operands_begin() <= U && U < data_operands_end();
  }
  bool isDataOperand(Value::const_user_iterator UI) const {
    return isDataOperand(&UI.getUse());
  }

  /// Given a value use iterator, return the data operand corresponding to it.
  /// Iterator must actually correspond to a data operand.
  unsigned getDataOperandNo(Value::const_user_iterator UI) const {
    return getDataOperandNo(&UI.getUse());
  }

  /// Given a use for a data operand, get the data operand number that
  /// corresponds to it.
  unsigned getDataOperandNo(const Use *U) const {
    assert(isDataOperand(U) && "Data operand # out of range!");
    return U - data_operands_begin();
  }

  /// Return the iterator pointing to the beginning of the argument list.
  User::op_iterator arg_begin() { return op_begin(); }
  User::const_op_iterator arg_begin() const {
    return const_cast<CallBase *>(this)->arg_begin();
  }

  /// Return the iterator pointing to the end of the argument list.
  User::op_iterator arg_end() {
    // From the end of the data operands, walk backwards past the bundle
    // operands.
    return data_operands_end() - getNumTotalBundleOperands();
  }
  User::const_op_iterator arg_end() const {
    return const_cast<CallBase *>(this)->arg_end();
  }

  /// Iteration adapter for range-for loops.
  iterator_range<User::op_iterator> args() {
    return make_range(arg_begin(), arg_end());
  }
  iterator_range<User::const_op_iterator> args() const {
    return make_range(arg_begin(), arg_end());
  }
  bool arg_empty() const { return arg_end() == arg_begin(); }
  unsigned arg_size() const { return arg_end() - arg_begin(); }

  Value *getArgOperand(unsigned i) const {
    assert(i < arg_size() && "Out of bounds!");
    return getOperand(i);
  }

  void setArgOperand(unsigned i, Value *v) {
    assert(i < arg_size() && "Out of bounds!");
    setOperand(i, v);
  }

  /// Wrappers for getting the \c Use of a call argument.
  const Use &getArgOperandUse(unsigned i) const {
    assert(i < arg_size() && "Out of bounds!");
    return User::getOperandUse(i);
  }
  Use &getArgOperandUse(unsigned i) {
    assert(i < arg_size() && "Out of bounds!");
    return User::getOperandUse(i);
  }

  bool isArgOperand(const Use *U) const {
    assert(this == U->getUser() &&
           "Only valid to query with a use of this instruction!");
    return arg_begin() <= U && U < arg_end();
  }
  bool isArgOperand(Value::const_user_iterator UI) const {
    return isArgOperand(&UI.getUse());
  }

  /// Given a use for a arg operand, get the arg operand number that
  /// corresponds to it.
  unsigned getArgOperandNo(const Use *U) const {
    assert(isArgOperand(U) && "Arg operand # out of range!");
    return U - arg_begin();
  }

  /// Given a value use iterator, return the arg operand number corresponding to
  /// it. Iterator must actually correspond to a data operand.
  unsigned getArgOperandNo(Value::const_user_iterator UI) const {
    return getArgOperandNo(&UI.getUse());
  }

  /// Returns true if this CallSite passes the given Value* as an argument to
  /// the called function.
  bool hasArgument(const Value *V) const {
    return llvm::is_contained(args(), V);
  }

  Value *getCalledOperand() const { return Op<CalledOperandOpEndIdx>(); }

  const Use &getCalledOperandUse() const { return Op<CalledOperandOpEndIdx>(); }
  Use &getCalledOperandUse() { return Op<CalledOperandOpEndIdx>(); }

  /// Returns the function called, or null if this is an indirect function
  /// invocation or the function signature does not match the call signature.
  Function *getCalledFunction() const {
    if (auto *F = dyn_cast_or_null<Function>(getCalledOperand()))
      if (F->getValueType() == getFunctionType())
        return F;
    return nullptr;
  }

  /// Return true if the callsite is an indirect call.
  bool isIndirectCall() const;

  /// Determine whether the passed iterator points to the callee operand's Use.
  bool isCallee(Value::const_user_iterator UI) const {
    return isCallee(&UI.getUse());
  }

  /// Determine whether this Use is the callee operand's Use.
  bool isCallee(const Use *U) const { return &getCalledOperandUse() == U; }

  /// Helper to get the caller (the parent function).
  Function *getCaller();
  const Function *getCaller() const {
    return const_cast<CallBase *>(this)->getCaller();
  }

  /// Tests if this call site must be tail call optimized. Only a CallInst can
  /// be tail call optimized.
  bool isMustTailCall() const;

  /// Tests if this call site is marked as a tail call.
  bool isTailCall() const;

  /// Returns the intrinsic ID of the intrinsic called or
  /// Intrinsic::not_intrinsic if the called function is not an intrinsic, or if
  /// this is an indirect call.
  Intrinsic::ID getIntrinsicID() const;

  void setCalledOperand(Value *V) { Op<CalledOperandOpEndIdx>() = V; }

  /// Sets the function called, including updating the function type.
  void setCalledFunction(Function *Fn) {
    setCalledFunction(Fn->getFunctionType(), Fn);
  }

  /// Sets the function called, including updating the function type.
  void setCalledFunction(FunctionCallee Fn) {
    setCalledFunction(Fn.getFunctionType(), Fn.getCallee());
  }

  /// Sets the function called, including updating to the specified function
  /// type.
  void setCalledFunction(FunctionType *FTy, Value *Fn) {
    this->FTy = FTy;
    // This function doesn't mutate the return type, only the function
    // type. Seems broken, but I'm just gonna stick an assert in for now.
    assert(getType() == FTy->getReturnType());
    setCalledOperand(Fn);
  }

  CallingConv::ID getCallingConv() const {
    return getSubclassData<CallingConvField>();
  }

  void setCallingConv(CallingConv::ID CC) {
    setSubclassData<CallingConvField>(CC);
  }

  /// Check if this call is an inline asm statement.
  bool isInlineAsm() const { return isa<InlineAsm>(getCalledOperand()); }

  /// \name Attribute API
  ///
  /// These methods access and modify attributes on this call (including
  /// looking through to the attributes on the called function when necessary).
  ///@{

  /// Return the parameter attributes for this call.
  ///
  AttributeList getAttributes() const { return Attrs; }

  /// Set the parameter attributes for this call.
  ///
  void setAttributes(AttributeList A) { Attrs = A; }

  /// Determine whether this call has the given attribute. If it does not
  /// then determine if the called function has the attribute, but only if
  /// the attribute is allowed for the call.
  bool hasFnAttr(Attribute::AttrKind Kind) const {
    assert(Kind != Attribute::NoBuiltin &&
           "Use CallBase::isNoBuiltin() to check for Attribute::NoBuiltin");
    return hasFnAttrImpl(Kind);
  }

  /// Determine whether this call has the given attribute. If it does not
  /// then determine if the called function has the attribute, but only if
  /// the attribute is allowed for the call.
  bool hasFnAttr(StringRef Kind) const { return hasFnAttrImpl(Kind); }

  // TODO: remove non-AtIndex versions of these methods.
  /// adds the attribute to the list of attributes.
  void addAttributeAtIndex(unsigned i, Attribute::AttrKind Kind) {
    Attrs = Attrs.addAttributeAtIndex(getContext(), i, Kind);
  }

  /// adds the attribute to the list of attributes.
  void addAttributeAtIndex(unsigned i, Attribute Attr) {
    Attrs = Attrs.addAttributeAtIndex(getContext(), i, Attr);
  }

  /// Adds the attribute to the function.
  void addFnAttr(Attribute::AttrKind Kind) {
    Attrs = Attrs.addFnAttribute(getContext(), Kind);
  }

  /// Adds the attribute to the function.
  void addFnAttr(Attribute Attr) {
    Attrs = Attrs.addFnAttribute(getContext(), Attr);
  }

  /// Adds the attribute to the return value.
  void addRetAttr(Attribute::AttrKind Kind) {
    Attrs = Attrs.addRetAttribute(getContext(), Kind);
  }

  /// Adds the attribute to the return value.
  void addRetAttr(Attribute Attr) {
    Attrs = Attrs.addRetAttribute(getContext(), Attr);
  }

  /// Adds the attribute to the indicated argument
  void addParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attrs = Attrs.addParamAttribute(getContext(), ArgNo, Kind);
  }

  /// Adds the attribute to the indicated argument
  void addParamAttr(unsigned ArgNo, Attribute Attr) {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attrs = Attrs.addParamAttribute(getContext(), ArgNo, Attr);
  }

  /// removes the attribute from the list of attributes.
  void removeAttributeAtIndex(unsigned i, Attribute::AttrKind Kind) {
    Attrs = Attrs.removeAttributeAtIndex(getContext(), i, Kind);
  }

  /// removes the attribute from the list of attributes.
  void removeAttributeAtIndex(unsigned i, StringRef Kind) {
    Attrs = Attrs.removeAttributeAtIndex(getContext(), i, Kind);
  }

  /// Removes the attributes from the function
  void removeFnAttrs(const AttributeMask &AttrsToRemove) {
    Attrs = Attrs.removeFnAttributes(getContext(), AttrsToRemove);
  }

  /// Removes the attribute from the function
  void removeFnAttr(Attribute::AttrKind Kind) {
    Attrs = Attrs.removeFnAttribute(getContext(), Kind);
  }

  /// Removes the attribute from the function
  void removeFnAttr(StringRef Kind) {
    Attrs = Attrs.removeFnAttribute(getContext(), Kind);
  }

  /// Removes the attribute from the return value
  void removeRetAttr(Attribute::AttrKind Kind) {
    Attrs = Attrs.removeRetAttribute(getContext(), Kind);
  }

  /// Removes the attributes from the return value
  void removeRetAttrs(const AttributeMask &AttrsToRemove) {
    Attrs = Attrs.removeRetAttributes(getContext(), AttrsToRemove);
  }

  /// Removes the attribute from the given argument
  void removeParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attrs = Attrs.removeParamAttribute(getContext(), ArgNo, Kind);
  }

  /// Removes the attribute from the given argument
  void removeParamAttr(unsigned ArgNo, StringRef Kind) {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attrs = Attrs.removeParamAttribute(getContext(), ArgNo, Kind);
  }

  /// Removes the attributes from the given argument
  void removeParamAttrs(unsigned ArgNo, const AttributeMask &AttrsToRemove) {
    Attrs = Attrs.removeParamAttributes(getContext(), ArgNo, AttrsToRemove);
  }

  /// adds the dereferenceable attribute to the list of attributes.
  void addDereferenceableParamAttr(unsigned i, uint64_t Bytes) {
    Attrs = Attrs.addDereferenceableParamAttr(getContext(), i, Bytes);
  }

  /// adds the dereferenceable attribute to the list of attributes.
  void addDereferenceableRetAttr(uint64_t Bytes) {
    Attrs = Attrs.addDereferenceableRetAttr(getContext(), Bytes);
  }

  /// adds the range attribute to the list of attributes.
  void addRangeRetAttr(const ConstantRange &CR) {
    Attrs = Attrs.addRangeRetAttr(getContext(), CR);
  }

  /// Determine whether the return value has the given attribute.
  bool hasRetAttr(Attribute::AttrKind Kind) const {
    return hasRetAttrImpl(Kind);
  }
  /// Determine whether the return value has the given attribute.
  bool hasRetAttr(StringRef Kind) const { return hasRetAttrImpl(Kind); }

  /// Return the attribute for the given attribute kind for the return value.
  Attribute getRetAttr(Attribute::AttrKind Kind) const {
    Attribute RetAttr = Attrs.getRetAttr(Kind);
    if (RetAttr.isValid())
      return RetAttr;

    // Look at the callee, if available.
    if (const Function *F = getCalledFunction())
      return F->getRetAttribute(Kind);
    return Attribute();
  }

  /// Determine whether the argument or parameter has the given attribute.
  bool paramHasAttr(unsigned ArgNo, Attribute::AttrKind Kind) const;

  /// Get the attribute of a given kind at a position.
  Attribute getAttributeAtIndex(unsigned i, Attribute::AttrKind Kind) const {
    return getAttributes().getAttributeAtIndex(i, Kind);
  }

  /// Get the attribute of a given kind at a position.
  Attribute getAttributeAtIndex(unsigned i, StringRef Kind) const {
    return getAttributes().getAttributeAtIndex(i, Kind);
  }

  /// Get the attribute of a given kind for the function.
  Attribute getFnAttr(StringRef Kind) const {
    Attribute Attr = getAttributes().getFnAttr(Kind);
    if (Attr.isValid())
      return Attr;
    return getFnAttrOnCalledFunction(Kind);
  }

  /// Get the attribute of a given kind for the function.
  Attribute getFnAttr(Attribute::AttrKind Kind) const {
    Attribute A = getAttributes().getFnAttr(Kind);
    if (A.isValid())
      return A;
    return getFnAttrOnCalledFunction(Kind);
  }

  /// Get the attribute of a given kind from a given arg
  Attribute getParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) const {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attribute A = getAttributes().getParamAttr(ArgNo, Kind);
    if (A.isValid())
      return A;
    return getParamAttrOnCalledFunction(ArgNo, Kind);
  }

  /// Get the attribute of a given kind from a given arg
  Attribute getParamAttr(unsigned ArgNo, StringRef Kind) const {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attribute A = getAttributes().getParamAttr(ArgNo, Kind);
    if (A.isValid())
      return A;
    return getParamAttrOnCalledFunction(ArgNo, Kind);
  }

  /// Return true if the data operand at index \p i has the attribute \p
  /// A.
  ///
  /// Data operands include call arguments and values used in operand bundles,
  /// but does not include the callee operand.
  ///
  /// The index \p i is interpreted as
  ///
  ///  \p i in [0, arg_size)  -> argument number (\p i)
  ///  \p i in [arg_size, data_operand_size) -> bundle operand at index
  ///     (\p i) in the operand list.
  bool dataOperandHasImpliedAttr(unsigned i, Attribute::AttrKind Kind) const {
    // Note that we have to add one because `i` isn't zero-indexed.
    assert(i < arg_size() + getNumTotalBundleOperands() &&
           "Data operand index out of bounds!");

    // The attribute A can either be directly specified, if the operand in
    // question is a call argument; or be indirectly implied by the kind of its
    // containing operand bundle, if the operand is a bundle operand.

    if (i < arg_size())
      return paramHasAttr(i, Kind);

    assert(hasOperandBundles() && i >= getBundleOperandsStartIndex() &&
           "Must be either a call argument or an operand bundle!");
    return bundleOperandHasAttr(i, Kind);
  }

  /// Determine whether this data operand is not captured.
  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  bool doesNotCapture(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo, Attribute::NoCapture);
  }

  /// Determine whether this argument is passed by value.
  bool isByValArgument(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::ByVal);
  }

  /// Determine whether this argument is passed in an alloca.
  bool isInAllocaArgument(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::InAlloca);
  }

  /// Determine whether this argument is passed by value, in an alloca, or is
  /// preallocated.
  bool isPassPointeeByValueArgument(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::ByVal) ||
           paramHasAttr(ArgNo, Attribute::InAlloca) ||
           paramHasAttr(ArgNo, Attribute::Preallocated);
  }

  /// Determine whether passing undef to this argument is undefined behavior.
  /// If passing undef to this argument is UB, passing poison is UB as well
  /// because poison is more undefined than undef.
  bool isPassingUndefUB(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::NoUndef) ||
           // dereferenceable implies noundef.
           paramHasAttr(ArgNo, Attribute::Dereferenceable) ||
           // dereferenceable implies noundef, and null is a well-defined value.
           paramHasAttr(ArgNo, Attribute::DereferenceableOrNull);
  }

  /// Determine if there are is an inalloca argument. Only the last argument can
  /// have the inalloca attribute.
  bool hasInAllocaArgument() const {
    return !arg_empty() && paramHasAttr(arg_size() - 1, Attribute::InAlloca);
  }

  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  bool doesNotAccessMemory(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo, Attribute::ReadNone);
  }

  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  bool onlyReadsMemory(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo, Attribute::ReadOnly) ||
           dataOperandHasImpliedAttr(OpNo, Attribute::ReadNone);
  }

  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  bool onlyWritesMemory(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo, Attribute::WriteOnly) ||
           dataOperandHasImpliedAttr(OpNo, Attribute::ReadNone);
  }

  /// Extract the alignment of the return value.
  MaybeAlign getRetAlign() const {
    if (auto Align = Attrs.getRetAlignment())
      return Align;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getRetAlignment();
    return std::nullopt;
  }

  /// Extract the alignment for a call or parameter (0=unknown).
  MaybeAlign getParamAlign(unsigned ArgNo) const {
    return Attrs.getParamAlignment(ArgNo);
  }

  MaybeAlign getParamStackAlign(unsigned ArgNo) const {
    return Attrs.getParamStackAlignment(ArgNo);
  }

  /// Extract the byref type for a call or parameter.
  Type *getParamByRefType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamByRefType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamByRefType(ArgNo);
    return nullptr;
  }

  /// Extract the byval type for a call or parameter.
  Type *getParamByValType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamByValType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamByValType(ArgNo);
    return nullptr;
  }

  /// Extract the preallocated type for a call or parameter.
  Type *getParamPreallocatedType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamPreallocatedType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamPreallocatedType(ArgNo);
    return nullptr;
  }

  /// Extract the inalloca type for a call or parameter.
  Type *getParamInAllocaType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamInAllocaType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamInAllocaType(ArgNo);
    return nullptr;
  }

  /// Extract the sret type for a call or parameter.
  Type *getParamStructRetType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamStructRetType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamStructRetType(ArgNo);
    return nullptr;
  }

  /// Extract the elementtype type for a parameter.
  /// Note that elementtype() can only be applied to call arguments, not
  /// function declaration parameters.
  Type *getParamElementType(unsigned ArgNo) const {
    return Attrs.getParamElementType(ArgNo);
  }

  /// Extract the number of dereferenceable bytes for a call or
  /// parameter (0=unknown).
  uint64_t getRetDereferenceableBytes() const {
    uint64_t Bytes = Attrs.getRetDereferenceableBytes();
    if (const Function *F = getCalledFunction())
      Bytes = std::max(Bytes, F->getAttributes().getRetDereferenceableBytes());
    return Bytes;
  }

  /// Extract the number of dereferenceable bytes for a call or
  /// parameter (0=unknown).
  uint64_t getParamDereferenceableBytes(unsigned i) const {
    return Attrs.getParamDereferenceableBytes(i);
  }

  /// Extract the number of dereferenceable_or_null bytes for a call
  /// (0=unknown).
  uint64_t getRetDereferenceableOrNullBytes() const {
    uint64_t Bytes = Attrs.getRetDereferenceableOrNullBytes();
    if (const Function *F = getCalledFunction()) {
      Bytes = std::max(Bytes,
                       F->getAttributes().getRetDereferenceableOrNullBytes());
    }

    return Bytes;
  }

  /// Extract the number of dereferenceable_or_null bytes for a
  /// parameter (0=unknown).
  uint64_t getParamDereferenceableOrNullBytes(unsigned i) const {
    return Attrs.getParamDereferenceableOrNullBytes(i);
  }

  /// Extract a test mask for disallowed floating-point value classes for the
  /// return value.
  FPClassTest getRetNoFPClass() const;

  /// Extract a test mask for disallowed floating-point value classes for the
  /// parameter.
  FPClassTest getParamNoFPClass(unsigned i) const;

  /// If this return value has a range attribute, return the value range of the
  /// argument. Otherwise, std::nullopt is returned.
  std::optional<ConstantRange> getRange() const;

  /// Return true if the return value is known to be not null.
  /// This may be because it has the nonnull attribute, or because at least
  /// one byte is dereferenceable and the pointer is in addrspace(0).
  bool isReturnNonNull() const;

  /// Determine if the return value is marked with NoAlias attribute.
  bool returnDoesNotAlias() const {
    return Attrs.hasRetAttr(Attribute::NoAlias);
  }

  /// If one of the arguments has the 'returned' attribute, returns its
  /// operand value. Otherwise, return nullptr.
  Value *getReturnedArgOperand() const {
    return getArgOperandWithAttribute(Attribute::Returned);
  }

  /// If one of the arguments has the specified attribute, returns its
  /// operand value. Otherwise, return nullptr.
  Value *getArgOperandWithAttribute(Attribute::AttrKind Kind) const;

  /// Return true if the call should not be treated as a call to a
  /// builtin.
  bool isNoBuiltin() const {
    return hasFnAttrImpl(Attribute::NoBuiltin) &&
           !hasFnAttrImpl(Attribute::Builtin);
  }

  /// Determine if the call requires strict floating point semantics.
  bool isStrictFP() const { return hasFnAttr(Attribute::StrictFP); }

  /// Return true if the call should not be inlined.
  bool isNoInline() const { return hasFnAttr(Attribute::NoInline); }
  void setIsNoInline() { addFnAttr(Attribute::NoInline); }

  MemoryEffects getMemoryEffects() const;
  void setMemoryEffects(MemoryEffects ME);

  /// Determine if the call does not access memory.
  bool doesNotAccessMemory() const;
  void setDoesNotAccessMemory();

  /// Determine if the call does not access or only reads memory.
  bool onlyReadsMemory() const;
  void setOnlyReadsMemory();

  /// Determine if the call does not access or only writes memory.
  bool onlyWritesMemory() const;
  void setOnlyWritesMemory();

  /// Determine if the call can access memmory only using pointers based
  /// on its arguments.
  bool onlyAccessesArgMemory() const;
  void setOnlyAccessesArgMemory();

  /// Determine if the function may only access memory that is
  /// inaccessible from the IR.
  bool onlyAccessesInaccessibleMemory() const;
  void setOnlyAccessesInaccessibleMemory();

  /// Determine if the function may only access memory that is
  /// either inaccessible from the IR or pointed to by its arguments.
  bool onlyAccessesInaccessibleMemOrArgMem() const;
  void setOnlyAccessesInaccessibleMemOrArgMem();

  /// Determine if the call cannot return.
  bool doesNotReturn() const { return hasFnAttr(Attribute::NoReturn); }
  void setDoesNotReturn() { addFnAttr(Attribute::NoReturn); }

  /// Determine if the call should not perform indirect branch tracking.
  bool doesNoCfCheck() const { return hasFnAttr(Attribute::NoCfCheck); }

  /// Determine if the call cannot unwind.
  bool doesNotThrow() const { return hasFnAttr(Attribute::NoUnwind); }
  void setDoesNotThrow() { addFnAttr(Attribute::NoUnwind); }

  /// Determine if the invoke cannot be duplicated.
  bool cannotDuplicate() const { return hasFnAttr(Attribute::NoDuplicate); }
  void setCannotDuplicate() { addFnAttr(Attribute::NoDuplicate); }

  /// Determine if the call cannot be tail merged.
  bool cannotMerge() const { return hasFnAttr(Attribute::NoMerge); }
  void setCannotMerge() { addFnAttr(Attribute::NoMerge); }

  /// Determine if the invoke is convergent
  bool isConvergent() const { return hasFnAttr(Attribute::Convergent); }
  void setConvergent() { addFnAttr(Attribute::Convergent); }
  void setNotConvergent() { removeFnAttr(Attribute::Convergent); }

  /// Determine if the call returns a structure through first
  /// pointer argument.
  bool hasStructRetAttr() const {
    if (arg_empty())
      return false;

    // Be friendly and also check the callee.
    return paramHasAttr(0, Attribute::StructRet);
  }

  /// Determine if any call argument is an aggregate passed by value.
  bool hasByValArgument() const {
    return Attrs.hasAttrSomewhere(Attribute::ByVal);
  }

  ///@}
  // End of attribute API.

  /// \name Operand Bundle API
  ///
  /// This group of methods provides the API to access and manipulate operand
  /// bundles on this call.
  /// @{

  /// Return the number of operand bundles associated with this User.
  unsigned getNumOperandBundles() const {
    return std::distance(bundle_op_info_begin(), bundle_op_info_end());
  }

  /// Return true if this User has any operand bundles.
  bool hasOperandBundles() const { return getNumOperandBundles() != 0; }

  /// Return the index of the first bundle operand in the Use array.
  unsigned getBundleOperandsStartIndex() const {
    assert(hasOperandBundles() && "Don't call otherwise!");
    return bundle_op_info_begin()->Begin;
  }

  /// Return the index of the last bundle operand in the Use array.
  unsigned getBundleOperandsEndIndex() const {
    assert(hasOperandBundles() && "Don't call otherwise!");
    return bundle_op_info_end()[-1].End;
  }

  /// Return true if the operand at index \p Idx is a bundle operand.
  bool isBundleOperand(unsigned Idx) const {
    return hasOperandBundles() && Idx >= getBundleOperandsStartIndex() &&
           Idx < getBundleOperandsEndIndex();
  }

  /// Return true if the operand at index \p Idx is a bundle operand that has
  /// tag ID \p ID.
  bool isOperandBundleOfType(uint32_t ID, unsigned Idx) const {
    return isBundleOperand(Idx) &&
           getOperandBundleForOperand(Idx).getTagID() == ID;
  }

  /// Returns true if the use is a bundle operand.
  bool isBundleOperand(const Use *U) const {
    assert(this == U->getUser() &&
           "Only valid to query with a use of this instruction!");
    return hasOperandBundles() && isBundleOperand(U - op_begin());
  }
  bool isBundleOperand(Value::const_user_iterator UI) const {
    return isBundleOperand(&UI.getUse());
  }

  /// Return the total number operands (not operand bundles) used by
  /// every operand bundle in this OperandBundleUser.
  unsigned getNumTotalBundleOperands() const {
    if (!hasOperandBundles())
      return 0;

    unsigned Begin = getBundleOperandsStartIndex();
    unsigned End = getBundleOperandsEndIndex();

    assert(Begin <= End && "Should be!");
    return End - Begin;
  }

  /// Return the operand bundle at a specific index.
  OperandBundleUse getOperandBundleAt(unsigned Index) const {
    assert(Index < getNumOperandBundles() && "Index out of bounds!");
    return operandBundleFromBundleOpInfo(*(bundle_op_info_begin() + Index));
  }

  /// Return the number of operand bundles with the tag Name attached to
  /// this instruction.
  unsigned countOperandBundlesOfType(StringRef Name) const {
    unsigned Count = 0;
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i)
      if (getOperandBundleAt(i).getTagName() == Name)
        Count++;

    return Count;
  }

  /// Return the number of operand bundles with the tag ID attached to
  /// this instruction.
  unsigned countOperandBundlesOfType(uint32_t ID) const {
    unsigned Count = 0;
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i)
      if (getOperandBundleAt(i).getTagID() == ID)
        Count++;

    return Count;
  }

  /// Return an operand bundle by name, if present.
  ///
  /// It is an error to call this for operand bundle types that may have
  /// multiple instances of them on the same instruction.
  std::optional<OperandBundleUse> getOperandBundle(StringRef Name) const {
    assert(countOperandBundlesOfType(Name) < 2 && "Precondition violated!");

    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      OperandBundleUse U = getOperandBundleAt(i);
      if (U.getTagName() == Name)
        return U;
    }

    return std::nullopt;
  }

  /// Return an operand bundle by tag ID, if present.
  ///
  /// It is an error to call this for operand bundle types that may have
  /// multiple instances of them on the same instruction.
  std::optional<OperandBundleUse> getOperandBundle(uint32_t ID) const {
    assert(countOperandBundlesOfType(ID) < 2 && "Precondition violated!");

    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      OperandBundleUse U = getOperandBundleAt(i);
      if (U.getTagID() == ID)
        return U;
    }

    return std::nullopt;
  }

  /// Return the list of operand bundles attached to this instruction as
  /// a vector of OperandBundleDefs.
  ///
  /// This function copies the OperandBundeUse instances associated with this
  /// OperandBundleUser to a vector of OperandBundleDefs.  Note:
  /// OperandBundeUses and OperandBundleDefs are non-trivially *different*
  /// representations of operand bundles (see documentation above).
  void getOperandBundlesAsDefs(SmallVectorImpl<OperandBundleDef> &Defs) const;

  /// Return the operand bundle for the operand at index OpIdx.
  ///
  /// It is an error to call this with an OpIdx that does not correspond to an
  /// bundle operand.
  OperandBundleUse getOperandBundleForOperand(unsigned OpIdx) const {
    return operandBundleFromBundleOpInfo(getBundleOpInfoForOperand(OpIdx));
  }

  /// Return true if this operand bundle user has operand bundles that
  /// may read from the heap.
  bool hasReadingOperandBundles() const;

  /// Return true if this operand bundle user has operand bundles that
  /// may write to the heap.
  bool hasClobberingOperandBundles() const;

  /// Return true if the bundle operand at index \p OpIdx has the
  /// attribute \p A.
  bool bundleOperandHasAttr(unsigned OpIdx,  Attribute::AttrKind A) const {
    auto &BOI = getBundleOpInfoForOperand(OpIdx);
    auto OBU = operandBundleFromBundleOpInfo(BOI);
    return OBU.operandHasAttr(OpIdx - BOI.Begin, A);
  }

  /// Return true if \p Other has the same sequence of operand bundle
  /// tags with the same number of operands on each one of them as this
  /// OperandBundleUser.
  bool hasIdenticalOperandBundleSchema(const CallBase &Other) const {
    if (getNumOperandBundles() != Other.getNumOperandBundles())
      return false;

    return std::equal(bundle_op_info_begin(), bundle_op_info_end(),
                      Other.bundle_op_info_begin());
  }

  /// Return true if this operand bundle user contains operand bundles
  /// with tags other than those specified in \p IDs.
  bool hasOperandBundlesOtherThan(ArrayRef<uint32_t> IDs) const {
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      uint32_t ID = getOperandBundleAt(i).getTagID();
      if (!is_contained(IDs, ID))
        return true;
    }
    return false;
  }

  /// Used to keep track of an operand bundle.  See the main comment on
  /// OperandBundleUser above.
  struct BundleOpInfo {
    /// The operand bundle tag, interned by
    /// LLVMContextImpl::getOrInsertBundleTag.
    StringMapEntry<uint32_t> *Tag;

    /// The index in the Use& vector where operands for this operand
    /// bundle starts.
    uint32_t Begin;

    /// The index in the Use& vector where operands for this operand
    /// bundle ends.
    uint32_t End;

    bool operator==(const BundleOpInfo &Other) const {
      return Tag == Other.Tag && Begin == Other.Begin && End == Other.End;
    }
  };

  /// Simple helper function to map a BundleOpInfo to an
  /// OperandBundleUse.
  OperandBundleUse
  operandBundleFromBundleOpInfo(const BundleOpInfo &BOI) const {
    const auto *begin = op_begin();
    ArrayRef<Use> Inputs(begin + BOI.Begin, begin + BOI.End);
    return OperandBundleUse(BOI.Tag, Inputs);
  }

  using bundle_op_iterator = BundleOpInfo *;
  using const_bundle_op_iterator = const BundleOpInfo *;

  /// Return the start of the list of BundleOpInfo instances associated
  /// with this OperandBundleUser.
  ///
  /// OperandBundleUser uses the descriptor area co-allocated with the host User
  /// to store some meta information about which operands are "normal" operands,
  /// and which ones belong to some operand bundle.
  ///
  /// The layout of an operand bundle user is
  ///
  ///          +-----------uint32_t End-------------------------------------+
  ///          |                                                            |
  ///          |  +--------uint32_t Begin--------------------+              |
  ///          |  |                                          |              |
  ///          ^  ^                                          v              v
  ///  |------|------|----|----|----|----|----|---------|----|---------|----|-----
  ///  | BOI0 | BOI1 | .. | DU | U0 | U1 | .. | BOI0_U0 | .. | BOI1_U0 | .. | Un
  ///  |------|------|----|----|----|----|----|---------|----|---------|----|-----
  ///   v  v                                  ^              ^
  ///   |  |                                  |              |
  ///   |  +--------uint32_t Begin------------+              |
  ///   |                                                    |
  ///   +-----------uint32_t End-----------------------------+
  ///
  ///
  /// BOI0, BOI1 ... are descriptions of operand bundles in this User's use
  /// list. These descriptions are installed and managed by this class, and
  /// they're all instances of OperandBundleUser<T>::BundleOpInfo.
  ///
  /// DU is an additional descriptor installed by User's 'operator new' to keep
  /// track of the 'BOI0 ... BOIN' co-allocation.  OperandBundleUser does not
  /// access or modify DU in any way, it's an implementation detail private to
  /// User.
  ///
  /// The regular Use& vector for the User starts at U0.  The operand bundle
  /// uses are part of the Use& vector, just like normal uses.  In the diagram
  /// above, the operand bundle uses start at BOI0_U0.  Each instance of
  /// BundleOpInfo has information about a contiguous set of uses constituting
  /// an operand bundle, and the total set of operand bundle uses themselves
  /// form a contiguous set of uses (i.e. there are no gaps between uses
  /// corresponding to individual operand bundles).
  ///
  /// This class does not know the location of the set of operand bundle uses
  /// within the use list -- that is decided by the User using this class via
  /// the BeginIdx argument in populateBundleOperandInfos.
  ///
  /// Currently operand bundle users with hung-off operands are not supported.
  bundle_op_iterator bundle_op_info_begin() {
    if (!hasDescriptor())
      return nullptr;

    uint8_t *BytesBegin = getDescriptor().begin();
    return reinterpret_cast<bundle_op_iterator>(BytesBegin);
  }

  /// Return the start of the list of BundleOpInfo instances associated
  /// with this OperandBundleUser.
  const_bundle_op_iterator bundle_op_info_begin() const {
    auto *NonConstThis = const_cast<CallBase *>(this);
    return NonConstThis->bundle_op_info_begin();
  }

  /// Return the end of the list of BundleOpInfo instances associated
  /// with this OperandBundleUser.
  bundle_op_iterator bundle_op_info_end() {
    if (!hasDescriptor())
      return nullptr;

    uint8_t *BytesEnd = getDescriptor().end();
    return reinterpret_cast<bundle_op_iterator>(BytesEnd);
  }

  /// Return the end of the list of BundleOpInfo instances associated
  /// with this OperandBundleUser.
  const_bundle_op_iterator bundle_op_info_end() const {
    auto *NonConstThis = const_cast<CallBase *>(this);
    return NonConstThis->bundle_op_info_end();
  }

  /// Return the range [\p bundle_op_info_begin, \p bundle_op_info_end).
  iterator_range<bundle_op_iterator> bundle_op_infos() {
    return make_range(bundle_op_info_begin(), bundle_op_info_end());
  }

  /// Return the range [\p bundle_op_info_begin, \p bundle_op_info_end).
  iterator_range<const_bundle_op_iterator> bundle_op_infos() const {
    return make_range(bundle_op_info_begin(), bundle_op_info_end());
  }

  /// Populate the BundleOpInfo instances and the Use& vector from \p
  /// Bundles.  Return the op_iterator pointing to the Use& one past the last
  /// last bundle operand use.
  ///
  /// Each \p OperandBundleDef instance is tracked by a OperandBundleInfo
  /// instance allocated in this User's descriptor.
  op_iterator populateBundleOperandInfos(ArrayRef<OperandBundleDef> Bundles,
                                         const unsigned BeginIndex);

  /// Return true if the call has deopt state bundle.
  bool hasDeoptState() const {
    return getOperandBundle(LLVMContext::OB_deopt).has_value();
  }

public:
  /// Return the BundleOpInfo for the operand at index OpIdx.
  ///
  /// It is an error to call this with an OpIdx that does not correspond to an
  /// bundle operand.
  BundleOpInfo &getBundleOpInfoForOperand(unsigned OpIdx);
  const BundleOpInfo &getBundleOpInfoForOperand(unsigned OpIdx) const {
    return const_cast<CallBase *>(this)->getBundleOpInfoForOperand(OpIdx);
  }

protected:
  /// Return the total number of values used in \p Bundles.
  static unsigned CountBundleInputs(ArrayRef<OperandBundleDef> Bundles) {
    unsigned Total = 0;
    for (const auto &B : Bundles)
      Total += B.input_size();
    return Total;
  }

  /// @}
  // End of operand bundle API.

private:
  bool hasFnAttrOnCalledFunction(Attribute::AttrKind Kind) const;
  bool hasFnAttrOnCalledFunction(StringRef Kind) const;

  template <typename AttrKind> bool hasFnAttrImpl(AttrKind Kind) const {
    if (Attrs.hasFnAttr(Kind))
      return true;

    return hasFnAttrOnCalledFunction(Kind);
  }
  template <typename AK> Attribute getFnAttrOnCalledFunction(AK Kind) const;
  template <typename AK>
  Attribute getParamAttrOnCalledFunction(unsigned ArgNo, AK Kind) const;

  /// Determine whether the return value has the given attribute. Supports
  /// Attribute::AttrKind and StringRef as \p AttrKind types.
  template <typename AttrKind> bool hasRetAttrImpl(AttrKind Kind) const {
    if (Attrs.hasRetAttr(Kind))
      return true;

    // Look at the callee, if available.
    if (const Function *F = getCalledFunction())
      return F->getAttributes().hasRetAttr(Kind);
    return false;
  }
};

template <>
struct OperandTraits<CallBase> : public VariadicOperandTraits<CallBase, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CallBase, Value)

//===----------------------------------------------------------------------===//
//                           FuncletPadInst Class
//===----------------------------------------------------------------------===//
class FuncletPadInst : public Instruction {
private:
  FuncletPadInst(const FuncletPadInst &CPI);

  explicit FuncletPadInst(Instruction::FuncletPadOps Op, Value *ParentPad,
                          ArrayRef<Value *> Args, unsigned Values,
                          const Twine &NameStr, InsertPosition InsertBefore);

  void init(Value *ParentPad, ArrayRef<Value *> Args, const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;
  friend class CatchPadInst;
  friend class CleanupPadInst;

  FuncletPadInst *cloneImpl() const;

public:
  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// arg_size - Return the number of funcletpad arguments.
  ///
  unsigned arg_size() const { return getNumOperands() - 1; }

  /// Convenience accessors

  /// Return the outer EH-pad this funclet is nested within.
  ///
  /// Note: This returns the associated CatchSwitchInst if this FuncletPadInst
  /// is a CatchPadInst.
  Value *getParentPad() const { return Op<-1>(); }
  void setParentPad(Value *ParentPad) {
    assert(ParentPad);
    Op<-1>() = ParentPad;
  }

  /// getArgOperand/setArgOperand - Return/set the i-th funcletpad argument.
  ///
  Value *getArgOperand(unsigned i) const { return getOperand(i); }
  void setArgOperand(unsigned i, Value *v) { setOperand(i, v); }

  /// arg_operands - iteration adapter for range-for loops.
  op_range arg_operands() { return op_range(op_begin(), op_end() - 1); }

  /// arg_operands - iteration adapter for range-for loops.
  const_op_range arg_operands() const {
    return const_op_range(op_begin(), op_end() - 1);
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) { return I->isFuncletPad(); }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<FuncletPadInst>
    : public VariadicOperandTraits<FuncletPadInst, /*MINARITY=*/1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(FuncletPadInst, Value)

} // end namespace llvm

#endif // LLVM_IR_INSTRTYPES_H
