//===- ConstraintManager.h - Constraints on symbolic values. ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defined the interface to manage constraints on symbolic values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CONSTRAINTMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CONSTRAINTMANAGER_H

#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/Support/SaveAndRestore.h"
#include <memory>
#include <optional>
#include <utility>

namespace llvm {

class APSInt;

} // namespace llvm

namespace clang {
namespace ento {

class ProgramStateManager;
class ExprEngine;
class SymbolReaper;

class ConditionTruthVal {
  std::optional<bool> Val;

public:
  /// Construct a ConditionTruthVal indicating the constraint is constrained
  /// to either true or false, depending on the boolean value provided.
  ConditionTruthVal(bool constraint) : Val(constraint) {}

  /// Construct a ConstraintVal indicating the constraint is underconstrained.
  ConditionTruthVal() = default;

  /// \return Stored value, assuming that the value is known.
  /// Crashes otherwise.
  bool getValue() const {
    return *Val;
  }

  /// Return true if the constraint is perfectly constrained to 'true'.
  bool isConstrainedTrue() const { return Val && *Val; }

  /// Return true if the constraint is perfectly constrained to 'false'.
  bool isConstrainedFalse() const { return Val && !*Val; }

  /// Return true if the constrained is perfectly constrained.
  bool isConstrained() const { return Val.has_value(); }

  /// Return true if the constrained is underconstrained and we do not know
  /// if the constraint is true of value.
  bool isUnderconstrained() const { return !Val.has_value(); }
};

class ConstraintManager {
public:
  ConstraintManager() = default;
  virtual ~ConstraintManager();

  virtual bool haveEqualConstraints(ProgramStateRef S1,
                                    ProgramStateRef S2) const = 0;

  ProgramStateRef assume(ProgramStateRef state, DefinedSVal Cond,
                         bool Assumption);

  using ProgramStatePair = std::pair<ProgramStateRef, ProgramStateRef>;

  /// Returns a pair of states (StTrue, StFalse) where the given condition is
  /// assumed to be true or false, respectively.
  /// (Note that these two states might be equal if the parent state turns out
  /// to be infeasible. This may happen if the underlying constraint solver is
  /// not perfectly precise and this may happen very rarely.)
  ProgramStatePair assumeDual(ProgramStateRef State, DefinedSVal Cond);

  ProgramStateRef assumeInclusiveRange(ProgramStateRef State, NonLoc Value,
                                       const llvm::APSInt &From,
                                       const llvm::APSInt &To, bool InBound);

  /// Returns a pair of states (StInRange, StOutOfRange) where the given value
  /// is assumed to be in the range or out of the range, respectively.
  /// (Note that these two states might be equal if the parent state turns out
  /// to be infeasible. This may happen if the underlying constraint solver is
  /// not perfectly precise and this may happen very rarely.)
  ProgramStatePair assumeInclusiveRangeDual(ProgramStateRef State, NonLoc Value,
                                            const llvm::APSInt &From,
                                            const llvm::APSInt &To);

  /// If a symbol is perfectly constrained to a constant, attempt
  /// to return the concrete value.
  ///
  /// Note that a ConstraintManager is not obligated to return a concretized
  /// value for a symbol, even if it is perfectly constrained.
  /// It might return null.
  virtual const llvm::APSInt* getSymVal(ProgramStateRef state,
                                        SymbolRef sym) const {
    return nullptr;
  }

  /// Attempt to return the minimal possible value for a given symbol. Note
  /// that a ConstraintManager is not obligated to return a lower bound, it may
  /// also return nullptr.
  virtual const llvm::APSInt *getSymMinVal(ProgramStateRef state,
                                           SymbolRef sym) const {
    return nullptr;
  }

  /// Attempt to return the minimal possible value for a given symbol. Note
  /// that a ConstraintManager is not obligated to return a lower bound, it may
  /// also return nullptr.
  virtual const llvm::APSInt *getSymMaxVal(ProgramStateRef state,
                                           SymbolRef sym) const {
    return nullptr;
  }

  /// Scan all symbols referenced by the constraints. If the symbol is not
  /// alive, remove it.
  virtual ProgramStateRef removeDeadBindings(ProgramStateRef state,
                                                 SymbolReaper& SymReaper) = 0;

  virtual void printJson(raw_ostream &Out, ProgramStateRef State,
                         const char *NL, unsigned int Space,
                         bool IsDot) const = 0;

  virtual void printValue(raw_ostream &Out, ProgramStateRef State,
                          SymbolRef Sym) {}

  /// Convenience method to query the state to see if a symbol is null or
  /// not null, or if neither assumption can be made.
  ConditionTruthVal isNull(ProgramStateRef State, SymbolRef Sym) {
    return checkNull(State, Sym);
  }

protected:
  /// A helper class to simulate the call stack of nested assume calls.
  class AssumeStackTy {
  public:
    void push(const ProgramState *S) { Aux.push_back(S); }
    void pop() { Aux.pop_back(); }
    bool contains(const ProgramState *S) const {
      return llvm::is_contained(Aux, S);
    }

  private:
    llvm::SmallVector<const ProgramState *, 4> Aux;
  };
  AssumeStackTy AssumeStack;

  virtual ProgramStateRef assumeInternal(ProgramStateRef state,
                                         DefinedSVal Cond, bool Assumption) = 0;

  virtual ProgramStateRef assumeInclusiveRangeInternal(ProgramStateRef State,
                                                       NonLoc Value,
                                                       const llvm::APSInt &From,
                                                       const llvm::APSInt &To,
                                                       bool InBound) = 0;

  /// canReasonAbout - Not all ConstraintManagers can accurately reason about
  ///  all SVal values.  This method returns true if the ConstraintManager can
  ///  reasonably handle a given SVal value.  This is typically queried by
  ///  ExprEngine to determine if the value should be replaced with a
  ///  conjured symbolic value in order to recover some precision.
  virtual bool canReasonAbout(SVal X) const = 0;

  /// Returns whether or not a symbol is known to be null ("true"), known to be
  /// non-null ("false"), or may be either ("underconstrained").
  virtual ConditionTruthVal checkNull(ProgramStateRef State, SymbolRef Sym);

  template <typename AssumeFunction>
  ProgramStatePair assumeDualImpl(ProgramStateRef &State,
                                  AssumeFunction &Assume);
};

std::unique_ptr<ConstraintManager>
CreateRangeConstraintManager(ProgramStateManager &statemgr,
                             ExprEngine *exprengine);

std::unique_ptr<ConstraintManager>
CreateZ3ConstraintManager(ProgramStateManager &statemgr,
                          ExprEngine *exprengine);

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CONSTRAINTMANAGER_H
