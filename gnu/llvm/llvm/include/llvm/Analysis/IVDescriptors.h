//===- llvm/Analysis/IVDescriptors.h - IndVar Descriptors -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file "describes" induction and recurrence variables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_IVDESCRIPTORS_H
#define LLVM_ANALYSIS_IVDESCRIPTORS_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {

class AssumptionCache;
class DemandedBits;
class DominatorTree;
class Instruction;
class Loop;
class PredicatedScalarEvolution;
class ScalarEvolution;
class SCEV;
class StoreInst;

/// These are the kinds of recurrences that we support.
enum class RecurKind {
  None,     ///< Not a recurrence.
  Add,      ///< Sum of integers.
  Mul,      ///< Product of integers.
  Or,       ///< Bitwise or logical OR of integers.
  And,      ///< Bitwise or logical AND of integers.
  Xor,      ///< Bitwise or logical XOR of integers.
  SMin,     ///< Signed integer min implemented in terms of select(cmp()).
  SMax,     ///< Signed integer max implemented in terms of select(cmp()).
  UMin,     ///< Unsigned integer min implemented in terms of select(cmp()).
  UMax,     ///< Unsigned integer max implemented in terms of select(cmp()).
  FAdd,     ///< Sum of floats.
  FMul,     ///< Product of floats.
  FMin,     ///< FP min implemented in terms of select(cmp()).
  FMax,     ///< FP max implemented in terms of select(cmp()).
  FMinimum, ///< FP min with llvm.minimum semantics
  FMaximum, ///< FP max with llvm.maximum semantics
  FMulAdd,  ///< Sum of float products with llvm.fmuladd(a * b + sum).
  IAnyOf,   ///< Any_of reduction with select(icmp(),x,y) where one of (x,y) is
            ///< loop invariant, and both x and y are integer type.
  FAnyOf    ///< Any_of reduction with select(fcmp(),x,y) where one of (x,y) is
            ///< loop invariant, and both x and y are integer type.
  // TODO: Any_of reduction need not be restricted to integer type only.
};

/// The RecurrenceDescriptor is used to identify recurrences variables in a
/// loop. Reduction is a special case of recurrence that has uses of the
/// recurrence variable outside the loop. The method isReductionPHI identifies
/// reductions that are basic recurrences.
///
/// Basic recurrences are defined as the summation, product, OR, AND, XOR, min,
/// or max of a set of terms. For example: for(i=0; i<n; i++) { total +=
/// array[i]; } is a summation of array elements. Basic recurrences are a
/// special case of chains of recurrences (CR). See ScalarEvolution for CR
/// references.

/// This struct holds information about recurrence variables.
class RecurrenceDescriptor {
public:
  RecurrenceDescriptor() = default;

  RecurrenceDescriptor(Value *Start, Instruction *Exit, StoreInst *Store,
                       RecurKind K, FastMathFlags FMF, Instruction *ExactFP,
                       Type *RT, bool Signed, bool Ordered,
                       SmallPtrSetImpl<Instruction *> &CI,
                       unsigned MinWidthCastToRecurTy)
      : IntermediateStore(Store), StartValue(Start), LoopExitInstr(Exit),
        Kind(K), FMF(FMF), ExactFPMathInst(ExactFP), RecurrenceType(RT),
        IsSigned(Signed), IsOrdered(Ordered),
        MinWidthCastToRecurrenceType(MinWidthCastToRecurTy) {
    CastInsts.insert(CI.begin(), CI.end());
  }

  /// This POD struct holds information about a potential recurrence operation.
  class InstDesc {
  public:
    InstDesc(bool IsRecur, Instruction *I, Instruction *ExactFP = nullptr)
        : IsRecurrence(IsRecur), PatternLastInst(I),
          RecKind(RecurKind::None), ExactFPMathInst(ExactFP) {}

    InstDesc(Instruction *I, RecurKind K, Instruction *ExactFP = nullptr)
        : IsRecurrence(true), PatternLastInst(I), RecKind(K),
          ExactFPMathInst(ExactFP) {}

    bool isRecurrence() const { return IsRecurrence; }

    bool needsExactFPMath() const { return ExactFPMathInst != nullptr; }

    Instruction *getExactFPMathInst() const { return ExactFPMathInst; }

    RecurKind getRecKind() const { return RecKind; }

    Instruction *getPatternInst() const { return PatternLastInst; }

  private:
    // Is this instruction a recurrence candidate.
    bool IsRecurrence;
    // The last instruction in a min/max pattern (select of the select(icmp())
    // pattern), or the current recurrence instruction otherwise.
    Instruction *PatternLastInst;
    // If this is a min/max pattern.
    RecurKind RecKind;
    // Recurrence does not allow floating-point reassociation.
    Instruction *ExactFPMathInst;
  };

  /// Returns a struct describing if the instruction 'I' can be a recurrence
  /// variable of type 'Kind' for a Loop \p L and reduction PHI \p Phi.
  /// If the recurrence is a min/max pattern of select(icmp()) this function
  /// advances the instruction pointer 'I' from the compare instruction to the
  /// select instruction and stores this pointer in 'PatternLastInst' member of
  /// the returned struct.
  static InstDesc isRecurrenceInstr(Loop *L, PHINode *Phi, Instruction *I,
                                    RecurKind Kind, InstDesc &Prev,
                                    FastMathFlags FuncFMF);

  /// Returns true if instruction I has multiple uses in Insts
  static bool hasMultipleUsesOf(Instruction *I,
                                SmallPtrSetImpl<Instruction *> &Insts,
                                unsigned MaxNumUses);

  /// Returns true if all uses of the instruction I is within the Set.
  static bool areAllUsesIn(Instruction *I, SmallPtrSetImpl<Instruction *> &Set);

  /// Returns a struct describing if the instruction is a llvm.(s/u)(min/max),
  /// llvm.minnum/maxnum or a Select(ICmp(X, Y), X, Y) pair of instructions
  /// corresponding to a min(X, Y) or max(X, Y), matching the recurrence kind \p
  /// Kind. \p Prev specifies the description of an already processed select
  /// instruction, so its corresponding cmp can be matched to it.
  static InstDesc isMinMaxPattern(Instruction *I, RecurKind Kind,
                                  const InstDesc &Prev);

  /// Returns a struct describing whether the instruction is either a
  ///   Select(ICmp(A, B), X, Y), or
  ///   Select(FCmp(A, B), X, Y)
  /// where one of (X, Y) is a loop invariant integer and the other is a PHI
  /// value. \p Prev specifies the description of an already processed select
  /// instruction, so its corresponding cmp can be matched to it.
  static InstDesc isAnyOfPattern(Loop *Loop, PHINode *OrigPhi, Instruction *I,
                                 InstDesc &Prev);

  /// Returns a struct describing if the instruction is a
  /// Select(FCmp(X, Y), (Z = X op PHINode), PHINode) instruction pattern.
  static InstDesc isConditionalRdxPattern(RecurKind Kind, Instruction *I);

  /// Returns identity corresponding to the RecurrenceKind.
  Value *getRecurrenceIdentity(RecurKind K, Type *Tp, FastMathFlags FMF) const;

  /// Returns the opcode corresponding to the RecurrenceKind.
  static unsigned getOpcode(RecurKind Kind);

  /// Returns true if Phi is a reduction of type Kind and adds it to the
  /// RecurrenceDescriptor. If either \p DB is non-null or \p AC and \p DT are
  /// non-null, the minimal bit width needed to compute the reduction will be
  /// computed.
  static bool
  AddReductionVar(PHINode *Phi, RecurKind Kind, Loop *TheLoop,
                  FastMathFlags FuncFMF, RecurrenceDescriptor &RedDes,
                  DemandedBits *DB = nullptr, AssumptionCache *AC = nullptr,
                  DominatorTree *DT = nullptr, ScalarEvolution *SE = nullptr);

  /// Returns true if Phi is a reduction in TheLoop. The RecurrenceDescriptor
  /// is returned in RedDes. If either \p DB is non-null or \p AC and \p DT are
  /// non-null, the minimal bit width needed to compute the reduction will be
  /// computed. If \p SE is non-null, store instructions to loop invariant
  /// addresses are processed.
  static bool
  isReductionPHI(PHINode *Phi, Loop *TheLoop, RecurrenceDescriptor &RedDes,
                 DemandedBits *DB = nullptr, AssumptionCache *AC = nullptr,
                 DominatorTree *DT = nullptr, ScalarEvolution *SE = nullptr);

  /// Returns true if Phi is a fixed-order recurrence. A fixed-order recurrence
  /// is a non-reduction recurrence relation in which the value of the
  /// recurrence in the current loop iteration equals a value defined in a
  /// previous iteration (e.g. if the value is defined in the previous
  /// iteration, we refer to it as first-order recurrence, if it is defined in
  /// the iteration before the previous, we refer to it as second-order
  /// recurrence and so on). Note that this function optimistically assumes that
  /// uses of the recurrence can be re-ordered if necessary and users need to
  /// check and perform the re-ordering.
  static bool isFixedOrderRecurrence(PHINode *Phi, Loop *TheLoop,
                                     DominatorTree *DT);

  RecurKind getRecurrenceKind() const { return Kind; }

  unsigned getOpcode() const { return getOpcode(getRecurrenceKind()); }

  FastMathFlags getFastMathFlags() const { return FMF; }

  TrackingVH<Value> getRecurrenceStartValue() const { return StartValue; }

  Instruction *getLoopExitInstr() const { return LoopExitInstr; }

  /// Returns true if the recurrence has floating-point math that requires
  /// precise (ordered) operations.
  bool hasExactFPMath() const { return ExactFPMathInst != nullptr; }

  /// Returns 1st non-reassociative FP instruction in the PHI node's use-chain.
  Instruction *getExactFPMathInst() const { return ExactFPMathInst; }

  /// Returns true if the recurrence kind is an integer kind.
  static bool isIntegerRecurrenceKind(RecurKind Kind);

  /// Returns true if the recurrence kind is a floating point kind.
  static bool isFloatingPointRecurrenceKind(RecurKind Kind);

  /// Returns true if the recurrence kind is an integer min/max kind.
  static bool isIntMinMaxRecurrenceKind(RecurKind Kind) {
    return Kind == RecurKind::UMin || Kind == RecurKind::UMax ||
           Kind == RecurKind::SMin || Kind == RecurKind::SMax;
  }

  /// Returns true if the recurrence kind is a floating-point min/max kind.
  static bool isFPMinMaxRecurrenceKind(RecurKind Kind) {
    return Kind == RecurKind::FMin || Kind == RecurKind::FMax ||
           Kind == RecurKind::FMinimum || Kind == RecurKind::FMaximum;
  }

  /// Returns true if the recurrence kind is any min/max kind.
  static bool isMinMaxRecurrenceKind(RecurKind Kind) {
    return isIntMinMaxRecurrenceKind(Kind) || isFPMinMaxRecurrenceKind(Kind);
  }

  /// Returns true if the recurrence kind is of the form
  ///   select(cmp(),x,y) where one of (x,y) is loop invariant.
  static bool isAnyOfRecurrenceKind(RecurKind Kind) {
    return Kind == RecurKind::IAnyOf || Kind == RecurKind::FAnyOf;
  }

  /// Returns the type of the recurrence. This type can be narrower than the
  /// actual type of the Phi if the recurrence has been type-promoted.
  Type *getRecurrenceType() const { return RecurrenceType; }

  /// Returns a reference to the instructions used for type-promoting the
  /// recurrence.
  const SmallPtrSet<Instruction *, 8> &getCastInsts() const { return CastInsts; }

  /// Returns the minimum width used by the recurrence in bits.
  unsigned getMinWidthCastToRecurrenceTypeInBits() const {
    return MinWidthCastToRecurrenceType;
  }

  /// Returns true if all source operands of the recurrence are SExtInsts.
  bool isSigned() const { return IsSigned; }

  /// Expose an ordered FP reduction to the instance users.
  bool isOrdered() const { return IsOrdered; }

  /// Attempts to find a chain of operations from Phi to LoopExitInst that can
  /// be treated as a set of reductions instructions for in-loop reductions.
  SmallVector<Instruction *, 4> getReductionOpChain(PHINode *Phi,
                                                    Loop *L) const;

  /// Returns true if the instruction is a call to the llvm.fmuladd intrinsic.
  static bool isFMulAddIntrinsic(Instruction *I) {
    return isa<IntrinsicInst>(I) &&
           cast<IntrinsicInst>(I)->getIntrinsicID() == Intrinsic::fmuladd;
  }

  /// Reductions may store temporary or final result to an invariant address.
  /// If there is such a store in the loop then, after successfull run of
  /// AddReductionVar method, this field will be assigned the last met store.
  StoreInst *IntermediateStore = nullptr;

private:
  // The starting value of the recurrence.
  // It does not have to be zero!
  TrackingVH<Value> StartValue;
  // The instruction who's value is used outside the loop.
  Instruction *LoopExitInstr = nullptr;
  // The kind of the recurrence.
  RecurKind Kind = RecurKind::None;
  // The fast-math flags on the recurrent instructions.  We propagate these
  // fast-math flags into the vectorized FP instructions we generate.
  FastMathFlags FMF;
  // First instance of non-reassociative floating-point in the PHI's use-chain.
  Instruction *ExactFPMathInst = nullptr;
  // The type of the recurrence.
  Type *RecurrenceType = nullptr;
  // True if all source operands of the recurrence are SExtInsts.
  bool IsSigned = false;
  // True if this recurrence can be treated as an in-order reduction.
  // Currently only a non-reassociative FAdd can be considered in-order,
  // if it is also the only FAdd in the PHI's use chain.
  bool IsOrdered = false;
  // Instructions used for type-promoting the recurrence.
  SmallPtrSet<Instruction *, 8> CastInsts;
  // The minimum width used by the recurrence.
  unsigned MinWidthCastToRecurrenceType;
};

/// A struct for saving information about induction variables.
class InductionDescriptor {
public:
  /// This enum represents the kinds of inductions that we support.
  enum InductionKind {
    IK_NoInduction,  ///< Not an induction variable.
    IK_IntInduction, ///< Integer induction variable. Step = C.
    IK_PtrInduction, ///< Pointer induction var. Step = C.
    IK_FpInduction   ///< Floating point induction variable.
  };

public:
  /// Default constructor - creates an invalid induction.
  InductionDescriptor() = default;

  Value *getStartValue() const { return StartValue; }
  InductionKind getKind() const { return IK; }
  const SCEV *getStep() const { return Step; }
  BinaryOperator *getInductionBinOp() const { return InductionBinOp; }
  ConstantInt *getConstIntStepValue() const;

  /// Returns true if \p Phi is an induction in the loop \p L. If \p Phi is an
  /// induction, the induction descriptor \p D will contain the data describing
  /// this induction. Since Induction Phis can only be present inside loop
  /// headers, the function will assert if it is passed a Phi whose parent is
  /// not the loop header. If by some other means the caller has a better SCEV
  /// expression for \p Phi than the one returned by the ScalarEvolution
  /// analysis, it can be passed through \p Expr. If the def-use chain
  /// associated with the phi includes casts (that we know we can ignore
  /// under proper runtime checks), they are passed through \p CastsToIgnore.
  static bool
  isInductionPHI(PHINode *Phi, const Loop *L, ScalarEvolution *SE,
                 InductionDescriptor &D, const SCEV *Expr = nullptr,
                 SmallVectorImpl<Instruction *> *CastsToIgnore = nullptr);

  /// Returns true if \p Phi is a floating point induction in the loop \p L.
  /// If \p Phi is an induction, the induction descriptor \p D will contain
  /// the data describing this induction.
  static bool isFPInductionPHI(PHINode *Phi, const Loop *L, ScalarEvolution *SE,
                               InductionDescriptor &D);

  /// Returns true if \p Phi is a loop \p L induction, in the context associated
  /// with the run-time predicate of PSE. If \p Assume is true, this can add
  /// further SCEV predicates to \p PSE in order to prove that \p Phi is an
  /// induction.
  /// If \p Phi is an induction, \p D will contain the data describing this
  /// induction.
  static bool isInductionPHI(PHINode *Phi, const Loop *L,
                             PredicatedScalarEvolution &PSE,
                             InductionDescriptor &D, bool Assume = false);

  /// Returns floating-point induction operator that does not allow
  /// reassociation (transforming the induction requires an override of normal
  /// floating-point rules).
  Instruction *getExactFPMathInst() {
    if (IK == IK_FpInduction && InductionBinOp &&
        !InductionBinOp->hasAllowReassoc())
      return InductionBinOp;
    return nullptr;
  }

  /// Returns binary opcode of the induction operator.
  Instruction::BinaryOps getInductionOpcode() const {
    return InductionBinOp ? InductionBinOp->getOpcode()
                          : Instruction::BinaryOpsEnd;
  }

  /// Returns a reference to the type cast instructions in the induction
  /// update chain, that are redundant when guarded with a runtime
  /// SCEV overflow check.
  const SmallVectorImpl<Instruction *> &getCastInsts() const {
    return RedundantCasts;
  }

private:
  /// Private constructor - used by \c isInductionPHI.
  InductionDescriptor(Value *Start, InductionKind K, const SCEV *Step,
                      BinaryOperator *InductionBinOp = nullptr,
                      SmallVectorImpl<Instruction *> *Casts = nullptr);

  /// Start value.
  TrackingVH<Value> StartValue;
  /// Induction kind.
  InductionKind IK = IK_NoInduction;
  /// Step value.
  const SCEV *Step = nullptr;
  // Instruction that advances induction variable.
  BinaryOperator *InductionBinOp = nullptr;
  // Instructions used for type-casts of the induction variable,
  // that are redundant when guarded with a runtime SCEV overflow check.
  SmallVector<Instruction *, 2> RedundantCasts;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_IVDESCRIPTORS_H
