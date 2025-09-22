//===-- ConstraintElimination.cpp - Eliminate conds using constraints. ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Eliminate conditions based on constraints collected from dominating
// conditions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/ConstraintElimination.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ConstraintSystem.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include <cmath>
#include <optional>
#include <string>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "constraint-elimination"

STATISTIC(NumCondsRemoved, "Number of instructions removed");
DEBUG_COUNTER(EliminatedCounter, "conds-eliminated",
              "Controls which conditions are eliminated");

static cl::opt<unsigned>
    MaxRows("constraint-elimination-max-rows", cl::init(500), cl::Hidden,
            cl::desc("Maximum number of rows to keep in constraint system"));

static cl::opt<bool> DumpReproducers(
    "constraint-elimination-dump-reproducers", cl::init(false), cl::Hidden,
    cl::desc("Dump IR to reproduce successful transformations."));

static int64_t MaxConstraintValue = std::numeric_limits<int64_t>::max();
static int64_t MinSignedConstraintValue = std::numeric_limits<int64_t>::min();

// A helper to multiply 2 signed integers where overflowing is allowed.
static int64_t multiplyWithOverflow(int64_t A, int64_t B) {
  int64_t Result;
  MulOverflow(A, B, Result);
  return Result;
}

// A helper to add 2 signed integers where overflowing is allowed.
static int64_t addWithOverflow(int64_t A, int64_t B) {
  int64_t Result;
  AddOverflow(A, B, Result);
  return Result;
}

static Instruction *getContextInstForUse(Use &U) {
  Instruction *UserI = cast<Instruction>(U.getUser());
  if (auto *Phi = dyn_cast<PHINode>(UserI))
    UserI = Phi->getIncomingBlock(U)->getTerminator();
  return UserI;
}

namespace {
/// Struct to express a condition of the form %Op0 Pred %Op1.
struct ConditionTy {
  CmpInst::Predicate Pred;
  Value *Op0;
  Value *Op1;

  ConditionTy()
      : Pred(CmpInst::BAD_ICMP_PREDICATE), Op0(nullptr), Op1(nullptr) {}
  ConditionTy(CmpInst::Predicate Pred, Value *Op0, Value *Op1)
      : Pred(Pred), Op0(Op0), Op1(Op1) {}
};

/// Represents either
///  * a condition that holds on entry to a block (=condition fact)
///  * an assume (=assume fact)
///  * a use of a compare instruction to simplify.
/// It also tracks the Dominator DFS in and out numbers for each entry.
struct FactOrCheck {
  enum class EntryTy {
    ConditionFact, /// A condition that holds on entry to a block.
    InstFact,      /// A fact that holds after Inst executed (e.g. an assume or
                   /// min/mix intrinsic.
    InstCheck,     /// An instruction to simplify (e.g. an overflow math
                   /// intrinsics).
    UseCheck       /// An use of a compare instruction to simplify.
  };

  union {
    Instruction *Inst;
    Use *U;
    ConditionTy Cond;
  };

  /// A pre-condition that must hold for the current fact to be added to the
  /// system.
  ConditionTy DoesHold;

  unsigned NumIn;
  unsigned NumOut;
  EntryTy Ty;

  FactOrCheck(EntryTy Ty, DomTreeNode *DTN, Instruction *Inst)
      : Inst(Inst), NumIn(DTN->getDFSNumIn()), NumOut(DTN->getDFSNumOut()),
        Ty(Ty) {}

  FactOrCheck(DomTreeNode *DTN, Use *U)
      : U(U), DoesHold(CmpInst::BAD_ICMP_PREDICATE, nullptr, nullptr),
        NumIn(DTN->getDFSNumIn()), NumOut(DTN->getDFSNumOut()),
        Ty(EntryTy::UseCheck) {}

  FactOrCheck(DomTreeNode *DTN, CmpInst::Predicate Pred, Value *Op0, Value *Op1,
              ConditionTy Precond = ConditionTy())
      : Cond(Pred, Op0, Op1), DoesHold(Precond), NumIn(DTN->getDFSNumIn()),
        NumOut(DTN->getDFSNumOut()), Ty(EntryTy::ConditionFact) {}

  static FactOrCheck getConditionFact(DomTreeNode *DTN, CmpInst::Predicate Pred,
                                      Value *Op0, Value *Op1,
                                      ConditionTy Precond = ConditionTy()) {
    return FactOrCheck(DTN, Pred, Op0, Op1, Precond);
  }

  static FactOrCheck getInstFact(DomTreeNode *DTN, Instruction *Inst) {
    return FactOrCheck(EntryTy::InstFact, DTN, Inst);
  }

  static FactOrCheck getCheck(DomTreeNode *DTN, Use *U) {
    return FactOrCheck(DTN, U);
  }

  static FactOrCheck getCheck(DomTreeNode *DTN, CallInst *CI) {
    return FactOrCheck(EntryTy::InstCheck, DTN, CI);
  }

  bool isCheck() const {
    return Ty == EntryTy::InstCheck || Ty == EntryTy::UseCheck;
  }

  Instruction *getContextInst() const {
    if (Ty == EntryTy::UseCheck)
      return getContextInstForUse(*U);
    return Inst;
  }

  Instruction *getInstructionToSimplify() const {
    assert(isCheck());
    if (Ty == EntryTy::InstCheck)
      return Inst;
    // The use may have been simplified to a constant already.
    return dyn_cast<Instruction>(*U);
  }

  bool isConditionFact() const { return Ty == EntryTy::ConditionFact; }
};

/// Keep state required to build worklist.
struct State {
  DominatorTree &DT;
  LoopInfo &LI;
  ScalarEvolution &SE;
  SmallVector<FactOrCheck, 64> WorkList;

  State(DominatorTree &DT, LoopInfo &LI, ScalarEvolution &SE)
      : DT(DT), LI(LI), SE(SE) {}

  /// Process block \p BB and add known facts to work-list.
  void addInfoFor(BasicBlock &BB);

  /// Try to add facts for loop inductions (AddRecs) in EQ/NE compares
  /// controlling the loop header.
  void addInfoForInductions(BasicBlock &BB);

  /// Returns true if we can add a known condition from BB to its successor
  /// block Succ.
  bool canAddSuccessor(BasicBlock &BB, BasicBlock *Succ) const {
    return DT.dominates(BasicBlockEdge(&BB, Succ), Succ);
  }
};

class ConstraintInfo;

struct StackEntry {
  unsigned NumIn;
  unsigned NumOut;
  bool IsSigned = false;
  /// Variables that can be removed from the system once the stack entry gets
  /// removed.
  SmallVector<Value *, 2> ValuesToRelease;

  StackEntry(unsigned NumIn, unsigned NumOut, bool IsSigned,
             SmallVector<Value *, 2> ValuesToRelease)
      : NumIn(NumIn), NumOut(NumOut), IsSigned(IsSigned),
        ValuesToRelease(ValuesToRelease) {}
};

struct ConstraintTy {
  SmallVector<int64_t, 8> Coefficients;
  SmallVector<ConditionTy, 2> Preconditions;

  SmallVector<SmallVector<int64_t, 8>> ExtraInfo;

  bool IsSigned = false;

  ConstraintTy() = default;

  ConstraintTy(SmallVector<int64_t, 8> Coefficients, bool IsSigned, bool IsEq,
               bool IsNe)
      : Coefficients(std::move(Coefficients)), IsSigned(IsSigned), IsEq(IsEq),
        IsNe(IsNe) {}

  unsigned size() const { return Coefficients.size(); }

  unsigned empty() const { return Coefficients.empty(); }

  /// Returns true if all preconditions for this list of constraints are
  /// satisfied given \p CS and the corresponding \p Value2Index mapping.
  bool isValid(const ConstraintInfo &Info) const;

  bool isEq() const { return IsEq; }

  bool isNe() const { return IsNe; }

  /// Check if the current constraint is implied by the given ConstraintSystem.
  ///
  /// \return true or false if the constraint is proven to be respectively true,
  /// or false. When the constraint cannot be proven to be either true or false,
  /// std::nullopt is returned.
  std::optional<bool> isImpliedBy(const ConstraintSystem &CS) const;

private:
  bool IsEq = false;
  bool IsNe = false;
};

/// Wrapper encapsulating separate constraint systems and corresponding value
/// mappings for both unsigned and signed information. Facts are added to and
/// conditions are checked against the corresponding system depending on the
/// signed-ness of their predicates. While the information is kept separate
/// based on signed-ness, certain conditions can be transferred between the two
/// systems.
class ConstraintInfo {

  ConstraintSystem UnsignedCS;
  ConstraintSystem SignedCS;

  const DataLayout &DL;

public:
  ConstraintInfo(const DataLayout &DL, ArrayRef<Value *> FunctionArgs)
      : UnsignedCS(FunctionArgs), SignedCS(FunctionArgs), DL(DL) {
    auto &Value2Index = getValue2Index(false);
    // Add Arg > -1 constraints to unsigned system for all function arguments.
    for (Value *Arg : FunctionArgs) {
      ConstraintTy VarPos(SmallVector<int64_t, 8>(Value2Index.size() + 1, 0),
                          false, false, false);
      VarPos.Coefficients[Value2Index[Arg]] = -1;
      UnsignedCS.addVariableRow(VarPos.Coefficients);
    }
  }

  DenseMap<Value *, unsigned> &getValue2Index(bool Signed) {
    return Signed ? SignedCS.getValue2Index() : UnsignedCS.getValue2Index();
  }
  const DenseMap<Value *, unsigned> &getValue2Index(bool Signed) const {
    return Signed ? SignedCS.getValue2Index() : UnsignedCS.getValue2Index();
  }

  ConstraintSystem &getCS(bool Signed) {
    return Signed ? SignedCS : UnsignedCS;
  }
  const ConstraintSystem &getCS(bool Signed) const {
    return Signed ? SignedCS : UnsignedCS;
  }

  void popLastConstraint(bool Signed) { getCS(Signed).popLastConstraint(); }
  void popLastNVariables(bool Signed, unsigned N) {
    getCS(Signed).popLastNVariables(N);
  }

  bool doesHold(CmpInst::Predicate Pred, Value *A, Value *B) const;

  void addFact(CmpInst::Predicate Pred, Value *A, Value *B, unsigned NumIn,
               unsigned NumOut, SmallVectorImpl<StackEntry> &DFSInStack);

  /// Turn a comparison of the form \p Op0 \p Pred \p Op1 into a vector of
  /// constraints, using indices from the corresponding constraint system.
  /// New variables that need to be added to the system are collected in
  /// \p NewVariables.
  ConstraintTy getConstraint(CmpInst::Predicate Pred, Value *Op0, Value *Op1,
                             SmallVectorImpl<Value *> &NewVariables) const;

  /// Turns a comparison of the form \p Op0 \p Pred \p Op1 into a vector of
  /// constraints using getConstraint. Returns an empty constraint if the result
  /// cannot be used to query the existing constraint system, e.g. because it
  /// would require adding new variables. Also tries to convert signed
  /// predicates to unsigned ones if possible to allow using the unsigned system
  /// which increases the effectiveness of the signed <-> unsigned transfer
  /// logic.
  ConstraintTy getConstraintForSolving(CmpInst::Predicate Pred, Value *Op0,
                                       Value *Op1) const;

  /// Try to add information from \p A \p Pred \p B to the unsigned/signed
  /// system if \p Pred is signed/unsigned.
  void transferToOtherSystem(CmpInst::Predicate Pred, Value *A, Value *B,
                             unsigned NumIn, unsigned NumOut,
                             SmallVectorImpl<StackEntry> &DFSInStack);
};

/// Represents a (Coefficient * Variable) entry after IR decomposition.
struct DecompEntry {
  int64_t Coefficient;
  Value *Variable;
  /// True if the variable is known positive in the current constraint.
  bool IsKnownNonNegative;

  DecompEntry(int64_t Coefficient, Value *Variable,
              bool IsKnownNonNegative = false)
      : Coefficient(Coefficient), Variable(Variable),
        IsKnownNonNegative(IsKnownNonNegative) {}
};

/// Represents an Offset + Coefficient1 * Variable1 + ... decomposition.
struct Decomposition {
  int64_t Offset = 0;
  SmallVector<DecompEntry, 3> Vars;

  Decomposition(int64_t Offset) : Offset(Offset) {}
  Decomposition(Value *V, bool IsKnownNonNegative = false) {
    Vars.emplace_back(1, V, IsKnownNonNegative);
  }
  Decomposition(int64_t Offset, ArrayRef<DecompEntry> Vars)
      : Offset(Offset), Vars(Vars) {}

  void add(int64_t OtherOffset) {
    Offset = addWithOverflow(Offset, OtherOffset);
  }

  void add(const Decomposition &Other) {
    add(Other.Offset);
    append_range(Vars, Other.Vars);
  }

  void sub(const Decomposition &Other) {
    Decomposition Tmp = Other;
    Tmp.mul(-1);
    add(Tmp.Offset);
    append_range(Vars, Tmp.Vars);
  }

  void mul(int64_t Factor) {
    Offset = multiplyWithOverflow(Offset, Factor);
    for (auto &Var : Vars)
      Var.Coefficient = multiplyWithOverflow(Var.Coefficient, Factor);
  }
};

// Variable and constant offsets for a chain of GEPs, with base pointer BasePtr.
struct OffsetResult {
  Value *BasePtr;
  APInt ConstantOffset;
  MapVector<Value *, APInt> VariableOffsets;
  bool AllInbounds;

  OffsetResult() : BasePtr(nullptr), ConstantOffset(0, uint64_t(0)) {}

  OffsetResult(GEPOperator &GEP, const DataLayout &DL)
      : BasePtr(GEP.getPointerOperand()), AllInbounds(GEP.isInBounds()) {
    ConstantOffset = APInt(DL.getIndexTypeSizeInBits(BasePtr->getType()), 0);
  }
};
} // namespace

// Try to collect variable and constant offsets for \p GEP, partly traversing
// nested GEPs. Returns an OffsetResult with nullptr as BasePtr of collecting
// the offset fails.
static OffsetResult collectOffsets(GEPOperator &GEP, const DataLayout &DL) {
  OffsetResult Result(GEP, DL);
  unsigned BitWidth = Result.ConstantOffset.getBitWidth();
  if (!GEP.collectOffset(DL, BitWidth, Result.VariableOffsets,
                         Result.ConstantOffset))
    return {};

  // If we have a nested GEP, check if we can combine the constant offset of the
  // inner GEP with the outer GEP.
  if (auto *InnerGEP = dyn_cast<GetElementPtrInst>(Result.BasePtr)) {
    MapVector<Value *, APInt> VariableOffsets2;
    APInt ConstantOffset2(BitWidth, 0);
    bool CanCollectInner = InnerGEP->collectOffset(
        DL, BitWidth, VariableOffsets2, ConstantOffset2);
    // TODO: Support cases with more than 1 variable offset.
    if (!CanCollectInner || Result.VariableOffsets.size() > 1 ||
        VariableOffsets2.size() > 1 ||
        (Result.VariableOffsets.size() >= 1 && VariableOffsets2.size() >= 1)) {
      // More than 1 variable index, use outer result.
      return Result;
    }
    Result.BasePtr = InnerGEP->getPointerOperand();
    Result.ConstantOffset += ConstantOffset2;
    if (Result.VariableOffsets.size() == 0 && VariableOffsets2.size() == 1)
      Result.VariableOffsets = VariableOffsets2;
    Result.AllInbounds &= InnerGEP->isInBounds();
  }
  return Result;
}

static Decomposition decompose(Value *V,
                               SmallVectorImpl<ConditionTy> &Preconditions,
                               bool IsSigned, const DataLayout &DL);

static bool canUseSExt(ConstantInt *CI) {
  const APInt &Val = CI->getValue();
  return Val.sgt(MinSignedConstraintValue) && Val.slt(MaxConstraintValue);
}

static Decomposition decomposeGEP(GEPOperator &GEP,
                                  SmallVectorImpl<ConditionTy> &Preconditions,
                                  bool IsSigned, const DataLayout &DL) {
  // Do not reason about pointers where the index size is larger than 64 bits,
  // as the coefficients used to encode constraints are 64 bit integers.
  if (DL.getIndexTypeSizeInBits(GEP.getPointerOperand()->getType()) > 64)
    return &GEP;

  assert(!IsSigned && "The logic below only supports decomposition for "
                      "unsigned predicates at the moment.");
  const auto &[BasePtr, ConstantOffset, VariableOffsets, AllInbounds] =
      collectOffsets(GEP, DL);
  if (!BasePtr || !AllInbounds)
    return &GEP;

  Decomposition Result(ConstantOffset.getSExtValue(), DecompEntry(1, BasePtr));
  for (auto [Index, Scale] : VariableOffsets) {
    auto IdxResult = decompose(Index, Preconditions, IsSigned, DL);
    IdxResult.mul(Scale.getSExtValue());
    Result.add(IdxResult);

    // If Op0 is signed non-negative, the GEP is increasing monotonically and
    // can be de-composed.
    if (!isKnownNonNegative(Index, DL))
      Preconditions.emplace_back(CmpInst::ICMP_SGE, Index,
                                 ConstantInt::get(Index->getType(), 0));
  }
  return Result;
}

// Decomposes \p V into a constant offset + list of pairs { Coefficient,
// Variable } where Coefficient * Variable. The sum of the constant offset and
// pairs equals \p V.
static Decomposition decompose(Value *V,
                               SmallVectorImpl<ConditionTy> &Preconditions,
                               bool IsSigned, const DataLayout &DL) {

  auto MergeResults = [&Preconditions, IsSigned, &DL](Value *A, Value *B,
                                                      bool IsSignedB) {
    auto ResA = decompose(A, Preconditions, IsSigned, DL);
    auto ResB = decompose(B, Preconditions, IsSignedB, DL);
    ResA.add(ResB);
    return ResA;
  };

  Type *Ty = V->getType()->getScalarType();
  if (Ty->isPointerTy() && !IsSigned) {
    if (auto *GEP = dyn_cast<GEPOperator>(V))
      return decomposeGEP(*GEP, Preconditions, IsSigned, DL);
    if (isa<ConstantPointerNull>(V))
      return int64_t(0);

    return V;
  }

  // Don't handle integers > 64 bit. Our coefficients are 64-bit large, so
  // coefficient add/mul may wrap, while the operation in the full bit width
  // would not.
  if (!Ty->isIntegerTy() || Ty->getIntegerBitWidth() > 64)
    return V;

  bool IsKnownNonNegative = false;

  // Decompose \p V used with a signed predicate.
  if (IsSigned) {
    if (auto *CI = dyn_cast<ConstantInt>(V)) {
      if (canUseSExt(CI))
        return CI->getSExtValue();
    }
    Value *Op0;
    Value *Op1;

    if (match(V, m_SExt(m_Value(Op0))))
      V = Op0;
    else if (match(V, m_NNegZExt(m_Value(Op0)))) {
      V = Op0;
      IsKnownNonNegative = true;
    }

    if (match(V, m_NSWAdd(m_Value(Op0), m_Value(Op1))))
      return MergeResults(Op0, Op1, IsSigned);

    ConstantInt *CI;
    if (match(V, m_NSWMul(m_Value(Op0), m_ConstantInt(CI))) && canUseSExt(CI)) {
      auto Result = decompose(Op0, Preconditions, IsSigned, DL);
      Result.mul(CI->getSExtValue());
      return Result;
    }

    // (shl nsw x, shift) is (mul nsw x, (1<<shift)), with the exception of
    // shift == bw-1.
    if (match(V, m_NSWShl(m_Value(Op0), m_ConstantInt(CI)))) {
      uint64_t Shift = CI->getValue().getLimitedValue();
      if (Shift < Ty->getIntegerBitWidth() - 1) {
        assert(Shift < 64 && "Would overflow");
        auto Result = decompose(Op0, Preconditions, IsSigned, DL);
        Result.mul(int64_t(1) << Shift);
        return Result;
      }
    }

    return {V, IsKnownNonNegative};
  }

  if (auto *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->uge(MaxConstraintValue))
      return V;
    return int64_t(CI->getZExtValue());
  }

  Value *Op0;
  if (match(V, m_ZExt(m_Value(Op0)))) {
    IsKnownNonNegative = true;
    V = Op0;
  }

  if (match(V, m_SExt(m_Value(Op0)))) {
    V = Op0;
    Preconditions.emplace_back(CmpInst::ICMP_SGE, Op0,
                               ConstantInt::get(Op0->getType(), 0));
  }

  Value *Op1;
  ConstantInt *CI;
  if (match(V, m_NUWAdd(m_Value(Op0), m_Value(Op1)))) {
    return MergeResults(Op0, Op1, IsSigned);
  }
  if (match(V, m_NSWAdd(m_Value(Op0), m_Value(Op1)))) {
    if (!isKnownNonNegative(Op0, DL))
      Preconditions.emplace_back(CmpInst::ICMP_SGE, Op0,
                                 ConstantInt::get(Op0->getType(), 0));
    if (!isKnownNonNegative(Op1, DL))
      Preconditions.emplace_back(CmpInst::ICMP_SGE, Op1,
                                 ConstantInt::get(Op1->getType(), 0));

    return MergeResults(Op0, Op1, IsSigned);
  }

  if (match(V, m_Add(m_Value(Op0), m_ConstantInt(CI))) && CI->isNegative() &&
      canUseSExt(CI)) {
    Preconditions.emplace_back(
        CmpInst::ICMP_UGE, Op0,
        ConstantInt::get(Op0->getType(), CI->getSExtValue() * -1));
    return MergeResults(Op0, CI, true);
  }

  // Decompose or as an add if there are no common bits between the operands.
  if (match(V, m_DisjointOr(m_Value(Op0), m_ConstantInt(CI))))
    return MergeResults(Op0, CI, IsSigned);

  if (match(V, m_NUWShl(m_Value(Op1), m_ConstantInt(CI))) && canUseSExt(CI)) {
    if (CI->getSExtValue() < 0 || CI->getSExtValue() >= 64)
      return {V, IsKnownNonNegative};
    auto Result = decompose(Op1, Preconditions, IsSigned, DL);
    Result.mul(int64_t{1} << CI->getSExtValue());
    return Result;
  }

  if (match(V, m_NUWMul(m_Value(Op1), m_ConstantInt(CI))) && canUseSExt(CI) &&
      (!CI->isNegative())) {
    auto Result = decompose(Op1, Preconditions, IsSigned, DL);
    Result.mul(CI->getSExtValue());
    return Result;
  }

  if (match(V, m_NUWSub(m_Value(Op0), m_Value(Op1)))) {
    auto ResA = decompose(Op0, Preconditions, IsSigned, DL);
    auto ResB = decompose(Op1, Preconditions, IsSigned, DL);
    ResA.sub(ResB);
    return ResA;
  }

  return {V, IsKnownNonNegative};
}

ConstraintTy
ConstraintInfo::getConstraint(CmpInst::Predicate Pred, Value *Op0, Value *Op1,
                              SmallVectorImpl<Value *> &NewVariables) const {
  assert(NewVariables.empty() && "NewVariables must be empty when passed in");
  bool IsEq = false;
  bool IsNe = false;

  // Try to convert Pred to one of ULE/SLT/SLE/SLT.
  switch (Pred) {
  case CmpInst::ICMP_UGT:
  case CmpInst::ICMP_UGE:
  case CmpInst::ICMP_SGT:
  case CmpInst::ICMP_SGE: {
    Pred = CmpInst::getSwappedPredicate(Pred);
    std::swap(Op0, Op1);
    break;
  }
  case CmpInst::ICMP_EQ:
    if (match(Op1, m_Zero())) {
      Pred = CmpInst::ICMP_ULE;
    } else {
      IsEq = true;
      Pred = CmpInst::ICMP_ULE;
    }
    break;
  case CmpInst::ICMP_NE:
    if (match(Op1, m_Zero())) {
      Pred = CmpInst::getSwappedPredicate(CmpInst::ICMP_UGT);
      std::swap(Op0, Op1);
    } else {
      IsNe = true;
      Pred = CmpInst::ICMP_ULE;
    }
    break;
  default:
    break;
  }

  if (Pred != CmpInst::ICMP_ULE && Pred != CmpInst::ICMP_ULT &&
      Pred != CmpInst::ICMP_SLE && Pred != CmpInst::ICMP_SLT)
    return {};

  SmallVector<ConditionTy, 4> Preconditions;
  bool IsSigned = CmpInst::isSigned(Pred);
  auto &Value2Index = getValue2Index(IsSigned);
  auto ADec = decompose(Op0->stripPointerCastsSameRepresentation(),
                        Preconditions, IsSigned, DL);
  auto BDec = decompose(Op1->stripPointerCastsSameRepresentation(),
                        Preconditions, IsSigned, DL);
  int64_t Offset1 = ADec.Offset;
  int64_t Offset2 = BDec.Offset;
  Offset1 *= -1;

  auto &VariablesA = ADec.Vars;
  auto &VariablesB = BDec.Vars;

  // First try to look up \p V in Value2Index and NewVariables. Otherwise add a
  // new entry to NewVariables.
  SmallDenseMap<Value *, unsigned> NewIndexMap;
  auto GetOrAddIndex = [&Value2Index, &NewVariables,
                        &NewIndexMap](Value *V) -> unsigned {
    auto V2I = Value2Index.find(V);
    if (V2I != Value2Index.end())
      return V2I->second;
    auto Insert =
        NewIndexMap.insert({V, Value2Index.size() + NewVariables.size() + 1});
    if (Insert.second)
      NewVariables.push_back(V);
    return Insert.first->second;
  };

  // Make sure all variables have entries in Value2Index or NewVariables.
  for (const auto &KV : concat<DecompEntry>(VariablesA, VariablesB))
    GetOrAddIndex(KV.Variable);

  // Build result constraint, by first adding all coefficients from A and then
  // subtracting all coefficients from B.
  ConstraintTy Res(
      SmallVector<int64_t, 8>(Value2Index.size() + NewVariables.size() + 1, 0),
      IsSigned, IsEq, IsNe);
  // Collect variables that are known to be positive in all uses in the
  // constraint.
  SmallDenseMap<Value *, bool> KnownNonNegativeVariables;
  auto &R = Res.Coefficients;
  for (const auto &KV : VariablesA) {
    R[GetOrAddIndex(KV.Variable)] += KV.Coefficient;
    auto I =
        KnownNonNegativeVariables.insert({KV.Variable, KV.IsKnownNonNegative});
    I.first->second &= KV.IsKnownNonNegative;
  }

  for (const auto &KV : VariablesB) {
    if (SubOverflow(R[GetOrAddIndex(KV.Variable)], KV.Coefficient,
                    R[GetOrAddIndex(KV.Variable)]))
      return {};
    auto I =
        KnownNonNegativeVariables.insert({KV.Variable, KV.IsKnownNonNegative});
    I.first->second &= KV.IsKnownNonNegative;
  }

  int64_t OffsetSum;
  if (AddOverflow(Offset1, Offset2, OffsetSum))
    return {};
  if (Pred == (IsSigned ? CmpInst::ICMP_SLT : CmpInst::ICMP_ULT))
    if (AddOverflow(OffsetSum, int64_t(-1), OffsetSum))
      return {};
  R[0] = OffsetSum;
  Res.Preconditions = std::move(Preconditions);

  // Remove any (Coefficient, Variable) entry where the Coefficient is 0 for new
  // variables.
  while (!NewVariables.empty()) {
    int64_t Last = R.back();
    if (Last != 0)
      break;
    R.pop_back();
    Value *RemovedV = NewVariables.pop_back_val();
    NewIndexMap.erase(RemovedV);
  }

  // Add extra constraints for variables that are known positive.
  for (auto &KV : KnownNonNegativeVariables) {
    if (!KV.second ||
        (!Value2Index.contains(KV.first) && !NewIndexMap.contains(KV.first)))
      continue;
    SmallVector<int64_t, 8> C(Value2Index.size() + NewVariables.size() + 1, 0);
    C[GetOrAddIndex(KV.first)] = -1;
    Res.ExtraInfo.push_back(C);
  }
  return Res;
}

ConstraintTy ConstraintInfo::getConstraintForSolving(CmpInst::Predicate Pred,
                                                     Value *Op0,
                                                     Value *Op1) const {
  Constant *NullC = Constant::getNullValue(Op0->getType());
  // Handle trivially true compares directly to avoid adding V UGE 0 constraints
  // for all variables in the unsigned system.
  if ((Pred == CmpInst::ICMP_ULE && Op0 == NullC) ||
      (Pred == CmpInst::ICMP_UGE && Op1 == NullC)) {
    auto &Value2Index = getValue2Index(false);
    // Return constraint that's trivially true.
    return ConstraintTy(SmallVector<int64_t, 8>(Value2Index.size(), 0), false,
                        false, false);
  }

  // If both operands are known to be non-negative, change signed predicates to
  // unsigned ones. This increases the reasoning effectiveness in combination
  // with the signed <-> unsigned transfer logic.
  if (CmpInst::isSigned(Pred) &&
      isKnownNonNegative(Op0, DL, /*Depth=*/MaxAnalysisRecursionDepth - 1) &&
      isKnownNonNegative(Op1, DL, /*Depth=*/MaxAnalysisRecursionDepth - 1))
    Pred = CmpInst::getUnsignedPredicate(Pred);

  SmallVector<Value *> NewVariables;
  ConstraintTy R = getConstraint(Pred, Op0, Op1, NewVariables);
  if (!NewVariables.empty())
    return {};
  return R;
}

bool ConstraintTy::isValid(const ConstraintInfo &Info) const {
  return Coefficients.size() > 0 &&
         all_of(Preconditions, [&Info](const ConditionTy &C) {
           return Info.doesHold(C.Pred, C.Op0, C.Op1);
         });
}

std::optional<bool>
ConstraintTy::isImpliedBy(const ConstraintSystem &CS) const {
  bool IsConditionImplied = CS.isConditionImplied(Coefficients);

  if (IsEq || IsNe) {
    auto NegatedOrEqual = ConstraintSystem::negateOrEqual(Coefficients);
    bool IsNegatedOrEqualImplied =
        !NegatedOrEqual.empty() && CS.isConditionImplied(NegatedOrEqual);

    // In order to check that `%a == %b` is true (equality), both conditions `%a
    // >= %b` and `%a <= %b` must hold true. When checking for equality (`IsEq`
    // is true), we return true if they both hold, false in the other cases.
    if (IsConditionImplied && IsNegatedOrEqualImplied)
      return IsEq;

    auto Negated = ConstraintSystem::negate(Coefficients);
    bool IsNegatedImplied = !Negated.empty() && CS.isConditionImplied(Negated);

    auto StrictLessThan = ConstraintSystem::toStrictLessThan(Coefficients);
    bool IsStrictLessThanImplied =
        !StrictLessThan.empty() && CS.isConditionImplied(StrictLessThan);

    // In order to check that `%a != %b` is true (non-equality), either
    // condition `%a > %b` or `%a < %b` must hold true. When checking for
    // non-equality (`IsNe` is true), we return true if one of the two holds,
    // false in the other cases.
    if (IsNegatedImplied || IsStrictLessThanImplied)
      return IsNe;

    return std::nullopt;
  }

  if (IsConditionImplied)
    return true;

  auto Negated = ConstraintSystem::negate(Coefficients);
  auto IsNegatedImplied = !Negated.empty() && CS.isConditionImplied(Negated);
  if (IsNegatedImplied)
    return false;

  // Neither the condition nor its negated holds, did not prove anything.
  return std::nullopt;
}

bool ConstraintInfo::doesHold(CmpInst::Predicate Pred, Value *A,
                              Value *B) const {
  auto R = getConstraintForSolving(Pred, A, B);
  return R.isValid(*this) &&
         getCS(R.IsSigned).isConditionImplied(R.Coefficients);
}

void ConstraintInfo::transferToOtherSystem(
    CmpInst::Predicate Pred, Value *A, Value *B, unsigned NumIn,
    unsigned NumOut, SmallVectorImpl<StackEntry> &DFSInStack) {
  auto IsKnownNonNegative = [this](Value *V) {
    return doesHold(CmpInst::ICMP_SGE, V, ConstantInt::get(V->getType(), 0)) ||
           isKnownNonNegative(V, DL, /*Depth=*/MaxAnalysisRecursionDepth - 1);
  };
  // Check if we can combine facts from the signed and unsigned systems to
  // derive additional facts.
  if (!A->getType()->isIntegerTy())
    return;
  // FIXME: This currently depends on the order we add facts. Ideally we
  // would first add all known facts and only then try to add additional
  // facts.
  switch (Pred) {
  default:
    break;
  case CmpInst::ICMP_ULT:
  case CmpInst::ICMP_ULE:
    //  If B is a signed positive constant, then A >=s 0 and A <s (or <=s) B.
    if (IsKnownNonNegative(B)) {
      addFact(CmpInst::ICMP_SGE, A, ConstantInt::get(B->getType(), 0), NumIn,
              NumOut, DFSInStack);
      addFact(CmpInst::getSignedPredicate(Pred), A, B, NumIn, NumOut,
              DFSInStack);
    }
    break;
  case CmpInst::ICMP_UGE:
  case CmpInst::ICMP_UGT:
    //  If A is a signed positive constant, then B >=s 0 and A >s (or >=s) B.
    if (IsKnownNonNegative(A)) {
      addFact(CmpInst::ICMP_SGE, B, ConstantInt::get(B->getType(), 0), NumIn,
              NumOut, DFSInStack);
      addFact(CmpInst::getSignedPredicate(Pred), A, B, NumIn, NumOut,
              DFSInStack);
    }
    break;
  case CmpInst::ICMP_SLT:
    if (IsKnownNonNegative(A))
      addFact(CmpInst::ICMP_ULT, A, B, NumIn, NumOut, DFSInStack);
    break;
  case CmpInst::ICMP_SGT: {
    if (doesHold(CmpInst::ICMP_SGE, B, ConstantInt::get(B->getType(), -1)))
      addFact(CmpInst::ICMP_UGE, A, ConstantInt::get(B->getType(), 0), NumIn,
              NumOut, DFSInStack);
    if (IsKnownNonNegative(B))
      addFact(CmpInst::ICMP_UGT, A, B, NumIn, NumOut, DFSInStack);

    break;
  }
  case CmpInst::ICMP_SGE:
    if (IsKnownNonNegative(B))
      addFact(CmpInst::ICMP_UGE, A, B, NumIn, NumOut, DFSInStack);
    break;
  }
}

#ifndef NDEBUG

static void dumpConstraint(ArrayRef<int64_t> C,
                           const DenseMap<Value *, unsigned> &Value2Index) {
  ConstraintSystem CS(Value2Index);
  CS.addVariableRowFill(C);
  CS.dump();
}
#endif

void State::addInfoForInductions(BasicBlock &BB) {
  auto *L = LI.getLoopFor(&BB);
  if (!L || L->getHeader() != &BB)
    return;

  Value *A;
  Value *B;
  CmpInst::Predicate Pred;

  if (!match(BB.getTerminator(),
             m_Br(m_ICmp(Pred, m_Value(A), m_Value(B)), m_Value(), m_Value())))
    return;
  PHINode *PN = dyn_cast<PHINode>(A);
  if (!PN) {
    Pred = CmpInst::getSwappedPredicate(Pred);
    std::swap(A, B);
    PN = dyn_cast<PHINode>(A);
  }

  if (!PN || PN->getParent() != &BB || PN->getNumIncomingValues() != 2 ||
      !SE.isSCEVable(PN->getType()))
    return;

  BasicBlock *InLoopSucc = nullptr;
  if (Pred == CmpInst::ICMP_NE)
    InLoopSucc = cast<BranchInst>(BB.getTerminator())->getSuccessor(0);
  else if (Pred == CmpInst::ICMP_EQ)
    InLoopSucc = cast<BranchInst>(BB.getTerminator())->getSuccessor(1);
  else
    return;

  if (!L->contains(InLoopSucc) || !L->isLoopExiting(&BB) || InLoopSucc == &BB)
    return;

  auto *AR = dyn_cast_or_null<SCEVAddRecExpr>(SE.getSCEV(PN));
  BasicBlock *LoopPred = L->getLoopPredecessor();
  if (!AR || AR->getLoop() != L || !LoopPred)
    return;

  const SCEV *StartSCEV = AR->getStart();
  Value *StartValue = nullptr;
  if (auto *C = dyn_cast<SCEVConstant>(StartSCEV)) {
    StartValue = C->getValue();
  } else {
    StartValue = PN->getIncomingValueForBlock(LoopPred);
    assert(SE.getSCEV(StartValue) == StartSCEV && "inconsistent start value");
  }

  DomTreeNode *DTN = DT.getNode(InLoopSucc);
  auto IncUnsigned = SE.getMonotonicPredicateType(AR, CmpInst::ICMP_UGT);
  auto IncSigned = SE.getMonotonicPredicateType(AR, CmpInst::ICMP_SGT);
  bool MonotonicallyIncreasingUnsigned =
      IncUnsigned && *IncUnsigned == ScalarEvolution::MonotonicallyIncreasing;
  bool MonotonicallyIncreasingSigned =
      IncSigned && *IncSigned == ScalarEvolution::MonotonicallyIncreasing;
  // If SCEV guarantees that AR does not wrap, PN >= StartValue can be added
  // unconditionally.
  if (MonotonicallyIncreasingUnsigned)
    WorkList.push_back(
        FactOrCheck::getConditionFact(DTN, CmpInst::ICMP_UGE, PN, StartValue));
  if (MonotonicallyIncreasingSigned)
    WorkList.push_back(
        FactOrCheck::getConditionFact(DTN, CmpInst::ICMP_SGE, PN, StartValue));

  APInt StepOffset;
  if (auto *C = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE)))
    StepOffset = C->getAPInt();
  else
    return;

  // Make sure the bound B is loop-invariant.
  if (!L->isLoopInvariant(B))
    return;

  // Handle negative steps.
  if (StepOffset.isNegative()) {
    // TODO: Extend to allow steps > -1.
    if (!(-StepOffset).isOne())
      return;

    // AR may wrap.
    // Add StartValue >= PN conditional on B <= StartValue which guarantees that
    // the loop exits before wrapping with a step of -1.
    WorkList.push_back(FactOrCheck::getConditionFact(
        DTN, CmpInst::ICMP_UGE, StartValue, PN,
        ConditionTy(CmpInst::ICMP_ULE, B, StartValue)));
    WorkList.push_back(FactOrCheck::getConditionFact(
        DTN, CmpInst::ICMP_SGE, StartValue, PN,
        ConditionTy(CmpInst::ICMP_SLE, B, StartValue)));
    // Add PN > B conditional on B <= StartValue which guarantees that the loop
    // exits when reaching B with a step of -1.
    WorkList.push_back(FactOrCheck::getConditionFact(
        DTN, CmpInst::ICMP_UGT, PN, B,
        ConditionTy(CmpInst::ICMP_ULE, B, StartValue)));
    WorkList.push_back(FactOrCheck::getConditionFact(
        DTN, CmpInst::ICMP_SGT, PN, B,
        ConditionTy(CmpInst::ICMP_SLE, B, StartValue)));
    return;
  }

  // Make sure AR either steps by 1 or that the value we compare against is a
  // GEP based on the same start value and all offsets are a multiple of the
  // step size, to guarantee that the induction will reach the value.
  if (StepOffset.isZero() || StepOffset.isNegative())
    return;

  if (!StepOffset.isOne()) {
    // Check whether B-Start is known to be a multiple of StepOffset.
    const SCEV *BMinusStart = SE.getMinusSCEV(SE.getSCEV(B), StartSCEV);
    if (isa<SCEVCouldNotCompute>(BMinusStart) ||
        !SE.getConstantMultiple(BMinusStart).urem(StepOffset).isZero())
      return;
  }

  // AR may wrap. Add PN >= StartValue conditional on StartValue <= B which
  // guarantees that the loop exits before wrapping in combination with the
  // restrictions on B and the step above.
  if (!MonotonicallyIncreasingUnsigned)
    WorkList.push_back(FactOrCheck::getConditionFact(
        DTN, CmpInst::ICMP_UGE, PN, StartValue,
        ConditionTy(CmpInst::ICMP_ULE, StartValue, B)));
  if (!MonotonicallyIncreasingSigned)
    WorkList.push_back(FactOrCheck::getConditionFact(
        DTN, CmpInst::ICMP_SGE, PN, StartValue,
        ConditionTy(CmpInst::ICMP_SLE, StartValue, B)));

  WorkList.push_back(FactOrCheck::getConditionFact(
      DTN, CmpInst::ICMP_ULT, PN, B,
      ConditionTy(CmpInst::ICMP_ULE, StartValue, B)));
  WorkList.push_back(FactOrCheck::getConditionFact(
      DTN, CmpInst::ICMP_SLT, PN, B,
      ConditionTy(CmpInst::ICMP_SLE, StartValue, B)));

  // Try to add condition from header to the dedicated exit blocks. When exiting
  // either with EQ or NE in the header, we know that the induction value must
  // be u<= B, as other exits may only exit earlier.
  assert(!StepOffset.isNegative() && "induction must be increasing");
  assert((Pred == CmpInst::ICMP_EQ || Pred == CmpInst::ICMP_NE) &&
         "unsupported predicate");
  ConditionTy Precond = {CmpInst::ICMP_ULE, StartValue, B};
  SmallVector<BasicBlock *> ExitBBs;
  L->getExitBlocks(ExitBBs);
  for (BasicBlock *EB : ExitBBs) {
    // Bail out on non-dedicated exits.
    if (DT.dominates(&BB, EB)) {
      WorkList.emplace_back(FactOrCheck::getConditionFact(
          DT.getNode(EB), CmpInst::ICMP_ULE, A, B, Precond));
    }
  }
}

void State::addInfoFor(BasicBlock &BB) {
  addInfoForInductions(BB);

  // True as long as long as the current instruction is guaranteed to execute.
  bool GuaranteedToExecute = true;
  // Queue conditions and assumes.
  for (Instruction &I : BB) {
    if (auto Cmp = dyn_cast<ICmpInst>(&I)) {
      for (Use &U : Cmp->uses()) {
        auto *UserI = getContextInstForUse(U);
        auto *DTN = DT.getNode(UserI->getParent());
        if (!DTN)
          continue;
        WorkList.push_back(FactOrCheck::getCheck(DTN, &U));
      }
      continue;
    }

    auto *II = dyn_cast<IntrinsicInst>(&I);
    Intrinsic::ID ID = II ? II->getIntrinsicID() : Intrinsic::not_intrinsic;
    switch (ID) {
    case Intrinsic::assume: {
      Value *A, *B;
      CmpInst::Predicate Pred;
      if (!match(I.getOperand(0), m_ICmp(Pred, m_Value(A), m_Value(B))))
        break;
      if (GuaranteedToExecute) {
        // The assume is guaranteed to execute when BB is entered, hence Cond
        // holds on entry to BB.
        WorkList.emplace_back(FactOrCheck::getConditionFact(
            DT.getNode(I.getParent()), Pred, A, B));
      } else {
        WorkList.emplace_back(
            FactOrCheck::getInstFact(DT.getNode(I.getParent()), &I));
      }
      break;
    }
    // Enqueue ssub_with_overflow for simplification.
    case Intrinsic::ssub_with_overflow:
    case Intrinsic::ucmp:
    case Intrinsic::scmp:
      WorkList.push_back(
          FactOrCheck::getCheck(DT.getNode(&BB), cast<CallInst>(&I)));
      break;
    // Enqueue the intrinsics to add extra info.
    case Intrinsic::umin:
    case Intrinsic::umax:
    case Intrinsic::smin:
    case Intrinsic::smax:
      // TODO: handle llvm.abs as well
      WorkList.push_back(
          FactOrCheck::getCheck(DT.getNode(&BB), cast<CallInst>(&I)));
      // TODO: Check if it is possible to instead only added the min/max facts
      // when simplifying uses of the min/max intrinsics.
      if (!isGuaranteedNotToBePoison(&I))
        break;
      [[fallthrough]];
    case Intrinsic::abs:
      WorkList.push_back(FactOrCheck::getInstFact(DT.getNode(&BB), &I));
      break;
    }

    GuaranteedToExecute &= isGuaranteedToTransferExecutionToSuccessor(&I);
  }

  if (auto *Switch = dyn_cast<SwitchInst>(BB.getTerminator())) {
    for (auto &Case : Switch->cases()) {
      BasicBlock *Succ = Case.getCaseSuccessor();
      Value *V = Case.getCaseValue();
      if (!canAddSuccessor(BB, Succ))
        continue;
      WorkList.emplace_back(FactOrCheck::getConditionFact(
          DT.getNode(Succ), CmpInst::ICMP_EQ, Switch->getCondition(), V));
    }
    return;
  }

  auto *Br = dyn_cast<BranchInst>(BB.getTerminator());
  if (!Br || !Br->isConditional())
    return;

  Value *Cond = Br->getCondition();

  // If the condition is a chain of ORs/AND and the successor only has the
  // current block as predecessor, queue conditions for the successor.
  Value *Op0, *Op1;
  if (match(Cond, m_LogicalOr(m_Value(Op0), m_Value(Op1))) ||
      match(Cond, m_LogicalAnd(m_Value(Op0), m_Value(Op1)))) {
    bool IsOr = match(Cond, m_LogicalOr());
    bool IsAnd = match(Cond, m_LogicalAnd());
    // If there's a select that matches both AND and OR, we need to commit to
    // one of the options. Arbitrarily pick OR.
    if (IsOr && IsAnd)
      IsAnd = false;

    BasicBlock *Successor = Br->getSuccessor(IsOr ? 1 : 0);
    if (canAddSuccessor(BB, Successor)) {
      SmallVector<Value *> CondWorkList;
      SmallPtrSet<Value *, 8> SeenCond;
      auto QueueValue = [&CondWorkList, &SeenCond](Value *V) {
        if (SeenCond.insert(V).second)
          CondWorkList.push_back(V);
      };
      QueueValue(Op1);
      QueueValue(Op0);
      while (!CondWorkList.empty()) {
        Value *Cur = CondWorkList.pop_back_val();
        if (auto *Cmp = dyn_cast<ICmpInst>(Cur)) {
          WorkList.emplace_back(FactOrCheck::getConditionFact(
              DT.getNode(Successor),
              IsOr ? CmpInst::getInversePredicate(Cmp->getPredicate())
                   : Cmp->getPredicate(),
              Cmp->getOperand(0), Cmp->getOperand(1)));
          continue;
        }
        if (IsOr && match(Cur, m_LogicalOr(m_Value(Op0), m_Value(Op1)))) {
          QueueValue(Op1);
          QueueValue(Op0);
          continue;
        }
        if (IsAnd && match(Cur, m_LogicalAnd(m_Value(Op0), m_Value(Op1)))) {
          QueueValue(Op1);
          QueueValue(Op0);
          continue;
        }
      }
    }
    return;
  }

  auto *CmpI = dyn_cast<ICmpInst>(Br->getCondition());
  if (!CmpI)
    return;
  if (canAddSuccessor(BB, Br->getSuccessor(0)))
    WorkList.emplace_back(FactOrCheck::getConditionFact(
        DT.getNode(Br->getSuccessor(0)), CmpI->getPredicate(),
        CmpI->getOperand(0), CmpI->getOperand(1)));
  if (canAddSuccessor(BB, Br->getSuccessor(1)))
    WorkList.emplace_back(FactOrCheck::getConditionFact(
        DT.getNode(Br->getSuccessor(1)),
        CmpInst::getInversePredicate(CmpI->getPredicate()), CmpI->getOperand(0),
        CmpI->getOperand(1)));
}

#ifndef NDEBUG
static void dumpUnpackedICmp(raw_ostream &OS, ICmpInst::Predicate Pred,
                             Value *LHS, Value *RHS) {
  OS << "icmp " << Pred << ' ';
  LHS->printAsOperand(OS, /*PrintType=*/true);
  OS << ", ";
  RHS->printAsOperand(OS, /*PrintType=*/false);
}
#endif

namespace {
/// Helper to keep track of a condition and if it should be treated as negated
/// for reproducer construction.
/// Pred == Predicate::BAD_ICMP_PREDICATE indicates that this entry is a
/// placeholder to keep the ReproducerCondStack in sync with DFSInStack.
struct ReproducerEntry {
  ICmpInst::Predicate Pred;
  Value *LHS;
  Value *RHS;

  ReproducerEntry(ICmpInst::Predicate Pred, Value *LHS, Value *RHS)
      : Pred(Pred), LHS(LHS), RHS(RHS) {}
};
} // namespace

/// Helper function to generate a reproducer function for simplifying \p Cond.
/// The reproducer function contains a series of @llvm.assume calls, one for
/// each condition in \p Stack. For each condition, the operand instruction are
/// cloned until we reach operands that have an entry in \p Value2Index. Those
/// will then be added as function arguments. \p DT is used to order cloned
/// instructions. The reproducer function will get added to \p M, if it is
/// non-null. Otherwise no reproducer function is generated.
static void generateReproducer(CmpInst *Cond, Module *M,
                               ArrayRef<ReproducerEntry> Stack,
                               ConstraintInfo &Info, DominatorTree &DT) {
  if (!M)
    return;

  LLVMContext &Ctx = Cond->getContext();

  LLVM_DEBUG(dbgs() << "Creating reproducer for " << *Cond << "\n");

  ValueToValueMapTy Old2New;
  SmallVector<Value *> Args;
  SmallPtrSet<Value *, 8> Seen;
  // Traverse Cond and its operands recursively until we reach a value that's in
  // Value2Index or not an instruction, or not a operation that
  // ConstraintElimination can decompose. Such values will be considered as
  // external inputs to the reproducer, they are collected and added as function
  // arguments later.
  auto CollectArguments = [&](ArrayRef<Value *> Ops, bool IsSigned) {
    auto &Value2Index = Info.getValue2Index(IsSigned);
    SmallVector<Value *, 4> WorkList(Ops);
    while (!WorkList.empty()) {
      Value *V = WorkList.pop_back_val();
      if (!Seen.insert(V).second)
        continue;
      if (Old2New.find(V) != Old2New.end())
        continue;
      if (isa<Constant>(V))
        continue;

      auto *I = dyn_cast<Instruction>(V);
      if (Value2Index.contains(V) || !I ||
          !isa<CmpInst, BinaryOperator, GEPOperator, CastInst>(V)) {
        Old2New[V] = V;
        Args.push_back(V);
        LLVM_DEBUG(dbgs() << "  found external input " << *V << "\n");
      } else {
        append_range(WorkList, I->operands());
      }
    }
  };

  for (auto &Entry : Stack)
    if (Entry.Pred != ICmpInst::BAD_ICMP_PREDICATE)
      CollectArguments({Entry.LHS, Entry.RHS}, ICmpInst::isSigned(Entry.Pred));
  CollectArguments(Cond, ICmpInst::isSigned(Cond->getPredicate()));

  SmallVector<Type *> ParamTys;
  for (auto *P : Args)
    ParamTys.push_back(P->getType());

  FunctionType *FTy = FunctionType::get(Cond->getType(), ParamTys,
                                        /*isVarArg=*/false);
  Function *F = Function::Create(FTy, Function::ExternalLinkage,
                                 Cond->getModule()->getName() +
                                     Cond->getFunction()->getName() + "repro",
                                 M);
  // Add arguments to the reproducer function for each external value collected.
  for (unsigned I = 0; I < Args.size(); ++I) {
    F->getArg(I)->setName(Args[I]->getName());
    Old2New[Args[I]] = F->getArg(I);
  }

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> Builder(Entry);
  Builder.CreateRet(Builder.getTrue());
  Builder.SetInsertPoint(Entry->getTerminator());

  // Clone instructions in \p Ops and their operands recursively until reaching
  // an value in Value2Index (external input to the reproducer). Update Old2New
  // mapping for the original and cloned instructions. Sort instructions to
  // clone by dominance, then insert the cloned instructions in the function.
  auto CloneInstructions = [&](ArrayRef<Value *> Ops, bool IsSigned) {
    SmallVector<Value *, 4> WorkList(Ops);
    SmallVector<Instruction *> ToClone;
    auto &Value2Index = Info.getValue2Index(IsSigned);
    while (!WorkList.empty()) {
      Value *V = WorkList.pop_back_val();
      if (Old2New.find(V) != Old2New.end())
        continue;

      auto *I = dyn_cast<Instruction>(V);
      if (!Value2Index.contains(V) && I) {
        Old2New[V] = nullptr;
        ToClone.push_back(I);
        append_range(WorkList, I->operands());
      }
    }

    sort(ToClone,
         [&DT](Instruction *A, Instruction *B) { return DT.dominates(A, B); });
    for (Instruction *I : ToClone) {
      Instruction *Cloned = I->clone();
      Old2New[I] = Cloned;
      Old2New[I]->setName(I->getName());
      Cloned->insertBefore(&*Builder.GetInsertPoint());
      Cloned->dropUnknownNonDebugMetadata();
      Cloned->setDebugLoc({});
    }
  };

  // Materialize the assumptions for the reproducer using the entries in Stack.
  // That is, first clone the operands of the condition recursively until we
  // reach an external input to the reproducer and add them to the reproducer
  // function. Then add an ICmp for the condition (with the inverse predicate if
  // the entry is negated) and an assert using the ICmp.
  for (auto &Entry : Stack) {
    if (Entry.Pred == ICmpInst::BAD_ICMP_PREDICATE)
      continue;

    LLVM_DEBUG(dbgs() << "  Materializing assumption ";
               dumpUnpackedICmp(dbgs(), Entry.Pred, Entry.LHS, Entry.RHS);
               dbgs() << "\n");
    CloneInstructions({Entry.LHS, Entry.RHS}, CmpInst::isSigned(Entry.Pred));

    auto *Cmp = Builder.CreateICmp(Entry.Pred, Entry.LHS, Entry.RHS);
    Builder.CreateAssumption(Cmp);
  }

  // Finally, clone the condition to reproduce and remap instruction operands in
  // the reproducer using Old2New.
  CloneInstructions(Cond, CmpInst::isSigned(Cond->getPredicate()));
  Entry->getTerminator()->setOperand(0, Cond);
  remapInstructionsInBlocks({Entry}, Old2New);

  assert(!verifyFunction(*F, &dbgs()));
}

static std::optional<bool> checkCondition(CmpInst::Predicate Pred, Value *A,
                                          Value *B, Instruction *CheckInst,
                                          ConstraintInfo &Info) {
  LLVM_DEBUG(dbgs() << "Checking " << *CheckInst << "\n");

  auto R = Info.getConstraintForSolving(Pred, A, B);
  if (R.empty() || !R.isValid(Info)){
    LLVM_DEBUG(dbgs() << "   failed to decompose condition\n");
    return std::nullopt;
  }

  auto &CSToUse = Info.getCS(R.IsSigned);

  // If there was extra information collected during decomposition, apply
  // it now and remove it immediately once we are done with reasoning
  // about the constraint.
  for (auto &Row : R.ExtraInfo)
    CSToUse.addVariableRow(Row);
  auto InfoRestorer = make_scope_exit([&]() {
    for (unsigned I = 0; I < R.ExtraInfo.size(); ++I)
      CSToUse.popLastConstraint();
  });

  if (auto ImpliedCondition = R.isImpliedBy(CSToUse)) {
    if (!DebugCounter::shouldExecute(EliminatedCounter))
      return std::nullopt;

    LLVM_DEBUG({
      dbgs() << "Condition ";
      dumpUnpackedICmp(
          dbgs(), *ImpliedCondition ? Pred : CmpInst::getInversePredicate(Pred),
          A, B);
      dbgs() << " implied by dominating constraints\n";
      CSToUse.dump();
    });
    return ImpliedCondition;
  }

  return std::nullopt;
}

static bool checkAndReplaceCondition(
    CmpInst *Cmp, ConstraintInfo &Info, unsigned NumIn, unsigned NumOut,
    Instruction *ContextInst, Module *ReproducerModule,
    ArrayRef<ReproducerEntry> ReproducerCondStack, DominatorTree &DT,
    SmallVectorImpl<Instruction *> &ToRemove) {
  auto ReplaceCmpWithConstant = [&](CmpInst *Cmp, bool IsTrue) {
    generateReproducer(Cmp, ReproducerModule, ReproducerCondStack, Info, DT);
    Constant *ConstantC = ConstantInt::getBool(
        CmpInst::makeCmpResultType(Cmp->getType()), IsTrue);
    Cmp->replaceUsesWithIf(ConstantC, [&DT, NumIn, NumOut,
                                       ContextInst](Use &U) {
      auto *UserI = getContextInstForUse(U);
      auto *DTN = DT.getNode(UserI->getParent());
      if (!DTN || DTN->getDFSNumIn() < NumIn || DTN->getDFSNumOut() > NumOut)
        return false;
      if (UserI->getParent() == ContextInst->getParent() &&
          UserI->comesBefore(ContextInst))
        return false;

      // Conditions in an assume trivially simplify to true. Skip uses
      // in assume calls to not destroy the available information.
      auto *II = dyn_cast<IntrinsicInst>(U.getUser());
      return !II || II->getIntrinsicID() != Intrinsic::assume;
    });
    NumCondsRemoved++;
    if (Cmp->use_empty())
      ToRemove.push_back(Cmp);
    return true;
  };

  if (auto ImpliedCondition =
          checkCondition(Cmp->getPredicate(), Cmp->getOperand(0),
                         Cmp->getOperand(1), Cmp, Info))
    return ReplaceCmpWithConstant(Cmp, *ImpliedCondition);
  return false;
}

static bool checkAndReplaceMinMax(MinMaxIntrinsic *MinMax, ConstraintInfo &Info,
                                  SmallVectorImpl<Instruction *> &ToRemove) {
  auto ReplaceMinMaxWithOperand = [&](MinMaxIntrinsic *MinMax, bool UseLHS) {
    // TODO: generate reproducer for min/max.
    MinMax->replaceAllUsesWith(MinMax->getOperand(UseLHS ? 0 : 1));
    ToRemove.push_back(MinMax);
    return true;
  };

  ICmpInst::Predicate Pred =
      ICmpInst::getNonStrictPredicate(MinMax->getPredicate());
  if (auto ImpliedCondition = checkCondition(
          Pred, MinMax->getOperand(0), MinMax->getOperand(1), MinMax, Info))
    return ReplaceMinMaxWithOperand(MinMax, *ImpliedCondition);
  if (auto ImpliedCondition = checkCondition(
          Pred, MinMax->getOperand(1), MinMax->getOperand(0), MinMax, Info))
    return ReplaceMinMaxWithOperand(MinMax, !*ImpliedCondition);
  return false;
}

static bool checkAndReplaceCmp(CmpIntrinsic *I, ConstraintInfo &Info,
                               SmallVectorImpl<Instruction *> &ToRemove) {
  Value *LHS = I->getOperand(0);
  Value *RHS = I->getOperand(1);
  if (checkCondition(I->getGTPredicate(), LHS, RHS, I, Info).value_or(false)) {
    I->replaceAllUsesWith(ConstantInt::get(I->getType(), 1));
    ToRemove.push_back(I);
    return true;
  }
  if (checkCondition(I->getLTPredicate(), LHS, RHS, I, Info).value_or(false)) {
    I->replaceAllUsesWith(ConstantInt::getSigned(I->getType(), -1));
    ToRemove.push_back(I);
    return true;
  }
  if (checkCondition(ICmpInst::ICMP_EQ, LHS, RHS, I, Info).value_or(false)) {
    I->replaceAllUsesWith(ConstantInt::get(I->getType(), 0));
    ToRemove.push_back(I);
    return true;
  }
  return false;
}

static void
removeEntryFromStack(const StackEntry &E, ConstraintInfo &Info,
                     Module *ReproducerModule,
                     SmallVectorImpl<ReproducerEntry> &ReproducerCondStack,
                     SmallVectorImpl<StackEntry> &DFSInStack) {
  Info.popLastConstraint(E.IsSigned);
  // Remove variables in the system that went out of scope.
  auto &Mapping = Info.getValue2Index(E.IsSigned);
  for (Value *V : E.ValuesToRelease)
    Mapping.erase(V);
  Info.popLastNVariables(E.IsSigned, E.ValuesToRelease.size());
  DFSInStack.pop_back();
  if (ReproducerModule)
    ReproducerCondStack.pop_back();
}

/// Check if either the first condition of an AND or OR is implied by the
/// (negated in case of OR) second condition or vice versa.
static bool checkOrAndOpImpliedByOther(
    FactOrCheck &CB, ConstraintInfo &Info, Module *ReproducerModule,
    SmallVectorImpl<ReproducerEntry> &ReproducerCondStack,
    SmallVectorImpl<StackEntry> &DFSInStack) {

  CmpInst::Predicate Pred;
  Value *A, *B;
  Instruction *JoinOp = CB.getContextInst();
  CmpInst *CmpToCheck = cast<CmpInst>(CB.getInstructionToSimplify());
  unsigned OtherOpIdx = JoinOp->getOperand(0) == CmpToCheck ? 1 : 0;

  // Don't try to simplify the first condition of a select by the second, as
  // this may make the select more poisonous than the original one.
  // TODO: check if the first operand may be poison.
  if (OtherOpIdx != 0 && isa<SelectInst>(JoinOp))
    return false;

  if (!match(JoinOp->getOperand(OtherOpIdx),
             m_ICmp(Pred, m_Value(A), m_Value(B))))
    return false;

  // For OR, check if the negated condition implies CmpToCheck.
  bool IsOr = match(JoinOp, m_LogicalOr());
  if (IsOr)
    Pred = CmpInst::getInversePredicate(Pred);

  // Optimistically add fact from first condition.
  unsigned OldSize = DFSInStack.size();
  Info.addFact(Pred, A, B, CB.NumIn, CB.NumOut, DFSInStack);
  if (OldSize == DFSInStack.size())
    return false;

  bool Changed = false;
  // Check if the second condition can be simplified now.
  if (auto ImpliedCondition =
          checkCondition(CmpToCheck->getPredicate(), CmpToCheck->getOperand(0),
                         CmpToCheck->getOperand(1), CmpToCheck, Info)) {
    if (IsOr && isa<SelectInst>(JoinOp)) {
      JoinOp->setOperand(
          OtherOpIdx == 0 ? 2 : 0,
          ConstantInt::getBool(JoinOp->getType(), *ImpliedCondition));
    } else
      JoinOp->setOperand(
          1 - OtherOpIdx,
          ConstantInt::getBool(JoinOp->getType(), *ImpliedCondition));

    Changed = true;
  }

  // Remove entries again.
  while (OldSize < DFSInStack.size()) {
    StackEntry E = DFSInStack.back();
    removeEntryFromStack(E, Info, ReproducerModule, ReproducerCondStack,
                         DFSInStack);
  }
  return Changed;
}

void ConstraintInfo::addFact(CmpInst::Predicate Pred, Value *A, Value *B,
                             unsigned NumIn, unsigned NumOut,
                             SmallVectorImpl<StackEntry> &DFSInStack) {
  // If the constraint has a pre-condition, skip the constraint if it does not
  // hold.
  SmallVector<Value *> NewVariables;
  auto R = getConstraint(Pred, A, B, NewVariables);

  // TODO: Support non-equality for facts as well.
  if (!R.isValid(*this) || R.isNe())
    return;

  LLVM_DEBUG(dbgs() << "Adding '"; dumpUnpackedICmp(dbgs(), Pred, A, B);
             dbgs() << "'\n");
  bool Added = false;
  auto &CSToUse = getCS(R.IsSigned);
  if (R.Coefficients.empty())
    return;

  Added |= CSToUse.addVariableRowFill(R.Coefficients);

  // If R has been added to the system, add the new variables and queue it for
  // removal once it goes out-of-scope.
  if (Added) {
    SmallVector<Value *, 2> ValuesToRelease;
    auto &Value2Index = getValue2Index(R.IsSigned);
    for (Value *V : NewVariables) {
      Value2Index.insert({V, Value2Index.size() + 1});
      ValuesToRelease.push_back(V);
    }

    LLVM_DEBUG({
      dbgs() << "  constraint: ";
      dumpConstraint(R.Coefficients, getValue2Index(R.IsSigned));
      dbgs() << "\n";
    });

    DFSInStack.emplace_back(NumIn, NumOut, R.IsSigned,
                            std::move(ValuesToRelease));

    if (!R.IsSigned) {
      for (Value *V : NewVariables) {
        ConstraintTy VarPos(SmallVector<int64_t, 8>(Value2Index.size() + 1, 0),
                            false, false, false);
        VarPos.Coefficients[Value2Index[V]] = -1;
        CSToUse.addVariableRow(VarPos.Coefficients);
        DFSInStack.emplace_back(NumIn, NumOut, R.IsSigned,
                                SmallVector<Value *, 2>());
      }
    }

    if (R.isEq()) {
      // Also add the inverted constraint for equality constraints.
      for (auto &Coeff : R.Coefficients)
        Coeff *= -1;
      CSToUse.addVariableRowFill(R.Coefficients);

      DFSInStack.emplace_back(NumIn, NumOut, R.IsSigned,
                              SmallVector<Value *, 2>());
    }
  }
}

static bool replaceSubOverflowUses(IntrinsicInst *II, Value *A, Value *B,
                                   SmallVectorImpl<Instruction *> &ToRemove) {
  bool Changed = false;
  IRBuilder<> Builder(II->getParent(), II->getIterator());
  Value *Sub = nullptr;
  for (User *U : make_early_inc_range(II->users())) {
    if (match(U, m_ExtractValue<0>(m_Value()))) {
      if (!Sub)
        Sub = Builder.CreateSub(A, B);
      U->replaceAllUsesWith(Sub);
      Changed = true;
    } else if (match(U, m_ExtractValue<1>(m_Value()))) {
      U->replaceAllUsesWith(Builder.getFalse());
      Changed = true;
    } else
      continue;

    if (U->use_empty()) {
      auto *I = cast<Instruction>(U);
      ToRemove.push_back(I);
      I->setOperand(0, PoisonValue::get(II->getType()));
      Changed = true;
    }
  }

  if (II->use_empty()) {
    II->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

static bool
tryToSimplifyOverflowMath(IntrinsicInst *II, ConstraintInfo &Info,
                          SmallVectorImpl<Instruction *> &ToRemove) {
  auto DoesConditionHold = [](CmpInst::Predicate Pred, Value *A, Value *B,
                              ConstraintInfo &Info) {
    auto R = Info.getConstraintForSolving(Pred, A, B);
    if (R.size() < 2 || !R.isValid(Info))
      return false;

    auto &CSToUse = Info.getCS(R.IsSigned);
    return CSToUse.isConditionImplied(R.Coefficients);
  };

  bool Changed = false;
  if (II->getIntrinsicID() == Intrinsic::ssub_with_overflow) {
    // If A s>= B && B s>= 0, ssub.with.overflow(a, b) should not overflow and
    // can be simplified to a regular sub.
    Value *A = II->getArgOperand(0);
    Value *B = II->getArgOperand(1);
    if (!DoesConditionHold(CmpInst::ICMP_SGE, A, B, Info) ||
        !DoesConditionHold(CmpInst::ICMP_SGE, B,
                           ConstantInt::get(A->getType(), 0), Info))
      return false;
    Changed = replaceSubOverflowUses(II, A, B, ToRemove);
  }
  return Changed;
}

static bool eliminateConstraints(Function &F, DominatorTree &DT, LoopInfo &LI,
                                 ScalarEvolution &SE,
                                 OptimizationRemarkEmitter &ORE) {
  bool Changed = false;
  DT.updateDFSNumbers();
  SmallVector<Value *> FunctionArgs;
  for (Value &Arg : F.args())
    FunctionArgs.push_back(&Arg);
  ConstraintInfo Info(F.getDataLayout(), FunctionArgs);
  State S(DT, LI, SE);
  std::unique_ptr<Module> ReproducerModule(
      DumpReproducers ? new Module(F.getName(), F.getContext()) : nullptr);

  // First, collect conditions implied by branches and blocks with their
  // Dominator DFS in and out numbers.
  for (BasicBlock &BB : F) {
    if (!DT.getNode(&BB))
      continue;
    S.addInfoFor(BB);
  }

  // Next, sort worklist by dominance, so that dominating conditions to check
  // and facts come before conditions and facts dominated by them. If a
  // condition to check and a fact have the same numbers, conditional facts come
  // first. Assume facts and checks are ordered according to their relative
  // order in the containing basic block. Also make sure conditions with
  // constant operands come before conditions without constant operands. This
  // increases the effectiveness of the current signed <-> unsigned fact
  // transfer logic.
  stable_sort(S.WorkList, [](const FactOrCheck &A, const FactOrCheck &B) {
    auto HasNoConstOp = [](const FactOrCheck &B) {
      Value *V0 = B.isConditionFact() ? B.Cond.Op0 : B.Inst->getOperand(0);
      Value *V1 = B.isConditionFact() ? B.Cond.Op1 : B.Inst->getOperand(1);
      return !isa<ConstantInt>(V0) && !isa<ConstantInt>(V1);
    };
    // If both entries have the same In numbers, conditional facts come first.
    // Otherwise use the relative order in the basic block.
    if (A.NumIn == B.NumIn) {
      if (A.isConditionFact() && B.isConditionFact()) {
        bool NoConstOpA = HasNoConstOp(A);
        bool NoConstOpB = HasNoConstOp(B);
        return NoConstOpA < NoConstOpB;
      }
      if (A.isConditionFact())
        return true;
      if (B.isConditionFact())
        return false;
      auto *InstA = A.getContextInst();
      auto *InstB = B.getContextInst();
      return InstA->comesBefore(InstB);
    }
    return A.NumIn < B.NumIn;
  });

  SmallVector<Instruction *> ToRemove;

  // Finally, process ordered worklist and eliminate implied conditions.
  SmallVector<StackEntry, 16> DFSInStack;
  SmallVector<ReproducerEntry> ReproducerCondStack;
  for (FactOrCheck &CB : S.WorkList) {
    // First, pop entries from the stack that are out-of-scope for CB. Remove
    // the corresponding entry from the constraint system.
    while (!DFSInStack.empty()) {
      auto &E = DFSInStack.back();
      LLVM_DEBUG(dbgs() << "Top of stack : " << E.NumIn << " " << E.NumOut
                        << "\n");
      LLVM_DEBUG(dbgs() << "CB: " << CB.NumIn << " " << CB.NumOut << "\n");
      assert(E.NumIn <= CB.NumIn);
      if (CB.NumOut <= E.NumOut)
        break;
      LLVM_DEBUG({
        dbgs() << "Removing ";
        dumpConstraint(Info.getCS(E.IsSigned).getLastConstraint(),
                       Info.getValue2Index(E.IsSigned));
        dbgs() << "\n";
      });
      removeEntryFromStack(E, Info, ReproducerModule.get(), ReproducerCondStack,
                           DFSInStack);
    }

    // For a block, check if any CmpInsts become known based on the current set
    // of constraints.
    if (CB.isCheck()) {
      Instruction *Inst = CB.getInstructionToSimplify();
      if (!Inst)
        continue;
      LLVM_DEBUG(dbgs() << "Processing condition to simplify: " << *Inst
                        << "\n");
      if (auto *II = dyn_cast<WithOverflowInst>(Inst)) {
        Changed |= tryToSimplifyOverflowMath(II, Info, ToRemove);
      } else if (auto *Cmp = dyn_cast<ICmpInst>(Inst)) {
        bool Simplified = checkAndReplaceCondition(
            Cmp, Info, CB.NumIn, CB.NumOut, CB.getContextInst(),
            ReproducerModule.get(), ReproducerCondStack, S.DT, ToRemove);
        if (!Simplified &&
            match(CB.getContextInst(), m_LogicalOp(m_Value(), m_Value()))) {
          Simplified =
              checkOrAndOpImpliedByOther(CB, Info, ReproducerModule.get(),
                                         ReproducerCondStack, DFSInStack);
        }
        Changed |= Simplified;
      } else if (auto *MinMax = dyn_cast<MinMaxIntrinsic>(Inst)) {
        Changed |= checkAndReplaceMinMax(MinMax, Info, ToRemove);
      } else if (auto *CmpIntr = dyn_cast<CmpIntrinsic>(Inst)) {
        Changed |= checkAndReplaceCmp(CmpIntr, Info, ToRemove);
      }
      continue;
    }

    auto AddFact = [&](CmpInst::Predicate Pred, Value *A, Value *B) {
      LLVM_DEBUG(dbgs() << "Processing fact to add to the system: ";
                 dumpUnpackedICmp(dbgs(), Pred, A, B); dbgs() << "\n");
      if (Info.getCS(CmpInst::isSigned(Pred)).size() > MaxRows) {
        LLVM_DEBUG(
            dbgs()
            << "Skip adding constraint because system has too many rows.\n");
        return;
      }

      Info.addFact(Pred, A, B, CB.NumIn, CB.NumOut, DFSInStack);
      if (ReproducerModule && DFSInStack.size() > ReproducerCondStack.size())
        ReproducerCondStack.emplace_back(Pred, A, B);

      Info.transferToOtherSystem(Pred, A, B, CB.NumIn, CB.NumOut, DFSInStack);
      if (ReproducerModule && DFSInStack.size() > ReproducerCondStack.size()) {
        // Add dummy entries to ReproducerCondStack to keep it in sync with
        // DFSInStack.
        for (unsigned I = 0,
                      E = (DFSInStack.size() - ReproducerCondStack.size());
             I < E; ++I) {
          ReproducerCondStack.emplace_back(ICmpInst::BAD_ICMP_PREDICATE,
                                           nullptr, nullptr);
        }
      }
    };

    ICmpInst::Predicate Pred;
    if (!CB.isConditionFact()) {
      Value *X;
      if (match(CB.Inst, m_Intrinsic<Intrinsic::abs>(m_Value(X)))) {
        // If is_int_min_poison is true then we may assume llvm.abs >= 0.
        if (cast<ConstantInt>(CB.Inst->getOperand(1))->isOne())
          AddFact(CmpInst::ICMP_SGE, CB.Inst,
                  ConstantInt::get(CB.Inst->getType(), 0));
        AddFact(CmpInst::ICMP_SGE, CB.Inst, X);
        continue;
      }

      if (auto *MinMax = dyn_cast<MinMaxIntrinsic>(CB.Inst)) {
        Pred = ICmpInst::getNonStrictPredicate(MinMax->getPredicate());
        AddFact(Pred, MinMax, MinMax->getLHS());
        AddFact(Pred, MinMax, MinMax->getRHS());
        continue;
      }
    }

    Value *A = nullptr, *B = nullptr;
    if (CB.isConditionFact()) {
      Pred = CB.Cond.Pred;
      A = CB.Cond.Op0;
      B = CB.Cond.Op1;
      if (CB.DoesHold.Pred != CmpInst::BAD_ICMP_PREDICATE &&
          !Info.doesHold(CB.DoesHold.Pred, CB.DoesHold.Op0, CB.DoesHold.Op1)) {
        LLVM_DEBUG({
          dbgs() << "Not adding fact ";
          dumpUnpackedICmp(dbgs(), Pred, A, B);
          dbgs() << " because precondition ";
          dumpUnpackedICmp(dbgs(), CB.DoesHold.Pred, CB.DoesHold.Op0,
                           CB.DoesHold.Op1);
          dbgs() << " does not hold.\n";
        });
        continue;
      }
    } else {
      bool Matched = match(CB.Inst, m_Intrinsic<Intrinsic::assume>(
                                        m_ICmp(Pred, m_Value(A), m_Value(B))));
      (void)Matched;
      assert(Matched && "Must have an assume intrinsic with a icmp operand");
    }
    AddFact(Pred, A, B);
  }

  if (ReproducerModule && !ReproducerModule->functions().empty()) {
    std::string S;
    raw_string_ostream StringS(S);
    ReproducerModule->print(StringS, nullptr);
    StringS.flush();
    OptimizationRemark Rem(DEBUG_TYPE, "Reproducer", &F);
    Rem << ore::NV("module") << S;
    ORE.emit(Rem);
  }

#ifndef NDEBUG
  unsigned SignedEntries =
      count_if(DFSInStack, [](const StackEntry &E) { return E.IsSigned; });
  assert(Info.getCS(false).size() - FunctionArgs.size() ==
             DFSInStack.size() - SignedEntries &&
         "updates to CS and DFSInStack are out of sync");
  assert(Info.getCS(true).size() == SignedEntries &&
         "updates to CS and DFSInStack are out of sync");
#endif

  for (Instruction *I : ToRemove)
    I->eraseFromParent();
  return Changed;
}

PreservedAnalyses ConstraintEliminationPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  if (!eliminateConstraints(F, DT, LI, SE, ORE))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<LoopAnalysis>();
  PA.preserve<ScalarEvolutionAnalysis>();
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
