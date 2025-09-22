//===- CNFFormula.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  A representation of a boolean formula in 3-CNF.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/CNFFormula.h"
#include "llvm/ADT/DenseSet.h"

#include <queue>

namespace clang {
namespace dataflow {

namespace {

/// Applies simplifications while building up a BooleanFormula.
/// We keep track of unit clauses, which tell us variables that must be
/// true/false in any model that satisfies the overall formula.
/// Such variables can be dropped from subsequently-added clauses, which
/// may in turn yield more unit clauses or even a contradiction.
/// The total added complexity of this preprocessing is O(N) where we
/// for every clause, we do a lookup for each unit clauses.
/// The lookup is O(1) on average. This method won't catch all
/// contradictory formulas, more passes can in principle catch
/// more cases but we leave all these and the general case to the
/// proper SAT solver.
struct CNFFormulaBuilder {
  // Formula should outlive CNFFormulaBuilder.
  explicit CNFFormulaBuilder(CNFFormula &CNF) : Formula(CNF) {}

  /// Adds the `L1 v ... v Ln` clause to the formula. Applies
  /// simplifications, based on single-literal clauses.
  ///
  /// Requirements:
  ///
  ///  `Li` must not be `NullLit`.
  ///
  ///  All literals must be distinct.
  void addClause(ArrayRef<Literal> Literals) {
    // We generate clauses with up to 3 literals in this file.
    assert(!Literals.empty() && Literals.size() <= 3);
    // Contains literals of the simplified clause.
    llvm::SmallVector<Literal> Simplified;
    for (auto L : Literals) {
      assert(L != NullLit &&
             llvm::all_of(Simplified, [L](Literal S) { return S != L; }));
      auto X = var(L);
      if (trueVars.contains(X)) { // X must be true
        if (isPosLit(L))
          return; // Omit clause `(... v X v ...)`, it is `true`.
        else
          continue; // Omit `!X` from `(... v !X v ...)`.
      }
      if (falseVars.contains(X)) { // X must be false
        if (isNegLit(L))
          return; // Omit clause `(... v !X v ...)`, it is `true`.
        else
          continue; // Omit `X` from `(... v X v ...)`.
      }
      Simplified.push_back(L);
    }
    if (Simplified.empty()) {
      // Simplification made the clause empty, which is equivalent to `false`.
      // We already know that this formula is unsatisfiable.
      Formula.addClause(Simplified);
      return;
    }
    if (Simplified.size() == 1) {
      // We have new unit clause.
      const Literal lit = Simplified.front();
      const Variable v = var(lit);
      if (isPosLit(lit))
        trueVars.insert(v);
      else
        falseVars.insert(v);
    }
    Formula.addClause(Simplified);
  }

  /// Returns true if we observed a contradiction while adding clauses.
  /// In this case then the formula is already known to be unsatisfiable.
  bool isKnownContradictory() { return Formula.knownContradictory(); }

private:
  CNFFormula &Formula;
  llvm::DenseSet<Variable> trueVars;
  llvm::DenseSet<Variable> falseVars;
};

} // namespace

CNFFormula::CNFFormula(Variable LargestVar)
    : LargestVar(LargestVar), KnownContradictory(false) {
  Clauses.push_back(0);
  ClauseStarts.push_back(0);
}

void CNFFormula::addClause(ArrayRef<Literal> lits) {
  assert(llvm::all_of(lits, [](Literal L) { return L != NullLit; }));

  if (lits.empty())
    KnownContradictory = true;

  const size_t S = Clauses.size();
  ClauseStarts.push_back(S);
  Clauses.insert(Clauses.end(), lits.begin(), lits.end());
}

CNFFormula buildCNF(const llvm::ArrayRef<const Formula *> &Formulas,
                    llvm::DenseMap<Variable, Atom> &Atomics) {
  // The general strategy of the algorithm implemented below is to map each
  // of the sub-values in `Vals` to a unique variable and use these variables in
  // the resulting CNF expression to avoid exponential blow up. The number of
  // literals in the resulting formula is guaranteed to be linear in the number
  // of sub-formulas in `Vals`.

  // Map each sub-formula in `Vals` to a unique variable.
  llvm::DenseMap<const Formula *, Variable> FormulaToVar;
  // Store variable identifiers and Atom of atomic booleans.
  Variable NextVar = 1;
  {
    std::queue<const Formula *> UnprocessedFormulas;
    for (const Formula *F : Formulas)
      UnprocessedFormulas.push(F);
    while (!UnprocessedFormulas.empty()) {
      Variable Var = NextVar;
      const Formula *F = UnprocessedFormulas.front();
      UnprocessedFormulas.pop();

      if (!FormulaToVar.try_emplace(F, Var).second)
        continue;
      ++NextVar;

      for (const Formula *Op : F->operands())
        UnprocessedFormulas.push(Op);
      if (F->kind() == Formula::AtomRef)
        Atomics[Var] = F->getAtom();
    }
  }

  auto GetVar = [&FormulaToVar](const Formula *F) {
    auto ValIt = FormulaToVar.find(F);
    assert(ValIt != FormulaToVar.end());
    return ValIt->second;
  };

  CNFFormula CNF(NextVar - 1);
  std::vector<bool> ProcessedSubVals(NextVar, false);
  CNFFormulaBuilder builder(CNF);

  // Add a conjunct for each variable that represents a top-level conjunction
  // value in `Vals`.
  for (const Formula *F : Formulas)
    builder.addClause(posLit(GetVar(F)));

  // Add conjuncts that represent the mapping between newly-created variables
  // and their corresponding sub-formulas.
  std::queue<const Formula *> UnprocessedFormulas;
  for (const Formula *F : Formulas)
    UnprocessedFormulas.push(F);
  while (!UnprocessedFormulas.empty()) {
    const Formula *F = UnprocessedFormulas.front();
    UnprocessedFormulas.pop();
    const Variable Var = GetVar(F);

    if (ProcessedSubVals[Var])
      continue;
    ProcessedSubVals[Var] = true;

    switch (F->kind()) {
    case Formula::AtomRef:
      break;
    case Formula::Literal:
      CNF.addClause(F->literal() ? posLit(Var) : negLit(Var));
      break;
    case Formula::And: {
      const Variable LHS = GetVar(F->operands()[0]);
      const Variable RHS = GetVar(F->operands()[1]);

      if (LHS == RHS) {
        // `X <=> (A ^ A)` is equivalent to `(!X v A) ^ (X v !A)` which is
        // already in conjunctive normal form. Below we add each of the
        // conjuncts of the latter expression to the result.
        builder.addClause({negLit(Var), posLit(LHS)});
        builder.addClause({posLit(Var), negLit(LHS)});
      } else {
        // `X <=> (A ^ B)` is equivalent to `(!X v A) ^ (!X v B) ^ (X v !A v
        // !B)` which is already in conjunctive normal form. Below we add each
        // of the conjuncts of the latter expression to the result.
        builder.addClause({negLit(Var), posLit(LHS)});
        builder.addClause({negLit(Var), posLit(RHS)});
        builder.addClause({posLit(Var), negLit(LHS), negLit(RHS)});
      }
      break;
    }
    case Formula::Or: {
      const Variable LHS = GetVar(F->operands()[0]);
      const Variable RHS = GetVar(F->operands()[1]);

      if (LHS == RHS) {
        // `X <=> (A v A)` is equivalent to `(!X v A) ^ (X v !A)` which is
        // already in conjunctive normal form. Below we add each of the
        // conjuncts of the latter expression to the result.
        builder.addClause({negLit(Var), posLit(LHS)});
        builder.addClause({posLit(Var), negLit(LHS)});
      } else {
        // `X <=> (A v B)` is equivalent to `(!X v A v B) ^ (X v !A) ^ (X v
        // !B)` which is already in conjunctive normal form. Below we add each
        // of the conjuncts of the latter expression to the result.
        builder.addClause({negLit(Var), posLit(LHS), posLit(RHS)});
        builder.addClause({posLit(Var), negLit(LHS)});
        builder.addClause({posLit(Var), negLit(RHS)});
      }
      break;
    }
    case Formula::Not: {
      const Variable Operand = GetVar(F->operands()[0]);

      // `X <=> !Y` is equivalent to `(!X v !Y) ^ (X v Y)` which is
      // already in conjunctive normal form. Below we add each of the
      // conjuncts of the latter expression to the result.
      builder.addClause({negLit(Var), negLit(Operand)});
      builder.addClause({posLit(Var), posLit(Operand)});
      break;
    }
    case Formula::Implies: {
      const Variable LHS = GetVar(F->operands()[0]);
      const Variable RHS = GetVar(F->operands()[1]);

      // `X <=> (A => B)` is equivalent to
      // `(X v A) ^ (X v !B) ^ (!X v !A v B)` which is already in
      // conjunctive normal form. Below we add each of the conjuncts of
      // the latter expression to the result.
      builder.addClause({posLit(Var), posLit(LHS)});
      builder.addClause({posLit(Var), negLit(RHS)});
      builder.addClause({negLit(Var), negLit(LHS), posLit(RHS)});
      break;
    }
    case Formula::Equal: {
      const Variable LHS = GetVar(F->operands()[0]);
      const Variable RHS = GetVar(F->operands()[1]);

      if (LHS == RHS) {
        // `X <=> (A <=> A)` is equivalent to `X` which is already in
        // conjunctive normal form. Below we add each of the conjuncts of the
        // latter expression to the result.
        builder.addClause(posLit(Var));

        // No need to visit the sub-values of `Val`.
        continue;
      }
      // `X <=> (A <=> B)` is equivalent to
      // `(X v A v B) ^ (X v !A v !B) ^ (!X v A v !B) ^ (!X v !A v B)` which
      // is already in conjunctive normal form. Below we add each of the
      // conjuncts of the latter expression to the result.
      builder.addClause({posLit(Var), posLit(LHS), posLit(RHS)});
      builder.addClause({posLit(Var), negLit(LHS), negLit(RHS)});
      builder.addClause({negLit(Var), posLit(LHS), negLit(RHS)});
      builder.addClause({negLit(Var), negLit(LHS), posLit(RHS)});
      break;
    }
    }
    if (builder.isKnownContradictory()) {
      return CNF;
    }
    for (const Formula *Child : F->operands())
      UnprocessedFormulas.push(Child);
  }

  // Unit clauses that were added later were not
  // considered for the simplification of earlier clauses. Do a final
  // pass to find more opportunities for simplification.
  CNFFormula FinalCNF(NextVar - 1);
  CNFFormulaBuilder FinalBuilder(FinalCNF);

  // Collect unit clauses.
  for (ClauseID C = 1; C <= CNF.numClauses(); ++C) {
    if (CNF.clauseSize(C) == 1) {
      FinalBuilder.addClause(CNF.clauseLiterals(C)[0]);
    }
  }

  // Add all clauses that were added previously, preserving the order.
  for (ClauseID C = 1; C <= CNF.numClauses(); ++C) {
    FinalBuilder.addClause(CNF.clauseLiterals(C));
    if (FinalBuilder.isKnownContradictory()) {
      break;
    }
  }
  // It is possible there were new unit clauses again, but
  // we stop here and leave the rest to the solver algorithm.
  return FinalCNF;
}

} // namespace dataflow
} // namespace clang
