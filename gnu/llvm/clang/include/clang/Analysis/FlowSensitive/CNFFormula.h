//===- CNFFormula.h ---------------------------------------------*- C++ -*-===//
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

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_CNFFORMULA_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_CNFFORMULA_H

#include <cstdint>
#include <vector>

#include "clang/Analysis/FlowSensitive/Formula.h"

namespace clang {
namespace dataflow {

/// Boolean variables are represented as positive integers.
using Variable = uint32_t;

/// A null boolean variable is used as a placeholder in various data structures
/// and algorithms.
constexpr Variable NullVar = 0;

/// Literals are represented as positive integers. Specifically, for a boolean
/// variable `V` that is represented as the positive integer `I`, the positive
/// literal `V` is represented as the integer `2*I` and the negative literal
/// `!V` is represented as the integer `2*I+1`.
using Literal = uint32_t;

/// A null literal is used as a placeholder in various data structures and
/// algorithms.
constexpr Literal NullLit = 0;

/// Clause identifiers are represented as positive integers.
using ClauseID = uint32_t;

/// A null clause identifier is used as a placeholder in various data structures
/// and algorithms.
constexpr ClauseID NullClause = 0;

/// Returns the positive literal `V`.
inline constexpr Literal posLit(Variable V) { return 2 * V; }

/// Returns the negative literal `!V`.
inline constexpr Literal negLit(Variable V) { return 2 * V + 1; }

/// Returns whether `L` is a positive literal.
inline constexpr bool isPosLit(Literal L) { return 0 == (L & 1); }

/// Returns whether `L` is a negative literal.
inline constexpr bool isNegLit(Literal L) { return 1 == (L & 1); }

/// Returns the negated literal `!L`.
inline constexpr Literal notLit(Literal L) { return L ^ 1; }

/// Returns the variable of `L`.
inline constexpr Variable var(Literal L) { return L >> 1; }

/// A boolean formula in 3-CNF (conjunctive normal form with at most 3 literals
/// per clause).
class CNFFormula {
  /// `LargestVar` is equal to the largest positive integer that represents a
  /// variable in the formula.
  const Variable LargestVar;

  /// Literals of all clauses in the formula.
  ///
  /// The element at index 0 stands for the literal in the null clause. It is
  /// set to 0 and isn't used. Literals of clauses in the formula start from the
  /// element at index 1.
  ///
  /// For example, for the formula `(L1 v L2) ^ (L2 v L3 v L4)` the elements of
  /// `Clauses` will be `[0, L1, L2, L2, L3, L4]`.
  std::vector<Literal> Clauses;

  /// Start indices of clauses of the formula in `Clauses`.
  ///
  /// The element at index 0 stands for the start index of the null clause. It
  /// is set to 0 and isn't used. Start indices of clauses in the formula start
  /// from the element at index 1.
  ///
  /// For example, for the formula `(L1 v L2) ^ (L2 v L3 v L4)` the elements of
  /// `ClauseStarts` will be `[0, 1, 3]`. Note that the literals of the first
  /// clause always start at index 1. The start index for the literals of the
  /// second clause depends on the size of the first clause and so on.
  std::vector<size_t> ClauseStarts;

  /// Indicates that we already know the formula is unsatisfiable.
  /// During construction, we catch simple cases of conflicting unit-clauses.
  bool KnownContradictory;

public:
  explicit CNFFormula(Variable LargestVar);

  /// Adds the `L1 v ... v Ln` clause to the formula.
  /// Requirements:
  ///
  ///  `Li` must not be `NullLit`.
  ///
  ///  All literals in the input that are not `NullLit` must be distinct.
  void addClause(ArrayRef<Literal> lits);

  /// Returns whether the formula is known to be contradictory.
  /// This is the case if any of the clauses is empty.
  bool knownContradictory() const { return KnownContradictory; }

  /// Returns the largest variable in the formula.
  Variable largestVar() const { return LargestVar; }

  /// Returns the number of clauses in the formula.
  /// Valid clause IDs are in the range [1, `numClauses()`].
  ClauseID numClauses() const { return ClauseStarts.size() - 1; }

  /// Returns the number of literals in clause `C`.
  size_t clauseSize(ClauseID C) const {
    return C == ClauseStarts.size() - 1 ? Clauses.size() - ClauseStarts[C]
                                        : ClauseStarts[C + 1] - ClauseStarts[C];
  }

  /// Returns the literals of clause `C`.
  /// If `knownContradictory()` is false, each clause has at least one literal.
  llvm::ArrayRef<Literal> clauseLiterals(ClauseID C) const {
    size_t S = clauseSize(C);
    if (S == 0)
      return llvm::ArrayRef<Literal>();
    return llvm::ArrayRef<Literal>(&Clauses[ClauseStarts[C]], S);
  }

  /// An iterator over all literals of all clauses in the formula.
  /// The iterator allows mutation of the literal through the `*` operator.
  /// This is to support solvers that mutate the formula during solving.
  class Iterator {
    friend class CNFFormula;
    CNFFormula *CNF;
    size_t Idx;
    Iterator(CNFFormula *CNF, size_t Idx) : CNF(CNF), Idx(Idx) {}

  public:
    Iterator(const Iterator &) = default;
    Iterator &operator=(const Iterator &) = default;

    Iterator &operator++() {
      ++Idx;
      assert(Idx < CNF->Clauses.size() && "Iterator out of bounds");
      return *this;
    }

    Iterator next() const {
      Iterator I = *this;
      ++I;
      return I;
    }

    Literal &operator*() const { return CNF->Clauses[Idx]; }
  };
  friend class Iterator;

  /// Returns an iterator to the first literal of clause `C`.
  Iterator startOfClause(ClauseID C) { return Iterator(this, ClauseStarts[C]); }
};

/// Converts the conjunction of `Vals` into a formula in conjunctive normal
/// form where each clause has at least one and at most three literals.
/// `Atomics` is populated with a mapping from `Variables` to the corresponding
/// `Atom`s for atomic booleans in the input formulas.
CNFFormula buildCNF(const llvm::ArrayRef<const Formula *> &Formulas,
                    llvm::DenseMap<Variable, Atom> &Atomics);

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_CNFFORMULA_H
