//===- WatchedLiteralsSolver.cpp --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a SAT solver implementation that can be used by dataflow
//  analyses.
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <vector>

#include "clang/Analysis/FlowSensitive/CNFFormula.h"
#include "clang/Analysis/FlowSensitive/Formula.h"
#include "clang/Analysis/FlowSensitive/Solver.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"


namespace clang {
namespace dataflow {

namespace {

class WatchedLiteralsSolverImpl {
  /// Stores the variable identifier and Atom for atomic booleans in the
  /// formula.
  llvm::DenseMap<Variable, Atom> Atomics;

  /// A boolean formula in conjunctive normal form that the solver will attempt
  /// to prove satisfiable. The formula will be modified in the process.
  CNFFormula CNF;

  /// Maps literals (indices of the vector) to clause identifiers (elements of
  /// the vector) that watch the respective literals.
  ///
  /// For a given clause, its watched literal is always its first literal in
  /// `Clauses`. This invariant is maintained when watched literals change.
  std::vector<ClauseID> WatchedHead;

  /// Maps clause identifiers (elements of the vector) to identifiers of other
  /// clauses that watch the same literals, forming a set of linked lists.
  ///
  /// The element at index 0 stands for the identifier of the clause that
  /// follows the null clause. It is set to 0 and isn't used. Identifiers of
  /// clauses in the formula start from the element at index 1.
  std::vector<ClauseID> NextWatched;

  /// The search for a satisfying assignment of the variables in `Formula` will
  /// proceed in levels, starting from 1 and going up to `Formula.LargestVar`
  /// (inclusive). The current level is stored in `Level`. At each level the
  /// solver will assign a value to an unassigned variable. If this leads to a
  /// consistent partial assignment, `Level` will be incremented. Otherwise, if
  /// it results in a conflict, the solver will backtrack by decrementing
  /// `Level` until it reaches the most recent level where a decision was made.
  size_t Level = 0;

  /// Maps levels (indices of the vector) to variables (elements of the vector)
  /// that are assigned values at the respective levels.
  ///
  /// The element at index 0 isn't used. Variables start from the element at
  /// index 1.
  std::vector<Variable> LevelVars;

  /// State of the solver at a particular level.
  enum class State : uint8_t {
    /// Indicates that the solver made a decision.
    Decision = 0,

    /// Indicates that the solver made a forced move.
    Forced = 1,
  };

  /// State of the solver at a particular level. It keeps track of previous
  /// decisions that the solver can refer to when backtracking.
  ///
  /// The element at index 0 isn't used. States start from the element at index
  /// 1.
  std::vector<State> LevelStates;

  enum class Assignment : int8_t {
    Unassigned = -1,
    AssignedFalse = 0,
    AssignedTrue = 1
  };

  /// Maps variables (indices of the vector) to their assignments (elements of
  /// the vector).
  ///
  /// The element at index 0 isn't used. Variable assignments start from the
  /// element at index 1.
  std::vector<Assignment> VarAssignments;

  /// A set of unassigned variables that appear in watched literals in
  /// `Formula`. The vector is guaranteed to contain unique elements.
  std::vector<Variable> ActiveVars;

public:
  explicit WatchedLiteralsSolverImpl(
      const llvm::ArrayRef<const Formula *> &Vals)
      // `Atomics` needs to be initialized first so that we can use it as an
      // output argument of `buildCNF()`.
      : Atomics(), CNF(buildCNF(Vals, Atomics)),
        LevelVars(CNF.largestVar() + 1), LevelStates(CNF.largestVar() + 1) {
    assert(!Vals.empty());

    // Skip initialization if the formula is known to be contradictory.
    if (CNF.knownContradictory())
      return;

    // Initialize `NextWatched` and `WatchedHead`.
    NextWatched.push_back(0);
    const size_t NumLiterals = 2 * CNF.largestVar() + 1;
    WatchedHead.resize(NumLiterals + 1, 0);
    for (ClauseID C = 1; C <= CNF.numClauses(); ++C) {
      // Designate the first literal as the "watched" literal of the clause.
      Literal FirstLit = CNF.clauseLiterals(C).front();
      NextWatched.push_back(WatchedHead[FirstLit]);
      WatchedHead[FirstLit] = C;
    }

    // Initialize the state at the root level to a decision so that in
    // `reverseForcedMoves` we don't have to check that `Level >= 0` on each
    // iteration.
    LevelStates[0] = State::Decision;

    // Initialize all variables as unassigned.
    VarAssignments.resize(CNF.largestVar() + 1, Assignment::Unassigned);

    // Initialize the active variables.
    for (Variable Var = CNF.largestVar(); Var != NullVar; --Var) {
      if (isWatched(posLit(Var)) || isWatched(negLit(Var)))
        ActiveVars.push_back(Var);
    }
  }

  // Returns the `Result` and the number of iterations "remaining" from
  // `MaxIterations` (that is, `MaxIterations` - iterations in this call).
  std::pair<Solver::Result, std::int64_t> solve(std::int64_t MaxIterations) && {
    if (CNF.knownContradictory()) {
      // Short-cut the solving process. We already found out at CNF
      // construction time that the formula is unsatisfiable.
      return std::make_pair(Solver::Result::Unsatisfiable(), MaxIterations);
    }
    size_t I = 0;
    while (I < ActiveVars.size()) {
      if (MaxIterations == 0)
        return std::make_pair(Solver::Result::TimedOut(), 0);
      --MaxIterations;

      // Assert that the following invariants hold:
      // 1. All active variables are unassigned.
      // 2. All active variables form watched literals.
      // 3. Unassigned variables that form watched literals are active.
      // FIXME: Consider replacing these with test cases that fail if the any
      // of the invariants is broken. That might not be easy due to the
      // transformations performed by `buildCNF`.
      assert(activeVarsAreUnassigned());
      assert(activeVarsFormWatchedLiterals());
      assert(unassignedVarsFormingWatchedLiteralsAreActive());

      const Variable ActiveVar = ActiveVars[I];

      // Look for unit clauses that contain the active variable.
      const bool unitPosLit = watchedByUnitClause(posLit(ActiveVar));
      const bool unitNegLit = watchedByUnitClause(negLit(ActiveVar));
      if (unitPosLit && unitNegLit) {
        // We found a conflict!

        // Backtrack and rewind the `Level` until the most recent non-forced
        // assignment.
        reverseForcedMoves();

        // If the root level is reached, then all possible assignments lead to
        // a conflict.
        if (Level == 0)
          return std::make_pair(Solver::Result::Unsatisfiable(), MaxIterations);

        // Otherwise, take the other branch at the most recent level where a
        // decision was made.
        LevelStates[Level] = State::Forced;
        const Variable Var = LevelVars[Level];
        VarAssignments[Var] = VarAssignments[Var] == Assignment::AssignedTrue
                                  ? Assignment::AssignedFalse
                                  : Assignment::AssignedTrue;

        updateWatchedLiterals();
      } else if (unitPosLit || unitNegLit) {
        // We found a unit clause! The value of its unassigned variable is
        // forced.
        ++Level;

        LevelVars[Level] = ActiveVar;
        LevelStates[Level] = State::Forced;
        VarAssignments[ActiveVar] =
            unitPosLit ? Assignment::AssignedTrue : Assignment::AssignedFalse;

        // Remove the variable that was just assigned from the set of active
        // variables.
        if (I + 1 < ActiveVars.size()) {
          // Replace the variable that was just assigned with the last active
          // variable for efficient removal.
          ActiveVars[I] = ActiveVars.back();
        } else {
          // This was the last active variable. Repeat the process from the
          // beginning.
          I = 0;
        }
        ActiveVars.pop_back();

        updateWatchedLiterals();
      } else if (I + 1 == ActiveVars.size()) {
        // There are no remaining unit clauses in the formula! Make a decision
        // for one of the active variables at the current level.
        ++Level;

        LevelVars[Level] = ActiveVar;
        LevelStates[Level] = State::Decision;
        VarAssignments[ActiveVar] = decideAssignment(ActiveVar);

        // Remove the variable that was just assigned from the set of active
        // variables.
        ActiveVars.pop_back();

        updateWatchedLiterals();

        // This was the last active variable. Repeat the process from the
        // beginning.
        I = 0;
      } else {
        ++I;
      }
    }
    return std::make_pair(Solver::Result::Satisfiable(buildSolution()),
                          MaxIterations);
  }

private:
  /// Returns a satisfying truth assignment to the atoms in the boolean formula.
  llvm::DenseMap<Atom, Solver::Result::Assignment> buildSolution() {
    llvm::DenseMap<Atom, Solver::Result::Assignment> Solution;
    for (auto &Atomic : Atomics) {
      // A variable may have a definite true/false assignment, or it may be
      // unassigned indicating its truth value does not affect the result of
      // the formula. Unassigned variables are assigned to true as a default.
      Solution[Atomic.second] =
          VarAssignments[Atomic.first] == Assignment::AssignedFalse
              ? Solver::Result::Assignment::AssignedFalse
              : Solver::Result::Assignment::AssignedTrue;
    }
    return Solution;
  }

  /// Reverses forced moves until the most recent level where a decision was
  /// made on the assignment of a variable.
  void reverseForcedMoves() {
    for (; LevelStates[Level] == State::Forced; --Level) {
      const Variable Var = LevelVars[Level];

      VarAssignments[Var] = Assignment::Unassigned;

      // If the variable that we pass through is watched then we add it to the
      // active variables.
      if (isWatched(posLit(Var)) || isWatched(negLit(Var)))
        ActiveVars.push_back(Var);
    }
  }

  /// Updates watched literals that are affected by a variable assignment.
  void updateWatchedLiterals() {
    const Variable Var = LevelVars[Level];

    // Update the watched literals of clauses that currently watch the literal
    // that falsifies `Var`.
    const Literal FalseLit = VarAssignments[Var] == Assignment::AssignedTrue
                                 ? negLit(Var)
                                 : posLit(Var);
    ClauseID FalseLitWatcher = WatchedHead[FalseLit];
    WatchedHead[FalseLit] = NullClause;
    while (FalseLitWatcher != NullClause) {
      const ClauseID NextFalseLitWatcher = NextWatched[FalseLitWatcher];

      // Pick the first non-false literal as the new watched literal.
      const CNFFormula::Iterator FalseLitWatcherStart =
          CNF.startOfClause(FalseLitWatcher);
      CNFFormula::Iterator NewWatchedLitIter = FalseLitWatcherStart.next();
      while (isCurrentlyFalse(*NewWatchedLitIter))
        ++NewWatchedLitIter;
      const Literal NewWatchedLit = *NewWatchedLitIter;
      const Variable NewWatchedLitVar = var(NewWatchedLit);

      // Swap the old watched literal for the new one in `FalseLitWatcher` to
      // maintain the invariant that the watched literal is at the beginning of
      // the clause.
      *NewWatchedLitIter = FalseLit;
      *FalseLitWatcherStart = NewWatchedLit;

      // If the new watched literal isn't watched by any other clause and its
      // variable isn't assigned we need to add it to the active variables.
      if (!isWatched(NewWatchedLit) && !isWatched(notLit(NewWatchedLit)) &&
          VarAssignments[NewWatchedLitVar] == Assignment::Unassigned)
        ActiveVars.push_back(NewWatchedLitVar);

      NextWatched[FalseLitWatcher] = WatchedHead[NewWatchedLit];
      WatchedHead[NewWatchedLit] = FalseLitWatcher;

      // Go to the next clause that watches `FalseLit`.
      FalseLitWatcher = NextFalseLitWatcher;
    }
  }

  /// Returns true if and only if one of the clauses that watch `Lit` is a unit
  /// clause.
  bool watchedByUnitClause(Literal Lit) const {
    for (ClauseID LitWatcher = WatchedHead[Lit]; LitWatcher != NullClause;
         LitWatcher = NextWatched[LitWatcher]) {
      llvm::ArrayRef<Literal> Clause = CNF.clauseLiterals(LitWatcher);

      // Assert the invariant that the watched literal is always the first one
      // in the clause.
      // FIXME: Consider replacing this with a test case that fails if the
      // invariant is broken by `updateWatchedLiterals`. That might not be easy
      // due to the transformations performed by `buildCNF`.
      assert(Clause.front() == Lit);

      if (isUnit(Clause))
        return true;
    }
    return false;
  }

  /// Returns true if and only if `Clause` is a unit clause.
  bool isUnit(llvm::ArrayRef<Literal> Clause) const {
    return llvm::all_of(Clause.drop_front(),
                        [this](Literal L) { return isCurrentlyFalse(L); });
  }

  /// Returns true if and only if `Lit` evaluates to `false` in the current
  /// partial assignment.
  bool isCurrentlyFalse(Literal Lit) const {
    return static_cast<int8_t>(VarAssignments[var(Lit)]) ==
           static_cast<int8_t>(Lit & 1);
  }

  /// Returns true if and only if `Lit` is watched by a clause in `Formula`.
  bool isWatched(Literal Lit) const { return WatchedHead[Lit] != NullClause; }

  /// Returns an assignment for an unassigned variable.
  Assignment decideAssignment(Variable Var) const {
    return !isWatched(posLit(Var)) || isWatched(negLit(Var))
               ? Assignment::AssignedFalse
               : Assignment::AssignedTrue;
  }

  /// Returns a set of all watched literals.
  llvm::DenseSet<Literal> watchedLiterals() const {
    llvm::DenseSet<Literal> WatchedLiterals;
    for (Literal Lit = 2; Lit < WatchedHead.size(); Lit++) {
      if (WatchedHead[Lit] == NullClause)
        continue;
      WatchedLiterals.insert(Lit);
    }
    return WatchedLiterals;
  }

  /// Returns true if and only if all active variables are unassigned.
  bool activeVarsAreUnassigned() const {
    return llvm::all_of(ActiveVars, [this](Variable Var) {
      return VarAssignments[Var] == Assignment::Unassigned;
    });
  }

  /// Returns true if and only if all active variables form watched literals.
  bool activeVarsFormWatchedLiterals() const {
    const llvm::DenseSet<Literal> WatchedLiterals = watchedLiterals();
    return llvm::all_of(ActiveVars, [&WatchedLiterals](Variable Var) {
      return WatchedLiterals.contains(posLit(Var)) ||
             WatchedLiterals.contains(negLit(Var));
    });
  }

  /// Returns true if and only if all unassigned variables that are forming
  /// watched literals are active.
  bool unassignedVarsFormingWatchedLiteralsAreActive() const {
    const llvm::DenseSet<Variable> ActiveVarsSet(ActiveVars.begin(),
                                                 ActiveVars.end());
    for (Literal Lit : watchedLiterals()) {
      const Variable Var = var(Lit);
      if (VarAssignments[Var] != Assignment::Unassigned)
        continue;
      if (ActiveVarsSet.contains(Var))
        continue;
      return false;
    }
    return true;
  }
};

} // namespace

Solver::Result
WatchedLiteralsSolver::solve(llvm::ArrayRef<const Formula *> Vals) {
  if (Vals.empty())
    return Solver::Result::Satisfiable({{}});
  auto [Res, Iterations] = WatchedLiteralsSolverImpl(Vals).solve(MaxIterations);
  MaxIterations = Iterations;
  return Res;
}

} // namespace dataflow
} // namespace clang
