//===- InstructionCombining.cpp - Combine multiple instructions -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// InstructionCombining - Combine instructions to form fewer, simple
// instructions.  This pass does not modify the CFG.  This pass is where
// algebraic simplification happens.
//
// This pass combines things like:
//    %Y = add i32 %X, 1
//    %Z = add i32 %Y, 1
// into:
//    %Z = add i32 %X, 2
//
// This is a simple worklist driven algorithm.
//
// This pass guarantees that the following canonicalizations are performed on
// the program:
//    1. If a binary operator has a constant operand, it is moved to the RHS
//    2. Bitwise operators with constant operands are always grouped so that
//       shifts are performed first, then or's, then and's, then xor's.
//    3. Compare instructions are converted from <,>,<=,>= to ==,!= if possible
//    4. All cmp instructions on boolean values are replaced with logical ops
//    5. add X, X is represented as (X*2) => (X << 1)
//    6. Multiplies with a power-of-two constant argument are transformed into
//       shifts.
//   ... etc.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LazyBlockFrequencyInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#define DEBUG_TYPE "instcombine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"
#include <optional>

using namespace llvm;
using namespace llvm::PatternMatch;

STATISTIC(NumWorklistIterations,
          "Number of instruction combining iterations performed");
STATISTIC(NumOneIteration, "Number of functions with one iteration");
STATISTIC(NumTwoIterations, "Number of functions with two iterations");
STATISTIC(NumThreeIterations, "Number of functions with three iterations");
STATISTIC(NumFourOrMoreIterations,
          "Number of functions with four or more iterations");

STATISTIC(NumCombined , "Number of insts combined");
STATISTIC(NumConstProp, "Number of constant folds");
STATISTIC(NumDeadInst , "Number of dead inst eliminated");
STATISTIC(NumSunkInst , "Number of instructions sunk");
STATISTIC(NumExpand,    "Number of expansions");
STATISTIC(NumFactor   , "Number of factorizations");
STATISTIC(NumReassoc  , "Number of reassociations");
DEBUG_COUNTER(VisitCounter, "instcombine-visit",
              "Controls which instructions are visited");

static cl::opt<bool>
EnableCodeSinking("instcombine-code-sinking", cl::desc("Enable code sinking"),
                                              cl::init(true));

static cl::opt<unsigned> MaxSinkNumUsers(
    "instcombine-max-sink-users", cl::init(32),
    cl::desc("Maximum number of undroppable users for instruction sinking"));

static cl::opt<unsigned>
MaxArraySize("instcombine-maxarray-size", cl::init(1024),
             cl::desc("Maximum array size considered when doing a combine"));

// FIXME: Remove this flag when it is no longer necessary to convert
// llvm.dbg.declare to avoid inaccurate debug info. Setting this to false
// increases variable availability at the cost of accuracy. Variables that
// cannot be promoted by mem2reg or SROA will be described as living in memory
// for their entire lifetime. However, passes like DSE and instcombine can
// delete stores to the alloca, leading to misleading and inaccurate debug
// information. This flag can be removed when those passes are fixed.
static cl::opt<unsigned> ShouldLowerDbgDeclare("instcombine-lower-dbg-declare",
                                               cl::Hidden, cl::init(true));

std::optional<Instruction *>
InstCombiner::targetInstCombineIntrinsic(IntrinsicInst &II) {
  // Handle target specific intrinsics
  if (II.getCalledFunction()->isTargetIntrinsic()) {
    return TTI.instCombineIntrinsic(*this, II);
  }
  return std::nullopt;
}

std::optional<Value *> InstCombiner::targetSimplifyDemandedUseBitsIntrinsic(
    IntrinsicInst &II, APInt DemandedMask, KnownBits &Known,
    bool &KnownBitsComputed) {
  // Handle target specific intrinsics
  if (II.getCalledFunction()->isTargetIntrinsic()) {
    return TTI.simplifyDemandedUseBitsIntrinsic(*this, II, DemandedMask, Known,
                                                KnownBitsComputed);
  }
  return std::nullopt;
}

std::optional<Value *> InstCombiner::targetSimplifyDemandedVectorEltsIntrinsic(
    IntrinsicInst &II, APInt DemandedElts, APInt &PoisonElts,
    APInt &PoisonElts2, APInt &PoisonElts3,
    std::function<void(Instruction *, unsigned, APInt, APInt &)>
        SimplifyAndSetOp) {
  // Handle target specific intrinsics
  if (II.getCalledFunction()->isTargetIntrinsic()) {
    return TTI.simplifyDemandedVectorEltsIntrinsic(
        *this, II, DemandedElts, PoisonElts, PoisonElts2, PoisonElts3,
        SimplifyAndSetOp);
  }
  return std::nullopt;
}

bool InstCombiner::isValidAddrSpaceCast(unsigned FromAS, unsigned ToAS) const {
  return TTI.isValidAddrSpaceCast(FromAS, ToAS);
}

Value *InstCombinerImpl::EmitGEPOffset(GEPOperator *GEP, bool RewriteGEP) {
  if (!RewriteGEP)
    return llvm::emitGEPOffset(&Builder, DL, GEP);

  IRBuilderBase::InsertPointGuard Guard(Builder);
  auto *Inst = dyn_cast<Instruction>(GEP);
  if (Inst)
    Builder.SetInsertPoint(Inst);

  Value *Offset = EmitGEPOffset(GEP);
  // If a non-trivial GEP has other uses, rewrite it to avoid duplicating
  // the offset arithmetic.
  if (Inst && !GEP->hasOneUse() && !GEP->hasAllConstantIndices() &&
      !GEP->getSourceElementType()->isIntegerTy(8)) {
    replaceInstUsesWith(
        *Inst, Builder.CreateGEP(Builder.getInt8Ty(), GEP->getPointerOperand(),
                                 Offset, "", GEP->getNoWrapFlags()));
    eraseInstFromFunction(*Inst);
  }
  return Offset;
}

/// Legal integers and common types are considered desirable. This is used to
/// avoid creating instructions with types that may not be supported well by the
/// the backend.
/// NOTE: This treats i8, i16 and i32 specially because they are common
///       types in frontend languages.
bool InstCombinerImpl::isDesirableIntType(unsigned BitWidth) const {
  switch (BitWidth) {
  case 8:
  case 16:
  case 32:
    return true;
  default:
    return DL.isLegalInteger(BitWidth);
  }
}

/// Return true if it is desirable to convert an integer computation from a
/// given bit width to a new bit width.
/// We don't want to convert from a legal or desirable type (like i8) to an
/// illegal type or from a smaller to a larger illegal type. A width of '1'
/// is always treated as a desirable type because i1 is a fundamental type in
/// IR, and there are many specialized optimizations for i1 types.
/// Common/desirable widths are equally treated as legal to convert to, in
/// order to open up more combining opportunities.
bool InstCombinerImpl::shouldChangeType(unsigned FromWidth,
                                        unsigned ToWidth) const {
  bool FromLegal = FromWidth == 1 || DL.isLegalInteger(FromWidth);
  bool ToLegal = ToWidth == 1 || DL.isLegalInteger(ToWidth);

  // Convert to desirable widths even if they are not legal types.
  // Only shrink types, to prevent infinite loops.
  if (ToWidth < FromWidth && isDesirableIntType(ToWidth))
    return true;

  // If this is a legal or desiable integer from type, and the result would be
  // an illegal type, don't do the transformation.
  if ((FromLegal || isDesirableIntType(FromWidth)) && !ToLegal)
    return false;

  // Otherwise, if both are illegal, do not increase the size of the result. We
  // do allow things like i160 -> i64, but not i64 -> i160.
  if (!FromLegal && !ToLegal && ToWidth > FromWidth)
    return false;

  return true;
}

/// Return true if it is desirable to convert a computation from 'From' to 'To'.
/// We don't want to convert from a legal to an illegal type or from a smaller
/// to a larger illegal type. i1 is always treated as a legal type because it is
/// a fundamental type in IR, and there are many specialized optimizations for
/// i1 types.
bool InstCombinerImpl::shouldChangeType(Type *From, Type *To) const {
  // TODO: This could be extended to allow vectors. Datalayout changes might be
  // needed to properly support that.
  if (!From->isIntegerTy() || !To->isIntegerTy())
    return false;

  unsigned FromWidth = From->getPrimitiveSizeInBits();
  unsigned ToWidth = To->getPrimitiveSizeInBits();
  return shouldChangeType(FromWidth, ToWidth);
}

// Return true, if No Signed Wrap should be maintained for I.
// The No Signed Wrap flag can be kept if the operation "B (I.getOpcode) C",
// where both B and C should be ConstantInts, results in a constant that does
// not overflow. This function only handles the Add and Sub opcodes. For
// all other opcodes, the function conservatively returns false.
static bool maintainNoSignedWrap(BinaryOperator &I, Value *B, Value *C) {
  auto *OBO = dyn_cast<OverflowingBinaryOperator>(&I);
  if (!OBO || !OBO->hasNoSignedWrap())
    return false;

  // We reason about Add and Sub Only.
  Instruction::BinaryOps Opcode = I.getOpcode();
  if (Opcode != Instruction::Add && Opcode != Instruction::Sub)
    return false;

  const APInt *BVal, *CVal;
  if (!match(B, m_APInt(BVal)) || !match(C, m_APInt(CVal)))
    return false;

  bool Overflow = false;
  if (Opcode == Instruction::Add)
    (void)BVal->sadd_ov(*CVal, Overflow);
  else
    (void)BVal->ssub_ov(*CVal, Overflow);

  return !Overflow;
}

static bool hasNoUnsignedWrap(BinaryOperator &I) {
  auto *OBO = dyn_cast<OverflowingBinaryOperator>(&I);
  return OBO && OBO->hasNoUnsignedWrap();
}

static bool hasNoSignedWrap(BinaryOperator &I) {
  auto *OBO = dyn_cast<OverflowingBinaryOperator>(&I);
  return OBO && OBO->hasNoSignedWrap();
}

/// Conservatively clears subclassOptionalData after a reassociation or
/// commutation. We preserve fast-math flags when applicable as they can be
/// preserved.
static void ClearSubclassDataAfterReassociation(BinaryOperator &I) {
  FPMathOperator *FPMO = dyn_cast<FPMathOperator>(&I);
  if (!FPMO) {
    I.clearSubclassOptionalData();
    return;
  }

  FastMathFlags FMF = I.getFastMathFlags();
  I.clearSubclassOptionalData();
  I.setFastMathFlags(FMF);
}

/// Combine constant operands of associative operations either before or after a
/// cast to eliminate one of the associative operations:
/// (op (cast (op X, C2)), C1) --> (cast (op X, op (C1, C2)))
/// (op (cast (op X, C2)), C1) --> (op (cast X), op (C1, C2))
static bool simplifyAssocCastAssoc(BinaryOperator *BinOp1,
                                   InstCombinerImpl &IC) {
  auto *Cast = dyn_cast<CastInst>(BinOp1->getOperand(0));
  if (!Cast || !Cast->hasOneUse())
    return false;

  // TODO: Enhance logic for other casts and remove this check.
  auto CastOpcode = Cast->getOpcode();
  if (CastOpcode != Instruction::ZExt)
    return false;

  // TODO: Enhance logic for other BinOps and remove this check.
  if (!BinOp1->isBitwiseLogicOp())
    return false;

  auto AssocOpcode = BinOp1->getOpcode();
  auto *BinOp2 = dyn_cast<BinaryOperator>(Cast->getOperand(0));
  if (!BinOp2 || !BinOp2->hasOneUse() || BinOp2->getOpcode() != AssocOpcode)
    return false;

  Constant *C1, *C2;
  if (!match(BinOp1->getOperand(1), m_Constant(C1)) ||
      !match(BinOp2->getOperand(1), m_Constant(C2)))
    return false;

  // TODO: This assumes a zext cast.
  // Eg, if it was a trunc, we'd cast C1 to the source type because casting C2
  // to the destination type might lose bits.

  // Fold the constants together in the destination type:
  // (op (cast (op X, C2)), C1) --> (op (cast X), FoldedC)
  const DataLayout &DL = IC.getDataLayout();
  Type *DestTy = C1->getType();
  Constant *CastC2 = ConstantFoldCastOperand(CastOpcode, C2, DestTy, DL);
  if (!CastC2)
    return false;
  Constant *FoldedC = ConstantFoldBinaryOpOperands(AssocOpcode, C1, CastC2, DL);
  if (!FoldedC)
    return false;

  IC.replaceOperand(*Cast, 0, BinOp2->getOperand(0));
  IC.replaceOperand(*BinOp1, 1, FoldedC);
  BinOp1->dropPoisonGeneratingFlags();
  Cast->dropPoisonGeneratingFlags();
  return true;
}

// Simplifies IntToPtr/PtrToInt RoundTrip Cast.
// inttoptr ( ptrtoint (x) ) --> x
Value *InstCombinerImpl::simplifyIntToPtrRoundTripCast(Value *Val) {
  auto *IntToPtr = dyn_cast<IntToPtrInst>(Val);
  if (IntToPtr && DL.getTypeSizeInBits(IntToPtr->getDestTy()) ==
                      DL.getTypeSizeInBits(IntToPtr->getSrcTy())) {
    auto *PtrToInt = dyn_cast<PtrToIntInst>(IntToPtr->getOperand(0));
    Type *CastTy = IntToPtr->getDestTy();
    if (PtrToInt &&
        CastTy->getPointerAddressSpace() ==
            PtrToInt->getSrcTy()->getPointerAddressSpace() &&
        DL.getTypeSizeInBits(PtrToInt->getSrcTy()) ==
            DL.getTypeSizeInBits(PtrToInt->getDestTy()))
      return PtrToInt->getOperand(0);
  }
  return nullptr;
}

/// This performs a few simplifications for operators that are associative or
/// commutative:
///
///  Commutative operators:
///
///  1. Order operands such that they are listed from right (least complex) to
///     left (most complex).  This puts constants before unary operators before
///     binary operators.
///
///  Associative operators:
///
///  2. Transform: "(A op B) op C" ==> "A op (B op C)" if "B op C" simplifies.
///  3. Transform: "A op (B op C)" ==> "(A op B) op C" if "A op B" simplifies.
///
///  Associative and commutative operators:
///
///  4. Transform: "(A op B) op C" ==> "(C op A) op B" if "C op A" simplifies.
///  5. Transform: "A op (B op C)" ==> "B op (C op A)" if "C op A" simplifies.
///  6. Transform: "(A op C1) op (B op C2)" ==> "(A op B) op (C1 op C2)"
///     if C1 and C2 are constants.
bool InstCombinerImpl::SimplifyAssociativeOrCommutative(BinaryOperator &I) {
  Instruction::BinaryOps Opcode = I.getOpcode();
  bool Changed = false;

  do {
    // Order operands such that they are listed from right (least complex) to
    // left (most complex).  This puts constants before unary operators before
    // binary operators.
    if (I.isCommutative() && getComplexity(I.getOperand(0)) <
        getComplexity(I.getOperand(1)))
      Changed = !I.swapOperands();

    if (I.isCommutative()) {
      if (auto Pair = matchSymmetricPair(I.getOperand(0), I.getOperand(1))) {
        replaceOperand(I, 0, Pair->first);
        replaceOperand(I, 1, Pair->second);
        Changed = true;
      }
    }

    BinaryOperator *Op0 = dyn_cast<BinaryOperator>(I.getOperand(0));
    BinaryOperator *Op1 = dyn_cast<BinaryOperator>(I.getOperand(1));

    if (I.isAssociative()) {
      // Transform: "(A op B) op C" ==> "A op (B op C)" if "B op C" simplifies.
      if (Op0 && Op0->getOpcode() == Opcode) {
        Value *A = Op0->getOperand(0);
        Value *B = Op0->getOperand(1);
        Value *C = I.getOperand(1);

        // Does "B op C" simplify?
        if (Value *V = simplifyBinOp(Opcode, B, C, SQ.getWithInstruction(&I))) {
          // It simplifies to V.  Form "A op V".
          replaceOperand(I, 0, A);
          replaceOperand(I, 1, V);
          bool IsNUW = hasNoUnsignedWrap(I) && hasNoUnsignedWrap(*Op0);
          bool IsNSW = maintainNoSignedWrap(I, B, C) && hasNoSignedWrap(*Op0);

          // Conservatively clear all optional flags since they may not be
          // preserved by the reassociation. Reset nsw/nuw based on the above
          // analysis.
          ClearSubclassDataAfterReassociation(I);

          // Note: this is only valid because SimplifyBinOp doesn't look at
          // the operands to Op0.
          if (IsNUW)
            I.setHasNoUnsignedWrap(true);

          if (IsNSW)
            I.setHasNoSignedWrap(true);

          Changed = true;
          ++NumReassoc;
          continue;
        }
      }

      // Transform: "A op (B op C)" ==> "(A op B) op C" if "A op B" simplifies.
      if (Op1 && Op1->getOpcode() == Opcode) {
        Value *A = I.getOperand(0);
        Value *B = Op1->getOperand(0);
        Value *C = Op1->getOperand(1);

        // Does "A op B" simplify?
        if (Value *V = simplifyBinOp(Opcode, A, B, SQ.getWithInstruction(&I))) {
          // It simplifies to V.  Form "V op C".
          replaceOperand(I, 0, V);
          replaceOperand(I, 1, C);
          // Conservatively clear the optional flags, since they may not be
          // preserved by the reassociation.
          ClearSubclassDataAfterReassociation(I);
          Changed = true;
          ++NumReassoc;
          continue;
        }
      }
    }

    if (I.isAssociative() && I.isCommutative()) {
      if (simplifyAssocCastAssoc(&I, *this)) {
        Changed = true;
        ++NumReassoc;
        continue;
      }

      // Transform: "(A op B) op C" ==> "(C op A) op B" if "C op A" simplifies.
      if (Op0 && Op0->getOpcode() == Opcode) {
        Value *A = Op0->getOperand(0);
        Value *B = Op0->getOperand(1);
        Value *C = I.getOperand(1);

        // Does "C op A" simplify?
        if (Value *V = simplifyBinOp(Opcode, C, A, SQ.getWithInstruction(&I))) {
          // It simplifies to V.  Form "V op B".
          replaceOperand(I, 0, V);
          replaceOperand(I, 1, B);
          // Conservatively clear the optional flags, since they may not be
          // preserved by the reassociation.
          ClearSubclassDataAfterReassociation(I);
          Changed = true;
          ++NumReassoc;
          continue;
        }
      }

      // Transform: "A op (B op C)" ==> "B op (C op A)" if "C op A" simplifies.
      if (Op1 && Op1->getOpcode() == Opcode) {
        Value *A = I.getOperand(0);
        Value *B = Op1->getOperand(0);
        Value *C = Op1->getOperand(1);

        // Does "C op A" simplify?
        if (Value *V = simplifyBinOp(Opcode, C, A, SQ.getWithInstruction(&I))) {
          // It simplifies to V.  Form "B op V".
          replaceOperand(I, 0, B);
          replaceOperand(I, 1, V);
          // Conservatively clear the optional flags, since they may not be
          // preserved by the reassociation.
          ClearSubclassDataAfterReassociation(I);
          Changed = true;
          ++NumReassoc;
          continue;
        }
      }

      // Transform: "(A op C1) op (B op C2)" ==> "(A op B) op (C1 op C2)"
      // if C1 and C2 are constants.
      Value *A, *B;
      Constant *C1, *C2, *CRes;
      if (Op0 && Op1 &&
          Op0->getOpcode() == Opcode && Op1->getOpcode() == Opcode &&
          match(Op0, m_OneUse(m_BinOp(m_Value(A), m_Constant(C1)))) &&
          match(Op1, m_OneUse(m_BinOp(m_Value(B), m_Constant(C2)))) &&
          (CRes = ConstantFoldBinaryOpOperands(Opcode, C1, C2, DL))) {
        bool IsNUW = hasNoUnsignedWrap(I) &&
           hasNoUnsignedWrap(*Op0) &&
           hasNoUnsignedWrap(*Op1);
         BinaryOperator *NewBO = (IsNUW && Opcode == Instruction::Add) ?
           BinaryOperator::CreateNUW(Opcode, A, B) :
           BinaryOperator::Create(Opcode, A, B);

         if (isa<FPMathOperator>(NewBO)) {
           FastMathFlags Flags = I.getFastMathFlags() &
                                 Op0->getFastMathFlags() &
                                 Op1->getFastMathFlags();
           NewBO->setFastMathFlags(Flags);
        }
        InsertNewInstWith(NewBO, I.getIterator());
        NewBO->takeName(Op1);
        replaceOperand(I, 0, NewBO);
        replaceOperand(I, 1, CRes);
        // Conservatively clear the optional flags, since they may not be
        // preserved by the reassociation.
        ClearSubclassDataAfterReassociation(I);
        if (IsNUW)
          I.setHasNoUnsignedWrap(true);

        Changed = true;
        continue;
      }
    }

    // No further simplifications.
    return Changed;
  } while (true);
}

/// Return whether "X LOp (Y ROp Z)" is always equal to
/// "(X LOp Y) ROp (X LOp Z)".
static bool leftDistributesOverRight(Instruction::BinaryOps LOp,
                                     Instruction::BinaryOps ROp) {
  // X & (Y | Z) <--> (X & Y) | (X & Z)
  // X & (Y ^ Z) <--> (X & Y) ^ (X & Z)
  if (LOp == Instruction::And)
    return ROp == Instruction::Or || ROp == Instruction::Xor;

  // X | (Y & Z) <--> (X | Y) & (X | Z)
  if (LOp == Instruction::Or)
    return ROp == Instruction::And;

  // X * (Y + Z) <--> (X * Y) + (X * Z)
  // X * (Y - Z) <--> (X * Y) - (X * Z)
  if (LOp == Instruction::Mul)
    return ROp == Instruction::Add || ROp == Instruction::Sub;

  return false;
}

/// Return whether "(X LOp Y) ROp Z" is always equal to
/// "(X ROp Z) LOp (Y ROp Z)".
static bool rightDistributesOverLeft(Instruction::BinaryOps LOp,
                                     Instruction::BinaryOps ROp) {
  if (Instruction::isCommutative(ROp))
    return leftDistributesOverRight(ROp, LOp);

  // (X {&|^} Y) >> Z <--> (X >> Z) {&|^} (Y >> Z) for all shifts.
  return Instruction::isBitwiseLogicOp(LOp) && Instruction::isShift(ROp);

  // TODO: It would be nice to handle division, aka "(X + Y)/Z = X/Z + Y/Z",
  // but this requires knowing that the addition does not overflow and other
  // such subtleties.
}

/// This function returns identity value for given opcode, which can be used to
/// factor patterns like (X * 2) + X ==> (X * 2) + (X * 1) ==> X * (2 + 1).
static Value *getIdentityValue(Instruction::BinaryOps Opcode, Value *V) {
  if (isa<Constant>(V))
    return nullptr;

  return ConstantExpr::getBinOpIdentity(Opcode, V->getType());
}

/// This function predicates factorization using distributive laws. By default,
/// it just returns the 'Op' inputs. But for special-cases like
/// 'add(shl(X, 5), ...)', this function will have TopOpcode == Instruction::Add
/// and Op = shl(X, 5). The 'shl' is treated as the more general 'mul X, 32' to
/// allow more factorization opportunities.
static Instruction::BinaryOps
getBinOpsForFactorization(Instruction::BinaryOps TopOpcode, BinaryOperator *Op,
                          Value *&LHS, Value *&RHS, BinaryOperator *OtherOp) {
  assert(Op && "Expected a binary operator");
  LHS = Op->getOperand(0);
  RHS = Op->getOperand(1);
  if (TopOpcode == Instruction::Add || TopOpcode == Instruction::Sub) {
    Constant *C;
    if (match(Op, m_Shl(m_Value(), m_ImmConstant(C)))) {
      // X << C --> X * (1 << C)
      RHS = ConstantFoldBinaryInstruction(
          Instruction::Shl, ConstantInt::get(Op->getType(), 1), C);
      assert(RHS && "Constant folding of immediate constants failed");
      return Instruction::Mul;
    }
    // TODO: We can add other conversions e.g. shr => div etc.
  }
  if (Instruction::isBitwiseLogicOp(TopOpcode)) {
    if (OtherOp && OtherOp->getOpcode() == Instruction::AShr &&
        match(Op, m_LShr(m_NonNegative(), m_Value()))) {
      // lshr nneg C, X --> ashr nneg C, X
      return Instruction::AShr;
    }
  }
  return Op->getOpcode();
}

/// This tries to simplify binary operations by factorizing out common terms
/// (e. g. "(A*B)+(A*C)" -> "A*(B+C)").
static Value *tryFactorization(BinaryOperator &I, const SimplifyQuery &SQ,
                               InstCombiner::BuilderTy &Builder,
                               Instruction::BinaryOps InnerOpcode, Value *A,
                               Value *B, Value *C, Value *D) {
  assert(A && B && C && D && "All values must be provided");

  Value *V = nullptr;
  Value *RetVal = nullptr;
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  Instruction::BinaryOps TopLevelOpcode = I.getOpcode();

  // Does "X op' Y" always equal "Y op' X"?
  bool InnerCommutative = Instruction::isCommutative(InnerOpcode);

  // Does "X op' (Y op Z)" always equal "(X op' Y) op (X op' Z)"?
  if (leftDistributesOverRight(InnerOpcode, TopLevelOpcode)) {
    // Does the instruction have the form "(A op' B) op (A op' D)" or, in the
    // commutative case, "(A op' B) op (C op' A)"?
    if (A == C || (InnerCommutative && A == D)) {
      if (A != C)
        std::swap(C, D);
      // Consider forming "A op' (B op D)".
      // If "B op D" simplifies then it can be formed with no cost.
      V = simplifyBinOp(TopLevelOpcode, B, D, SQ.getWithInstruction(&I));

      // If "B op D" doesn't simplify then only go on if one of the existing
      // operations "A op' B" and "C op' D" will be zapped as no longer used.
      if (!V && (LHS->hasOneUse() || RHS->hasOneUse()))
        V = Builder.CreateBinOp(TopLevelOpcode, B, D, RHS->getName());
      if (V)
        RetVal = Builder.CreateBinOp(InnerOpcode, A, V);
    }
  }

  // Does "(X op Y) op' Z" always equal "(X op' Z) op (Y op' Z)"?
  if (!RetVal && rightDistributesOverLeft(TopLevelOpcode, InnerOpcode)) {
    // Does the instruction have the form "(A op' B) op (C op' B)" or, in the
    // commutative case, "(A op' B) op (B op' D)"?
    if (B == D || (InnerCommutative && B == C)) {
      if (B != D)
        std::swap(C, D);
      // Consider forming "(A op C) op' B".
      // If "A op C" simplifies then it can be formed with no cost.
      V = simplifyBinOp(TopLevelOpcode, A, C, SQ.getWithInstruction(&I));

      // If "A op C" doesn't simplify then only go on if one of the existing
      // operations "A op' B" and "C op' D" will be zapped as no longer used.
      if (!V && (LHS->hasOneUse() || RHS->hasOneUse()))
        V = Builder.CreateBinOp(TopLevelOpcode, A, C, LHS->getName());
      if (V)
        RetVal = Builder.CreateBinOp(InnerOpcode, V, B);
    }
  }

  if (!RetVal)
    return nullptr;

  ++NumFactor;
  RetVal->takeName(&I);

  // Try to add no-overflow flags to the final value.
  if (isa<OverflowingBinaryOperator>(RetVal)) {
    bool HasNSW = false;
    bool HasNUW = false;
    if (isa<OverflowingBinaryOperator>(&I)) {
      HasNSW = I.hasNoSignedWrap();
      HasNUW = I.hasNoUnsignedWrap();
    }
    if (auto *LOBO = dyn_cast<OverflowingBinaryOperator>(LHS)) {
      HasNSW &= LOBO->hasNoSignedWrap();
      HasNUW &= LOBO->hasNoUnsignedWrap();
    }

    if (auto *ROBO = dyn_cast<OverflowingBinaryOperator>(RHS)) {
      HasNSW &= ROBO->hasNoSignedWrap();
      HasNUW &= ROBO->hasNoUnsignedWrap();
    }

    if (TopLevelOpcode == Instruction::Add && InnerOpcode == Instruction::Mul) {
      // We can propagate 'nsw' if we know that
      //  %Y = mul nsw i16 %X, C
      //  %Z = add nsw i16 %Y, %X
      // =>
      //  %Z = mul nsw i16 %X, C+1
      //
      // iff C+1 isn't INT_MIN
      const APInt *CInt;
      if (match(V, m_APInt(CInt)) && !CInt->isMinSignedValue())
        cast<Instruction>(RetVal)->setHasNoSignedWrap(HasNSW);

      // nuw can be propagated with any constant or nuw value.
      cast<Instruction>(RetVal)->setHasNoUnsignedWrap(HasNUW);
    }
  }
  return RetVal;
}

// If `I` has one Const operand and the other matches `(ctpop (not x))`,
// replace `(ctpop (not x))` with `(sub nuw nsw BitWidth(x), (ctpop x))`.
// This is only useful is the new subtract can fold so we only handle the
// following cases:
//    1) (add/sub/disjoint_or C, (ctpop (not x))
//        -> (add/sub/disjoint_or C', (ctpop x))
//    1) (cmp pred C, (ctpop (not x))
//        -> (cmp pred C', (ctpop x))
Instruction *InstCombinerImpl::tryFoldInstWithCtpopWithNot(Instruction *I) {
  unsigned Opc = I->getOpcode();
  unsigned ConstIdx = 1;
  switch (Opc) {
  default:
    return nullptr;
    // (ctpop (not x)) <-> (sub nuw nsw BitWidth(x) - (ctpop x))
    // We can fold the BitWidth(x) with add/sub/icmp as long the other operand
    // is constant.
  case Instruction::Sub:
    ConstIdx = 0;
    break;
  case Instruction::ICmp:
    // Signed predicates aren't correct in some edge cases like for i2 types, as
    // well since (ctpop x) is known [0, log2(BitWidth(x))] almost all signed
    // comparisons against it are simplfied to unsigned.
    if (cast<ICmpInst>(I)->isSigned())
      return nullptr;
    break;
  case Instruction::Or:
    if (!match(I, m_DisjointOr(m_Value(), m_Value())))
      return nullptr;
    [[fallthrough]];
  case Instruction::Add:
    break;
  }

  Value *Op;
  // Find ctpop.
  if (!match(I->getOperand(1 - ConstIdx),
             m_OneUse(m_Intrinsic<Intrinsic::ctpop>(m_Value(Op)))))
    return nullptr;

  Constant *C;
  // Check other operand is ImmConstant.
  if (!match(I->getOperand(ConstIdx), m_ImmConstant(C)))
    return nullptr;

  Type *Ty = Op->getType();
  Constant *BitWidthC = ConstantInt::get(Ty, Ty->getScalarSizeInBits());
  // Need extra check for icmp. Note if this check is true, it generally means
  // the icmp will simplify to true/false.
  if (Opc == Instruction::ICmp && !cast<ICmpInst>(I)->isEquality()) {
    Constant *Cmp =
        ConstantFoldCompareInstOperands(ICmpInst::ICMP_UGT, C, BitWidthC, DL);
    if (!Cmp || !Cmp->isZeroValue())
      return nullptr;
  }

  // Check we can invert `(not x)` for free.
  bool Consumes = false;
  if (!isFreeToInvert(Op, Op->hasOneUse(), Consumes) || !Consumes)
    return nullptr;
  Value *NotOp = getFreelyInverted(Op, Op->hasOneUse(), &Builder);
  assert(NotOp != nullptr &&
         "Desync between isFreeToInvert and getFreelyInverted");

  Value *CtpopOfNotOp = Builder.CreateIntrinsic(Ty, Intrinsic::ctpop, NotOp);

  Value *R = nullptr;

  // Do the transformation here to avoid potentially introducing an infinite
  // loop.
  switch (Opc) {
  case Instruction::Sub:
    R = Builder.CreateAdd(CtpopOfNotOp, ConstantExpr::getSub(C, BitWidthC));
    break;
  case Instruction::Or:
  case Instruction::Add:
    R = Builder.CreateSub(ConstantExpr::getAdd(C, BitWidthC), CtpopOfNotOp);
    break;
  case Instruction::ICmp:
    R = Builder.CreateICmp(cast<ICmpInst>(I)->getSwappedPredicate(),
                           CtpopOfNotOp, ConstantExpr::getSub(BitWidthC, C));
    break;
  default:
    llvm_unreachable("Unhandled Opcode");
  }
  assert(R != nullptr);
  return replaceInstUsesWith(*I, R);
}

// (Binop1 (Binop2 (logic_shift X, C), C1), (logic_shift Y, C))
//   IFF
//    1) the logic_shifts match
//    2) either both binops are binops and one is `and` or
//       BinOp1 is `and`
//       (logic_shift (inv_logic_shift C1, C), C) == C1 or
//
//    -> (logic_shift (Binop1 (Binop2 X, inv_logic_shift(C1, C)), Y), C)
//
// (Binop1 (Binop2 (logic_shift X, Amt), Mask), (logic_shift Y, Amt))
//   IFF
//    1) the logic_shifts match
//    2) BinOp1 == BinOp2 (if BinOp ==  `add`, then also requires `shl`).
//
//    -> (BinOp (logic_shift (BinOp X, Y)), Mask)
//
// (Binop1 (Binop2 (arithmetic_shift X, Amt), Mask), (arithmetic_shift Y, Amt))
//   IFF
//   1) Binop1 is bitwise logical operator `and`, `or` or `xor`
//   2) Binop2 is `not`
//
//   -> (arithmetic_shift Binop1((not X), Y), Amt)

Instruction *InstCombinerImpl::foldBinOpShiftWithShift(BinaryOperator &I) {
  const DataLayout &DL = I.getDataLayout();
  auto IsValidBinOpc = [](unsigned Opc) {
    switch (Opc) {
    default:
      return false;
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::Add:
      // Skip Sub as we only match constant masks which will canonicalize to use
      // add.
      return true;
    }
  };

  // Check if we can distribute binop arbitrarily. `add` + `lshr` has extra
  // constraints.
  auto IsCompletelyDistributable = [](unsigned BinOpc1, unsigned BinOpc2,
                                      unsigned ShOpc) {
    assert(ShOpc != Instruction::AShr);
    return (BinOpc1 != Instruction::Add && BinOpc2 != Instruction::Add) ||
           ShOpc == Instruction::Shl;
  };

  auto GetInvShift = [](unsigned ShOpc) {
    assert(ShOpc != Instruction::AShr);
    return ShOpc == Instruction::LShr ? Instruction::Shl : Instruction::LShr;
  };

  auto CanDistributeBinops = [&](unsigned BinOpc1, unsigned BinOpc2,
                                 unsigned ShOpc, Constant *CMask,
                                 Constant *CShift) {
    // If the BinOp1 is `and` we don't need to check the mask.
    if (BinOpc1 == Instruction::And)
      return true;

    // For all other possible transfers we need complete distributable
    // binop/shift (anything but `add` + `lshr`).
    if (!IsCompletelyDistributable(BinOpc1, BinOpc2, ShOpc))
      return false;

    // If BinOp2 is `and`, any mask works (this only really helps for non-splat
    // vecs, otherwise the mask will be simplified and the following check will
    // handle it).
    if (BinOpc2 == Instruction::And)
      return true;

    // Otherwise, need mask that meets the below requirement.
    // (logic_shift (inv_logic_shift Mask, ShAmt), ShAmt) == Mask
    Constant *MaskInvShift =
        ConstantFoldBinaryOpOperands(GetInvShift(ShOpc), CMask, CShift, DL);
    return ConstantFoldBinaryOpOperands(ShOpc, MaskInvShift, CShift, DL) ==
           CMask;
  };

  auto MatchBinOp = [&](unsigned ShOpnum) -> Instruction * {
    Constant *CMask, *CShift;
    Value *X, *Y, *ShiftedX, *Mask, *Shift;
    if (!match(I.getOperand(ShOpnum),
               m_OneUse(m_Shift(m_Value(Y), m_Value(Shift)))))
      return nullptr;
    if (!match(I.getOperand(1 - ShOpnum),
               m_BinOp(m_Value(ShiftedX), m_Value(Mask))))
      return nullptr;

    if (!match(ShiftedX, m_OneUse(m_Shift(m_Value(X), m_Specific(Shift)))))
      return nullptr;

    // Make sure we are matching instruction shifts and not ConstantExpr
    auto *IY = dyn_cast<Instruction>(I.getOperand(ShOpnum));
    auto *IX = dyn_cast<Instruction>(ShiftedX);
    if (!IY || !IX)
      return nullptr;

    // LHS and RHS need same shift opcode
    unsigned ShOpc = IY->getOpcode();
    if (ShOpc != IX->getOpcode())
      return nullptr;

    // Make sure binop is real instruction and not ConstantExpr
    auto *BO2 = dyn_cast<Instruction>(I.getOperand(1 - ShOpnum));
    if (!BO2)
      return nullptr;

    unsigned BinOpc = BO2->getOpcode();
    // Make sure we have valid binops.
    if (!IsValidBinOpc(I.getOpcode()) || !IsValidBinOpc(BinOpc))
      return nullptr;

    if (ShOpc == Instruction::AShr) {
      if (Instruction::isBitwiseLogicOp(I.getOpcode()) &&
          BinOpc == Instruction::Xor && match(Mask, m_AllOnes())) {
        Value *NotX = Builder.CreateNot(X);
        Value *NewBinOp = Builder.CreateBinOp(I.getOpcode(), Y, NotX);
        return BinaryOperator::Create(
            static_cast<Instruction::BinaryOps>(ShOpc), NewBinOp, Shift);
      }

      return nullptr;
    }

    // If BinOp1 == BinOp2 and it's bitwise or shl with add, then just
    // distribute to drop the shift irrelevant of constants.
    if (BinOpc == I.getOpcode() &&
        IsCompletelyDistributable(I.getOpcode(), BinOpc, ShOpc)) {
      Value *NewBinOp2 = Builder.CreateBinOp(I.getOpcode(), X, Y);
      Value *NewBinOp1 = Builder.CreateBinOp(
          static_cast<Instruction::BinaryOps>(ShOpc), NewBinOp2, Shift);
      return BinaryOperator::Create(I.getOpcode(), NewBinOp1, Mask);
    }

    // Otherwise we can only distribute by constant shifting the mask, so
    // ensure we have constants.
    if (!match(Shift, m_ImmConstant(CShift)))
      return nullptr;
    if (!match(Mask, m_ImmConstant(CMask)))
      return nullptr;

    // Check if we can distribute the binops.
    if (!CanDistributeBinops(I.getOpcode(), BinOpc, ShOpc, CMask, CShift))
      return nullptr;

    Constant *NewCMask =
        ConstantFoldBinaryOpOperands(GetInvShift(ShOpc), CMask, CShift, DL);
    Value *NewBinOp2 = Builder.CreateBinOp(
        static_cast<Instruction::BinaryOps>(BinOpc), X, NewCMask);
    Value *NewBinOp1 = Builder.CreateBinOp(I.getOpcode(), Y, NewBinOp2);
    return BinaryOperator::Create(static_cast<Instruction::BinaryOps>(ShOpc),
                                  NewBinOp1, CShift);
  };

  if (Instruction *R = MatchBinOp(0))
    return R;
  return MatchBinOp(1);
}

// (Binop (zext C), (select C, T, F))
//    -> (select C, (binop 1, T), (binop 0, F))
//
// (Binop (sext C), (select C, T, F))
//    -> (select C, (binop -1, T), (binop 0, F))
//
// Attempt to simplify binary operations into a select with folded args, when
// one operand of the binop is a select instruction and the other operand is a
// zext/sext extension, whose value is the select condition.
Instruction *
InstCombinerImpl::foldBinOpOfSelectAndCastOfSelectCondition(BinaryOperator &I) {
  // TODO: this simplification may be extended to any speculatable instruction,
  // not just binops, and would possibly be handled better in FoldOpIntoSelect.
  Instruction::BinaryOps Opc = I.getOpcode();
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  Value *A, *CondVal, *TrueVal, *FalseVal;
  Value *CastOp;

  auto MatchSelectAndCast = [&](Value *CastOp, Value *SelectOp) {
    return match(CastOp, m_ZExtOrSExt(m_Value(A))) &&
           A->getType()->getScalarSizeInBits() == 1 &&
           match(SelectOp, m_Select(m_Value(CondVal), m_Value(TrueVal),
                                    m_Value(FalseVal)));
  };

  // Make sure one side of the binop is a select instruction, and the other is a
  // zero/sign extension operating on a i1.
  if (MatchSelectAndCast(LHS, RHS))
    CastOp = LHS;
  else if (MatchSelectAndCast(RHS, LHS))
    CastOp = RHS;
  else
    return nullptr;

  auto NewFoldedConst = [&](bool IsTrueArm, Value *V) {
    bool IsCastOpRHS = (CastOp == RHS);
    bool IsZExt = isa<ZExtInst>(CastOp);
    Constant *C;

    if (IsTrueArm) {
      C = Constant::getNullValue(V->getType());
    } else if (IsZExt) {
      unsigned BitWidth = V->getType()->getScalarSizeInBits();
      C = Constant::getIntegerValue(V->getType(), APInt(BitWidth, 1));
    } else {
      C = Constant::getAllOnesValue(V->getType());
    }

    return IsCastOpRHS ? Builder.CreateBinOp(Opc, V, C)
                       : Builder.CreateBinOp(Opc, C, V);
  };

  // If the value used in the zext/sext is the select condition, or the negated
  // of the select condition, the binop can be simplified.
  if (CondVal == A) {
    Value *NewTrueVal = NewFoldedConst(false, TrueVal);
    return SelectInst::Create(CondVal, NewTrueVal,
                              NewFoldedConst(true, FalseVal));
  }

  if (match(A, m_Not(m_Specific(CondVal)))) {
    Value *NewTrueVal = NewFoldedConst(true, TrueVal);
    return SelectInst::Create(CondVal, NewTrueVal,
                              NewFoldedConst(false, FalseVal));
  }

  return nullptr;
}

Value *InstCombinerImpl::tryFactorizationFolds(BinaryOperator &I) {
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  BinaryOperator *Op0 = dyn_cast<BinaryOperator>(LHS);
  BinaryOperator *Op1 = dyn_cast<BinaryOperator>(RHS);
  Instruction::BinaryOps TopLevelOpcode = I.getOpcode();
  Value *A, *B, *C, *D;
  Instruction::BinaryOps LHSOpcode, RHSOpcode;

  if (Op0)
    LHSOpcode = getBinOpsForFactorization(TopLevelOpcode, Op0, A, B, Op1);
  if (Op1)
    RHSOpcode = getBinOpsForFactorization(TopLevelOpcode, Op1, C, D, Op0);

  // The instruction has the form "(A op' B) op (C op' D)".  Try to factorize
  // a common term.
  if (Op0 && Op1 && LHSOpcode == RHSOpcode)
    if (Value *V = tryFactorization(I, SQ, Builder, LHSOpcode, A, B, C, D))
      return V;

  // The instruction has the form "(A op' B) op (C)".  Try to factorize common
  // term.
  if (Op0)
    if (Value *Ident = getIdentityValue(LHSOpcode, RHS))
      if (Value *V =
              tryFactorization(I, SQ, Builder, LHSOpcode, A, B, RHS, Ident))
        return V;

  // The instruction has the form "(B) op (C op' D)".  Try to factorize common
  // term.
  if (Op1)
    if (Value *Ident = getIdentityValue(RHSOpcode, LHS))
      if (Value *V =
              tryFactorization(I, SQ, Builder, RHSOpcode, LHS, Ident, C, D))
        return V;

  return nullptr;
}

/// This tries to simplify binary operations which some other binary operation
/// distributes over either by factorizing out common terms
/// (eg "(A*B)+(A*C)" -> "A*(B+C)") or expanding out if this results in
/// simplifications (eg: "A & (B | C) -> (A&B) | (A&C)" if this is a win).
/// Returns the simplified value, or null if it didn't simplify.
Value *InstCombinerImpl::foldUsingDistributiveLaws(BinaryOperator &I) {
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  BinaryOperator *Op0 = dyn_cast<BinaryOperator>(LHS);
  BinaryOperator *Op1 = dyn_cast<BinaryOperator>(RHS);
  Instruction::BinaryOps TopLevelOpcode = I.getOpcode();

  // Factorization.
  if (Value *R = tryFactorizationFolds(I))
    return R;

  // Expansion.
  if (Op0 && rightDistributesOverLeft(Op0->getOpcode(), TopLevelOpcode)) {
    // The instruction has the form "(A op' B) op C".  See if expanding it out
    // to "(A op C) op' (B op C)" results in simplifications.
    Value *A = Op0->getOperand(0), *B = Op0->getOperand(1), *C = RHS;
    Instruction::BinaryOps InnerOpcode = Op0->getOpcode(); // op'

    // Disable the use of undef because it's not safe to distribute undef.
    auto SQDistributive = SQ.getWithInstruction(&I).getWithoutUndef();
    Value *L = simplifyBinOp(TopLevelOpcode, A, C, SQDistributive);
    Value *R = simplifyBinOp(TopLevelOpcode, B, C, SQDistributive);

    // Do "A op C" and "B op C" both simplify?
    if (L && R) {
      // They do! Return "L op' R".
      ++NumExpand;
      C = Builder.CreateBinOp(InnerOpcode, L, R);
      C->takeName(&I);
      return C;
    }

    // Does "A op C" simplify to the identity value for the inner opcode?
    if (L && L == ConstantExpr::getBinOpIdentity(InnerOpcode, L->getType())) {
      // They do! Return "B op C".
      ++NumExpand;
      C = Builder.CreateBinOp(TopLevelOpcode, B, C);
      C->takeName(&I);
      return C;
    }

    // Does "B op C" simplify to the identity value for the inner opcode?
    if (R && R == ConstantExpr::getBinOpIdentity(InnerOpcode, R->getType())) {
      // They do! Return "A op C".
      ++NumExpand;
      C = Builder.CreateBinOp(TopLevelOpcode, A, C);
      C->takeName(&I);
      return C;
    }
  }

  if (Op1 && leftDistributesOverRight(TopLevelOpcode, Op1->getOpcode())) {
    // The instruction has the form "A op (B op' C)".  See if expanding it out
    // to "(A op B) op' (A op C)" results in simplifications.
    Value *A = LHS, *B = Op1->getOperand(0), *C = Op1->getOperand(1);
    Instruction::BinaryOps InnerOpcode = Op1->getOpcode(); // op'

    // Disable the use of undef because it's not safe to distribute undef.
    auto SQDistributive = SQ.getWithInstruction(&I).getWithoutUndef();
    Value *L = simplifyBinOp(TopLevelOpcode, A, B, SQDistributive);
    Value *R = simplifyBinOp(TopLevelOpcode, A, C, SQDistributive);

    // Do "A op B" and "A op C" both simplify?
    if (L && R) {
      // They do! Return "L op' R".
      ++NumExpand;
      A = Builder.CreateBinOp(InnerOpcode, L, R);
      A->takeName(&I);
      return A;
    }

    // Does "A op B" simplify to the identity value for the inner opcode?
    if (L && L == ConstantExpr::getBinOpIdentity(InnerOpcode, L->getType())) {
      // They do! Return "A op C".
      ++NumExpand;
      A = Builder.CreateBinOp(TopLevelOpcode, A, C);
      A->takeName(&I);
      return A;
    }

    // Does "A op C" simplify to the identity value for the inner opcode?
    if (R && R == ConstantExpr::getBinOpIdentity(InnerOpcode, R->getType())) {
      // They do! Return "A op B".
      ++NumExpand;
      A = Builder.CreateBinOp(TopLevelOpcode, A, B);
      A->takeName(&I);
      return A;
    }
  }

  return SimplifySelectsFeedingBinaryOp(I, LHS, RHS);
}

static std::optional<std::pair<Value *, Value *>>
matchSymmetricPhiNodesPair(PHINode *LHS, PHINode *RHS) {
  if (LHS->getParent() != RHS->getParent())
    return std::nullopt;

  if (LHS->getNumIncomingValues() < 2)
    return std::nullopt;

  if (!equal(LHS->blocks(), RHS->blocks()))
    return std::nullopt;

  Value *L0 = LHS->getIncomingValue(0);
  Value *R0 = RHS->getIncomingValue(0);

  for (unsigned I = 1, E = LHS->getNumIncomingValues(); I != E; ++I) {
    Value *L1 = LHS->getIncomingValue(I);
    Value *R1 = RHS->getIncomingValue(I);

    if ((L0 == L1 && R0 == R1) || (L0 == R1 && R0 == L1))
      continue;

    return std::nullopt;
  }

  return std::optional(std::pair(L0, R0));
}

std::optional<std::pair<Value *, Value *>>
InstCombinerImpl::matchSymmetricPair(Value *LHS, Value *RHS) {
  Instruction *LHSInst = dyn_cast<Instruction>(LHS);
  Instruction *RHSInst = dyn_cast<Instruction>(RHS);
  if (!LHSInst || !RHSInst || LHSInst->getOpcode() != RHSInst->getOpcode())
    return std::nullopt;
  switch (LHSInst->getOpcode()) {
  case Instruction::PHI:
    return matchSymmetricPhiNodesPair(cast<PHINode>(LHS), cast<PHINode>(RHS));
  case Instruction::Select: {
    Value *Cond = LHSInst->getOperand(0);
    Value *TrueVal = LHSInst->getOperand(1);
    Value *FalseVal = LHSInst->getOperand(2);
    if (Cond == RHSInst->getOperand(0) && TrueVal == RHSInst->getOperand(2) &&
        FalseVal == RHSInst->getOperand(1))
      return std::pair(TrueVal, FalseVal);
    return std::nullopt;
  }
  case Instruction::Call: {
    // Match min(a, b) and max(a, b)
    MinMaxIntrinsic *LHSMinMax = dyn_cast<MinMaxIntrinsic>(LHSInst);
    MinMaxIntrinsic *RHSMinMax = dyn_cast<MinMaxIntrinsic>(RHSInst);
    if (LHSMinMax && RHSMinMax &&
        LHSMinMax->getPredicate() ==
            ICmpInst::getSwappedPredicate(RHSMinMax->getPredicate()) &&
        ((LHSMinMax->getLHS() == RHSMinMax->getLHS() &&
          LHSMinMax->getRHS() == RHSMinMax->getRHS()) ||
         (LHSMinMax->getLHS() == RHSMinMax->getRHS() &&
          LHSMinMax->getRHS() == RHSMinMax->getLHS())))
      return std::pair(LHSMinMax->getLHS(), LHSMinMax->getRHS());
    return std::nullopt;
  }
  default:
    return std::nullopt;
  }
}

Value *InstCombinerImpl::SimplifySelectsFeedingBinaryOp(BinaryOperator &I,
                                                        Value *LHS,
                                                        Value *RHS) {
  Value *A, *B, *C, *D, *E, *F;
  bool LHSIsSelect = match(LHS, m_Select(m_Value(A), m_Value(B), m_Value(C)));
  bool RHSIsSelect = match(RHS, m_Select(m_Value(D), m_Value(E), m_Value(F)));
  if (!LHSIsSelect && !RHSIsSelect)
    return nullptr;

  FastMathFlags FMF;
  BuilderTy::FastMathFlagGuard Guard(Builder);
  if (isa<FPMathOperator>(&I)) {
    FMF = I.getFastMathFlags();
    Builder.setFastMathFlags(FMF);
  }

  Instruction::BinaryOps Opcode = I.getOpcode();
  SimplifyQuery Q = SQ.getWithInstruction(&I);

  Value *Cond, *True = nullptr, *False = nullptr;

  // Special-case for add/negate combination. Replace the zero in the negation
  // with the trailing add operand:
  // (Cond ? TVal : -N) + Z --> Cond ? True : (Z - N)
  // (Cond ? -N : FVal) + Z --> Cond ? (Z - N) : False
  auto foldAddNegate = [&](Value *TVal, Value *FVal, Value *Z) -> Value * {
    // We need an 'add' and exactly 1 arm of the select to have been simplified.
    if (Opcode != Instruction::Add || (!True && !False) || (True && False))
      return nullptr;

    Value *N;
    if (True && match(FVal, m_Neg(m_Value(N)))) {
      Value *Sub = Builder.CreateSub(Z, N);
      return Builder.CreateSelect(Cond, True, Sub, I.getName());
    }
    if (False && match(TVal, m_Neg(m_Value(N)))) {
      Value *Sub = Builder.CreateSub(Z, N);
      return Builder.CreateSelect(Cond, Sub, False, I.getName());
    }
    return nullptr;
  };

  if (LHSIsSelect && RHSIsSelect && A == D) {
    // (A ? B : C) op (A ? E : F) -> A ? (B op E) : (C op F)
    Cond = A;
    True = simplifyBinOp(Opcode, B, E, FMF, Q);
    False = simplifyBinOp(Opcode, C, F, FMF, Q);

    if (LHS->hasOneUse() && RHS->hasOneUse()) {
      if (False && !True)
        True = Builder.CreateBinOp(Opcode, B, E);
      else if (True && !False)
        False = Builder.CreateBinOp(Opcode, C, F);
    }
  } else if (LHSIsSelect && LHS->hasOneUse()) {
    // (A ? B : C) op Y -> A ? (B op Y) : (C op Y)
    Cond = A;
    True = simplifyBinOp(Opcode, B, RHS, FMF, Q);
    False = simplifyBinOp(Opcode, C, RHS, FMF, Q);
    if (Value *NewSel = foldAddNegate(B, C, RHS))
      return NewSel;
  } else if (RHSIsSelect && RHS->hasOneUse()) {
    // X op (D ? E : F) -> D ? (X op E) : (X op F)
    Cond = D;
    True = simplifyBinOp(Opcode, LHS, E, FMF, Q);
    False = simplifyBinOp(Opcode, LHS, F, FMF, Q);
    if (Value *NewSel = foldAddNegate(E, F, LHS))
      return NewSel;
  }

  if (!True || !False)
    return nullptr;

  Value *SI = Builder.CreateSelect(Cond, True, False);
  SI->takeName(&I);
  return SI;
}

/// Freely adapt every user of V as-if V was changed to !V.
/// WARNING: only if canFreelyInvertAllUsersOf() said this can be done.
void InstCombinerImpl::freelyInvertAllUsersOf(Value *I, Value *IgnoredUser) {
  assert(!isa<Constant>(I) && "Shouldn't invert users of constant");
  for (User *U : make_early_inc_range(I->users())) {
    if (U == IgnoredUser)
      continue; // Don't consider this user.
    switch (cast<Instruction>(U)->getOpcode()) {
    case Instruction::Select: {
      auto *SI = cast<SelectInst>(U);
      SI->swapValues();
      SI->swapProfMetadata();
      break;
    }
    case Instruction::Br: {
      BranchInst *BI = cast<BranchInst>(U);
      BI->swapSuccessors(); // swaps prof metadata too
      if (BPI)
        BPI->swapSuccEdgesProbabilities(BI->getParent());
      break;
    }
    case Instruction::Xor:
      replaceInstUsesWith(cast<Instruction>(*U), I);
      // Add to worklist for DCE.
      addToWorklist(cast<Instruction>(U));
      break;
    default:
      llvm_unreachable("Got unexpected user - out of sync with "
                       "canFreelyInvertAllUsersOf() ?");
    }
  }
}

/// Given a 'sub' instruction, return the RHS of the instruction if the LHS is a
/// constant zero (which is the 'negate' form).
Value *InstCombinerImpl::dyn_castNegVal(Value *V) const {
  Value *NegV;
  if (match(V, m_Neg(m_Value(NegV))))
    return NegV;

  // Constants can be considered to be negated values if they can be folded.
  if (ConstantInt *C = dyn_cast<ConstantInt>(V))
    return ConstantExpr::getNeg(C);

  if (ConstantDataVector *C = dyn_cast<ConstantDataVector>(V))
    if (C->getType()->getElementType()->isIntegerTy())
      return ConstantExpr::getNeg(C);

  if (ConstantVector *CV = dyn_cast<ConstantVector>(V)) {
    for (unsigned i = 0, e = CV->getNumOperands(); i != e; ++i) {
      Constant *Elt = CV->getAggregateElement(i);
      if (!Elt)
        return nullptr;

      if (isa<UndefValue>(Elt))
        continue;

      if (!isa<ConstantInt>(Elt))
        return nullptr;
    }
    return ConstantExpr::getNeg(CV);
  }

  // Negate integer vector splats.
  if (auto *CV = dyn_cast<Constant>(V))
    if (CV->getType()->isVectorTy() &&
        CV->getType()->getScalarType()->isIntegerTy() && CV->getSplatValue())
      return ConstantExpr::getNeg(CV);

  return nullptr;
}

// Try to fold:
//    1) (fp_binop ({s|u}itofp x), ({s|u}itofp y))
//        -> ({s|u}itofp (int_binop x, y))
//    2) (fp_binop ({s|u}itofp x), FpC)
//        -> ({s|u}itofp (int_binop x, (fpto{s|u}i FpC)))
//
// Assuming the sign of the cast for x/y is `OpsFromSigned`.
Instruction *InstCombinerImpl::foldFBinOpOfIntCastsFromSign(
    BinaryOperator &BO, bool OpsFromSigned, std::array<Value *, 2> IntOps,
    Constant *Op1FpC, SmallVectorImpl<WithCache<const Value *>> &OpsKnown) {

  Type *FPTy = BO.getType();
  Type *IntTy = IntOps[0]->getType();

  unsigned IntSz = IntTy->getScalarSizeInBits();
  // This is the maximum number of inuse bits by the integer where the int -> fp
  // casts are exact.
  unsigned MaxRepresentableBits =
      APFloat::semanticsPrecision(FPTy->getScalarType()->getFltSemantics());

  // Preserve known number of leading bits. This can allow us to trivial nsw/nuw
  // checks later on.
  unsigned NumUsedLeadingBits[2] = {IntSz, IntSz};

  // NB: This only comes up if OpsFromSigned is true, so there is no need to
  // cache if between calls to `foldFBinOpOfIntCastsFromSign`.
  auto IsNonZero = [&](unsigned OpNo) -> bool {
    if (OpsKnown[OpNo].hasKnownBits() &&
        OpsKnown[OpNo].getKnownBits(SQ).isNonZero())
      return true;
    return isKnownNonZero(IntOps[OpNo], SQ);
  };

  auto IsNonNeg = [&](unsigned OpNo) -> bool {
    // NB: This matches the impl in ValueTracking, we just try to use cached
    // knownbits here. If we ever start supporting WithCache for
    // `isKnownNonNegative`, change this to an explicit call.
    return OpsKnown[OpNo].getKnownBits(SQ).isNonNegative();
  };

  // Check if we know for certain that ({s|u}itofp op) is exact.
  auto IsValidPromotion = [&](unsigned OpNo) -> bool {
    // Can we treat this operand as the desired sign?
    if (OpsFromSigned != isa<SIToFPInst>(BO.getOperand(OpNo)) &&
        !IsNonNeg(OpNo))
      return false;

    // If fp precision >= bitwidth(op) then its exact.
    // NB: This is slightly conservative for `sitofp`. For signed conversion, we
    // can handle `MaxRepresentableBits == IntSz - 1` as the sign bit will be
    // handled specially. We can't, however, increase the bound arbitrarily for
    // `sitofp` as for larger sizes, it won't sign extend.
    if (MaxRepresentableBits < IntSz) {
      // Otherwise if its signed cast check that fp precisions >= bitwidth(op) -
      // numSignBits(op).
      // TODO: If we add support for `WithCache` in `ComputeNumSignBits`, change
      // `IntOps[OpNo]` arguments to `KnownOps[OpNo]`.
      if (OpsFromSigned)
        NumUsedLeadingBits[OpNo] = IntSz - ComputeNumSignBits(IntOps[OpNo]);
      // Finally for unsigned check that fp precision >= bitwidth(op) -
      // numLeadingZeros(op).
      else {
        NumUsedLeadingBits[OpNo] =
            IntSz - OpsKnown[OpNo].getKnownBits(SQ).countMinLeadingZeros();
      }
    }
    // NB: We could also check if op is known to be a power of 2 or zero (which
    // will always be representable). Its unlikely, however, that is we are
    // unable to bound op in any way we will be able to pass the overflow checks
    // later on.

    if (MaxRepresentableBits < NumUsedLeadingBits[OpNo])
      return false;
    // Signed + Mul also requires that op is non-zero to avoid -0 cases.
    return !OpsFromSigned || BO.getOpcode() != Instruction::FMul ||
           IsNonZero(OpNo);
  };

  // If we have a constant rhs, see if we can losslessly convert it to an int.
  if (Op1FpC != nullptr) {
    // Signed + Mul req non-zero
    if (OpsFromSigned && BO.getOpcode() == Instruction::FMul &&
        !match(Op1FpC, m_NonZeroFP()))
      return nullptr;

    Constant *Op1IntC = ConstantFoldCastOperand(
        OpsFromSigned ? Instruction::FPToSI : Instruction::FPToUI, Op1FpC,
        IntTy, DL);
    if (Op1IntC == nullptr)
      return nullptr;
    if (ConstantFoldCastOperand(OpsFromSigned ? Instruction::SIToFP
                                              : Instruction::UIToFP,
                                Op1IntC, FPTy, DL) != Op1FpC)
      return nullptr;

    // First try to keep sign of cast the same.
    IntOps[1] = Op1IntC;
  }

  // Ensure lhs/rhs integer types match.
  if (IntTy != IntOps[1]->getType())
    return nullptr;

  if (Op1FpC == nullptr) {
    if (!IsValidPromotion(1))
      return nullptr;
  }
  if (!IsValidPromotion(0))
    return nullptr;

  // Final we check if the integer version of the binop will not overflow.
  BinaryOperator::BinaryOps IntOpc;
  // Because of the precision check, we can often rule out overflows.
  bool NeedsOverflowCheck = true;
  // Try to conservatively rule out overflow based on the already done precision
  // checks.
  unsigned OverflowMaxOutputBits = OpsFromSigned ? 2 : 1;
  unsigned OverflowMaxCurBits =
      std::max(NumUsedLeadingBits[0], NumUsedLeadingBits[1]);
  bool OutputSigned = OpsFromSigned;
  switch (BO.getOpcode()) {
  case Instruction::FAdd:
    IntOpc = Instruction::Add;
    OverflowMaxOutputBits += OverflowMaxCurBits;
    break;
  case Instruction::FSub:
    IntOpc = Instruction::Sub;
    OverflowMaxOutputBits += OverflowMaxCurBits;
    break;
  case Instruction::FMul:
    IntOpc = Instruction::Mul;
    OverflowMaxOutputBits += OverflowMaxCurBits * 2;
    break;
  default:
    llvm_unreachable("Unsupported binop");
  }
  // The precision check may have already ruled out overflow.
  if (OverflowMaxOutputBits < IntSz) {
    NeedsOverflowCheck = false;
    // We can bound unsigned overflow from sub to in range signed value (this is
    // what allows us to avoid the overflow check for sub).
    if (IntOpc == Instruction::Sub)
      OutputSigned = true;
  }

  // Precision check did not rule out overflow, so need to check.
  // TODO: If we add support for `WithCache` in `willNotOverflow`, change
  // `IntOps[...]` arguments to `KnownOps[...]`.
  if (NeedsOverflowCheck &&
      !willNotOverflow(IntOpc, IntOps[0], IntOps[1], BO, OutputSigned))
    return nullptr;

  Value *IntBinOp = Builder.CreateBinOp(IntOpc, IntOps[0], IntOps[1]);
  if (auto *IntBO = dyn_cast<BinaryOperator>(IntBinOp)) {
    IntBO->setHasNoSignedWrap(OutputSigned);
    IntBO->setHasNoUnsignedWrap(!OutputSigned);
  }
  if (OutputSigned)
    return new SIToFPInst(IntBinOp, FPTy);
  return new UIToFPInst(IntBinOp, FPTy);
}

// Try to fold:
//    1) (fp_binop ({s|u}itofp x), ({s|u}itofp y))
//        -> ({s|u}itofp (int_binop x, y))
//    2) (fp_binop ({s|u}itofp x), FpC)
//        -> ({s|u}itofp (int_binop x, (fpto{s|u}i FpC)))
Instruction *InstCombinerImpl::foldFBinOpOfIntCasts(BinaryOperator &BO) {
  std::array<Value *, 2> IntOps = {nullptr, nullptr};
  Constant *Op1FpC = nullptr;
  // Check for:
  //    1) (binop ({s|u}itofp x), ({s|u}itofp y))
  //    2) (binop ({s|u}itofp x), FpC)
  if (!match(BO.getOperand(0), m_SIToFP(m_Value(IntOps[0]))) &&
      !match(BO.getOperand(0), m_UIToFP(m_Value(IntOps[0]))))
    return nullptr;

  if (!match(BO.getOperand(1), m_Constant(Op1FpC)) &&
      !match(BO.getOperand(1), m_SIToFP(m_Value(IntOps[1]))) &&
      !match(BO.getOperand(1), m_UIToFP(m_Value(IntOps[1]))))
    return nullptr;

  // Cache KnownBits a bit to potentially save some analysis.
  SmallVector<WithCache<const Value *>, 2> OpsKnown = {IntOps[0], IntOps[1]};

  // Try treating x/y as coming from both `uitofp` and `sitofp`. There are
  // different constraints depending on the sign of the cast.
  // NB: `(uitofp nneg X)` == `(sitofp nneg X)`.
  if (Instruction *R = foldFBinOpOfIntCastsFromSign(BO, /*OpsFromSigned=*/false,
                                                    IntOps, Op1FpC, OpsKnown))
    return R;
  return foldFBinOpOfIntCastsFromSign(BO, /*OpsFromSigned=*/true, IntOps,
                                      Op1FpC, OpsKnown);
}

/// A binop with a constant operand and a sign-extended boolean operand may be
/// converted into a select of constants by applying the binary operation to
/// the constant with the two possible values of the extended boolean (0 or -1).
Instruction *InstCombinerImpl::foldBinopOfSextBoolToSelect(BinaryOperator &BO) {
  // TODO: Handle non-commutative binop (constant is operand 0).
  // TODO: Handle zext.
  // TODO: Peek through 'not' of cast.
  Value *BO0 = BO.getOperand(0);
  Value *BO1 = BO.getOperand(1);
  Value *X;
  Constant *C;
  if (!match(BO0, m_SExt(m_Value(X))) || !match(BO1, m_ImmConstant(C)) ||
      !X->getType()->isIntOrIntVectorTy(1))
    return nullptr;

  // bo (sext i1 X), C --> select X, (bo -1, C), (bo 0, C)
  Constant *Ones = ConstantInt::getAllOnesValue(BO.getType());
  Constant *Zero = ConstantInt::getNullValue(BO.getType());
  Value *TVal = Builder.CreateBinOp(BO.getOpcode(), Ones, C);
  Value *FVal = Builder.CreateBinOp(BO.getOpcode(), Zero, C);
  return SelectInst::Create(X, TVal, FVal);
}

static Constant *constantFoldOperationIntoSelectOperand(Instruction &I,
                                                        SelectInst *SI,
                                                        bool IsTrueArm) {
  SmallVector<Constant *> ConstOps;
  for (Value *Op : I.operands()) {
    CmpInst::Predicate Pred;
    Constant *C = nullptr;
    if (Op == SI) {
      C = dyn_cast<Constant>(IsTrueArm ? SI->getTrueValue()
                                       : SI->getFalseValue());
    } else if (match(SI->getCondition(),
                     m_ICmp(Pred, m_Specific(Op), m_Constant(C))) &&
               Pred == (IsTrueArm ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE) &&
               isGuaranteedNotToBeUndefOrPoison(C)) {
      // Pass
    } else {
      C = dyn_cast<Constant>(Op);
    }
    if (C == nullptr)
      return nullptr;

    ConstOps.push_back(C);
  }

  return ConstantFoldInstOperands(&I, ConstOps, I.getDataLayout());
}

static Value *foldOperationIntoSelectOperand(Instruction &I, SelectInst *SI,
                                             Value *NewOp, InstCombiner &IC) {
  Instruction *Clone = I.clone();
  Clone->replaceUsesOfWith(SI, NewOp);
  Clone->dropUBImplyingAttrsAndMetadata();
  IC.InsertNewInstBefore(Clone, SI->getIterator());
  return Clone;
}

Instruction *InstCombinerImpl::FoldOpIntoSelect(Instruction &Op, SelectInst *SI,
                                                bool FoldWithMultiUse) {
  // Don't modify shared select instructions unless set FoldWithMultiUse
  if (!SI->hasOneUse() && !FoldWithMultiUse)
    return nullptr;

  Value *TV = SI->getTrueValue();
  Value *FV = SI->getFalseValue();
  if (!(isa<Constant>(TV) || isa<Constant>(FV)))
    return nullptr;

  // Bool selects with constant operands can be folded to logical ops.
  if (SI->getType()->isIntOrIntVectorTy(1))
    return nullptr;

  // Test if a FCmpInst instruction is used exclusively by a select as
  // part of a minimum or maximum operation. If so, refrain from doing
  // any other folding. This helps out other analyses which understand
  // non-obfuscated minimum and maximum idioms. And in this case, at
  // least one of the comparison operands has at least one user besides
  // the compare (the select), which would often largely negate the
  // benefit of folding anyway.
  if (auto *CI = dyn_cast<FCmpInst>(SI->getCondition())) {
    if (CI->hasOneUse()) {
      Value *Op0 = CI->getOperand(0), *Op1 = CI->getOperand(1);
      if ((TV == Op0 && FV == Op1) || (FV == Op0 && TV == Op1))
        return nullptr;
    }
  }

  // Make sure that one of the select arms constant folds successfully.
  Value *NewTV = constantFoldOperationIntoSelectOperand(Op, SI, /*IsTrueArm*/ true);
  Value *NewFV = constantFoldOperationIntoSelectOperand(Op, SI, /*IsTrueArm*/ false);
  if (!NewTV && !NewFV)
    return nullptr;

  // Create an instruction for the arm that did not fold.
  if (!NewTV)
    NewTV = foldOperationIntoSelectOperand(Op, SI, TV, *this);
  if (!NewFV)
    NewFV = foldOperationIntoSelectOperand(Op, SI, FV, *this);
  return SelectInst::Create(SI->getCondition(), NewTV, NewFV, "", nullptr, SI);
}

static Value *simplifyInstructionWithPHI(Instruction &I, PHINode *PN,
                                         Value *InValue, BasicBlock *InBB,
                                         const DataLayout &DL,
                                         const SimplifyQuery SQ) {
  // NB: It is a precondition of this transform that the operands be
  // phi translatable! This is usually trivially satisfied by limiting it
  // to constant ops, and for selects we do a more sophisticated check.
  SmallVector<Value *> Ops;
  for (Value *Op : I.operands()) {
    if (Op == PN)
      Ops.push_back(InValue);
    else
      Ops.push_back(Op->DoPHITranslation(PN->getParent(), InBB));
  }

  // Don't consider the simplification successful if we get back a constant
  // expression. That's just an instruction in hiding.
  // Also reject the case where we simplify back to the phi node. We wouldn't
  // be able to remove it in that case.
  Value *NewVal = simplifyInstructionWithOperands(
      &I, Ops, SQ.getWithInstruction(InBB->getTerminator()));
  if (NewVal && NewVal != PN && !match(NewVal, m_ConstantExpr()))
    return NewVal;

  // Check if incoming PHI value can be replaced with constant
  // based on implied condition.
  BranchInst *TerminatorBI = dyn_cast<BranchInst>(InBB->getTerminator());
  const ICmpInst *ICmp = dyn_cast<ICmpInst>(&I);
  if (TerminatorBI && TerminatorBI->isConditional() &&
      TerminatorBI->getSuccessor(0) != TerminatorBI->getSuccessor(1) && ICmp) {
    bool LHSIsTrue = TerminatorBI->getSuccessor(0) == PN->getParent();
    std::optional<bool> ImpliedCond =
        isImpliedCondition(TerminatorBI->getCondition(), ICmp->getPredicate(),
                           Ops[0], Ops[1], DL, LHSIsTrue);
    if (ImpliedCond)
      return ConstantInt::getBool(I.getType(), ImpliedCond.value());
  }

  return nullptr;
}

Instruction *InstCombinerImpl::foldOpIntoPhi(Instruction &I, PHINode *PN) {
  unsigned NumPHIValues = PN->getNumIncomingValues();
  if (NumPHIValues == 0)
    return nullptr;

  // We normally only transform phis with a single use.  However, if a PHI has
  // multiple uses and they are all the same operation, we can fold *all* of the
  // uses into the PHI.
  if (!PN->hasOneUse()) {
    // Walk the use list for the instruction, comparing them to I.
    for (User *U : PN->users()) {
      Instruction *UI = cast<Instruction>(U);
      if (UI != &I && !I.isIdenticalTo(UI))
        return nullptr;
    }
    // Otherwise, we can replace *all* users with the new PHI we form.
  }

  // Check to see whether the instruction can be folded into each phi operand.
  // If there is one operand that does not fold, remember the BB it is in.
  // If there is more than one or if *it* is a PHI, bail out.
  SmallVector<Value *> NewPhiValues;
  BasicBlock *NonSimplifiedBB = nullptr;
  Value *NonSimplifiedInVal = nullptr;
  for (unsigned i = 0; i != NumPHIValues; ++i) {
    Value *InVal = PN->getIncomingValue(i);
    BasicBlock *InBB = PN->getIncomingBlock(i);

    if (auto *NewVal = simplifyInstructionWithPHI(I, PN, InVal, InBB, DL, SQ)) {
      NewPhiValues.push_back(NewVal);
      continue;
    }

    if (NonSimplifiedBB) return nullptr;  // More than one non-simplified value.

    NonSimplifiedBB = InBB;
    NonSimplifiedInVal = InVal;
    NewPhiValues.push_back(nullptr);

    // If the InVal is an invoke at the end of the pred block, then we can't
    // insert a computation after it without breaking the edge.
    if (isa<InvokeInst>(InVal))
      if (cast<Instruction>(InVal)->getParent() == NonSimplifiedBB)
        return nullptr;

    // If the incoming non-constant value is reachable from the phis block,
    // we'll push the operation across a loop backedge. This could result in
    // an infinite combine loop, and is generally non-profitable (especially
    // if the operation was originally outside the loop).
    if (isPotentiallyReachable(PN->getParent(), NonSimplifiedBB, nullptr, &DT,
                               LI))
      return nullptr;
  }

  // If there is exactly one non-simplified value, we can insert a copy of the
  // operation in that block.  However, if this is a critical edge, we would be
  // inserting the computation on some other paths (e.g. inside a loop).  Only
  // do this if the pred block is unconditionally branching into the phi block.
  // Also, make sure that the pred block is not dead code.
  if (NonSimplifiedBB != nullptr) {
    BranchInst *BI = dyn_cast<BranchInst>(NonSimplifiedBB->getTerminator());
    if (!BI || !BI->isUnconditional() ||
        !DT.isReachableFromEntry(NonSimplifiedBB))
      return nullptr;
  }

  // Okay, we can do the transformation: create the new PHI node.
  PHINode *NewPN = PHINode::Create(I.getType(), PN->getNumIncomingValues());
  InsertNewInstBefore(NewPN, PN->getIterator());
  NewPN->takeName(PN);
  NewPN->setDebugLoc(PN->getDebugLoc());

  // If we are going to have to insert a new computation, do so right before the
  // predecessor's terminator.
  Instruction *Clone = nullptr;
  if (NonSimplifiedBB) {
    Clone = I.clone();
    for (Use &U : Clone->operands()) {
      if (U == PN)
        U = NonSimplifiedInVal;
      else
        U = U->DoPHITranslation(PN->getParent(), NonSimplifiedBB);
    }
    InsertNewInstBefore(Clone, NonSimplifiedBB->getTerminator()->getIterator());
  }

  for (unsigned i = 0; i != NumPHIValues; ++i) {
    if (NewPhiValues[i])
      NewPN->addIncoming(NewPhiValues[i], PN->getIncomingBlock(i));
    else
      NewPN->addIncoming(Clone, PN->getIncomingBlock(i));
  }

  for (User *U : make_early_inc_range(PN->users())) {
    Instruction *User = cast<Instruction>(U);
    if (User == &I) continue;
    replaceInstUsesWith(*User, NewPN);
    eraseInstFromFunction(*User);
  }

  replaceAllDbgUsesWith(const_cast<PHINode &>(*PN),
                        const_cast<PHINode &>(*NewPN),
                        const_cast<PHINode &>(*PN), DT);
  return replaceInstUsesWith(I, NewPN);
}

Instruction *InstCombinerImpl::foldBinopWithPhiOperands(BinaryOperator &BO) {
  // TODO: This should be similar to the incoming values check in foldOpIntoPhi:
  //       we are guarding against replicating the binop in >1 predecessor.
  //       This could miss matching a phi with 2 constant incoming values.
  auto *Phi0 = dyn_cast<PHINode>(BO.getOperand(0));
  auto *Phi1 = dyn_cast<PHINode>(BO.getOperand(1));
  if (!Phi0 || !Phi1 || !Phi0->hasOneUse() || !Phi1->hasOneUse() ||
      Phi0->getNumOperands() != Phi1->getNumOperands())
    return nullptr;

  // TODO: Remove the restriction for binop being in the same block as the phis.
  if (BO.getParent() != Phi0->getParent() ||
      BO.getParent() != Phi1->getParent())
    return nullptr;

  // Fold if there is at least one specific constant value in phi0 or phi1's
  // incoming values that comes from the same block and this specific constant
  // value can be used to do optimization for specific binary operator.
  // For example:
  // %phi0 = phi i32 [0, %bb0], [%i, %bb1]
  // %phi1 = phi i32 [%j, %bb0], [0, %bb1]
  // %add = add i32 %phi0, %phi1
  // ==>
  // %add = phi i32 [%j, %bb0], [%i, %bb1]
  Constant *C = ConstantExpr::getBinOpIdentity(BO.getOpcode(), BO.getType(),
                                               /*AllowRHSConstant*/ false);
  if (C) {
    SmallVector<Value *, 4> NewIncomingValues;
    auto CanFoldIncomingValuePair = [&](std::tuple<Use &, Use &> T) {
      auto &Phi0Use = std::get<0>(T);
      auto &Phi1Use = std::get<1>(T);
      if (Phi0->getIncomingBlock(Phi0Use) != Phi1->getIncomingBlock(Phi1Use))
        return false;
      Value *Phi0UseV = Phi0Use.get();
      Value *Phi1UseV = Phi1Use.get();
      if (Phi0UseV == C)
        NewIncomingValues.push_back(Phi1UseV);
      else if (Phi1UseV == C)
        NewIncomingValues.push_back(Phi0UseV);
      else
        return false;
      return true;
    };

    if (all_of(zip(Phi0->operands(), Phi1->operands()),
               CanFoldIncomingValuePair)) {
      PHINode *NewPhi =
          PHINode::Create(Phi0->getType(), Phi0->getNumOperands());
      assert(NewIncomingValues.size() == Phi0->getNumOperands() &&
             "The number of collected incoming values should equal the number "
             "of the original PHINode operands!");
      for (unsigned I = 0; I < Phi0->getNumOperands(); I++)
        NewPhi->addIncoming(NewIncomingValues[I], Phi0->getIncomingBlock(I));
      return NewPhi;
    }
  }

  if (Phi0->getNumOperands() != 2 || Phi1->getNumOperands() != 2)
    return nullptr;

  // Match a pair of incoming constants for one of the predecessor blocks.
  BasicBlock *ConstBB, *OtherBB;
  Constant *C0, *C1;
  if (match(Phi0->getIncomingValue(0), m_ImmConstant(C0))) {
    ConstBB = Phi0->getIncomingBlock(0);
    OtherBB = Phi0->getIncomingBlock(1);
  } else if (match(Phi0->getIncomingValue(1), m_ImmConstant(C0))) {
    ConstBB = Phi0->getIncomingBlock(1);
    OtherBB = Phi0->getIncomingBlock(0);
  } else {
    return nullptr;
  }
  if (!match(Phi1->getIncomingValueForBlock(ConstBB), m_ImmConstant(C1)))
    return nullptr;

  // The block that we are hoisting to must reach here unconditionally.
  // Otherwise, we could be speculatively executing an expensive or
  // non-speculative op.
  auto *PredBlockBranch = dyn_cast<BranchInst>(OtherBB->getTerminator());
  if (!PredBlockBranch || PredBlockBranch->isConditional() ||
      !DT.isReachableFromEntry(OtherBB))
    return nullptr;

  // TODO: This check could be tightened to only apply to binops (div/rem) that
  //       are not safe to speculatively execute. But that could allow hoisting
  //       potentially expensive instructions (fdiv for example).
  for (auto BBIter = BO.getParent()->begin(); &*BBIter != &BO; ++BBIter)
    if (!isGuaranteedToTransferExecutionToSuccessor(&*BBIter))
      return nullptr;

  // Fold constants for the predecessor block with constant incoming values.
  Constant *NewC = ConstantFoldBinaryOpOperands(BO.getOpcode(), C0, C1, DL);
  if (!NewC)
    return nullptr;

  // Make a new binop in the predecessor block with the non-constant incoming
  // values.
  Builder.SetInsertPoint(PredBlockBranch);
  Value *NewBO = Builder.CreateBinOp(BO.getOpcode(),
                                     Phi0->getIncomingValueForBlock(OtherBB),
                                     Phi1->getIncomingValueForBlock(OtherBB));
  if (auto *NotFoldedNewBO = dyn_cast<BinaryOperator>(NewBO))
    NotFoldedNewBO->copyIRFlags(&BO);

  // Replace the binop with a phi of the new values. The old phis are dead.
  PHINode *NewPhi = PHINode::Create(BO.getType(), 2);
  NewPhi->addIncoming(NewBO, OtherBB);
  NewPhi->addIncoming(NewC, ConstBB);
  return NewPhi;
}

Instruction *InstCombinerImpl::foldBinOpIntoSelectOrPhi(BinaryOperator &I) {
  if (!isa<Constant>(I.getOperand(1)))
    return nullptr;

  if (auto *Sel = dyn_cast<SelectInst>(I.getOperand(0))) {
    if (Instruction *NewSel = FoldOpIntoSelect(I, Sel))
      return NewSel;
  } else if (auto *PN = dyn_cast<PHINode>(I.getOperand(0))) {
    if (Instruction *NewPhi = foldOpIntoPhi(I, PN))
      return NewPhi;
  }
  return nullptr;
}

static bool shouldMergeGEPs(GEPOperator &GEP, GEPOperator &Src) {
  // If this GEP has only 0 indices, it is the same pointer as
  // Src. If Src is not a trivial GEP too, don't combine
  // the indices.
  if (GEP.hasAllZeroIndices() && !Src.hasAllZeroIndices() &&
      !Src.hasOneUse())
    return false;
  return true;
}

Instruction *InstCombinerImpl::foldVectorBinop(BinaryOperator &Inst) {
  if (!isa<VectorType>(Inst.getType()))
    return nullptr;

  BinaryOperator::BinaryOps Opcode = Inst.getOpcode();
  Value *LHS = Inst.getOperand(0), *RHS = Inst.getOperand(1);
  assert(cast<VectorType>(LHS->getType())->getElementCount() ==
         cast<VectorType>(Inst.getType())->getElementCount());
  assert(cast<VectorType>(RHS->getType())->getElementCount() ==
         cast<VectorType>(Inst.getType())->getElementCount());

  // If both operands of the binop are vector concatenations, then perform the
  // narrow binop on each pair of the source operands followed by concatenation
  // of the results.
  Value *L0, *L1, *R0, *R1;
  ArrayRef<int> Mask;
  if (match(LHS, m_Shuffle(m_Value(L0), m_Value(L1), m_Mask(Mask))) &&
      match(RHS, m_Shuffle(m_Value(R0), m_Value(R1), m_SpecificMask(Mask))) &&
      LHS->hasOneUse() && RHS->hasOneUse() &&
      cast<ShuffleVectorInst>(LHS)->isConcat() &&
      cast<ShuffleVectorInst>(RHS)->isConcat()) {
    // This transform does not have the speculative execution constraint as
    // below because the shuffle is a concatenation. The new binops are
    // operating on exactly the same elements as the existing binop.
    // TODO: We could ease the mask requirement to allow different undef lanes,
    //       but that requires an analysis of the binop-with-undef output value.
    Value *NewBO0 = Builder.CreateBinOp(Opcode, L0, R0);
    if (auto *BO = dyn_cast<BinaryOperator>(NewBO0))
      BO->copyIRFlags(&Inst);
    Value *NewBO1 = Builder.CreateBinOp(Opcode, L1, R1);
    if (auto *BO = dyn_cast<BinaryOperator>(NewBO1))
      BO->copyIRFlags(&Inst);
    return new ShuffleVectorInst(NewBO0, NewBO1, Mask);
  }

  auto createBinOpReverse = [&](Value *X, Value *Y) {
    Value *V = Builder.CreateBinOp(Opcode, X, Y, Inst.getName());
    if (auto *BO = dyn_cast<BinaryOperator>(V))
      BO->copyIRFlags(&Inst);
    Module *M = Inst.getModule();
    Function *F =
        Intrinsic::getDeclaration(M, Intrinsic::vector_reverse, V->getType());
    return CallInst::Create(F, V);
  };

  // NOTE: Reverse shuffles don't require the speculative execution protection
  // below because they don't affect which lanes take part in the computation.

  Value *V1, *V2;
  if (match(LHS, m_VecReverse(m_Value(V1)))) {
    // Op(rev(V1), rev(V2)) -> rev(Op(V1, V2))
    if (match(RHS, m_VecReverse(m_Value(V2))) &&
        (LHS->hasOneUse() || RHS->hasOneUse() ||
         (LHS == RHS && LHS->hasNUses(2))))
      return createBinOpReverse(V1, V2);

    // Op(rev(V1), RHSSplat)) -> rev(Op(V1, RHSSplat))
    if (LHS->hasOneUse() && isSplatValue(RHS))
      return createBinOpReverse(V1, RHS);
  }
  // Op(LHSSplat, rev(V2)) -> rev(Op(LHSSplat, V2))
  else if (isSplatValue(LHS) && match(RHS, m_OneUse(m_VecReverse(m_Value(V2)))))
    return createBinOpReverse(LHS, V2);

  // It may not be safe to reorder shuffles and things like div, urem, etc.
  // because we may trap when executing those ops on unknown vector elements.
  // See PR20059.
  if (!isSafeToSpeculativelyExecute(&Inst))
    return nullptr;

  auto createBinOpShuffle = [&](Value *X, Value *Y, ArrayRef<int> M) {
    Value *XY = Builder.CreateBinOp(Opcode, X, Y);
    if (auto *BO = dyn_cast<BinaryOperator>(XY))
      BO->copyIRFlags(&Inst);
    return new ShuffleVectorInst(XY, M);
  };

  // If both arguments of the binary operation are shuffles that use the same
  // mask and shuffle within a single vector, move the shuffle after the binop.
  if (match(LHS, m_Shuffle(m_Value(V1), m_Poison(), m_Mask(Mask))) &&
      match(RHS, m_Shuffle(m_Value(V2), m_Poison(), m_SpecificMask(Mask))) &&
      V1->getType() == V2->getType() &&
      (LHS->hasOneUse() || RHS->hasOneUse() || LHS == RHS)) {
    // Op(shuffle(V1, Mask), shuffle(V2, Mask)) -> shuffle(Op(V1, V2), Mask)
    return createBinOpShuffle(V1, V2, Mask);
  }

  // If both arguments of a commutative binop are select-shuffles that use the
  // same mask with commuted operands, the shuffles are unnecessary.
  if (Inst.isCommutative() &&
      match(LHS, m_Shuffle(m_Value(V1), m_Value(V2), m_Mask(Mask))) &&
      match(RHS,
            m_Shuffle(m_Specific(V2), m_Specific(V1), m_SpecificMask(Mask)))) {
    auto *LShuf = cast<ShuffleVectorInst>(LHS);
    auto *RShuf = cast<ShuffleVectorInst>(RHS);
    // TODO: Allow shuffles that contain undefs in the mask?
    //       That is legal, but it reduces undef knowledge.
    // TODO: Allow arbitrary shuffles by shuffling after binop?
    //       That might be legal, but we have to deal with poison.
    if (LShuf->isSelect() &&
        !is_contained(LShuf->getShuffleMask(), PoisonMaskElem) &&
        RShuf->isSelect() &&
        !is_contained(RShuf->getShuffleMask(), PoisonMaskElem)) {
      // Example:
      // LHS = shuffle V1, V2, <0, 5, 6, 3>
      // RHS = shuffle V2, V1, <0, 5, 6, 3>
      // LHS + RHS --> (V10+V20, V21+V11, V22+V12, V13+V23) --> V1 + V2
      Instruction *NewBO = BinaryOperator::Create(Opcode, V1, V2);
      NewBO->copyIRFlags(&Inst);
      return NewBO;
    }
  }

  // If one argument is a shuffle within one vector and the other is a constant,
  // try moving the shuffle after the binary operation. This canonicalization
  // intends to move shuffles closer to other shuffles and binops closer to
  // other binops, so they can be folded. It may also enable demanded elements
  // transforms.
  Constant *C;
  auto *InstVTy = dyn_cast<FixedVectorType>(Inst.getType());
  if (InstVTy &&
      match(&Inst, m_c_BinOp(m_OneUse(m_Shuffle(m_Value(V1), m_Poison(),
                                                m_Mask(Mask))),
                             m_ImmConstant(C))) &&
      cast<FixedVectorType>(V1->getType())->getNumElements() <=
          InstVTy->getNumElements()) {
    assert(InstVTy->getScalarType() == V1->getType()->getScalarType() &&
           "Shuffle should not change scalar type");

    // Find constant NewC that has property:
    //   shuffle(NewC, ShMask) = C
    // If such constant does not exist (example: ShMask=<0,0> and C=<1,2>)
    // reorder is not possible. A 1-to-1 mapping is not required. Example:
    // ShMask = <1,1,2,2> and C = <5,5,6,6> --> NewC = <undef,5,6,undef>
    bool ConstOp1 = isa<Constant>(RHS);
    ArrayRef<int> ShMask = Mask;
    unsigned SrcVecNumElts =
        cast<FixedVectorType>(V1->getType())->getNumElements();
    PoisonValue *PoisonScalar = PoisonValue::get(C->getType()->getScalarType());
    SmallVector<Constant *, 16> NewVecC(SrcVecNumElts, PoisonScalar);
    bool MayChange = true;
    unsigned NumElts = InstVTy->getNumElements();
    for (unsigned I = 0; I < NumElts; ++I) {
      Constant *CElt = C->getAggregateElement(I);
      if (ShMask[I] >= 0) {
        assert(ShMask[I] < (int)NumElts && "Not expecting narrowing shuffle");
        Constant *NewCElt = NewVecC[ShMask[I]];
        // Bail out if:
        // 1. The constant vector contains a constant expression.
        // 2. The shuffle needs an element of the constant vector that can't
        //    be mapped to a new constant vector.
        // 3. This is a widening shuffle that copies elements of V1 into the
        //    extended elements (extending with poison is allowed).
        if (!CElt || (!isa<PoisonValue>(NewCElt) && NewCElt != CElt) ||
            I >= SrcVecNumElts) {
          MayChange = false;
          break;
        }
        NewVecC[ShMask[I]] = CElt;
      }
      // If this is a widening shuffle, we must be able to extend with poison
      // elements. If the original binop does not produce a poison in the high
      // lanes, then this transform is not safe.
      // Similarly for poison lanes due to the shuffle mask, we can only
      // transform binops that preserve poison.
      // TODO: We could shuffle those non-poison constant values into the
      //       result by using a constant vector (rather than an poison vector)
      //       as operand 1 of the new binop, but that might be too aggressive
      //       for target-independent shuffle creation.
      if (I >= SrcVecNumElts || ShMask[I] < 0) {
        Constant *MaybePoison =
            ConstOp1
                ? ConstantFoldBinaryOpOperands(Opcode, PoisonScalar, CElt, DL)
                : ConstantFoldBinaryOpOperands(Opcode, CElt, PoisonScalar, DL);
        if (!MaybePoison || !isa<PoisonValue>(MaybePoison)) {
          MayChange = false;
          break;
        }
      }
    }
    if (MayChange) {
      Constant *NewC = ConstantVector::get(NewVecC);
      // It may not be safe to execute a binop on a vector with poison elements
      // because the entire instruction can be folded to undef or create poison
      // that did not exist in the original code.
      // TODO: The shift case should not be necessary.
      if (Inst.isIntDivRem() || (Inst.isShift() && ConstOp1))
        NewC = getSafeVectorConstantForBinop(Opcode, NewC, ConstOp1);

      // Op(shuffle(V1, Mask), C) -> shuffle(Op(V1, NewC), Mask)
      // Op(C, shuffle(V1, Mask)) -> shuffle(Op(NewC, V1), Mask)
      Value *NewLHS = ConstOp1 ? V1 : NewC;
      Value *NewRHS = ConstOp1 ? NewC : V1;
      return createBinOpShuffle(NewLHS, NewRHS, Mask);
    }
  }

  // Try to reassociate to sink a splat shuffle after a binary operation.
  if (Inst.isAssociative() && Inst.isCommutative()) {
    // Canonicalize shuffle operand as LHS.
    if (isa<ShuffleVectorInst>(RHS))
      std::swap(LHS, RHS);

    Value *X;
    ArrayRef<int> MaskC;
    int SplatIndex;
    Value *Y, *OtherOp;
    if (!match(LHS,
               m_OneUse(m_Shuffle(m_Value(X), m_Undef(), m_Mask(MaskC)))) ||
        !match(MaskC, m_SplatOrPoisonMask(SplatIndex)) ||
        X->getType() != Inst.getType() ||
        !match(RHS, m_OneUse(m_BinOp(Opcode, m_Value(Y), m_Value(OtherOp)))))
      return nullptr;

    // FIXME: This may not be safe if the analysis allows undef elements. By
    //        moving 'Y' before the splat shuffle, we are implicitly assuming
    //        that it is not undef/poison at the splat index.
    if (isSplatValue(OtherOp, SplatIndex)) {
      std::swap(Y, OtherOp);
    } else if (!isSplatValue(Y, SplatIndex)) {
      return nullptr;
    }

    // X and Y are splatted values, so perform the binary operation on those
    // values followed by a splat followed by the 2nd binary operation:
    // bo (splat X), (bo Y, OtherOp) --> bo (splat (bo X, Y)), OtherOp
    Value *NewBO = Builder.CreateBinOp(Opcode, X, Y);
    SmallVector<int, 8> NewMask(MaskC.size(), SplatIndex);
    Value *NewSplat = Builder.CreateShuffleVector(NewBO, NewMask);
    Instruction *R = BinaryOperator::Create(Opcode, NewSplat, OtherOp);

    // Intersect FMF on both new binops. Other (poison-generating) flags are
    // dropped to be safe.
    if (isa<FPMathOperator>(R)) {
      R->copyFastMathFlags(&Inst);
      R->andIRFlags(RHS);
    }
    if (auto *NewInstBO = dyn_cast<BinaryOperator>(NewBO))
      NewInstBO->copyIRFlags(R);
    return R;
  }

  return nullptr;
}

/// Try to narrow the width of a binop if at least 1 operand is an extend of
/// of a value. This requires a potentially expensive known bits check to make
/// sure the narrow op does not overflow.
Instruction *InstCombinerImpl::narrowMathIfNoOverflow(BinaryOperator &BO) {
  // We need at least one extended operand.
  Value *Op0 = BO.getOperand(0), *Op1 = BO.getOperand(1);

  // If this is a sub, we swap the operands since we always want an extension
  // on the RHS. The LHS can be an extension or a constant.
  if (BO.getOpcode() == Instruction::Sub)
    std::swap(Op0, Op1);

  Value *X;
  bool IsSext = match(Op0, m_SExt(m_Value(X)));
  if (!IsSext && !match(Op0, m_ZExt(m_Value(X))))
    return nullptr;

  // If both operands are the same extension from the same source type and we
  // can eliminate at least one (hasOneUse), this might work.
  CastInst::CastOps CastOpc = IsSext ? Instruction::SExt : Instruction::ZExt;
  Value *Y;
  if (!(match(Op1, m_ZExtOrSExt(m_Value(Y))) && X->getType() == Y->getType() &&
        cast<Operator>(Op1)->getOpcode() == CastOpc &&
        (Op0->hasOneUse() || Op1->hasOneUse()))) {
    // If that did not match, see if we have a suitable constant operand.
    // Truncating and extending must produce the same constant.
    Constant *WideC;
    if (!Op0->hasOneUse() || !match(Op1, m_Constant(WideC)))
      return nullptr;
    Constant *NarrowC = getLosslessTrunc(WideC, X->getType(), CastOpc);
    if (!NarrowC)
      return nullptr;
    Y = NarrowC;
  }

  // Swap back now that we found our operands.
  if (BO.getOpcode() == Instruction::Sub)
    std::swap(X, Y);

  // Both operands have narrow versions. Last step: the math must not overflow
  // in the narrow width.
  if (!willNotOverflow(BO.getOpcode(), X, Y, BO, IsSext))
    return nullptr;

  // bo (ext X), (ext Y) --> ext (bo X, Y)
  // bo (ext X), C       --> ext (bo X, C')
  Value *NarrowBO = Builder.CreateBinOp(BO.getOpcode(), X, Y, "narrow");
  if (auto *NewBinOp = dyn_cast<BinaryOperator>(NarrowBO)) {
    if (IsSext)
      NewBinOp->setHasNoSignedWrap();
    else
      NewBinOp->setHasNoUnsignedWrap();
  }
  return CastInst::Create(CastOpc, NarrowBO, BO.getType());
}

static bool isMergedGEPInBounds(GEPOperator &GEP1, GEPOperator &GEP2) {
  return GEP1.isInBounds() && GEP2.isInBounds();
}

/// Thread a GEP operation with constant indices through the constant true/false
/// arms of a select.
static Instruction *foldSelectGEP(GetElementPtrInst &GEP,
                                  InstCombiner::BuilderTy &Builder) {
  if (!GEP.hasAllConstantIndices())
    return nullptr;

  Instruction *Sel;
  Value *Cond;
  Constant *TrueC, *FalseC;
  if (!match(GEP.getPointerOperand(), m_Instruction(Sel)) ||
      !match(Sel,
             m_Select(m_Value(Cond), m_Constant(TrueC), m_Constant(FalseC))))
    return nullptr;

  // gep (select Cond, TrueC, FalseC), IndexC --> select Cond, TrueC', FalseC'
  // Propagate 'inbounds' and metadata from existing instructions.
  // Note: using IRBuilder to create the constants for efficiency.
  SmallVector<Value *, 4> IndexC(GEP.indices());
  GEPNoWrapFlags NW = GEP.getNoWrapFlags();
  Type *Ty = GEP.getSourceElementType();
  Value *NewTrueC = Builder.CreateGEP(Ty, TrueC, IndexC, "", NW);
  Value *NewFalseC = Builder.CreateGEP(Ty, FalseC, IndexC, "", NW);
  return SelectInst::Create(Cond, NewTrueC, NewFalseC, "", nullptr, Sel);
}

// Canonicalization:
// gep T, (gep i8, base, C1), (Index + C2) into
// gep T, (gep i8, base, C1 + C2 * sizeof(T)), Index
static Instruction *canonicalizeGEPOfConstGEPI8(GetElementPtrInst &GEP,
                                                GEPOperator *Src,
                                                InstCombinerImpl &IC) {
  if (GEP.getNumIndices() != 1)
    return nullptr;
  auto &DL = IC.getDataLayout();
  Value *Base;
  const APInt *C1;
  if (!match(Src, m_PtrAdd(m_Value(Base), m_APInt(C1))))
    return nullptr;
  Value *VarIndex;
  const APInt *C2;
  Type *PtrTy = Src->getType()->getScalarType();
  unsigned IndexSizeInBits = DL.getIndexTypeSizeInBits(PtrTy);
  if (!match(GEP.getOperand(1), m_AddLike(m_Value(VarIndex), m_APInt(C2))))
    return nullptr;
  if (C1->getBitWidth() != IndexSizeInBits ||
      C2->getBitWidth() != IndexSizeInBits)
    return nullptr;
  Type *BaseType = GEP.getSourceElementType();
  if (isa<ScalableVectorType>(BaseType))
    return nullptr;
  APInt TypeSize(IndexSizeInBits, DL.getTypeAllocSize(BaseType));
  APInt NewOffset = TypeSize * *C2 + *C1;
  if (NewOffset.isZero() ||
      (Src->hasOneUse() && GEP.getOperand(1)->hasOneUse())) {
    Value *GEPConst =
        IC.Builder.CreatePtrAdd(Base, IC.Builder.getInt(NewOffset));
    return GetElementPtrInst::Create(BaseType, GEPConst, VarIndex);
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitGEPOfGEP(GetElementPtrInst &GEP,
                                             GEPOperator *Src) {
  // Combine Indices - If the source pointer to this getelementptr instruction
  // is a getelementptr instruction with matching element type, combine the
  // indices of the two getelementptr instructions into a single instruction.
  if (!shouldMergeGEPs(*cast<GEPOperator>(&GEP), *Src))
    return nullptr;

  if (auto *I = canonicalizeGEPOfConstGEPI8(GEP, Src, *this))
    return I;

  // For constant GEPs, use a more general offset-based folding approach.
  Type *PtrTy = Src->getType()->getScalarType();
  if (GEP.hasAllConstantIndices() &&
      (Src->hasOneUse() || Src->hasAllConstantIndices())) {
    // Split Src into a variable part and a constant suffix.
    gep_type_iterator GTI = gep_type_begin(*Src);
    Type *BaseType = GTI.getIndexedType();
    bool IsFirstType = true;
    unsigned NumVarIndices = 0;
    for (auto Pair : enumerate(Src->indices())) {
      if (!isa<ConstantInt>(Pair.value())) {
        BaseType = GTI.getIndexedType();
        IsFirstType = false;
        NumVarIndices = Pair.index() + 1;
      }
      ++GTI;
    }

    // Determine the offset for the constant suffix of Src.
    APInt Offset(DL.getIndexTypeSizeInBits(PtrTy), 0);
    if (NumVarIndices != Src->getNumIndices()) {
      // FIXME: getIndexedOffsetInType() does not handled scalable vectors.
      if (BaseType->isScalableTy())
        return nullptr;

      SmallVector<Value *> ConstantIndices;
      if (!IsFirstType)
        ConstantIndices.push_back(
            Constant::getNullValue(Type::getInt32Ty(GEP.getContext())));
      append_range(ConstantIndices, drop_begin(Src->indices(), NumVarIndices));
      Offset += DL.getIndexedOffsetInType(BaseType, ConstantIndices);
    }

    // Add the offset for GEP (which is fully constant).
    if (!GEP.accumulateConstantOffset(DL, Offset))
      return nullptr;

    APInt OffsetOld = Offset;
    // Convert the total offset back into indices.
    SmallVector<APInt> ConstIndices =
        DL.getGEPIndicesForOffset(BaseType, Offset);
    if (!Offset.isZero() || (!IsFirstType && !ConstIndices[0].isZero())) {
      // If both GEP are constant-indexed, and cannot be merged in either way,
      // convert them to a GEP of i8.
      if (Src->hasAllConstantIndices())
        return replaceInstUsesWith(
            GEP, Builder.CreateGEP(
                     Builder.getInt8Ty(), Src->getOperand(0),
                     Builder.getInt(OffsetOld), "",
                     isMergedGEPInBounds(*Src, *cast<GEPOperator>(&GEP))));
      return nullptr;
    }

    bool IsInBounds = isMergedGEPInBounds(*Src, *cast<GEPOperator>(&GEP));
    SmallVector<Value *> Indices;
    append_range(Indices, drop_end(Src->indices(),
                                   Src->getNumIndices() - NumVarIndices));
    for (const APInt &Idx : drop_begin(ConstIndices, !IsFirstType)) {
      Indices.push_back(ConstantInt::get(GEP.getContext(), Idx));
      // Even if the total offset is inbounds, we may end up representing it
      // by first performing a larger negative offset, and then a smaller
      // positive one. The large negative offset might go out of bounds. Only
      // preserve inbounds if all signs are the same.
      IsInBounds &= Idx.isNonNegative() == ConstIndices[0].isNonNegative();
    }

    return replaceInstUsesWith(
        GEP, Builder.CreateGEP(Src->getSourceElementType(), Src->getOperand(0),
                               Indices, "", IsInBounds));
  }

  if (Src->getResultElementType() != GEP.getSourceElementType())
    return nullptr;

  SmallVector<Value*, 8> Indices;

  // Find out whether the last index in the source GEP is a sequential idx.
  bool EndsWithSequential = false;
  for (gep_type_iterator I = gep_type_begin(*Src), E = gep_type_end(*Src);
       I != E; ++I)
    EndsWithSequential = I.isSequential();

  // Can we combine the two pointer arithmetics offsets?
  if (EndsWithSequential) {
    // Replace: gep (gep %P, long B), long A, ...
    // With:    T = long A+B; gep %P, T, ...
    Value *SO1 = Src->getOperand(Src->getNumOperands()-1);
    Value *GO1 = GEP.getOperand(1);

    // If they aren't the same type, then the input hasn't been processed
    // by the loop above yet (which canonicalizes sequential index types to
    // intptr_t).  Just avoid transforming this until the input has been
    // normalized.
    if (SO1->getType() != GO1->getType())
      return nullptr;

    Value *Sum =
        simplifyAddInst(GO1, SO1, false, false, SQ.getWithInstruction(&GEP));
    // Only do the combine when we are sure the cost after the
    // merge is never more than that before the merge.
    if (Sum == nullptr)
      return nullptr;

    // Update the GEP in place if possible.
    if (Src->getNumOperands() == 2) {
      GEP.setIsInBounds(isMergedGEPInBounds(*Src, *cast<GEPOperator>(&GEP)));
      replaceOperand(GEP, 0, Src->getOperand(0));
      replaceOperand(GEP, 1, Sum);
      return &GEP;
    }
    Indices.append(Src->op_begin()+1, Src->op_end()-1);
    Indices.push_back(Sum);
    Indices.append(GEP.op_begin()+2, GEP.op_end());
  } else if (isa<Constant>(*GEP.idx_begin()) &&
             cast<Constant>(*GEP.idx_begin())->isNullValue() &&
             Src->getNumOperands() != 1) {
    // Otherwise we can do the fold if the first index of the GEP is a zero
    Indices.append(Src->op_begin()+1, Src->op_end());
    Indices.append(GEP.idx_begin()+1, GEP.idx_end());
  }

  if (!Indices.empty())
    return replaceInstUsesWith(
        GEP, Builder.CreateGEP(
                 Src->getSourceElementType(), Src->getOperand(0), Indices, "",
                 isMergedGEPInBounds(*Src, *cast<GEPOperator>(&GEP))));

  return nullptr;
}

Value *InstCombiner::getFreelyInvertedImpl(Value *V, bool WillInvertAllUses,
                                           BuilderTy *Builder,
                                           bool &DoesConsume, unsigned Depth) {
  static Value *const NonNull = reinterpret_cast<Value *>(uintptr_t(1));
  // ~(~(X)) -> X.
  Value *A, *B;
  if (match(V, m_Not(m_Value(A)))) {
    DoesConsume = true;
    return A;
  }

  Constant *C;
  // Constants can be considered to be not'ed values.
  if (match(V, m_ImmConstant(C)))
    return ConstantExpr::getNot(C);

  if (Depth++ >= MaxAnalysisRecursionDepth)
    return nullptr;

  // The rest of the cases require that we invert all uses so don't bother
  // doing the analysis if we know we can't use the result.
  if (!WillInvertAllUses)
    return nullptr;

  // Compares can be inverted if all of their uses are being modified to use
  // the ~V.
  if (auto *I = dyn_cast<CmpInst>(V)) {
    if (Builder != nullptr)
      return Builder->CreateCmp(I->getInversePredicate(), I->getOperand(0),
                                I->getOperand(1));
    return NonNull;
  }

  // If `V` is of the form `A + B` then `-1 - V` can be folded into
  // `(-1 - B) - A` if we are willing to invert all of the uses.
  if (match(V, m_Add(m_Value(A), m_Value(B)))) {
    if (auto *BV = getFreelyInvertedImpl(B, B->hasOneUse(), Builder,
                                         DoesConsume, Depth))
      return Builder ? Builder->CreateSub(BV, A) : NonNull;
    if (auto *AV = getFreelyInvertedImpl(A, A->hasOneUse(), Builder,
                                         DoesConsume, Depth))
      return Builder ? Builder->CreateSub(AV, B) : NonNull;
    return nullptr;
  }

  // If `V` is of the form `A ^ ~B` then `~(A ^ ~B)` can be folded
  // into `A ^ B` if we are willing to invert all of the uses.
  if (match(V, m_Xor(m_Value(A), m_Value(B)))) {
    if (auto *BV = getFreelyInvertedImpl(B, B->hasOneUse(), Builder,
                                         DoesConsume, Depth))
      return Builder ? Builder->CreateXor(A, BV) : NonNull;
    if (auto *AV = getFreelyInvertedImpl(A, A->hasOneUse(), Builder,
                                         DoesConsume, Depth))
      return Builder ? Builder->CreateXor(AV, B) : NonNull;
    return nullptr;
  }

  // If `V` is of the form `B - A` then `-1 - V` can be folded into
  // `A + (-1 - B)` if we are willing to invert all of the uses.
  if (match(V, m_Sub(m_Value(A), m_Value(B)))) {
    if (auto *AV = getFreelyInvertedImpl(A, A->hasOneUse(), Builder,
                                         DoesConsume, Depth))
      return Builder ? Builder->CreateAdd(AV, B) : NonNull;
    return nullptr;
  }

  // If `V` is of the form `(~A) s>> B` then `~((~A) s>> B)` can be folded
  // into `A s>> B` if we are willing to invert all of the uses.
  if (match(V, m_AShr(m_Value(A), m_Value(B)))) {
    if (auto *AV = getFreelyInvertedImpl(A, A->hasOneUse(), Builder,
                                         DoesConsume, Depth))
      return Builder ? Builder->CreateAShr(AV, B) : NonNull;
    return nullptr;
  }

  Value *Cond;
  // LogicOps are special in that we canonicalize them at the cost of an
  // instruction.
  bool IsSelect = match(V, m_Select(m_Value(Cond), m_Value(A), m_Value(B))) &&
                  !shouldAvoidAbsorbingNotIntoSelect(*cast<SelectInst>(V));
  // Selects/min/max with invertible operands are freely invertible
  if (IsSelect || match(V, m_MaxOrMin(m_Value(A), m_Value(B)))) {
    bool LocalDoesConsume = DoesConsume;
    if (!getFreelyInvertedImpl(B, B->hasOneUse(), /*Builder*/ nullptr,
                               LocalDoesConsume, Depth))
      return nullptr;
    if (Value *NotA = getFreelyInvertedImpl(A, A->hasOneUse(), Builder,
                                            LocalDoesConsume, Depth)) {
      DoesConsume = LocalDoesConsume;
      if (Builder != nullptr) {
        Value *NotB = getFreelyInvertedImpl(B, B->hasOneUse(), Builder,
                                            DoesConsume, Depth);
        assert(NotB != nullptr &&
               "Unable to build inverted value for known freely invertable op");
        if (auto *II = dyn_cast<IntrinsicInst>(V))
          return Builder->CreateBinaryIntrinsic(
              getInverseMinMaxIntrinsic(II->getIntrinsicID()), NotA, NotB);
        return Builder->CreateSelect(Cond, NotA, NotB);
      }
      return NonNull;
    }
  }

  if (PHINode *PN = dyn_cast<PHINode>(V)) {
    bool LocalDoesConsume = DoesConsume;
    SmallVector<std::pair<Value *, BasicBlock *>, 8> IncomingValues;
    for (Use &U : PN->operands()) {
      BasicBlock *IncomingBlock = PN->getIncomingBlock(U);
      Value *NewIncomingVal = getFreelyInvertedImpl(
          U.get(), /*WillInvertAllUses=*/false,
          /*Builder=*/nullptr, LocalDoesConsume, MaxAnalysisRecursionDepth - 1);
      if (NewIncomingVal == nullptr)
        return nullptr;
      // Make sure that we can safely erase the original PHI node.
      if (NewIncomingVal == V)
        return nullptr;
      if (Builder != nullptr)
        IncomingValues.emplace_back(NewIncomingVal, IncomingBlock);
    }

    DoesConsume = LocalDoesConsume;
    if (Builder != nullptr) {
      IRBuilderBase::InsertPointGuard Guard(*Builder);
      Builder->SetInsertPoint(PN);
      PHINode *NewPN =
          Builder->CreatePHI(PN->getType(), PN->getNumIncomingValues());
      for (auto [Val, Pred] : IncomingValues)
        NewPN->addIncoming(Val, Pred);
      return NewPN;
    }
    return NonNull;
  }

  if (match(V, m_SExtLike(m_Value(A)))) {
    if (auto *AV = getFreelyInvertedImpl(A, A->hasOneUse(), Builder,
                                         DoesConsume, Depth))
      return Builder ? Builder->CreateSExt(AV, V->getType()) : NonNull;
    return nullptr;
  }

  if (match(V, m_Trunc(m_Value(A)))) {
    if (auto *AV = getFreelyInvertedImpl(A, A->hasOneUse(), Builder,
                                         DoesConsume, Depth))
      return Builder ? Builder->CreateTrunc(AV, V->getType()) : NonNull;
    return nullptr;
  }

  // De Morgan's Laws:
  // (~(A | B)) -> (~A & ~B)
  // (~(A & B)) -> (~A | ~B)
  auto TryInvertAndOrUsingDeMorgan = [&](Instruction::BinaryOps Opcode,
                                         bool IsLogical, Value *A,
                                         Value *B) -> Value * {
    bool LocalDoesConsume = DoesConsume;
    if (!getFreelyInvertedImpl(B, B->hasOneUse(), /*Builder=*/nullptr,
                               LocalDoesConsume, Depth))
      return nullptr;
    if (auto *NotA = getFreelyInvertedImpl(A, A->hasOneUse(), Builder,
                                           LocalDoesConsume, Depth)) {
      auto *NotB = getFreelyInvertedImpl(B, B->hasOneUse(), Builder,
                                         LocalDoesConsume, Depth);
      DoesConsume = LocalDoesConsume;
      if (IsLogical)
        return Builder ? Builder->CreateLogicalOp(Opcode, NotA, NotB) : NonNull;
      return Builder ? Builder->CreateBinOp(Opcode, NotA, NotB) : NonNull;
    }

    return nullptr;
  };

  if (match(V, m_Or(m_Value(A), m_Value(B))))
    return TryInvertAndOrUsingDeMorgan(Instruction::And, /*IsLogical=*/false, A,
                                       B);

  if (match(V, m_And(m_Value(A), m_Value(B))))
    return TryInvertAndOrUsingDeMorgan(Instruction::Or, /*IsLogical=*/false, A,
                                       B);

  if (match(V, m_LogicalOr(m_Value(A), m_Value(B))))
    return TryInvertAndOrUsingDeMorgan(Instruction::And, /*IsLogical=*/true, A,
                                       B);

  if (match(V, m_LogicalAnd(m_Value(A), m_Value(B))))
    return TryInvertAndOrUsingDeMorgan(Instruction::Or, /*IsLogical=*/true, A,
                                       B);

  return nullptr;
}

Instruction *InstCombinerImpl::visitGetElementPtrInst(GetElementPtrInst &GEP) {
  Value *PtrOp = GEP.getOperand(0);
  SmallVector<Value *, 8> Indices(GEP.indices());
  Type *GEPType = GEP.getType();
  Type *GEPEltType = GEP.getSourceElementType();
  if (Value *V =
          simplifyGEPInst(GEPEltType, PtrOp, Indices, GEP.getNoWrapFlags(),
                          SQ.getWithInstruction(&GEP)))
    return replaceInstUsesWith(GEP, V);

  // For vector geps, use the generic demanded vector support.
  // Skip if GEP return type is scalable. The number of elements is unknown at
  // compile-time.
  if (auto *GEPFVTy = dyn_cast<FixedVectorType>(GEPType)) {
    auto VWidth = GEPFVTy->getNumElements();
    APInt PoisonElts(VWidth, 0);
    APInt AllOnesEltMask(APInt::getAllOnes(VWidth));
    if (Value *V = SimplifyDemandedVectorElts(&GEP, AllOnesEltMask,
                                              PoisonElts)) {
      if (V != &GEP)
        return replaceInstUsesWith(GEP, V);
      return &GEP;
    }

    // TODO: 1) Scalarize splat operands, 2) scalarize entire instruction if
    // possible (decide on canonical form for pointer broadcast), 3) exploit
    // undef elements to decrease demanded bits
  }

  // Eliminate unneeded casts for indices, and replace indices which displace
  // by multiples of a zero size type with zero.
  bool MadeChange = false;

  // Index width may not be the same width as pointer width.
  // Data layout chooses the right type based on supported integer types.
  Type *NewScalarIndexTy =
      DL.getIndexType(GEP.getPointerOperandType()->getScalarType());

  gep_type_iterator GTI = gep_type_begin(GEP);
  for (User::op_iterator I = GEP.op_begin() + 1, E = GEP.op_end(); I != E;
       ++I, ++GTI) {
    // Skip indices into struct types.
    if (GTI.isStruct())
      continue;

    Type *IndexTy = (*I)->getType();
    Type *NewIndexType =
        IndexTy->isVectorTy()
            ? VectorType::get(NewScalarIndexTy,
                              cast<VectorType>(IndexTy)->getElementCount())
            : NewScalarIndexTy;

    // If the element type has zero size then any index over it is equivalent
    // to an index of zero, so replace it with zero if it is not zero already.
    Type *EltTy = GTI.getIndexedType();
    if (EltTy->isSized() && DL.getTypeAllocSize(EltTy).isZero())
      if (!isa<Constant>(*I) || !match(I->get(), m_Zero())) {
        *I = Constant::getNullValue(NewIndexType);
        MadeChange = true;
      }

    if (IndexTy != NewIndexType) {
      // If we are using a wider index than needed for this platform, shrink
      // it to what we need.  If narrower, sign-extend it to what we need.
      // This explicit cast can make subsequent optimizations more obvious.
      *I = Builder.CreateIntCast(*I, NewIndexType, true);
      MadeChange = true;
    }
  }
  if (MadeChange)
    return &GEP;

  // Canonicalize constant GEPs to i8 type.
  if (!GEPEltType->isIntegerTy(8) && GEP.hasAllConstantIndices()) {
    APInt Offset(DL.getIndexTypeSizeInBits(GEPType), 0);
    if (GEP.accumulateConstantOffset(DL, Offset))
      return replaceInstUsesWith(
          GEP, Builder.CreatePtrAdd(PtrOp, Builder.getInt(Offset), "",
                                    GEP.getNoWrapFlags()));
  }

  // Canonicalize
  //  - scalable GEPs to an explicit offset using the llvm.vscale intrinsic.
  //    This has better support in BasicAA.
  //  - gep i32 p, mul(O, C) -> gep i8, p, mul(O, C*4) to fold the two
  //    multiplies together.
  if (GEPEltType->isScalableTy() ||
      (!GEPEltType->isIntegerTy(8) && GEP.getNumIndices() == 1 &&
       match(GEP.getOperand(1),
             m_OneUse(m_CombineOr(m_Mul(m_Value(), m_ConstantInt()),
                                  m_Shl(m_Value(), m_ConstantInt())))))) {
    Value *Offset = EmitGEPOffset(cast<GEPOperator>(&GEP));
    return replaceInstUsesWith(
        GEP, Builder.CreatePtrAdd(PtrOp, Offset, "", GEP.getNoWrapFlags()));
  }

  // Check to see if the inputs to the PHI node are getelementptr instructions.
  if (auto *PN = dyn_cast<PHINode>(PtrOp)) {
    auto *Op1 = dyn_cast<GetElementPtrInst>(PN->getOperand(0));
    if (!Op1)
      return nullptr;

    // Don't fold a GEP into itself through a PHI node. This can only happen
    // through the back-edge of a loop. Folding a GEP into itself means that
    // the value of the previous iteration needs to be stored in the meantime,
    // thus requiring an additional register variable to be live, but not
    // actually achieving anything (the GEP still needs to be executed once per
    // loop iteration).
    if (Op1 == &GEP)
      return nullptr;

    int DI = -1;

    for (auto I = PN->op_begin()+1, E = PN->op_end(); I !=E; ++I) {
      auto *Op2 = dyn_cast<GetElementPtrInst>(*I);
      if (!Op2 || Op1->getNumOperands() != Op2->getNumOperands() ||
          Op1->getSourceElementType() != Op2->getSourceElementType())
        return nullptr;

      // As for Op1 above, don't try to fold a GEP into itself.
      if (Op2 == &GEP)
        return nullptr;

      // Keep track of the type as we walk the GEP.
      Type *CurTy = nullptr;

      for (unsigned J = 0, F = Op1->getNumOperands(); J != F; ++J) {
        if (Op1->getOperand(J)->getType() != Op2->getOperand(J)->getType())
          return nullptr;

        if (Op1->getOperand(J) != Op2->getOperand(J)) {
          if (DI == -1) {
            // We have not seen any differences yet in the GEPs feeding the
            // PHI yet, so we record this one if it is allowed to be a
            // variable.

            // The first two arguments can vary for any GEP, the rest have to be
            // static for struct slots
            if (J > 1) {
              assert(CurTy && "No current type?");
              if (CurTy->isStructTy())
                return nullptr;
            }

            DI = J;
          } else {
            // The GEP is different by more than one input. While this could be
            // extended to support GEPs that vary by more than one variable it
            // doesn't make sense since it greatly increases the complexity and
            // would result in an R+R+R addressing mode which no backend
            // directly supports and would need to be broken into several
            // simpler instructions anyway.
            return nullptr;
          }
        }

        // Sink down a layer of the type for the next iteration.
        if (J > 0) {
          if (J == 1) {
            CurTy = Op1->getSourceElementType();
          } else {
            CurTy =
                GetElementPtrInst::getTypeAtIndex(CurTy, Op1->getOperand(J));
          }
        }
      }
    }

    // If not all GEPs are identical we'll have to create a new PHI node.
    // Check that the old PHI node has only one use so that it will get
    // removed.
    if (DI != -1 && !PN->hasOneUse())
      return nullptr;

    auto *NewGEP = cast<GetElementPtrInst>(Op1->clone());
    if (DI == -1) {
      // All the GEPs feeding the PHI are identical. Clone one down into our
      // BB so that it can be merged with the current GEP.
    } else {
      // All the GEPs feeding the PHI differ at a single offset. Clone a GEP
      // into the current block so it can be merged, and create a new PHI to
      // set that index.
      PHINode *NewPN;
      {
        IRBuilderBase::InsertPointGuard Guard(Builder);
        Builder.SetInsertPoint(PN);
        NewPN = Builder.CreatePHI(Op1->getOperand(DI)->getType(),
                                  PN->getNumOperands());
      }

      for (auto &I : PN->operands())
        NewPN->addIncoming(cast<GEPOperator>(I)->getOperand(DI),
                           PN->getIncomingBlock(I));

      NewGEP->setOperand(DI, NewPN);
    }

    NewGEP->insertBefore(*GEP.getParent(), GEP.getParent()->getFirstInsertionPt());
    return replaceOperand(GEP, 0, NewGEP);
  }

  if (auto *Src = dyn_cast<GEPOperator>(PtrOp))
    if (Instruction *I = visitGEPOfGEP(GEP, Src))
      return I;

  if (GEP.getNumIndices() == 1) {
    unsigned AS = GEP.getPointerAddressSpace();
    if (GEP.getOperand(1)->getType()->getScalarSizeInBits() ==
        DL.getIndexSizeInBits(AS)) {
      uint64_t TyAllocSize = DL.getTypeAllocSize(GEPEltType).getFixedValue();

      if (TyAllocSize == 1) {
        // Canonicalize (gep i8* X, (ptrtoint Y)-(ptrtoint X)) to (bitcast Y),
        // but only if the result pointer is only used as if it were an integer,
        // or both point to the same underlying object (otherwise provenance is
        // not necessarily retained).
        Value *X = GEP.getPointerOperand();
        Value *Y;
        if (match(GEP.getOperand(1),
                  m_Sub(m_PtrToInt(m_Value(Y)), m_PtrToInt(m_Specific(X)))) &&
            GEPType == Y->getType()) {
          bool HasSameUnderlyingObject =
              getUnderlyingObject(X) == getUnderlyingObject(Y);
          bool Changed = false;
          GEP.replaceUsesWithIf(Y, [&](Use &U) {
            bool ShouldReplace = HasSameUnderlyingObject ||
                                 isa<ICmpInst>(U.getUser()) ||
                                 isa<PtrToIntInst>(U.getUser());
            Changed |= ShouldReplace;
            return ShouldReplace;
          });
          return Changed ? &GEP : nullptr;
        }
      } else if (auto *ExactIns =
                     dyn_cast<PossiblyExactOperator>(GEP.getOperand(1))) {
        // Canonicalize (gep T* X, V / sizeof(T)) to (gep i8* X, V)
        Value *V;
        if (ExactIns->isExact()) {
          if ((has_single_bit(TyAllocSize) &&
               match(GEP.getOperand(1),
                     m_Shr(m_Value(V),
                           m_SpecificInt(countr_zero(TyAllocSize))))) ||
              match(GEP.getOperand(1),
                    m_IDiv(m_Value(V), m_SpecificInt(TyAllocSize)))) {
            return GetElementPtrInst::Create(Builder.getInt8Ty(),
                                             GEP.getPointerOperand(), V,
                                             GEP.getNoWrapFlags());
          }
        }
        if (ExactIns->isExact() && ExactIns->hasOneUse()) {
          // Try to canonicalize non-i8 element type to i8 if the index is an
          // exact instruction. If the index is an exact instruction (div/shr)
          // with a constant RHS, we can fold the non-i8 element scale into the
          // div/shr (similiar to the mul case, just inverted).
          const APInt *C;
          std::optional<APInt> NewC;
          if (has_single_bit(TyAllocSize) &&
              match(ExactIns, m_Shr(m_Value(V), m_APInt(C))) &&
              C->uge(countr_zero(TyAllocSize)))
            NewC = *C - countr_zero(TyAllocSize);
          else if (match(ExactIns, m_UDiv(m_Value(V), m_APInt(C)))) {
            APInt Quot;
            uint64_t Rem;
            APInt::udivrem(*C, TyAllocSize, Quot, Rem);
            if (Rem == 0)
              NewC = Quot;
          } else if (match(ExactIns, m_SDiv(m_Value(V), m_APInt(C)))) {
            APInt Quot;
            int64_t Rem;
            APInt::sdivrem(*C, TyAllocSize, Quot, Rem);
            // For sdiv we need to make sure we arent creating INT_MIN / -1.
            if (!Quot.isAllOnes() && Rem == 0)
              NewC = Quot;
          }

          if (NewC.has_value()) {
            Value *NewOp = Builder.CreateBinOp(
                static_cast<Instruction::BinaryOps>(ExactIns->getOpcode()), V,
                ConstantInt::get(V->getType(), *NewC));
            cast<BinaryOperator>(NewOp)->setIsExact();
            return GetElementPtrInst::Create(Builder.getInt8Ty(),
                                             GEP.getPointerOperand(), NewOp,
                                             GEP.getNoWrapFlags());
          }
        }
      }
    }
  }
  // We do not handle pointer-vector geps here.
  if (GEPType->isVectorTy())
    return nullptr;

  if (GEP.getNumIndices() == 1) {
    // We can only preserve inbounds if the original gep is inbounds, the add
    // is nsw, and the add operands are non-negative.
    auto CanPreserveInBounds = [&](bool AddIsNSW, Value *Idx1, Value *Idx2) {
      SimplifyQuery Q = SQ.getWithInstruction(&GEP);
      return GEP.isInBounds() && AddIsNSW && isKnownNonNegative(Idx1, Q) &&
             isKnownNonNegative(Idx2, Q);
    };

    // Try to replace ADD + GEP with GEP + GEP.
    Value *Idx1, *Idx2;
    if (match(GEP.getOperand(1),
              m_OneUse(m_Add(m_Value(Idx1), m_Value(Idx2))))) {
      //   %idx = add i64 %idx1, %idx2
      //   %gep = getelementptr i32, ptr %ptr, i64 %idx
      // as:
      //   %newptr = getelementptr i32, ptr %ptr, i64 %idx1
      //   %newgep = getelementptr i32, ptr %newptr, i64 %idx2
      bool IsInBounds = CanPreserveInBounds(
          cast<OverflowingBinaryOperator>(GEP.getOperand(1))->hasNoSignedWrap(),
          Idx1, Idx2);
      auto *NewPtr =
          Builder.CreateGEP(GEP.getSourceElementType(), GEP.getPointerOperand(),
                            Idx1, "", IsInBounds);
      return replaceInstUsesWith(
          GEP, Builder.CreateGEP(GEP.getSourceElementType(), NewPtr, Idx2, "",
                                 IsInBounds));
    }
    ConstantInt *C;
    if (match(GEP.getOperand(1), m_OneUse(m_SExtLike(m_OneUse(m_NSWAdd(
                                     m_Value(Idx1), m_ConstantInt(C))))))) {
      // %add = add nsw i32 %idx1, idx2
      // %sidx = sext i32 %add to i64
      // %gep = getelementptr i32, ptr %ptr, i64 %sidx
      // as:
      // %newptr = getelementptr i32, ptr %ptr, i32 %idx1
      // %newgep = getelementptr i32, ptr %newptr, i32 idx2
      bool IsInBounds = CanPreserveInBounds(
          /*IsNSW=*/true, Idx1, C);
      auto *NewPtr = Builder.CreateGEP(
          GEP.getSourceElementType(), GEP.getPointerOperand(),
          Builder.CreateSExt(Idx1, GEP.getOperand(1)->getType()), "",
          IsInBounds);
      return replaceInstUsesWith(
          GEP,
          Builder.CreateGEP(GEP.getSourceElementType(), NewPtr,
                            Builder.CreateSExt(C, GEP.getOperand(1)->getType()),
                            "", IsInBounds));
    }
  }

  if (!GEP.isInBounds()) {
    unsigned IdxWidth =
        DL.getIndexSizeInBits(PtrOp->getType()->getPointerAddressSpace());
    APInt BasePtrOffset(IdxWidth, 0);
    Value *UnderlyingPtrOp =
            PtrOp->stripAndAccumulateInBoundsConstantOffsets(DL,
                                                             BasePtrOffset);
    bool CanBeNull, CanBeFreed;
    uint64_t DerefBytes = UnderlyingPtrOp->getPointerDereferenceableBytes(
        DL, CanBeNull, CanBeFreed);
    if (!CanBeNull && !CanBeFreed && DerefBytes != 0) {
      if (GEP.accumulateConstantOffset(DL, BasePtrOffset) &&
          BasePtrOffset.isNonNegative()) {
        APInt AllocSize(IdxWidth, DerefBytes);
        if (BasePtrOffset.ule(AllocSize)) {
          return GetElementPtrInst::CreateInBounds(
              GEP.getSourceElementType(), PtrOp, Indices, GEP.getName());
        }
      }
    }
  }

  if (Instruction *R = foldSelectGEP(GEP, Builder))
    return R;

  return nullptr;
}

static bool isNeverEqualToUnescapedAlloc(Value *V, const TargetLibraryInfo &TLI,
                                         Instruction *AI) {
  if (isa<ConstantPointerNull>(V))
    return true;
  if (auto *LI = dyn_cast<LoadInst>(V))
    return isa<GlobalVariable>(LI->getPointerOperand());
  // Two distinct allocations will never be equal.
  return isAllocLikeFn(V, &TLI) && V != AI;
}

/// Given a call CB which uses an address UsedV, return true if we can prove the
/// call's only possible effect is storing to V.
static bool isRemovableWrite(CallBase &CB, Value *UsedV,
                             const TargetLibraryInfo &TLI) {
  if (!CB.use_empty())
    // TODO: add recursion if returned attribute is present
    return false;

  if (CB.isTerminator())
    // TODO: remove implementation restriction
    return false;

  if (!CB.willReturn() || !CB.doesNotThrow())
    return false;

  // If the only possible side effect of the call is writing to the alloca,
  // and the result isn't used, we can safely remove any reads implied by the
  // call including those which might read the alloca itself.
  std::optional<MemoryLocation> Dest = MemoryLocation::getForDest(&CB, TLI);
  return Dest && Dest->Ptr == UsedV;
}

static bool isAllocSiteRemovable(Instruction *AI,
                                 SmallVectorImpl<WeakTrackingVH> &Users,
                                 const TargetLibraryInfo &TLI) {
  SmallVector<Instruction*, 4> Worklist;
  const std::optional<StringRef> Family = getAllocationFamily(AI, &TLI);
  Worklist.push_back(AI);

  do {
    Instruction *PI = Worklist.pop_back_val();
    for (User *U : PI->users()) {
      Instruction *I = cast<Instruction>(U);
      switch (I->getOpcode()) {
      default:
        // Give up the moment we see something we can't handle.
        return false;

      case Instruction::AddrSpaceCast:
      case Instruction::BitCast:
      case Instruction::GetElementPtr:
        Users.emplace_back(I);
        Worklist.push_back(I);
        continue;

      case Instruction::ICmp: {
        ICmpInst *ICI = cast<ICmpInst>(I);
        // We can fold eq/ne comparisons with null to false/true, respectively.
        // We also fold comparisons in some conditions provided the alloc has
        // not escaped (see isNeverEqualToUnescapedAlloc).
        if (!ICI->isEquality())
          return false;
        unsigned OtherIndex = (ICI->getOperand(0) == PI) ? 1 : 0;
        if (!isNeverEqualToUnescapedAlloc(ICI->getOperand(OtherIndex), TLI, AI))
          return false;

        // Do not fold compares to aligned_alloc calls, as they may have to
        // return null in case the required alignment cannot be satisfied,
        // unless we can prove that both alignment and size are valid.
        auto AlignmentAndSizeKnownValid = [](CallBase *CB) {
          // Check if alignment and size of a call to aligned_alloc is valid,
          // that is alignment is a power-of-2 and the size is a multiple of the
          // alignment.
          const APInt *Alignment;
          const APInt *Size;
          return match(CB->getArgOperand(0), m_APInt(Alignment)) &&
                 match(CB->getArgOperand(1), m_APInt(Size)) &&
                 Alignment->isPowerOf2() && Size->urem(*Alignment).isZero();
        };
        auto *CB = dyn_cast<CallBase>(AI);
        LibFunc TheLibFunc;
        if (CB && TLI.getLibFunc(*CB->getCalledFunction(), TheLibFunc) &&
            TLI.has(TheLibFunc) && TheLibFunc == LibFunc_aligned_alloc &&
            !AlignmentAndSizeKnownValid(CB))
          return false;
        Users.emplace_back(I);
        continue;
      }

      case Instruction::Call:
        // Ignore no-op and store intrinsics.
        if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
          switch (II->getIntrinsicID()) {
          default:
            return false;

          case Intrinsic::memmove:
          case Intrinsic::memcpy:
          case Intrinsic::memset: {
            MemIntrinsic *MI = cast<MemIntrinsic>(II);
            if (MI->isVolatile() || MI->getRawDest() != PI)
              return false;
            [[fallthrough]];
          }
          case Intrinsic::assume:
          case Intrinsic::invariant_start:
          case Intrinsic::invariant_end:
          case Intrinsic::lifetime_start:
          case Intrinsic::lifetime_end:
          case Intrinsic::objectsize:
            Users.emplace_back(I);
            continue;
          case Intrinsic::launder_invariant_group:
          case Intrinsic::strip_invariant_group:
            Users.emplace_back(I);
            Worklist.push_back(I);
            continue;
          }
        }

        if (isRemovableWrite(*cast<CallBase>(I), PI, TLI)) {
          Users.emplace_back(I);
          continue;
        }

        if (getFreedOperand(cast<CallBase>(I), &TLI) == PI &&
            getAllocationFamily(I, &TLI) == Family) {
          assert(Family);
          Users.emplace_back(I);
          continue;
        }

        if (getReallocatedOperand(cast<CallBase>(I)) == PI &&
            getAllocationFamily(I, &TLI) == Family) {
          assert(Family);
          Users.emplace_back(I);
          Worklist.push_back(I);
          continue;
        }

        return false;

      case Instruction::Store: {
        StoreInst *SI = cast<StoreInst>(I);
        if (SI->isVolatile() || SI->getPointerOperand() != PI)
          return false;
        Users.emplace_back(I);
        continue;
      }
      }
      llvm_unreachable("missing a return?");
    }
  } while (!Worklist.empty());
  return true;
}

Instruction *InstCombinerImpl::visitAllocSite(Instruction &MI) {
  assert(isa<AllocaInst>(MI) || isRemovableAlloc(&cast<CallBase>(MI), &TLI));

  // If we have a malloc call which is only used in any amount of comparisons to
  // null and free calls, delete the calls and replace the comparisons with true
  // or false as appropriate.

  // This is based on the principle that we can substitute our own allocation
  // function (which will never return null) rather than knowledge of the
  // specific function being called. In some sense this can change the permitted
  // outputs of a program (when we convert a malloc to an alloca, the fact that
  // the allocation is now on the stack is potentially visible, for example),
  // but we believe in a permissible manner.
  SmallVector<WeakTrackingVH, 64> Users;

  // If we are removing an alloca with a dbg.declare, insert dbg.value calls
  // before each store.
  SmallVector<DbgVariableIntrinsic *, 8> DVIs;
  SmallVector<DbgVariableRecord *, 8> DVRs;
  std::unique_ptr<DIBuilder> DIB;
  if (isa<AllocaInst>(MI)) {
    findDbgUsers(DVIs, &MI, &DVRs);
    DIB.reset(new DIBuilder(*MI.getModule(), /*AllowUnresolved=*/false));
  }

  if (isAllocSiteRemovable(&MI, Users, TLI)) {
    for (unsigned i = 0, e = Users.size(); i != e; ++i) {
      // Lowering all @llvm.objectsize calls first because they may
      // use a bitcast/GEP of the alloca we are removing.
      if (!Users[i])
       continue;

      Instruction *I = cast<Instruction>(&*Users[i]);

      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
        if (II->getIntrinsicID() == Intrinsic::objectsize) {
          SmallVector<Instruction *> InsertedInstructions;
          Value *Result = lowerObjectSizeCall(
              II, DL, &TLI, AA, /*MustSucceed=*/true, &InsertedInstructions);
          for (Instruction *Inserted : InsertedInstructions)
            Worklist.add(Inserted);
          replaceInstUsesWith(*I, Result);
          eraseInstFromFunction(*I);
          Users[i] = nullptr; // Skip examining in the next loop.
        }
      }
    }
    for (unsigned i = 0, e = Users.size(); i != e; ++i) {
      if (!Users[i])
        continue;

      Instruction *I = cast<Instruction>(&*Users[i]);

      if (ICmpInst *C = dyn_cast<ICmpInst>(I)) {
        replaceInstUsesWith(*C,
                            ConstantInt::get(Type::getInt1Ty(C->getContext()),
                                             C->isFalseWhenEqual()));
      } else if (auto *SI = dyn_cast<StoreInst>(I)) {
        for (auto *DVI : DVIs)
          if (DVI->isAddressOfVariable())
            ConvertDebugDeclareToDebugValue(DVI, SI, *DIB);
        for (auto *DVR : DVRs)
          if (DVR->isAddressOfVariable())
            ConvertDebugDeclareToDebugValue(DVR, SI, *DIB);
      } else {
        // Casts, GEP, or anything else: we're about to delete this instruction,
        // so it can not have any valid uses.
        replaceInstUsesWith(*I, PoisonValue::get(I->getType()));
      }
      eraseInstFromFunction(*I);
    }

    if (InvokeInst *II = dyn_cast<InvokeInst>(&MI)) {
      // Replace invoke with a NOP intrinsic to maintain the original CFG
      Module *M = II->getModule();
      Function *F = Intrinsic::getDeclaration(M, Intrinsic::donothing);
      InvokeInst::Create(F, II->getNormalDest(), II->getUnwindDest(),
                         std::nullopt, "", II->getParent());
    }

    // Remove debug intrinsics which describe the value contained within the
    // alloca. In addition to removing dbg.{declare,addr} which simply point to
    // the alloca, remove dbg.value(<alloca>, ..., DW_OP_deref)'s as well, e.g.:
    //
    // ```
    //   define void @foo(i32 %0) {
    //     %a = alloca i32                              ; Deleted.
    //     store i32 %0, i32* %a
    //     dbg.value(i32 %0, "arg0")                    ; Not deleted.
    //     dbg.value(i32* %a, "arg0", DW_OP_deref)      ; Deleted.
    //     call void @trivially_inlinable_no_op(i32* %a)
    //     ret void
    //  }
    // ```
    //
    // This may not be required if we stop describing the contents of allocas
    // using dbg.value(<alloca>, ..., DW_OP_deref), but we currently do this in
    // the LowerDbgDeclare utility.
    //
    // If there is a dead store to `%a` in @trivially_inlinable_no_op, the
    // "arg0" dbg.value may be stale after the call. However, failing to remove
    // the DW_OP_deref dbg.value causes large gaps in location coverage.
    //
    // FIXME: the Assignment Tracking project has now likely made this
    // redundant (and it's sometimes harmful).
    for (auto *DVI : DVIs)
      if (DVI->isAddressOfVariable() || DVI->getExpression()->startsWithDeref())
        DVI->eraseFromParent();
    for (auto *DVR : DVRs)
      if (DVR->isAddressOfVariable() || DVR->getExpression()->startsWithDeref())
        DVR->eraseFromParent();

    return eraseInstFromFunction(MI);
  }
  return nullptr;
}

/// Move the call to free before a NULL test.
///
/// Check if this free is accessed after its argument has been test
/// against NULL (property 0).
/// If yes, it is legal to move this call in its predecessor block.
///
/// The move is performed only if the block containing the call to free
/// will be removed, i.e.:
/// 1. it has only one predecessor P, and P has two successors
/// 2. it contains the call, noops, and an unconditional branch
/// 3. its successor is the same as its predecessor's successor
///
/// The profitability is out-of concern here and this function should
/// be called only if the caller knows this transformation would be
/// profitable (e.g., for code size).
static Instruction *tryToMoveFreeBeforeNullTest(CallInst &FI,
                                                const DataLayout &DL) {
  Value *Op = FI.getArgOperand(0);
  BasicBlock *FreeInstrBB = FI.getParent();
  BasicBlock *PredBB = FreeInstrBB->getSinglePredecessor();

  // Validate part of constraint #1: Only one predecessor
  // FIXME: We can extend the number of predecessor, but in that case, we
  //        would duplicate the call to free in each predecessor and it may
  //        not be profitable even for code size.
  if (!PredBB)
    return nullptr;

  // Validate constraint #2: Does this block contains only the call to
  //                         free, noops, and an unconditional branch?
  BasicBlock *SuccBB;
  Instruction *FreeInstrBBTerminator = FreeInstrBB->getTerminator();
  if (!match(FreeInstrBBTerminator, m_UnconditionalBr(SuccBB)))
    return nullptr;

  // If there are only 2 instructions in the block, at this point,
  // this is the call to free and unconditional.
  // If there are more than 2 instructions, check that they are noops
  // i.e., they won't hurt the performance of the generated code.
  if (FreeInstrBB->size() != 2) {
    for (const Instruction &Inst : FreeInstrBB->instructionsWithoutDebug()) {
      if (&Inst == &FI || &Inst == FreeInstrBBTerminator)
        continue;
      auto *Cast = dyn_cast<CastInst>(&Inst);
      if (!Cast || !Cast->isNoopCast(DL))
        return nullptr;
    }
  }
  // Validate the rest of constraint #1 by matching on the pred branch.
  Instruction *TI = PredBB->getTerminator();
  BasicBlock *TrueBB, *FalseBB;
  ICmpInst::Predicate Pred;
  if (!match(TI, m_Br(m_ICmp(Pred,
                             m_CombineOr(m_Specific(Op),
                                         m_Specific(Op->stripPointerCasts())),
                             m_Zero()),
                      TrueBB, FalseBB)))
    return nullptr;
  if (Pred != ICmpInst::ICMP_EQ && Pred != ICmpInst::ICMP_NE)
    return nullptr;

  // Validate constraint #3: Ensure the null case just falls through.
  if (SuccBB != (Pred == ICmpInst::ICMP_EQ ? TrueBB : FalseBB))
    return nullptr;
  assert(FreeInstrBB == (Pred == ICmpInst::ICMP_EQ ? FalseBB : TrueBB) &&
         "Broken CFG: missing edge from predecessor to successor");

  // At this point, we know that everything in FreeInstrBB can be moved
  // before TI.
  for (Instruction &Instr : llvm::make_early_inc_range(*FreeInstrBB)) {
    if (&Instr == FreeInstrBBTerminator)
      break;
    Instr.moveBeforePreserving(TI);
  }
  assert(FreeInstrBB->size() == 1 &&
         "Only the branch instruction should remain");

  // Now that we've moved the call to free before the NULL check, we have to
  // remove any attributes on its parameter that imply it's non-null, because
  // those attributes might have only been valid because of the NULL check, and
  // we can get miscompiles if we keep them. This is conservative if non-null is
  // also implied by something other than the NULL check, but it's guaranteed to
  // be correct, and the conservativeness won't matter in practice, since the
  // attributes are irrelevant for the call to free itself and the pointer
  // shouldn't be used after the call.
  AttributeList Attrs = FI.getAttributes();
  Attrs = Attrs.removeParamAttribute(FI.getContext(), 0, Attribute::NonNull);
  Attribute Dereferenceable = Attrs.getParamAttr(0, Attribute::Dereferenceable);
  if (Dereferenceable.isValid()) {
    uint64_t Bytes = Dereferenceable.getDereferenceableBytes();
    Attrs = Attrs.removeParamAttribute(FI.getContext(), 0,
                                       Attribute::Dereferenceable);
    Attrs = Attrs.addDereferenceableOrNullParamAttr(FI.getContext(), 0, Bytes);
  }
  FI.setAttributes(Attrs);

  return &FI;
}

Instruction *InstCombinerImpl::visitFree(CallInst &FI, Value *Op) {
  // free undef -> unreachable.
  if (isa<UndefValue>(Op)) {
    // Leave a marker since we can't modify the CFG here.
    CreateNonTerminatorUnreachable(&FI);
    return eraseInstFromFunction(FI);
  }

  // If we have 'free null' delete the instruction.  This can happen in stl code
  // when lots of inlining happens.
  if (isa<ConstantPointerNull>(Op))
    return eraseInstFromFunction(FI);

  // If we had free(realloc(...)) with no intervening uses, then eliminate the
  // realloc() entirely.
  CallInst *CI = dyn_cast<CallInst>(Op);
  if (CI && CI->hasOneUse())
    if (Value *ReallocatedOp = getReallocatedOperand(CI))
      return eraseInstFromFunction(*replaceInstUsesWith(*CI, ReallocatedOp));

  // If we optimize for code size, try to move the call to free before the null
  // test so that simplify cfg can remove the empty block and dead code
  // elimination the branch. I.e., helps to turn something like:
  // if (foo) free(foo);
  // into
  // free(foo);
  //
  // Note that we can only do this for 'free' and not for any flavor of
  // 'operator delete'; there is no 'operator delete' symbol for which we are
  // permitted to invent a call, even if we're passing in a null pointer.
  if (MinimizeSize) {
    LibFunc Func;
    if (TLI.getLibFunc(FI, Func) && TLI.has(Func) && Func == LibFunc_free)
      if (Instruction *I = tryToMoveFreeBeforeNullTest(FI, DL))
        return I;
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitReturnInst(ReturnInst &RI) {
  Value *RetVal = RI.getReturnValue();
  if (!RetVal || !AttributeFuncs::isNoFPClassCompatibleType(RetVal->getType()))
    return nullptr;

  Function *F = RI.getFunction();
  FPClassTest ReturnClass = F->getAttributes().getRetNoFPClass();
  if (ReturnClass == fcNone)
    return nullptr;

  KnownFPClass KnownClass;
  Value *Simplified =
      SimplifyDemandedUseFPClass(RetVal, ~ReturnClass, KnownClass, 0, &RI);
  if (!Simplified)
    return nullptr;

  return ReturnInst::Create(RI.getContext(), Simplified);
}

// WARNING: keep in sync with SimplifyCFGOpt::simplifyUnreachable()!
bool InstCombinerImpl::removeInstructionsBeforeUnreachable(Instruction &I) {
  // Try to remove the previous instruction if it must lead to unreachable.
  // This includes instructions like stores and "llvm.assume" that may not get
  // removed by simple dead code elimination.
  bool Changed = false;
  while (Instruction *Prev = I.getPrevNonDebugInstruction()) {
    // While we theoretically can erase EH, that would result in a block that
    // used to start with an EH no longer starting with EH, which is invalid.
    // To make it valid, we'd need to fixup predecessors to no longer refer to
    // this block, but that changes CFG, which is not allowed in InstCombine.
    if (Prev->isEHPad())
      break; // Can not drop any more instructions. We're done here.

    if (!isGuaranteedToTransferExecutionToSuccessor(Prev))
      break; // Can not drop any more instructions. We're done here.
    // Otherwise, this instruction can be freely erased,
    // even if it is not side-effect free.

    // A value may still have uses before we process it here (for example, in
    // another unreachable block), so convert those to poison.
    replaceInstUsesWith(*Prev, PoisonValue::get(Prev->getType()));
    eraseInstFromFunction(*Prev);
    Changed = true;
  }
  return Changed;
}

Instruction *InstCombinerImpl::visitUnreachableInst(UnreachableInst &I) {
  removeInstructionsBeforeUnreachable(I);
  return nullptr;
}

Instruction *InstCombinerImpl::visitUnconditionalBranchInst(BranchInst &BI) {
  assert(BI.isUnconditional() && "Only for unconditional branches.");

  // If this store is the second-to-last instruction in the basic block
  // (excluding debug info and bitcasts of pointers) and if the block ends with
  // an unconditional branch, try to move the store to the successor block.

  auto GetLastSinkableStore = [](BasicBlock::iterator BBI) {
    auto IsNoopInstrForStoreMerging = [](BasicBlock::iterator BBI) {
      return BBI->isDebugOrPseudoInst() ||
             (isa<BitCastInst>(BBI) && BBI->getType()->isPointerTy());
    };

    BasicBlock::iterator FirstInstr = BBI->getParent()->begin();
    do {
      if (BBI != FirstInstr)
        --BBI;
    } while (BBI != FirstInstr && IsNoopInstrForStoreMerging(BBI));

    return dyn_cast<StoreInst>(BBI);
  };

  if (StoreInst *SI = GetLastSinkableStore(BasicBlock::iterator(BI)))
    if (mergeStoreIntoSuccessor(*SI))
      return &BI;

  return nullptr;
}

void InstCombinerImpl::addDeadEdge(BasicBlock *From, BasicBlock *To,
                                   SmallVectorImpl<BasicBlock *> &Worklist) {
  if (!DeadEdges.insert({From, To}).second)
    return;

  // Replace phi node operands in successor with poison.
  for (PHINode &PN : To->phis())
    for (Use &U : PN.incoming_values())
      if (PN.getIncomingBlock(U) == From && !isa<PoisonValue>(U)) {
        replaceUse(U, PoisonValue::get(PN.getType()));
        addToWorklist(&PN);
        MadeIRChange = true;
      }

  Worklist.push_back(To);
}

// Under the assumption that I is unreachable, remove it and following
// instructions. Changes are reported directly to MadeIRChange.
void InstCombinerImpl::handleUnreachableFrom(
    Instruction *I, SmallVectorImpl<BasicBlock *> &Worklist) {
  BasicBlock *BB = I->getParent();
  for (Instruction &Inst : make_early_inc_range(
           make_range(std::next(BB->getTerminator()->getReverseIterator()),
                      std::next(I->getReverseIterator())))) {
    if (!Inst.use_empty() && !Inst.getType()->isTokenTy()) {
      replaceInstUsesWith(Inst, PoisonValue::get(Inst.getType()));
      MadeIRChange = true;
    }
    if (Inst.isEHPad() || Inst.getType()->isTokenTy())
      continue;
    // RemoveDIs: erase debug-info on this instruction manually.
    Inst.dropDbgRecords();
    eraseInstFromFunction(Inst);
    MadeIRChange = true;
  }

  SmallVector<Value *> Changed;
  if (handleUnreachableTerminator(BB->getTerminator(), Changed)) {
    MadeIRChange = true;
    for (Value *V : Changed)
      addToWorklist(cast<Instruction>(V));
  }

  // Handle potentially dead successors.
  for (BasicBlock *Succ : successors(BB))
    addDeadEdge(BB, Succ, Worklist);
}

void InstCombinerImpl::handlePotentiallyDeadBlocks(
    SmallVectorImpl<BasicBlock *> &Worklist) {
  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();
    if (!all_of(predecessors(BB), [&](BasicBlock *Pred) {
          return DeadEdges.contains({Pred, BB}) || DT.dominates(BB, Pred);
        }))
      continue;

    handleUnreachableFrom(&BB->front(), Worklist);
  }
}

void InstCombinerImpl::handlePotentiallyDeadSuccessors(BasicBlock *BB,
                                                       BasicBlock *LiveSucc) {
  SmallVector<BasicBlock *> Worklist;
  for (BasicBlock *Succ : successors(BB)) {
    // The live successor isn't dead.
    if (Succ == LiveSucc)
      continue;

    addDeadEdge(BB, Succ, Worklist);
  }

  handlePotentiallyDeadBlocks(Worklist);
}

Instruction *InstCombinerImpl::visitBranchInst(BranchInst &BI) {
  if (BI.isUnconditional())
    return visitUnconditionalBranchInst(BI);

  // Change br (not X), label True, label False to: br X, label False, True
  Value *Cond = BI.getCondition();
  Value *X;
  if (match(Cond, m_Not(m_Value(X))) && !isa<Constant>(X)) {
    // Swap Destinations and condition...
    BI.swapSuccessors();
    if (BPI)
      BPI->swapSuccEdgesProbabilities(BI.getParent());
    return replaceOperand(BI, 0, X);
  }

  // Canonicalize logical-and-with-invert as logical-or-with-invert.
  // This is done by inverting the condition and swapping successors:
  // br (X && !Y), T, F --> br !(X && !Y), F, T --> br (!X || Y), F, T
  Value *Y;
  if (isa<SelectInst>(Cond) &&
      match(Cond,
            m_OneUse(m_LogicalAnd(m_Value(X), m_OneUse(m_Not(m_Value(Y))))))) {
    Value *NotX = Builder.CreateNot(X, "not." + X->getName());
    Value *Or = Builder.CreateLogicalOr(NotX, Y);
    BI.swapSuccessors();
    if (BPI)
      BPI->swapSuccEdgesProbabilities(BI.getParent());
    return replaceOperand(BI, 0, Or);
  }

  // If the condition is irrelevant, remove the use so that other
  // transforms on the condition become more effective.
  if (!isa<ConstantInt>(Cond) && BI.getSuccessor(0) == BI.getSuccessor(1))
    return replaceOperand(BI, 0, ConstantInt::getFalse(Cond->getType()));

  // Canonicalize, for example, fcmp_one -> fcmp_oeq.
  CmpInst::Predicate Pred;
  if (match(Cond, m_OneUse(m_FCmp(Pred, m_Value(), m_Value()))) &&
      !isCanonicalPredicate(Pred)) {
    // Swap destinations and condition.
    auto *Cmp = cast<CmpInst>(Cond);
    Cmp->setPredicate(CmpInst::getInversePredicate(Pred));
    BI.swapSuccessors();
    if (BPI)
      BPI->swapSuccEdgesProbabilities(BI.getParent());
    Worklist.push(Cmp);
    return &BI;
  }

  if (isa<UndefValue>(Cond)) {
    handlePotentiallyDeadSuccessors(BI.getParent(), /*LiveSucc*/ nullptr);
    return nullptr;
  }
  if (auto *CI = dyn_cast<ConstantInt>(Cond)) {
    handlePotentiallyDeadSuccessors(BI.getParent(),
                                    BI.getSuccessor(!CI->getZExtValue()));
    return nullptr;
  }

  DC.registerBranch(&BI);
  return nullptr;
}

// Replaces (switch (select cond, X, C)/(select cond, C, X)) with (switch X) if
// we can prove that both (switch C) and (switch X) go to the default when cond
// is false/true.
static Value *simplifySwitchOnSelectUsingRanges(SwitchInst &SI,
                                                SelectInst *Select,
                                                bool IsTrueArm) {
  unsigned CstOpIdx = IsTrueArm ? 1 : 2;
  auto *C = dyn_cast<ConstantInt>(Select->getOperand(CstOpIdx));
  if (!C)
    return nullptr;

  BasicBlock *CstBB = SI.findCaseValue(C)->getCaseSuccessor();
  if (CstBB != SI.getDefaultDest())
    return nullptr;
  Value *X = Select->getOperand(3 - CstOpIdx);
  ICmpInst::Predicate Pred;
  const APInt *RHSC;
  if (!match(Select->getCondition(),
             m_ICmp(Pred, m_Specific(X), m_APInt(RHSC))))
    return nullptr;
  if (IsTrueArm)
    Pred = ICmpInst::getInversePredicate(Pred);

  // See whether we can replace the select with X
  ConstantRange CR = ConstantRange::makeExactICmpRegion(Pred, *RHSC);
  for (auto Case : SI.cases())
    if (!CR.contains(Case.getCaseValue()->getValue()))
      return nullptr;

  return X;
}

Instruction *InstCombinerImpl::visitSwitchInst(SwitchInst &SI) {
  Value *Cond = SI.getCondition();
  Value *Op0;
  ConstantInt *AddRHS;
  if (match(Cond, m_Add(m_Value(Op0), m_ConstantInt(AddRHS)))) {
    // Change 'switch (X+4) case 1:' into 'switch (X) case -3'.
    for (auto Case : SI.cases()) {
      Constant *NewCase = ConstantExpr::getSub(Case.getCaseValue(), AddRHS);
      assert(isa<ConstantInt>(NewCase) &&
             "Result of expression should be constant");
      Case.setValue(cast<ConstantInt>(NewCase));
    }
    return replaceOperand(SI, 0, Op0);
  }

  ConstantInt *SubLHS;
  if (match(Cond, m_Sub(m_ConstantInt(SubLHS), m_Value(Op0)))) {
    // Change 'switch (1-X) case 1:' into 'switch (X) case 0'.
    for (auto Case : SI.cases()) {
      Constant *NewCase = ConstantExpr::getSub(SubLHS, Case.getCaseValue());
      assert(isa<ConstantInt>(NewCase) &&
             "Result of expression should be constant");
      Case.setValue(cast<ConstantInt>(NewCase));
    }
    return replaceOperand(SI, 0, Op0);
  }

  uint64_t ShiftAmt;
  if (match(Cond, m_Shl(m_Value(Op0), m_ConstantInt(ShiftAmt))) &&
      ShiftAmt < Op0->getType()->getScalarSizeInBits() &&
      all_of(SI.cases(), [&](const auto &Case) {
        return Case.getCaseValue()->getValue().countr_zero() >= ShiftAmt;
      })) {
    // Change 'switch (X << 2) case 4:' into 'switch (X) case 1:'.
    OverflowingBinaryOperator *Shl = cast<OverflowingBinaryOperator>(Cond);
    if (Shl->hasNoUnsignedWrap() || Shl->hasNoSignedWrap() ||
        Shl->hasOneUse()) {
      Value *NewCond = Op0;
      if (!Shl->hasNoUnsignedWrap() && !Shl->hasNoSignedWrap()) {
        // If the shift may wrap, we need to mask off the shifted bits.
        unsigned BitWidth = Op0->getType()->getScalarSizeInBits();
        NewCond = Builder.CreateAnd(
            Op0, APInt::getLowBitsSet(BitWidth, BitWidth - ShiftAmt));
      }
      for (auto Case : SI.cases()) {
        const APInt &CaseVal = Case.getCaseValue()->getValue();
        APInt ShiftedCase = Shl->hasNoSignedWrap() ? CaseVal.ashr(ShiftAmt)
                                                   : CaseVal.lshr(ShiftAmt);
        Case.setValue(ConstantInt::get(SI.getContext(), ShiftedCase));
      }
      return replaceOperand(SI, 0, NewCond);
    }
  }

  // Fold switch(zext/sext(X)) into switch(X) if possible.
  if (match(Cond, m_ZExtOrSExt(m_Value(Op0)))) {
    bool IsZExt = isa<ZExtInst>(Cond);
    Type *SrcTy = Op0->getType();
    unsigned NewWidth = SrcTy->getScalarSizeInBits();

    if (all_of(SI.cases(), [&](const auto &Case) {
          const APInt &CaseVal = Case.getCaseValue()->getValue();
          return IsZExt ? CaseVal.isIntN(NewWidth)
                        : CaseVal.isSignedIntN(NewWidth);
        })) {
      for (auto &Case : SI.cases()) {
        APInt TruncatedCase = Case.getCaseValue()->getValue().trunc(NewWidth);
        Case.setValue(ConstantInt::get(SI.getContext(), TruncatedCase));
      }
      return replaceOperand(SI, 0, Op0);
    }
  }

  // Fold switch(select cond, X, Y) into switch(X/Y) if possible
  if (auto *Select = dyn_cast<SelectInst>(Cond)) {
    if (Value *V =
            simplifySwitchOnSelectUsingRanges(SI, Select, /*IsTrueArm=*/true))
      return replaceOperand(SI, 0, V);
    if (Value *V =
            simplifySwitchOnSelectUsingRanges(SI, Select, /*IsTrueArm=*/false))
      return replaceOperand(SI, 0, V);
  }

  KnownBits Known = computeKnownBits(Cond, 0, &SI);
  unsigned LeadingKnownZeros = Known.countMinLeadingZeros();
  unsigned LeadingKnownOnes = Known.countMinLeadingOnes();

  // Compute the number of leading bits we can ignore.
  // TODO: A better way to determine this would use ComputeNumSignBits().
  for (const auto &C : SI.cases()) {
    LeadingKnownZeros =
        std::min(LeadingKnownZeros, C.getCaseValue()->getValue().countl_zero());
    LeadingKnownOnes =
        std::min(LeadingKnownOnes, C.getCaseValue()->getValue().countl_one());
  }

  unsigned NewWidth = Known.getBitWidth() - std::max(LeadingKnownZeros, LeadingKnownOnes);

  // Shrink the condition operand if the new type is smaller than the old type.
  // But do not shrink to a non-standard type, because backend can't generate
  // good code for that yet.
  // TODO: We can make it aggressive again after fixing PR39569.
  if (NewWidth > 0 && NewWidth < Known.getBitWidth() &&
      shouldChangeType(Known.getBitWidth(), NewWidth)) {
    IntegerType *Ty = IntegerType::get(SI.getContext(), NewWidth);
    Builder.SetInsertPoint(&SI);
    Value *NewCond = Builder.CreateTrunc(Cond, Ty, "trunc");

    for (auto Case : SI.cases()) {
      APInt TruncatedCase = Case.getCaseValue()->getValue().trunc(NewWidth);
      Case.setValue(ConstantInt::get(SI.getContext(), TruncatedCase));
    }
    return replaceOperand(SI, 0, NewCond);
  }

  if (isa<UndefValue>(Cond)) {
    handlePotentiallyDeadSuccessors(SI.getParent(), /*LiveSucc*/ nullptr);
    return nullptr;
  }
  if (auto *CI = dyn_cast<ConstantInt>(Cond)) {
    handlePotentiallyDeadSuccessors(SI.getParent(),
                                    SI.findCaseValue(CI)->getCaseSuccessor());
    return nullptr;
  }

  return nullptr;
}

Instruction *
InstCombinerImpl::foldExtractOfOverflowIntrinsic(ExtractValueInst &EV) {
  auto *WO = dyn_cast<WithOverflowInst>(EV.getAggregateOperand());
  if (!WO)
    return nullptr;

  Intrinsic::ID OvID = WO->getIntrinsicID();
  const APInt *C = nullptr;
  if (match(WO->getRHS(), m_APIntAllowPoison(C))) {
    if (*EV.idx_begin() == 0 && (OvID == Intrinsic::smul_with_overflow ||
                                 OvID == Intrinsic::umul_with_overflow)) {
      // extractvalue (any_mul_with_overflow X, -1), 0 --> -X
      if (C->isAllOnes())
        return BinaryOperator::CreateNeg(WO->getLHS());
      // extractvalue (any_mul_with_overflow X, 2^n), 0 --> X << n
      if (C->isPowerOf2()) {
        return BinaryOperator::CreateShl(
            WO->getLHS(),
            ConstantInt::get(WO->getLHS()->getType(), C->logBase2()));
      }
    }
  }

  // We're extracting from an overflow intrinsic. See if we're the only user.
  // That allows us to simplify multiple result intrinsics to simpler things
  // that just get one value.
  if (!WO->hasOneUse())
    return nullptr;

  // Check if we're grabbing only the result of a 'with overflow' intrinsic
  // and replace it with a traditional binary instruction.
  if (*EV.idx_begin() == 0) {
    Instruction::BinaryOps BinOp = WO->getBinaryOp();
    Value *LHS = WO->getLHS(), *RHS = WO->getRHS();
    // Replace the old instruction's uses with poison.
    replaceInstUsesWith(*WO, PoisonValue::get(WO->getType()));
    eraseInstFromFunction(*WO);
    return BinaryOperator::Create(BinOp, LHS, RHS);
  }

  assert(*EV.idx_begin() == 1 && "Unexpected extract index for overflow inst");

  // (usub LHS, RHS) overflows when LHS is unsigned-less-than RHS.
  if (OvID == Intrinsic::usub_with_overflow)
    return new ICmpInst(ICmpInst::ICMP_ULT, WO->getLHS(), WO->getRHS());

  // smul with i1 types overflows when both sides are set: -1 * -1 == +1, but
  // +1 is not possible because we assume signed values.
  if (OvID == Intrinsic::smul_with_overflow &&
      WO->getLHS()->getType()->isIntOrIntVectorTy(1))
    return BinaryOperator::CreateAnd(WO->getLHS(), WO->getRHS());

  // extractvalue (umul_with_overflow X, X), 1 -> X u> 2^(N/2)-1
  if (OvID == Intrinsic::umul_with_overflow && WO->getLHS() == WO->getRHS()) {
    unsigned BitWidth = WO->getLHS()->getType()->getScalarSizeInBits();
    // Only handle even bitwidths for performance reasons.
    if (BitWidth % 2 == 0)
      return new ICmpInst(
          ICmpInst::ICMP_UGT, WO->getLHS(),
          ConstantInt::get(WO->getLHS()->getType(),
                           APInt::getLowBitsSet(BitWidth, BitWidth / 2)));
  }

  // If only the overflow result is used, and the right hand side is a
  // constant (or constant splat), we can remove the intrinsic by directly
  // checking for overflow.
  if (C) {
    // Compute the no-wrap range for LHS given RHS=C, then construct an
    // equivalent icmp, potentially using an offset.
    ConstantRange NWR = ConstantRange::makeExactNoWrapRegion(
        WO->getBinaryOp(), *C, WO->getNoWrapKind());

    CmpInst::Predicate Pred;
    APInt NewRHSC, Offset;
    NWR.getEquivalentICmp(Pred, NewRHSC, Offset);
    auto *OpTy = WO->getRHS()->getType();
    auto *NewLHS = WO->getLHS();
    if (Offset != 0)
      NewLHS = Builder.CreateAdd(NewLHS, ConstantInt::get(OpTy, Offset));
    return new ICmpInst(ICmpInst::getInversePredicate(Pred), NewLHS,
                        ConstantInt::get(OpTy, NewRHSC));
  }

  return nullptr;
}

Instruction *InstCombinerImpl::visitExtractValueInst(ExtractValueInst &EV) {
  Value *Agg = EV.getAggregateOperand();

  if (!EV.hasIndices())
    return replaceInstUsesWith(EV, Agg);

  if (Value *V = simplifyExtractValueInst(Agg, EV.getIndices(),
                                          SQ.getWithInstruction(&EV)))
    return replaceInstUsesWith(EV, V);

  if (InsertValueInst *IV = dyn_cast<InsertValueInst>(Agg)) {
    // We're extracting from an insertvalue instruction, compare the indices
    const unsigned *exti, *exte, *insi, *inse;
    for (exti = EV.idx_begin(), insi = IV->idx_begin(),
         exte = EV.idx_end(), inse = IV->idx_end();
         exti != exte && insi != inse;
         ++exti, ++insi) {
      if (*insi != *exti)
        // The insert and extract both reference distinctly different elements.
        // This means the extract is not influenced by the insert, and we can
        // replace the aggregate operand of the extract with the aggregate
        // operand of the insert. i.e., replace
        // %I = insertvalue { i32, { i32 } } %A, { i32 } { i32 42 }, 1
        // %E = extractvalue { i32, { i32 } } %I, 0
        // with
        // %E = extractvalue { i32, { i32 } } %A, 0
        return ExtractValueInst::Create(IV->getAggregateOperand(),
                                        EV.getIndices());
    }
    if (exti == exte && insi == inse)
      // Both iterators are at the end: Index lists are identical. Replace
      // %B = insertvalue { i32, { i32 } } %A, i32 42, 1, 0
      // %C = extractvalue { i32, { i32 } } %B, 1, 0
      // with "i32 42"
      return replaceInstUsesWith(EV, IV->getInsertedValueOperand());
    if (exti == exte) {
      // The extract list is a prefix of the insert list. i.e. replace
      // %I = insertvalue { i32, { i32 } } %A, i32 42, 1, 0
      // %E = extractvalue { i32, { i32 } } %I, 1
      // with
      // %X = extractvalue { i32, { i32 } } %A, 1
      // %E = insertvalue { i32 } %X, i32 42, 0
      // by switching the order of the insert and extract (though the
      // insertvalue should be left in, since it may have other uses).
      Value *NewEV = Builder.CreateExtractValue(IV->getAggregateOperand(),
                                                EV.getIndices());
      return InsertValueInst::Create(NewEV, IV->getInsertedValueOperand(),
                                     ArrayRef(insi, inse));
    }
    if (insi == inse)
      // The insert list is a prefix of the extract list
      // We can simply remove the common indices from the extract and make it
      // operate on the inserted value instead of the insertvalue result.
      // i.e., replace
      // %I = insertvalue { i32, { i32 } } %A, { i32 } { i32 42 }, 1
      // %E = extractvalue { i32, { i32 } } %I, 1, 0
      // with
      // %E extractvalue { i32 } { i32 42 }, 0
      return ExtractValueInst::Create(IV->getInsertedValueOperand(),
                                      ArrayRef(exti, exte));
  }

  if (Instruction *R = foldExtractOfOverflowIntrinsic(EV))
    return R;

  if (LoadInst *L = dyn_cast<LoadInst>(Agg)) {
    // Bail out if the aggregate contains scalable vector type
    if (auto *STy = dyn_cast<StructType>(Agg->getType());
        STy && STy->containsScalableVectorType())
      return nullptr;

    // If the (non-volatile) load only has one use, we can rewrite this to a
    // load from a GEP. This reduces the size of the load. If a load is used
    // only by extractvalue instructions then this either must have been
    // optimized before, or it is a struct with padding, in which case we
    // don't want to do the transformation as it loses padding knowledge.
    if (L->isSimple() && L->hasOneUse()) {
      // extractvalue has integer indices, getelementptr has Value*s. Convert.
      SmallVector<Value*, 4> Indices;
      // Prefix an i32 0 since we need the first element.
      Indices.push_back(Builder.getInt32(0));
      for (unsigned Idx : EV.indices())
        Indices.push_back(Builder.getInt32(Idx));

      // We need to insert these at the location of the old load, not at that of
      // the extractvalue.
      Builder.SetInsertPoint(L);
      Value *GEP = Builder.CreateInBoundsGEP(L->getType(),
                                             L->getPointerOperand(), Indices);
      Instruction *NL = Builder.CreateLoad(EV.getType(), GEP);
      // Whatever aliasing information we had for the orignal load must also
      // hold for the smaller load, so propagate the annotations.
      NL->setAAMetadata(L->getAAMetadata());
      // Returning the load directly will cause the main loop to insert it in
      // the wrong spot, so use replaceInstUsesWith().
      return replaceInstUsesWith(EV, NL);
    }
  }

  if (auto *PN = dyn_cast<PHINode>(Agg))
    if (Instruction *Res = foldOpIntoPhi(EV, PN))
      return Res;

  // Canonicalize extract (select Cond, TV, FV)
  // -> select cond, (extract TV), (extract FV)
  if (auto *SI = dyn_cast<SelectInst>(Agg))
    if (Instruction *R = FoldOpIntoSelect(EV, SI, /*FoldWithMultiUse=*/true))
      return R;

  // We could simplify extracts from other values. Note that nested extracts may
  // already be simplified implicitly by the above: extract (extract (insert) )
  // will be translated into extract ( insert ( extract ) ) first and then just
  // the value inserted, if appropriate. Similarly for extracts from single-use
  // loads: extract (extract (load)) will be translated to extract (load (gep))
  // and if again single-use then via load (gep (gep)) to load (gep).
  // However, double extracts from e.g. function arguments or return values
  // aren't handled yet.
  return nullptr;
}

/// Return 'true' if the given typeinfo will match anything.
static bool isCatchAll(EHPersonality Personality, Constant *TypeInfo) {
  switch (Personality) {
  case EHPersonality::GNU_C:
  case EHPersonality::GNU_C_SjLj:
  case EHPersonality::Rust:
    // The GCC C EH and Rust personality only exists to support cleanups, so
    // it's not clear what the semantics of catch clauses are.
    return false;
  case EHPersonality::Unknown:
    return false;
  case EHPersonality::GNU_Ada:
    // While __gnat_all_others_value will match any Ada exception, it doesn't
    // match foreign exceptions (or didn't, before gcc-4.7).
    return false;
  case EHPersonality::GNU_CXX:
  case EHPersonality::GNU_CXX_SjLj:
  case EHPersonality::GNU_ObjC:
  case EHPersonality::MSVC_X86SEH:
  case EHPersonality::MSVC_TableSEH:
  case EHPersonality::MSVC_CXX:
  case EHPersonality::CoreCLR:
  case EHPersonality::Wasm_CXX:
  case EHPersonality::XL_CXX:
  case EHPersonality::ZOS_CXX:
    return TypeInfo->isNullValue();
  }
  llvm_unreachable("invalid enum");
}

static bool shorter_filter(const Value *LHS, const Value *RHS) {
  return
    cast<ArrayType>(LHS->getType())->getNumElements()
  <
    cast<ArrayType>(RHS->getType())->getNumElements();
}

Instruction *InstCombinerImpl::visitLandingPadInst(LandingPadInst &LI) {
  // The logic here should be correct for any real-world personality function.
  // However if that turns out not to be true, the offending logic can always
  // be conditioned on the personality function, like the catch-all logic is.
  EHPersonality Personality =
      classifyEHPersonality(LI.getParent()->getParent()->getPersonalityFn());

  // Simplify the list of clauses, eg by removing repeated catch clauses
  // (these are often created by inlining).
  bool MakeNewInstruction = false; // If true, recreate using the following:
  SmallVector<Constant *, 16> NewClauses; // - Clauses for the new instruction;
  bool CleanupFlag = LI.isCleanup();   // - The new instruction is a cleanup.

  SmallPtrSet<Value *, 16> AlreadyCaught; // Typeinfos known caught already.
  for (unsigned i = 0, e = LI.getNumClauses(); i != e; ++i) {
    bool isLastClause = i + 1 == e;
    if (LI.isCatch(i)) {
      // A catch clause.
      Constant *CatchClause = LI.getClause(i);
      Constant *TypeInfo = CatchClause->stripPointerCasts();

      // If we already saw this clause, there is no point in having a second
      // copy of it.
      if (AlreadyCaught.insert(TypeInfo).second) {
        // This catch clause was not already seen.
        NewClauses.push_back(CatchClause);
      } else {
        // Repeated catch clause - drop the redundant copy.
        MakeNewInstruction = true;
      }

      // If this is a catch-all then there is no point in keeping any following
      // clauses or marking the landingpad as having a cleanup.
      if (isCatchAll(Personality, TypeInfo)) {
        if (!isLastClause)
          MakeNewInstruction = true;
        CleanupFlag = false;
        break;
      }
    } else {
      // A filter clause.  If any of the filter elements were already caught
      // then they can be dropped from the filter.  It is tempting to try to
      // exploit the filter further by saying that any typeinfo that does not
      // occur in the filter can't be caught later (and thus can be dropped).
      // However this would be wrong, since typeinfos can match without being
      // equal (for example if one represents a C++ class, and the other some
      // class derived from it).
      assert(LI.isFilter(i) && "Unsupported landingpad clause!");
      Constant *FilterClause = LI.getClause(i);
      ArrayType *FilterType = cast<ArrayType>(FilterClause->getType());
      unsigned NumTypeInfos = FilterType->getNumElements();

      // An empty filter catches everything, so there is no point in keeping any
      // following clauses or marking the landingpad as having a cleanup.  By
      // dealing with this case here the following code is made a bit simpler.
      if (!NumTypeInfos) {
        NewClauses.push_back(FilterClause);
        if (!isLastClause)
          MakeNewInstruction = true;
        CleanupFlag = false;
        break;
      }

      bool MakeNewFilter = false; // If true, make a new filter.
      SmallVector<Constant *, 16> NewFilterElts; // New elements.
      if (isa<ConstantAggregateZero>(FilterClause)) {
        // Not an empty filter - it contains at least one null typeinfo.
        assert(NumTypeInfos > 0 && "Should have handled empty filter already!");
        Constant *TypeInfo =
          Constant::getNullValue(FilterType->getElementType());
        // If this typeinfo is a catch-all then the filter can never match.
        if (isCatchAll(Personality, TypeInfo)) {
          // Throw the filter away.
          MakeNewInstruction = true;
          continue;
        }

        // There is no point in having multiple copies of this typeinfo, so
        // discard all but the first copy if there is more than one.
        NewFilterElts.push_back(TypeInfo);
        if (NumTypeInfos > 1)
          MakeNewFilter = true;
      } else {
        ConstantArray *Filter = cast<ConstantArray>(FilterClause);
        SmallPtrSet<Value *, 16> SeenInFilter; // For uniquing the elements.
        NewFilterElts.reserve(NumTypeInfos);

        // Remove any filter elements that were already caught or that already
        // occurred in the filter.  While there, see if any of the elements are
        // catch-alls.  If so, the filter can be discarded.
        bool SawCatchAll = false;
        for (unsigned j = 0; j != NumTypeInfos; ++j) {
          Constant *Elt = Filter->getOperand(j);
          Constant *TypeInfo = Elt->stripPointerCasts();
          if (isCatchAll(Personality, TypeInfo)) {
            // This element is a catch-all.  Bail out, noting this fact.
            SawCatchAll = true;
            break;
          }

          // Even if we've seen a type in a catch clause, we don't want to
          // remove it from the filter.  An unexpected type handler may be
          // set up for a call site which throws an exception of the same
          // type caught.  In order for the exception thrown by the unexpected
          // handler to propagate correctly, the filter must be correctly
          // described for the call site.
          //
          // Example:
          //
          // void unexpected() { throw 1;}
          // void foo() throw (int) {
          //   std::set_unexpected(unexpected);
          //   try {
          //     throw 2.0;
          //   } catch (int i) {}
          // }

          // There is no point in having multiple copies of the same typeinfo in
          // a filter, so only add it if we didn't already.
          if (SeenInFilter.insert(TypeInfo).second)
            NewFilterElts.push_back(cast<Constant>(Elt));
        }
        // A filter containing a catch-all cannot match anything by definition.
        if (SawCatchAll) {
          // Throw the filter away.
          MakeNewInstruction = true;
          continue;
        }

        // If we dropped something from the filter, make a new one.
        if (NewFilterElts.size() < NumTypeInfos)
          MakeNewFilter = true;
      }
      if (MakeNewFilter) {
        FilterType = ArrayType::get(FilterType->getElementType(),
                                    NewFilterElts.size());
        FilterClause = ConstantArray::get(FilterType, NewFilterElts);
        MakeNewInstruction = true;
      }

      NewClauses.push_back(FilterClause);

      // If the new filter is empty then it will catch everything so there is
      // no point in keeping any following clauses or marking the landingpad
      // as having a cleanup.  The case of the original filter being empty was
      // already handled above.
      if (MakeNewFilter && !NewFilterElts.size()) {
        assert(MakeNewInstruction && "New filter but not a new instruction!");
        CleanupFlag = false;
        break;
      }
    }
  }

  // If several filters occur in a row then reorder them so that the shortest
  // filters come first (those with the smallest number of elements).  This is
  // advantageous because shorter filters are more likely to match, speeding up
  // unwinding, but mostly because it increases the effectiveness of the other
  // filter optimizations below.
  for (unsigned i = 0, e = NewClauses.size(); i + 1 < e; ) {
    unsigned j;
    // Find the maximal 'j' s.t. the range [i, j) consists entirely of filters.
    for (j = i; j != e; ++j)
      if (!isa<ArrayType>(NewClauses[j]->getType()))
        break;

    // Check whether the filters are already sorted by length.  We need to know
    // if sorting them is actually going to do anything so that we only make a
    // new landingpad instruction if it does.
    for (unsigned k = i; k + 1 < j; ++k)
      if (shorter_filter(NewClauses[k+1], NewClauses[k])) {
        // Not sorted, so sort the filters now.  Doing an unstable sort would be
        // correct too but reordering filters pointlessly might confuse users.
        std::stable_sort(NewClauses.begin() + i, NewClauses.begin() + j,
                         shorter_filter);
        MakeNewInstruction = true;
        break;
      }

    // Look for the next batch of filters.
    i = j + 1;
  }

  // If typeinfos matched if and only if equal, then the elements of a filter L
  // that occurs later than a filter F could be replaced by the intersection of
  // the elements of F and L.  In reality two typeinfos can match without being
  // equal (for example if one represents a C++ class, and the other some class
  // derived from it) so it would be wrong to perform this transform in general.
  // However the transform is correct and useful if F is a subset of L.  In that
  // case L can be replaced by F, and thus removed altogether since repeating a
  // filter is pointless.  So here we look at all pairs of filters F and L where
  // L follows F in the list of clauses, and remove L if every element of F is
  // an element of L.  This can occur when inlining C++ functions with exception
  // specifications.
  for (unsigned i = 0; i + 1 < NewClauses.size(); ++i) {
    // Examine each filter in turn.
    Value *Filter = NewClauses[i];
    ArrayType *FTy = dyn_cast<ArrayType>(Filter->getType());
    if (!FTy)
      // Not a filter - skip it.
      continue;
    unsigned FElts = FTy->getNumElements();
    // Examine each filter following this one.  Doing this backwards means that
    // we don't have to worry about filters disappearing under us when removed.
    for (unsigned j = NewClauses.size() - 1; j != i; --j) {
      Value *LFilter = NewClauses[j];
      ArrayType *LTy = dyn_cast<ArrayType>(LFilter->getType());
      if (!LTy)
        // Not a filter - skip it.
        continue;
      // If Filter is a subset of LFilter, i.e. every element of Filter is also
      // an element of LFilter, then discard LFilter.
      SmallVectorImpl<Constant *>::iterator J = NewClauses.begin() + j;
      // If Filter is empty then it is a subset of LFilter.
      if (!FElts) {
        // Discard LFilter.
        NewClauses.erase(J);
        MakeNewInstruction = true;
        // Move on to the next filter.
        continue;
      }
      unsigned LElts = LTy->getNumElements();
      // If Filter is longer than LFilter then it cannot be a subset of it.
      if (FElts > LElts)
        // Move on to the next filter.
        continue;
      // At this point we know that LFilter has at least one element.
      if (isa<ConstantAggregateZero>(LFilter)) { // LFilter only contains zeros.
        // Filter is a subset of LFilter iff Filter contains only zeros (as we
        // already know that Filter is not longer than LFilter).
        if (isa<ConstantAggregateZero>(Filter)) {
          assert(FElts <= LElts && "Should have handled this case earlier!");
          // Discard LFilter.
          NewClauses.erase(J);
          MakeNewInstruction = true;
        }
        // Move on to the next filter.
        continue;
      }
      ConstantArray *LArray = cast<ConstantArray>(LFilter);
      if (isa<ConstantAggregateZero>(Filter)) { // Filter only contains zeros.
        // Since Filter is non-empty and contains only zeros, it is a subset of
        // LFilter iff LFilter contains a zero.
        assert(FElts > 0 && "Should have eliminated the empty filter earlier!");
        for (unsigned l = 0; l != LElts; ++l)
          if (LArray->getOperand(l)->isNullValue()) {
            // LFilter contains a zero - discard it.
            NewClauses.erase(J);
            MakeNewInstruction = true;
            break;
          }
        // Move on to the next filter.
        continue;
      }
      // At this point we know that both filters are ConstantArrays.  Loop over
      // operands to see whether every element of Filter is also an element of
      // LFilter.  Since filters tend to be short this is probably faster than
      // using a method that scales nicely.
      ConstantArray *FArray = cast<ConstantArray>(Filter);
      bool AllFound = true;
      for (unsigned f = 0; f != FElts; ++f) {
        Value *FTypeInfo = FArray->getOperand(f)->stripPointerCasts();
        AllFound = false;
        for (unsigned l = 0; l != LElts; ++l) {
          Value *LTypeInfo = LArray->getOperand(l)->stripPointerCasts();
          if (LTypeInfo == FTypeInfo) {
            AllFound = true;
            break;
          }
        }
        if (!AllFound)
          break;
      }
      if (AllFound) {
        // Discard LFilter.
        NewClauses.erase(J);
        MakeNewInstruction = true;
      }
      // Move on to the next filter.
    }
  }

  // If we changed any of the clauses, replace the old landingpad instruction
  // with a new one.
  if (MakeNewInstruction) {
    LandingPadInst *NLI = LandingPadInst::Create(LI.getType(),
                                                 NewClauses.size());
    for (Constant *C : NewClauses)
      NLI->addClause(C);
    // A landing pad with no clauses must have the cleanup flag set.  It is
    // theoretically possible, though highly unlikely, that we eliminated all
    // clauses.  If so, force the cleanup flag to true.
    if (NewClauses.empty())
      CleanupFlag = true;
    NLI->setCleanup(CleanupFlag);
    return NLI;
  }

  // Even if none of the clauses changed, we may nonetheless have understood
  // that the cleanup flag is pointless.  Clear it if so.
  if (LI.isCleanup() != CleanupFlag) {
    assert(!CleanupFlag && "Adding a cleanup, not removing one?!");
    LI.setCleanup(CleanupFlag);
    return &LI;
  }

  return nullptr;
}

Value *
InstCombinerImpl::pushFreezeToPreventPoisonFromPropagating(FreezeInst &OrigFI) {
  // Try to push freeze through instructions that propagate but don't produce
  // poison as far as possible.  If an operand of freeze follows three
  // conditions 1) one-use, 2) does not produce poison, and 3) has all but one
  // guaranteed-non-poison operands then push the freeze through to the one
  // operand that is not guaranteed non-poison.  The actual transform is as
  // follows.
  //   Op1 = ...                        ; Op1 can be posion
  //   Op0 = Inst(Op1, NonPoisonOps...) ; Op0 has only one use and only have
  //                                    ; single guaranteed-non-poison operands
  //   ... = Freeze(Op0)
  // =>
  //   Op1 = ...
  //   Op1.fr = Freeze(Op1)
  //   ... = Inst(Op1.fr, NonPoisonOps...)
  auto *OrigOp = OrigFI.getOperand(0);
  auto *OrigOpInst = dyn_cast<Instruction>(OrigOp);

  // While we could change the other users of OrigOp to use freeze(OrigOp), that
  // potentially reduces their optimization potential, so let's only do this iff
  // the OrigOp is only used by the freeze.
  if (!OrigOpInst || !OrigOpInst->hasOneUse() || isa<PHINode>(OrigOp))
    return nullptr;

  // We can't push the freeze through an instruction which can itself create
  // poison.  If the only source of new poison is flags, we can simply
  // strip them (since we know the only use is the freeze and nothing can
  // benefit from them.)
  if (canCreateUndefOrPoison(cast<Operator>(OrigOp),
                             /*ConsiderFlagsAndMetadata*/ false))
    return nullptr;

  // If operand is guaranteed not to be poison, there is no need to add freeze
  // to the operand. So we first find the operand that is not guaranteed to be
  // poison.
  Use *MaybePoisonOperand = nullptr;
  for (Use &U : OrigOpInst->operands()) {
    if (isa<MetadataAsValue>(U.get()) ||
        isGuaranteedNotToBeUndefOrPoison(U.get()))
      continue;
    if (!MaybePoisonOperand)
      MaybePoisonOperand = &U;
    else
      return nullptr;
  }

  OrigOpInst->dropPoisonGeneratingAnnotations();

  // If all operands are guaranteed to be non-poison, we can drop freeze.
  if (!MaybePoisonOperand)
    return OrigOp;

  Builder.SetInsertPoint(OrigOpInst);
  auto *FrozenMaybePoisonOperand = Builder.CreateFreeze(
      MaybePoisonOperand->get(), MaybePoisonOperand->get()->getName() + ".fr");

  replaceUse(*MaybePoisonOperand, FrozenMaybePoisonOperand);
  return OrigOp;
}

Instruction *InstCombinerImpl::foldFreezeIntoRecurrence(FreezeInst &FI,
                                                        PHINode *PN) {
  // Detect whether this is a recurrence with a start value and some number of
  // backedge values. We'll check whether we can push the freeze through the
  // backedge values (possibly dropping poison flags along the way) until we
  // reach the phi again. In that case, we can move the freeze to the start
  // value.
  Use *StartU = nullptr;
  SmallVector<Value *> Worklist;
  for (Use &U : PN->incoming_values()) {
    if (DT.dominates(PN->getParent(), PN->getIncomingBlock(U))) {
      // Add backedge value to worklist.
      Worklist.push_back(U.get());
      continue;
    }

    // Don't bother handling multiple start values.
    if (StartU)
      return nullptr;
    StartU = &U;
  }

  if (!StartU || Worklist.empty())
    return nullptr; // Not a recurrence.

  Value *StartV = StartU->get();
  BasicBlock *StartBB = PN->getIncomingBlock(*StartU);
  bool StartNeedsFreeze = !isGuaranteedNotToBeUndefOrPoison(StartV);
  // We can't insert freeze if the start value is the result of the
  // terminator (e.g. an invoke).
  if (StartNeedsFreeze && StartBB->getTerminator() == StartV)
    return nullptr;

  SmallPtrSet<Value *, 32> Visited;
  SmallVector<Instruction *> DropFlags;
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    if (!Visited.insert(V).second)
      continue;

    if (Visited.size() > 32)
      return nullptr; // Limit the total number of values we inspect.

    // Assume that PN is non-poison, because it will be after the transform.
    if (V == PN || isGuaranteedNotToBeUndefOrPoison(V))
      continue;

    Instruction *I = dyn_cast<Instruction>(V);
    if (!I || canCreateUndefOrPoison(cast<Operator>(I),
                                     /*ConsiderFlagsAndMetadata*/ false))
      return nullptr;

    DropFlags.push_back(I);
    append_range(Worklist, I->operands());
  }

  for (Instruction *I : DropFlags)
    I->dropPoisonGeneratingAnnotations();

  if (StartNeedsFreeze) {
    Builder.SetInsertPoint(StartBB->getTerminator());
    Value *FrozenStartV = Builder.CreateFreeze(StartV,
                                               StartV->getName() + ".fr");
    replaceUse(*StartU, FrozenStartV);
  }
  return replaceInstUsesWith(FI, PN);
}

bool InstCombinerImpl::freezeOtherUses(FreezeInst &FI) {
  Value *Op = FI.getOperand(0);

  if (isa<Constant>(Op) || Op->hasOneUse())
    return false;

  // Move the freeze directly after the definition of its operand, so that
  // it dominates the maximum number of uses. Note that it may not dominate
  // *all* uses if the operand is an invoke/callbr and the use is in a phi on
  // the normal/default destination. This is why the domination check in the
  // replacement below is still necessary.
  BasicBlock::iterator MoveBefore;
  if (isa<Argument>(Op)) {
    MoveBefore =
        FI.getFunction()->getEntryBlock().getFirstNonPHIOrDbgOrAlloca();
  } else {
    auto MoveBeforeOpt = cast<Instruction>(Op)->getInsertionPointAfterDef();
    if (!MoveBeforeOpt)
      return false;
    MoveBefore = *MoveBeforeOpt;
  }

  // Don't move to the position of a debug intrinsic.
  if (isa<DbgInfoIntrinsic>(MoveBefore))
    MoveBefore = MoveBefore->getNextNonDebugInstruction()->getIterator();
  // Re-point iterator to come after any debug-info records, if we're
  // running in "RemoveDIs" mode
  MoveBefore.setHeadBit(false);

  bool Changed = false;
  if (&FI != &*MoveBefore) {
    FI.moveBefore(*MoveBefore->getParent(), MoveBefore);
    Changed = true;
  }

  Op->replaceUsesWithIf(&FI, [&](Use &U) -> bool {
    bool Dominates = DT.dominates(&FI, U);
    Changed |= Dominates;
    return Dominates;
  });

  return Changed;
}

// Check if any direct or bitcast user of this value is a shuffle instruction.
static bool isUsedWithinShuffleVector(Value *V) {
  for (auto *U : V->users()) {
    if (isa<ShuffleVectorInst>(U))
      return true;
    else if (match(U, m_BitCast(m_Specific(V))) && isUsedWithinShuffleVector(U))
      return true;
  }
  return false;
}

Instruction *InstCombinerImpl::visitFreeze(FreezeInst &I) {
  Value *Op0 = I.getOperand(0);

  if (Value *V = simplifyFreezeInst(Op0, SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  // freeze (phi const, x) --> phi const, (freeze x)
  if (auto *PN = dyn_cast<PHINode>(Op0)) {
    if (Instruction *NV = foldOpIntoPhi(I, PN))
      return NV;
    if (Instruction *NV = foldFreezeIntoRecurrence(I, PN))
      return NV;
  }

  if (Value *NI = pushFreezeToPreventPoisonFromPropagating(I))
    return replaceInstUsesWith(I, NI);

  // If I is freeze(undef), check its uses and fold it to a fixed constant.
  // - or: pick -1
  // - select's condition: if the true value is constant, choose it by making
  //                       the condition true.
  // - default: pick 0
  //
  // Note that this transform is intentionally done here rather than
  // via an analysis in InstSimplify or at individual user sites. That is
  // because we must produce the same value for all uses of the freeze -
  // it's the reason "freeze" exists!
  //
  // TODO: This could use getBinopAbsorber() / getBinopIdentity() to avoid
  //       duplicating logic for binops at least.
  auto getUndefReplacement = [&I](Type *Ty) {
    Constant *BestValue = nullptr;
    Constant *NullValue = Constant::getNullValue(Ty);
    for (const auto *U : I.users()) {
      Constant *C = NullValue;
      if (match(U, m_Or(m_Value(), m_Value())))
        C = ConstantInt::getAllOnesValue(Ty);
      else if (match(U, m_Select(m_Specific(&I), m_Constant(), m_Value())))
        C = ConstantInt::getTrue(Ty);

      if (!BestValue)
        BestValue = C;
      else if (BestValue != C)
        BestValue = NullValue;
    }
    assert(BestValue && "Must have at least one use");
    return BestValue;
  };

  if (match(Op0, m_Undef())) {
    // Don't fold freeze(undef/poison) if it's used as a vector operand in
    // a shuffle. This may improve codegen for shuffles that allow
    // unspecified inputs.
    if (isUsedWithinShuffleVector(&I))
      return nullptr;
    return replaceInstUsesWith(I, getUndefReplacement(I.getType()));
  }

  Constant *C;
  if (match(Op0, m_Constant(C)) && C->containsUndefOrPoisonElement()) {
    Constant *ReplaceC = getUndefReplacement(I.getType()->getScalarType());
    return replaceInstUsesWith(I, Constant::replaceUndefsWith(C, ReplaceC));
  }

  // Replace uses of Op with freeze(Op).
  if (freezeOtherUses(I))
    return &I;

  return nullptr;
}

/// Check for case where the call writes to an otherwise dead alloca.  This
/// shows up for unused out-params in idiomatic C/C++ code.   Note that this
/// helper *only* analyzes the write; doesn't check any other legality aspect.
static bool SoleWriteToDeadLocal(Instruction *I, TargetLibraryInfo &TLI) {
  auto *CB = dyn_cast<CallBase>(I);
  if (!CB)
    // TODO: handle e.g. store to alloca here - only worth doing if we extend
    // to allow reload along used path as described below.  Otherwise, this
    // is simply a store to a dead allocation which will be removed.
    return false;
  std::optional<MemoryLocation> Dest = MemoryLocation::getForDest(CB, TLI);
  if (!Dest)
    return false;
  auto *AI = dyn_cast<AllocaInst>(getUnderlyingObject(Dest->Ptr));
  if (!AI)
    // TODO: allow malloc?
    return false;
  // TODO: allow memory access dominated by move point?  Note that since AI
  // could have a reference to itself captured by the call, we would need to
  // account for cycles in doing so.
  SmallVector<const User *> AllocaUsers;
  SmallPtrSet<const User *, 4> Visited;
  auto pushUsers = [&](const Instruction &I) {
    for (const User *U : I.users()) {
      if (Visited.insert(U).second)
        AllocaUsers.push_back(U);
    }
  };
  pushUsers(*AI);
  while (!AllocaUsers.empty()) {
    auto *UserI = cast<Instruction>(AllocaUsers.pop_back_val());
    if (isa<BitCastInst>(UserI) || isa<GetElementPtrInst>(UserI) ||
        isa<AddrSpaceCastInst>(UserI)) {
      pushUsers(*UserI);
      continue;
    }
    if (UserI == CB)
      continue;
    // TODO: support lifetime.start/end here
    return false;
  }
  return true;
}

/// Try to move the specified instruction from its current block into the
/// beginning of DestBlock, which can only happen if it's safe to move the
/// instruction past all of the instructions between it and the end of its
/// block.
bool InstCombinerImpl::tryToSinkInstruction(Instruction *I,
                                            BasicBlock *DestBlock) {
  BasicBlock *SrcBlock = I->getParent();

  // Cannot move control-flow-involving, volatile loads, vaarg, etc.
  if (isa<PHINode>(I) || I->isEHPad() || I->mayThrow() || !I->willReturn() ||
      I->isTerminator())
    return false;

  // Do not sink static or dynamic alloca instructions. Static allocas must
  // remain in the entry block, and dynamic allocas must not be sunk in between
  // a stacksave / stackrestore pair, which would incorrectly shorten its
  // lifetime.
  if (isa<AllocaInst>(I))
    return false;

  // Do not sink into catchswitch blocks.
  if (isa<CatchSwitchInst>(DestBlock->getTerminator()))
    return false;

  // Do not sink convergent call instructions.
  if (auto *CI = dyn_cast<CallInst>(I)) {
    if (CI->isConvergent())
      return false;
  }

  // Unless we can prove that the memory write isn't visibile except on the
  // path we're sinking to, we must bail.
  if (I->mayWriteToMemory()) {
    if (!SoleWriteToDeadLocal(I, TLI))
      return false;
  }

  // We can only sink load instructions if there is nothing between the load and
  // the end of block that could change the value.
  if (I->mayReadFromMemory()) {
    // We don't want to do any sophisticated alias analysis, so we only check
    // the instructions after I in I's parent block if we try to sink to its
    // successor block.
    if (DestBlock->getUniquePredecessor() != I->getParent())
      return false;
    for (BasicBlock::iterator Scan = std::next(I->getIterator()),
                              E = I->getParent()->end();
         Scan != E; ++Scan)
      if (Scan->mayWriteToMemory())
        return false;
  }

  I->dropDroppableUses([&](const Use *U) {
    auto *I = dyn_cast<Instruction>(U->getUser());
    if (I && I->getParent() != DestBlock) {
      Worklist.add(I);
      return true;
    }
    return false;
  });
  /// FIXME: We could remove droppable uses that are not dominated by
  /// the new position.

  BasicBlock::iterator InsertPos = DestBlock->getFirstInsertionPt();
  I->moveBefore(*DestBlock, InsertPos);
  ++NumSunkInst;

  // Also sink all related debug uses from the source basic block. Otherwise we
  // get debug use before the def. Attempt to salvage debug uses first, to
  // maximise the range variables have location for. If we cannot salvage, then
  // mark the location undef: we know it was supposed to receive a new location
  // here, but that computation has been sunk.
  SmallVector<DbgVariableIntrinsic *, 2> DbgUsers;
  SmallVector<DbgVariableRecord *, 2> DbgVariableRecords;
  findDbgUsers(DbgUsers, I, &DbgVariableRecords);
  if (!DbgUsers.empty())
    tryToSinkInstructionDbgValues(I, InsertPos, SrcBlock, DestBlock, DbgUsers);
  if (!DbgVariableRecords.empty())
    tryToSinkInstructionDbgVariableRecords(I, InsertPos, SrcBlock, DestBlock,
                                           DbgVariableRecords);

  // PS: there are numerous flaws with this behaviour, not least that right now
  // assignments can be re-ordered past other assignments to the same variable
  // if they use different Values. Creating more undef assignements can never be
  // undone. And salvaging all users outside of this block can un-necessarily
  // alter the lifetime of the live-value that the variable refers to.
  // Some of these things can be resolved by tolerating debug use-before-defs in
  // LLVM-IR, however it depends on the instruction-referencing CodeGen backend
  // being used for more architectures.

  return true;
}

void InstCombinerImpl::tryToSinkInstructionDbgValues(
    Instruction *I, BasicBlock::iterator InsertPos, BasicBlock *SrcBlock,
    BasicBlock *DestBlock, SmallVectorImpl<DbgVariableIntrinsic *> &DbgUsers) {
  // For all debug values in the destination block, the sunk instruction
  // will still be available, so they do not need to be dropped.
  SmallVector<DbgVariableIntrinsic *, 2> DbgUsersToSalvage;
  for (auto &DbgUser : DbgUsers)
    if (DbgUser->getParent() != DestBlock)
      DbgUsersToSalvage.push_back(DbgUser);

  // Process the sinking DbgUsersToSalvage in reverse order, as we only want
  // to clone the last appearing debug intrinsic for each given variable.
  SmallVector<DbgVariableIntrinsic *, 2> DbgUsersToSink;
  for (DbgVariableIntrinsic *DVI : DbgUsersToSalvage)
    if (DVI->getParent() == SrcBlock)
      DbgUsersToSink.push_back(DVI);
  llvm::sort(DbgUsersToSink,
             [](auto *A, auto *B) { return B->comesBefore(A); });

  SmallVector<DbgVariableIntrinsic *, 2> DIIClones;
  SmallSet<DebugVariable, 4> SunkVariables;
  for (auto *User : DbgUsersToSink) {
    // A dbg.declare instruction should not be cloned, since there can only be
    // one per variable fragment. It should be left in the original place
    // because the sunk instruction is not an alloca (otherwise we could not be
    // here).
    if (isa<DbgDeclareInst>(User))
      continue;

    DebugVariable DbgUserVariable =
        DebugVariable(User->getVariable(), User->getExpression(),
                      User->getDebugLoc()->getInlinedAt());

    if (!SunkVariables.insert(DbgUserVariable).second)
      continue;

    // Leave dbg.assign intrinsics in their original positions and there should
    // be no need to insert a clone.
    if (isa<DbgAssignIntrinsic>(User))
      continue;

    DIIClones.emplace_back(cast<DbgVariableIntrinsic>(User->clone()));
    if (isa<DbgDeclareInst>(User) && isa<CastInst>(I))
      DIIClones.back()->replaceVariableLocationOp(I, I->getOperand(0));
    LLVM_DEBUG(dbgs() << "CLONE: " << *DIIClones.back() << '\n');
  }

  // Perform salvaging without the clones, then sink the clones.
  if (!DIIClones.empty()) {
    salvageDebugInfoForDbgValues(*I, DbgUsersToSalvage, {});
    // The clones are in reverse order of original appearance, reverse again to
    // maintain the original order.
    for (auto &DIIClone : llvm::reverse(DIIClones)) {
      DIIClone->insertBefore(&*InsertPos);
      LLVM_DEBUG(dbgs() << "SINK: " << *DIIClone << '\n');
    }
  }
}

void InstCombinerImpl::tryToSinkInstructionDbgVariableRecords(
    Instruction *I, BasicBlock::iterator InsertPos, BasicBlock *SrcBlock,
    BasicBlock *DestBlock,
    SmallVectorImpl<DbgVariableRecord *> &DbgVariableRecords) {
  // Implementation of tryToSinkInstructionDbgValues, but for the
  // DbgVariableRecord of variable assignments rather than dbg.values.

  // Fetch all DbgVariableRecords not already in the destination.
  SmallVector<DbgVariableRecord *, 2> DbgVariableRecordsToSalvage;
  for (auto &DVR : DbgVariableRecords)
    if (DVR->getParent() != DestBlock)
      DbgVariableRecordsToSalvage.push_back(DVR);

  // Fetch a second collection, of DbgVariableRecords in the source block that
  // we're going to sink.
  SmallVector<DbgVariableRecord *> DbgVariableRecordsToSink;
  for (DbgVariableRecord *DVR : DbgVariableRecordsToSalvage)
    if (DVR->getParent() == SrcBlock)
      DbgVariableRecordsToSink.push_back(DVR);

  // Sort DbgVariableRecords according to their position in the block. This is a
  // partial order: DbgVariableRecords attached to different instructions will
  // be ordered by the instruction order, but DbgVariableRecords attached to the
  // same instruction won't have an order.
  auto Order = [](DbgVariableRecord *A, DbgVariableRecord *B) -> bool {
    return B->getInstruction()->comesBefore(A->getInstruction());
  };
  llvm::stable_sort(DbgVariableRecordsToSink, Order);

  // If there are two assignments to the same variable attached to the same
  // instruction, the ordering between the two assignments is important. Scan
  // for this (rare) case and establish which is the last assignment.
  using InstVarPair = std::pair<const Instruction *, DebugVariable>;
  SmallDenseMap<InstVarPair, DbgVariableRecord *> FilterOutMap;
  if (DbgVariableRecordsToSink.size() > 1) {
    SmallDenseMap<InstVarPair, unsigned> CountMap;
    // Count how many assignments to each variable there is per instruction.
    for (DbgVariableRecord *DVR : DbgVariableRecordsToSink) {
      DebugVariable DbgUserVariable =
          DebugVariable(DVR->getVariable(), DVR->getExpression(),
                        DVR->getDebugLoc()->getInlinedAt());
      CountMap[std::make_pair(DVR->getInstruction(), DbgUserVariable)] += 1;
    }

    // If there are any instructions with two assignments, add them to the
    // FilterOutMap to record that they need extra filtering.
    SmallPtrSet<const Instruction *, 4> DupSet;
    for (auto It : CountMap) {
      if (It.second > 1) {
        FilterOutMap[It.first] = nullptr;
        DupSet.insert(It.first.first);
      }
    }

    // For all instruction/variable pairs needing extra filtering, find the
    // latest assignment.
    for (const Instruction *Inst : DupSet) {
      for (DbgVariableRecord &DVR :
           llvm::reverse(filterDbgVars(Inst->getDbgRecordRange()))) {
        DebugVariable DbgUserVariable =
            DebugVariable(DVR.getVariable(), DVR.getExpression(),
                          DVR.getDebugLoc()->getInlinedAt());
        auto FilterIt =
            FilterOutMap.find(std::make_pair(Inst, DbgUserVariable));
        if (FilterIt == FilterOutMap.end())
          continue;
        if (FilterIt->second != nullptr)
          continue;
        FilterIt->second = &DVR;
      }
    }
  }

  // Perform cloning of the DbgVariableRecords that we plan on sinking, filter
  // out any duplicate assignments identified above.
  SmallVector<DbgVariableRecord *, 2> DVRClones;
  SmallSet<DebugVariable, 4> SunkVariables;
  for (DbgVariableRecord *DVR : DbgVariableRecordsToSink) {
    if (DVR->Type == DbgVariableRecord::LocationType::Declare)
      continue;

    DebugVariable DbgUserVariable =
        DebugVariable(DVR->getVariable(), DVR->getExpression(),
                      DVR->getDebugLoc()->getInlinedAt());

    // For any variable where there were multiple assignments in the same place,
    // ignore all but the last assignment.
    if (!FilterOutMap.empty()) {
      InstVarPair IVP = std::make_pair(DVR->getInstruction(), DbgUserVariable);
      auto It = FilterOutMap.find(IVP);

      // Filter out.
      if (It != FilterOutMap.end() && It->second != DVR)
        continue;
    }

    if (!SunkVariables.insert(DbgUserVariable).second)
      continue;

    if (DVR->isDbgAssign())
      continue;

    DVRClones.emplace_back(DVR->clone());
    LLVM_DEBUG(dbgs() << "CLONE: " << *DVRClones.back() << '\n');
  }

  // Perform salvaging without the clones, then sink the clones.
  if (DVRClones.empty())
    return;

  salvageDebugInfoForDbgValues(*I, {}, DbgVariableRecordsToSalvage);

  // The clones are in reverse order of original appearance. Assert that the
  // head bit is set on the iterator as we _should_ have received it via
  // getFirstInsertionPt. Inserting like this will reverse the clone order as
  // we'll repeatedly insert at the head, such as:
  //   DVR-3 (third insertion goes here)
  //   DVR-2 (second insertion goes here)
  //   DVR-1 (first insertion goes here)
  //   Any-Prior-DVRs
  //   InsertPtInst
  assert(InsertPos.getHeadBit());
  for (DbgVariableRecord *DVRClone : DVRClones) {
    InsertPos->getParent()->insertDbgRecordBefore(DVRClone, InsertPos);
    LLVM_DEBUG(dbgs() << "SINK: " << *DVRClone << '\n');
  }
}

bool InstCombinerImpl::run() {
  while (!Worklist.isEmpty()) {
    // Walk deferred instructions in reverse order, and push them to the
    // worklist, which means they'll end up popped from the worklist in-order.
    while (Instruction *I = Worklist.popDeferred()) {
      // Check to see if we can DCE the instruction. We do this already here to
      // reduce the number of uses and thus allow other folds to trigger.
      // Note that eraseInstFromFunction() may push additional instructions on
      // the deferred worklist, so this will DCE whole instruction chains.
      if (isInstructionTriviallyDead(I, &TLI)) {
        eraseInstFromFunction(*I);
        ++NumDeadInst;
        continue;
      }

      Worklist.push(I);
    }

    Instruction *I = Worklist.removeOne();
    if (I == nullptr) continue;  // skip null values.

    // Check to see if we can DCE the instruction.
    if (isInstructionTriviallyDead(I, &TLI)) {
      eraseInstFromFunction(*I);
      ++NumDeadInst;
      continue;
    }

    if (!DebugCounter::shouldExecute(VisitCounter))
      continue;

    // See if we can trivially sink this instruction to its user if we can
    // prove that the successor is not executed more frequently than our block.
    // Return the UserBlock if successful.
    auto getOptionalSinkBlockForInst =
        [this](Instruction *I) -> std::optional<BasicBlock *> {
      if (!EnableCodeSinking)
        return std::nullopt;

      BasicBlock *BB = I->getParent();
      BasicBlock *UserParent = nullptr;
      unsigned NumUsers = 0;

      for (Use &U : I->uses()) {
        User *User = U.getUser();
        if (User->isDroppable())
          continue;
        if (NumUsers > MaxSinkNumUsers)
          return std::nullopt;

        Instruction *UserInst = cast<Instruction>(User);
        // Special handling for Phi nodes - get the block the use occurs in.
        BasicBlock *UserBB = UserInst->getParent();
        if (PHINode *PN = dyn_cast<PHINode>(UserInst))
          UserBB = PN->getIncomingBlock(U);
        // Bail out if we have uses in different blocks. We don't do any
        // sophisticated analysis (i.e finding NearestCommonDominator of these
        // use blocks).
        if (UserParent && UserParent != UserBB)
          return std::nullopt;
        UserParent = UserBB;

        // Make sure these checks are done only once, naturally we do the checks
        // the first time we get the userparent, this will save compile time.
        if (NumUsers == 0) {
          // Try sinking to another block. If that block is unreachable, then do
          // not bother. SimplifyCFG should handle it.
          if (UserParent == BB || !DT.isReachableFromEntry(UserParent))
            return std::nullopt;

          auto *Term = UserParent->getTerminator();
          // See if the user is one of our successors that has only one
          // predecessor, so that we don't have to split the critical edge.
          // Another option where we can sink is a block that ends with a
          // terminator that does not pass control to other block (such as
          // return or unreachable or resume). In this case:
          //   - I dominates the User (by SSA form);
          //   - the User will be executed at most once.
          // So sinking I down to User is always profitable or neutral.
          if (UserParent->getUniquePredecessor() != BB && !succ_empty(Term))
            return std::nullopt;

          assert(DT.dominates(BB, UserParent) && "Dominance relation broken?");
        }

        NumUsers++;
      }

      // No user or only has droppable users.
      if (!UserParent)
        return std::nullopt;

      return UserParent;
    };

    auto OptBB = getOptionalSinkBlockForInst(I);
    if (OptBB) {
      auto *UserParent = *OptBB;
      // Okay, the CFG is simple enough, try to sink this instruction.
      if (tryToSinkInstruction(I, UserParent)) {
        LLVM_DEBUG(dbgs() << "IC: Sink: " << *I << '\n');
        MadeIRChange = true;
        // We'll add uses of the sunk instruction below, but since
        // sinking can expose opportunities for it's *operands* add
        // them to the worklist
        for (Use &U : I->operands())
          if (Instruction *OpI = dyn_cast<Instruction>(U.get()))
            Worklist.push(OpI);
      }
    }

    // Now that we have an instruction, try combining it to simplify it.
    Builder.SetInsertPoint(I);
    Builder.CollectMetadataToCopy(
        I, {LLVMContext::MD_dbg, LLVMContext::MD_annotation});

#ifndef NDEBUG
    std::string OrigI;
#endif
    LLVM_DEBUG(raw_string_ostream SS(OrigI); I->print(SS););
    LLVM_DEBUG(dbgs() << "IC: Visiting: " << OrigI << '\n');

    if (Instruction *Result = visit(*I)) {
      ++NumCombined;
      // Should we replace the old instruction with a new one?
      if (Result != I) {
        LLVM_DEBUG(dbgs() << "IC: Old = " << *I << '\n'
                          << "    New = " << *Result << '\n');

        Result->copyMetadata(*I,
                             {LLVMContext::MD_dbg, LLVMContext::MD_annotation});
        // Everything uses the new instruction now.
        I->replaceAllUsesWith(Result);

        // Move the name to the new instruction first.
        Result->takeName(I);

        // Insert the new instruction into the basic block...
        BasicBlock *InstParent = I->getParent();
        BasicBlock::iterator InsertPos = I->getIterator();

        // Are we replace a PHI with something that isn't a PHI, or vice versa?
        if (isa<PHINode>(Result) != isa<PHINode>(I)) {
          // We need to fix up the insertion point.
          if (isa<PHINode>(I)) // PHI -> Non-PHI
            InsertPos = InstParent->getFirstInsertionPt();
          else // Non-PHI -> PHI
            InsertPos = InstParent->getFirstNonPHIIt();
        }

        Result->insertInto(InstParent, InsertPos);

        // Push the new instruction and any users onto the worklist.
        Worklist.pushUsersToWorkList(*Result);
        Worklist.push(Result);

        eraseInstFromFunction(*I);
      } else {
        LLVM_DEBUG(dbgs() << "IC: Mod = " << OrigI << '\n'
                          << "    New = " << *I << '\n');

        // If the instruction was modified, it's possible that it is now dead.
        // if so, remove it.
        if (isInstructionTriviallyDead(I, &TLI)) {
          eraseInstFromFunction(*I);
        } else {
          Worklist.pushUsersToWorkList(*I);
          Worklist.push(I);
        }
      }
      MadeIRChange = true;
    }
  }

  Worklist.zap();
  return MadeIRChange;
}

// Track the scopes used by !alias.scope and !noalias. In a function, a
// @llvm.experimental.noalias.scope.decl is only useful if that scope is used
// by both sets. If not, the declaration of the scope can be safely omitted.
// The MDNode of the scope can be omitted as well for the instructions that are
// part of this function. We do not do that at this point, as this might become
// too time consuming to do.
class AliasScopeTracker {
  SmallPtrSet<const MDNode *, 8> UsedAliasScopesAndLists;
  SmallPtrSet<const MDNode *, 8> UsedNoAliasScopesAndLists;

public:
  void analyse(Instruction *I) {
    // This seems to be faster than checking 'mayReadOrWriteMemory()'.
    if (!I->hasMetadataOtherThanDebugLoc())
      return;

    auto Track = [](Metadata *ScopeList, auto &Container) {
      const auto *MDScopeList = dyn_cast_or_null<MDNode>(ScopeList);
      if (!MDScopeList || !Container.insert(MDScopeList).second)
        return;
      for (const auto &MDOperand : MDScopeList->operands())
        if (auto *MDScope = dyn_cast<MDNode>(MDOperand))
          Container.insert(MDScope);
    };

    Track(I->getMetadata(LLVMContext::MD_alias_scope), UsedAliasScopesAndLists);
    Track(I->getMetadata(LLVMContext::MD_noalias), UsedNoAliasScopesAndLists);
  }

  bool isNoAliasScopeDeclDead(Instruction *Inst) {
    NoAliasScopeDeclInst *Decl = dyn_cast<NoAliasScopeDeclInst>(Inst);
    if (!Decl)
      return false;

    assert(Decl->use_empty() &&
           "llvm.experimental.noalias.scope.decl in use ?");
    const MDNode *MDSL = Decl->getScopeList();
    assert(MDSL->getNumOperands() == 1 &&
           "llvm.experimental.noalias.scope should refer to a single scope");
    auto &MDOperand = MDSL->getOperand(0);
    if (auto *MD = dyn_cast<MDNode>(MDOperand))
      return !UsedAliasScopesAndLists.contains(MD) ||
             !UsedNoAliasScopesAndLists.contains(MD);

    // Not an MDNode ? throw away.
    return true;
  }
};

/// Populate the IC worklist from a function, by walking it in reverse
/// post-order and adding all reachable code to the worklist.
///
/// This has a couple of tricks to make the code faster and more powerful.  In
/// particular, we constant fold and DCE instructions as we go, to avoid adding
/// them to the worklist (this significantly speeds up instcombine on code where
/// many instructions are dead or constant).  Additionally, if we find a branch
/// whose condition is a known constant, we only visit the reachable successors.
bool InstCombinerImpl::prepareWorklist(
    Function &F, ReversePostOrderTraversal<BasicBlock *> &RPOT) {
  bool MadeIRChange = false;
  SmallPtrSet<BasicBlock *, 32> LiveBlocks;
  SmallVector<Instruction *, 128> InstrsForInstructionWorklist;
  DenseMap<Constant *, Constant *> FoldedConstants;
  AliasScopeTracker SeenAliasScopes;

  auto HandleOnlyLiveSuccessor = [&](BasicBlock *BB, BasicBlock *LiveSucc) {
    for (BasicBlock *Succ : successors(BB))
      if (Succ != LiveSucc && DeadEdges.insert({BB, Succ}).second)
        for (PHINode &PN : Succ->phis())
          for (Use &U : PN.incoming_values())
            if (PN.getIncomingBlock(U) == BB && !isa<PoisonValue>(U)) {
              U.set(PoisonValue::get(PN.getType()));
              MadeIRChange = true;
            }
  };

  for (BasicBlock *BB : RPOT) {
    if (!BB->isEntryBlock() && all_of(predecessors(BB), [&](BasicBlock *Pred) {
          return DeadEdges.contains({Pred, BB}) || DT.dominates(BB, Pred);
        })) {
      HandleOnlyLiveSuccessor(BB, nullptr);
      continue;
    }
    LiveBlocks.insert(BB);

    for (Instruction &Inst : llvm::make_early_inc_range(*BB)) {
      // ConstantProp instruction if trivially constant.
      if (!Inst.use_empty() &&
          (Inst.getNumOperands() == 0 || isa<Constant>(Inst.getOperand(0))))
        if (Constant *C = ConstantFoldInstruction(&Inst, DL, &TLI)) {
          LLVM_DEBUG(dbgs() << "IC: ConstFold to: " << *C << " from: " << Inst
                            << '\n');
          Inst.replaceAllUsesWith(C);
          ++NumConstProp;
          if (isInstructionTriviallyDead(&Inst, &TLI))
            Inst.eraseFromParent();
          MadeIRChange = true;
          continue;
        }

      // See if we can constant fold its operands.
      for (Use &U : Inst.operands()) {
        if (!isa<ConstantVector>(U) && !isa<ConstantExpr>(U))
          continue;

        auto *C = cast<Constant>(U);
        Constant *&FoldRes = FoldedConstants[C];
        if (!FoldRes)
          FoldRes = ConstantFoldConstant(C, DL, &TLI);

        if (FoldRes != C) {
          LLVM_DEBUG(dbgs() << "IC: ConstFold operand of: " << Inst
                            << "\n    Old = " << *C
                            << "\n    New = " << *FoldRes << '\n');
          U = FoldRes;
          MadeIRChange = true;
        }
      }

      // Skip processing debug and pseudo intrinsics in InstCombine. Processing
      // these call instructions consumes non-trivial amount of time and
      // provides no value for the optimization.
      if (!Inst.isDebugOrPseudoInst()) {
        InstrsForInstructionWorklist.push_back(&Inst);
        SeenAliasScopes.analyse(&Inst);
      }
    }

    // If this is a branch or switch on a constant, mark only the single
    // live successor. Otherwise assume all successors are live.
    Instruction *TI = BB->getTerminator();
    if (BranchInst *BI = dyn_cast<BranchInst>(TI); BI && BI->isConditional()) {
      if (isa<UndefValue>(BI->getCondition())) {
        // Branch on undef is UB.
        HandleOnlyLiveSuccessor(BB, nullptr);
        continue;
      }
      if (auto *Cond = dyn_cast<ConstantInt>(BI->getCondition())) {
        bool CondVal = Cond->getZExtValue();
        HandleOnlyLiveSuccessor(BB, BI->getSuccessor(!CondVal));
        continue;
      }
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(TI)) {
      if (isa<UndefValue>(SI->getCondition())) {
        // Switch on undef is UB.
        HandleOnlyLiveSuccessor(BB, nullptr);
        continue;
      }
      if (auto *Cond = dyn_cast<ConstantInt>(SI->getCondition())) {
        HandleOnlyLiveSuccessor(BB,
                                SI->findCaseValue(Cond)->getCaseSuccessor());
        continue;
      }
    }
  }

  // Remove instructions inside unreachable blocks. This prevents the
  // instcombine code from having to deal with some bad special cases, and
  // reduces use counts of instructions.
  for (BasicBlock &BB : F) {
    if (LiveBlocks.count(&BB))
      continue;

    unsigned NumDeadInstInBB;
    unsigned NumDeadDbgInstInBB;
    std::tie(NumDeadInstInBB, NumDeadDbgInstInBB) =
        removeAllNonTerminatorAndEHPadInstructions(&BB);

    MadeIRChange |= NumDeadInstInBB + NumDeadDbgInstInBB > 0;
    NumDeadInst += NumDeadInstInBB;
  }

  // Once we've found all of the instructions to add to instcombine's worklist,
  // add them in reverse order.  This way instcombine will visit from the top
  // of the function down.  This jives well with the way that it adds all uses
  // of instructions to the worklist after doing a transformation, thus avoiding
  // some N^2 behavior in pathological cases.
  Worklist.reserve(InstrsForInstructionWorklist.size());
  for (Instruction *Inst : reverse(InstrsForInstructionWorklist)) {
    // DCE instruction if trivially dead. As we iterate in reverse program
    // order here, we will clean up whole chains of dead instructions.
    if (isInstructionTriviallyDead(Inst, &TLI) ||
        SeenAliasScopes.isNoAliasScopeDeclDead(Inst)) {
      ++NumDeadInst;
      LLVM_DEBUG(dbgs() << "IC: DCE: " << *Inst << '\n');
      salvageDebugInfo(*Inst);
      Inst->eraseFromParent();
      MadeIRChange = true;
      continue;
    }

    Worklist.push(Inst);
  }

  return MadeIRChange;
}

static bool combineInstructionsOverFunction(
    Function &F, InstructionWorklist &Worklist, AliasAnalysis *AA,
    AssumptionCache &AC, TargetLibraryInfo &TLI, TargetTransformInfo &TTI,
    DominatorTree &DT, OptimizationRemarkEmitter &ORE, BlockFrequencyInfo *BFI,
    BranchProbabilityInfo *BPI, ProfileSummaryInfo *PSI, LoopInfo *LI,
    const InstCombineOptions &Opts) {
  auto &DL = F.getDataLayout();

  /// Builder - This is an IRBuilder that automatically inserts new
  /// instructions into the worklist when they are created.
  IRBuilder<TargetFolder, IRBuilderCallbackInserter> Builder(
      F.getContext(), TargetFolder(DL),
      IRBuilderCallbackInserter([&Worklist, &AC](Instruction *I) {
        Worklist.add(I);
        if (auto *Assume = dyn_cast<AssumeInst>(I))
          AC.registerAssumption(Assume);
      }));

  ReversePostOrderTraversal<BasicBlock *> RPOT(&F.front());

  // Lower dbg.declare intrinsics otherwise their value may be clobbered
  // by instcombiner.
  bool MadeIRChange = false;
  if (ShouldLowerDbgDeclare)
    MadeIRChange = LowerDbgDeclare(F);

  // Iterate while there is work to do.
  unsigned Iteration = 0;
  while (true) {
    ++Iteration;

    if (Iteration > Opts.MaxIterations && !Opts.VerifyFixpoint) {
      LLVM_DEBUG(dbgs() << "\n\n[IC] Iteration limit #" << Opts.MaxIterations
                        << " on " << F.getName()
                        << " reached; stopping without verifying fixpoint\n");
      break;
    }

    ++NumWorklistIterations;
    LLVM_DEBUG(dbgs() << "\n\nINSTCOMBINE ITERATION #" << Iteration << " on "
                      << F.getName() << "\n");

    InstCombinerImpl IC(Worklist, Builder, F.hasMinSize(), AA, AC, TLI, TTI, DT,
                        ORE, BFI, BPI, PSI, DL, LI);
    IC.MaxArraySizeForCombine = MaxArraySize;
    bool MadeChangeInThisIteration = IC.prepareWorklist(F, RPOT);
    MadeChangeInThisIteration |= IC.run();
    if (!MadeChangeInThisIteration)
      break;

    MadeIRChange = true;
    if (Iteration > Opts.MaxIterations) {
      report_fatal_error(
          "Instruction Combining did not reach a fixpoint after " +
              Twine(Opts.MaxIterations) + " iterations",
          /*GenCrashDiag=*/false);
    }
  }

  if (Iteration == 1)
    ++NumOneIteration;
  else if (Iteration == 2)
    ++NumTwoIterations;
  else if (Iteration == 3)
    ++NumThreeIterations;
  else
    ++NumFourOrMoreIterations;

  return MadeIRChange;
}

InstCombinePass::InstCombinePass(InstCombineOptions Opts) : Options(Opts) {}

void InstCombinePass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<InstCombinePass> *>(this)->printPipeline(
      OS, MapClassName2PassName);
  OS << '<';
  OS << "max-iterations=" << Options.MaxIterations << ";";
  OS << (Options.UseLoopInfo ? "" : "no-") << "use-loop-info;";
  OS << (Options.VerifyFixpoint ? "" : "no-") << "verify-fixpoint";
  OS << '>';
}

PreservedAnalyses InstCombinePass::run(Function &F,
                                       FunctionAnalysisManager &AM) {
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);

  // TODO: Only use LoopInfo when the option is set. This requires that the
  //       callers in the pass pipeline explicitly set the option.
  auto *LI = AM.getCachedResult<LoopAnalysis>(F);
  if (!LI && Options.UseLoopInfo)
    LI = &AM.getResult<LoopAnalysis>(F);

  auto *AA = &AM.getResult<AAManager>(F);
  auto &MAMProxy = AM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
  ProfileSummaryInfo *PSI =
      MAMProxy.getCachedResult<ProfileSummaryAnalysis>(*F.getParent());
  auto *BFI = (PSI && PSI->hasProfileSummary()) ?
      &AM.getResult<BlockFrequencyAnalysis>(F) : nullptr;
  auto *BPI = AM.getCachedResult<BranchProbabilityAnalysis>(F);

  if (!combineInstructionsOverFunction(F, Worklist, AA, AC, TLI, TTI, DT, ORE,
                                       BFI, BPI, PSI, LI, Options))
    // No changes, all analyses are preserved.
    return PreservedAnalyses::all();

  // Mark all the analyses that instcombine updates as preserved.
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

void InstructionCombiningPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addRequired<AssumptionCacheTracker>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addRequired<TargetTransformInfoWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
  AU.addPreserved<DominatorTreeWrapperPass>();
  AU.addPreserved<AAResultsWrapperPass>();
  AU.addPreserved<BasicAAWrapperPass>();
  AU.addPreserved<GlobalsAAWrapperPass>();
  AU.addRequired<ProfileSummaryInfoWrapperPass>();
  LazyBlockFrequencyInfoPass::getLazyBFIAnalysisUsage(AU);
}

bool InstructionCombiningPass::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  // Required analyses.
  auto AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
  auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &ORE = getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE();

  // Optional analyses.
  auto *LIWP = getAnalysisIfAvailable<LoopInfoWrapperPass>();
  auto *LI = LIWP ? &LIWP->getLoopInfo() : nullptr;
  ProfileSummaryInfo *PSI =
      &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  BlockFrequencyInfo *BFI =
      (PSI && PSI->hasProfileSummary()) ?
      &getAnalysis<LazyBlockFrequencyInfoPass>().getBFI() :
      nullptr;
  BranchProbabilityInfo *BPI = nullptr;
  if (auto *WrapperPass =
          getAnalysisIfAvailable<BranchProbabilityInfoWrapperPass>())
    BPI = &WrapperPass->getBPI();

  return combineInstructionsOverFunction(F, Worklist, AA, AC, TLI, TTI, DT, ORE,
                                         BFI, BPI, PSI, LI,
                                         InstCombineOptions());
}

char InstructionCombiningPass::ID = 0;

InstructionCombiningPass::InstructionCombiningPass() : FunctionPass(ID) {
  initializeInstructionCombiningPassPass(*PassRegistry::getPassRegistry());
}

INITIALIZE_PASS_BEGIN(InstructionCombiningPass, "instcombine",
                      "Combine redundant instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LazyBlockFrequencyInfoPass)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_END(InstructionCombiningPass, "instcombine",
                    "Combine redundant instructions", false, false)

// Initialization Routines
void llvm::initializeInstCombine(PassRegistry &Registry) {
  initializeInstructionCombiningPassPass(Registry);
}

FunctionPass *llvm::createInstructionCombiningPass() {
  return new InstructionCombiningPass();
}
