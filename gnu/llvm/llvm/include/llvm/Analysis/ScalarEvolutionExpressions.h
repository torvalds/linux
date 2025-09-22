//===- llvm/Analysis/ScalarEvolutionExpressions.h - SCEV Exprs --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the classes used to represent and build scalar expressions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTIONEXPRESSIONS_H
#define LLVM_ANALYSIS_SCALAREVOLUTIONEXPRESSIONS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>

namespace llvm {

class APInt;
class Constant;
class ConstantInt;
class ConstantRange;
class Loop;
class Type;
class Value;

enum SCEVTypes : unsigned short {
  // These should be ordered in terms of increasing complexity to make the
  // folders simpler.
  scConstant,
  scVScale,
  scTruncate,
  scZeroExtend,
  scSignExtend,
  scAddExpr,
  scMulExpr,
  scUDivExpr,
  scAddRecExpr,
  scUMaxExpr,
  scSMaxExpr,
  scUMinExpr,
  scSMinExpr,
  scSequentialUMinExpr,
  scPtrToInt,
  scUnknown,
  scCouldNotCompute
};

/// This class represents a constant integer value.
class SCEVConstant : public SCEV {
  friend class ScalarEvolution;

  ConstantInt *V;

  SCEVConstant(const FoldingSetNodeIDRef ID, ConstantInt *v)
      : SCEV(ID, scConstant, 1), V(v) {}

public:
  ConstantInt *getValue() const { return V; }
  const APInt &getAPInt() const { return getValue()->getValue(); }

  Type *getType() const { return V->getType(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scConstant; }
};

/// This class represents the value of vscale, as used when defining the length
/// of a scalable vector or returned by the llvm.vscale() intrinsic.
class SCEVVScale : public SCEV {
  friend class ScalarEvolution;

  SCEVVScale(const FoldingSetNodeIDRef ID, Type *ty)
      : SCEV(ID, scVScale, 0), Ty(ty) {}

  Type *Ty;

public:
  Type *getType() const { return Ty; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scVScale; }
};

inline unsigned short computeExpressionSize(ArrayRef<const SCEV *> Args) {
  APInt Size(16, 1);
  for (const auto *Arg : Args)
    Size = Size.uadd_sat(APInt(16, Arg->getExpressionSize()));
  return (unsigned short)Size.getZExtValue();
}

/// This is the base class for unary cast operator classes.
class SCEVCastExpr : public SCEV {
protected:
  const SCEV *Op;
  Type *Ty;

  SCEVCastExpr(const FoldingSetNodeIDRef ID, SCEVTypes SCEVTy, const SCEV *op,
               Type *ty);

public:
  const SCEV *getOperand() const { return Op; }
  const SCEV *getOperand(unsigned i) const {
    assert(i == 0 && "Operand index out of range!");
    return Op;
  }
  ArrayRef<const SCEV *> operands() const { return Op; }
  size_t getNumOperands() const { return 1; }
  Type *getType() const { return Ty; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) {
    return S->getSCEVType() == scPtrToInt || S->getSCEVType() == scTruncate ||
           S->getSCEVType() == scZeroExtend || S->getSCEVType() == scSignExtend;
  }
};

/// This class represents a cast from a pointer to a pointer-sized integer
/// value.
class SCEVPtrToIntExpr : public SCEVCastExpr {
  friend class ScalarEvolution;

  SCEVPtrToIntExpr(const FoldingSetNodeIDRef ID, const SCEV *Op, Type *ITy);

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scPtrToInt; }
};

/// This is the base class for unary integral cast operator classes.
class SCEVIntegralCastExpr : public SCEVCastExpr {
protected:
  SCEVIntegralCastExpr(const FoldingSetNodeIDRef ID, SCEVTypes SCEVTy,
                       const SCEV *op, Type *ty);

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) {
    return S->getSCEVType() == scTruncate || S->getSCEVType() == scZeroExtend ||
           S->getSCEVType() == scSignExtend;
  }
};

/// This class represents a truncation of an integer value to a
/// smaller integer value.
class SCEVTruncateExpr : public SCEVIntegralCastExpr {
  friend class ScalarEvolution;

  SCEVTruncateExpr(const FoldingSetNodeIDRef ID, const SCEV *op, Type *ty);

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scTruncate; }
};

/// This class represents a zero extension of a small integer value
/// to a larger integer value.
class SCEVZeroExtendExpr : public SCEVIntegralCastExpr {
  friend class ScalarEvolution;

  SCEVZeroExtendExpr(const FoldingSetNodeIDRef ID, const SCEV *op, Type *ty);

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) {
    return S->getSCEVType() == scZeroExtend;
  }
};

/// This class represents a sign extension of a small integer value
/// to a larger integer value.
class SCEVSignExtendExpr : public SCEVIntegralCastExpr {
  friend class ScalarEvolution;

  SCEVSignExtendExpr(const FoldingSetNodeIDRef ID, const SCEV *op, Type *ty);

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) {
    return S->getSCEVType() == scSignExtend;
  }
};

/// This node is a base class providing common functionality for
/// n'ary operators.
class SCEVNAryExpr : public SCEV {
protected:
  // Since SCEVs are immutable, ScalarEvolution allocates operand
  // arrays with its SCEVAllocator, so this class just needs a simple
  // pointer rather than a more elaborate vector-like data structure.
  // This also avoids the need for a non-trivial destructor.
  const SCEV *const *Operands;
  size_t NumOperands;

  SCEVNAryExpr(const FoldingSetNodeIDRef ID, enum SCEVTypes T,
               const SCEV *const *O, size_t N)
      : SCEV(ID, T, computeExpressionSize(ArrayRef(O, N))), Operands(O),
        NumOperands(N) {}

public:
  size_t getNumOperands() const { return NumOperands; }

  const SCEV *getOperand(unsigned i) const {
    assert(i < NumOperands && "Operand index out of range!");
    return Operands[i];
  }

  ArrayRef<const SCEV *> operands() const {
    return ArrayRef(Operands, NumOperands);
  }

  NoWrapFlags getNoWrapFlags(NoWrapFlags Mask = NoWrapMask) const {
    return (NoWrapFlags)(SubclassData & Mask);
  }

  bool hasNoUnsignedWrap() const {
    return getNoWrapFlags(FlagNUW) != FlagAnyWrap;
  }

  bool hasNoSignedWrap() const {
    return getNoWrapFlags(FlagNSW) != FlagAnyWrap;
  }

  bool hasNoSelfWrap() const { return getNoWrapFlags(FlagNW) != FlagAnyWrap; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) {
    return S->getSCEVType() == scAddExpr || S->getSCEVType() == scMulExpr ||
           S->getSCEVType() == scSMaxExpr || S->getSCEVType() == scUMaxExpr ||
           S->getSCEVType() == scSMinExpr || S->getSCEVType() == scUMinExpr ||
           S->getSCEVType() == scSequentialUMinExpr ||
           S->getSCEVType() == scAddRecExpr;
  }
};

/// This node is the base class for n'ary commutative operators.
class SCEVCommutativeExpr : public SCEVNAryExpr {
protected:
  SCEVCommutativeExpr(const FoldingSetNodeIDRef ID, enum SCEVTypes T,
                      const SCEV *const *O, size_t N)
      : SCEVNAryExpr(ID, T, O, N) {}

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) {
    return S->getSCEVType() == scAddExpr || S->getSCEVType() == scMulExpr ||
           S->getSCEVType() == scSMaxExpr || S->getSCEVType() == scUMaxExpr ||
           S->getSCEVType() == scSMinExpr || S->getSCEVType() == scUMinExpr;
  }

  /// Set flags for a non-recurrence without clearing previously set flags.
  void setNoWrapFlags(NoWrapFlags Flags) { SubclassData |= Flags; }
};

/// This node represents an addition of some number of SCEVs.
class SCEVAddExpr : public SCEVCommutativeExpr {
  friend class ScalarEvolution;

  Type *Ty;

  SCEVAddExpr(const FoldingSetNodeIDRef ID, const SCEV *const *O, size_t N)
      : SCEVCommutativeExpr(ID, scAddExpr, O, N) {
    auto *FirstPointerTypedOp = find_if(operands(), [](const SCEV *Op) {
      return Op->getType()->isPointerTy();
    });
    if (FirstPointerTypedOp != operands().end())
      Ty = (*FirstPointerTypedOp)->getType();
    else
      Ty = getOperand(0)->getType();
  }

public:
  Type *getType() const { return Ty; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scAddExpr; }
};

/// This node represents multiplication of some number of SCEVs.
class SCEVMulExpr : public SCEVCommutativeExpr {
  friend class ScalarEvolution;

  SCEVMulExpr(const FoldingSetNodeIDRef ID, const SCEV *const *O, size_t N)
      : SCEVCommutativeExpr(ID, scMulExpr, O, N) {}

public:
  Type *getType() const { return getOperand(0)->getType(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scMulExpr; }
};

/// This class represents a binary unsigned division operation.
class SCEVUDivExpr : public SCEV {
  friend class ScalarEvolution;

  std::array<const SCEV *, 2> Operands;

  SCEVUDivExpr(const FoldingSetNodeIDRef ID, const SCEV *lhs, const SCEV *rhs)
      : SCEV(ID, scUDivExpr, computeExpressionSize({lhs, rhs})) {
    Operands[0] = lhs;
    Operands[1] = rhs;
  }

public:
  const SCEV *getLHS() const { return Operands[0]; }
  const SCEV *getRHS() const { return Operands[1]; }
  size_t getNumOperands() const { return 2; }
  const SCEV *getOperand(unsigned i) const {
    assert((i == 0 || i == 1) && "Operand index out of range!");
    return i == 0 ? getLHS() : getRHS();
  }

  ArrayRef<const SCEV *> operands() const { return Operands; }

  Type *getType() const {
    // In most cases the types of LHS and RHS will be the same, but in some
    // crazy cases one or the other may be a pointer. ScalarEvolution doesn't
    // depend on the type for correctness, but handling types carefully can
    // avoid extra casts in the SCEVExpander. The LHS is more likely to be
    // a pointer type than the RHS, so use the RHS' type here.
    return getRHS()->getType();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scUDivExpr; }
};

/// This node represents a polynomial recurrence on the trip count
/// of the specified loop.  This is the primary focus of the
/// ScalarEvolution framework; all the other SCEV subclasses are
/// mostly just supporting infrastructure to allow SCEVAddRecExpr
/// expressions to be created and analyzed.
///
/// All operands of an AddRec are required to be loop invariant.
///
class SCEVAddRecExpr : public SCEVNAryExpr {
  friend class ScalarEvolution;

  const Loop *L;

  SCEVAddRecExpr(const FoldingSetNodeIDRef ID, const SCEV *const *O, size_t N,
                 const Loop *l)
      : SCEVNAryExpr(ID, scAddRecExpr, O, N), L(l) {}

public:
  Type *getType() const { return getStart()->getType(); }
  const SCEV *getStart() const { return Operands[0]; }
  const Loop *getLoop() const { return L; }

  /// Constructs and returns the recurrence indicating how much this
  /// expression steps by.  If this is a polynomial of degree N, it
  /// returns a chrec of degree N-1.  We cannot determine whether
  /// the step recurrence has self-wraparound.
  const SCEV *getStepRecurrence(ScalarEvolution &SE) const {
    if (isAffine())
      return getOperand(1);
    return SE.getAddRecExpr(
        SmallVector<const SCEV *, 3>(operands().drop_front()), getLoop(),
        FlagAnyWrap);
  }

  /// Return true if this represents an expression A + B*x where A
  /// and B are loop invariant values.
  bool isAffine() const {
    // We know that the start value is invariant.  This expression is thus
    // affine iff the step is also invariant.
    return getNumOperands() == 2;
  }

  /// Return true if this represents an expression A + B*x + C*x^2
  /// where A, B and C are loop invariant values.  This corresponds
  /// to an addrec of the form {L,+,M,+,N}
  bool isQuadratic() const { return getNumOperands() == 3; }

  /// Set flags for a recurrence without clearing any previously set flags.
  /// For AddRec, either NUW or NSW implies NW. Keep track of this fact here
  /// to make it easier to propagate flags.
  void setNoWrapFlags(NoWrapFlags Flags) {
    if (Flags & (FlagNUW | FlagNSW))
      Flags = ScalarEvolution::setFlags(Flags, FlagNW);
    SubclassData |= Flags;
  }

  /// Return the value of this chain of recurrences at the specified
  /// iteration number.
  const SCEV *evaluateAtIteration(const SCEV *It, ScalarEvolution &SE) const;

  /// Return the value of this chain of recurrences at the specified iteration
  /// number. Takes an explicit list of operands to represent an AddRec.
  static const SCEV *evaluateAtIteration(ArrayRef<const SCEV *> Operands,
                                         const SCEV *It, ScalarEvolution &SE);

  /// Return the number of iterations of this loop that produce
  /// values in the specified constant range.  Another way of
  /// looking at this is that it returns the first iteration number
  /// where the value is not in the condition, thus computing the
  /// exit count.  If the iteration count can't be computed, an
  /// instance of SCEVCouldNotCompute is returned.
  const SCEV *getNumIterationsInRange(const ConstantRange &Range,
                                      ScalarEvolution &SE) const;

  /// Return an expression representing the value of this expression
  /// one iteration of the loop ahead.
  const SCEVAddRecExpr *getPostIncExpr(ScalarEvolution &SE) const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) {
    return S->getSCEVType() == scAddRecExpr;
  }
};

/// This node is the base class min/max selections.
class SCEVMinMaxExpr : public SCEVCommutativeExpr {
  friend class ScalarEvolution;

  static bool isMinMaxType(enum SCEVTypes T) {
    return T == scSMaxExpr || T == scUMaxExpr || T == scSMinExpr ||
           T == scUMinExpr;
  }

protected:
  /// Note: Constructing subclasses via this constructor is allowed
  SCEVMinMaxExpr(const FoldingSetNodeIDRef ID, enum SCEVTypes T,
                 const SCEV *const *O, size_t N)
      : SCEVCommutativeExpr(ID, T, O, N) {
    assert(isMinMaxType(T));
    // Min and max never overflow
    setNoWrapFlags((NoWrapFlags)(FlagNUW | FlagNSW));
  }

public:
  Type *getType() const { return getOperand(0)->getType(); }

  static bool classof(const SCEV *S) { return isMinMaxType(S->getSCEVType()); }

  static enum SCEVTypes negate(enum SCEVTypes T) {
    switch (T) {
    case scSMaxExpr:
      return scSMinExpr;
    case scSMinExpr:
      return scSMaxExpr;
    case scUMaxExpr:
      return scUMinExpr;
    case scUMinExpr:
      return scUMaxExpr;
    default:
      llvm_unreachable("Not a min or max SCEV type!");
    }
  }
};

/// This class represents a signed maximum selection.
class SCEVSMaxExpr : public SCEVMinMaxExpr {
  friend class ScalarEvolution;

  SCEVSMaxExpr(const FoldingSetNodeIDRef ID, const SCEV *const *O, size_t N)
      : SCEVMinMaxExpr(ID, scSMaxExpr, O, N) {}

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scSMaxExpr; }
};

/// This class represents an unsigned maximum selection.
class SCEVUMaxExpr : public SCEVMinMaxExpr {
  friend class ScalarEvolution;

  SCEVUMaxExpr(const FoldingSetNodeIDRef ID, const SCEV *const *O, size_t N)
      : SCEVMinMaxExpr(ID, scUMaxExpr, O, N) {}

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scUMaxExpr; }
};

/// This class represents a signed minimum selection.
class SCEVSMinExpr : public SCEVMinMaxExpr {
  friend class ScalarEvolution;

  SCEVSMinExpr(const FoldingSetNodeIDRef ID, const SCEV *const *O, size_t N)
      : SCEVMinMaxExpr(ID, scSMinExpr, O, N) {}

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scSMinExpr; }
};

/// This class represents an unsigned minimum selection.
class SCEVUMinExpr : public SCEVMinMaxExpr {
  friend class ScalarEvolution;

  SCEVUMinExpr(const FoldingSetNodeIDRef ID, const SCEV *const *O, size_t N)
      : SCEVMinMaxExpr(ID, scUMinExpr, O, N) {}

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scUMinExpr; }
};

/// This node is the base class for sequential/in-order min/max selections.
/// Note that their fundamental difference from SCEVMinMaxExpr's is that they
/// are early-returning upon reaching saturation point.
/// I.e. given `0 umin_seq poison`, the result will be `0`,
/// while the result of `0 umin poison` is `poison`.
class SCEVSequentialMinMaxExpr : public SCEVNAryExpr {
  friend class ScalarEvolution;

  static bool isSequentialMinMaxType(enum SCEVTypes T) {
    return T == scSequentialUMinExpr;
  }

  /// Set flags for a non-recurrence without clearing previously set flags.
  void setNoWrapFlags(NoWrapFlags Flags) { SubclassData |= Flags; }

protected:
  /// Note: Constructing subclasses via this constructor is allowed
  SCEVSequentialMinMaxExpr(const FoldingSetNodeIDRef ID, enum SCEVTypes T,
                           const SCEV *const *O, size_t N)
      : SCEVNAryExpr(ID, T, O, N) {
    assert(isSequentialMinMaxType(T));
    // Min and max never overflow
    setNoWrapFlags((NoWrapFlags)(FlagNUW | FlagNSW));
  }

public:
  Type *getType() const { return getOperand(0)->getType(); }

  static SCEVTypes getEquivalentNonSequentialSCEVType(SCEVTypes Ty) {
    assert(isSequentialMinMaxType(Ty));
    switch (Ty) {
    case scSequentialUMinExpr:
      return scUMinExpr;
    default:
      llvm_unreachable("Not a sequential min/max type.");
    }
  }

  SCEVTypes getEquivalentNonSequentialSCEVType() const {
    return getEquivalentNonSequentialSCEVType(getSCEVType());
  }

  static bool classof(const SCEV *S) {
    return isSequentialMinMaxType(S->getSCEVType());
  }
};

/// This class represents a sequential/in-order unsigned minimum selection.
class SCEVSequentialUMinExpr : public SCEVSequentialMinMaxExpr {
  friend class ScalarEvolution;

  SCEVSequentialUMinExpr(const FoldingSetNodeIDRef ID, const SCEV *const *O,
                         size_t N)
      : SCEVSequentialMinMaxExpr(ID, scSequentialUMinExpr, O, N) {}

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) {
    return S->getSCEVType() == scSequentialUMinExpr;
  }
};

/// This means that we are dealing with an entirely unknown SCEV
/// value, and only represent it as its LLVM Value.  This is the
/// "bottom" value for the analysis.
class SCEVUnknown final : public SCEV, private CallbackVH {
  friend class ScalarEvolution;

  /// The parent ScalarEvolution value. This is used to update the
  /// parent's maps when the value associated with a SCEVUnknown is
  /// deleted or RAUW'd.
  ScalarEvolution *SE;

  /// The next pointer in the linked list of all SCEVUnknown
  /// instances owned by a ScalarEvolution.
  SCEVUnknown *Next;

  SCEVUnknown(const FoldingSetNodeIDRef ID, Value *V, ScalarEvolution *se,
              SCEVUnknown *next)
      : SCEV(ID, scUnknown, 1), CallbackVH(V), SE(se), Next(next) {}

  // Implement CallbackVH.
  void deleted() override;
  void allUsesReplacedWith(Value *New) override;

public:
  Value *getValue() const { return getValPtr(); }

  Type *getType() const { return getValPtr()->getType(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const SCEV *S) { return S->getSCEVType() == scUnknown; }
};

/// This class defines a simple visitor class that may be used for
/// various SCEV analysis purposes.
template <typename SC, typename RetVal = void> struct SCEVVisitor {
  RetVal visit(const SCEV *S) {
    switch (S->getSCEVType()) {
    case scConstant:
      return ((SC *)this)->visitConstant((const SCEVConstant *)S);
    case scVScale:
      return ((SC *)this)->visitVScale((const SCEVVScale *)S);
    case scPtrToInt:
      return ((SC *)this)->visitPtrToIntExpr((const SCEVPtrToIntExpr *)S);
    case scTruncate:
      return ((SC *)this)->visitTruncateExpr((const SCEVTruncateExpr *)S);
    case scZeroExtend:
      return ((SC *)this)->visitZeroExtendExpr((const SCEVZeroExtendExpr *)S);
    case scSignExtend:
      return ((SC *)this)->visitSignExtendExpr((const SCEVSignExtendExpr *)S);
    case scAddExpr:
      return ((SC *)this)->visitAddExpr((const SCEVAddExpr *)S);
    case scMulExpr:
      return ((SC *)this)->visitMulExpr((const SCEVMulExpr *)S);
    case scUDivExpr:
      return ((SC *)this)->visitUDivExpr((const SCEVUDivExpr *)S);
    case scAddRecExpr:
      return ((SC *)this)->visitAddRecExpr((const SCEVAddRecExpr *)S);
    case scSMaxExpr:
      return ((SC *)this)->visitSMaxExpr((const SCEVSMaxExpr *)S);
    case scUMaxExpr:
      return ((SC *)this)->visitUMaxExpr((const SCEVUMaxExpr *)S);
    case scSMinExpr:
      return ((SC *)this)->visitSMinExpr((const SCEVSMinExpr *)S);
    case scUMinExpr:
      return ((SC *)this)->visitUMinExpr((const SCEVUMinExpr *)S);
    case scSequentialUMinExpr:
      return ((SC *)this)
          ->visitSequentialUMinExpr((const SCEVSequentialUMinExpr *)S);
    case scUnknown:
      return ((SC *)this)->visitUnknown((const SCEVUnknown *)S);
    case scCouldNotCompute:
      return ((SC *)this)->visitCouldNotCompute((const SCEVCouldNotCompute *)S);
    }
    llvm_unreachable("Unknown SCEV kind!");
  }

  RetVal visitCouldNotCompute(const SCEVCouldNotCompute *S) {
    llvm_unreachable("Invalid use of SCEVCouldNotCompute!");
  }
};

/// Visit all nodes in the expression tree using worklist traversal.
///
/// Visitor implements:
///   // return true to follow this node.
///   bool follow(const SCEV *S);
///   // return true to terminate the search.
///   bool isDone();
template <typename SV> class SCEVTraversal {
  SV &Visitor;
  SmallVector<const SCEV *, 8> Worklist;
  SmallPtrSet<const SCEV *, 8> Visited;

  void push(const SCEV *S) {
    if (Visited.insert(S).second && Visitor.follow(S))
      Worklist.push_back(S);
  }

public:
  SCEVTraversal(SV &V) : Visitor(V) {}

  void visitAll(const SCEV *Root) {
    push(Root);
    while (!Worklist.empty() && !Visitor.isDone()) {
      const SCEV *S = Worklist.pop_back_val();

      switch (S->getSCEVType()) {
      case scConstant:
      case scVScale:
      case scUnknown:
        continue;
      case scPtrToInt:
      case scTruncate:
      case scZeroExtend:
      case scSignExtend:
      case scAddExpr:
      case scMulExpr:
      case scUDivExpr:
      case scSMaxExpr:
      case scUMaxExpr:
      case scSMinExpr:
      case scUMinExpr:
      case scSequentialUMinExpr:
      case scAddRecExpr:
        for (const auto *Op : S->operands()) {
          push(Op);
          if (Visitor.isDone())
            break;
        }
        continue;
      case scCouldNotCompute:
        llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
      }
      llvm_unreachable("Unknown SCEV kind!");
    }
  }
};

/// Use SCEVTraversal to visit all nodes in the given expression tree.
template <typename SV> void visitAll(const SCEV *Root, SV &Visitor) {
  SCEVTraversal<SV> T(Visitor);
  T.visitAll(Root);
}

/// Return true if any node in \p Root satisfies the predicate \p Pred.
template <typename PredTy>
bool SCEVExprContains(const SCEV *Root, PredTy Pred) {
  struct FindClosure {
    bool Found = false;
    PredTy Pred;

    FindClosure(PredTy Pred) : Pred(Pred) {}

    bool follow(const SCEV *S) {
      if (!Pred(S))
        return true;

      Found = true;
      return false;
    }

    bool isDone() const { return Found; }
  };

  FindClosure FC(Pred);
  visitAll(Root, FC);
  return FC.Found;
}

/// This visitor recursively visits a SCEV expression and re-writes it.
/// The result from each visit is cached, so it will return the same
/// SCEV for the same input.
template <typename SC>
class SCEVRewriteVisitor : public SCEVVisitor<SC, const SCEV *> {
protected:
  ScalarEvolution &SE;
  // Memoize the result of each visit so that we only compute once for
  // the same input SCEV. This is to avoid redundant computations when
  // a SCEV is referenced by multiple SCEVs. Without memoization, this
  // visit algorithm would have exponential time complexity in the worst
  // case, causing the compiler to hang on certain tests.
  SmallDenseMap<const SCEV *, const SCEV *> RewriteResults;

public:
  SCEVRewriteVisitor(ScalarEvolution &SE) : SE(SE) {}

  const SCEV *visit(const SCEV *S) {
    auto It = RewriteResults.find(S);
    if (It != RewriteResults.end())
      return It->second;
    auto *Visited = SCEVVisitor<SC, const SCEV *>::visit(S);
    auto Result = RewriteResults.try_emplace(S, Visited);
    assert(Result.second && "Should insert a new entry");
    return Result.first->second;
  }

  const SCEV *visitConstant(const SCEVConstant *Constant) { return Constant; }

  const SCEV *visitVScale(const SCEVVScale *VScale) { return VScale; }

  const SCEV *visitPtrToIntExpr(const SCEVPtrToIntExpr *Expr) {
    const SCEV *Operand = ((SC *)this)->visit(Expr->getOperand());
    return Operand == Expr->getOperand()
               ? Expr
               : SE.getPtrToIntExpr(Operand, Expr->getType());
  }

  const SCEV *visitTruncateExpr(const SCEVTruncateExpr *Expr) {
    const SCEV *Operand = ((SC *)this)->visit(Expr->getOperand());
    return Operand == Expr->getOperand()
               ? Expr
               : SE.getTruncateExpr(Operand, Expr->getType());
  }

  const SCEV *visitZeroExtendExpr(const SCEVZeroExtendExpr *Expr) {
    const SCEV *Operand = ((SC *)this)->visit(Expr->getOperand());
    return Operand == Expr->getOperand()
               ? Expr
               : SE.getZeroExtendExpr(Operand, Expr->getType());
  }

  const SCEV *visitSignExtendExpr(const SCEVSignExtendExpr *Expr) {
    const SCEV *Operand = ((SC *)this)->visit(Expr->getOperand());
    return Operand == Expr->getOperand()
               ? Expr
               : SE.getSignExtendExpr(Operand, Expr->getType());
  }

  const SCEV *visitAddExpr(const SCEVAddExpr *Expr) {
    SmallVector<const SCEV *, 2> Operands;
    bool Changed = false;
    for (const auto *Op : Expr->operands()) {
      Operands.push_back(((SC *)this)->visit(Op));
      Changed |= Op != Operands.back();
    }
    return !Changed ? Expr : SE.getAddExpr(Operands);
  }

  const SCEV *visitMulExpr(const SCEVMulExpr *Expr) {
    SmallVector<const SCEV *, 2> Operands;
    bool Changed = false;
    for (const auto *Op : Expr->operands()) {
      Operands.push_back(((SC *)this)->visit(Op));
      Changed |= Op != Operands.back();
    }
    return !Changed ? Expr : SE.getMulExpr(Operands);
  }

  const SCEV *visitUDivExpr(const SCEVUDivExpr *Expr) {
    auto *LHS = ((SC *)this)->visit(Expr->getLHS());
    auto *RHS = ((SC *)this)->visit(Expr->getRHS());
    bool Changed = LHS != Expr->getLHS() || RHS != Expr->getRHS();
    return !Changed ? Expr : SE.getUDivExpr(LHS, RHS);
  }

  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) {
    SmallVector<const SCEV *, 2> Operands;
    bool Changed = false;
    for (const auto *Op : Expr->operands()) {
      Operands.push_back(((SC *)this)->visit(Op));
      Changed |= Op != Operands.back();
    }
    return !Changed ? Expr
                    : SE.getAddRecExpr(Operands, Expr->getLoop(),
                                       Expr->getNoWrapFlags());
  }

  const SCEV *visitSMaxExpr(const SCEVSMaxExpr *Expr) {
    SmallVector<const SCEV *, 2> Operands;
    bool Changed = false;
    for (const auto *Op : Expr->operands()) {
      Operands.push_back(((SC *)this)->visit(Op));
      Changed |= Op != Operands.back();
    }
    return !Changed ? Expr : SE.getSMaxExpr(Operands);
  }

  const SCEV *visitUMaxExpr(const SCEVUMaxExpr *Expr) {
    SmallVector<const SCEV *, 2> Operands;
    bool Changed = false;
    for (const auto *Op : Expr->operands()) {
      Operands.push_back(((SC *)this)->visit(Op));
      Changed |= Op != Operands.back();
    }
    return !Changed ? Expr : SE.getUMaxExpr(Operands);
  }

  const SCEV *visitSMinExpr(const SCEVSMinExpr *Expr) {
    SmallVector<const SCEV *, 2> Operands;
    bool Changed = false;
    for (const auto *Op : Expr->operands()) {
      Operands.push_back(((SC *)this)->visit(Op));
      Changed |= Op != Operands.back();
    }
    return !Changed ? Expr : SE.getSMinExpr(Operands);
  }

  const SCEV *visitUMinExpr(const SCEVUMinExpr *Expr) {
    SmallVector<const SCEV *, 2> Operands;
    bool Changed = false;
    for (const auto *Op : Expr->operands()) {
      Operands.push_back(((SC *)this)->visit(Op));
      Changed |= Op != Operands.back();
    }
    return !Changed ? Expr : SE.getUMinExpr(Operands);
  }

  const SCEV *visitSequentialUMinExpr(const SCEVSequentialUMinExpr *Expr) {
    SmallVector<const SCEV *, 2> Operands;
    bool Changed = false;
    for (const auto *Op : Expr->operands()) {
      Operands.push_back(((SC *)this)->visit(Op));
      Changed |= Op != Operands.back();
    }
    return !Changed ? Expr : SE.getUMinExpr(Operands, /*Sequential=*/true);
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) { return Expr; }

  const SCEV *visitCouldNotCompute(const SCEVCouldNotCompute *Expr) {
    return Expr;
  }
};

using ValueToValueMap = DenseMap<const Value *, Value *>;
using ValueToSCEVMapTy = DenseMap<const Value *, const SCEV *>;

/// The SCEVParameterRewriter takes a scalar evolution expression and updates
/// the SCEVUnknown components following the Map (Value -> SCEV).
class SCEVParameterRewriter : public SCEVRewriteVisitor<SCEVParameterRewriter> {
public:
  static const SCEV *rewrite(const SCEV *Scev, ScalarEvolution &SE,
                             ValueToSCEVMapTy &Map) {
    SCEVParameterRewriter Rewriter(SE, Map);
    return Rewriter.visit(Scev);
  }

  SCEVParameterRewriter(ScalarEvolution &SE, ValueToSCEVMapTy &M)
      : SCEVRewriteVisitor(SE), Map(M) {}

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    auto I = Map.find(Expr->getValue());
    if (I == Map.end())
      return Expr;
    return I->second;
  }

private:
  ValueToSCEVMapTy &Map;
};

using LoopToScevMapT = DenseMap<const Loop *, const SCEV *>;

/// The SCEVLoopAddRecRewriter takes a scalar evolution expression and applies
/// the Map (Loop -> SCEV) to all AddRecExprs.
class SCEVLoopAddRecRewriter
    : public SCEVRewriteVisitor<SCEVLoopAddRecRewriter> {
public:
  SCEVLoopAddRecRewriter(ScalarEvolution &SE, LoopToScevMapT &M)
      : SCEVRewriteVisitor(SE), Map(M) {}

  static const SCEV *rewrite(const SCEV *Scev, LoopToScevMapT &Map,
                             ScalarEvolution &SE) {
    SCEVLoopAddRecRewriter Rewriter(SE, Map);
    return Rewriter.visit(Scev);
  }

  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) {
    SmallVector<const SCEV *, 2> Operands;
    for (const SCEV *Op : Expr->operands())
      Operands.push_back(visit(Op));

    const Loop *L = Expr->getLoop();
    if (0 == Map.count(L))
      return SE.getAddRecExpr(Operands, L, Expr->getNoWrapFlags());

    return SCEVAddRecExpr::evaluateAtIteration(Operands, Map[L], SE);
  }

private:
  LoopToScevMapT &Map;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_SCALAREVOLUTIONEXPRESSIONS_H
