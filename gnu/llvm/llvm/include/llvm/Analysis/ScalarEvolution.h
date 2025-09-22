//===- llvm/Analysis/ScalarEvolution.h - Scalar Evolution -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The ScalarEvolution class is an LLVM pass which can be used to analyze and
// categorize scalar expressions in loops.  It specializes in recognizing
// general induction variables, representing them with the abstract and opaque
// SCEV class.  Given this analysis, trip counts of loops and other important
// properties can be obtained.
//
// This analysis is primarily useful for induction variable substitution and
// strength reduction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTION_H
#define LLVM_ANALYSIS_SCALAREVOLUTION_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace llvm {

class OverflowingBinaryOperator;
class AssumptionCache;
class BasicBlock;
class Constant;
class ConstantInt;
class DataLayout;
class DominatorTree;
class Function;
class GEPOperator;
class Instruction;
class LLVMContext;
class Loop;
class LoopInfo;
class raw_ostream;
class ScalarEvolution;
class SCEVAddRecExpr;
class SCEVUnknown;
class StructType;
class TargetLibraryInfo;
class Type;
class Value;
enum SCEVTypes : unsigned short;

extern bool VerifySCEV;

/// This class represents an analyzed expression in the program.  These are
/// opaque objects that the client is not allowed to do much with directly.
///
class SCEV : public FoldingSetNode {
  friend struct FoldingSetTrait<SCEV>;

  /// A reference to an Interned FoldingSetNodeID for this node.  The
  /// ScalarEvolution's BumpPtrAllocator holds the data.
  FoldingSetNodeIDRef FastID;

  // The SCEV baseclass this node corresponds to
  const SCEVTypes SCEVType;

protected:
  // Estimated complexity of this node's expression tree size.
  const unsigned short ExpressionSize;

  /// This field is initialized to zero and may be used in subclasses to store
  /// miscellaneous information.
  unsigned short SubclassData = 0;

public:
  /// NoWrapFlags are bitfield indices into SubclassData.
  ///
  /// Add and Mul expressions may have no-unsigned-wrap <NUW> or
  /// no-signed-wrap <NSW> properties, which are derived from the IR
  /// operator. NSW is a misnomer that we use to mean no signed overflow or
  /// underflow.
  ///
  /// AddRec expressions may have a no-self-wraparound <NW> property if, in
  /// the integer domain, abs(step) * max-iteration(loop) <=
  /// unsigned-max(bitwidth).  This means that the recurrence will never reach
  /// its start value if the step is non-zero.  Computing the same value on
  /// each iteration is not considered wrapping, and recurrences with step = 0
  /// are trivially <NW>.  <NW> is independent of the sign of step and the
  /// value the add recurrence starts with.
  ///
  /// Note that NUW and NSW are also valid properties of a recurrence, and
  /// either implies NW. For convenience, NW will be set for a recurrence
  /// whenever either NUW or NSW are set.
  ///
  /// We require that the flag on a SCEV apply to the entire scope in which
  /// that SCEV is defined.  A SCEV's scope is set of locations dominated by
  /// a defining location, which is in turn described by the following rules:
  /// * A SCEVUnknown is at the point of definition of the Value.
  /// * A SCEVConstant is defined at all points.
  /// * A SCEVAddRec is defined starting with the header of the associated
  ///   loop.
  /// * All other SCEVs are defined at the earlest point all operands are
  ///   defined.
  ///
  /// The above rules describe a maximally hoisted form (without regards to
  /// potential control dependence).  A SCEV is defined anywhere a
  /// corresponding instruction could be defined in said maximally hoisted
  /// form.  Note that SCEVUDivExpr (currently the only expression type which
  /// can trap) can be defined per these rules in regions where it would trap
  /// at runtime.  A SCEV being defined does not require the existence of any
  /// instruction within the defined scope.
  enum NoWrapFlags {
    FlagAnyWrap = 0,    // No guarantee.
    FlagNW = (1 << 0),  // No self-wrap.
    FlagNUW = (1 << 1), // No unsigned wrap.
    FlagNSW = (1 << 2), // No signed wrap.
    NoWrapMask = (1 << 3) - 1
  };

  explicit SCEV(const FoldingSetNodeIDRef ID, SCEVTypes SCEVTy,
                unsigned short ExpressionSize)
      : FastID(ID), SCEVType(SCEVTy), ExpressionSize(ExpressionSize) {}
  SCEV(const SCEV &) = delete;
  SCEV &operator=(const SCEV &) = delete;

  SCEVTypes getSCEVType() const { return SCEVType; }

  /// Return the LLVM type of this SCEV expression.
  Type *getType() const;

  /// Return operands of this SCEV expression.
  ArrayRef<const SCEV *> operands() const;

  /// Return true if the expression is a constant zero.
  bool isZero() const;

  /// Return true if the expression is a constant one.
  bool isOne() const;

  /// Return true if the expression is a constant all-ones value.
  bool isAllOnesValue() const;

  /// Return true if the specified scev is negated, but not a constant.
  bool isNonConstantNegative() const;

  // Returns estimated size of the mathematical expression represented by this
  // SCEV. The rules of its calculation are following:
  // 1) Size of a SCEV without operands (like constants and SCEVUnknown) is 1;
  // 2) Size SCEV with operands Op1, Op2, ..., OpN is calculated by formula:
  //    (1 + Size(Op1) + ... + Size(OpN)).
  // This value gives us an estimation of time we need to traverse through this
  // SCEV and all its operands recursively. We may use it to avoid performing
  // heavy transformations on SCEVs of excessive size for sake of saving the
  // compilation time.
  unsigned short getExpressionSize() const {
    return ExpressionSize;
  }

  /// Print out the internal representation of this scalar to the specified
  /// stream.  This should really only be used for debugging purposes.
  void print(raw_ostream &OS) const;

  /// This method is used for debugging.
  void dump() const;
};

// Specialize FoldingSetTrait for SCEV to avoid needing to compute
// temporary FoldingSetNodeID values.
template <> struct FoldingSetTrait<SCEV> : DefaultFoldingSetTrait<SCEV> {
  static void Profile(const SCEV &X, FoldingSetNodeID &ID) { ID = X.FastID; }

  static bool Equals(const SCEV &X, const FoldingSetNodeID &ID, unsigned IDHash,
                     FoldingSetNodeID &TempID) {
    return ID == X.FastID;
  }

  static unsigned ComputeHash(const SCEV &X, FoldingSetNodeID &TempID) {
    return X.FastID.ComputeHash();
  }
};

inline raw_ostream &operator<<(raw_ostream &OS, const SCEV &S) {
  S.print(OS);
  return OS;
}

/// An object of this class is returned by queries that could not be answered.
/// For example, if you ask for the number of iterations of a linked-list
/// traversal loop, you will get one of these.  None of the standard SCEV
/// operations are valid on this class, it is just a marker.
struct SCEVCouldNotCompute : public SCEV {
  SCEVCouldNotCompute();

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S);
};

/// This class represents an assumption made using SCEV expressions which can
/// be checked at run-time.
class SCEVPredicate : public FoldingSetNode {
  friend struct FoldingSetTrait<SCEVPredicate>;

  /// A reference to an Interned FoldingSetNodeID for this node.  The
  /// ScalarEvolution's BumpPtrAllocator holds the data.
  FoldingSetNodeIDRef FastID;

public:
  enum SCEVPredicateKind { P_Union, P_Compare, P_Wrap };

protected:
  SCEVPredicateKind Kind;
  ~SCEVPredicate() = default;
  SCEVPredicate(const SCEVPredicate &) = default;
  SCEVPredicate &operator=(const SCEVPredicate &) = default;

public:
  SCEVPredicate(const FoldingSetNodeIDRef ID, SCEVPredicateKind Kind);

  SCEVPredicateKind getKind() const { return Kind; }

  /// Returns the estimated complexity of this predicate.  This is roughly
  /// measured in the number of run-time checks required.
  virtual unsigned getComplexity() const { return 1; }

  /// Returns true if the predicate is always true. This means that no
  /// assumptions were made and nothing needs to be checked at run-time.
  virtual bool isAlwaysTrue() const = 0;

  /// Returns true if this predicate implies \p N.
  virtual bool implies(const SCEVPredicate *N) const = 0;

  /// Prints a textual representation of this predicate with an indentation of
  /// \p Depth.
  virtual void print(raw_ostream &OS, unsigned Depth = 0) const = 0;
};

inline raw_ostream &operator<<(raw_ostream &OS, const SCEVPredicate &P) {
  P.print(OS);
  return OS;
}

// Specialize FoldingSetTrait for SCEVPredicate to avoid needing to compute
// temporary FoldingSetNodeID values.
template <>
struct FoldingSetTrait<SCEVPredicate> : DefaultFoldingSetTrait<SCEVPredicate> {
  static void Profile(const SCEVPredicate &X, FoldingSetNodeID &ID) {
    ID = X.FastID;
  }

  static bool Equals(const SCEVPredicate &X, const FoldingSetNodeID &ID,
                     unsigned IDHash, FoldingSetNodeID &TempID) {
    return ID == X.FastID;
  }

  static unsigned ComputeHash(const SCEVPredicate &X,
                              FoldingSetNodeID &TempID) {
    return X.FastID.ComputeHash();
  }
};

/// This class represents an assumption that the expression LHS Pred RHS
/// evaluates to true, and this can be checked at run-time.
class SCEVComparePredicate final : public SCEVPredicate {
  /// We assume that LHS Pred RHS is true.
  const ICmpInst::Predicate Pred;
  const SCEV *LHS;
  const SCEV *RHS;

public:
  SCEVComparePredicate(const FoldingSetNodeIDRef ID,
                       const ICmpInst::Predicate Pred,
                       const SCEV *LHS, const SCEV *RHS);

  /// Implementation of the SCEVPredicate interface
  bool implies(const SCEVPredicate *N) const override;
  void print(raw_ostream &OS, unsigned Depth = 0) const override;
  bool isAlwaysTrue() const override;

  ICmpInst::Predicate getPredicate() const { return Pred; }

  /// Returns the left hand side of the predicate.
  const SCEV *getLHS() const { return LHS; }

  /// Returns the right hand side of the predicate.
  const SCEV *getRHS() const { return RHS; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEVPredicate *P) {
    return P->getKind() == P_Compare;
  }
};

/// This class represents an assumption made on an AddRec expression. Given an
/// affine AddRec expression {a,+,b}, we assume that it has the nssw or nusw
/// flags (defined below) in the first X iterations of the loop, where X is a
/// SCEV expression returned by getPredicatedBackedgeTakenCount).
///
/// Note that this does not imply that X is equal to the backedge taken
/// count. This means that if we have a nusw predicate for i32 {0,+,1} with a
/// predicated backedge taken count of X, we only guarantee that {0,+,1} has
/// nusw in the first X iterations. {0,+,1} may still wrap in the loop if we
/// have more than X iterations.
class SCEVWrapPredicate final : public SCEVPredicate {
public:
  /// Similar to SCEV::NoWrapFlags, but with slightly different semantics
  /// for FlagNUSW. The increment is considered to be signed, and a + b
  /// (where b is the increment) is considered to wrap if:
  ///    zext(a + b) != zext(a) + sext(b)
  ///
  /// If Signed is a function that takes an n-bit tuple and maps to the
  /// integer domain as the tuples value interpreted as twos complement,
  /// and Unsigned a function that takes an n-bit tuple and maps to the
  /// integer domain as the base two value of input tuple, then a + b
  /// has IncrementNUSW iff:
  ///
  /// 0 <= Unsigned(a) + Signed(b) < 2^n
  ///
  /// The IncrementNSSW flag has identical semantics with SCEV::FlagNSW.
  ///
  /// Note that the IncrementNUSW flag is not commutative: if base + inc
  /// has IncrementNUSW, then inc + base doesn't neccessarily have this
  /// property. The reason for this is that this is used for sign/zero
  /// extending affine AddRec SCEV expressions when a SCEVWrapPredicate is
  /// assumed. A {base,+,inc} expression is already non-commutative with
  /// regards to base and inc, since it is interpreted as:
  ///     (((base + inc) + inc) + inc) ...
  enum IncrementWrapFlags {
    IncrementAnyWrap = 0,     // No guarantee.
    IncrementNUSW = (1 << 0), // No unsigned with signed increment wrap.
    IncrementNSSW = (1 << 1), // No signed with signed increment wrap
                              // (equivalent with SCEV::NSW)
    IncrementNoWrapMask = (1 << 2) - 1
  };

  /// Convenient IncrementWrapFlags manipulation methods.
  [[nodiscard]] static SCEVWrapPredicate::IncrementWrapFlags
  clearFlags(SCEVWrapPredicate::IncrementWrapFlags Flags,
             SCEVWrapPredicate::IncrementWrapFlags OffFlags) {
    assert((Flags & IncrementNoWrapMask) == Flags && "Invalid flags value!");
    assert((OffFlags & IncrementNoWrapMask) == OffFlags &&
           "Invalid flags value!");
    return (SCEVWrapPredicate::IncrementWrapFlags)(Flags & ~OffFlags);
  }

  [[nodiscard]] static SCEVWrapPredicate::IncrementWrapFlags
  maskFlags(SCEVWrapPredicate::IncrementWrapFlags Flags, int Mask) {
    assert((Flags & IncrementNoWrapMask) == Flags && "Invalid flags value!");
    assert((Mask & IncrementNoWrapMask) == Mask && "Invalid mask value!");

    return (SCEVWrapPredicate::IncrementWrapFlags)(Flags & Mask);
  }

  [[nodiscard]] static SCEVWrapPredicate::IncrementWrapFlags
  setFlags(SCEVWrapPredicate::IncrementWrapFlags Flags,
           SCEVWrapPredicate::IncrementWrapFlags OnFlags) {
    assert((Flags & IncrementNoWrapMask) == Flags && "Invalid flags value!");
    assert((OnFlags & IncrementNoWrapMask) == OnFlags &&
           "Invalid flags value!");

    return (SCEVWrapPredicate::IncrementWrapFlags)(Flags | OnFlags);
  }

  /// Returns the set of SCEVWrapPredicate no wrap flags implied by a
  /// SCEVAddRecExpr.
  [[nodiscard]] static SCEVWrapPredicate::IncrementWrapFlags
  getImpliedFlags(const SCEVAddRecExpr *AR, ScalarEvolution &SE);

private:
  const SCEVAddRecExpr *AR;
  IncrementWrapFlags Flags;

public:
  explicit SCEVWrapPredicate(const FoldingSetNodeIDRef ID,
                             const SCEVAddRecExpr *AR,
                             IncrementWrapFlags Flags);

  /// Returns the set assumed no overflow flags.
  IncrementWrapFlags getFlags() const { return Flags; }

  /// Implementation of the SCEVPredicate interface
  const SCEVAddRecExpr *getExpr() const;
  bool implies(const SCEVPredicate *N) const override;
  void print(raw_ostream &OS, unsigned Depth = 0) const override;
  bool isAlwaysTrue() const override;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEVPredicate *P) {
    return P->getKind() == P_Wrap;
  }
};

/// This class represents a composition of other SCEV predicates, and is the
/// class that most clients will interact with.  This is equivalent to a
/// logical "AND" of all the predicates in the union.
///
/// NB! Unlike other SCEVPredicate sub-classes this class does not live in the
/// ScalarEvolution::Preds folding set.  This is why the \c add function is sound.
class SCEVUnionPredicate final : public SCEVPredicate {
private:
  using PredicateMap =
      DenseMap<const SCEV *, SmallVector<const SCEVPredicate *, 4>>;

  /// Vector with references to all predicates in this union.
  SmallVector<const SCEVPredicate *, 16> Preds;

  /// Adds a predicate to this union.
  void add(const SCEVPredicate *N);

public:
  SCEVUnionPredicate(ArrayRef<const SCEVPredicate *> Preds);

  const SmallVectorImpl<const SCEVPredicate *> &getPredicates() const {
    return Preds;
  }

  /// Implementation of the SCEVPredicate interface
  bool isAlwaysTrue() const override;
  bool implies(const SCEVPredicate *N) const override;
  void print(raw_ostream &OS, unsigned Depth) const override;

  /// We estimate the complexity of a union predicate as the size number of
  /// predicates in the union.
  unsigned getComplexity() const override { return Preds.size(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEVPredicate *P) {
    return P->getKind() == P_Union;
  }
};

/// The main scalar evolution driver. Because client code (intentionally)
/// can't do much with the SCEV objects directly, they must ask this class
/// for services.
class ScalarEvolution {
  friend class ScalarEvolutionsTest;

public:
  /// An enum describing the relationship between a SCEV and a loop.
  enum LoopDisposition {
    LoopVariant,   ///< The SCEV is loop-variant (unknown).
    LoopInvariant, ///< The SCEV is loop-invariant.
    LoopComputable ///< The SCEV varies predictably with the loop.
  };

  /// An enum describing the relationship between a SCEV and a basic block.
  enum BlockDisposition {
    DoesNotDominateBlock,  ///< The SCEV does not dominate the block.
    DominatesBlock,        ///< The SCEV dominates the block.
    ProperlyDominatesBlock ///< The SCEV properly dominates the block.
  };

  /// Convenient NoWrapFlags manipulation that hides enum casts and is
  /// visible in the ScalarEvolution name space.
  [[nodiscard]] static SCEV::NoWrapFlags maskFlags(SCEV::NoWrapFlags Flags,
                                                   int Mask) {
    return (SCEV::NoWrapFlags)(Flags & Mask);
  }
  [[nodiscard]] static SCEV::NoWrapFlags setFlags(SCEV::NoWrapFlags Flags,
                                                  SCEV::NoWrapFlags OnFlags) {
    return (SCEV::NoWrapFlags)(Flags | OnFlags);
  }
  [[nodiscard]] static SCEV::NoWrapFlags
  clearFlags(SCEV::NoWrapFlags Flags, SCEV::NoWrapFlags OffFlags) {
    return (SCEV::NoWrapFlags)(Flags & ~OffFlags);
  }
  [[nodiscard]] static bool hasFlags(SCEV::NoWrapFlags Flags,
                                     SCEV::NoWrapFlags TestFlags) {
    return TestFlags == maskFlags(Flags, TestFlags);
  };

  ScalarEvolution(Function &F, TargetLibraryInfo &TLI, AssumptionCache &AC,
                  DominatorTree &DT, LoopInfo &LI);
  ScalarEvolution(ScalarEvolution &&Arg);
  ~ScalarEvolution();

  LLVMContext &getContext() const { return F.getContext(); }

  /// Test if values of the given type are analyzable within the SCEV
  /// framework. This primarily includes integer types, and it can optionally
  /// include pointer types if the ScalarEvolution class has access to
  /// target-specific information.
  bool isSCEVable(Type *Ty) const;

  /// Return the size in bits of the specified type, for which isSCEVable must
  /// return true.
  uint64_t getTypeSizeInBits(Type *Ty) const;

  /// Return a type with the same bitwidth as the given type and which
  /// represents how SCEV will treat the given type, for which isSCEVable must
  /// return true. For pointer types, this is the pointer-sized integer type.
  Type *getEffectiveSCEVType(Type *Ty) const;

  // Returns a wider type among {Ty1, Ty2}.
  Type *getWiderType(Type *Ty1, Type *Ty2) const;

  /// Return true if there exists a point in the program at which both
  /// A and B could be operands to the same instruction.
  /// SCEV expressions are generally assumed to correspond to instructions
  /// which could exists in IR.  In general, this requires that there exists
  /// a use point in the program where all operands dominate the use.
  ///
  /// Example:
  /// loop {
  ///   if
  ///     loop { v1 = load @global1; }
  ///   else
  ///     loop { v2 = load @global2; }
  /// }
  /// No SCEV with operand V1, and v2 can exist in this program.
  bool instructionCouldExistWithOperands(const SCEV *A, const SCEV *B);

  /// Return true if the SCEV is a scAddRecExpr or it contains
  /// scAddRecExpr. The result will be cached in HasRecMap.
  bool containsAddRecurrence(const SCEV *S);

  /// Is operation \p BinOp between \p LHS and \p RHS provably does not have
  /// a signed/unsigned overflow (\p Signed)? If \p CtxI is specified, the
  /// no-overflow fact should be true in the context of this instruction.
  bool willNotOverflow(Instruction::BinaryOps BinOp, bool Signed,
                       const SCEV *LHS, const SCEV *RHS,
                       const Instruction *CtxI = nullptr);

  /// Parse NSW/NUW flags from add/sub/mul IR binary operation \p Op into
  /// SCEV no-wrap flags, and deduce flag[s] that aren't known yet.
  /// Does not mutate the original instruction. Returns std::nullopt if it could
  /// not deduce more precise flags than the instruction already has, otherwise
  /// returns proven flags.
  std::optional<SCEV::NoWrapFlags>
  getStrengthenedNoWrapFlagsFromBinOp(const OverflowingBinaryOperator *OBO);

  /// Notify this ScalarEvolution that \p User directly uses SCEVs in \p Ops.
  void registerUser(const SCEV *User, ArrayRef<const SCEV *> Ops);

  /// Return true if the SCEV expression contains an undef value.
  bool containsUndefs(const SCEV *S) const;

  /// Return true if the SCEV expression contains a Value that has been
  /// optimised out and is now a nullptr.
  bool containsErasedValue(const SCEV *S) const;

  /// Return a SCEV expression for the full generality of the specified
  /// expression.
  const SCEV *getSCEV(Value *V);

  /// Return an existing SCEV for V if there is one, otherwise return nullptr.
  const SCEV *getExistingSCEV(Value *V);

  const SCEV *getConstant(ConstantInt *V);
  const SCEV *getConstant(const APInt &Val);
  const SCEV *getConstant(Type *Ty, uint64_t V, bool isSigned = false);
  const SCEV *getLosslessPtrToIntExpr(const SCEV *Op, unsigned Depth = 0);
  const SCEV *getPtrToIntExpr(const SCEV *Op, Type *Ty);
  const SCEV *getTruncateExpr(const SCEV *Op, Type *Ty, unsigned Depth = 0);
  const SCEV *getVScale(Type *Ty);
  const SCEV *getElementCount(Type *Ty, ElementCount EC);
  const SCEV *getZeroExtendExpr(const SCEV *Op, Type *Ty, unsigned Depth = 0);
  const SCEV *getZeroExtendExprImpl(const SCEV *Op, Type *Ty,
                                    unsigned Depth = 0);
  const SCEV *getSignExtendExpr(const SCEV *Op, Type *Ty, unsigned Depth = 0);
  const SCEV *getSignExtendExprImpl(const SCEV *Op, Type *Ty,
                                    unsigned Depth = 0);
  const SCEV *getCastExpr(SCEVTypes Kind, const SCEV *Op, Type *Ty);
  const SCEV *getAnyExtendExpr(const SCEV *Op, Type *Ty);
  const SCEV *getAddExpr(SmallVectorImpl<const SCEV *> &Ops,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0);
  const SCEV *getAddExpr(const SCEV *LHS, const SCEV *RHS,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0) {
    SmallVector<const SCEV *, 2> Ops = {LHS, RHS};
    return getAddExpr(Ops, Flags, Depth);
  }
  const SCEV *getAddExpr(const SCEV *Op0, const SCEV *Op1, const SCEV *Op2,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0) {
    SmallVector<const SCEV *, 3> Ops = {Op0, Op1, Op2};
    return getAddExpr(Ops, Flags, Depth);
  }
  const SCEV *getMulExpr(SmallVectorImpl<const SCEV *> &Ops,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0);
  const SCEV *getMulExpr(const SCEV *LHS, const SCEV *RHS,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0) {
    SmallVector<const SCEV *, 2> Ops = {LHS, RHS};
    return getMulExpr(Ops, Flags, Depth);
  }
  const SCEV *getMulExpr(const SCEV *Op0, const SCEV *Op1, const SCEV *Op2,
                         SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                         unsigned Depth = 0) {
    SmallVector<const SCEV *, 3> Ops = {Op0, Op1, Op2};
    return getMulExpr(Ops, Flags, Depth);
  }
  const SCEV *getUDivExpr(const SCEV *LHS, const SCEV *RHS);
  const SCEV *getUDivExactExpr(const SCEV *LHS, const SCEV *RHS);
  const SCEV *getURemExpr(const SCEV *LHS, const SCEV *RHS);
  const SCEV *getAddRecExpr(const SCEV *Start, const SCEV *Step, const Loop *L,
                            SCEV::NoWrapFlags Flags);
  const SCEV *getAddRecExpr(SmallVectorImpl<const SCEV *> &Operands,
                            const Loop *L, SCEV::NoWrapFlags Flags);
  const SCEV *getAddRecExpr(const SmallVectorImpl<const SCEV *> &Operands,
                            const Loop *L, SCEV::NoWrapFlags Flags) {
    SmallVector<const SCEV *, 4> NewOp(Operands.begin(), Operands.end());
    return getAddRecExpr(NewOp, L, Flags);
  }

  /// Checks if \p SymbolicPHI can be rewritten as an AddRecExpr under some
  /// Predicates. If successful return these <AddRecExpr, Predicates>;
  /// The function is intended to be called from PSCEV (the caller will decide
  /// whether to actually add the predicates and carry out the rewrites).
  std::optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
  createAddRecFromPHIWithCasts(const SCEVUnknown *SymbolicPHI);

  /// Returns an expression for a GEP
  ///
  /// \p GEP The GEP. The indices contained in the GEP itself are ignored,
  /// instead we use IndexExprs.
  /// \p IndexExprs The expressions for the indices.
  const SCEV *getGEPExpr(GEPOperator *GEP,
                         const SmallVectorImpl<const SCEV *> &IndexExprs);
  const SCEV *getAbsExpr(const SCEV *Op, bool IsNSW);
  const SCEV *getMinMaxExpr(SCEVTypes Kind,
                            SmallVectorImpl<const SCEV *> &Operands);
  const SCEV *getSequentialMinMaxExpr(SCEVTypes Kind,
                                      SmallVectorImpl<const SCEV *> &Operands);
  const SCEV *getSMaxExpr(const SCEV *LHS, const SCEV *RHS);
  const SCEV *getSMaxExpr(SmallVectorImpl<const SCEV *> &Operands);
  const SCEV *getUMaxExpr(const SCEV *LHS, const SCEV *RHS);
  const SCEV *getUMaxExpr(SmallVectorImpl<const SCEV *> &Operands);
  const SCEV *getSMinExpr(const SCEV *LHS, const SCEV *RHS);
  const SCEV *getSMinExpr(SmallVectorImpl<const SCEV *> &Operands);
  const SCEV *getUMinExpr(const SCEV *LHS, const SCEV *RHS,
                          bool Sequential = false);
  const SCEV *getUMinExpr(SmallVectorImpl<const SCEV *> &Operands,
                          bool Sequential = false);
  const SCEV *getUnknown(Value *V);
  const SCEV *getCouldNotCompute();

  /// Return a SCEV for the constant 0 of a specific type.
  const SCEV *getZero(Type *Ty) { return getConstant(Ty, 0); }

  /// Return a SCEV for the constant 1 of a specific type.
  const SCEV *getOne(Type *Ty) { return getConstant(Ty, 1); }

  /// Return a SCEV for the constant \p Power of two.
  const SCEV *getPowerOfTwo(Type *Ty, unsigned Power) {
    assert(Power < getTypeSizeInBits(Ty) && "Power out of range");
    return getConstant(APInt::getOneBitSet(getTypeSizeInBits(Ty), Power));
  }

  /// Return a SCEV for the constant -1 of a specific type.
  const SCEV *getMinusOne(Type *Ty) {
    return getConstant(Ty, -1, /*isSigned=*/true);
  }

  /// Return an expression for a TypeSize.
  const SCEV *getSizeOfExpr(Type *IntTy, TypeSize Size);

  /// Return an expression for the alloc size of AllocTy that is type IntTy
  const SCEV *getSizeOfExpr(Type *IntTy, Type *AllocTy);

  /// Return an expression for the store size of StoreTy that is type IntTy
  const SCEV *getStoreSizeOfExpr(Type *IntTy, Type *StoreTy);

  /// Return an expression for offsetof on the given field with type IntTy
  const SCEV *getOffsetOfExpr(Type *IntTy, StructType *STy, unsigned FieldNo);

  /// Return the SCEV object corresponding to -V.
  const SCEV *getNegativeSCEV(const SCEV *V,
                              SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap);

  /// Return the SCEV object corresponding to ~V.
  const SCEV *getNotSCEV(const SCEV *V);

  /// Return LHS-RHS.  Minus is represented in SCEV as A+B*-1.
  ///
  /// If the LHS and RHS are pointers which don't share a common base
  /// (according to getPointerBase()), this returns a SCEVCouldNotCompute.
  /// To compute the difference between two unrelated pointers, you can
  /// explicitly convert the arguments using getPtrToIntExpr(), for pointer
  /// types that support it.
  const SCEV *getMinusSCEV(const SCEV *LHS, const SCEV *RHS,
                           SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap,
                           unsigned Depth = 0);

  /// Compute ceil(N / D). N and D are treated as unsigned values.
  ///
  /// Since SCEV doesn't have native ceiling division, this generates a
  /// SCEV expression of the following form:
  ///
  /// umin(N, 1) + floor((N - umin(N, 1)) / D)
  ///
  /// A denominator of zero or poison is handled the same way as getUDivExpr().
  const SCEV *getUDivCeilSCEV(const SCEV *N, const SCEV *D);

  /// Return a SCEV corresponding to a conversion of the input value to the
  /// specified type.  If the type must be extended, it is zero extended.
  const SCEV *getTruncateOrZeroExtend(const SCEV *V, Type *Ty,
                                      unsigned Depth = 0);

  /// Return a SCEV corresponding to a conversion of the input value to the
  /// specified type.  If the type must be extended, it is sign extended.
  const SCEV *getTruncateOrSignExtend(const SCEV *V, Type *Ty,
                                      unsigned Depth = 0);

  /// Return a SCEV corresponding to a conversion of the input value to the
  /// specified type.  If the type must be extended, it is zero extended.  The
  /// conversion must not be narrowing.
  const SCEV *getNoopOrZeroExtend(const SCEV *V, Type *Ty);

  /// Return a SCEV corresponding to a conversion of the input value to the
  /// specified type.  If the type must be extended, it is sign extended.  The
  /// conversion must not be narrowing.
  const SCEV *getNoopOrSignExtend(const SCEV *V, Type *Ty);

  /// Return a SCEV corresponding to a conversion of the input value to the
  /// specified type. If the type must be extended, it is extended with
  /// unspecified bits. The conversion must not be narrowing.
  const SCEV *getNoopOrAnyExtend(const SCEV *V, Type *Ty);

  /// Return a SCEV corresponding to a conversion of the input value to the
  /// specified type.  The conversion must not be widening.
  const SCEV *getTruncateOrNoop(const SCEV *V, Type *Ty);

  /// Promote the operands to the wider of the types using zero-extension, and
  /// then perform a umax operation with them.
  const SCEV *getUMaxFromMismatchedTypes(const SCEV *LHS, const SCEV *RHS);

  /// Promote the operands to the wider of the types using zero-extension, and
  /// then perform a umin operation with them.
  const SCEV *getUMinFromMismatchedTypes(const SCEV *LHS, const SCEV *RHS,
                                         bool Sequential = false);

  /// Promote the operands to the wider of the types using zero-extension, and
  /// then perform a umin operation with them. N-ary function.
  const SCEV *getUMinFromMismatchedTypes(SmallVectorImpl<const SCEV *> &Ops,
                                         bool Sequential = false);

  /// Transitively follow the chain of pointer-type operands until reaching a
  /// SCEV that does not have a single pointer operand. This returns a
  /// SCEVUnknown pointer for well-formed pointer-type expressions, but corner
  /// cases do exist.
  const SCEV *getPointerBase(const SCEV *V);

  /// Compute an expression equivalent to S - getPointerBase(S).
  const SCEV *removePointerBase(const SCEV *S);

  /// Return a SCEV expression for the specified value at the specified scope
  /// in the program.  The L value specifies a loop nest to evaluate the
  /// expression at, where null is the top-level or a specified loop is
  /// immediately inside of the loop.
  ///
  /// This method can be used to compute the exit value for a variable defined
  /// in a loop by querying what the value will hold in the parent loop.
  ///
  /// In the case that a relevant loop exit value cannot be computed, the
  /// original value V is returned.
  const SCEV *getSCEVAtScope(const SCEV *S, const Loop *L);

  /// This is a convenience function which does getSCEVAtScope(getSCEV(V), L).
  const SCEV *getSCEVAtScope(Value *V, const Loop *L);

  /// Test whether entry to the loop is protected by a conditional between LHS
  /// and RHS.  This is used to help avoid max expressions in loop trip
  /// counts, and to eliminate casts.
  bool isLoopEntryGuardedByCond(const Loop *L, ICmpInst::Predicate Pred,
                                const SCEV *LHS, const SCEV *RHS);

  /// Test whether entry to the basic block is protected by a conditional
  /// between LHS and RHS.
  bool isBasicBlockEntryGuardedByCond(const BasicBlock *BB,
                                      ICmpInst::Predicate Pred, const SCEV *LHS,
                                      const SCEV *RHS);

  /// Test whether the backedge of the loop is protected by a conditional
  /// between LHS and RHS.  This is used to eliminate casts.
  bool isLoopBackedgeGuardedByCond(const Loop *L, ICmpInst::Predicate Pred,
                                   const SCEV *LHS, const SCEV *RHS);

  /// A version of getTripCountFromExitCount below which always picks an
  /// evaluation type which can not result in overflow.
  const SCEV *getTripCountFromExitCount(const SCEV *ExitCount);

  /// Convert from an "exit count" (i.e. "backedge taken count") to a "trip
  /// count".  A "trip count" is the number of times the header of the loop
  /// will execute if an exit is taken after the specified number of backedges
  /// have been taken.  (e.g. TripCount = ExitCount + 1).  Note that the
  /// expression can overflow if ExitCount = UINT_MAX.  If EvalTy is not wide
  /// enough to hold the result without overflow, result unsigned wraps with
  /// 2s-complement semantics.  ex: EC = 255 (i8), TC = 0 (i8)
  const SCEV *getTripCountFromExitCount(const SCEV *ExitCount, Type *EvalTy,
                                        const Loop *L);

  /// Returns the exact trip count of the loop if we can compute it, and
  /// the result is a small constant.  '0' is used to represent an unknown
  /// or non-constant trip count.  Note that a trip count is simply one more
  /// than the backedge taken count for the loop.
  unsigned getSmallConstantTripCount(const Loop *L);

  /// Return the exact trip count for this loop if we exit through ExitingBlock.
  /// '0' is used to represent an unknown or non-constant trip count.  Note
  /// that a trip count is simply one more than the backedge taken count for
  /// the same exit.
  /// This "trip count" assumes that control exits via ExitingBlock. More
  /// precisely, it is the number of times that control will reach ExitingBlock
  /// before taking the branch. For loops with multiple exits, it may not be
  /// the number times that the loop header executes if the loop exits
  /// prematurely via another branch.
  unsigned getSmallConstantTripCount(const Loop *L,
                                     const BasicBlock *ExitingBlock);

  /// Returns the upper bound of the loop trip count as a normal unsigned
  /// value.
  /// Returns 0 if the trip count is unknown or not constant.
  unsigned getSmallConstantMaxTripCount(const Loop *L);

  /// Returns the largest constant divisor of the trip count as a normal
  /// unsigned value, if possible. This means that the actual trip count is
  /// always a multiple of the returned value. Returns 1 if the trip count is
  /// unknown or not guaranteed to be the multiple of a constant., Will also
  /// return 1 if the trip count is very large (>= 2^32).
  /// Note that the argument is an exit count for loop L, NOT a trip count.
  unsigned getSmallConstantTripMultiple(const Loop *L,
                                        const SCEV *ExitCount);

  /// Returns the largest constant divisor of the trip count of the
  /// loop.  Will return 1 if no trip count could be computed, or if a
  /// divisor could not be found.
  unsigned getSmallConstantTripMultiple(const Loop *L);

  /// Returns the largest constant divisor of the trip count of this loop as a
  /// normal unsigned value, if possible. This means that the actual trip
  /// count is always a multiple of the returned value (don't forget the trip
  /// count could very well be zero as well!). As explained in the comments
  /// for getSmallConstantTripCount, this assumes that control exits the loop
  /// via ExitingBlock.
  unsigned getSmallConstantTripMultiple(const Loop *L,
                                        const BasicBlock *ExitingBlock);

  /// The terms "backedge taken count" and "exit count" are used
  /// interchangeably to refer to the number of times the backedge of a loop
  /// has executed before the loop is exited.
  enum ExitCountKind {
    /// An expression exactly describing the number of times the backedge has
    /// executed when a loop is exited.
    Exact,
    /// A constant which provides an upper bound on the exact trip count.
    ConstantMaximum,
    /// An expression which provides an upper bound on the exact trip count.
    SymbolicMaximum,
  };

  /// Return the number of times the backedge executes before the given exit
  /// would be taken; if not exactly computable, return SCEVCouldNotCompute.
  /// For a single exit loop, this value is equivelent to the result of
  /// getBackedgeTakenCount.  The loop is guaranteed to exit (via *some* exit)
  /// before the backedge is executed (ExitCount + 1) times.  Note that there
  /// is no guarantee about *which* exit is taken on the exiting iteration.
  const SCEV *getExitCount(const Loop *L, const BasicBlock *ExitingBlock,
                           ExitCountKind Kind = Exact);

  /// If the specified loop has a predictable backedge-taken count, return it,
  /// otherwise return a SCEVCouldNotCompute object. The backedge-taken count is
  /// the number of times the loop header will be branched to from within the
  /// loop, assuming there are no abnormal exists like exception throws. This is
  /// one less than the trip count of the loop, since it doesn't count the first
  /// iteration, when the header is branched to from outside the loop.
  ///
  /// Note that it is not valid to call this method on a loop without a
  /// loop-invariant backedge-taken count (see
  /// hasLoopInvariantBackedgeTakenCount).
  const SCEV *getBackedgeTakenCount(const Loop *L, ExitCountKind Kind = Exact);

  /// Similar to getBackedgeTakenCount, except it will add a set of
  /// SCEV predicates to Predicates that are required to be true in order for
  /// the answer to be correct. Predicates can be checked with run-time
  /// checks and can be used to perform loop versioning.
  const SCEV *getPredicatedBackedgeTakenCount(const Loop *L,
                                              SmallVector<const SCEVPredicate *, 4> &Predicates);

  /// When successful, this returns a SCEVConstant that is greater than or equal
  /// to (i.e. a "conservative over-approximation") of the value returend by
  /// getBackedgeTakenCount.  If such a value cannot be computed, it returns the
  /// SCEVCouldNotCompute object.
  const SCEV *getConstantMaxBackedgeTakenCount(const Loop *L) {
    return getBackedgeTakenCount(L, ConstantMaximum);
  }

  /// When successful, this returns a SCEV that is greater than or equal
  /// to (i.e. a "conservative over-approximation") of the value returend by
  /// getBackedgeTakenCount.  If such a value cannot be computed, it returns the
  /// SCEVCouldNotCompute object.
  const SCEV *getSymbolicMaxBackedgeTakenCount(const Loop *L) {
    return getBackedgeTakenCount(L, SymbolicMaximum);
  }

  /// Similar to getSymbolicMaxBackedgeTakenCount, except it will add a set of
  /// SCEV predicates to Predicates that are required to be true in order for
  /// the answer to be correct. Predicates can be checked with run-time
  /// checks and can be used to perform loop versioning.
  const SCEV *getPredicatedSymbolicMaxBackedgeTakenCount(
      const Loop *L, SmallVector<const SCEVPredicate *, 4> &Predicates);

  /// Return true if the backedge taken count is either the value returned by
  /// getConstantMaxBackedgeTakenCount or zero.
  bool isBackedgeTakenCountMaxOrZero(const Loop *L);

  /// Return true if the specified loop has an analyzable loop-invariant
  /// backedge-taken count.
  bool hasLoopInvariantBackedgeTakenCount(const Loop *L);

  // This method should be called by the client when it made any change that
  // would invalidate SCEV's answers, and the client wants to remove all loop
  // information held internally by ScalarEvolution. This is intended to be used
  // when the alternative to forget a loop is too expensive (i.e. large loop
  // bodies).
  void forgetAllLoops();

  /// This method should be called by the client when it has changed a loop in
  /// a way that may effect ScalarEvolution's ability to compute a trip count,
  /// or if the loop is deleted.  This call is potentially expensive for large
  /// loop bodies.
  void forgetLoop(const Loop *L);

  // This method invokes forgetLoop for the outermost loop of the given loop
  // \p L, making ScalarEvolution forget about all this subtree. This needs to
  // be done whenever we make a transform that may affect the parameters of the
  // outer loop, such as exit counts for branches.
  void forgetTopmostLoop(const Loop *L);

  /// This method should be called by the client when it has changed a value
  /// in a way that may effect its value, or which may disconnect it from a
  /// def-use chain linking it to a loop.
  void forgetValue(Value *V);

  /// Forget LCSSA phi node V of loop L to which a new predecessor was added,
  /// such that it may no longer be trivial.
  void forgetLcssaPhiWithNewPredecessor(Loop *L, PHINode *V);

  /// Called when the client has changed the disposition of values in
  /// this loop.
  ///
  /// We don't have a way to invalidate per-loop dispositions. Clear and
  /// recompute is simpler.
  void forgetLoopDispositions();

  /// Called when the client has changed the disposition of values in
  /// a loop or block.
  ///
  /// We don't have a way to invalidate per-loop/per-block dispositions. Clear
  /// and recompute is simpler.
  void forgetBlockAndLoopDispositions(Value *V = nullptr);

  /// Determine the minimum number of zero bits that S is guaranteed to end in
  /// (at every loop iteration).  It is, at the same time, the minimum number
  /// of times S is divisible by 2.  For example, given {4,+,8} it returns 2.
  /// If S is guaranteed to be 0, it returns the bitwidth of S.
  uint32_t getMinTrailingZeros(const SCEV *S);

  /// Returns the max constant multiple of S.
  APInt getConstantMultiple(const SCEV *S);

  // Returns the max constant multiple of S. If S is exactly 0, return 1.
  APInt getNonZeroConstantMultiple(const SCEV *S);

  /// Determine the unsigned range for a particular SCEV.
  /// NOTE: This returns a copy of the reference returned by getRangeRef.
  ConstantRange getUnsignedRange(const SCEV *S) {
    return getRangeRef(S, HINT_RANGE_UNSIGNED);
  }

  /// Determine the min of the unsigned range for a particular SCEV.
  APInt getUnsignedRangeMin(const SCEV *S) {
    return getRangeRef(S, HINT_RANGE_UNSIGNED).getUnsignedMin();
  }

  /// Determine the max of the unsigned range for a particular SCEV.
  APInt getUnsignedRangeMax(const SCEV *S) {
    return getRangeRef(S, HINT_RANGE_UNSIGNED).getUnsignedMax();
  }

  /// Determine the signed range for a particular SCEV.
  /// NOTE: This returns a copy of the reference returned by getRangeRef.
  ConstantRange getSignedRange(const SCEV *S) {
    return getRangeRef(S, HINT_RANGE_SIGNED);
  }

  /// Determine the min of the signed range for a particular SCEV.
  APInt getSignedRangeMin(const SCEV *S) {
    return getRangeRef(S, HINT_RANGE_SIGNED).getSignedMin();
  }

  /// Determine the max of the signed range for a particular SCEV.
  APInt getSignedRangeMax(const SCEV *S) {
    return getRangeRef(S, HINT_RANGE_SIGNED).getSignedMax();
  }

  /// Test if the given expression is known to be negative.
  bool isKnownNegative(const SCEV *S);

  /// Test if the given expression is known to be positive.
  bool isKnownPositive(const SCEV *S);

  /// Test if the given expression is known to be non-negative.
  bool isKnownNonNegative(const SCEV *S);

  /// Test if the given expression is known to be non-positive.
  bool isKnownNonPositive(const SCEV *S);

  /// Test if the given expression is known to be non-zero.
  bool isKnownNonZero(const SCEV *S);

  /// Splits SCEV expression \p S into two SCEVs. One of them is obtained from
  /// \p S by substitution of all AddRec sub-expression related to loop \p L
  /// with initial value of that SCEV. The second is obtained from \p S by
  /// substitution of all AddRec sub-expressions related to loop \p L with post
  /// increment of this AddRec in the loop \p L. In both cases all other AddRec
  /// sub-expressions (not related to \p L) remain the same.
  /// If the \p S contains non-invariant unknown SCEV the function returns
  /// CouldNotCompute SCEV in both values of std::pair.
  /// For example, for SCEV S={0, +, 1}<L1> + {0, +, 1}<L2> and loop L=L1
  /// the function returns pair:
  /// first = {0, +, 1}<L2>
  /// second = {1, +, 1}<L1> + {0, +, 1}<L2>
  /// We can see that for the first AddRec sub-expression it was replaced with
  /// 0 (initial value) for the first element and to {1, +, 1}<L1> (post
  /// increment value) for the second one. In both cases AddRec expression
  /// related to L2 remains the same.
  std::pair<const SCEV *, const SCEV *> SplitIntoInitAndPostInc(const Loop *L,
                                                                const SCEV *S);

  /// We'd like to check the predicate on every iteration of the most dominated
  /// loop between loops used in LHS and RHS.
  /// To do this we use the following list of steps:
  /// 1. Collect set S all loops on which either LHS or RHS depend.
  /// 2. If S is non-empty
  /// a. Let PD be the element of S which is dominated by all other elements.
  /// b. Let E(LHS) be value of LHS on entry of PD.
  ///    To get E(LHS), we should just take LHS and replace all AddRecs that are
  ///    attached to PD on with their entry values.
  ///    Define E(RHS) in the same way.
  /// c. Let B(LHS) be value of L on backedge of PD.
  ///    To get B(LHS), we should just take LHS and replace all AddRecs that are
  ///    attached to PD on with their backedge values.
  ///    Define B(RHS) in the same way.
  /// d. Note that E(LHS) and E(RHS) are automatically available on entry of PD,
  ///    so we can assert on that.
  /// e. Return true if isLoopEntryGuardedByCond(Pred, E(LHS), E(RHS)) &&
  ///                   isLoopBackedgeGuardedByCond(Pred, B(LHS), B(RHS))
  bool isKnownViaInduction(ICmpInst::Predicate Pred, const SCEV *LHS,
                           const SCEV *RHS);

  /// Test if the given expression is known to satisfy the condition described
  /// by Pred, LHS, and RHS.
  bool isKnownPredicate(ICmpInst::Predicate Pred, const SCEV *LHS,
                        const SCEV *RHS);

  /// Check whether the condition described by Pred, LHS, and RHS is true or
  /// false. If we know it, return the evaluation of this condition. If neither
  /// is proved, return std::nullopt.
  std::optional<bool> evaluatePredicate(ICmpInst::Predicate Pred,
                                        const SCEV *LHS, const SCEV *RHS);

  /// Test if the given expression is known to satisfy the condition described
  /// by Pred, LHS, and RHS in the given Context.
  bool isKnownPredicateAt(ICmpInst::Predicate Pred, const SCEV *LHS,
                          const SCEV *RHS, const Instruction *CtxI);

  /// Check whether the condition described by Pred, LHS, and RHS is true or
  /// false in the given \p Context. If we know it, return the evaluation of
  /// this condition. If neither is proved, return std::nullopt.
  std::optional<bool> evaluatePredicateAt(ICmpInst::Predicate Pred,
                                          const SCEV *LHS, const SCEV *RHS,
                                          const Instruction *CtxI);

  /// Test if the condition described by Pred, LHS, RHS is known to be true on
  /// every iteration of the loop of the recurrency LHS.
  bool isKnownOnEveryIteration(ICmpInst::Predicate Pred,
                               const SCEVAddRecExpr *LHS, const SCEV *RHS);

  /// Information about the number of loop iterations for which a loop exit's
  /// branch condition evaluates to the not-taken path.  This is a temporary
  /// pair of exact and max expressions that are eventually summarized in
  /// ExitNotTakenInfo and BackedgeTakenInfo.
  struct ExitLimit {
    const SCEV *ExactNotTaken; // The exit is not taken exactly this many times
    const SCEV *ConstantMaxNotTaken; // The exit is not taken at most this many
                                     // times
    const SCEV *SymbolicMaxNotTaken;

    // Not taken either exactly ConstantMaxNotTaken or zero times
    bool MaxOrZero = false;

    /// A set of predicate guards for this ExitLimit. The result is only valid
    /// if all of the predicates in \c Predicates evaluate to 'true' at
    /// run-time.
    SmallPtrSet<const SCEVPredicate *, 4> Predicates;

    void addPredicate(const SCEVPredicate *P) {
      assert(!isa<SCEVUnionPredicate>(P) && "Only add leaf predicates here!");
      Predicates.insert(P);
    }

    /// Construct either an exact exit limit from a constant, or an unknown
    /// one from a SCEVCouldNotCompute.  No other types of SCEVs are allowed
    /// as arguments and asserts enforce that internally.
    /*implicit*/ ExitLimit(const SCEV *E);

    ExitLimit(
        const SCEV *E, const SCEV *ConstantMaxNotTaken,
        const SCEV *SymbolicMaxNotTaken, bool MaxOrZero,
        ArrayRef<const SmallPtrSetImpl<const SCEVPredicate *> *> PredSetList =
            std::nullopt);

    ExitLimit(const SCEV *E, const SCEV *ConstantMaxNotTaken,
              const SCEV *SymbolicMaxNotTaken, bool MaxOrZero,
              const SmallPtrSetImpl<const SCEVPredicate *> &PredSet);

    /// Test whether this ExitLimit contains any computed information, or
    /// whether it's all SCEVCouldNotCompute values.
    bool hasAnyInfo() const {
      return !isa<SCEVCouldNotCompute>(ExactNotTaken) ||
             !isa<SCEVCouldNotCompute>(ConstantMaxNotTaken);
    }

    /// Test whether this ExitLimit contains all information.
    bool hasFullInfo() const {
      return !isa<SCEVCouldNotCompute>(ExactNotTaken);
    }
  };

  /// Compute the number of times the backedge of the specified loop will
  /// execute if its exit condition were a conditional branch of ExitCond.
  ///
  /// \p ControlsOnlyExit is true if ExitCond directly controls the only exit
  /// branch. In this case, we can assume that the loop exits only if the
  /// condition is true and can infer that failing to meet the condition prior
  /// to integer wraparound results in undefined behavior.
  ///
  /// If \p AllowPredicates is set, this call will try to use a minimal set of
  /// SCEV predicates in order to return an exact answer.
  ExitLimit computeExitLimitFromCond(const Loop *L, Value *ExitCond,
                                     bool ExitIfTrue, bool ControlsOnlyExit,
                                     bool AllowPredicates = false);

  /// A predicate is said to be monotonically increasing if may go from being
  /// false to being true as the loop iterates, but never the other way
  /// around.  A predicate is said to be monotonically decreasing if may go
  /// from being true to being false as the loop iterates, but never the other
  /// way around.
  enum MonotonicPredicateType {
    MonotonicallyIncreasing,
    MonotonicallyDecreasing
  };

  /// If, for all loop invariant X, the predicate "LHS `Pred` X" is
  /// monotonically increasing or decreasing, returns
  /// Some(MonotonicallyIncreasing) and Some(MonotonicallyDecreasing)
  /// respectively. If we could not prove either of these facts, returns
  /// std::nullopt.
  std::optional<MonotonicPredicateType>
  getMonotonicPredicateType(const SCEVAddRecExpr *LHS,
                            ICmpInst::Predicate Pred);

  struct LoopInvariantPredicate {
    ICmpInst::Predicate Pred;
    const SCEV *LHS;
    const SCEV *RHS;

    LoopInvariantPredicate(ICmpInst::Predicate Pred, const SCEV *LHS,
                           const SCEV *RHS)
        : Pred(Pred), LHS(LHS), RHS(RHS) {}
  };
  /// If the result of the predicate LHS `Pred` RHS is loop invariant with
  /// respect to L, return a LoopInvariantPredicate with LHS and RHS being
  /// invariants, available at L's entry. Otherwise, return std::nullopt.
  std::optional<LoopInvariantPredicate>
  getLoopInvariantPredicate(ICmpInst::Predicate Pred, const SCEV *LHS,
                            const SCEV *RHS, const Loop *L,
                            const Instruction *CtxI = nullptr);

  /// If the result of the predicate LHS `Pred` RHS is loop invariant with
  /// respect to L at given Context during at least first MaxIter iterations,
  /// return a LoopInvariantPredicate with LHS and RHS being invariants,
  /// available at L's entry. Otherwise, return std::nullopt. The predicate
  /// should be the loop's exit condition.
  std::optional<LoopInvariantPredicate>
  getLoopInvariantExitCondDuringFirstIterations(ICmpInst::Predicate Pred,
                                                const SCEV *LHS,
                                                const SCEV *RHS, const Loop *L,
                                                const Instruction *CtxI,
                                                const SCEV *MaxIter);

  std::optional<LoopInvariantPredicate>
  getLoopInvariantExitCondDuringFirstIterationsImpl(
      ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS, const Loop *L,
      const Instruction *CtxI, const SCEV *MaxIter);

  /// Simplify LHS and RHS in a comparison with predicate Pred. Return true
  /// iff any changes were made. If the operands are provably equal or
  /// unequal, LHS and RHS are set to the same value and Pred is set to either
  /// ICMP_EQ or ICMP_NE.
  bool SimplifyICmpOperands(ICmpInst::Predicate &Pred, const SCEV *&LHS,
                            const SCEV *&RHS, unsigned Depth = 0);

  /// Return the "disposition" of the given SCEV with respect to the given
  /// loop.
  LoopDisposition getLoopDisposition(const SCEV *S, const Loop *L);

  /// Return true if the value of the given SCEV is unchanging in the
  /// specified loop.
  bool isLoopInvariant(const SCEV *S, const Loop *L);

  /// Determine if the SCEV can be evaluated at loop's entry. It is true if it
  /// doesn't depend on a SCEVUnknown of an instruction which is dominated by
  /// the header of loop L.
  bool isAvailableAtLoopEntry(const SCEV *S, const Loop *L);

  /// Return true if the given SCEV changes value in a known way in the
  /// specified loop.  This property being true implies that the value is
  /// variant in the loop AND that we can emit an expression to compute the
  /// value of the expression at any particular loop iteration.
  bool hasComputableLoopEvolution(const SCEV *S, const Loop *L);

  /// Return the "disposition" of the given SCEV with respect to the given
  /// block.
  BlockDisposition getBlockDisposition(const SCEV *S, const BasicBlock *BB);

  /// Return true if elements that makes up the given SCEV dominate the
  /// specified basic block.
  bool dominates(const SCEV *S, const BasicBlock *BB);

  /// Return true if elements that makes up the given SCEV properly dominate
  /// the specified basic block.
  bool properlyDominates(const SCEV *S, const BasicBlock *BB);

  /// Test whether the given SCEV has Op as a direct or indirect operand.
  bool hasOperand(const SCEV *S, const SCEV *Op) const;

  /// Return the size of an element read or written by Inst.
  const SCEV *getElementSize(Instruction *Inst);

  void print(raw_ostream &OS) const;
  void verify() const;
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);

  /// Return the DataLayout associated with the module this SCEV instance is
  /// operating on.
  const DataLayout &getDataLayout() const { return DL; }

  const SCEVPredicate *getEqualPredicate(const SCEV *LHS, const SCEV *RHS);
  const SCEVPredicate *getComparePredicate(ICmpInst::Predicate Pred,
                                           const SCEV *LHS, const SCEV *RHS);

  const SCEVPredicate *
  getWrapPredicate(const SCEVAddRecExpr *AR,
                   SCEVWrapPredicate::IncrementWrapFlags AddedFlags);

  /// Re-writes the SCEV according to the Predicates in \p A.
  const SCEV *rewriteUsingPredicate(const SCEV *S, const Loop *L,
                                    const SCEVPredicate &A);
  /// Tries to convert the \p S expression to an AddRec expression,
  /// adding additional predicates to \p Preds as required.
  const SCEVAddRecExpr *convertSCEVToAddRecWithPredicates(
      const SCEV *S, const Loop *L,
      SmallPtrSetImpl<const SCEVPredicate *> &Preds);

  /// Compute \p LHS - \p RHS and returns the result as an APInt if it is a
  /// constant, and std::nullopt if it isn't.
  ///
  /// This is intended to be a cheaper version of getMinusSCEV.  We can be
  /// frugal here since we just bail out of actually constructing and
  /// canonicalizing an expression in the cases where the result isn't going
  /// to be a constant.
  std::optional<APInt> computeConstantDifference(const SCEV *LHS,
                                                 const SCEV *RHS);

  /// Update no-wrap flags of an AddRec. This may drop the cached info about
  /// this AddRec (such as range info) in case if new flags may potentially
  /// sharpen it.
  void setNoWrapFlags(SCEVAddRecExpr *AddRec, SCEV::NoWrapFlags Flags);

  class LoopGuards {
    DenseMap<const SCEV *, const SCEV *> RewriteMap;
    bool PreserveNUW = false;
    bool PreserveNSW = false;
    ScalarEvolution &SE;

    LoopGuards(ScalarEvolution &SE) : SE(SE) {}

  public:
    /// Collect rewrite map for loop guards for loop \p L, together with flags
    /// indicating if NUW and NSW can be preserved during rewriting.
    static LoopGuards collect(const Loop *L, ScalarEvolution &SE);

    /// Try to apply the collected loop guards to \p Expr.
    const SCEV *rewrite(const SCEV *Expr) const;
  };

  /// Try to apply information from loop guards for \p L to \p Expr.
  const SCEV *applyLoopGuards(const SCEV *Expr, const Loop *L);
  const SCEV *applyLoopGuards(const SCEV *Expr, const LoopGuards &Guards);

  /// Return true if the loop has no abnormal exits. That is, if the loop
  /// is not infinite, it must exit through an explicit edge in the CFG.
  /// (As opposed to either a) throwing out of the function or b) entering a
  /// well defined infinite loop in some callee.)
  bool loopHasNoAbnormalExits(const Loop *L) {
    return getLoopProperties(L).HasNoAbnormalExits;
  }

  /// Return true if this loop is finite by assumption.  That is,
  /// to be infinite, it must also be undefined.
  bool loopIsFiniteByAssumption(const Loop *L);

  /// Return the set of Values that, if poison, will definitively result in S
  /// being poison as well. The returned set may be incomplete, i.e. there can
  /// be additional Values that also result in S being poison.
  void getPoisonGeneratingValues(SmallPtrSetImpl<const Value *> &Result,
                                 const SCEV *S);

  /// Check whether it is poison-safe to represent the expression S using the
  /// instruction I. If such a replacement is performed, the poison flags of
  /// instructions in DropPoisonGeneratingInsts must be dropped.
  bool canReuseInstruction(
      const SCEV *S, Instruction *I,
      SmallVectorImpl<Instruction *> &DropPoisonGeneratingInsts);

  class FoldID {
    const SCEV *Op = nullptr;
    const Type *Ty = nullptr;
    unsigned short C;

  public:
    FoldID(SCEVTypes C, const SCEV *Op, const Type *Ty) : Op(Op), Ty(Ty), C(C) {
      assert(Op);
      assert(Ty);
    }

    FoldID(unsigned short C) : C(C) {}

    unsigned computeHash() const {
      return detail::combineHashValue(
          C, detail::combineHashValue(reinterpret_cast<uintptr_t>(Op),
                                      reinterpret_cast<uintptr_t>(Ty)));
    }

    bool operator==(const FoldID &RHS) const {
      return std::tie(Op, Ty, C) == std::tie(RHS.Op, RHS.Ty, RHS.C);
    }
  };

private:
  /// A CallbackVH to arrange for ScalarEvolution to be notified whenever a
  /// Value is deleted.
  class SCEVCallbackVH final : public CallbackVH {
    ScalarEvolution *SE;

    void deleted() override;
    void allUsesReplacedWith(Value *New) override;

  public:
    SCEVCallbackVH(Value *V, ScalarEvolution *SE = nullptr);
  };

  friend class SCEVCallbackVH;
  friend class SCEVExpander;
  friend class SCEVUnknown;

  /// The function we are analyzing.
  Function &F;

  /// Data layout of the module.
  const DataLayout &DL;

  /// Does the module have any calls to the llvm.experimental.guard intrinsic
  /// at all?  If this is false, we avoid doing work that will only help if
  /// thare are guards present in the IR.
  bool HasGuards;

  /// The target library information for the target we are targeting.
  TargetLibraryInfo &TLI;

  /// The tracker for \@llvm.assume intrinsics in this function.
  AssumptionCache &AC;

  /// The dominator tree.
  DominatorTree &DT;

  /// The loop information for the function we are currently analyzing.
  LoopInfo &LI;

  /// This SCEV is used to represent unknown trip counts and things.
  std::unique_ptr<SCEVCouldNotCompute> CouldNotCompute;

  /// The type for HasRecMap.
  using HasRecMapType = DenseMap<const SCEV *, bool>;

  /// This is a cache to record whether a SCEV contains any scAddRecExpr.
  HasRecMapType HasRecMap;

  /// The type for ExprValueMap.
  using ValueSetVector = SmallSetVector<Value *, 4>;
  using ExprValueMapType = DenseMap<const SCEV *, ValueSetVector>;

  /// ExprValueMap -- This map records the original values from which
  /// the SCEV expr is generated from.
  ExprValueMapType ExprValueMap;

  /// The type for ValueExprMap.
  using ValueExprMapType =
      DenseMap<SCEVCallbackVH, const SCEV *, DenseMapInfo<Value *>>;

  /// This is a cache of the values we have analyzed so far.
  ValueExprMapType ValueExprMap;

  /// This is a cache for expressions that got folded to a different existing
  /// SCEV.
  DenseMap<FoldID, const SCEV *> FoldCache;
  DenseMap<const SCEV *, SmallVector<FoldID, 2>> FoldCacheUser;

  /// Mark predicate values currently being processed by isImpliedCond.
  SmallPtrSet<const Value *, 6> PendingLoopPredicates;

  /// Mark SCEVUnknown Phis currently being processed by getRangeRef.
  SmallPtrSet<const PHINode *, 6> PendingPhiRanges;

  /// Mark SCEVUnknown Phis currently being processed by getRangeRefIter.
  SmallPtrSet<const PHINode *, 6> PendingPhiRangesIter;

  // Mark SCEVUnknown Phis currently being processed by isImpliedViaMerge.
  SmallPtrSet<const PHINode *, 6> PendingMerges;

  /// Set to true by isLoopBackedgeGuardedByCond when we're walking the set of
  /// conditions dominating the backedge of a loop.
  bool WalkingBEDominatingConds = false;

  /// Set to true by isKnownPredicateViaSplitting when we're trying to prove a
  /// predicate by splitting it into a set of independent predicates.
  bool ProvingSplitPredicate = false;

  /// Memoized values for the getConstantMultiple
  DenseMap<const SCEV *, APInt> ConstantMultipleCache;

  /// Return the Value set from which the SCEV expr is generated.
  ArrayRef<Value *> getSCEVValues(const SCEV *S);

  /// Private helper method for the getConstantMultiple method.
  APInt getConstantMultipleImpl(const SCEV *S);

  /// Information about the number of times a particular loop exit may be
  /// reached before exiting the loop.
  struct ExitNotTakenInfo {
    PoisoningVH<BasicBlock> ExitingBlock;
    const SCEV *ExactNotTaken;
    const SCEV *ConstantMaxNotTaken;
    const SCEV *SymbolicMaxNotTaken;
    SmallPtrSet<const SCEVPredicate *, 4> Predicates;

    explicit ExitNotTakenInfo(
        PoisoningVH<BasicBlock> ExitingBlock, const SCEV *ExactNotTaken,
        const SCEV *ConstantMaxNotTaken, const SCEV *SymbolicMaxNotTaken,
        const SmallPtrSet<const SCEVPredicate *, 4> &Predicates)
        : ExitingBlock(ExitingBlock), ExactNotTaken(ExactNotTaken),
          ConstantMaxNotTaken(ConstantMaxNotTaken),
          SymbolicMaxNotTaken(SymbolicMaxNotTaken), Predicates(Predicates) {}

    bool hasAlwaysTruePredicate() const {
      return Predicates.empty();
    }
  };

  /// Information about the backedge-taken count of a loop. This currently
  /// includes an exact count and a maximum count.
  ///
  class BackedgeTakenInfo {
    friend class ScalarEvolution;

    /// A list of computable exits and their not-taken counts.  Loops almost
    /// never have more than one computable exit.
    SmallVector<ExitNotTakenInfo, 1> ExitNotTaken;

    /// Expression indicating the least constant maximum backedge-taken count of
    /// the loop that is known, or a SCEVCouldNotCompute. This expression is
    /// only valid if the redicates associated with all loop exits are true.
    const SCEV *ConstantMax = nullptr;

    /// Indicating if \c ExitNotTaken has an element for every exiting block in
    /// the loop.
    bool IsComplete = false;

    /// Expression indicating the least maximum backedge-taken count of the loop
    /// that is known, or a SCEVCouldNotCompute. Lazily computed on first query.
    const SCEV *SymbolicMax = nullptr;

    /// True iff the backedge is taken either exactly Max or zero times.
    bool MaxOrZero = false;

    bool isComplete() const { return IsComplete; }
    const SCEV *getConstantMax() const { return ConstantMax; }

  public:
    BackedgeTakenInfo() = default;
    BackedgeTakenInfo(BackedgeTakenInfo &&) = default;
    BackedgeTakenInfo &operator=(BackedgeTakenInfo &&) = default;

    using EdgeExitInfo = std::pair<BasicBlock *, ExitLimit>;

    /// Initialize BackedgeTakenInfo from a list of exact exit counts.
    BackedgeTakenInfo(ArrayRef<EdgeExitInfo> ExitCounts, bool IsComplete,
                      const SCEV *ConstantMax, bool MaxOrZero);

    /// Test whether this BackedgeTakenInfo contains any computed information,
    /// or whether it's all SCEVCouldNotCompute values.
    bool hasAnyInfo() const {
      return !ExitNotTaken.empty() ||
             !isa<SCEVCouldNotCompute>(getConstantMax());
    }

    /// Test whether this BackedgeTakenInfo contains complete information.
    bool hasFullInfo() const { return isComplete(); }

    /// Return an expression indicating the exact *backedge-taken*
    /// count of the loop if it is known or SCEVCouldNotCompute
    /// otherwise.  If execution makes it to the backedge on every
    /// iteration (i.e. there are no abnormal exists like exception
    /// throws and thread exits) then this is the number of times the
    /// loop header will execute minus one.
    ///
    /// If the SCEV predicate associated with the answer can be different
    /// from AlwaysTrue, we must add a (non null) Predicates argument.
    /// The SCEV predicate associated with the answer will be added to
    /// Predicates. A run-time check needs to be emitted for the SCEV
    /// predicate in order for the answer to be valid.
    ///
    /// Note that we should always know if we need to pass a predicate
    /// argument or not from the way the ExitCounts vector was computed.
    /// If we allowed SCEV predicates to be generated when populating this
    /// vector, this information can contain them and therefore a
    /// SCEVPredicate argument should be added to getExact.
    const SCEV *getExact(const Loop *L, ScalarEvolution *SE,
                         SmallVector<const SCEVPredicate *, 4> *Predicates = nullptr) const;

    /// Return the number of times this loop exit may fall through to the back
    /// edge, or SCEVCouldNotCompute. The loop is guaranteed not to exit via
    /// this block before this number of iterations, but may exit via another
    /// block.
    const SCEV *getExact(const BasicBlock *ExitingBlock,
                         ScalarEvolution *SE) const;

    /// Get the constant max backedge taken count for the loop.
    const SCEV *getConstantMax(ScalarEvolution *SE) const;

    /// Get the constant max backedge taken count for the particular loop exit.
    const SCEV *getConstantMax(const BasicBlock *ExitingBlock,
                               ScalarEvolution *SE) const;

    /// Get the symbolic max backedge taken count for the loop.
    const SCEV *
    getSymbolicMax(const Loop *L, ScalarEvolution *SE,
                   SmallVector<const SCEVPredicate *, 4> *Predicates = nullptr);

    /// Get the symbolic max backedge taken count for the particular loop exit.
    const SCEV *getSymbolicMax(const BasicBlock *ExitingBlock,
                               ScalarEvolution *SE) const;

    /// Return true if the number of times this backedge is taken is either the
    /// value returned by getConstantMax or zero.
    bool isConstantMaxOrZero(ScalarEvolution *SE) const;
  };

  /// Cache the backedge-taken count of the loops for this function as they
  /// are computed.
  DenseMap<const Loop *, BackedgeTakenInfo> BackedgeTakenCounts;

  /// Cache the predicated backedge-taken count of the loops for this
  /// function as they are computed.
  DenseMap<const Loop *, BackedgeTakenInfo> PredicatedBackedgeTakenCounts;

  /// Loops whose backedge taken counts directly use this non-constant SCEV.
  DenseMap<const SCEV *, SmallPtrSet<PointerIntPair<const Loop *, 1, bool>, 4>>
      BECountUsers;

  /// This map contains entries for all of the PHI instructions that we
  /// attempt to compute constant evolutions for.  This allows us to avoid
  /// potentially expensive recomputation of these properties.  An instruction
  /// maps to null if we are unable to compute its exit value.
  DenseMap<PHINode *, Constant *> ConstantEvolutionLoopExitValue;

  /// This map contains entries for all the expressions that we attempt to
  /// compute getSCEVAtScope information for, which can be expensive in
  /// extreme cases.
  DenseMap<const SCEV *, SmallVector<std::pair<const Loop *, const SCEV *>, 2>>
      ValuesAtScopes;

  /// Reverse map for invalidation purposes: Stores of which SCEV and which
  /// loop this is the value-at-scope of.
  DenseMap<const SCEV *, SmallVector<std::pair<const Loop *, const SCEV *>, 2>>
      ValuesAtScopesUsers;

  /// Memoized computeLoopDisposition results.
  DenseMap<const SCEV *,
           SmallVector<PointerIntPair<const Loop *, 2, LoopDisposition>, 2>>
      LoopDispositions;

  struct LoopProperties {
    /// Set to true if the loop contains no instruction that can abnormally exit
    /// the loop (i.e. via throwing an exception, by terminating the thread
    /// cleanly or by infinite looping in a called function).  Strictly
    /// speaking, the last one is not leaving the loop, but is identical to
    /// leaving the loop for reasoning about undefined behavior.
    bool HasNoAbnormalExits;

    /// Set to true if the loop contains no instruction that can have side
    /// effects (i.e. via throwing an exception, volatile or atomic access).
    bool HasNoSideEffects;
  };

  /// Cache for \c getLoopProperties.
  DenseMap<const Loop *, LoopProperties> LoopPropertiesCache;

  /// Return a \c LoopProperties instance for \p L, creating one if necessary.
  LoopProperties getLoopProperties(const Loop *L);

  bool loopHasNoSideEffects(const Loop *L) {
    return getLoopProperties(L).HasNoSideEffects;
  }

  /// Compute a LoopDisposition value.
  LoopDisposition computeLoopDisposition(const SCEV *S, const Loop *L);

  /// Memoized computeBlockDisposition results.
  DenseMap<
      const SCEV *,
      SmallVector<PointerIntPair<const BasicBlock *, 2, BlockDisposition>, 2>>
      BlockDispositions;

  /// Compute a BlockDisposition value.
  BlockDisposition computeBlockDisposition(const SCEV *S, const BasicBlock *BB);

  /// Stores all SCEV that use a given SCEV as its direct operand.
  DenseMap<const SCEV *, SmallPtrSet<const SCEV *, 8> > SCEVUsers;

  /// Memoized results from getRange
  DenseMap<const SCEV *, ConstantRange> UnsignedRanges;

  /// Memoized results from getRange
  DenseMap<const SCEV *, ConstantRange> SignedRanges;

  /// Used to parameterize getRange
  enum RangeSignHint { HINT_RANGE_UNSIGNED, HINT_RANGE_SIGNED };

  /// Set the memoized range for the given SCEV.
  const ConstantRange &setRange(const SCEV *S, RangeSignHint Hint,
                                ConstantRange CR) {
    DenseMap<const SCEV *, ConstantRange> &Cache =
        Hint == HINT_RANGE_UNSIGNED ? UnsignedRanges : SignedRanges;

    auto Pair = Cache.insert_or_assign(S, std::move(CR));
    return Pair.first->second;
  }

  /// Determine the range for a particular SCEV.
  /// NOTE: This returns a reference to an entry in a cache. It must be
  /// copied if its needed for longer.
  const ConstantRange &getRangeRef(const SCEV *S, RangeSignHint Hint,
                                   unsigned Depth = 0);

  /// Determine the range for a particular SCEV, but evaluates ranges for
  /// operands iteratively first.
  const ConstantRange &getRangeRefIter(const SCEV *S, RangeSignHint Hint);

  /// Determines the range for the affine SCEVAddRecExpr {\p Start,+,\p Step}.
  /// Helper for \c getRange.
  ConstantRange getRangeForAffineAR(const SCEV *Start, const SCEV *Step,
                                    const APInt &MaxBECount);

  /// Determines the range for the affine non-self-wrapping SCEVAddRecExpr {\p
  /// Start,+,\p Step}<nw>.
  ConstantRange getRangeForAffineNoSelfWrappingAR(const SCEVAddRecExpr *AddRec,
                                                  const SCEV *MaxBECount,
                                                  unsigned BitWidth,
                                                  RangeSignHint SignHint);

  /// Try to compute a range for the affine SCEVAddRecExpr {\p Start,+,\p
  /// Step} by "factoring out" a ternary expression from the add recurrence.
  /// Helper called by \c getRange.
  ConstantRange getRangeViaFactoring(const SCEV *Start, const SCEV *Step,
                                     const APInt &MaxBECount);

  /// If the unknown expression U corresponds to a simple recurrence, return
  /// a constant range which represents the entire recurrence.  Note that
  /// *add* recurrences with loop invariant steps aren't represented by
  /// SCEVUnknowns and thus don't use this mechanism.
  ConstantRange getRangeForUnknownRecurrence(const SCEVUnknown *U);

  /// We know that there is no SCEV for the specified value.  Analyze the
  /// expression recursively.
  const SCEV *createSCEV(Value *V);

  /// We know that there is no SCEV for the specified value. Create a new SCEV
  /// for \p V iteratively.
  const SCEV *createSCEVIter(Value *V);
  /// Collect operands of \p V for which SCEV expressions should be constructed
  /// first. Returns a SCEV directly if it can be constructed trivially for \p
  /// V.
  const SCEV *getOperandsToCreate(Value *V, SmallVectorImpl<Value *> &Ops);

  /// Provide the special handling we need to analyze PHI SCEVs.
  const SCEV *createNodeForPHI(PHINode *PN);

  /// Helper function called from createNodeForPHI.
  const SCEV *createAddRecFromPHI(PHINode *PN);

  /// A helper function for createAddRecFromPHI to handle simple cases.
  const SCEV *createSimpleAffineAddRec(PHINode *PN, Value *BEValueV,
                                            Value *StartValueV);

  /// Helper function called from createNodeForPHI.
  const SCEV *createNodeFromSelectLikePHI(PHINode *PN);

  /// Provide special handling for a select-like instruction (currently this
  /// is either a select instruction or a phi node).  \p Ty is the type of the
  /// instruction being processed, that is assumed equivalent to
  /// "Cond ? TrueVal : FalseVal".
  std::optional<const SCEV *>
  createNodeForSelectOrPHIInstWithICmpInstCond(Type *Ty, ICmpInst *Cond,
                                               Value *TrueVal, Value *FalseVal);

  /// See if we can model this select-like instruction via umin_seq expression.
  const SCEV *createNodeForSelectOrPHIViaUMinSeq(Value *I, Value *Cond,
                                                 Value *TrueVal,
                                                 Value *FalseVal);

  /// Given a value \p V, which is a select-like instruction (currently this is
  /// either a select instruction or a phi node), which is assumed equivalent to
  ///   Cond ? TrueVal : FalseVal
  /// see if we can model it as a SCEV expression.
  const SCEV *createNodeForSelectOrPHI(Value *V, Value *Cond, Value *TrueVal,
                                       Value *FalseVal);

  /// Provide the special handling we need to analyze GEP SCEVs.
  const SCEV *createNodeForGEP(GEPOperator *GEP);

  /// Implementation code for getSCEVAtScope; called at most once for each
  /// SCEV+Loop pair.
  const SCEV *computeSCEVAtScope(const SCEV *S, const Loop *L);

  /// Return the BackedgeTakenInfo for the given loop, lazily computing new
  /// values if the loop hasn't been analyzed yet. The returned result is
  /// guaranteed not to be predicated.
  BackedgeTakenInfo &getBackedgeTakenInfo(const Loop *L);

  /// Similar to getBackedgeTakenInfo, but will add predicates as required
  /// with the purpose of returning complete information.
  BackedgeTakenInfo &getPredicatedBackedgeTakenInfo(const Loop *L);

  /// Compute the number of times the specified loop will iterate.
  /// If AllowPredicates is set, we will create new SCEV predicates as
  /// necessary in order to return an exact answer.
  BackedgeTakenInfo computeBackedgeTakenCount(const Loop *L,
                                              bool AllowPredicates = false);

  /// Compute the number of times the backedge of the specified loop will
  /// execute if it exits via the specified block. If AllowPredicates is set,
  /// this call will try to use a minimal set of SCEV predicates in order to
  /// return an exact answer.
  ExitLimit computeExitLimit(const Loop *L, BasicBlock *ExitingBlock,
                             bool IsOnlyExit, bool AllowPredicates = false);

  // Helper functions for computeExitLimitFromCond to avoid exponential time
  // complexity.

  class ExitLimitCache {
    // It may look like we need key on the whole (L, ExitIfTrue,
    // ControlsOnlyExit, AllowPredicates) tuple, but recursive calls to
    // computeExitLimitFromCondCached from computeExitLimitFromCondImpl only
    // vary the in \c ExitCond and \c ControlsOnlyExit parameters.  We remember
    // the initial values of the other values to assert our assumption.
    SmallDenseMap<PointerIntPair<Value *, 1>, ExitLimit> TripCountMap;

    const Loop *L;
    bool ExitIfTrue;
    bool AllowPredicates;

  public:
    ExitLimitCache(const Loop *L, bool ExitIfTrue, bool AllowPredicates)
        : L(L), ExitIfTrue(ExitIfTrue), AllowPredicates(AllowPredicates) {}

    std::optional<ExitLimit> find(const Loop *L, Value *ExitCond,
                                  bool ExitIfTrue, bool ControlsOnlyExit,
                                  bool AllowPredicates);

    void insert(const Loop *L, Value *ExitCond, bool ExitIfTrue,
                bool ControlsOnlyExit, bool AllowPredicates,
                const ExitLimit &EL);
  };

  using ExitLimitCacheTy = ExitLimitCache;

  ExitLimit computeExitLimitFromCondCached(ExitLimitCacheTy &Cache,
                                           const Loop *L, Value *ExitCond,
                                           bool ExitIfTrue,
                                           bool ControlsOnlyExit,
                                           bool AllowPredicates);
  ExitLimit computeExitLimitFromCondImpl(ExitLimitCacheTy &Cache, const Loop *L,
                                         Value *ExitCond, bool ExitIfTrue,
                                         bool ControlsOnlyExit,
                                         bool AllowPredicates);
  std::optional<ScalarEvolution::ExitLimit> computeExitLimitFromCondFromBinOp(
      ExitLimitCacheTy &Cache, const Loop *L, Value *ExitCond, bool ExitIfTrue,
      bool ControlsOnlyExit, bool AllowPredicates);

  /// Compute the number of times the backedge of the specified loop will
  /// execute if its exit condition were a conditional branch of the ICmpInst
  /// ExitCond and ExitIfTrue. If AllowPredicates is set, this call will try
  /// to use a minimal set of SCEV predicates in order to return an exact
  /// answer.
  ExitLimit computeExitLimitFromICmp(const Loop *L, ICmpInst *ExitCond,
                                     bool ExitIfTrue,
                                     bool IsSubExpr,
                                     bool AllowPredicates = false);

  /// Variant of previous which takes the components representing an ICmp
  /// as opposed to the ICmpInst itself.  Note that the prior version can
  /// return more precise results in some cases and is preferred when caller
  /// has a materialized ICmp.
  ExitLimit computeExitLimitFromICmp(const Loop *L, ICmpInst::Predicate Pred,
                                     const SCEV *LHS, const SCEV *RHS,
                                     bool IsSubExpr,
                                     bool AllowPredicates = false);

  /// Compute the number of times the backedge of the specified loop will
  /// execute if its exit condition were a switch with a single exiting case
  /// to ExitingBB.
  ExitLimit computeExitLimitFromSingleExitSwitch(const Loop *L,
                                                 SwitchInst *Switch,
                                                 BasicBlock *ExitingBB,
                                                 bool IsSubExpr);

  /// Compute the exit limit of a loop that is controlled by a
  /// "(IV >> 1) != 0" type comparison.  We cannot compute the exact trip
  /// count in these cases (since SCEV has no way of expressing them), but we
  /// can still sometimes compute an upper bound.
  ///
  /// Return an ExitLimit for a loop whose backedge is guarded by `LHS Pred
  /// RHS`.
  ExitLimit computeShiftCompareExitLimit(Value *LHS, Value *RHS, const Loop *L,
                                         ICmpInst::Predicate Pred);

  /// If the loop is known to execute a constant number of times (the
  /// condition evolves only from constants), try to evaluate a few iterations
  /// of the loop until we get the exit condition gets a value of ExitWhen
  /// (true or false).  If we cannot evaluate the exit count of the loop,
  /// return CouldNotCompute.
  const SCEV *computeExitCountExhaustively(const Loop *L, Value *Cond,
                                           bool ExitWhen);

  /// Return the number of times an exit condition comparing the specified
  /// value to zero will execute.  If not computable, return CouldNotCompute.
  /// If AllowPredicates is set, this call will try to use a minimal set of
  /// SCEV predicates in order to return an exact answer.
  ExitLimit howFarToZero(const SCEV *V, const Loop *L, bool IsSubExpr,
                         bool AllowPredicates = false);

  /// Return the number of times an exit condition checking the specified
  /// value for nonzero will execute.  If not computable, return
  /// CouldNotCompute.
  ExitLimit howFarToNonZero(const SCEV *V, const Loop *L);

  /// Return the number of times an exit condition containing the specified
  /// less-than comparison will execute.  If not computable, return
  /// CouldNotCompute.
  ///
  /// \p isSigned specifies whether the less-than is signed.
  ///
  /// \p ControlsOnlyExit is true when the LHS < RHS condition directly controls
  /// the branch (loops exits only if condition is true). In this case, we can
  /// use NoWrapFlags to skip overflow checks.
  ///
  /// If \p AllowPredicates is set, this call will try to use a minimal set of
  /// SCEV predicates in order to return an exact answer.
  ExitLimit howManyLessThans(const SCEV *LHS, const SCEV *RHS, const Loop *L,
                             bool isSigned, bool ControlsOnlyExit,
                             bool AllowPredicates = false);

  ExitLimit howManyGreaterThans(const SCEV *LHS, const SCEV *RHS, const Loop *L,
                                bool isSigned, bool IsSubExpr,
                                bool AllowPredicates = false);

  /// Return a predecessor of BB (which may not be an immediate predecessor)
  /// which has exactly one successor from which BB is reachable, or null if
  /// no such block is found.
  std::pair<const BasicBlock *, const BasicBlock *>
  getPredecessorWithUniqueSuccessorForBB(const BasicBlock *BB) const;

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the given FoundCondValue value evaluates to true in given
  /// Context. If Context is nullptr, then the found predicate is true
  /// everywhere. LHS and FoundLHS may have different type width.
  bool isImpliedCond(ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS,
                     const Value *FoundCondValue, bool Inverse,
                     const Instruction *Context = nullptr);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the given FoundCondValue value evaluates to true in given
  /// Context. If Context is nullptr, then the found predicate is true
  /// everywhere. LHS and FoundLHS must have same type width.
  bool isImpliedCondBalancedTypes(ICmpInst::Predicate Pred, const SCEV *LHS,
                                  const SCEV *RHS,
                                  ICmpInst::Predicate FoundPred,
                                  const SCEV *FoundLHS, const SCEV *FoundRHS,
                                  const Instruction *CtxI);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by FoundPred, FoundLHS, FoundRHS is
  /// true in given Context. If Context is nullptr, then the found predicate is
  /// true everywhere.
  bool isImpliedCond(ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS,
                     ICmpInst::Predicate FoundPred, const SCEV *FoundLHS,
                     const SCEV *FoundRHS,
                     const Instruction *Context = nullptr);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true in given Context. If Context is nullptr, then the found predicate is
  /// true everywhere.
  bool isImpliedCondOperands(ICmpInst::Predicate Pred, const SCEV *LHS,
                             const SCEV *RHS, const SCEV *FoundLHS,
                             const SCEV *FoundRHS,
                             const Instruction *Context = nullptr);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true. Here LHS is an operation that includes FoundLHS as one of its
  /// arguments.
  bool isImpliedViaOperations(ICmpInst::Predicate Pred,
                              const SCEV *LHS, const SCEV *RHS,
                              const SCEV *FoundLHS, const SCEV *FoundRHS,
                              unsigned Depth = 0);

  /// Test whether the condition described by Pred, LHS, and RHS is true.
  /// Use only simple non-recursive types of checks, such as range analysis etc.
  bool isKnownViaNonRecursiveReasoning(ICmpInst::Predicate Pred,
                                       const SCEV *LHS, const SCEV *RHS);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  bool isImpliedCondOperandsHelper(ICmpInst::Predicate Pred, const SCEV *LHS,
                                   const SCEV *RHS, const SCEV *FoundLHS,
                                   const SCEV *FoundRHS);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.  Utility function used by isImpliedCondOperands.  Tries to get
  /// cases like "X `sgt` 0 => X - 1 `sgt` -1".
  bool isImpliedCondOperandsViaRanges(ICmpInst::Predicate Pred, const SCEV *LHS,
                                      const SCEV *RHS,
                                      ICmpInst::Predicate FoundPred,
                                      const SCEV *FoundLHS,
                                      const SCEV *FoundRHS);

  /// Return true if the condition denoted by \p LHS \p Pred \p RHS is implied
  /// by a call to @llvm.experimental.guard in \p BB.
  bool isImpliedViaGuard(const BasicBlock *BB, ICmpInst::Predicate Pred,
                         const SCEV *LHS, const SCEV *RHS);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  ///
  /// This routine tries to rule out certain kinds of integer overflow, and
  /// then tries to reason about arithmetic properties of the predicates.
  bool isImpliedCondOperandsViaNoOverflow(ICmpInst::Predicate Pred,
                                          const SCEV *LHS, const SCEV *RHS,
                                          const SCEV *FoundLHS,
                                          const SCEV *FoundRHS);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  ///
  /// This routine tries to weaken the known condition basing on fact that
  /// FoundLHS is an AddRec.
  bool isImpliedCondOperandsViaAddRecStart(ICmpInst::Predicate Pred,
                                           const SCEV *LHS, const SCEV *RHS,
                                           const SCEV *FoundLHS,
                                           const SCEV *FoundRHS,
                                           const Instruction *CtxI);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  ///
  /// This routine tries to figure out predicate for Phis which are SCEVUnknown
  /// if it is true for every possible incoming value from their respective
  /// basic blocks.
  bool isImpliedViaMerge(ICmpInst::Predicate Pred,
                         const SCEV *LHS, const SCEV *RHS,
                         const SCEV *FoundLHS, const SCEV *FoundRHS,
                         unsigned Depth);

  /// Test whether the condition described by Pred, LHS, and RHS is true
  /// whenever the condition described by Pred, FoundLHS, and FoundRHS is
  /// true.
  ///
  /// This routine tries to reason about shifts.
  bool isImpliedCondOperandsViaShift(ICmpInst::Predicate Pred, const SCEV *LHS,
                                     const SCEV *RHS, const SCEV *FoundLHS,
                                     const SCEV *FoundRHS);

  /// If we know that the specified Phi is in the header of its containing
  /// loop, we know the loop executes a constant number of times, and the PHI
  /// node is just a recurrence involving constants, fold it.
  Constant *getConstantEvolutionLoopExitValue(PHINode *PN, const APInt &BEs,
                                              const Loop *L);

  /// Test if the given expression is known to satisfy the condition described
  /// by Pred and the known constant ranges of LHS and RHS.
  bool isKnownPredicateViaConstantRanges(ICmpInst::Predicate Pred,
                                         const SCEV *LHS, const SCEV *RHS);

  /// Try to prove the condition described by "LHS Pred RHS" by ruling out
  /// integer overflow.
  ///
  /// For instance, this will return true for "A s< (A + C)<nsw>" if C is
  /// positive.
  bool isKnownPredicateViaNoOverflow(ICmpInst::Predicate Pred, const SCEV *LHS,
                                     const SCEV *RHS);

  /// Try to split Pred LHS RHS into logical conjunctions (and's) and try to
  /// prove them individually.
  bool isKnownPredicateViaSplitting(ICmpInst::Predicate Pred, const SCEV *LHS,
                                    const SCEV *RHS);

  /// Try to match the Expr as "(L + R)<Flags>".
  bool splitBinaryAdd(const SCEV *Expr, const SCEV *&L, const SCEV *&R,
                      SCEV::NoWrapFlags &Flags);

  /// Forget predicated/non-predicated backedge taken counts for the given loop.
  void forgetBackedgeTakenCounts(const Loop *L, bool Predicated);

  /// Drop memoized information for all \p SCEVs.
  void forgetMemoizedResults(ArrayRef<const SCEV *> SCEVs);

  /// Helper for forgetMemoizedResults.
  void forgetMemoizedResultsImpl(const SCEV *S);

  /// Iterate over instructions in \p Worklist and their users. Erase entries
  /// from ValueExprMap and collect SCEV expressions in \p ToForget
  void visitAndClearUsers(SmallVectorImpl<Instruction *> &Worklist,
                          SmallPtrSetImpl<Instruction *> &Visited,
                          SmallVectorImpl<const SCEV *> &ToForget);

  /// Erase Value from ValueExprMap and ExprValueMap.
  void eraseValueFromMap(Value *V);

  /// Insert V to S mapping into ValueExprMap and ExprValueMap.
  void insertValueToMap(Value *V, const SCEV *S);

  /// Return false iff given SCEV contains a SCEVUnknown with NULL value-
  /// pointer.
  bool checkValidity(const SCEV *S) const;

  /// Return true if `ExtendOpTy`({`Start`,+,`Step`}) can be proved to be
  /// equal to {`ExtendOpTy`(`Start`),+,`ExtendOpTy`(`Step`)}.  This is
  /// equivalent to proving no signed (resp. unsigned) wrap in
  /// {`Start`,+,`Step`} if `ExtendOpTy` is `SCEVSignExtendExpr`
  /// (resp. `SCEVZeroExtendExpr`).
  template <typename ExtendOpTy>
  bool proveNoWrapByVaryingStart(const SCEV *Start, const SCEV *Step,
                                 const Loop *L);

  /// Try to prove NSW or NUW on \p AR relying on ConstantRange manipulation.
  SCEV::NoWrapFlags proveNoWrapViaConstantRanges(const SCEVAddRecExpr *AR);

  /// Try to prove NSW on \p AR by proving facts about conditions known  on
  /// entry and backedge.
  SCEV::NoWrapFlags proveNoSignedWrapViaInduction(const SCEVAddRecExpr *AR);

  /// Try to prove NUW on \p AR by proving facts about conditions known on
  /// entry and backedge.
  SCEV::NoWrapFlags proveNoUnsignedWrapViaInduction(const SCEVAddRecExpr *AR);

  std::optional<MonotonicPredicateType>
  getMonotonicPredicateTypeImpl(const SCEVAddRecExpr *LHS,
                                ICmpInst::Predicate Pred);

  /// Return SCEV no-wrap flags that can be proven based on reasoning about
  /// how poison produced from no-wrap flags on this value (e.g. a nuw add)
  /// would trigger undefined behavior on overflow.
  SCEV::NoWrapFlags getNoWrapFlagsFromUB(const Value *V);

  /// Return a scope which provides an upper bound on the defining scope of
  /// 'S'. Specifically, return the first instruction in said bounding scope.
  /// Return nullptr if the scope is trivial (function entry).
  /// (See scope definition rules associated with flag discussion above)
  const Instruction *getNonTrivialDefiningScopeBound(const SCEV *S);

  /// Return a scope which provides an upper bound on the defining scope for
  /// a SCEV with the operands in Ops.  The outparam Precise is set if the
  /// bound found is a precise bound (i.e. must be the defining scope.)
  const Instruction *getDefiningScopeBound(ArrayRef<const SCEV *> Ops,
                                           bool &Precise);

  /// Wrapper around the above for cases which don't care if the bound
  /// is precise.
  const Instruction *getDefiningScopeBound(ArrayRef<const SCEV *> Ops);

  /// Given two instructions in the same function, return true if we can
  /// prove B must execute given A executes.
  bool isGuaranteedToTransferExecutionTo(const Instruction *A,
                                         const Instruction *B);

  /// Return true if the SCEV corresponding to \p I is never poison.  Proving
  /// this is more complex than proving that just \p I is never poison, since
  /// SCEV commons expressions across control flow, and you can have cases
  /// like:
  ///
  ///   idx0 = a + b;
  ///   ptr[idx0] = 100;
  ///   if (<condition>) {
  ///     idx1 = a +nsw b;
  ///     ptr[idx1] = 200;
  ///   }
  ///
  /// where the SCEV expression (+ a b) is guaranteed to not be poison (and
  /// hence not sign-overflow) only if "<condition>" is true.  Since both
  /// `idx0` and `idx1` will be mapped to the same SCEV expression, (+ a b),
  /// it is not okay to annotate (+ a b) with <nsw> in the above example.
  bool isSCEVExprNeverPoison(const Instruction *I);

  /// This is like \c isSCEVExprNeverPoison but it specifically works for
  /// instructions that will get mapped to SCEV add recurrences.  Return true
  /// if \p I will never generate poison under the assumption that \p I is an
  /// add recurrence on the loop \p L.
  bool isAddRecNeverPoison(const Instruction *I, const Loop *L);

  /// Similar to createAddRecFromPHI, but with the additional flexibility of
  /// suggesting runtime overflow checks in case casts are encountered.
  /// If successful, the analysis records that for this loop, \p SymbolicPHI,
  /// which is the UnknownSCEV currently representing the PHI, can be rewritten
  /// into an AddRec, assuming some predicates; The function then returns the
  /// AddRec and the predicates as a pair, and caches this pair in
  /// PredicatedSCEVRewrites.
  /// If the analysis is not successful, a mapping from the \p SymbolicPHI to
  /// itself (with no predicates) is recorded, and a nullptr with an empty
  /// predicates vector is returned as a pair.
  std::optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
  createAddRecFromPHIWithCastsImpl(const SCEVUnknown *SymbolicPHI);

  /// Compute the maximum backedge count based on the range of values
  /// permitted by Start, End, and Stride. This is for loops of the form
  /// {Start, +, Stride} LT End.
  ///
  /// Preconditions:
  /// * the induction variable is known to be positive.
  /// * the induction variable is assumed not to overflow (i.e. either it
  ///   actually doesn't, or we'd have to immediately execute UB)
  /// We *don't* assert these preconditions so please be careful.
  const SCEV *computeMaxBECountForLT(const SCEV *Start, const SCEV *Stride,
                                     const SCEV *End, unsigned BitWidth,
                                     bool IsSigned);

  /// Verify if an linear IV with positive stride can overflow when in a
  /// less-than comparison, knowing the invariant term of the comparison,
  /// the stride.
  bool canIVOverflowOnLT(const SCEV *RHS, const SCEV *Stride, bool IsSigned);

  /// Verify if an linear IV with negative stride can overflow when in a
  /// greater-than comparison, knowing the invariant term of the comparison,
  /// the stride.
  bool canIVOverflowOnGT(const SCEV *RHS, const SCEV *Stride, bool IsSigned);

  /// Get add expr already created or create a new one.
  const SCEV *getOrCreateAddExpr(ArrayRef<const SCEV *> Ops,
                                 SCEV::NoWrapFlags Flags);

  /// Get mul expr already created or create a new one.
  const SCEV *getOrCreateMulExpr(ArrayRef<const SCEV *> Ops,
                                 SCEV::NoWrapFlags Flags);

  // Get addrec expr already created or create a new one.
  const SCEV *getOrCreateAddRecExpr(ArrayRef<const SCEV *> Ops,
                                    const Loop *L, SCEV::NoWrapFlags Flags);

  /// Return x if \p Val is f(x) where f is a 1-1 function.
  const SCEV *stripInjectiveFunctions(const SCEV *Val) const;

  /// Find all of the loops transitively used in \p S, and fill \p LoopsUsed.
  /// A loop is considered "used" by an expression if it contains
  /// an add rec on said loop.
  void getUsedLoops(const SCEV *S, SmallPtrSetImpl<const Loop *> &LoopsUsed);

  /// Try to match the pattern generated by getURemExpr(A, B). If successful,
  /// Assign A and B to LHS and RHS, respectively.
  bool matchURem(const SCEV *Expr, const SCEV *&LHS, const SCEV *&RHS);

  /// Look for a SCEV expression with type `SCEVType` and operands `Ops` in
  /// `UniqueSCEVs`.  Return if found, else nullptr.
  SCEV *findExistingSCEVInCache(SCEVTypes SCEVType, ArrayRef<const SCEV *> Ops);

  /// Get reachable blocks in this function, making limited use of SCEV
  /// reasoning about conditions.
  void getReachableBlocks(SmallPtrSetImpl<BasicBlock *> &Reachable,
                          Function &F);

  /// Return the given SCEV expression with a new set of operands.
  /// This preserves the origial nowrap flags.
  const SCEV *getWithOperands(const SCEV *S,
                              SmallVectorImpl<const SCEV *> &NewOps);

  FoldingSet<SCEV> UniqueSCEVs;
  FoldingSet<SCEVPredicate> UniquePreds;
  BumpPtrAllocator SCEVAllocator;

  /// This maps loops to a list of addrecs that directly use said loop.
  DenseMap<const Loop *, SmallVector<const SCEVAddRecExpr *, 4>> LoopUsers;

  /// Cache tentative mappings from UnknownSCEVs in a Loop, to a SCEV expression
  /// they can be rewritten into under certain predicates.
  DenseMap<std::pair<const SCEVUnknown *, const Loop *>,
           std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
      PredicatedSCEVRewrites;

  /// Set of AddRecs for which proving NUW via an induction has already been
  /// tried.
  SmallPtrSet<const SCEVAddRecExpr *, 16> UnsignedWrapViaInductionTried;

  /// Set of AddRecs for which proving NSW via an induction has already been
  /// tried.
  SmallPtrSet<const SCEVAddRecExpr *, 16> SignedWrapViaInductionTried;

  /// The head of a linked list of all SCEVUnknown values that have been
  /// allocated. This is used by releaseMemory to locate them all and call
  /// their destructors.
  SCEVUnknown *FirstUnknown = nullptr;
};

/// Analysis pass that exposes the \c ScalarEvolution for a function.
class ScalarEvolutionAnalysis
    : public AnalysisInfoMixin<ScalarEvolutionAnalysis> {
  friend AnalysisInfoMixin<ScalarEvolutionAnalysis>;

  static AnalysisKey Key;

public:
  using Result = ScalarEvolution;

  ScalarEvolution run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for the \c ScalarEvolutionAnalysis results.
class ScalarEvolutionVerifierPass
    : public PassInfoMixin<ScalarEvolutionVerifierPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

/// Printer pass for the \c ScalarEvolutionAnalysis results.
class ScalarEvolutionPrinterPass
    : public PassInfoMixin<ScalarEvolutionPrinterPass> {
  raw_ostream &OS;

public:
  explicit ScalarEvolutionPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

class ScalarEvolutionWrapperPass : public FunctionPass {
  std::unique_ptr<ScalarEvolution> SE;

public:
  static char ID;

  ScalarEvolutionWrapperPass();

  ScalarEvolution &getSE() { return *SE; }
  const ScalarEvolution &getSE() const { return *SE; }

  bool runOnFunction(Function &F) override;
  void releaseMemory() override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void print(raw_ostream &OS, const Module * = nullptr) const override;
  void verifyAnalysis() const override;
};

/// An interface layer with SCEV used to manage how we see SCEV expressions
/// for values in the context of existing predicates. We can add new
/// predicates, but we cannot remove them.
///
/// This layer has multiple purposes:
///   - provides a simple interface for SCEV versioning.
///   - guarantees that the order of transformations applied on a SCEV
///     expression for a single Value is consistent across two different
///     getSCEV calls. This means that, for example, once we've obtained
///     an AddRec expression for a certain value through expression
///     rewriting, we will continue to get an AddRec expression for that
///     Value.
///   - lowers the number of expression rewrites.
class PredicatedScalarEvolution {
public:
  PredicatedScalarEvolution(ScalarEvolution &SE, Loop &L);

  const SCEVPredicate &getPredicate() const;

  /// Returns the SCEV expression of V, in the context of the current SCEV
  /// predicate.  The order of transformations applied on the expression of V
  /// returned by ScalarEvolution is guaranteed to be preserved, even when
  /// adding new predicates.
  const SCEV *getSCEV(Value *V);

  /// Get the (predicated) backedge count for the analyzed loop.
  const SCEV *getBackedgeTakenCount();

  /// Get the (predicated) symbolic max backedge count for the analyzed loop.
  const SCEV *getSymbolicMaxBackedgeTakenCount();

  /// Adds a new predicate.
  void addPredicate(const SCEVPredicate &Pred);

  /// Attempts to produce an AddRecExpr for V by adding additional SCEV
  /// predicates. If we can't transform the expression into an AddRecExpr we
  /// return nullptr and not add additional SCEV predicates to the current
  /// context.
  const SCEVAddRecExpr *getAsAddRec(Value *V);

  /// Proves that V doesn't overflow by adding SCEV predicate.
  void setNoOverflow(Value *V, SCEVWrapPredicate::IncrementWrapFlags Flags);

  /// Returns true if we've proved that V doesn't wrap by means of a SCEV
  /// predicate.
  bool hasNoOverflow(Value *V, SCEVWrapPredicate::IncrementWrapFlags Flags);

  /// Returns the ScalarEvolution analysis used.
  ScalarEvolution *getSE() const { return &SE; }

  /// We need to explicitly define the copy constructor because of FlagsMap.
  PredicatedScalarEvolution(const PredicatedScalarEvolution &);

  /// Print the SCEV mappings done by the Predicated Scalar Evolution.
  /// The printed text is indented by \p Depth.
  void print(raw_ostream &OS, unsigned Depth) const;

  /// Check if \p AR1 and \p AR2 are equal, while taking into account
  /// Equal predicates in Preds.
  bool areAddRecsEqualWithPreds(const SCEVAddRecExpr *AR1,
                                const SCEVAddRecExpr *AR2) const;

private:
  /// Increments the version number of the predicate.  This needs to be called
  /// every time the SCEV predicate changes.
  void updateGeneration();

  /// Holds a SCEV and the version number of the SCEV predicate used to
  /// perform the rewrite of the expression.
  using RewriteEntry = std::pair<unsigned, const SCEV *>;

  /// Maps a SCEV to the rewrite result of that SCEV at a certain version
  /// number. If this number doesn't match the current Generation, we will
  /// need to do a rewrite. To preserve the transformation order of previous
  /// rewrites, we will rewrite the previous result instead of the original
  /// SCEV.
  DenseMap<const SCEV *, RewriteEntry> RewriteMap;

  /// Records what NoWrap flags we've added to a Value *.
  ValueMap<Value *, SCEVWrapPredicate::IncrementWrapFlags> FlagsMap;

  /// The ScalarEvolution analysis.
  ScalarEvolution &SE;

  /// The analyzed Loop.
  const Loop &L;

  /// The SCEVPredicate that forms our context. We will rewrite all
  /// expressions assuming that this predicate true.
  std::unique_ptr<SCEVUnionPredicate> Preds;

  /// Marks the version of the SCEV predicate used. When rewriting a SCEV
  /// expression we mark it with the version of the predicate. We use this to
  /// figure out if the predicate has changed from the last rewrite of the
  /// SCEV. If so, we need to perform a new rewrite.
  unsigned Generation = 0;

  /// The backedge taken count.
  const SCEV *BackedgeCount = nullptr;

  /// The symbolic backedge taken count.
  const SCEV *SymbolicMaxBackedgeCount = nullptr;
};

template <> struct DenseMapInfo<ScalarEvolution::FoldID> {
  static inline ScalarEvolution::FoldID getEmptyKey() {
    ScalarEvolution::FoldID ID(0);
    return ID;
  }
  static inline ScalarEvolution::FoldID getTombstoneKey() {
    ScalarEvolution::FoldID ID(1);
    return ID;
  }

  static unsigned getHashValue(const ScalarEvolution::FoldID &Val) {
    return Val.computeHash();
  }

  static bool isEqual(const ScalarEvolution::FoldID &LHS,
                      const ScalarEvolution::FoldID &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_SCALAREVOLUTION_H
