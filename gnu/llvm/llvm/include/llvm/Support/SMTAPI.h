//===- SMTAPI.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a SMT generic Solver API, which will be the base class
//  for every SMT solver specific class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SMTAPI_H
#define LLVM_SUPPORT_SMTAPI_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {

/// Generic base class for SMT sorts
class SMTSort {
public:
  SMTSort() = default;
  virtual ~SMTSort() = default;

  /// Returns true if the sort is a bitvector, calls isBitvectorSortImpl().
  virtual bool isBitvectorSort() const { return isBitvectorSortImpl(); }

  /// Returns true if the sort is a floating-point, calls isFloatSortImpl().
  virtual bool isFloatSort() const { return isFloatSortImpl(); }

  /// Returns true if the sort is a boolean, calls isBooleanSortImpl().
  virtual bool isBooleanSort() const { return isBooleanSortImpl(); }

  /// Returns the bitvector size, fails if the sort is not a bitvector
  /// Calls getBitvectorSortSizeImpl().
  virtual unsigned getBitvectorSortSize() const {
    assert(isBitvectorSort() && "Not a bitvector sort!");
    unsigned Size = getBitvectorSortSizeImpl();
    assert(Size && "Size is zero!");
    return Size;
  };

  /// Returns the floating-point size, fails if the sort is not a floating-point
  /// Calls getFloatSortSizeImpl().
  virtual unsigned getFloatSortSize() const {
    assert(isFloatSort() && "Not a floating-point sort!");
    unsigned Size = getFloatSortSizeImpl();
    assert(Size && "Size is zero!");
    return Size;
  };

  virtual void Profile(llvm::FoldingSetNodeID &ID) const = 0;

  bool operator<(const SMTSort &Other) const {
    llvm::FoldingSetNodeID ID1, ID2;
    Profile(ID1);
    Other.Profile(ID2);
    return ID1 < ID2;
  }

  friend bool operator==(SMTSort const &LHS, SMTSort const &RHS) {
    return LHS.equal_to(RHS);
  }

  virtual void print(raw_ostream &OS) const = 0;

  LLVM_DUMP_METHOD void dump() const;

protected:
  /// Query the SMT solver and returns true if two sorts are equal (same kind
  /// and bit width). This does not check if the two sorts are the same objects.
  virtual bool equal_to(SMTSort const &other) const = 0;

  /// Query the SMT solver and checks if a sort is bitvector.
  virtual bool isBitvectorSortImpl() const = 0;

  /// Query the SMT solver and checks if a sort is floating-point.
  virtual bool isFloatSortImpl() const = 0;

  /// Query the SMT solver and checks if a sort is boolean.
  virtual bool isBooleanSortImpl() const = 0;

  /// Query the SMT solver and returns the sort bit width.
  virtual unsigned getBitvectorSortSizeImpl() const = 0;

  /// Query the SMT solver and returns the sort bit width.
  virtual unsigned getFloatSortSizeImpl() const = 0;
};

/// Shared pointer for SMTSorts, used by SMTSolver API.
using SMTSortRef = const SMTSort *;

/// Generic base class for SMT exprs
class SMTExpr {
public:
  SMTExpr() = default;
  virtual ~SMTExpr() = default;

  bool operator<(const SMTExpr &Other) const {
    llvm::FoldingSetNodeID ID1, ID2;
    Profile(ID1);
    Other.Profile(ID2);
    return ID1 < ID2;
  }

  virtual void Profile(llvm::FoldingSetNodeID &ID) const = 0;

  friend bool operator==(SMTExpr const &LHS, SMTExpr const &RHS) {
    return LHS.equal_to(RHS);
  }

  virtual void print(raw_ostream &OS) const = 0;

  LLVM_DUMP_METHOD void dump() const;

protected:
  /// Query the SMT solver and returns true if two sorts are equal (same kind
  /// and bit width). This does not check if the two sorts are the same objects.
  virtual bool equal_to(SMTExpr const &other) const = 0;
};

class SMTSolverStatistics {
public:
  SMTSolverStatistics() = default;
  virtual ~SMTSolverStatistics() = default;

  virtual double getDouble(llvm::StringRef) const = 0;
  virtual unsigned getUnsigned(llvm::StringRef) const = 0;

  virtual void print(raw_ostream &OS) const = 0;

  LLVM_DUMP_METHOD void dump() const;
};

/// Shared pointer for SMTExprs, used by SMTSolver API.
using SMTExprRef = const SMTExpr *;

/// Generic base class for SMT Solvers
///
/// This class is responsible for wrapping all sorts and expression generation,
/// through the mk* methods. It also provides methods to create SMT expressions
/// straight from clang's AST, through the from* methods.
class SMTSolver {
public:
  SMTSolver() = default;
  virtual ~SMTSolver() = default;

  LLVM_DUMP_METHOD void dump() const;

  // Returns an appropriate floating-point sort for the given bitwidth.
  SMTSortRef getFloatSort(unsigned BitWidth) {
    switch (BitWidth) {
    case 16:
      return getFloat16Sort();
    case 32:
      return getFloat32Sort();
    case 64:
      return getFloat64Sort();
    case 128:
      return getFloat128Sort();
    default:;
    }
    llvm_unreachable("Unsupported floating-point bitwidth!");
  }

  // Returns a boolean sort.
  virtual SMTSortRef getBoolSort() = 0;

  // Returns an appropriate bitvector sort for the given bitwidth.
  virtual SMTSortRef getBitvectorSort(const unsigned BitWidth) = 0;

  // Returns a floating-point sort of width 16
  virtual SMTSortRef getFloat16Sort() = 0;

  // Returns a floating-point sort of width 32
  virtual SMTSortRef getFloat32Sort() = 0;

  // Returns a floating-point sort of width 64
  virtual SMTSortRef getFloat64Sort() = 0;

  // Returns a floating-point sort of width 128
  virtual SMTSortRef getFloat128Sort() = 0;

  // Returns an appropriate sort for the given AST.
  virtual SMTSortRef getSort(const SMTExprRef &AST) = 0;

  /// Given a constraint, adds it to the solver
  virtual void addConstraint(const SMTExprRef &Exp) const = 0;

  /// Creates a bitvector addition operation
  virtual SMTExprRef mkBVAdd(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector subtraction operation
  virtual SMTExprRef mkBVSub(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector multiplication operation
  virtual SMTExprRef mkBVMul(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed modulus operation
  virtual SMTExprRef mkBVSRem(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned modulus operation
  virtual SMTExprRef mkBVURem(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed division operation
  virtual SMTExprRef mkBVSDiv(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned division operation
  virtual SMTExprRef mkBVUDiv(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector logical shift left operation
  virtual SMTExprRef mkBVShl(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector arithmetic shift right operation
  virtual SMTExprRef mkBVAshr(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector logical shift right operation
  virtual SMTExprRef mkBVLshr(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector negation operation
  virtual SMTExprRef mkBVNeg(const SMTExprRef &Exp) = 0;

  /// Creates a bitvector not operation
  virtual SMTExprRef mkBVNot(const SMTExprRef &Exp) = 0;

  /// Creates a bitvector xor operation
  virtual SMTExprRef mkBVXor(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector or operation
  virtual SMTExprRef mkBVOr(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector and operation
  virtual SMTExprRef mkBVAnd(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned less-than operation
  virtual SMTExprRef mkBVUlt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed less-than operation
  virtual SMTExprRef mkBVSlt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned greater-than operation
  virtual SMTExprRef mkBVUgt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed greater-than operation
  virtual SMTExprRef mkBVSgt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned less-equal-than operation
  virtual SMTExprRef mkBVUle(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed less-equal-than operation
  virtual SMTExprRef mkBVSle(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned greater-equal-than operation
  virtual SMTExprRef mkBVUge(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed greater-equal-than operation
  virtual SMTExprRef mkBVSge(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a boolean not operation
  virtual SMTExprRef mkNot(const SMTExprRef &Exp) = 0;

  /// Creates a boolean equality operation
  virtual SMTExprRef mkEqual(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a boolean and operation
  virtual SMTExprRef mkAnd(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a boolean or operation
  virtual SMTExprRef mkOr(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a boolean ite operation
  virtual SMTExprRef mkIte(const SMTExprRef &Cond, const SMTExprRef &T,
                           const SMTExprRef &F) = 0;

  /// Creates a bitvector sign extension operation
  virtual SMTExprRef mkBVSignExt(unsigned i, const SMTExprRef &Exp) = 0;

  /// Creates a bitvector zero extension operation
  virtual SMTExprRef mkBVZeroExt(unsigned i, const SMTExprRef &Exp) = 0;

  /// Creates a bitvector extract operation
  virtual SMTExprRef mkBVExtract(unsigned High, unsigned Low,
                                 const SMTExprRef &Exp) = 0;

  /// Creates a bitvector concat operation
  virtual SMTExprRef mkBVConcat(const SMTExprRef &LHS,
                                const SMTExprRef &RHS) = 0;

  /// Creates a predicate that checks for overflow in a bitvector addition
  /// operation
  virtual SMTExprRef mkBVAddNoOverflow(const SMTExprRef &LHS,
                                       const SMTExprRef &RHS,
                                       bool isSigned) = 0;

  /// Creates a predicate that checks for underflow in a signed bitvector
  /// addition operation
  virtual SMTExprRef mkBVAddNoUnderflow(const SMTExprRef &LHS,
                                        const SMTExprRef &RHS) = 0;

  /// Creates a predicate that checks for overflow in a signed bitvector
  /// subtraction operation
  virtual SMTExprRef mkBVSubNoOverflow(const SMTExprRef &LHS,
                                       const SMTExprRef &RHS) = 0;

  /// Creates a predicate that checks for underflow in a bitvector subtraction
  /// operation
  virtual SMTExprRef mkBVSubNoUnderflow(const SMTExprRef &LHS,
                                        const SMTExprRef &RHS,
                                        bool isSigned) = 0;

  /// Creates a predicate that checks for overflow in a signed bitvector
  /// division/modulus operation
  virtual SMTExprRef mkBVSDivNoOverflow(const SMTExprRef &LHS,
                                        const SMTExprRef &RHS) = 0;

  /// Creates a predicate that checks for overflow in a bitvector negation
  /// operation
  virtual SMTExprRef mkBVNegNoOverflow(const SMTExprRef &Exp) = 0;

  /// Creates a predicate that checks for overflow in a bitvector multiplication
  /// operation
  virtual SMTExprRef mkBVMulNoOverflow(const SMTExprRef &LHS,
                                       const SMTExprRef &RHS,
                                       bool isSigned) = 0;

  /// Creates a predicate that checks for underflow in a signed bitvector
  /// multiplication operation
  virtual SMTExprRef mkBVMulNoUnderflow(const SMTExprRef &LHS,
                                        const SMTExprRef &RHS) = 0;

  /// Creates a floating-point negation operation
  virtual SMTExprRef mkFPNeg(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point isInfinite operation
  virtual SMTExprRef mkFPIsInfinite(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point isNaN operation
  virtual SMTExprRef mkFPIsNaN(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point isNormal operation
  virtual SMTExprRef mkFPIsNormal(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point isZero operation
  virtual SMTExprRef mkFPIsZero(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point multiplication operation
  virtual SMTExprRef mkFPMul(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point division operation
  virtual SMTExprRef mkFPDiv(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point remainder operation
  virtual SMTExprRef mkFPRem(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point addition operation
  virtual SMTExprRef mkFPAdd(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point subtraction operation
  virtual SMTExprRef mkFPSub(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point less-than operation
  virtual SMTExprRef mkFPLt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point greater-than operation
  virtual SMTExprRef mkFPGt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point less-than-or-equal operation
  virtual SMTExprRef mkFPLe(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point greater-than-or-equal operation
  virtual SMTExprRef mkFPGe(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point equality operation
  virtual SMTExprRef mkFPEqual(const SMTExprRef &LHS,
                               const SMTExprRef &RHS) = 0;

  /// Creates a floating-point conversion from floatint-point to floating-point
  /// operation
  virtual SMTExprRef mkFPtoFP(const SMTExprRef &From, const SMTSortRef &To) = 0;

  /// Creates a floating-point conversion from signed bitvector to
  /// floatint-point operation
  virtual SMTExprRef mkSBVtoFP(const SMTExprRef &From,
                               const SMTSortRef &To) = 0;

  /// Creates a floating-point conversion from unsigned bitvector to
  /// floatint-point operation
  virtual SMTExprRef mkUBVtoFP(const SMTExprRef &From,
                               const SMTSortRef &To) = 0;

  /// Creates a floating-point conversion from floatint-point to signed
  /// bitvector operation
  virtual SMTExprRef mkFPtoSBV(const SMTExprRef &From, unsigned ToWidth) = 0;

  /// Creates a floating-point conversion from floatint-point to unsigned
  /// bitvector operation
  virtual SMTExprRef mkFPtoUBV(const SMTExprRef &From, unsigned ToWidth) = 0;

  /// Creates a new symbol, given a name and a sort
  virtual SMTExprRef mkSymbol(const char *Name, SMTSortRef Sort) = 0;

  // Returns an appropriate floating-point rounding mode.
  virtual SMTExprRef getFloatRoundingMode() = 0;

  // If the a model is available, returns the value of a given bitvector symbol
  virtual llvm::APSInt getBitvector(const SMTExprRef &Exp, unsigned BitWidth,
                                    bool isUnsigned) = 0;

  // If the a model is available, returns the value of a given boolean symbol
  virtual bool getBoolean(const SMTExprRef &Exp) = 0;

  /// Constructs an SMTExprRef from a boolean.
  virtual SMTExprRef mkBoolean(const bool b) = 0;

  /// Constructs an SMTExprRef from a finite APFloat.
  virtual SMTExprRef mkFloat(const llvm::APFloat Float) = 0;

  /// Constructs an SMTExprRef from an APSInt and its bit width
  virtual SMTExprRef mkBitvector(const llvm::APSInt Int, unsigned BitWidth) = 0;

  /// Given an expression, extract the value of this operand in the model.
  virtual bool getInterpretation(const SMTExprRef &Exp, llvm::APSInt &Int) = 0;

  /// Given an expression extract the value of this operand in the model.
  virtual bool getInterpretation(const SMTExprRef &Exp,
                                 llvm::APFloat &Float) = 0;

  /// Check if the constraints are satisfiable
  virtual std::optional<bool> check() const = 0;

  /// Push the current solver state
  virtual void push() = 0;

  /// Pop the previous solver state
  virtual void pop(unsigned NumStates = 1) = 0;

  /// Reset the solver and remove all constraints.
  virtual void reset() = 0;

  /// Checks if the solver supports floating-points.
  virtual bool isFPSupported() = 0;

  virtual void print(raw_ostream &OS) const = 0;

  /// Sets the requested option.
  virtual void setBoolParam(StringRef Key, bool Value) = 0;
  virtual void setUnsignedParam(StringRef Key, unsigned Value) = 0;

  virtual std::unique_ptr<SMTSolverStatistics> getStatistics() const = 0;
};

/// Shared pointer for SMTSolvers.
using SMTSolverRef = std::shared_ptr<SMTSolver>;

/// Convenience method to create and Z3Solver object
SMTSolverRef CreateZ3Solver();

} // namespace llvm

#endif
